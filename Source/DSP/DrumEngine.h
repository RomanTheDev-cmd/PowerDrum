#pragma once

#include <atomic>
#include <juce_audio_basics/juce_audio_basics.h>
#include "DrumVoice.h"
#include "AnalogCharacter.h"

// PowerDrum signal path, per slot (at twice the sample rate up to the decimator):
//   voices (up to 4; kick, tom, rumble and hat are one voice with a 1.5 ms hand-off) -> resonator (rumble smear, clap
//   room, impact space) -> DRIVE (SAT / CLIP / FOLD / CRUSH) -> FILTER (one knob: low-pass left, high-pass right)
//   -> half-band decimator -> WIDTH (decorrelated side above 250 Hz: the mono sum stays the dry slot) -> PAN -> LEVEL
//   -> its output (MAIN or OUT 1..16) and the post-fader REVERB and DELAY sends.
// MAIN: the slots routed to it + reverb and delay returns -> GLUE (bus compressor) -> master DRIVE.
// VOLUME and the emergency ceiling apply to every output.
namespace pd::dsp
{
inline double dbToGain (double db) noexcept { return std::pow (10.0, db / 20.0); }

// Half-band FIR decimator (97 taps, Kaiser beta 8): about -80 dB above 26 kHz at 48 kHz. Group delay 48 samples at 2x
// = 24 at the base rate (the plug-in latency). The same filter PowerSynth 3.1 uses.
struct Decimator2x
{
    static constexpr int taps = 97, latencyBase = 24;
    std::array<double, 25> hOdd {};   // the non-zero taps of one half (odd offsets from the centre)
    std::array<double, 128> hist {};
    int pos = 0;
    Decimator2x()
    {
        auto i0 = [] (double x) { double s = 1.0, t = 1.0; for (int k = 1; k < 30; ++k) { t *= (x / (2.0 * k)) * (x / (2.0 * k)); s += t; } return s; };
        const double beta = 8.0, m = (taps - 1) / 2.0;
        std::array<double, taps> h {};
        double sum = 0.0;
        for (int k = 0; k < taps; ++k)
        {
            const double d = k - m, w = i0 (beta * std::sqrt (std::max (0.0, 1.0 - (d / m) * (d / m)))) / i0 (beta);
            h[(size_t) k] = (d == 0.0 ? 0.5 : std::sin (pi * 0.5 * d) / (pi * d)) * w;
            sum += h[(size_t) k];
        }
        for (auto& v : h) v /= sum;
        centre = h[48];
        for (int j = 0; j < 24; ++j) hOdd[(size_t) j] = h[(size_t) (48 - (2 * j + 1))];   // symmetric: h[48-d] = h[48+d]
    }
    void reset() { hist.fill (0.0); pos = 0; }
    // Two samples at 2x in, one at the base rate out.
    double tick (double x0, double x1) noexcept
    {
        pos = (pos + 1) & 127; hist[(size_t) pos] = x0;
        pos = (pos + 1) & 127; hist[(size_t) pos] = x1;
        // newest sample is at offset 0; the centre tap sits 48 samples back
        double acc = centre * hist[(size_t) ((pos - 48) & 127)];
        for (int j = 0; j < 24; ++j)
        {
            const int d = 2 * j + 1;
            acc += hOdd[(size_t) j] * (hist[(size_t) ((pos - 48 + d) & 127)] + hist[(size_t) ((pos - 48 - d) & 127)]);
        }
        return acc;
    }
    double centre = 0.5;
};

//==============================================================================
// The slot's resonator, at 2x: RUMBLE (comb smear / reverb smear / growl, low-passed, driven, gated per hit), CLAP room,
// IMPACT space. Settings are taken at each hit.
class Resonator
{
public:
    void prepare (double sampleRate2x)
    {
        sr = sampleRate2x;
        for (int i = 0; i < 4; ++i) lines[(size_t) i].prepare ((int) (0.09 * sr) + 8);
        for (int i = 0; i < 3; ++i) ap[(size_t) i].prepare ((int) (0.025 * sr) + 8);
        hpLow.highpass (30.0, 0.7, sr);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& a : ap) a.reset();
        for (auto& d : damp) d = 0.0;
        lp.reset(); hpLow.reset(); lpOut = 0.0;
        gate = 0.0; gateT = 1.0e9; mode = Mode::none;
    }

    void start (const HitParams& p, double length)
    {
        Mode m = Mode::none;
        if (p.type == DrumType::rumble) m = p.model == 0 ? Mode::comb : (p.model == 1 ? Mode::verb : Mode::growl);
        else if (p.type == DrumType::clap) m = Mode::room;
        else if (p.type == DrumType::fx && p.model == 2) m = Mode::space;
        if (m != mode) { reset(); mode = m; }
        if (mode == Mode::none) return;
        static constexpr double combMs[4] = { 29.7, 37.1, 41.1, 43.7 }, verbMs[4] = { 53.7, 61.3, 71.1, 79.9 };
        static constexpr double roomMs[4] = { 11.3, 17.9, 23.1, 31.7 }, spaceMs[4] = { 41.3, 53.9, 67.1, 83.3 };
        const double* ms = mode == Mode::verb ? verbMs : (mode == Mode::room ? roomMs : (mode == Mode::space ? spaceMs : combMs));
        for (int i = 0; i < 4; ++i) len[(size_t) i] = ms[i] * 0.001 * sr;
        static constexpr double apMs[3] = { 5.1, 7.3, 1.7 };
        for (int i = 0; i < 3; ++i) apLen[(size_t) i] = apMs[i] * 0.001 * sr;
        switch (mode)
        {
            case Mode::comb: case Mode::verb: case Mode::growl:
                fb = 0.55 + 0.4 * p.b;                               // DENSITY
                dampCoef = std::exp (-2.0 * pi * 320.0 / sr);
                lp.set (70.0 * std::pow (6.0, p.tone), 0.8, sr);     // TONE: 70..420 Hz
                outGain = mode == Mode::growl ? 0.9 : 1.0;
                gateLen = length;
                gateT = 0.0;
                break;
            case Mode::room:
                fb = 0.45 + 0.3 * p.b;
                dampCoef = std::exp (-2.0 * pi * 4000.0 / sr);
                outGain = 0.9;
                break;
            case Mode::space:
                fb = 0.7 + 0.2 * p.decay;
                dampCoef = std::exp (-2.0 * pi * 2500.0 / sr);
                outGain = 0.7;
                break;
            case Mode::none: break;
        }
    }

    bool isIdle() const noexcept { return mode == Mode::none; }

    double tick (double in) noexcept
    {
        if (mode == Mode::none) return 0.0;
        double x = in;
        if (mode == Mode::verb || mode == Mode::room || mode == Mode::space) // diffuse first: no comb periodicity
            for (int k = 0; k < 3; ++k)
            {
                const double delayed = ap[(size_t) k].read (apLen[(size_t) k] - 1.0);
                const double v = x + 0.6 * delayed;
                ap[(size_t) k].push (v);
                x = delayed - 0.6 * v;
            }
        double sum = 0.0;
        if (mode != Mode::growl)
            for (int i = 0; i < 4; ++i)
            {
                const double y = lines[(size_t) i].read (len[(size_t) i] - 1.0);
                damp[(size_t) i] = y + dampCoef * (damp[(size_t) i] - y);
                lines[(size_t) i].push (x + damp[(size_t) i] * fb * ((i & 1) == 0 || mode == Mode::comb ? 1.0 : -1.0));
                sum += y;
            }
        else
            sum = x * 3.0;
        if (mode == Mode::room || mode == Mode::space)
            return sum * 0.35 * outGain;
        // RUMBLE: low-passed, driven, gated by the hit.
        double r = lp.tick (sum * 0.3).low;
        r = std::tanh (2.5 * r);
        r = hpLow.tick (r);
        double g = 0.0;
        if (gateT < 1.0e8)
        {
            const double hold = 0.6 * gateLen;
            g = gateT < hold ? std::min (1.0, gateT / 0.008) : std::max (0.0, 1.0 - (gateT - hold) / (gateLen - hold));
            g = g * g;
            gateT += 1.0 / sr;
        }
        gate += (g - gate) * 0.002;   // no clicks when a new hit restarts the gate
        return r * gate * 1.4 * outGain;
    }

private:
    enum class Mode { none, comb, verb, growl, room, space };
    Mode mode = Mode::none;
    double sr = 96000.0, fb = 0.7, dampCoef = 0.9, outGain = 1.0, gate = 0.0, gateT = 1.0e9, gateLen = 0.4, lpOut = 0.0;
    std::array<DelayLine, 4> lines;
    std::array<DelayLine, 3> ap;
    std::array<double, 4> len {}, damp {};
    std::array<double, 3> apLen {};
    Svf lp;
    Biquad hpLow;
};

//==============================================================================
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

//==============================================================================
class Slot
{
public:
    static constexpr int maxVoices = 4;

    void prepare (double sampleRate, int slotIndex)
    {
        sr = sampleRate;
        index = slotIndex;
        for (auto& v : voices) v.prepare (2.0 * sr);
        resonator.prepare (2.0 * sr);
        static constexpr double apMs[3] = { 1.31, 2.87, 4.73 };
        for (int i = 0; i < 3; ++i) { apLen[(size_t) i] = apMs[i] * 0.001 * sr; ap[(size_t) i].prepare ((int) apLen[(size_t) i] + 8); }
        sideHp.highpass (250.0, 0.7, sr);
        dcR = std::exp (-2.0 * pi * 4.0 / sr);   // 4 Hz at any rate
        highRate = sr > 50000.0;
        band1.set (21000.0, 0.54, 2.0 * sr); band2.set (21000.0, 1.31, 2.0 * sr);
        reset();
    }

    void reset()
    {
        for (auto& v : voices) v = DrumVoice(), v.prepare (2.0 * sr);
        resonator.reset();
        decim.reset();
        filt.reset(); band1.reset(); band2.reset();
        for (auto& a : ap) a.reset();
        sideHp.reset();
        cs = 0;
        idleSamples = 1 << 30;
        initialised = false;
        crushHold = 0.0; crushCount = 0;
        dcX = dcY = 0.0;
        level.store (0.0f);
    }

    void noteOn (const SlotParams& p, float velocity, unsigned int seed, double analogCents, double analogDecay, double analogLevel)
    {
        HitParams h;
        h.type = p.type; h.model = p.model; h.tune = p.tune; h.decay = p.decay; h.tone = p.tone; h.a = p.a; h.b = p.b;
        const double v = juce::jlimit (0.0, 1.0, (double) velocity);
        h.velGain = std::pow (std::max (v, 0.001), 2.0 * p.vel) * analogLevel;
        h.seed = seed;
        h.analogCents = analogCents;
        h.analogDecay = analogDecay;
        const bool mono = p.type == DrumType::kick || p.type == DrumType::tom || p.type == DrumType::rumble || p.type == DrumType::hat;
        DrumVoice* target = nullptr;
        if (mono)
        {
            for (auto& vc : voices) vc.release (0.0015);
            for (auto& vc : voices) if (! vc.isActive()) { target = &vc; break; }
        }
        else
        {
            for (auto& vc : voices) if (! vc.isActive()) { target = &vc; break; }
            if (target == nullptr) // steal the oldest
            {
                DrumVoice* oldest = &voices[0];
                for (auto& vc : voices) if (vc.age() > oldest->age()) oldest = &vc;
                oldest->release (0.003);
            }
            for (auto& vc : voices) if (! vc.isActive()) { target = &vc; break; }
        }
        if (target == nullptr) // every voice still fading: take the quietest-to-be (the oldest)
        {
            target = &voices[0];
            for (auto& vc : voices) if (vc.age() > target->age()) target = &vc;
        }
        target->start (h);
        resonator.start (h, target->length());
        idleSamples = 0;
        hitFlash.store (1.0f);
    }

    void choke (double seconds = 0.006) noexcept { for (auto& v : voices) v.release (seconds); }

    bool isIdle() const noexcept { return idleSamples > (int) (0.3 * sr); }

    // Per block: smoothed mix targets.
    void setBlock (const SlotParams& p, bool audible)
    {
        const double g = audible ? dbToGain (p.levelDb + p.trimDb) * (p.levelDb <= -59.9f ? 0.0 : 1.0) : 0.0;
        const double panA = (juce::jlimit (-1.0, 1.0, (double) p.pan) + 1.0) * pi * 0.25;
        const double gl = g * std::sqrt (2.0) * std::cos (panA), gr = g * std::sqrt (2.0) * std::sin (panA);
        const double drv = juce::jlimit (0.0, 1.0, (double) p.drive);
        // filter: -1..0 low-pass 200 Hz..20 kHz, 0..1 high-pass 20 Hz..4 kHz (log), off near 0
        const double f = p.filter;
        const double fHz = f < 0.0 ? 20000.0 * std::pow (0.01, -f) : 20.0 * std::pow (200.0, f);
        const int ramp = (int) (0.02 * sr);
        if (! initialised)
        {
            gainL.reset (gl); gainR.reset (gr); driveR.reset (drv); widthR.reset (p.width);
            revR.reset (p.rev); dlyR.reset (p.dly); logF = std::log (fHz);
            initialised = true;
        }
        gainL.setTarget (gl, ramp); gainR.setTarget (gr, ramp); driveR.setTarget (drv, ramp);
        widthR.setTarget (p.width, ramp); revR.setTarget (p.rev, ramp); dlyR.setTarget (p.dly, ramp);
        targetLogF = std::log (fHz);
        filterMode = std::abs (f) < 0.02 ? 0 : (f < 0.0 ? 1 : 2);
        driveMode = p.driveMode;
    }

    // One base-rate sample: the slot's stereo output (post fader) and its sends.
    void tick (double& l, double& r, double& sendRev, double& sendDly) noexcept
    {
        l = r = sendRev = sendDly = 0.0;
        // Smoothed values advance even when idle, so a slot that wakes up starts from the current settings.
        const double gL = gainL.next(), gR = gainR.next(), drv = driveR.next(), w = widthR.next(), sRev = revR.next(), sDly = dlyR.next();
        if (--cs <= 0)
        {
            cs = 16;
            logF += (targetLogF - logF) * (1.0 - std::exp (-16.0 / (0.02 * sr)));
            filt.set (std::exp (logF), 0.75, 2.0 * sr);
            if (std::abs (drv - lastDrive) > 1.0e-4 || driveMode != lastMode)
            {
                lastDrive = drv; lastMode = driveMode;
                driveGain = dbToGain (30.0 * drv);
                norm = driveNorm (driveGain, driveMode);
            }
        }
        if (isIdle()) { ++idleSamples; return; }

        double x2[2];
        bool any = false;
        for (int k = 0; k < 2; ++k)
        {
            double dry = 0.0, wet = 0.0;
            for (auto& v : voices)
                if (v.isActive())
                {
                    double d, w2;
                    v.tick (d, w2);
                    dry += d; wet += w2;
                    any = true;
                }
            double x = dry + resonator.tick (wet);
            if (drv > 1.0e-4)
            {
                if (driveMode == DriveMode::crush) // CRUSH: fewer bits and held samples
                {
                    const double levels = std::pow (2.0, 11.0 - 9.0 * drv);
                    const int step = 1 + (int) (drv * 7.0);
                    if (crushCount++ % step == 0) crushHold = std::round (x * levels) / levels;
                    x = crushHold * (1.0 + drv);
                }
                else
                    x = shapeDrive (x, driveGain, driveMode) * norm;
            }
            if (filterMode != 0)
            {
                const auto o = filt.tick (x);
                x = filterMode == 1 ? o.low : o.high;
            }
            if (highRate) // above 48 kHz: the same audible band as at 44.1/48 kHz (no extra ultrasonic noise in the level)
                x = band2.tick (band1.tick (x).low).low;
            x2[k] = x;
        }
        double m = decim.tick (x2[0], x2[1]);
        // DC blocker (asymmetric drive)
        const double dcOut = m - dcX + dcR * dcY;
        dcX = m; dcY = dcOut;
        m = dcOut;

        // WIDTH: a decorrelated copy of the highs as the side signal; L + R is always 2 * mid.
        double side = 0.0;
        if (w > 1.0e-4)
        {
            double d = sideHp.tick (m);
            for (int i = 0; i < 3; ++i)
            {
                const double delayed = ap[(size_t) i].read (apLen[(size_t) i] - 1.0);
                const double v = d + 0.6 * delayed;
                ap[(size_t) i].push (v);
                d = delayed - 0.6 * v;
            }
            side = w * 0.9 * d;
        }
        l = (m + side) * gL;
        r = (m - side) * gR;
        sendRev = sRev;
        sendDly = sDly;

        const double a = std::max (std::abs (l), std::abs (r));
        if (any || a > 1.0e-6) idleSamples = 0; else ++idleSamples;
        const float lv = level.load (std::memory_order_relaxed);
        level.store (std::max ((float) a, lv * 0.9995f), std::memory_order_relaxed);
    }

    std::atomic<float> hitFlash { 0.0f }, level { 0.0f };   // for the pads (the editor fades them)

private:
    double sr = 48000.0;
    int index = 0;
    std::array<DrumVoice, maxVoices> voices;
    Resonator resonator;
    Decimator2x decim;
    Svf filt, band1, band2;
    bool highRate = false;
    std::array<DelayLine, 3> ap;
    std::array<double, 3> apLen {};
    Biquad sideHp;
    Ramp gainL, gainR, driveR, widthR, revR, dlyR;
    double logF = std::log (20000.0), targetLogF = std::log (20000.0);
    int filterMode = 0, cs = 0, idleSamples = 1 << 30, crushCount = 0;
    double driveGain = 1.0, norm = 1.0, lastDrive = -1.0, crushHold = 0.0, dcX = 0.0, dcY = 0.0, dcR = 0.9995;
    DriveMode driveMode = DriveMode::saturate, lastMode = DriveMode::saturate;
    bool initialised = false;
};

//==============================================================================
// Stereo FDN reverb (8 lines, Hadamard feedback, damping in the loop), high-passed input so the lows stay dry.
class Reverb
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        static constexpr double ms[8] = { 29.3, 33.7, 37.9, 41.3, 45.7, 50.9, 55.3, 61.1 };
        for (int i = 0; i < 8; ++i) { baseLen[(size_t) i] = ms[i] * 0.001 * sr; lines[(size_t) i].prepare ((int) (baseLen[(size_t) i] * 2.2) + 8); }
        static constexpr double dms[4] = { 4.1, 5.9, 7.7, 9.3 };
        for (int i = 0; i < 4; ++i) { difLen[(size_t) i] = dms[i] * 0.001 * sr; dif[(size_t) i].prepare ((int) difLen[(size_t) i] + 8); }
        pre.prepare ((int) (0.012 * sr) + 8);
        hpIn.highpass (180.0, 0.7, sr);
        reset();
    }
    void reset()
    {
        for (auto& l : lines) l.reset();
        for (auto& d : dif) d.reset();
        pre.reset(); hpIn.reset();
        damp.fill (0.0);
        quiet = 1 << 30;
    }
    void set (double size, double dampAmount)
    {
        const double scale = 0.6 + 1.5 * size;
        for (int i = 0; i < 8; ++i) len[(size_t) i] = baseLen[(size_t) i] * scale;
        const double rt60 = 0.35 + 3.2 * size * size;
        for (int i = 0; i < 8; ++i) g[(size_t) i] = std::pow (10.0, -3.0 * (len[(size_t) i] / sr) / rt60);
        dampCoef = std::exp (-2.0 * pi * (12000.0 * std::pow (0.12, dampAmount)) / sr);
    }
    void tick (double inL, double inR, double& outL, double& outR) noexcept
    {
        double x = 0.5 * (inL + inR);
        if (std::abs (x) > 1.0e-9) quiet = 0; else if (quiet < (1 << 30)) ++quiet;
        if (quiet > (int) (8.0 * sr)) { outL = outR = 0.0; return; }
        pre.push (hpIn.tick (x));
        x = pre.read (0.010 * sr);
        for (int k = 0; k < 4; ++k)
        {
            const double delayed = dif[(size_t) k].read (difLen[(size_t) k] - 1.0);
            const double v = x + 0.62 * delayed;
            dif[(size_t) k].push (v);
            x = delayed - 0.62 * v;
        }
        std::array<double, 8> y;
        for (int i = 0; i < 8; ++i)
        {
            const double v = lines[(size_t) i].read (len[(size_t) i] - 1.0);
            damp[(size_t) i] = v + dampCoef * (damp[(size_t) i] - v);
            y[(size_t) i] = damp[(size_t) i] * g[(size_t) i];
        }
        // Hadamard 8x8 (fast, normalised)
        std::array<double, 8> h = y;
        for (int s = 1; s < 8; s <<= 1)
            for (int i = 0; i < 8; i += 2 * s)
                for (int j = i; j < i + s; ++j) { const double a = h[(size_t) j], b = h[(size_t) (j + s)]; h[(size_t) j] = a + b; h[(size_t) (j + s)] = a - b; }
        for (int i = 0; i < 8; ++i)
            lines[(size_t) i].push (h[(size_t) i] * 0.35355339059327373 + x * (i < 4 ? 0.5 : -0.5));
        outL = 0.6 * (y[0] + y[2] + y[4] + y[6]);
        outR = 0.6 * (y[1] + y[3] + y[5] + y[7]);
    }
private:
    double sr = 48000.0, dampCoef = 0.5;
    std::array<DelayLine, 8> lines;
    std::array<DelayLine, 4> dif;
    DelayLine pre;
    std::array<double, 8> baseLen {}, len {}, g {}, damp {};
    std::array<double, 4> difLen {};
    Biquad hpIn;
    int quiet = 1 << 30;
};

// Ping-pong delay at the host tempo, darker and thinner repeats.
class Delay
{
public:
    void prepare (double sampleRate)
    {
        sr = sampleRate;
        l.prepare ((int) (sr * 2.4) + 8); r.prepare ((int) (sr * 2.4) + 8);
        hp.highpass (250.0, 0.7, sr);
        reset();
    }
    void reset() { l.reset(); r.reset(); lpL = lpR = 0.0; hp.reset(); time.reset (0.25 * sr); first = true; quiet = 1 << 30; }
    void set (double beats, double bpm, double feedback)
    {
        const double target = juce::jlimit (16.0, sr * 2.3, beats * 60.0 / juce::jlimit (40.0, 300.0, bpm) * sr);
        if (first) { time.reset (target); first = false; }
        time.setTarget (target, (int) (0.05 * sr));
        fb = feedback;
    }
    void tick (double inL, double inR, double& outL, double& outR) noexcept
    {
        const double in = 0.5 * (inL + inR);
        if (std::abs (in) > 1.0e-9) quiet = 0; else if (quiet < (1 << 30)) ++quiet;
        const double d = time.next();
        if (quiet > (int) (12.0 * sr)) { outL = outR = 0.0; l.push (0.0); r.push (0.0); return; }
        const double yl = l.read (d), yr = r.read (d);
        const double k = 1.0 - std::exp (-2.0 * pi * 5000.0 / sr);
        lpL += k * (yl - lpL); lpR += k * (yr - lpR);
        l.push (hp.tick (in) + lpR * fb);   // ping-pong: left feeds right and back
        r.push (lpL * fb);
        outL = yl; outR = yr;
    }
private:
    double sr = 48000.0, fb = 0.35, lpL = 0.0, lpR = 0.0;
    DelayLine l, r;
    Biquad hp;
    Ramp time;
    bool first = true;
    int quiet = 1 << 30;
};

// GLUE: a linked bus compressor on MAIN (feed-forward, soft knee), one knob: threshold, ratio and makeup together.
class Glue
{
public:
    void prepare (double sampleRate) { sr = sampleRate; att = std::exp (-1.0 / (0.010 * sr)); rel = std::exp (-1.0 / (0.12 * sr)); reset(); }
    void reset() { env = 0.0; }
    void tick (double& l, double& r, double amount) noexcept
    {
        if (amount < 1.0e-4) { env = 0.0; return; }
        const double thr = -6.0 - 18.0 * amount, ratio = 1.0 + 3.0 * amount, knee = 6.0;
        const double x = std::max (std::abs (l), std::abs (r));
        const double xdb = 20.0 * std::log10 (std::max (x, 1.0e-9));
        const double over = xdb - thr;
        double gr = 0.0;
        if (over > knee * 0.5) gr = over * (1.0 - 1.0 / ratio);
        else if (over > -knee * 0.5) { const double u = over + knee * 0.5; gr = (1.0 - 1.0 / ratio) * u * u / (2.0 * knee); }
        env = gr > env ? gr + att * (env - gr) : gr + rel * (env - gr);
        const double makeup = 0.4 * (-thr) * (1.0 - 1.0 / ratio);
        const double g = dbToGain (makeup - env);
        l *= g; r *= g;
    }
private:
    double sr = 48000.0, att = 0.9, rel = 0.99, env = 0.0;
};

//==============================================================================
class DrumEngine
{
public:
    void prepare (double sampleRate, int maxBlock)
    {
        sr = sampleRate;
        juce::ignoreUnused (maxBlock);
        for (int s = 0; s < numSlots; ++s) slots[(size_t) s].prepare (sr, s);
        reverb.prepare (sr);
        delay.prepare (sr);
        glue.prepare (sr);
        anaTopL.set (14000.0, sr); anaTopR.set (14000.0, sr);
        anaBlend.prepare (sr);
        reset();
    }

    void reset()
    {
        for (auto& s : slots) s.reset();
        reverb.reset(); delay.reset(); glue.reset();
        anaTopL.reset(); anaTopR.reset();
        anaRand.seed (0x5EED2811u);
        hitCounter = 0;
        volume.reset (1.0); mDrive.reset (0.0); glueAmt.reset (0.0); revRet.reset (1.0); dlyRet.reset (1.0);
        firstBlock = true;
    }

    static constexpr int latencySamples() { return Decimator2x::latencyBase; }

    // Trigger one slot (MIDI note, a pad click, the preview groove).
    void trigger (const DrumParams& p, int slot, float velocity)
    {
        if (slot < 0 || slot >= numSlots) return;
        const auto& sp = p.slots[(size_t) slot];
        if (sp.choke > 0) // CHOKE: a hit silences the other slots of its group (closed hat stops the open one)
            for (int s = 0; s < numSlots; ++s)
                if (s != slot && p.slots[(size_t) s].choke == sp.choke)
                    slots[(size_t) s].choke();
        // DIGITAL: every hit of a slot is the same (like a sample, as the sounds were fitted and levelled).
        // ANALOGUE: each hit a little different - pitch +/-6 cents, length +/-4 %, level +/-0.4 dB, its own noise.
        const double r1 = anaRand.next(), r2 = anaRand.next(), r3 = anaRand.next(); // drawn in DIGITAL too
        ++hitCounter;
        if (p.analog)
            slots[(size_t) slot].noteOn (sp, velocity, 0x9E3779B9u * (unsigned) hitCounter + (unsigned) slot * 7919u + 1u,
                                         6.0 * r1, 1.0 + 0.04 * r2, dbToGain (0.4 * r3));
        else
            slots[(size_t) slot].noteOn (sp, velocity, 0x2545F491u + (unsigned) slot * 104729u, 0.0, 1.0, 1.0);
    }

    void allOff() { for (auto& s : slots) s.choke (0.005); }

    // outL/outR: one pointer per output (0 = MAIN, k = OUT k); a null pointer means that output is off in the host:
    // its slots play on MAIN. Every non-null output is overwritten.
    void process (float* const* outL, float* const* outR, int numOutputsAvailable, int n, const juce::MidiBuffer& midi,
                  const DrumParams& p, int midiOffset = 0)
    {
        bool anySolo = false;
        for (const auto& s : p.slots) anySolo = anySolo || s.solo;
        for (int s = 0; s < numSlots; ++s)
        {
            const auto& sp = p.slots[(size_t) s];
            slots[(size_t) s].setBlock (sp, ! sp.mute && (! anySolo || sp.solo));
            const int o = sp.out;
            route[(size_t) s] = (o > 0 && o < numOutputsAvailable && outL[o] != nullptr && outR[o] != nullptr) ? o : 0;
        }
        for (int o = 0; o < numOutputsAvailable; ++o)
            if (outL[o] != nullptr && outR[o] != nullptr)
            {
                juce::FloatVectorOperations::clear (outL[o], n);
                juce::FloatVectorOperations::clear (outR[o], n);
            }
        reverb.set (p.revSize, p.revDamp);
        delay.set (delayBeats[juce::jlimit (0, numDelayTimes - 1, p.dlyTime)], p.bpm, p.dlyFb);
        const int ramp = (int) (0.02 * sr);
        if (firstBlock)
        {
            volume.reset (dbToGain (p.volumeDb) * (p.volumeDb <= -59.9f ? 0.0 : 1.0));
            mDrive.reset (p.mDrive); glueAmt.reset (p.glue); revRet.reset (p.revLevel); dlyRet.reset (p.dlyLevel);
            firstBlock = false;
        }
        volume.setTarget (dbToGain (p.volumeDb) * (p.volumeDb <= -59.9f ? 0.0 : 1.0), ramp);
        mDrive.setTarget (p.mDrive, ramp); glueAmt.setTarget (p.glue, ramp); revRet.setTarget (p.revLevel, ramp); dlyRet.setTarget (p.dlyLevel, ramp);

        auto it = midi.findNextSamplePosition (midiOffset);
        for (int i = 0; i < n; ++i)
        {
            while (it != midi.cend() && (*it).samplePosition <= midiOffset + i)
            {
                const auto msg = (*it).getMessage();
                if (msg.isNoteOn())
                    trigger (p, msg.getNoteNumber() - firstNote, msg.getFloatVelocity());
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                    allOff();
                ++it;
            }
            double mainL = 0.0, mainR = 0.0, revL = 0.0, revR = 0.0, dlL = 0.0, dlR = 0.0;
            for (int s = 0; s < numSlots; ++s)
            {
                double l, r, sRev, sDly;
                slots[(size_t) s].tick (l, r, sRev, sDly);
                if (l == 0.0 && r == 0.0) continue;
                revL += l * sRev; revR += r * sRev;
                dlL += l * sDly; dlR += r * sDly;
                const int o = route[(size_t) s];
                if (o == 0) { mainL += l; mainR += r; }
                else { outL[o][i] += (float) l; outR[o][i] += (float) r; }
            }
            double rl, rr, dl, dr;
            reverb.tick (revL, revR, rl, rr);
            delay.tick (dlL, dlR, dl, dr);
            const double rg = revRet.next(), dg = dlyRet.next();
            mainL += rl * rg + dl * dg;
            mainR += rr * rg + dr * dg;
            glue.tick (mainL, mainR, glueAmt.next());
            const double md = mDrive.next();
            if (md > 1.0e-4) // master DRIVE: soft saturation, louder and denser; the peak of a full-scale hit kept
            {
                const double k = 1.0 + 4.0 * md, nk = 1.0 / std::tanh (k);
                mainL = std::tanh (k * mainL) * nk;
                mainR = std::tanh (k * mainR) * nk;
            }
            const double ab = anaBlend.tick (p.analog);
            if (ab > 0.0) // ANALOGUE on MAIN: a little even-harmonic warmth and a softer top
            {
                mainL += ab * (anaTopL.tick (power_analog::warm (mainL)) - mainL);
                mainR += ab * (anaTopR.tick (power_analog::warm (mainR)) - mainR);
            }
            const double vol = volume.next();
            outL[0][i] = (float) safety (mainL * vol);
            outR[0][i] = (float) safety (mainR * vol);
            for (int o = 1; o < numOutputsAvailable; ++o)
                if (outL[o] != nullptr && outR[o] != nullptr)
                {
                    outL[o][i] = (float) safety (outL[o][i] * vol);
                    outR[o][i] = (float) safety (outR[o][i] * vol);
                }
        }
    }

    const Slot& slot (int i) const { return slots[(size_t) i]; }
    Slot& slot (int i) { return slots[(size_t) i]; }

private:
    // Emergency ceiling only: kits keep their peaks below it.
    static double safety (double x) noexcept
    {
        const double a = std::abs (x);
        if (a <= 0.9) return x;
        return std::copysign (0.9 + 0.099 * std::tanh ((a - 0.9) / 0.099), x);
    }

    double sr = 48000.0;
    std::array<Slot, numSlots> slots;
    std::array<int, numSlots> route {};
    Reverb reverb;
    Delay delay;
    Glue glue;
    Ramp volume, mDrive, glueAmt, revRet, dlyRet;
    power_analog::Random anaRand;
    power_analog::OnePole anaTopL, anaTopR;
    power_analog::Blend anaBlend;
    unsigned int hitCounter = 0;
    bool firstBlock = true;
};
}
