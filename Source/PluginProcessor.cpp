#include "PluginProcessor.h"
#include "PluginEditor.h"
#include "Groove.h"

namespace
{
juce::AudioProcessor::BusesProperties makeBuses()
{
    // MAIN + OUT 1..16. In AU every bus is always on (Logic: "Multi Output (17xStereo)" gives each its own channel);
    // in VST3 hosts the extra outputs start off, and a slot routed to an output that is off plays on MAIN.
    auto b = juce::AudioProcessor::BusesProperties().withOutput ("Main", juce::AudioChannelSet::stereo(), true);
    for (int k = 1; k <= pd::numSlots; ++k)
        b = b.withOutput ("Out " + juce::String (k), juce::AudioChannelSet::stereo(), false);
    return b;
}

}

PowerDrumProcessor::PowerDrumProcessor()
    : AudioProcessor (makeBuses()),
      state (*this, nullptr, "PowerDrum", pd::createLayout()),
      cache (state)
{
    for (auto& h : pendingHit) h.store (0.0f);
    for (auto& b : busActive) b.store (false);
    if (! kits.all().empty())
        loadKit (0);
}

bool PowerDrumProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    if (layouts.outputBuses.isEmpty())
        return false;
    const auto main = layouts.outputBuses[0];
    if (main != juce::AudioChannelSet::stereo() && main != juce::AudioChannelSet::mono())
        return false;
    for (int i = 1; i < layouts.outputBuses.size(); ++i)
        if (! layouts.outputBuses[i].isDisabled() && layouts.outputBuses[i] != juce::AudioChannelSet::stereo())
            return false;
    return layouts.inputBuses.isEmpty();
}

void PowerDrumProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    preparedRate = sampleRate;
    engine.prepare (sampleRate, samplesPerBlock);
    scratch.setSize (2, std::max (512, samplesPerBlock));
    setLatencySamples (pd::dsp::DrumEngine::latencySamples());
    grooveStep = 0;
    toNextStep = 0.0;
    grooveWasOn = false;
}

void PowerDrumProcessor::reset()
{
    // AU Reset: clear every hit and every tail.
    engine.reset();
    meterL.store (0.0f);
    meterR.store (0.0f);
}

void PowerDrumProcessor::releaseResources() { reset(); }

bool PowerDrumProcessor::outputActive (int bus) const
{
    return bus > 0 && bus < pd::numOutputs && busActive[(size_t) bus].load();
}

void PowerDrumProcessor::addGrooveNotes (juce::MidiBuffer& midi, int n, double bpm)
{
    const bool on = grooveOn.load();
    if (on && ! grooveWasOn) { grooveStep = 0; toNextStep = 0.0; }
    grooveWasOn = on;
    if (! on) return;
    const double samplesPerStep = preparedRate * 60.0 / juce::jlimit (40.0, 300.0, bpm) / 4.0;
    for (int i = 0; i < n; ++i)
    {
        while (toNextStep <= 0.0)
        {
            for (int s = 0; s < pd::numSlots; ++s)
                if (const float v = pd::grooveVelocity (s, grooveStep); v > 0.0f)
                    midi.addEvent (juce::MidiMessage::noteOn (10, pd::firstNote + s, v), i);
            grooveStep = (grooveStep + 1) % pd::grooveSteps;
            toNextStep += samplesPerStep;
        }
        toNextStep -= 1.0;
    }
}

void PowerDrumProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    const int n = buffer.getNumSamples();
    auto params = cache.read();
    if (auto* ph = getPlayHead())
        if (const auto pos = ph->getPosition())
            if (const auto bpm = pos->getBpm())
                params.bpm = *bpm;

    for (int s = 0; s < pd::numSlots; ++s) // pad clicks in the editor
        if (const float v = pendingHit[(size_t) s].exchange (0.0f); v > 0.0f)
            midi.addEvent (juce::MidiMessage::noteOn (10, pd::firstNote + s, v), 0);
    addGrooveNotes (midi, n, params.bpm);

    // One pointer pair per output; an output that is off (or missing) is null and its slots play on MAIN.
    ptrL.fill (nullptr); ptrR.fill (nullptr);
    const int buses = std::min (getBusCount (false), pd::numOutputs);
    bool monoMain = false;
    for (int b = 0; b < buses; ++b)
    {
        const auto* bus = getBus (false, b);
        const bool on = bus != nullptr && bus->isEnabled() && bus->getNumberOfChannels() > 0;
        busActive[(size_t) b].store (on && bus->getNumberOfChannels() == 2);
        if (! on) continue;
        const int first = bus->getChannelIndexInProcessBlockBuffer (0);
        if (bus->getNumberOfChannels() == 2 && first + 1 < buffer.getNumChannels())
        {
            ptrL[(size_t) b] = buffer.getWritePointer (first);
            ptrR[(size_t) b] = buffer.getWritePointer (first + 1);
        }
        else if (b == 0 && first < buffer.getNumChannels())
            monoMain = true;
    }
    if (monoMain || ptrL[0] == nullptr)
    {
        // A mono main output: render MAIN into the scratch pair (in parts if the host block is larger) and sum it.
        float* mono = buffer.getNumChannels() > 0 ? buffer.getWritePointer (0) : nullptr;
        for (int done = 0; done < n;)
        {
            const int len = std::min (n - done, scratch.getNumSamples());
            std::array<float*, pd::numOutputs> l = ptrL, r = ptrR;
            l[0] = scratch.getWritePointer (0); r[0] = scratch.getWritePointer (1);
            for (int b = 1; b < pd::numOutputs; ++b)
                if (l[(size_t) b] != nullptr) { l[(size_t) b] += done; r[(size_t) b] += done; }
            engine.process (l.data(), r.data(), buses, len, midi, params, done);
            if (mono != nullptr)
                for (int i = 0; i < len; ++i) mono[done + i] = 0.5f * (l[0][i] + r[0][i]);
            done += len;
        }
    }
    else
        engine.process (ptrL.data(), ptrR.data(), buses, n, midi, params);

    // Channels of outputs that are off are cleared (the host may read them).
    for (int b = 1; b < buses; ++b)
        if (ptrL[(size_t) b] == nullptr)
            if (const auto* bus = getBus (false, b); bus != nullptr && bus->isEnabled())
                for (int c = 0; c < bus->getNumberOfChannels(); ++c)
                    if (const int ch = bus->getChannelIndexInProcessBlockBuffer (c); ch < buffer.getNumChannels())
                        buffer.clear (ch, 0, n);

    auto peak = [n] (const float* x)
    {
        if (x == nullptr) return 0.0f;
        const auto range = juce::FloatVectorOperations::findMinAndMax (x, n);
        return std::max (std::abs (range.getStart()), std::abs (range.getEnd()));
    };
    const float* ml = buffer.getNumChannels() > 0 ? buffer.getReadPointer (0) : nullptr;
    const float* mr = buffer.getNumChannels() > 1 && ! monoMain ? buffer.getReadPointer (1) : ml;
    meterL.store (std::max (peak (ml), meterL.load() * 0.85f));
    meterR.store (std::max (peak (mr), meterR.load() * 0.85f));
}

void PowerDrumProcessor::loadKit (int index)
{
    const auto& all = kits.all();
    if (index < 0 || index >= (int) all.size())
        return;
    const auto& k = all[(size_t) index];
    pd::applyKit (state, k.values);
    setKitName (k.name);
    for (int s = 0; s < pd::numSlots; ++s)
        setSlotName (s, k.slotNames[(size_t) s]);
}

void PowerDrumProcessor::loadSound (int slot, int soundIndex)
{
    const auto& all = sounds.all();
    if (slot < 0 || slot >= pd::numSlots || soundIndex < 0 || soundIndex >= (int) all.size())
        return;
    pd::applySound (state, slot, all[(size_t) soundIndex].values);
    setSlotName (slot, all[(size_t) soundIndex].name);
}

juce::AudioProcessorEditor* PowerDrumProcessor::createEditor() { return new PowerDrumEditor (*this); }

void PowerDrumProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto tree = state.copyState();
    tree.setProperty ("kitName", getKitName(), nullptr);
    const auto names = getSlotNames();
    for (int s = 0; s < pd::numSlots; ++s)
        tree.setProperty ("slotName" + juce::String (s + 1), names[(size_t) s], nullptr);
    tree.setProperty ("selectedSlot", selectedSlot.load(), nullptr);
    if (auto xml = tree.createXml())
        copyXmlToBinary (*xml, destData);
}

void PowerDrumProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (state.state.getType()))
        {
            auto tree = juce::ValueTree::fromXml (*xml);
            setKitName (tree.getProperty ("kitName", getKitName()).toString());
            for (int s = 0; s < pd::numSlots; ++s)
                setSlotName (s, tree.getProperty ("slotName" + juce::String (s + 1), getSlotName (s)).toString());
            selectedSlot.store (juce::jlimit (0, pd::numSlots - 1, (int) tree.getProperty ("selectedSlot", 0)));
            state.replaceState (tree);
        }
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new PowerDrumProcessor(); }
