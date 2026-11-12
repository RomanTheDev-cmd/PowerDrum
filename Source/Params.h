#pragma once

#include <array>
#include <functional>
#include <juce_audio_processors/juce_audio_processors.h>

// PowerDrum (Roman, 2026-11-12): a drum machine of the MyDarknessDays series for hard techno, schranz and industrial.
// 16 slots on the notes C1..D#2 (36..51, laid out like General MIDI); any slot can hold any drum type. Every slot has
// its own sound (TYPE, MODEL, six knobs), its own mix (level, pan, width, filter, reverb and delay sends) and its own
// output: MAIN or one of 16 extra stereo outputs, so each drum can go to its own channel of the DAW mixer.
namespace pd
{
inline constexpr int numSlots = 16;
inline constexpr int firstNote = 36;            // slot 0 = C1 ... slot 15 = D#2
inline constexpr int numOutputs = numSlots + 1; // MAIN + OUT 1..16 (bus k carries slot k-1 in MULTI)

enum class DrumType { kick, snare, clap, hat, cymbal, tom, perc, rumble, fx };
inline constexpr int numTypes = 9;
enum class DriveMode { saturate, clip, fold, crush };

const juce::StringArray& typeNames();                 // KICK SNARE CLAP HAT CYMBAL TOM PERC RUMBLE FX
const juce::StringArray& modelNames (DrumType);       // three per type
juce::String knobAName (DrumType);                    // the two knobs whose meaning depends on the type
juce::String knobBName (DrumType);
juce::String noteName (int slot);                     // "C1" ...

// Parameter IDs. Slot parameters are "s<1..16>_<name>" (s1_type, s1_tune, ...).
namespace id
{
    // per slot: the sound
    inline constexpr const char* type      = "type";
    inline constexpr const char* model     = "model";
    inline constexpr const char* tune      = "tune";
    inline constexpr const char* decay     = "decay";
    inline constexpr const char* tone      = "tone";
    inline constexpr const char* knobA     = "a";
    inline constexpr const char* knobB     = "b";
    inline constexpr const char* drive     = "drive";
    inline constexpr const char* driveMode = "driveMode";
    inline constexpr const char* trim      = "trim";      // the sound's own gain (fitted: 0 dB level = peak about -1 dBFS)
    // per slot: the mix
    inline constexpr const char* level     = "level";
    inline constexpr const char* pan       = "pan";
    inline constexpr const char* width     = "width";
    inline constexpr const char* filter    = "filter";    // -1 low-pass .. 0 off .. +1 high-pass
    inline constexpr const char* rev       = "rev";
    inline constexpr const char* dly       = "dly";
    inline constexpr const char* out       = "out";       // 0 MAIN, 1..16 OUT 1..16
    inline constexpr const char* choke     = "choke";     // 0 off, 1..4 groups A..D
    inline constexpr const char* mute      = "mute";
    inline constexpr const char* solo      = "solo";
    inline constexpr const char* vel       = "vel";       // velocity sensitivity
    // global
    inline constexpr const char* revSize   = "revSize";
    inline constexpr const char* revDamp   = "revDamp";
    inline constexpr const char* revLevel  = "revLevel";
    inline constexpr const char* dlyTime   = "dlyTime";
    inline constexpr const char* dlyFb     = "dlyFb";
    inline constexpr const char* dlyLevel  = "dlyLevel";
    inline constexpr const char* glue      = "glue";
    inline constexpr const char* mDrive    = "mDrive";
    inline constexpr const char* volume    = "volume";
    inline constexpr const char* analog    = "analog";
}

// "s3_tune" for slot index 2.
juce::String slotId (int slot, const char* name);
// Sound parameters of a slot (what a sound from the library sets) and mix parameters (kept when a sound is loaded).
const std::vector<const char*>& soundParamNames();
const std::vector<const char*>& mixParamNames();

// Delay note values at the host tempo, in beats.
inline constexpr int numDelayTimes = 6;
inline constexpr float delayBeats[numDelayTimes] = { 0.25f, 1.0f / 3.0f, 0.5f, 0.75f, 1.0f, 1.5f };
const juce::StringArray& delayTimeNames(); // 1/16 1/8T 1/8 3/16 1/4 3/8

struct SlotParams
{
    DrumType type = DrumType::kick;
    int model = 0;
    float tune = 0.0f;       // semitones around the type's base pitch, -24..+24
    float decay = 0.5f;      // 0..1 (each type maps it to its own time range)
    float tone = 0.5f;
    float a = 0.5f, b = 0.5f;
    float drive = 0.0f;
    DriveMode driveMode = DriveMode::saturate;
    float trimDb = 0.0f;
    float levelDb = 0.0f;
    float pan = 0.0f, width = 0.0f, filter = 0.0f, rev = 0.0f, dly = 0.0f;
    int out = 0, choke = 0;
    bool mute = false, solo = false;
    float vel = 0.6f;
};

struct DrumParams
{
    std::array<SlotParams, numSlots> slots {};
    float revSize = 0.5f, revDamp = 0.5f, revLevel = 0.0f;   // dB of the return, as 0..1 here (see Params.cpp)
    int dlyTime = 2;
    float dlyFb = 0.35f, dlyLevel = 0.0f;
    float glue = 0.0f, mDrive = 0.0f, volumeDb = 0.0f;
    bool analog = false;
    double bpm = 140.0;      // host tempo (not a parameter)
};

// The default kit layout of a fresh instance, as General MIDI places these drums.
DrumType defaultType (int slot);

const juce::StringArray& allParamIds();
inline constexpr int paramsPerSlot = 21;                       // soundParamNames() then mixParamNames()
inline constexpr int numParams = numSlots * paramsPerSlot + 10; // then the ten global ones
juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
DrumParams readParams (const juce::AudioProcessorValueTreeState& state);
// Plain values in allParamIds() order -> DrumParams (no strings: safe on the audio thread).
DrumParams paramsFromValues (const float* values);

// The parameters of a processor, looked up once: read() runs on the audio thread without strings or allocation.
class ParamCache
{
public:
    explicit ParamCache (const juce::AudioProcessorValueTreeState&);
    DrumParams read() const;
private:
    std::array<std::atomic<float>*, numParams> ptr {};
};
// The one mapping from plain values to DrumParams; get (id, default) returns the plain value of a parameter.
DrumParams paramsFrom (const std::function<float (const juce::String&, float)>& get);
float defaultValue (const juce::String& paramId);
}
