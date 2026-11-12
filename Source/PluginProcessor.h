#pragma once

#include <array>

#include <juce_audio_processors/juce_audio_processors.h>
#include "DSP/DrumEngine.h"
#include "Presets.h"

class PowerDrumProcessor : public juce::AudioProcessor
{
public:
    PowerDrumProcessor();
    ~PowerDrumProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;
    void reset() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "PowerDrum"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return getKitName(); }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    void loadKit (int index);
    void loadSound (int slot, int soundIndex);

    juce::AudioProcessorValueTreeState state;
    pd::KitLibrary kits;
    pd::SoundLibrary sounds;

    // Names read by the host and the editor from other threads: guarded.
    juce::String getKitName() const { const juce::SpinLock::ScopedLockType l (nameLock); return kitName; }
    void setKitName (const juce::String& n) { const juce::SpinLock::ScopedLockType l (nameLock); kitName = n; }
    juce::String getSlotName (int s) const { const juce::SpinLock::ScopedLockType l (nameLock); return slotNames[(size_t) juce::jlimit (0, pd::numSlots - 1, s)]; }
    void setSlotName (int s, const juce::String& n) { const juce::SpinLock::ScopedLockType l (nameLock); slotNames[(size_t) juce::jlimit (0, pd::numSlots - 1, s)] = n; }
    std::array<juce::String, pd::numSlots> getSlotNames() const { const juce::SpinLock::ScopedLockType l (nameLock); return slotNames; }

    // Pads in the editor: a click plays the slot (velocity 0..1). Picked up by the next audio block.
    void playSlot (int slot, float velocity = 0.9f) { if (slot >= 0 && slot < pd::numSlots) pendingHit[(size_t) slot].store (velocity); }
    // PLAY: a two-bar groove through the kit at the host tempo (140 BPM without a host tempo), while on.
    std::atomic<bool> grooveOn { false };
    std::atomic<int> selectedSlot { 0 };   // the slot shown in the editor (saved with the project)

    float slotFlash (int s) { return engine.slot (s).hitFlash.exchange (0.0f); }
    float slotLevel (int s) const { return engine.slot (s).level.load(); }
    bool outputActive (int bus) const;     // is OUT bus k (1..16) on in the host?

    std::atomic<float> meterL { 0.0f }, meterR { 0.0f };

private:
    void addGrooveNotes (juce::MidiBuffer& midi, int n, double bpm);

    juce::String kitName { "Init" };
    std::array<juce::String, pd::numSlots> slotNames;
    mutable juce::SpinLock nameLock;
    pd::ParamCache cache;
    pd::dsp::DrumEngine engine;
    std::array<std::atomic<float>, pd::numSlots> pendingHit {};
    double preparedRate = 48000.0;
    int grooveStep = 0;
    double toNextStep = 0.0;  // samples to the next 16th of the groove
    bool grooveWasOn = false;
    juce::AudioBuffer<float> scratch;   // a mono main output: rendered in stereo, then summed
    std::array<float*, pd::numOutputs> ptrL {}, ptrR {};
    std::array<std::atomic<bool>, pd::numOutputs> busActive {};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerDrumProcessor)
};
