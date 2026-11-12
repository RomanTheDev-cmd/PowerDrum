// PowerDrum wrapper tests: processor, buses, state, kits and sounds through the plug-in's shared code.
//   powerdrum_wrapper_tests                     all checks
//   powerdrum_wrapper_tests --snapshot <dir>    main.png, browser.png, sound_menu.png, main_small.png, main_large.png
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include <iostream>
#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
int checks = 0, failures = 0;
void expect (bool ok, const juce::String& what)
{
    ++checks;
    if (! ok) { ++failures; std::cout << "FAIL: " << what << "\n"; }
}

void setPlain (PowerDrumProcessor& p, const juce::String& id, float v)
{
    auto* prm = dynamic_cast<juce::RangedAudioParameter*> (p.state.getParameter (id));
    prm->setValueNotifyingHost (prm->convertTo0to1 (v));
}
float plain (PowerDrumProcessor& p, const juce::String& id) { return p.state.getRawParameterValue (id)->load(); }

// Plays one note through processBlock with all 17 outputs in the buffer; returns the peak per output.
std::vector<float> playNote (PowerDrumProcessor& p, int note, int blocks = 40)
{
    p.reset();   // no tail of the previous hit (a reverb return) in this measurement
    const int ch = p.getTotalNumOutputChannels();
    juce::AudioBuffer<float> b (ch, 512);
    std::vector<float> pk ((size_t) pd::numOutputs, 0.0f);
    for (int i = 0; i < blocks; ++i)
    {
        juce::MidiBuffer m;
        if (i == 0) m.addEvent (juce::MidiMessage::noteOn (10, note, 1.0f), 0);
        b.clear();
        p.processBlock (b, m);
        for (int o = 0; o < pd::numOutputs; ++o)
            if (const auto* bus = p.getBus (false, o); bus != nullptr && bus->isEnabled())
                for (int c = 0; c < bus->getNumberOfChannels(); ++c)
                    pk[(size_t) o] = std::max (pk[(size_t) o], b.getMagnitude (bus->getChannelIndexInProcessBlockBuffer (c), 0, 512));
    }
    return pk;
}

void testBuses()
{
    PowerDrumProcessor p;
    expect (p.getBusCount (false) == pd::numOutputs, "17 stereo outputs (MAIN + OUT 1..16)");
    expect (p.getBusCount (true) == 0, "no inputs");
    // Default: only MAIN on (a VST3 host turns the others on when asked).
    expect (p.getBus (false, 0)->isEnabled() && ! p.getBus (false, 5)->isEnabled(), "only MAIN on by default");
    // All on (AU always, a VST3 host on request).
    p.enableAllBuses();
    expect (p.getTotalNumOutputChannels() == 2 * pd::numOutputs, "all outputs on: 34 channels");
    p.prepareToPlay (48000.0, 512);
    expect (p.getLatencySamples() == pd::dsp::DrumEngine::latencySamples(), "latency reported");
    // Default routing: every slot on MAIN.
    auto pk = playNote (p, 39);
    expect (pk[0] > 0.01f && pk[4] == 0.0f, "default: the clap (D#1) plays on MAIN");
    for (int s = 0; s < pd::numSlots; ++s) // no sends: their returns are on MAIN by design
    {
        setPlain (p, pd::slotId (s, pd::id::rev), 0.0f);
        setPlain (p, pd::slotId (s, pd::id::dly), 0.0f);
    }
    pd::setMultiOut (p.state, true);
    expect (pd::isMultiOut (p.state), "MULTI sets every slot to its own output");
    pk = playNote (p, 39);
    expect (pk[4] > 0.01f && pk[0] < 1.0e-6f, "MULTI: the clap (slot 4) plays on OUT 4 only");
    pk = playNote (p, 36);
    expect (pk[1] > 0.01f, "MULTI: the kick (slot 1) plays on OUT 1");
    pd::setMultiOut (p.state, false);
    pk = playNote (p, 39);
    expect (pk[0] > 0.01f && pk[4] == 0.0f, "back to MAIN");

    // Only MAIN on: a slot routed to OUT 4 falls back to MAIN.
    PowerDrumProcessor q;
    q.prepareToPlay (48000.0, 512);
    setPlain (q, pd::slotId (3, pd::id::out), 4.0f);
    pk = playNote (q, 39);
    expect (pk[0] > 0.01f, "an output that is off: its slot plays on MAIN");

    // Layouts: mono or stereo MAIN; extras stereo or off.
    auto layout = q.getBusesLayout();
    expect (q.checkBusesLayoutSupported (layout), "default layout supported");
    layout.outputBuses.getReference (0) = juce::AudioChannelSet::mono();
    expect (q.checkBusesLayoutSupported (layout), "mono MAIN supported");
    layout.outputBuses.getReference (3) = juce::AudioChannelSet::create5point1();
    expect (! q.checkBusesLayoutSupported (layout), "5.1 extra output refused");
}

void testStateRoundTrip()
{
    PowerDrumProcessor a;
    a.prepareToPlay (48000.0, 512);
    setPlain (a, pd::slotId (2, pd::id::tune), 5.0f);
    setPlain (a, pd::slotId (7, pd::id::out), 3.0f);
    setPlain (a, pd::id::analog, 1.0f);
    a.setSlotName (2, "My Snare");
    a.selectedSlot = 7;
    juce::MemoryBlock m;
    a.getStateInformation (m);
    PowerDrumProcessor b;
    b.prepareToPlay (48000.0, 512);
    b.setStateInformation (m.getData(), (int) m.getSize());
    expect (std::abs (plain (b, pd::slotId (2, pd::id::tune)) - 5.0f) < 0.01f && plain (b, pd::slotId (7, pd::id::out)) == 3.0f
                && plain (b, pd::id::analog) == 1.0f, "state round trip keeps sounds, routing and the switch");
    expect (b.getSlotName (2) == "My Snare" && b.getKitName() == a.getKitName() && b.selectedSlot.load() == 7, "state keeps names and the selected slot");
}

void testKitsAndSounds()
{
    PowerDrumProcessor p;
    p.prepareToPlay (48000.0, 512);
    const auto& all = p.kits.all();
    expect (! all.empty() && p.getKitName() == all[0].name, "a fresh instance shows the first kit");
    int ok = 0;
    for (int i = 0; i < (int) all.size(); ++i)
    {
        p.loadKit (i);
        bool same = p.getKitName() == all[(size_t) i].name;
        for (const auto& [k, v] : all[(size_t) i].values)
            if (! k.endsWith ("_out") && k != pd::id::analog)
                same = same && std::abs (plain (p, k) - v) < 0.01f * std::max (1.0f, std::abs (v));
        for (int s = 0; s < pd::numSlots; ++s) same = same && p.getSlotName (s) == all[(size_t) i].slotNames[(size_t) s];
        ok += same ? 1 : 0;
    }
    expect (ok == (int) all.size(), "every factory kit loads its values and names (" + juce::String (ok) + " of " + juce::String ((int) all.size()) + ")");
    // A kit change keeps the routing and DIGITAL | ANALOGUE.
    pd::setMultiOut (p.state, true);
    setPlain (p, pd::id::analog, 1.0f);
    p.loadKit (0);
    expect (pd::isMultiOut (p.state) && plain (p, pd::id::analog) == 1.0f, "a kit change keeps OUT routing and ANALOGUE");
    // A sound loaded into a slot changes its sound, not its mix.
    setPlain (p, pd::slotId (5, pd::id::level), -7.0f);
    setPlain (p, pd::slotId (5, pd::id::pan), 0.4f);
    const auto& sounds = p.sounds.all();
    int clap = -1;
    for (int i = 0; i < (int) sounds.size() && clap < 0; ++i) if (sounds[(size_t) i].type == pd::DrumType::clap) clap = i;
    expect (clap >= 0, "the sound library has claps");
    p.loadSound (5, clap);
    expect (juce::roundToInt (plain (p, pd::slotId (5, pd::id::type))) == (int) pd::DrumType::clap, "a clap loaded into slot 6");
    expect (std::abs (plain (p, pd::slotId (5, pd::id::level)) + 7.0f) < 0.01f && std::abs (plain (p, pd::slotId (5, pd::id::pan)) - 0.4f) < 0.01f,
            "loading a sound keeps the slot's level and pan");
    expect (p.getSlotName (5) == sounds[(size_t) clap].name, "the slot takes the sound's name");
}

void testEditor()
{
    PowerDrumProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    expect (ed != nullptr, "editor opens");
    for (auto [w, h] : { std::pair { 900, 600 }, std::pair { 1200, 800 }, std::pair { 1650, 1100 } })
    {
        ed->setSize (w, h);
        // no child component outside the window
        bool inside = true;
        for (auto* c : ed->getChildren())
            if (c->isVisible() && ! ed->getLocalBounds().contains (c->getBounds())) { inside = false; std::cout << "  outside: " << c->getComponentID() << " " << c->getBounds().toString() << "\n"; }
        expect (inside, "editor " + juce::String (w) + "x" + juce::String (h) + ": everything inside the window");
    }
}

void snapshot (const juce::File& dir)
{
    dir.createDirectory();
    PowerDrumProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed (p.createEditor());
    auto save = [&dir] (juce::Component& c, const juce::String& name)
    {
        const auto img = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::FileOutputStream out (dir.getChildFile (name));
        out.setPosition (0); out.truncate();
        juce::PNGImageFormat().writeImageToStream (img, out);
    };
    auto* editor = dynamic_cast<PowerDrumEditor*> (ed.get());
    ed->setSize (1080, 720);
    editor->selectSlot (0);
    save (*ed, "main.png");
    editor->selectSlot (6);
    save (*ed, "main_hat.png");
    if (auto* kb = ed->findChildWithID ("kitButton"))
        if (auto* b = dynamic_cast<juce::Button*> (kb)) { b->onClick(); save (*ed, "browser.png"); b->onClick(); }
    pd::setMultiOut (p.state, true);
    editor->selectSlot (3);
    save (*ed, "main_multi.png");
    ed->setSize (864, 576); save (*ed, "main_small.png");
    ed->setSize (1620, 1080); save (*ed, "main_large.png");
}
}

int main (int argc, char** argv)
{
    juce::ScopedJuceInitialiser_GUI init;
    if (argc > 2 && juce::String (argv[1]) == "--snapshot")
    {
        snapshot (juce::File::getCurrentWorkingDirectory().getChildFile (juce::String::fromUTF8 (argv[2])));
        return 0;
    }
    testBuses();
    testStateRoundTrip();
    testKitsAndSounds();
    testEditor();
    std::cout << (failures == 0 ? "PASS " : "FAILED ") << (checks - failures) << "/" << checks << " checks\n";
    return failures == 0 ? 0 : 1;
}
