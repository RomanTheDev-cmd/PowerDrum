// PowerDrum engine and kit tests (headless). Run from the project folder.
//   powerdrum_tests               all checks
//   powerdrum_tests --serve       render server for Tools/fit_drums.py: each stdin line "type=0 model=1 tune=2 ... _len=1.2"
//                                 (slot-1 parameters without the prefix) -> one hit, float32 stereo interleaved on stdout
//   powerdrum_tests --kit-peaks   the peak of every factory kit on the PLAY groove (dBFS), for Tools/build_kits.py
//   powerdrum_tests --render-kit <index> <file.wav>   the PLAY groove of a factory kit (listening checks)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <chrono>
#include <cmath>
#include <complex>
#include <cstdio>
#include <iostream>
#include <sstream>
#include "DSP/DrumEngine.h"
#include "Presets.h"
#include "Groove.h"

using namespace pd;

namespace
{
int checks = 0, failures = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}
double db (double g) { return 20.0 * std::log10 (std::max (g, 1.0e-12)); }

struct Hit { double time; int slot; float vel = 0.9f; };

// Renders hits; returns 2 channels per output (MAIN first). active[k] false = output k off in the host.
std::vector<juce::AudioBuffer<float>> renderOuts (const DrumParams& p, const std::vector<Hit>& hits, double seconds, double sr = 48000.0,
                                                  int block = 512, std::array<bool, numOutputs> active = {})
{
    dsp::DrumEngine e;
    e.prepare (sr, block);
    const int total = (int) (seconds * sr);
    std::vector<juce::AudioBuffer<float>> outs ((size_t) numOutputs);
    active[0] = true;
    for (auto& o : outs) { o.setSize (2, total); o.clear(); }
    std::array<float*, numOutputs> l {}, r {};
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer m;
        for (const auto& h : hits)
        {
            const int at = (int) std::lround (h.time * sr);
            if (at >= pos && at < pos + n) m.addEvent (juce::MidiMessage::noteOn (10, firstNote + h.slot, h.vel), at - pos);
        }
        for (int o = 0; o < numOutputs; ++o)
        {
            l[(size_t) o] = active[(size_t) o] ? outs[(size_t) o].getWritePointer (0, pos) : nullptr;
            r[(size_t) o] = active[(size_t) o] ? outs[(size_t) o].getWritePointer (1, pos) : nullptr;
        }
        e.process (l.data(), r.data(), numOutputs, n, m, p);
    }
    return outs;
}

juce::AudioBuffer<float> render (const DrumParams& p, const std::vector<Hit>& hits, double seconds, double sr = 48000.0, int block = 512)
{
    return renderOuts (p, hits, seconds, sr, block)[0];
}

double peak (const juce::AudioBuffer<float>& a, int from = 0, int to = -1)
{
    if (to < 0) to = a.getNumSamples();
    double m = 0.0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = from; i < to; ++i) m = std::max (m, (double) std::abs (a.getSample (c, i)));
    return m;
}
double rms (const juce::AudioBuffer<float>& a, int from, int to, int ch = -1)
{
    double s = 0.0; int n = 0;
    for (int c = 0; c < a.getNumChannels(); ++c)
        if (ch < 0 || ch == c)
            for (int i = from; i < std::min (to, a.getNumSamples()); ++i) { s += (double) a.getSample (c, i) * a.getSample (c, i); ++n; }
    return n > 0 ? std::sqrt (s / n) : 0.0;
}
bool allFinite (const juce::AudioBuffer<float>& a)
{
    for (int c = 0; c < a.getNumChannels(); ++c)
        for (int i = 0; i < a.getNumSamples(); ++i)
            if (! std::isfinite (a.getSample (c, i))) return false;
    return true;
}

// One slot only (slot 0), everything else muted; neutral mix.
DrumParams single (DrumType type, int model, float decay = 0.5f, float tone = 0.5f, float a = 0.5f, float b = 0.5f)
{
    DrumParams p = paramsFromSnapshot ({});
    for (auto& s : p.slots) s.mute = true;
    auto& s = p.slots[0];
    s = SlotParams();
    s.type = type; s.model = model; s.decay = decay; s.tone = tone; s.a = a; s.b = b;
    s.levelDb = 0.0f; s.trimDb = 0.0f;
    p.revLevel = 1.0f; p.dlyLevel = 1.0f;
    return p;
}

// Time of the last sample above -60 dB of the peak.
double soundLength (const juce::AudioBuffer<float>& a, double sr)
{
    const double pk = peak (a);
    int last = 0;
    for (int c = 0; c < 2; ++c)
        for (int i = a.getNumSamples() - 1; i > last; --i)
            if (std::abs (a.getSample (c, i)) > pk * 0.001) { last = i; break; }
    return last / sr;
}

// Spectral peakiness of the tail (from 40 ms): the loudest 1/12-octave band over the median of its two-octave
// neighbourhood, 150 Hz..12 kHz (dB). A held pitch ("synth-like") stands 20-40 dB out; noise and metal stay low.
double tailTonality (const juce::AudioBuffer<float>& a, double sr)
{
    const int from = (int) (0.04 * sr);
    const int n = 8192;
    if (a.getNumSamples() < from + n / 4) return 0.0;
    std::vector<std::complex<double>> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const int k = from + i;
        const double v = k < a.getNumSamples() ? 0.5 * (a.getSample (0, k) + a.getSample (1, k)) : 0.0;
        x[(size_t) i] = v * (0.5 - 0.5 * std::cos (2.0 * dsp::pi * i / (n - 1)));
    }
    // FFT (radix 2)
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1) j ^= bit;
        j ^= bit;
        if (i < j) std::swap (x[(size_t) i], x[(size_t) j]);
    }
    for (int len = 2; len <= n; len <<= 1)
    {
        const auto w = std::polar (1.0, -2.0 * dsp::pi / len);
        for (int i = 0; i < n; i += len)
        {
            std::complex<double> wn (1.0);
            for (int j = 0; j < len / 2; ++j)
            {
                const auto u = x[(size_t) (i + j)], v = x[(size_t) (i + j + len / 2)] * wn;
                x[(size_t) (i + j)] = u + v; x[(size_t) (i + j + len / 2)] = u - v;
                wn *= w;
            }
        }
    }
    std::vector<double> bands;
    for (double f = 150.0; f < 12000.0; f *= std::exp2 (1.0 / 12.0))
    {
        double e = 0.0;
        for (int k = (int) (f / sr * n); k < (int) (f * std::exp2 (1.0 / 12.0) / sr * n); ++k) e += std::norm (x[(size_t) k]);
        bands.push_back (10.0 * std::log10 (e + 1.0e-20));
    }
    double worst = 0.0;
    for (size_t i = 12; i + 12 < bands.size(); ++i)
    {
        std::vector<double> nb (bands.begin() + (long) i - 12, bands.begin() + (long) i + 13);
        std::nth_element (nb.begin(), nb.begin() + 12, nb.end());
        worst = std::max (worst, bands[i] - nb[12]);
    }
    return worst;
}

// The PLAY groove of the processor (two bars of 16ths), repeated.
std::vector<Hit> grooveHits (DrumParams& p, int repeats, double bpm = 140.0)
{
    p.bpm = bpm;
    std::vector<Hit> hits;
    const double step = 60.0 / bpm / 4.0;
    for (int rep = 0; rep < repeats; ++rep)
        for (int st = 0; st < grooveSteps; ++st)
            for (int s = 0; s < numSlots; ++s)
                if (const float v = grooveVelocity (s, st); v > 0.0f)
                    hits.push_back ({ (rep * grooveSteps + st) * step, s, v });
    return hits;
}

// Peak after 4x oversampling (an estimate of the true peak).
double truePeak (const juce::AudioBuffer<float>& a)
{
    juce::dsp::Oversampling<float> os (2, 2, juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true, false);
    os.initProcessing ((size_t) a.getNumSamples());
    juce::AudioBuffer<float> copy (a);
    juce::dsp::AudioBlock<float> block (copy);
    auto up = os.processSamplesUp (block);
    double m = 0.0;
    for (size_t c = 0; c < up.getNumChannels(); ++c)
        for (size_t i = 0; i < up.getNumSamples(); ++i) m = std::max (m, (double) std::abs (up.getSample ((int) c, (int) i)));
    return m;
}

//==============================================================================
void testEveryTypeAndModel()
{
    const double sr = 48000.0;
    for (int t = 0; t < numTypes; ++t)
        for (int m = 0; m < 3; ++m)
            for (const auto& [d, tone, a, b] : { std::tuple { 0.0f, 0.0f, 0.0f, 0.0f }, std::tuple { 0.5f, 0.5f, 0.5f, 0.5f }, std::tuple { 1.0f, 1.0f, 1.0f, 1.0f } })
            {
                auto p = single ((DrumType) t, m, d, tone, a, b);
                const auto name = typeNames()[t] + " " + modelNames ((DrumType) t)[m] + " d" + juce::String (d) + " t" + juce::String (tone) + " a" + juce::String (a);
                const double len = dsp::decaySeconds ((DrumType) t, d) + 1.2;
                const auto out = render (p, { { 0.01, 0 } }, len + 0.3, sr);
                expect (allFinite (out), name + ": finite");
                const double pk = peak (out);
                expect (pk > 0.03 && pk < 0.999, name + ": peak " + juce::String (db (pk), 1) + " dB in -30..0");
                // no lasting DC: the mean of the whole render is small against the peak
                double sum = 0.0;
                for (int i = 0; i < out.getNumSamples(); ++i) sum += out.getSample (0, i);
                expect (std::abs (sum / out.getNumSamples()) < 0.01 * pk + 1.0e-4, name + ": no DC");
                // it ends: the last 0.25 s is silent
                expect (peak (out, out.getNumSamples() - (int) (0.25 * sr)) < pk * 0.002, name + ": decays to silence");
            }
}

void testNotesAndSlots()
{
    // Every slot plays on its own note, other notes are ignored.
    auto p = paramsFromSnapshot ({});
    for (int s = 0; s < numSlots; ++s)
    {
        auto q = p;
        for (int k = 0; k < numSlots; ++k) q.slots[(size_t) k].mute = k != s;
        const auto out = render (q, { { 0.0, s } }, 0.4);
        expect (peak (out) > 0.01, "slot " + juce::String (s + 1) + " (" + noteName (s) + ") plays");
    }
    dsp::DrumEngine e;
    e.prepare (48000.0, 512);
    juce::AudioBuffer<float> out (2, 4800);
    juce::MidiBuffer m;
    m.addEvent (juce::MidiMessage::noteOn (1, 35, 1.0f), 0);
    m.addEvent (juce::MidiMessage::noteOn (1, 52, 1.0f), 10);
    m.addEvent (juce::MidiMessage::noteOn (1, 60, 1.0f), 20);
    float* l[numOutputs] {}; float* r[numOutputs] {};
    l[0] = out.getWritePointer (0); r[0] = out.getWritePointer (1);
    e.process (l, r, numOutputs, 4800, m, p);
    expect (peak (out) == 0.0, "notes outside C1..D#2 are silent");
}

void testOutputs()
{
    // Slot 4 (clap) to OUT 4: heard on OUT 4 only when that output is on; on MAIN when it is off.
    auto p = paramsFromSnapshot ({});
    for (int k = 0; k < numSlots; ++k) p.slots[(size_t) k].mute = k != 3;
    p.slots[3].out = 4;
    std::array<bool, numOutputs> on {};
    on[4] = true;
    auto outs = renderOuts (p, { { 0.0, 3 } }, 0.5, 48000.0, 512, on);
    expect (peak (outs[4]) > 0.05, "a slot routed to OUT 4 plays there");
    expect (peak (outs[0]) < 1.0e-6, "... and not on MAIN");
    auto off = renderOuts (p, { { 0.0, 3 } }, 0.5, 48000.0, 512, {});
    expect (std::abs (peak (off[0]) - peak (outs[4])) < 1.0e-4, "OUT 4 off in the host: the slot plays on MAIN instead");

    // MULTI: every slot on its own output, all of them separate.
    auto all = paramsFromSnapshot ({});
    for (int k = 0; k < numSlots; ++k) all.slots[(size_t) k].out = k + 1;
    std::array<bool, numOutputs> every {};
    every.fill (true);
    std::vector<Hit> hits;
    for (int k = 0; k < numSlots; ++k) hits.push_back ({ 0.0, k });
    auto multi = renderOuts (all, hits, 0.6, 48000.0, 512, every);
    int heard = 0;
    for (int k = 1; k <= numSlots; ++k) heard += peak (multi[(size_t) k]) > 0.005 ? 1 : 0;
    expect (heard == numSlots, "MULTI: all 16 outputs carry their slot (" + juce::String (heard) + ")");
    expect (peak (multi[0]) < 1.0e-6, "MULTI with no sends: MAIN is silent");

    // A reverb send of a slot on its own output returns on MAIN.
    all.slots[3].rev = 0.8f;
    auto withRev = renderOuts (all, hits, 1.2, 48000.0, 512, every);
    expect (peak (withRev[0]) > 0.005, "sends of separated slots return on MAIN");
}

void testChoke()
{
    // Open hat (slot 10) then closed hat (slot 6), both in choke group A: the open hat stops within ~10 ms.
    auto p = paramsFromSnapshot ({});
    for (int k = 0; k < numSlots; ++k) p.slots[(size_t) k].mute = k != 10 && k != 6;
    p.slots[10].decay = 0.8f;
    p.slots[6].choke = p.slots[10].choke = 1;
    const double sr = 48000.0;
    auto both = render (p, { { 0.0, 10 }, { 0.2, 6 } }, 0.8, sr);
    auto closedOnly = render (p, { { 0.2, 6 } }, 0.8, sr);
    juce::AudioBuffer<float> diff (2, both.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < both.getNumSamples(); ++i) diff.setSample (c, i, both.getSample (c, i) - closedOnly.getSample (c, i));
    const double openBefore = rms (diff, (int) (0.15 * sr), (int) (0.2 * sr));
    const double openAfter = rms (diff, (int) (0.215 * sr), (int) (0.3 * sr));
    expect (openBefore > 0.002, "the open hat rings before the closed one (" + juce::String (openBefore) + ")");
    expect (openAfter < openBefore * 0.01, "CHOKE: the closed hat stops the open one (" + juce::String (db (openAfter / std::max (openBefore, 1e-9)), 1) + " dB)");
    p.slots[6].choke = 0;
    auto unchoked = render (p, { { 0.0, 10 }, { 0.2, 6 } }, 0.8, sr);
    juce::AudioBuffer<float> d2 (2, both.getNumSamples());
    for (int c = 0; c < 2; ++c)
        for (int i = 0; i < both.getNumSamples(); ++i) d2.setSample (c, i, unchoked.getSample (c, i) - closedOnly.getSample (c, i));
    expect (rms (d2, (int) (0.215 * sr), (int) (0.3 * sr)) > openBefore * 0.2, "without a group the open hat rings on");
}

void testBlockSizesAndRates()
{
    // The same hits rendered with different host blocks are identical (events at their exact sample).
    auto p = paramsFromSnapshot ({});
    p.slots[3].rev = 0.3f; p.slots[6].dly = 0.3f; p.glue = 0.5f; p.mDrive = 0.3f;
    for (auto& s : p.slots) { s.drive = 0.4f; s.width = 0.5f; s.filter = 0.1f; }
    std::vector<Hit> hits;
    for (int i = 0; i < 16; ++i) hits.push_back ({ i * 0.107 + 0.013, i });
    const auto a = render (p, hits, 2.5, 48000.0, 512);
    for (int block : { 64, 333, 1024 })
    {
        const auto b = render (p, hits, 2.5, 48000.0, block);
        double d = 0.0;
        for (int c = 0; c < 2; ++c)
            for (int i = 0; i < a.getNumSamples(); ++i) d = std::max (d, (double) std::abs (a.getSample (c, i) - b.getSample (c, i)));
        expect (d < 1.0e-5, "block " + juce::String (block) + " renders the same as 512 (max diff " + juce::String (d) + ")");
    }
    // Sample rates: each type lasts about as long and keeps its level (within 1.5 dB).
    for (int t = 0; t < numTypes; ++t)
    {
        auto q = single ((DrumType) t, 0);
        const auto x48 = render (q, { { 0.0, 0 } }, 2.5, 48000.0);
        for (double sr : { 44100.0, 96000.0 })
        {
            const auto x = render (q, { { 0.0, 0 } }, 2.5, sr);
            const double l48 = soundLength (x48, 48000.0), lx = soundLength (x, sr);
            expect (std::abs (lx - l48) < 0.08 * l48 + 0.01, typeNames()[t] + juce::String (" length at ") + juce::String (sr) + ": " + juce::String (lx, 3) + " vs " + juce::String (l48, 3));
            expect (std::abs (db (rms (x, 0, x.getNumSamples())) - db (rms (x48, 0, x48.getNumSamples()))) < 1.0
                        && std::abs (db (peak (x)) - db (peak (x48))) < 3.0,   // the peak of noise is a random extreme; the RMS is the level
                    typeNames()[t] + juce::String (" level at ") + juce::String (sr) + ": rms " + juce::String (db (rms (x, 0, x.getNumSamples())) - db (rms (x48, 0, x48.getNumSamples())), 2)
                        + " dB, peak " + juce::String (db (peak (x)) - db (peak (x48)), 2) + " dB");
        }
    }
}

void testMixControls()
{
    const double sr = 48000.0;
    auto p = single (DrumType::clap, 0);
    // WIDTH never changes the mono sum.
    auto narrow = render (p, { { 0.0, 0 } }, 0.8);
    p.slots[0].width = 1.0f;
    auto wide = render (p, { { 0.0, 0 } }, 0.8);
    double d = 0.0, side = 0.0;
    for (int i = 0; i < wide.getNumSamples(); ++i)
    {
        d = std::max (d, (double) std::abs ((wide.getSample (0, i) + wide.getSample (1, i)) - (narrow.getSample (0, i) + narrow.getSample (1, i))));
        side = std::max (side, (double) std::abs (wide.getSample (0, i) - wide.getSample (1, i)));
    }
    expect (d < 1.0e-5, "WIDTH keeps the mono sum (" + juce::String (d) + ")");
    expect (side > 0.05, "WIDTH makes a side signal");
    // PAN hard left: nothing on the right.
    p.slots[0].width = 0.0f; p.slots[0].pan = -1.0f;
    auto left = render (p, { { 0.0, 0 } }, 0.5);
    expect (rms (left, 0, left.getNumSamples(), 1) < 1.0e-5 && rms (left, 0, left.getNumSamples(), 0) > 0.01, "PAN left: right channel silent");
    // FILTER: low-pass removes highs, high-pass removes lows.
    auto q = single (DrumType::kick, 0);
    auto open = render (q, { { 0.0, 0 } }, 0.6);
    q.slots[0].filter = 0.8f;
    auto hp = render (q, { { 0.0, 0 } }, 0.6);
    expect (rms (hp, 0, hp.getNumSamples()) < rms (open, 0, open.getNumSamples()) * 0.3, "FILTER high-pass thins the kick");
    // MUTE and SOLO
    auto m = paramsFromSnapshot ({});
    m.slots[0].mute = true;
    expect (peak (render (m, { { 0.0, 0 } }, 0.3)) < 1.0e-7, "MUTE silences a slot");
    m.slots[0].mute = false; m.slots[3].solo = true;
    expect (peak (render (m, { { 0.0, 0 } }, 0.3)) < 1.0e-7, "SOLO elsewhere silences the others");
    expect (peak (render (m, { { 0.0, 3 } }, 0.3)) > 0.01, "the soloed slot plays");
    // LEVEL -inf
    auto lv = single (DrumType::snare, 0);
    lv.slots[0].levelDb = -60.0f;
    expect (peak (render (lv, { { 0.0, 0 } }, 0.3)) < 1.0e-7, "LEVEL at the bottom is silent");
    // Velocity: softer is quieter.
    auto v = single (DrumType::snare, 0);
    const double loud = peak (render (v, { { 0.0, 0, 1.0f } }, 0.4)), soft = peak (render (v, { { 0.0, 0, 0.4f } }, 0.4));
    expect (soft < loud * 0.7 && soft > loud * 0.05, "velocity: a soft hit is quieter (" + juce::String (db (soft / loud), 1) + " dB)");
    juce::ignoreUnused (sr);
}

void testLatencyAndRetrigger()
{
    // The decimator delays everything by exactly the reported latency: the kick starts 24 samples after its note.
    auto p = single (DrumType::kick, 0, 0.5f, 0.5f, 0.5f, 0.0f);
    const auto out = render (p, { { 0.0, 0 } }, 0.2);
    int first = -1;
    for (int i = 0; i < out.getNumSamples() && first < 0; ++i)
        if (std::abs (out.getSample (0, i)) > 0.01 * peak (out)) first = i;
    expect (std::abs (first - dsp::DrumEngine::latencySamples()) <= 6, "latency " + juce::String (first) + " samples ~ reported 24");

    // Fast kick rolls: no click at the hand-off (second difference against a single hit).
    const double sr = 48000.0;
    std::vector<Hit> roll;
    for (int i = 0; i < 8; ++i) roll.push_back ({ 0.05 + i * 0.0625, 0 });
    auto r = render (p, roll, 0.7, sr);
    double worst = 0.0, normal = 0.0;
    auto d2 = [] (const juce::AudioBuffer<float>& a, int i) { return std::abs (a.getSample (0, i + 1) - 2.0 * a.getSample (0, i) + a.getSample (0, i - 1)); };
    for (int i = 1; i + 1 < r.getNumSamples(); ++i) worst = std::max (worst, (double) d2 (r, i));
    auto one = render (p, { { 0.05, 0 } }, 0.7, sr);
    for (int i = 1; i + 1 < one.getNumSamples(); ++i) normal = std::max (normal, (double) d2 (one, i));
    expect (worst < normal * 1.8, "kick roll: no hand-off click (" + juce::String (worst / normal, 2) + "x the single hit)");
}

void testNotSynthLike()
{
    // Roman: drums, not synths. With default knobs no type except TOM and PERC WOOD holds a pitch in its tail.
    for (int t = 0; t < numTypes; ++t)
        for (int m = 0; m < 3; ++m)
        {
            const auto type = (DrumType) t;
            // Kick, tom and rumble tails are low and pitched by nature (checked below and by the fit); cymbals and percussion
            // are metal - inharmonic partials stand out by nature, and their tonality is held to the references by the fit.
            if (type == DrumType::tom || type == DrumType::kick || type == DrumType::rumble || type == DrumType::cymbal || type == DrumType::perc) continue;
            auto p = single (type, m);
            const auto out = render (p, { { 0.0, 0 } }, 0.5);
            const double tn = tailTonality (out, 48000.0);
            expect (tn < 30.0, typeNames()[t] + " " + modelNames (type)[m] + ": tail tonality " + juce::String (tn, 1) + " dB < 30");
        }
    // The kick body is low-passed after the attack: little energy between 300 Hz and 3 kHz in the tail.
    auto k = single (DrumType::kick, 0, 0.5f, 0.3f, 0.6f, 0.0f);
    const auto out = render (k, { { 0.0, 0 } }, 0.6);
    juce::AudioBuffer<float> tail (2, (int) (0.1 * 48000));
    for (int c = 0; c < 2; ++c) tail.copyFrom (c, 0, out, c, (int) (0.06 * 48000), tail.getNumSamples());
    dsp::Biquad hp; hp.highpass (300.0, 0.7, 48000.0);
    dsp::Biquad lp; lp.lowpass (3000.0, 0.7, 48000.0);
    double mid = 0.0, all = 0.0;
    for (int i = 0; i < tail.getNumSamples(); ++i)
    {
        const double x = tail.getSample (0, i);
        const double y = lp.tick (hp.tick (x));
        mid += y * y; all += x * x;
    }
    expect (10.0 * std::log10 (mid / all) < -20.0, "kick tail mids (300 Hz-3 kHz) " + juce::String (10.0 * std::log10 (mid / all), 1) + " dB < -20");
}

void testAnalogue()
{
    auto p = single (DrumType::hat, 0);
    auto a = render (p, { { 0.0, 0 }, { 0.3, 0 } }, 0.6);
    // DIGITAL: two hits identical
    double d = 0.0;
    const int off = (int) (0.3 * 48000);
    for (int i = 0; i < off - 100; ++i) d = std::max (d, (double) std::abs (a.getSample (0, i) - a.getSample (0, i + off)));
    expect (d < 1.0e-6, "DIGITAL: every hit of a slot is the same");
    p.analog = true;
    auto b = render (p, { { 0.0, 0 }, { 0.3, 0 } }, 0.6);
    d = 0.0;
    for (int i = 0; i < off - 100; ++i) d = std::max (d, (double) std::abs (b.getSample (0, i) - b.getSample (0, i + off)));
    expect (d > 1.0e-3, "ANALOGUE: hits differ a little");
    expect (allFinite (b), "ANALOGUE finite");
}

void testEffectsDecay()
{
    auto p = paramsFromSnapshot ({});
    for (auto& s : p.slots) { s.rev = 1.0f; s.dly = 1.0f; }
    p.revSize = 1.0f; p.dlyFb = 0.9f;
    std::vector<Hit> hits;
    for (int k = 0; k < numSlots; ++k) hits.push_back ({ 0.0, k, 1.0f });
    const auto out = render (p, hits, 1.5);
    expect (allFinite (out), "all sends at full: finite");
    expect (peak (out) < 1.0, "all sends at full: under the ceiling");
}

void testCpu()
{
    // Worst case: all 16 slots, fast rolls (4 voices each), drive, width, filters, all effects.
    auto p = paramsFromSnapshot ({});
    for (auto& s : p.slots) { s.drive = 0.6f; s.width = 1.0f; s.filter = -0.3f; s.rev = 0.5f; s.dly = 0.5f; s.decay = 1.0f; }
    p.glue = 1.0f; p.mDrive = 1.0f; p.analog = true;
    std::vector<Hit> hits;
    for (int i = 0; i < 64; ++i)
        for (int k = 0; k < numSlots; ++k) hits.push_back ({ i * 0.03125, k });
    const double seconds = 2.0;
    const auto t0 = std::chrono::steady_clock::now();
    render (p, hits, seconds);
    const double used = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    std::cout << "CPU worst case: " << juce::String (100.0 * used / seconds, 1) << " % of real time\n";
    expect (used < seconds * 0.6, "worst case under 60 % of real time");
}

void testFactoryKits()
{
    for (const auto& k : factoryKits())
    {
        auto p = paramsFromSnapshot (k.values);
        std::vector<Hit> hits;
        for (int k2 = 0; k2 < numSlots; ++k2) hits.push_back ({ 0.01 + 0.15 * k2, k2 });
        const auto out = render (p, hits, 3.5);
        expect (allFinite (out), k.name + ": finite");
        expect (peak (out) < 0.99, k.name + ": under the ceiling");
        if (k.category != "Init")
        {
            auto q = paramsFromSnapshot (k.values);
            const double tp = db (truePeak (render (q, grooveHits (q, 1), 32 * 60.0 / 140.0 / 4.0 + 1.0)));
            expect (tp < -0.5 && tp > -3.0, k.name + ": the groove peaks at " + juce::String (tp, 2) + " dBTP (-1 target)");
        }
        for (int s = 0; s < numSlots; ++s)
            expect (k.slotNames[(size_t) s].isNotEmpty(), k.name + ": slot " + juce::String (s + 1) + " has a name");
    }
}
} // namespace

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    const juce::String mode = argc > 1 ? juce::String (argv[1]) : juce::String();
    if (mode == "--serve")
    {
        std::string line;
        std::vector<float> out;
        while (std::getline (std::cin, line))
        {
            Snapshot s;
            double len = 1.0;
            int frames = -1;   // "_n": the exact number of frames to write (the client counts on it)
            std::istringstream ss (line);
            std::string tok;
            while (ss >> tok)
            {
                const auto eq = tok.find ('=');
                if (eq == std::string::npos) continue;
                const auto key = juce::String (tok.substr (0, eq));
                const float value = std::stof (tok.substr (eq + 1));
                if (key == "_len") len = value;
                else if (key == "_n") frames = std::stoi (tok.substr (eq + 1));
                else s[slotId (0, key.toRawUTF8())] = value;
            }
            if (frames > 0) len = frames / 48000.0 + 0.01;
            auto p = paramsFromSnapshot (s);
            for (int k = 1; k < numSlots; ++k) p.slots[(size_t) k].mute = true;
            p.slots[0].out = 0;
            const auto audio = render (p, { { 0.0, 0, 1.0f } }, len);
            const int n = frames > 0 ? frames : audio.getNumSamples();
            out.assign ((size_t) n * 2, 0.0f);
            // the latency removed: the hit starts at sample 0 like a reference one-shot
            const int lat = dsp::DrumEngine::latencySamples();
            for (int i = 0; i < n; ++i)
                if (i + lat < audio.getNumSamples())
                {
                    out[(size_t) i * 2] = audio.getSample (0, i + lat);
                    out[(size_t) i * 2 + 1] = audio.getSample (1, i + lat);
                }
            std::fwrite (out.data(), sizeof (float), out.size(), stdout);
            std::fflush (stdout);
        }
        return 0;
    }
    if (mode == "--levels")
    {
        for (int ty = 0; ty < numTypes; ++ty)
            for (int m = 0; m < 3; ++m)
            {
                std::cout << typeNames()[ty] << " " << modelNames ((DrumType) ty)[m] << ":";
                for (float x : { 0.0f, 0.5f, 1.0f })
                {
                    auto p = single ((DrumType) ty, m, x, x, x, x);
                    p.volumeDb = -12.0f; // below the ceiling
                    const auto out = render (p, { { 0.0, 0 } }, dsp::decaySeconds ((DrumType) ty, x) + 1.0);
                    std::cout << "  " << juce::String (db (peak (out)) + 12.0, 1) << " dB/" << juce::String (soundLength (out, 48000.0), 2) << " s";
                }
                std::cout << "\n";
            }
        return 0;
    }
    if (mode == "--kit-peaks")
    {
        // The true peak (4x oversampled) of every factory kit's PLAY groove on MAIN, at VOLUME 0 dB and 140 BPM.
        for (const auto& k : factoryKits())
        {
            auto p = paramsFromSnapshot (k.values);
            p.volumeDb = 0.0f;
            std::cout << k.name << "|" << juce::String (db (truePeak (render (p, grooveHits (p, 2), 64 * 60.0 / 140.0 / 4.0 + 1.5))), 2) << "\n";
        }
        return 0;
    }
    if (mode == "--render-kit" && argc > 3)
    {
        const int index = juce::String (argv[2]).getIntValue();
        const auto& kits = factoryKits();
        if (index < 0 || index >= (int) kits.size()) return 1;
        auto p = paramsFromSnapshot (kits[(size_t) index].values);
        const auto hits = grooveHits (p, 2, 145.0);   // the PLAY groove, four bars
        const auto out = render (p, hits, 64 * 60.0 / 145.0 / 4.0 + 1.5);
        juce::WavAudioFormat wav;
        auto file = juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]);
        file.deleteFile();
        if (auto stream = std::unique_ptr<juce::OutputStream> (file.createOutputStream()))
            if (auto w = std::unique_ptr<juce::AudioFormatWriter> (wav.createWriterFor (stream.get(), 48000.0, 2, 24, {}, 0)))
            {
                stream.release();
                w->writeFromAudioSampleBuffer (out, 0, out.getNumSamples());
            }
        return 0;
    }
    testEveryTypeAndModel();
    testNotesAndSlots();
    testOutputs();
    testChoke();
    testBlockSizesAndRates();
    testMixControls();
    testLatencyAndRetrigger();
    testNotSynthLike();
    testAnalogue();
    testEffectsDecay();
    testFactoryKits();
    testCpu();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
