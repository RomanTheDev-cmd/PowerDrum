#include "Params.h"

namespace pd
{
namespace
{
using APF = juce::AudioParameterFloat;
using APC = juce::AudioParameterChoice;
using APB = juce::AudioParameterBool;
using Attr = juce::AudioParameterFloatAttributes;
using Range = juce::NormalisableRange<float>;
using Layout = juce::AudioProcessorValueTreeState::ParameterLayout;

juce::String pct (float v, int) { return juce::String (juce::roundToInt (v * 100.0f)) + " %"; }
juce::String db (float v, int) { return v <= -59.9f ? juce::String ("-inf dB") : (v > 0.05f ? "+" : "") + juce::String (v, 1) + " dB"; }
juce::String semis (float v, int) { return (v > 0.05f ? "+" : "") + juce::String (v, 1) + " st"; }
juce::String panText (float v, int)
{
    const int p = juce::roundToInt (v * 100.0f);
    return p == 0 ? juce::String ("C") : (p < 0 ? "L " + juce::String (-p) : "R " + juce::String (p));
}

void add (Layout& l, const juce::String& pid, const juce::String& name, Range r, float def, std::function<juce::String (float, int)> text)
{
    l.add (std::make_unique<APF> (juce::ParameterID { pid, 1 }, name, r, def, Attr().withStringFromValueFunction (std::move (text))));
}

Range skewed (float lo, float hi, float centre)
{
    Range r (lo, hi);
    r.setSkewForCentre (centre);
    return r;
}
}

juce::String filterText (float v)
{
    if (std::abs (v) < 0.02f) return "OFF";
    const float hz = v < 0.0f ? 20000.0f * std::pow (200.0f / 20000.0f, -v) : 20.0f * std::pow (4000.0f / 20.0f, v);
    const auto f = hz < 1000.0f ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0f, 1) + " kHz";
    return (v < 0.0f ? "LP " : "HP ") + f;
}

const juce::StringArray& typeNames()
{
    static const juce::StringArray n { "KICK", "SNARE", "CLAP", "HAT", "CYMBAL", "TOM", "PERC", "RUMBLE", "FX" };
    return n;
}

const juce::StringArray& modelNames (DrumType t)
{
    static const juce::StringArray m[numTypes] = {
        { "HARD", "ROUND", "DIST" },        // kick
        { "TIGHT", "FAT", "NOISE" },        // snare
        { "CLASSIC", "WIDE", "DENSE" },     // clap
        { "ANALOG", "BRIGHT", "LOFI" },     // hat
        { "RIDE", "CRASH", "CHINA" },       // cymbal
        { "DEEP", "CONGA", "CLICK" },       // tom
        { "METAL", "RIM", "WOOD" },         // perc
        { "COMB", "VERB", "GROWL" },        // rumble
        { "ZAP", "SWEEP", "IMPACT" },       // fx
    };
    return m[juce::jlimit (0, numTypes - 1, (int) t)];
}

juce::String knobAName (DrumType t)
{
    static const char* n[numTypes] = { "PUNCH", "SNAP", "SPREAD", "METAL", "METAL", "PITCH", "METAL", "PUNCH", "SWEEP" };
    return n[juce::jlimit (0, numTypes - 1, (int) t)];
}

juce::String knobBName (DrumType t)
{
    static const char* n[numTypes] = { "CLICK", "BODY", "ROOM", "HOLD", "BELL", "NOISE", "PITCH", "DENSITY", "GRIT" };
    return n[juce::jlimit (0, numTypes - 1, (int) t)];
}

juce::String noteName (int slot)
{
    return juce::MidiMessage::getMidiNoteName (firstNote + slot, true, true, 3);
}

juce::String slotId (int slot, const char* name) { return "s" + juce::String (slot + 1) + "_" + name; }

const std::vector<const char*>& soundParamNames()
{
    static const std::vector<const char*> n { id::type, id::model, id::tune, id::decay, id::tone, id::knobA, id::knobB, id::drive, id::driveMode, id::trim };
    return n;
}

const std::vector<const char*>& mixParamNames()
{
    static const std::vector<const char*> n { id::level, id::pan, id::width, id::filter, id::rev, id::dly, id::out, id::choke, id::mute, id::solo, id::vel };
    return n;
}

const juce::StringArray& delayTimeNames()
{
    static const juce::StringArray n { "1/16", "1/8T", "1/8", "3/16", "1/4", "3/8" };
    return n;
}

DrumType defaultType (int slot)
{
    // C1 kick, C#1 perc (rim), D1 snare, D#1 clap, E1 rumble, F1 tom, F#1 closed hat, G1 perc, G#1 hat, A1 perc,
    // A#1 open hat, B1 perc, C2 fx, C#2 crash (cymbal), D2 fx, D#2 ride (cymbal).
    static const DrumType t[numSlots] = { DrumType::kick, DrumType::perc, DrumType::snare, DrumType::clap, DrumType::rumble, DrumType::tom,
                                          DrumType::hat, DrumType::perc, DrumType::hat, DrumType::perc, DrumType::hat, DrumType::perc,
                                          DrumType::fx, DrumType::cymbal, DrumType::fx, DrumType::cymbal };
    return t[juce::jlimit (0, numSlots - 1, slot)];
}

const juce::StringArray& allParamIds()
{
    static const juce::StringArray ids = []
    {
        juce::StringArray a;
        for (int s = 0; s < numSlots; ++s)
        {
            for (auto* n : soundParamNames()) a.add (slotId (s, n));
            for (auto* n : mixParamNames()) a.add (slotId (s, n));
        }
        for (auto* g : { id::revSize, id::revDamp, id::revLevel, id::dlyTime, id::dlyFb, id::dlyLevel, id::glue, id::mDrive, id::volume, id::analog })
            a.add (g);
        return a;
    }();
    return ids;
}

namespace
{
// Defaults of a fresh slot: its General MIDI drum, neutral sound, at the centre of the main output.
struct SlotDefaults { float decay, tone, a, b; int model; float levelDb; int choke; };
SlotDefaults slotDefaults (int s)
{
    switch (defaultType (s))
    {
        case DrumType::kick:   return { 0.45f, 0.5f, 0.6f, 0.5f, 0, 0.0f, 0 };
        case DrumType::snare:  return { 0.4f, 0.5f, 0.6f, 0.4f, 0, -4.0f, 0 };
        case DrumType::clap:   return { 0.4f, 0.5f, 0.5f, 0.4f, 0, -4.0f, 0 };
        case DrumType::hat:    return { s == 10 ? 0.55f : 0.15f, 0.6f, 0.6f, 0.0f, 0, -9.0f, 1 };
        case DrumType::cymbal: return { 0.6f, 0.5f, 0.6f, 0.3f, s == 13 ? 1 : 0, -10.0f, 0 };
        case DrumType::tom:    return { 0.4f, 0.5f, 0.4f, 0.3f, 0, -6.0f, 0 };
        case DrumType::perc:   return { 0.3f, 0.5f, 0.5f, 0.3f, s == 1 ? 1 : 0, -7.0f, 0 };
        case DrumType::rumble: return { 0.5f, 0.4f, 0.4f, 0.5f, 0, -4.0f, 0 };
        case DrumType::fx:     return { 0.5f, 0.5f, 0.5f, 0.2f, s == 12 ? 2 : 1, -10.0f, 0 };
    }
    return { 0.5f, 0.5f, 0.5f, 0.5f, 0, 0.0f, 0 };
}
}

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    Layout l;
    const DrumParams g;
    juce::StringArray outs { "MAIN" };
    for (int i = 1; i <= numSlots; ++i) outs.add ("OUT " + juce::String (i));
    for (int s = 0; s < numSlots; ++s)
    {
        const auto d = slotDefaults (s);
        const juce::String n = "Slot " + juce::String (s + 1) + " ";
        l.add (std::make_unique<APC> (juce::ParameterID { slotId (s, id::type), 1 }, n + "Type", typeNames(), (int) defaultType (s)));
        l.add (std::make_unique<APC> (juce::ParameterID { slotId (s, id::model), 1 }, n + "Model", juce::StringArray { "1", "2", "3" }, d.model));
        add (l, slotId (s, id::tune), n + "Tune", Range (-24.0f, 24.0f), 0.0f, semis);
        add (l, slotId (s, id::decay), n + "Decay", Range (0.0f, 1.0f), d.decay, pct);
        add (l, slotId (s, id::tone), n + "Tone", Range (0.0f, 1.0f), d.tone, pct);
        add (l, slotId (s, id::knobA), n + "A", Range (0.0f, 1.0f), d.a, pct);
        add (l, slotId (s, id::knobB), n + "B", Range (0.0f, 1.0f), d.b, pct);
        add (l, slotId (s, id::drive), n + "Drive", Range (0.0f, 1.0f), 0.0f, pct);
        l.add (std::make_unique<APC> (juce::ParameterID { slotId (s, id::driveMode), 1 }, n + "Drive Mode", juce::StringArray { "Saturate", "Clip", "Fold", "Crush" }, 0));
        add (l, slotId (s, id::trim), n + "Gain", Range (-30.0f, 30.0f), 0.0f, db);
        add (l, slotId (s, id::level), n + "Level", skewed (-60.0f, 6.0f, -12.0f), d.levelDb, db);
        add (l, slotId (s, id::pan), n + "Pan", Range (-1.0f, 1.0f), 0.0f, panText);
        add (l, slotId (s, id::width), n + "Width", Range (0.0f, 1.0f), 0.0f, pct);
        add (l, slotId (s, id::filter), n + "Filter", Range (-1.0f, 1.0f), 0.0f, [] (float v, int) { return filterText (v); });
        add (l, slotId (s, id::rev), n + "Reverb Send", Range (0.0f, 1.0f), 0.0f, pct);
        add (l, slotId (s, id::dly), n + "Delay Send", Range (0.0f, 1.0f), 0.0f, pct);
        l.add (std::make_unique<APC> (juce::ParameterID { slotId (s, id::out), 1 }, n + "Output", outs, 0));
        l.add (std::make_unique<APC> (juce::ParameterID { slotId (s, id::choke), 1 }, n + "Choke", juce::StringArray { "OFF", "A", "B", "C", "D" }, d.choke));
        l.add (std::make_unique<APB> (juce::ParameterID { slotId (s, id::mute), 1 }, n + "Mute", false));
        l.add (std::make_unique<APB> (juce::ParameterID { slotId (s, id::solo), 1 }, n + "Solo", false));
        add (l, slotId (s, id::vel), n + "Velocity", Range (0.0f, 1.0f), 0.6f, pct);
    }
    add (l, id::revSize, "Reverb Size", Range (0.0f, 1.0f), g.revSize, pct);
    add (l, id::revDamp, "Reverb Damp", Range (0.0f, 1.0f), g.revDamp, pct);
    add (l, id::revLevel, "Reverb Return", skewed (-60.0f, 6.0f, -12.0f), 0.0f, db);
    l.add (std::make_unique<APC> (juce::ParameterID { id::dlyTime, 1 }, "Delay Time", delayTimeNames(), g.dlyTime));
    add (l, id::dlyFb, "Delay Feedback", Range (0.0f, 0.9f), g.dlyFb, pct);
    add (l, id::dlyLevel, "Delay Return", skewed (-60.0f, 6.0f, -12.0f), 0.0f, db);
    add (l, id::glue, "Glue", Range (0.0f, 1.0f), g.glue, pct);
    add (l, id::mDrive, "Master Drive", Range (0.0f, 1.0f), g.mDrive, pct);
    add (l, id::volume, "Volume", skewed (-60.0f, 6.0f, -12.0f), g.volumeDb, db);
    l.add (std::make_unique<APB> (juce::ParameterID { id::analog, 1 }, "Analogue", g.analog));
    return l;
}

float defaultValue (const juce::String& pid)
{
    static const std::map<juce::String, float> defaults = []
    {
        std::map<juce::String, float> m;
        auto layout = createLayout();
        juce::AudioProcessor* none = nullptr;
        juce::ignoreUnused (none);
        // A throw-away processor-less pass: the parameters own their defaults.
        struct Dummy : juce::AudioProcessor
        {
            Dummy() : juce::AudioProcessor (BusesProperties()) {}
            const juce::String getName() const override { return {}; }
            void prepareToPlay (double, int) override {}
            void releaseResources() override {}
            void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override {}
            double getTailLengthSeconds() const override { return 0; }
            bool acceptsMidi() const override { return false; }
            bool producesMidi() const override { return false; }
            juce::AudioProcessorEditor* createEditor() override { return nullptr; }
            bool hasEditor() const override { return false; }
            int getNumPrograms() override { return 1; }
            int getCurrentProgram() override { return 0; }
            void setCurrentProgram (int) override {}
            const juce::String getProgramName (int) override { return {}; }
            void changeProgramName (int, const juce::String&) override {}
            void getStateInformation (juce::MemoryBlock&) override {}
            void setStateInformation (const void*, int) override {}
        } dummy;
        juce::AudioProcessorValueTreeState st (dummy, nullptr, "D", std::move (layout));
        for (const auto& p : allParamIds())
            if (auto* r = dynamic_cast<juce::RangedAudioParameter*> (st.getParameter (p)))
                m[p] = r->convertFrom0to1 (r->getDefaultValue());
        return m;
    }();
    const auto it = defaults.find (pid);
    return it != defaults.end() ? it->second : 0.0f;
}

DrumParams paramsFromValues (const float* x)
{
    DrumParams p;
    auto in = [] (float v, int n) { return juce::jlimit (0, n - 1, juce::roundToInt (v)); };
    for (int s = 0; s < numSlots; ++s)
    {
        auto& sp = p.slots[(size_t) s];
        const float* v = x + s * paramsPerSlot; // type model tune decay tone a b drive driveMode trim | level pan width filter rev dly out choke mute solo vel
        sp.type = (DrumType) in (v[0], numTypes);
        sp.model = in (v[1], 3);
        sp.tune = v[2];
        sp.decay = v[3];
        sp.tone = v[4];
        sp.a = v[5];
        sp.b = v[6];
        sp.drive = v[7];
        sp.driveMode = (DriveMode) in (v[8], 4);
        sp.trimDb = v[9];
        sp.levelDb = v[10];
        sp.pan = v[11];
        sp.width = v[12];
        sp.filter = v[13];
        sp.rev = v[14];
        sp.dly = v[15];
        sp.out = in (v[16], numOutputs);
        sp.choke = in (v[17], 5);
        sp.mute = v[18] > 0.5f;
        sp.solo = v[19] > 0.5f;
        sp.vel = v[20];
    }
    const float* g = x + numSlots * paramsPerSlot; // revSize revDamp revLevel dlyTime dlyFb dlyLevel glue mDrive volume analog
    p.revSize = g[0];
    p.revDamp = g[1];
    p.revLevel = juce::Decibels::decibelsToGain (g[2], -59.9f);
    p.dlyTime = in (g[3], numDelayTimes);
    p.dlyFb = g[4];
    p.dlyLevel = juce::Decibels::decibelsToGain (g[5], -59.9f);
    p.glue = g[6];
    p.mDrive = g[7];
    p.volumeDb = g[8];
    p.analog = g[9] > 0.5f;
    return p;
}

DrumParams paramsFrom (const std::function<float (const juce::String&, float)>& get)
{
    const auto& ids = allParamIds();
    std::vector<float> v ((size_t) ids.size());
    for (int i = 0; i < ids.size(); ++i)
        v[(size_t) i] = get (ids[i], defaultValue (ids[i]));
    return paramsFromValues (v.data());
}

ParamCache::ParamCache (const juce::AudioProcessorValueTreeState& state)
{
    const auto& ids = allParamIds();
    jassert (ids.size() == numParams);
    for (int i = 0; i < numParams; ++i)
        ptr[(size_t) i] = state.getRawParameterValue (ids[i]);
}

DrumParams ParamCache::read() const
{
    std::array<float, numParams> v;
    for (size_t i = 0; i < v.size(); ++i)
        v[i] = ptr[i] != nullptr ? ptr[i]->load() : 0.0f;
    return paramsFromValues (v.data());
}

DrumParams readParams (const juce::AudioProcessorValueTreeState& state)
{
    return paramsFrom ([&state] (const juce::String& pid, float def)
    {
        auto* v = state.getRawParameterValue (pid);
        return v != nullptr ? v->load() : def;
    });
}
}
