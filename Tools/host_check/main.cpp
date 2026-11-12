// host_check <PowerDrum.component | PowerDrum.vst3>: loads the BUILT plug-in through its AU or VST3 interface, as a DAW
// does, and checks what matters for mixing: 17 stereo outputs can be switched on, a slot routed to OUT 4 comes out of
// output 4 and not MAIN, back on MAIN when routed back, every slot plays on its own note, audio is finite and below
// 0 dBFS at several rates and block sizes, a state round trip keeps the routing. Exit code 0 only if every check passed.
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include <cstdio>

namespace
{
int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::printf ("%s %s\n", ok ? "OK  " : "FAIL", what.toRawUTF8());
    std::fflush (stdout);
    if (! ok) ++failures;
}

juce::AudioProcessorParameter* findParam (juce::AudioPluginInstance& p, const juce::String& id)
{
    for (auto* prm : p.getParameters())
        if (auto* h = dynamic_cast<juce::HostedAudioProcessorParameter*> (prm))
            if (h->getParameterID() == id || prm->getName (64) == id) return prm;
    return nullptr;
}

// One hit of a note; the peak of every output bus.
std::vector<float> hit (juce::AudioPluginInstance& p, int note, double sr, int block)
{
    const int ch = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> buf (std::max (2, ch), block);
    std::vector<float> pk ((size_t) p.getBusCount (false), 0.0f);
    const int total = (int) (0.6 * sr);
    for (int pos = 0; pos < total; pos += block)
    {
        juce::MidiBuffer m;
        if (pos == 0) m.addEvent (juce::MidiMessage::noteOn (10, note, (juce::uint8) 120), 0);
        buf.clear();
        p.processBlock (buf, m);
        for (int b = 0; b < p.getBusCount (false); ++b)
            if (auto* bus = p.getBus (false, b); bus != nullptr && bus->isEnabled())
                for (int c = 0; c < bus->getNumberOfChannels(); ++c)
                {
                    const int idx = bus->getChannelIndexInProcessBlockBuffer (c);
                    if (idx < buf.getNumChannels())
                    {
                        const float mag = buf.getMagnitude (idx, 0, block);
                        pk[(size_t) b] = std::max (pk[(size_t) b], std::isfinite (mag) ? mag : 1.0e9f);
                    }
                }
    }
    return pk;
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc < 2) { std::printf ("usage: host_check <plug-in>\n"); return 2; }
    const juce::String path = juce::String::fromUTF8 (argv[1]);
    juce::AudioPluginFormatManager fm;
    juce::addDefaultFormatsToManager (fm);
    juce::OwnedArray<juce::PluginDescription> found;
    for (auto* f : fm.getFormats())
        if (f->fileMightContainThisPluginType (path))
            f->findAllTypesForFile (found, path);
    check (found.size() == 1, "one instrument in " + path.fromLastOccurrenceOf ("/", false, false) + " (" + juce::String (found.size()) + ")");
    if (found.isEmpty()) return 1;
    check (found[0]->isInstrument && found[0]->name == "PowerDrum", "it is the PowerDrum instrument (" + found[0]->pluginFormatName + ")");
    juce::String err;
    auto inst = fm.createPluginInstance (*found[0], 48000.0, 512, err);
    check (inst != nullptr, "instance created " + err);
    if (inst == nullptr) return 1;

    check (inst->getBusCount (false) == 17, "17 output buses (" + juce::String (inst->getBusCount (false)) + ")");
    auto layout = inst->getBusesLayout();
    for (auto& b : layout.outputBuses) b = juce::AudioChannelSet::stereo();
    check (inst->setBusesLayout (layout), "all 17 outputs on as stereo (the host's Multi Output)");
    check (inst->getTotalNumOutputChannels() == 34, "34 output channels (" + juce::String (inst->getTotalNumOutputChannels()) + ")");

    for (double sr : { 44100.0, 48000.0, 96000.0 })
        for (int block : { 64, 512 })
        {
            inst->setRateAndBufferSizeDetails (sr, block);
            inst->prepareToPlay (sr, block);
            bool allSlots = true, finite = true;
            for (int note = 36; note <= 51; ++note)
            {
                const auto pk = hit (*inst, note, sr, block);
                allSlots = allSlots && pk[0] > 0.003f;
                finite = finite && pk[0] < 1.0f;
            }
            check (allSlots && finite, "every slot plays on its note, below 0 dBFS (" + juce::String (sr) + " Hz, block " + juce::String (block) + ")");
            inst->releaseResources();
        }
    inst->prepareToPlay (48000.0, 512);
    auto* out4 = findParam (*inst, "s4_out");
    check (out4 != nullptr, "the OUTPUT parameter of slot 4 is visible to the host");
    if (out4 != nullptr)
    {
        auto pk = hit (*inst, 39, 48000.0, 512);
        check (pk[0] > 0.01f && pk[4] < 1.0e-6f, "default: the clap (D#1) on MAIN");
        out4->setValueNotifyingHost (4.0f / 16.0f);
        pk = hit (*inst, 39, 48000.0, 512);
        check (pk[4] > 0.01f && pk[0] < 1.0e-6f, "slot 4 -> OUT 4: heard on output 4 only (" + juce::String (pk[4], 3) + " / MAIN " + juce::String (pk[0], 6) + ")");
        juce::MemoryBlock state;
        inst->getStateInformation (state);
        out4->setValueNotifyingHost (0.0f);
        pk = hit (*inst, 39, 48000.0, 512);
        check (pk[0] > 0.01f && pk[4] < 1.0e-6f, "back to MAIN");
        inst->setStateInformation (state.getData(), (int) state.getSize());
        pk = hit (*inst, 39, 48000.0, 512);
        check (pk[4] > 0.01f && pk[0] < 1.0e-6f, "a saved project brings the routing back (OUT 4)");
        out4->setValueNotifyingHost (0.0f);
    }
    // Every slot on its own output at once (MULTI OUT).
    int separate = 0;
    for (int s = 1; s <= 16; ++s)
        if (auto* o = findParam (*inst, "s" + juce::String (s) + "_out")) o->setValueNotifyingHost ((float) s / 16.0f);
    for (int s = 0; s < 16; ++s)
    {
        const auto pk = hit (*inst, 36 + s, 48000.0, 512);
        separate += (pk[(size_t) (s + 1)] > 0.003f && pk[0] < 1.0e-6f) ? 1 : 0;
    }
    check (separate == 16, "MULTI: each of the 16 slots on its own output (" + juce::String (separate) + ")");
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (inst->createEditorIfNeeded());
        check (ed != nullptr && ed->getWidth() > 600, "the editor opens");
    }
    inst->releaseResources();
    inst.reset();
    std::printf (failures == 0 ? "HOST CHECK PASSED\n" : "HOST CHECK FAILED (%d)\n", failures);
    return failures == 0 ? 0 : 1;
}
