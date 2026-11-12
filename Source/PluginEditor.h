#pragma once

#include "PluginProcessor.h"

namespace pd::ui
{
namespace colours
{
    // Black with a single red accent; the same family as the other plug-ins of the series (PowerKick warm white,
    // PowerBass orange, PowerSynth green, PowerComp blue).
    inline const juce::Colour bgTop    { 0xff1d1a1a };
    inline const juce::Colour bgBottom { 0xff080606 };
    inline const juce::Colour panelTop { 0xff252020 };
    inline const juce::Colour panelBot { 0xff120f0f };
    inline const juce::Colour line     { 0x1affc4c4 };
    inline const juce::Colour text     { 0xffefe7e6 };
    inline const juce::Colour dim      { 0xffa69796 };
    inline const juce::Colour faint    { 0xff665958 };
    inline const juce::Colour accent   { 0xffff3341 };
    inline const juce::Colour ink      { 0xff0e0909 };
    inline const juce::Colour hot      { 0xffffc44d };
}

juce::Font uiFont (float size, bool bold = false);
float uiScale (const juce::Component&);   // 1080 px wide = 1.0

class LookAndFeel : public juce::LookAndFeel_V4
{
public:
    LookAndFeel();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
    juce::Font getTextButtonFont (juce::TextButton&, int) override;
    juce::Font getPopupMenuFont() override;
};

// A knob with its name above and its value below. It can be re-bound to another parameter (the editor shows one slot
// at a time), and its title and value text may depend on the slot's drum type.
class Knob : public juce::Component, public juce::SettableTooltipClient
{
public:
    Knob (juce::AudioProcessorValueTreeState&, const juce::String& title, const juce::String& tooltip);
    void bind (const juce::String& paramId);
    void setTitle (const juce::String& t) { if (t != title) { title = t; repaint(); } }
    void setPrimary (bool);
    bool isPrimary() const { return (bool) slider.getProperties()["primary"]; }
    std::function<juce::String (double)> valueText;   // overrides the parameter's text
    void resized() override;
    void paint (juce::Graphics&) override;
    juce::Slider slider;

private:
    int labelHeight() const;
    juce::AudioProcessorValueTreeState& state;
    juce::String title, boundId;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
};

// Row of buttons bound to a choice (or bool) parameter; can be re-bound.
class Segment : public juce::Component, public juce::SettableTooltipClient, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    Segment (juce::AudioProcessorValueTreeState&, const juce::StringArray& labels, const juce::String& tooltip);
    ~Segment() override;
    void bind (const juce::String& paramId);
    void setLabels (const juce::StringArray& labels);
    std::function<void (int)> onChoose;   // instead of setting the parameter (TYPE loads a sound)
    void resized() override;

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    juce::AudioProcessorValueTreeState& state;
    juce::String paramId;
    juce::OwnedArray<juce::TextButton> buttons;
};

// A choice parameter as a button showing "OUT  MAIN"; a click opens the list.
class ChoiceButton : public juce::TextButton, private juce::AudioProcessorValueTreeState::Listener, private juce::AsyncUpdater
{
public:
    ChoiceButton (juce::AudioProcessorValueTreeState&, const juce::String& caption, const juce::String& tooltip);
    ~ChoiceButton() override;
    void bind (const juce::String& paramId);
    std::function<juce::String (int)> itemText;   // list text per index (default: the parameter's choices)

private:
    void parameterChanged (const juce::String&, float) override { triggerAsyncUpdate(); }
    void handleAsyncUpdate() override;
    void clicked() override;
    juce::AudioProcessorValueTreeState& state;
    juce::String caption, paramId;
};

// The current slot's sound as a waveform; a click plays the slot.
class WaveformView : public juce::Component, public juce::SettableTooltipClient
{
public:
    WaveformView();
    std::function<void()> onClick;
    void setAudio (const juce::AudioBuffer<float>& mono, double sampleRate, double windowMs);
    void flash() { flashLevel = 1.0f; repaint(); }
    void decay();
    void paint (juce::Graphics&) override;
    void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
    void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp (const juce::MouseEvent&) override;

private:
    std::vector<float> minV, maxV;
    double lengthMs = 0.0, window = 1000.0;
    float flashLevel = 0.0f;
    bool hover = false;
};

class Meter : public juce::Component
{
public:
    void setLevels (float l, float r);
    void paint (juce::Graphics&) override;

private:
    float levels[2] {}, held[2] {};
    int holdFrames[2] {};
};

// One pad: note, type, sound name; lights up on every hit; selected = accent frame. Click plays and selects.
class Pad : public juce::Component, public juce::SettableTooltipClient
{
public:
    Pad (PowerDrumProcessor&, int slot);
    void refresh();                 // name, type, mute/solo/out (timer)
    void hit (float amount) { glow = std::max (glow, amount); }
    void decay();
    void setSelected (bool s) { if (s != selected) { selected = s; repaint(); } }
    std::function<void (int, bool)> onPress;   // slot, play
    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;

private:
    PowerDrumProcessor& proc;
    int slot;
    float glow = 0.0f;
    bool selected = false, muted = false, soloed = false;
    int out = 0, type = 0;
    juce::String name;
};

void drawStar (juce::Graphics&, juce::Rectangle<float> box, bool filled, juce::Colour, float stroke);

class StarButton : public juce::TextButton
{
public:
    void setStarred (bool on) { if (on != starred) { starred = on; repaint(); } }
    bool isStarred() const { return starred; }
    void paintButton (juce::Graphics&, bool over, bool down) override;

private:
    bool starred = false;
};

// Kit browser (as the preset browser of the other plug-ins): styles on the left, search and groups on top.
class KitBrowser : public juce::Component, private juce::ListBoxModel
{
public:
    explicit KitBrowser (PowerDrumProcessor&);
    ~KitBrowser() override;
    void open();
    void setCategory (const juce::String&);
    void toggleGroup (const juce::String&);
    void setSearch (const juce::String&);
    void choose (int row, bool close);
    void toggleFavourite (int row);
    void favouritesChanged();
    int numShown() const { return (int) shown.size(); }

    bool keyPressed (const juce::KeyPress&) override;
    void paint (juce::Graphics&) override;
    void paintOverChildren (juce::Graphics&) override;
    void resized() override;

    std::function<void()> onLoaded, onClose, onFavouritesChanged;

private:
    struct StarSpot : juce::Component, juce::SettableTooltipClient
    {
        explicit StarSpot (KitBrowser&);
        bool hitTest (int x, int) override { return row >= 0 && row < owner.numShown() && x < owner.starWidth(); }
        void mouseDown (const juce::MouseEvent& e) override { if (e.getNumberOfClicks() == 1) owner.toggleFavourite (row); }
        KitBrowser& owner;
        int row = -1;
    };
    struct SearchField : juce::TextEditor
    {
        std::function<bool (const juce::KeyPress&)> onKey;
        bool keyPressed (const juce::KeyPress& k) override { return (onKey && onKey (k)) || juce::TextEditor::keyPressed (k); }
    };
    struct Categories : juce::Component
    {
        explicit Categories (KitBrowser& b) : owner (b) {}
        void paint (juce::Graphics&) override;
        void mouseMove (const juce::MouseEvent& e) override;
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        void mouseDown (const juce::MouseEvent& e) override;
        int rowAt (int y) const;
        KitBrowser& owner;
        int hover = -1;
    };

    int getNumRows() override { return numShown(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    juce::Component* refreshComponentForRow (int row, bool selected, juce::Component* existing) override;
    void selectedRowsChanged (int lastRow) override;
    void listBoxItemDoubleClicked (int, const juce::MouseEvent&) override { close(); }
    void returnKeyPressed (int) override { close(); }
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    void refilter();
    void rebuildGroups();
    void selectCurrent();
    void close() { if (onClose) onClose(); }
    float scale() const { return getWidth() / 1032.0f; }
    int starWidth() const { return juce::roundToInt (30.0f * scale()); }

    PowerDrumProcessor& proc;
    SearchField search;
    juce::TextButton playButton { "PLAY" }, closeButton { "CLOSE" }, allGroupsButton { "ALL" };
    juce::OwnedArray<juce::TextButton> groupButtons;
    Categories categories { *this };
    juce::ListBox list { "Kits", this };
    juce::StringArray categoryNames, groupNames, selectedGroups;
    juce::String category;
    std::vector<int> shown;
    bool updating = false;
    int hoverRow = -1;
    bool hoverStar = false;
    juce::Rectangle<int> titleArea, emptyArea;
};
}

class PowerDrumEditor : public juce::AudioProcessorEditor, private juce::Timer, private juce::AudioProcessorValueTreeState::Listener
{
public:
    explicit PowerDrumEditor (PowerDrumProcessor&);
    ~PowerDrumEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    void selectSlot (int slot);   // public for the wrapper tests

private:
    void timerCallback() override;
    void parameterChanged (const juce::String&, float) override { soundDirty = true; }
    void renderWaveform();
    void refreshSlotLabels();
    void showSoundMenu();
    void stepSound (int delta);
    void chooseType (int type);
    void showMainMenu();
    void stepKit (int delta);
    void saveKit();
    void saveSound();
    void askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done);
    void showError (const juce::String& message);
    void refreshHeader();

    PowerDrumProcessor& proc;
    pd::ui::LookAndFeel lnf;
    juce::TooltipWindow tooltips { this, 600 };

    juce::TextButton prevButton { "<" }, nextButton { ">" }, kitButton, saveButton { "SAVE" }, menuButton { "..." },
        playButton { "PLAY" }, multiButton { "MULTI OUT" };
    pd::ui::StarButton favouriteButton;
    std::unique_ptr<pd::ui::Segment> analogMode;

    std::array<std::unique_ptr<pd::ui::Pad>, pd::numSlots> pads;

    // the selected slot
    int slot = 0;
    juce::TextButton soundPrev { "<" }, soundNext { ">" }, soundButton;
    pd::ui::Segment typeSeg, modelSeg, driveModeSeg, muteSeg, soloSeg;
    pd::ui::Knob tune, decay, tone, knobA, knobB, drive, level, pan, width, filter, rev, dly, vel, gain;
    pd::ui::ChoiceButton outButton, chokeButton;
    pd::ui::WaveformView waveform;

    // master
    pd::ui::Knob revSize, revDamp, revLevel, dlyTime, dlyFb, dlyLevel, glue, mDrive, volume;
    pd::ui::Meter meter;
    pd::ui::KitBrowser browser;

    std::atomic<bool> soundDirty { true };
    int dirtyAge = -1, lastType = -1;
    std::unique_ptr<juce::FileChooser> chooser;
    struct Section { juce::String title; juce::Rectangle<int> area; };
    std::vector<Section> sections;
    juce::Rectangle<int> slotTitleArea, soundLabel, mixLabel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PowerDrumEditor)
};
