#pragma once

#include <array>
#include <juce_core/juce_core.h>
#include "Filters.h"
#include "../Params.h"

// One drum hit. Runs at twice the host sample rate (the slot decimates), mono. Every sound parameter is captured when
// the hit starts, so turning a knob never zips inside a hit; the next hit takes the new value.
//
// Roman (2026-11-12): "only drums - without the synth-like sound we removed from the kicks". So nothing here holds a
// pitch: bodies fall in pitch and are short or low-passed after the attack, metal is inharmonic (squares / FM pairs at
// non-integer ratios), and noise carries snares, claps, hats and cymbals.
namespace pd::dsp
{
inline double shapeDrive (double x, double g, DriveMode m) noexcept
{
    const double v = x * g;
    switch (m)
    {
        case DriveMode::clip:  return juce::jlimit (-1.0, 1.0, v);
        case DriveMode::fold:  return std::sin (juce::jlimit (-8.0, 8.0, v) * pi * 0.5) * 0.8 + 0.2 * std::tanh (v);
        case DriveMode::crush:
        case DriveMode::saturate:
        default:               return std::tanh (v + 0.06) - std::tanh (0.06);
    }
}

// Keeps the peak of a full-scale hit near constant as DRIVE rises (density, not level, grows).
inline double driveNorm (double g, DriveMode m) noexcept
{
    double peak = 0.0;
    for (int i = 1; i <= 64; ++i)
    {
        const double x = 0.9 * i / 64.0;
        peak = std::max ({ peak, std::abs (shapeDrive (x, g, m)), std::abs (shapeDrive (-x, g, m)) });
    }
    return juce::jlimit (0.25, 4.0, 0.9 / std::max (peak, 1.0e-6));
}

struct HitParams
{
    DrumType type = DrumType::kick;
    int model = 0;
    double tune = 0.0, decay = 0.5, tone = 0.5, a = 0.5, b = 0.5;
    double velGain = 1.0;
    unsigned int seed = 1;
    double analogCents = 0.0, analogDecay = 1.0;   // ANALOGUE: this hit's small pitch and length spread
    double drive = 0.0;                            // KICK only: the slot's DRIVE runs inside the voice (before the body tone)
    DriveMode driveMode = DriveMode::saturate;
};

// Length of a hit (to about -60 dB) for DECAY 0..1, per type, in seconds.
inline double decaySeconds (DrumType t, double x) noexcept
{
    static constexpr double lo[numTypes] = { 0.08, 0.05, 0.04, 0.012, 0.25, 0.05, 0.01, 0.12, 0.05 };
    static constexpr double hi[numTypes] = { 1.5, 1.0, 1.5, 2.0, 6.0, 1.5, 1.2, 2.0, 3.0 };
    const int i = juce::jlimit (0, numTypes - 1, (int) t);
    return lo[i] * std::pow (hi[i] / lo[i], juce::jlimit (0.0, 1.0, x));
}

// Base pitch of a type at TUNE 0 (Hz; for hats and cymbals the scale of the metal).
inline double basePitch (DrumType t) noexcept
{
    static constexpr double f[numTypes] = { 50.0, 190.0, 1100.0, 1.0, 1.0, 110.0, 400.0, 50.0, 1000.0 };
    return f[juce::jlimit (0, numTypes - 1, (int) t)];
}

// Pitch at TUNE semitones, kept where the type stays a drum (a kick above 110 Hz turns into a tom, and so on).
inline double tunedPitch (DrumType t, double semis) noexcept
{
    static constexpr double lo[numTypes] = { 28.0, 90.0, 400.0, 0.5, 0.5, 45.0, 80.0, 28.0, 60.0 };
    static constexpr double hi[numTypes] = { 110.0, 500.0, 4000.0, 2.0, 2.0, 600.0, 5000.0, 110.0, 8000.0 };
    const int i = juce::jlimit (0, numTypes - 1, (int) t);
    return juce::jlimit (lo[i], hi[i], basePitch (t) * std::exp2 (semis / 12.0));
}

// Band-limited square (polyBLEP), phase 0..1.
struct BlepSquare
{
    double phase = 0.0, inc = 0.0;
    static double blep (double t, double dt) noexcept
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0; }
        if (t > 1.0 - dt) { t = (t - 1.0) / dt; return t * t + t + t + 1.0; }
        return 0.0;
    }
    double tick() noexcept
    {
        double v = phase < 0.5 ? 1.0 : -1.0;
        v += blep (phase, inc);
        v -= blep (std::fmod (phase + 0.5, 1.0), inc);
        phase += inc;
        phase -= std::floor (phase);
        return v;
    }
};

class DrumVoice
{
public:
    void prepare (double sampleRate2x) { sr = sampleRate2x; noiseNorm = std::sqrt (sr / 96000.0); active = false; }

    void start (const HitParams& p)
    {
        hp = p;
        type = p.type;
        model = juce::jlimit (0, 2, p.model);
        T = decaySeconds (type, p.decay) * p.analogDecay;
        tau = T / 6.9;
        pitch = tunedPitch (type, p.tune) * std::exp2 (p.analogCents / 1200.0);
        vel = p.velGain;
        rng.seed (p.seed);
        jitRng.seed (p.seed ^ 0x6A09E667u);
        for (auto& z : jz) z = { 0.0, 0.0 };
        jitDepth = 0.0; ringNoise = 0.0;
        t = 0.0;
        fade = 1.0; fadeStep = 0.0;
        for (auto& ph : phase) ph = 0.0;
        f1.reset(); f2.reset(); f3.reset(); f4.reset();
        lp1 = lp2 = 0.0; hold = 0.0; holdCount = 0;
        setupType();
        active = true;
    }

    // A short fade (hand-off, choke or note stealing): never cut mid-cycle.
    void release (double seconds) noexcept
    {
        if (active && fadeStep == 0.0)
            fadeStep = 1.0 / std::max (1.0, seconds * sr);
    }

    bool isActive() const noexcept { return active; }
    bool isReleasing() const noexcept { return fadeStep > 0.0; }
    double age() const noexcept { return t; }
    DrumType getType() const noexcept { return type; }

    // One sample at 2x. dry goes straight to the slot; wet feeds the slot's resonator (rumble comb, clap room, impact
    // space) - zero for the other types.
    void tick (double& dry, double& wet) noexcept
    {
        dry = wet = 0.0;
        if (! active)
            return;
        double y = 0.0;
        switch (type)
        {
            case DrumType::kick:   y = kick(); break;
            case DrumType::snare:  y = snare(); break;
            case DrumType::clap:   y = clap (wet); break;
            case DrumType::hat:    y = hat(); break;
            case DrumType::cymbal: y = cymbal(); break;
            case DrumType::tom:    y = tom(); break;
            case DrumType::perc:   y = perc(); break;
            case DrumType::rumble: y = rumble (wet); break;
            case DrumType::fx:     y = fx (wet); break;
        }
        // Output trim per type and model: the default sound peaks about -4 dB, the hottest settings near 0 dB.
        static constexpr double trim[numTypes][3] = { { 0.6, 0.6, 0.6 }, { 0.45, 0.55, 0.6 }, { 0.6, 0.6, 0.6 }, { 0.6, 0.45, 0.6 },
                                                      { 0.6, 0.6, 0.42 }, { 0.6, 0.6, 0.6 }, { 0.6, 0.42, 0.6 }, { 0.6, 0.6, 0.42 }, { 0.6, 0.6, 0.45 } };
        y *= trim[(int) type][model];
        wet *= trim[(int) type][model];
        const double g = vel * (fadeStep > 0.0 ? 0.5 - 0.5 * std::cos (pi * fade) : 1.0); // raised cosine: smooth hand-off
        dry = y * g;
        wet *= g;
        t += 1.0 / sr;
        if (fadeStep > 0.0)
            fade = std::max (0.0, fade - fadeStep);
        if (fade <= 0.0 || t > T + tailExtra)
            active = false;
    }

    double length() const noexcept { return T; }

private:
    static constexpr double twoPi = 2.0 * pi;

    double sine (int i, double freq) noexcept
    {
        phase[(size_t) i] += freq / sr;
        phase[(size_t) i] -= std::floor (phase[(size_t) i]);
        return std::sin (twoPi * phase[(size_t) i]);
    }
    double env() const noexcept { return std::exp (-t / tau); }
    // A 4 ms fade at the end of the hit (to exactly zero at T).
    double endFade() const noexcept { return juce::jlimit (0.0, 1.0, (T - t) / 0.004); }
    double attack (double seconds) const noexcept { return std::min (1.0, t / seconds); }
    double onePole (double& z, double x, double fc) noexcept
    {
        const double k = 1.0 - std::exp (-twoPi * std::min (fc, sr * 0.45) / sr);
        z += k * (x - z);
        return z;
    }
    // White noise of the same level per Hz at any rate (twice the rate spreads the same power over twice the band).
    double white() noexcept { return rng.white() * noiseNorm; }

    void setupType()
    {
        tailExtra = 0.01;
        const double tone = hp.tone, a = hp.a, b = hp.b;
        switch (type)
        {
            case DrumType::kick:
            {
                // HARD: the body holds and then stops (measured hard-techno kicks: tail 1-2 dB under the hit);
                // ROUND: a smooth decay; DIST: held and folded (industrial).
                static constexpr double holdPart[3] = { 0.45, 0.0, 0.4 };
                static constexpr double sat[3] = { 1.8, 1.2, 3.0 };
                static constexpr double falls[3] = { 5.0, 2.0, 6.0 };      // measured: 2-6 semitones down through the tail
                static constexpr double weights[3] = { 0.9, 0.4, 1.0 };    // the 2nd harmonic (the weight around 120 Hz)
                holdT = T * holdPart[model];
                satGain = sat[model];
                satNorm = 1.0 / std::tanh (satGain);
                fall = falls[model];
                weight = weights[model];
                depth = 24.0 + 36.0 * a;              // PUNCH: a deeper, faster sweep and a harder hit
                pTau = 0.024 - 0.016 * a;
                clickGain = 0.9 * std::pow (b, 1.5);
                f1.set (1800.0 + 7000.0 * tone, 0.7, sr); // click noise (high-pass output)
                blipF = 1200.0 + 2600.0 * tone;
                // DRIVE inside the voice, before the body tone (after it, the slot's drive put the mids back: a fit went
                // to 28 Hz to hide them). Then, as PowerKick measured on hard-techno kicks: the attack bright, the body
                // with strong lows, scooped mids around 1 kHz (TONE: -26..-4 dB) and the top "fizz" of the drive kept.
                kDrive = hp.drive;
                kMode = hp.driveMode;
                kGain = std::pow (10.0, 30.0 * kDrive / 20.0);
                kNorm = driveNorm (kGain, kMode);
                protect = 0.75 * std::sqrt (kDrive);
                scoopDb = -(4.0 + 22.0 * (1.0 - tone));
                bell1.reset(); bell2.reset();
                bell2.set (480.0, 0.6, -12.0 * kDrive, sr);   // the drive's "honk" (PowerKick: -14 dB x drive at 480 Hz)
                bellCount = 0; bellDb = 1.0;
                lpA = lpB = 0.0;
                break;
            }
            case DrumType::snare:
            {
                static constexpr double bodyLen[3] = { 0.7, 1.5, 0.6 };
                bodyTau = (0.02 + 0.09 * b) * bodyLen[model] / 2.3;
                bodyGain = (0.35 + 0.65 * b) * (model == 2 ? 0.7 : 1.0);
                noiseGain = 0.35 + 0.85 * a;
                snapGain = a;
                if (model == 1) pitch *= std::exp2 (-3.0 / 12.0);
                f1.set (400.0 + 3200.0 * tone * tone, 0.7, sr);         // noise high-pass
                f2.set (4500.0 + 3000.0 * tone, 1.4, sr);               // snap peak
                f3.set (pitch, 3.5, sr);                                // NOISE model: a noisy body
                lpHz = model == 1 ? 9000.0 : 16000.0;
                break;
            }
            case DrumType::clap:
            {
                static constexpr int counts[3] = { 4, 5, 7 };
                bursts = counts[model];
                spacing = (0.004 + 0.016 * a) * (model == 2 ? 0.6 : 1.0);
                const double q = 2.4 - 1.5 * tone;
                f1.set (pitch, q, sr);
                f2.set (500.0 + 1500.0 * tone, 0.7, sr);   // high-pass after the band
                roomGain = b;
                tailExtra = 0.25;                          // the room rings on in the slot
                break;
            }
            case DrumType::hat:
            {
                static constexpr double ratios[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                for (int i = 0; i < 6; ++i) { baseInc[(size_t) i] = ratios[i] * pitch / sr; sq[(size_t) i] = { 0.0, baseInc[(size_t) i] }; }
                // TONE moves the whole band (measured hats: centroid 4.5-10 kHz, median 6.4 kHz)
                const double bright = std::exp2 ((tone - 0.5) * 2.0);
                f1.set (5200.0 * std::sqrt (bright), 1.3, sr);
                f2.set (8200.0 * std::sqrt (bright), 1.1, sr);
                f3.set (std::min (3600.0 * bright, 12000.0), 0.7, sr);        // high-pass
                f4.set (std::min (3600.0 * bright * 3.4, sr * 0.45), 0.7, sr); // and the top of the band
                jitDepth = 0.03;
                metal = a;
                holdT = b * 0.6 * T;
                tau = std::max (0.002, (T - holdT) / 6.9);
                crushStep = model == 2 ? std::max (1, (int) std::round (sr / 16000.0)) : 1;
                break;
            }
            case DrumType::cymbal:
            {
                // Two sets of squares at inharmonic ratios, their pitch shaken by band-limited noise: the lines of the
                // spectrum smear into bands as on a real cymbal (measured: tail tonality 7-11 dB; plain squares gave 40).
                static constexpr double ratios[6] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
                for (int i = 0; i < 6; ++i)
                {
                    baseInc[(size_t) i] = ratios[i] * 1.31 * pitch / sr;
                    baseInc2[(size_t) i] = ratios[i] * 1.31 * 1.4731 * pitch / sr;
                    sq[(size_t) i] = { 0.0, baseInc[(size_t) i] };
                    sq2[(size_t) i] = { 0.0, baseInc2[(size_t) i] };
                }
                const double bright = std::exp2 ((tone - 0.5) * 2.0);
                f1.set (std::min (6000.0 * bright, 15000.0), 0.8, sr);
                f2.set (std::min (3000.0 * bright, 9000.0), 0.7, sr);            // high-pass
                f4.set (std::min (3000.0 * bright * 4.0, sr * 0.45), 0.7, sr);   // top
                static constexpr double jit[3] = { 0.09, 0.11, 0.13 }, ringN[3] = { 0.5, 0.8, 1.4 };
                jitDepth = jit[model];
                ringNoise = ringN[model];
                metal = a;
                bellGain = 1.0 * b;
                bellTau = (0.08 + 0.3 * b) / 2.3;
                f3.set (std::min (2400.0 * pitch, sr * 0.4), 5.0, sr);
                break;
            }
            case DrumType::tom:
            {
                static constexpr double sweepTau[3] = { 0.02, 0.008, 0.004 };
                static constexpr double mult[3] = { 1.0, 2.0, 1.4 };
                pitch = juce::jlimit (45.0, 900.0, pitch * mult[model]);
                depth = 24.0 * a + (model == 2 ? 12.0 : 0.0);
                pTau = sweepTau[model];
                noiseGain = b;
                jitDepth = 0.03;
                f3.set (pitch * 1.3, 1.5, sr);   // the skin: noise around the body
                f1.set (model == 1 ? 2200.0 : 1500.0, model == 1 ? 1.2 : 0.7, sr);
                bodyLp = 500.0 + 5500.0 * tone * tone;
                break;
            }
            case DrumType::perc:
            {
                depth = 24.0 * b;           // PITCH: the drop at the start
                pTau = model == 1 ? 0.002 : 0.006;
                metal = a;
                lpHz = 1000.0 * std::pow (16.0, tone);
                if (model == 1) // RIM: two short resonances
                {
                    f1.set (std::min (pitch * 4.2, sr * 0.4), 4.5, sr);
                    f2.set (pitch * 1.3, 3.5, sr);
                }
                f3.set (std::min (pitch * 3.0, sr * 0.4), 2.0, sr); // WOOD: knock noise
                if (model == 2) f2.set (std::min (pitch * 1.1, sr * 0.4), 2.5, sr);
                f4.set (std::min (pitch * 2.76, sr * 0.4), 3.0, sr); // METAL: a band of noise under the clank
                jitDepth = model == 0 ? 0.05 : 0.035;
                break;
            }
            case DrumType::rumble:
            {
                depth = 24.0 * a;
                pTau = 0.015;
                punchGain = a;
                tailExtra = 0.05;
                break;
            }
            case DrumType::fx:
            {
                sweepAmt = a;
                grit = b;
                tailExtra = model == 2 ? 0.4 : 0.02;
                if (model == 1) f1.set (pitch, 4.0, sr);
                if (model == 2) f2.set (300.0 + 6000.0 * tone * tone, 0.7, sr);
                break;
            }
        }
    }

    double kick() noexcept
    {
        const double sweep = depth * std::exp (-t / pTau) + 12.0 * hp.a * std::exp (-t / 0.003)
                             + fall * std::exp (-t / std::max (0.04, 0.35 * T));
        const double f = std::min (pitch * std::exp2 (sweep / 12.0), sr * 0.45);
        const double s = sine (0, f);
        double body = std::tanh (satGain * s) * satNorm;
        if (model == 2) body = 0.7 * body + 0.3 * std::sin (1.7 * satGain * s) * std::tanh (0.8 * satGain); // folded
        body = (body + weight * (1.1 * body * body - 0.55)) / (1.0 + 0.45 * weight);                 // weight: 2nd harmonic
        double a;
        if (model == 1)
            a = std::exp (-t / (T / 4.6));
        else if (t < holdT)
            a = 1.0;
        else
            a = std::pow (std::max (0.0, 1.0 - (t - holdT) / (T - holdT)), 1.4);
        a *= attack (0.0004) * endFade();
        // PUNCH high: a loud strike; low: the body swells after it (the median hard-techno kick is 2-3 dB louder at
        // 40-160 ms than in its first 10 ms).
        a *= 1.0 + 0.9 * (hp.a - 0.35) * std::exp (-t / 0.02);
        double x = body * a;
        if (kDrive > 1.0e-4)
        {
            const double clean = x;
            if (kMode == DriveMode::crush)
            {
                const double levels = std::pow (2.0, 11.0 - 9.0 * kDrive);
                x = std::round (x * levels) / levels;
            }
            else // more drive on the attack (+18 dB decaying in ~25 ms): a bright, flat strike, a darker tail
                x = shapeDrive (x, kGain * (1.0 + 7.0 * kDrive * std::exp (-t / 0.025)), kMode) * kNorm;
            // the lows stay the clean sine under the drive (PowerKick: sub protection)
            const double lc = onePole (lpA, clean, 90.0), ld = onePole (lpB, x, 90.0);
            x += (lc - ld) * protect;
            x = bell2.tick (x);
        }
        // After the first 3 ms the mids around 1 kHz go down to TONE: bright attack, no singing body, the fizz stays.
        if (--bellCount <= 0)
        {
            bellCount = 32;
            const double open = t < 0.003 ? 1.0 : std::exp (-(t - 0.003) / 0.012);
            const double target = scoopDb * (1.0 - open);
            if (std::abs (target - bellDb) > 0.05) { bellDb = target; bell1.set (1000.0, 0.7, bellDb, sr); }
        }
        double y = bell1.tick (x) * 0.8;
        // The body darkens (measured: 250 Hz -27, 1 kHz -39 dB under the body at 20-200 ms, darker still after): a
        // low-pass closing from 20 ms down to TONE, so the held body never sings in the mids.
        const double close = t < 0.02 ? 0.0 : 1.0 - std::exp (-(t - 0.02) / 0.03);
        const double fcTail = 16000.0 + ((250.0 + 8000.0 * hp.tone * hp.tone) - 16000.0) * close;
        y = onePole (lp2, onePole (lp1, y, fcTail), fcTail);   // measured body mids (300 Hz-3 kHz): median -30 dB
        if (clickGain > 0.0 && t < 0.03)
        {
            const double e = std::exp (-t / 0.0012) * attack (0.0002);
            const double n = f1.tick (white()).high;
            const double blip = std::sin (twoPi * blipF * t) * std::exp (-t / 0.0025);
            y += clickGain * (1.2 * n + 0.5 * blip) * e;
        }
        return y;
    }

    double snare() noexcept
    {
        const double sw = 5.0 * std::exp (-t / 0.006);
        const double f = pitch * std::exp2 (sw / 12.0);
        double body;
        if (model == 2)
            body = f3.tick (white()).band * 3.0;                       // a noise body: no tone at all
        else
            body = sine (0, f) + 0.55 * sine (1, f * 1.62);
        if (model == 1) body = std::tanh (1.8 * body);
        body *= bodyGain * std::exp (-t / bodyTau) * attack (0.0003);
        const double nz = white();
        const double n = f1.tick (nz).high;
        const double snap = f2.tick (nz).band;
        double noise = noiseGain * n * (0.75 * env() + 0.6 * std::exp (-t / 0.008)) + snapGain * 0.9 * snap * std::exp (-t / 0.012);
        if (model == 2) noise *= 1.0 + 0.5 * sine (2, 3170.0);   // a grain of ring metal (industrial)
        double y = (body + noise * attack (0.0002)) * endFade();
        if (lpHz < 15000.0) y = onePole (lp1, y, lpHz);
        return y * 0.75;
    }

    double clap (double& wet) noexcept
    {
        // Bursts of band-passed noise, then the tail. WIDE spaces them irregularly; DENSE packs many short ones.
        static constexpr double wideGap[6] = { 1.0, 1.35, 0.75, 1.6, 1.1, 1.0 };
        static constexpr double wideLevel[7] = { 0.8, 0.6, 1.0, 0.7, 0.9, 1.0, 1.0 };
        double e = 0.0, start = 0.0;
        for (int i = 0; i < bursts; ++i)
        {
            const bool last = i == bursts - 1;
            if (t >= start)
            {
                const double u = t - start;
                const double lvl = model == 1 ? wideLevel[i] : (last ? 1.0 : 0.85);
                e = last ? lvl * (0.75 * std::exp (-u / 0.006) + 0.6 * std::exp (-u / tau))
                         : lvl * std::exp (-u / (model == 2 ? 0.0022 : 0.0035));
            }
            if (! last) start += spacing * (model == 1 ? wideGap[i] : 1.0);
        }
        e *= attack (0.0002);
        const double n = f2.tick (f1.tick (white()).band).high;
        const double y = n * e * endFade() * 2.4;
        wet = y * roomGain;
        return y;
    }

    // Band-limited noise (about 700 Hz, unit-ish level) that shakes the pitch of metal and skins.
    double jitter (int k) noexcept
    {
        const double n = jitRng.white();
        jz[(size_t) k][0] += 0.09 * (n - jz[(size_t) k][0]);
        jz[(size_t) k][1] += 0.09 * (jz[(size_t) k][0] - jz[(size_t) k][1]);
        return jz[(size_t) k][1] * 4.0;
    }

    double metalSquares (double j = 0.0) noexcept
    {
        double s = 0.0;
        const double m = 1.0 + jitDepth * j;
        for (size_t i = 0; i < sq.size(); ++i) { sq[i].inc = baseInc[i] * m; s += sq[i].tick(); }
        return s / 6.0;
    }

    double hat() noexcept
    {
        double m;
        if (model == 1)
        {
            // BRIGHT: three ring-modulated sine pairs (inharmonic, band-limited by construction) plus the squares.
            static constexpr double r[6] = { 3127.0, 4513.0, 5297.0, 6961.0, 8017.0, 9851.0 };
            m = 0.5 * metalSquares();
            for (int i = 0; i < 3; ++i)
                m += 0.5 * sine (2 * i, r[2 * i] * pitch) * sine (2 * i + 1, r[2 * i + 1] * pitch);
        }
        else
            m = metalSquares (jitter (0));
        const double metalBand = f1.tick (m).band + 0.8 * f2.tick (m).band;
        const double nz = white();
        double x = metal * 2.2 * metalBand + (1.0 - metal) * nz * 0.9 + (model == 1 ? 0.25 * nz : 0.0);
        x = f4.tick (f3.tick (x).high).low * 1.3;
        if (crushStep > 1) // LOFI: held samples and few bits
        {
            if (holdCount++ % crushStep == 0) hold = std::round (x * 24.0) / 24.0;
            x = onePole (lp1, hold, 9000.0);
        }
        const double e = (t < holdT ? 1.0 : std::exp (-(t - holdT) / tau)) * attack (0.0002) * endFade();
        return x * e * 1.1;
    }

    double cymbal() noexcept
    {
        double m = metalSquares (jitter (0));
        const double m2 = 1.0 + jitDepth * jitter (1);
        double s2 = 0.0;
        for (size_t i = 0; i < sq2.size(); ++i) { sq2[i].inc = baseInc2[i] * m2; s2 += sq2[i].tick(); }
        m += 0.8 * s2 / 6.0;
        const double nz = white();
        m *= 1.0 + ringNoise * nz;                         // CHINA most: trashy ring modulation by noise
        const double band = f1.tick (m).band;
        double x = metal * 1.8 * band + (1.0 - metal) * nz * 0.8;
        x = f4.tick (f2.tick (x).high).low * 1.2;
        double e = env();
        if (model == 1) // CRASH: a burst of noise first, the metal swells in and decays longer
            e = 0.7 * env() + 0.5 * std::exp (-t / 0.02) * (1.0 - metal * 0.5);
        double y = x * e * attack (model == 1 ? 0.002 : 0.0003);
        if (bellGain > 0.0) // BELL: the ping of a ride - a narrow band of the metal itself, not a sine (a sine sang: 40 dB)
            y += bellGain * f3.tick (m).band * std::exp (-t / bellTau) * attack (0.0003);
        return y * endFade() * 0.9;
    }

    double tom() noexcept
    {
        // Two modes of a skin (1 : 1.59) whose pitch is shaken a little, plus noise around the body: a drum, not a note.
        const double j = 1.0 + jitDepth * jitter (0);
        const double f = std::min (pitch * std::exp2 (depth * std::exp (-t / pTau) / 12.0), sr * 0.45) * j;
        const double modes = sine (0, f) + 0.45 * sine (1, f * 1.593) * std::exp (-t / std::max (0.01, 0.4 * tau));
        const double body = std::tanh (1.6 * modes) / std::tanh (1.6);
        // The pitched body is short; DECAY lengthens the skin noise around it (measured percussion: a short tone, a noisy
        // ring - tail tonality median 9 dB, where a long sine body gave 25-40).
        const double tauTone = std::min (tau, 0.015 + 0.3 * tau);
        const double a = std::exp (-t / tauTone) * attack (0.0004) * endFade();
        const double open = t < 0.003 ? 1.0 : std::exp (-(t - 0.003) / 0.01);
        const double fc = bodyLp + (14000.0 - bodyLp) * open;
        double y = onePole (lp1, body * a, fc) * 0.85;
        y += 0.6 * f3.tick (white()).band * env() * attack (0.0004) * endFade();
        const double n = model == 1 ? f1.tick (white()).band * 2.0 : f1.tick (white()).high;
        y += noiseGain * n * (std::exp (-t / 0.008) + 0.15 * env()) * attack (0.0002) * endFade();
        return y;
    }

    double perc() noexcept
    {
        const double sw = depth * std::exp (-t / pTau);
        const double f = std::min (pitch * std::exp2 (sw / 12.0), sr * 0.4);
        double y = 0.0;
        const double e = env() * attack (0.0002) * endFade();
        const double eTone = std::exp (-t / std::min (tau, 0.012 + 0.3 * tau)) * attack (0.0002) * endFade(); // the pitched part is short
        switch (model)
        {
            case 0: // METAL: a clank - FM at an inharmonic ratio plus a ring-modulated partial
            {
                const double j = 1.0 + jitDepth * jitter (0);
                const double index = (1.0 + 9.0 * metal) * std::exp (-t / std::max (0.004, 0.3 * T));
                const double mod = sine (1, f * 1.4142 * j);
                phase[0] += f * j / sr; phase[0] -= std::floor (phase[0]);
                const double car = std::sin (twoPi * phase[0] + index * mod);
                const double ring = car * sine (2, f * 2.7613 * (1.0 + jitDepth * jitter (1)));
                y = (0.8 * car + metal * 0.6 * ring) * eTone;
                y += 0.6 * f4.tick (white() * (1.0 + 0.5 * ring)).band * e;   // a noisy ring under the clank carries DECAY
                y += white() * std::exp (-t / 0.0015) * 0.5;   // the strike
                break;
            }
            case 1: // RIM: a strike through two short resonances
            {
                const double strike = white() * std::exp (-t / 0.0008) + (t < 0.0002 ? 1.0 : 0.0);
                y = (f1.tick (strike).band * 2.4 + f2.tick (strike).band * 1.8) * std::exp (-t / std::min (tau, 0.03));
                y += metal * 0.5 * sine (0, f * 2.17) * sine (1, f * 3.41) * e * std::exp (-t / 0.025);
                y += 0.4 * f3.tick (white()).band * std::exp (-t / 0.004);
                y *= endFade();
                break;
            }
            default: // WOOD: a knock - a falling sine with a short band of noise
            {
                const double j = 1.0 + jitDepth * jitter (0);
                y = (sine (0, f * j) + 0.5 * sine (3, f * 1.593 * j) * std::exp (-t / std::max (0.004, 0.4 * tau))) * eTone;
                if (metal > 0.0) y += metal * 0.6 * sine (1, f * 2.7613 * j) * sine (2, f * 0.73) * eTone;
                y += 0.8 * f3.tick (white()).band * std::exp (-t / 0.003) * endFade();
                y += 0.5 * f2.tick (white()).band * e;          // the hollow ring of the wood carries DECAY
                break;
            }
        }
        return onePole (lp1, y, lpHz);
    }

    double rumble (double& wet) noexcept
    {
        // The source: a short kick-like thump; the slot's resonator smears, filters and drives it into the rumble.
        const double f = std::min (pitch * std::exp2 (depth * std::exp (-t / pTau) / 12.0), sr * 0.45);
        double src = 0.0;
        if (model == 2) // GROWL: the low sine wobbled by slow noise, saturated
        {
            const double wob = onePole (lp2, white(), 25.0) * 60.0;
            src = std::tanh (3.0 * sine (0, std::max (20.0, f + wob * pitch * 0.02)));
        }
        else
            src = sine (0, f);
        const double thump = src * std::exp (-t / 0.05) * attack (0.0005);
        wet = (model == 2 ? src * 0.6 : thump) + 0.25 * onePole (lp1, white(), 400.0) * std::exp (-t / 0.03);
        return punchGain * thump * 0.6 * endFade();
    }

    double fx (double& wet) noexcept
    {
        double y = 0.0;
        const double e = env() * attack (0.0005) * endFade();
        switch (model)
        {
            case 0: // ZAP: a fast falling sine
            {
                const double top = pitch * std::exp2 (5.0 * sweepAmt);
                const double f = pitch * 0.25 + (top - pitch * 0.25) * std::exp (-t / std::max (0.002, 0.15 * T));
                y = sine (0, std::min (f, sr * 0.45)) * e;
                if (grit > 0.0) y *= 1.0 - grit + grit * (white() > 0.0 ? 1.0 : -1.0);   // ring-modulated by noise
                break;
            }
            case 1: // SWEEP: resonant noise, up (SWEEP < 50 %) or down
            {
                const double dir = sweepAmt - 0.5, oct = 4.0 * std::abs (dir) * 2.0;
                const double u = juce::jlimit (0.0, 1.0, t / T);
                const double f = pitch * std::exp2 (dir < 0.0 ? oct * (u - 0.5) : oct * (0.5 - u));
                f1.set (juce::jlimit (40.0, sr * 0.4, f), 4.0, sr);
                const double shape = dir < 0.0 ? std::pow (u, 1.5) * juce::jlimit (0.0, 1.0, (T - t) / 0.02) : e;
                y = f1.tick (white()).band * 1.8 * shape;
                if (grit > 0.0) y = (1.0 - grit) * y + grit * std::tanh (4.0 * y);
                break;
            }
            default: // IMPACT: a low boom with a burst of noise; the slot's space rings it out
            {
                const double f = std::min (45.0 * std::exp2 (hp.tune / 24.0) * std::exp2 (24.0 * std::exp (-t / 0.03) / 12.0), sr * 0.45);
                const double boom = sine (0, f) * e;
                const double n = f2.tick (white()).low * std::exp (-t / std::max (0.01, 0.35 * tau)) * attack (0.0003);
                y = 0.8 * boom + 0.9 * n;
                if (grit > 0.0) y = std::tanh ((1.0 + 6.0 * grit) * y) / std::tanh (1.0 + 6.0 * grit) * (1.0 + grit);
                wet = n * 0.8;
                break;
            }
        }
        return y;
    }

    double sr = 96000.0, noiseNorm = 1.0, t = 0.0, T = 0.4, tau = 0.06, tailExtra = 0.01;
    double pitch = 50.0, vel = 1.0, fade = 1.0, fadeStep = 0.0;
    DrumType type = DrumType::kick;
    int model = 0;
    bool active = false;
    HitParams hp;
    std::array<double, 8> phase {};
    std::array<BlepSquare, 6> sq {}, sq2 {};
    std::array<double, 6> baseInc {}, baseInc2 {};
    Rng jitRng;
    std::array<std::array<double, 2>, 3> jz {};
    double jitDepth = 0.0, ringNoise = 0.0;
    Svf f1, f2, f3, f4;
    Rng rng;
    double lp1 = 0.0, lp2 = 0.0, hold = 0.0;
    int holdCount = 0, crushStep = 1, bursts = 4;
    // per-type settings (setupType)
    double holdT = 0.0, satGain = 2.0, satNorm = 1.0, fall = 0.0, depth = 24.0, pTau = 0.02, clickGain = 0.0, blipF = 2000.0;
    double bodyLp = 2000.0, bodyTau = 0.03, bodyGain = 1.0, noiseGain = 1.0, snapGain = 0.5, lpHz = 16000.0;
    double weight = 0.0, kDrive = 0.0, kGain = 1.0, kNorm = 1.0, protect = 0.0, scoopDb = -15.0, bellDb = 1.0, lpA = 0.0, lpB = 0.0;
    DriveMode kMode = DriveMode::saturate;
    SvfBell bell1, bell2;
    int bellCount = 0;
    double spacing = 0.01, roomGain = 0.0, metal = 0.5, bellGain = 0.0, bellTau = 0.1, punchGain = 0.5, sweepAmt = 0.5, grit = 0.0;
};
}
