#include "PluginEditor.h"

using namespace pd::ui;

juce::Font pd::ui::uiFont (float size, bool bold) { return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain)); }

float pd::ui::uiScale (const juce::Component& c)
{
    const auto* top = c.getTopLevelComponent();
    return top != nullptr && top->getWidth() > 0 ? (float) top->getWidth() / 1080.0f : 1.0f;
}

namespace
{
juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }

void fillPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius = 10.0f)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, r.getX(), r.getY(), colours::panelBot, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setGradientFill (juce::ColourGradient (juce::Colours::white.withAlpha (0.08f), r.getX(), r.getY(),
                                             juce::Colours::white.withAlpha (0.0f), r.getX(), r.getY() + r.getHeight() * 0.5f, false));
    g.drawRoundedRectangle (r.reduced (0.5f), radius, 1.0f);
}

juce::String msText (double s) { return s < 1.0 ? juce::String (juce::roundToInt (s * 1000.0)) + " ms" : juce::String (s, 2) + " s"; }
juce::String hzText (double hz) { return hz < 1000.0 ? juce::String (juce::roundToInt (hz)) + " Hz" : juce::String (hz / 1000.0, 2) + " kHz"; }
}

//==============================================================================
LookAndFeel::LookAndFeel()
{
    setColour (juce::PopupMenu::backgroundColourId, colours::panelBot);
    setColour (juce::PopupMenu::textColourId, colours::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, colours::accent);
    setColour (juce::PopupMenu::highlightedTextColourId, colours::ink);
    setColour (juce::PopupMenu::headerTextColourId, colours::dim);
    setColour (juce::TooltipWindow::backgroundColourId, colours::panelTop);
    setColour (juce::TooltipWindow::textColourId, colours::text);
    setColour (juce::TooltipWindow::outlineColourId, colours::line);
    setColour (juce::AlertWindow::backgroundColourId, colours::panelBot);
    setColour (juce::AlertWindow::textColourId, colours::text);
    setColour (juce::AlertWindow::outlineColourId, colours::line);
    setColour (juce::TextEditor::backgroundColourId, colours::bgBottom);
    setColour (juce::TextEditor::textColourId, colours::text);
    setColour (juce::TextEditor::outlineColourId, colours::line);
    setColour (juce::TextEditor::focusedOutlineColourId, colours::accent);
    setColour (juce::TextButton::buttonColourId, colours::panelTop);
    setColour (juce::TextButton::textColourOffId, colours::text);
}

void LookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    const auto bounds = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (3.0f);
    const float radius = std::min (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto c = bounds.getCentre();
    const float track = std::max (2.5f, radius * 0.09f);
    const float angle = start + pos * (end - start);
    const float face = radius - track * 2.2f;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff4a4646), c.x, c.y - face, juce::Colour (0xff211e1e), c.x, c.y + face, false));
    g.fillEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f);
    g.setColour (juce::Colours::white.withAlpha (0.07f));
    g.drawEllipse (c.x - face, c.y - face, face * 2.0f, face * 2.0f, 1.0f);
    const float r = radius - track * 0.5f;
    juce::Path bg;
    bg.addCentredArc (c.x, c.y, r, r, 0.0f, start, end, true);
    g.setColour (juce::Colours::white.withAlpha (0.09f));
    g.strokePath (bg, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    const bool bipolar = s.getProperties().contains ("bipolar");
    const float from = bipolar ? (start + end) * 0.5f : start;
    if (std::abs (angle - from) > 0.001f)
    {
        juce::Path val;
        val.addCentredArc (c.x, c.y, r, r, 0.0f, std::min (from, angle), std::max (from, angle), true);
        const bool primary = (bool) s.getProperties()["primary"];
        g.setColour (! s.isEnabled() ? colours::faint : (primary ? colours::accent : colours::text.withAlpha (0.62f)));
        g.strokePath (val, juce::PathStrokeType (track, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (colours::text);
    g.drawLine ({ c.getPointOnCircumference (face * 0.38f, angle), c.getPointOnCircumference (face * 0.78f, angle) }, std::max (2.0f, radius * 0.06f));
}

void LookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (0.5f);
    const float radius = std::min (8.0f, r.getHeight() * 0.5f);
    if (b.getToggleState())
    {
        g.setColour (colours::accent.withAlpha (down ? 0.8f : 1.0f));
        g.fillRoundedRectangle (r, radius);
        return;
    }
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff3b3636).brighter (over ? 0.12f : 0.0f), r.getX(), r.getY(),
                                             juce::Colour (0xff252121), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (over ? 0.16f : 0.08f));
    g.drawRoundedRectangle (r, radius, 1.0f);
}

void LookAndFeel::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    g.setFont (getTextButtonFont (b, b.getHeight()));
    g.setColour (b.getToggleState() ? colours::ink : (b.isEnabled() ? colours::text : colours::faint));
    g.drawFittedText (b.getButtonText(), b.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
}

void LookAndFeel::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.setGradientFill (juce::ColourGradient (colours::panelTop, 0, 0, colours::panelBot, 0, (float) h, false));
    g.fillAll();
    g.setColour (colours::line);
    g.drawRect (0, 0, w, h);
}

juce::Font LookAndFeel::getTextButtonFont (juce::TextButton& b, int h)
{
    return uiFont (std::min (12.5f * uiScale (b), (float) h * 0.46f), true).withExtraKerningFactor (0.06f);
}
juce::Font LookAndFeel::getPopupMenuFont() { return uiFont (14.0f); }

//==============================================================================
Knob::Knob (juce::AudioProcessorValueTreeState& s, const juce::String& t, const juce::String& tooltip) : state (s), title (t)
{
    slider.setSliderStyle (juce::Slider::RotaryVerticalDrag);
    slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    slider.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
    slider.setMouseDragSensitivity (220);
    slider.setVelocityModeParameters (0.4, 1, 0.0, false, juce::ModifierKeys::shiftModifier);
    slider.setTooltip (tooltip);
    slider.onValueChange = [this] { repaint(); };
    addAndMakeVisible (slider);
    setTooltip (tooltip);
}

void Knob::bind (const juce::String& pid)
{
    if (pid == boundId) return;
    attachment.reset();
    boundId = pid;
    setComponentID (pid);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (state, pid, slider);
    if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (pid)))
        slider.setDoubleClickReturnValue (true, (double) p->convertFrom0to1 (p->getDefaultValue()));
    repaint();
}

void Knob::setPrimary (bool p)
{
    slider.getProperties().set ("primary", p);
    resized();
    repaint();
}

int Knob::labelHeight() const { return juce::roundToInt ((isPrimary() ? 19.0f : 16.0f) * uiScale (*this)); }

void Knob::resized()
{
    const int label = labelHeight();
    const int cap = juce::roundToInt ((isPrimary() ? 84.0f : 56.0f) * uiScale (*this));
    const int d = std::max (0, std::min ({ getWidth(), getHeight() - 2 * label, cap }));
    auto group = getLocalBounds().withSizeKeepingCentre (getWidth(), d + 2 * label);
    group.removeFromTop (label);
    slider.setBounds (group.removeFromTop (d).withSizeKeepingCentre (d, d));
}

void Knob::paint (juce::Graphics& g)
{
    const float s = uiScale (*this);
    const bool primary = isPrimary();
    const int label = labelHeight();
    const auto dial = slider.getBounds();
    g.setColour (primary ? colours::text : colours::dim);
    g.setFont (uiFont ((primary ? 11.5f : 10.5f) * s, true).withExtraKerningFactor (0.1f));
    g.drawText (title, juce::Rectangle<int> (0, dial.getY() - label, getWidth(), label), juce::Justification::centred);
    g.setColour (colours::text);
    g.setFont (uiFont ((primary ? 13.0f : 11.5f) * s));
    const auto text = valueText ? valueText (slider.getValue()) : slider.getTextFromValue (slider.getValue());
    g.drawText (text, juce::Rectangle<int> (0, dial.getBottom(), getWidth(), label), juce::Justification::centred);
}

//==============================================================================
Segment::Segment (juce::AudioProcessorValueTreeState& s, const juce::StringArray& labels, const juce::String& tooltip) : state (s)
{
    setTooltip (tooltip);
    setLabels (labels);
}

void Segment::setLabels (const juce::StringArray& labels)
{
    if (buttons.size() == labels.size())
    {
        for (int i = 0; i < labels.size(); ++i) buttons[i]->setButtonText (labels[i]);
        return;
    }
    buttons.clear();
    for (int i = 0; i < labels.size(); ++i)
    {
        auto* b = buttons.add (new juce::TextButton (labels[i]));
        b->setTooltip (getTooltip());
        b->onClick = [this, i, single = labels.size() == 1]
        {
            if (onChoose) { onChoose (i); return; }
            if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (state.getParameter (paramId)))
            {
                const int now = juce::roundToInt (state.getRawParameterValue (paramId)->load());
                const float target = single ? (now == 1 ? 0.0f : 1.0f) : (float) i;
                p->beginChangeGesture();
                p->setValueNotifyingHost (p->convertTo0to1 (target));
                p->endChangeGesture();
            }
        };
        addAndMakeVisible (b);
    }
    resized();
    handleAsyncUpdate();
}

Segment::~Segment() { if (paramId.isNotEmpty()) state.removeParameterListener (paramId, this); }

void Segment::bind (const juce::String& pid)
{
    if (pid == paramId) return;
    if (paramId.isNotEmpty()) state.removeParameterListener (paramId, this);
    paramId = pid;
    setComponentID (pid);
    state.addParameterListener (paramId, this);
    handleAsyncUpdate();
}

void Segment::resized()
{
    auto r = getLocalBounds();
    const int n = std::max (1, buttons.size());
    for (int i = 0; i < buttons.size(); ++i)
    {
        const int x0 = r.getX() + r.getWidth() * i / n, x1 = r.getX() + r.getWidth() * (i + 1) / n;
        buttons[i]->setBounds (juce::Rectangle<int> (x0, r.getY(), x1 - x0, r.getHeight()).reduced (2, 0));
    }
}

void Segment::handleAsyncUpdate()
{
    if (paramId.isEmpty()) return;
    const int index = juce::roundToInt (state.getRawParameterValue (paramId)->load());
    for (int i = 0; i < buttons.size(); ++i)
        buttons[i]->setToggleState (buttons.size() == 1 ? index == 1 : i == index, juce::dontSendNotification);
}

//==============================================================================
ChoiceButton::ChoiceButton (juce::AudioProcessorValueTreeState& s, const juce::String& c, const juce::String& tooltip) : state (s), caption (c)
{
    setTooltip (tooltip);
}

ChoiceButton::~ChoiceButton() { if (paramId.isNotEmpty()) state.removeParameterListener (paramId, this); }

void ChoiceButton::bind (const juce::String& pid)
{
    if (pid == paramId) return;
    if (paramId.isNotEmpty()) state.removeParameterListener (paramId, this);
    paramId = pid;
    setComponentID (pid);
    state.addParameterListener (paramId, this);
    handleAsyncUpdate();
}

void ChoiceButton::handleAsyncUpdate()
{
    if (auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
    {
        const int i = p->getIndex();
        const auto item = itemText ? itemText (i) : p->choices[i];
        setButtonText (caption.isEmpty() ? item : caption + "  " + item);
    }
}

void ChoiceButton::clicked()
{
    auto* p = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId));
    if (p == nullptr) return;
    juce::PopupMenu m;
    for (int i = 0; i < p->choices.size(); ++i)
        m.addItem (i + 1, itemText ? itemText (i) : p->choices[i], true, i == p->getIndex());
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this] (int r)
    {
        if (r <= 0) return;
        if (auto* q = dynamic_cast<juce::AudioParameterChoice*> (state.getParameter (paramId)))
        {
            q->beginChangeGesture();
            q->setValueNotifyingHost (q->convertTo0to1 ((float) (r - 1)));
            q->endChangeGesture();
        }
    });
}

//==============================================================================
WaveformView::WaveformView()
{
    setTooltip (u8 ("Щелчок — прослушать звук выбранного слота"));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void WaveformView::setAudio (const juce::AudioBuffer<float>& mono, double sampleRate, double windowMs)
{
    window = windowMs;
    const int columns = 500;
    minV.assign (columns, 0.0f);
    maxV.assign (columns, 0.0f);
    const int n = mono.getNumSamples();
    const float peak = std::max (mono.getMagnitude (0, 0, n), 1.0e-9f);
    int last = n - 1;
    while (last > 0 && std::abs (mono.getSample (0, last)) < peak * 0.001f) --last;
    lengthMs = 1000.0 * (last + 1) / sampleRate;
    const float norm = 0.95f / peak;   // the shape, whatever the level
    for (int c = 0; c < columns; ++c)
    {
        const int from = (int) ((double) c * n / columns), to = std::max (from + 1, (int) ((double) (c + 1) * n / columns));
        const auto range = juce::FloatVectorOperations::findMinAndMax (mono.getReadPointer (0, from), std::min (to, n) - from);
        minV[(size_t) c] = range.getStart() * norm;
        maxV[(size_t) c] = range.getEnd() * norm;
    }
    repaint();
}

void WaveformView::decay()
{
    if (flashLevel > 0.0f) { flashLevel = std::max (0.0f, flashLevel - 0.08f); repaint(); }
}

void WaveformView::mouseUp (const juce::MouseEvent& e)
{
    if (! e.mouseWasDraggedSinceMouseDown() && onClick != nullptr) onClick();
}

void WaveformView::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    fillPanel (g, r, 10.0f);
    auto plot = r.reduced (12.0f, 10.0f);
    const float mid = plot.getCentreY();
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawHorizontalLine ((int) mid, plot.getX(), plot.getRight());
    const double step = window > 1500.0 ? 500.0 : 100.0;
    for (double t = step; t < window; t += step)
        g.drawVerticalLine ((int) (plot.getX() + plot.getWidth() * (float) (t / window)), plot.getY() + 4, plot.getBottom() - 4);
    if (! maxV.empty())
    {
        juce::Path p;
        const auto cols = (int) maxV.size();
        const float half = plot.getHeight() * 0.48f;
        p.startNewSubPath (plot.getX(), mid);
        for (int c = 0; c < cols; ++c) p.lineTo (plot.getX() + plot.getWidth() * c / (cols - 1.0f), mid - juce::jlimit (-1.0f, 1.0f, maxV[(size_t) c]) * half);
        for (int c = cols - 1; c >= 0; --c) p.lineTo (plot.getX() + plot.getWidth() * c / (cols - 1.0f), mid - juce::jlimit (-1.0f, 1.0f, minV[(size_t) c]) * half);
        p.closeSubPath();
        juce::ColourGradient fill (juce::Colour (0xff716a6a), 0, mid - half, juce::Colour (0xff716a6a), 0, mid + half, false);
        fill.addColour (0.5, colours::accent);
        g.setGradientFill (fill);
        g.fillPath (p);
        if (flashLevel > 0.0f) { g.setColour (juce::Colours::white.withAlpha (0.25f * flashLevel)); g.fillPath (p); }
    }
    const float s = uiScale (*this);
    const auto label = r.reduced (12.0f, 6.0f).removeFromTop (14.0f * s);
    g.setFont (uiFont (10.5f * s, true).withExtraKerningFactor (0.1f));
    g.setColour (colours::dim);
    g.drawText (juce::String (juce::roundToInt (lengthMs)) + " MS", label, juce::Justification::topRight);
    g.setColour (hover ? colours::text.withAlpha (0.85f) : colours::dim.withAlpha (0.7f));
    g.drawText (u8 ("CLICK TO PLAY \xe2\x96\xb6"), label, juce::Justification::topLeft);
}

//==============================================================================
void Meter::setLevels (float l, float r)
{
    const float in[2] = { l, r };
    for (int c = 0; c < 2; ++c)
    {
        levels[c] = std::max (in[c], levels[c] * 0.82f);
        if (in[c] >= held[c]) { held[c] = in[c]; holdFrames[c] = 45; }
        else if (--holdFrames[c] <= 0) held[c] = std::max (0.0f, held[c] - 0.02f);
    }
    repaint();
}

void Meter::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float bw = (r.getWidth() - 4.0f) / 2.0f;
    auto toY = [&r] (float lin)
    {
        const float dbv = juce::jlimit (-48.0f, 0.0f, juce::Decibels::gainToDecibels (lin, -48.0f));
        return r.getBottom() - r.getHeight() * (dbv + 48.0f) / 48.0f;
    };
    for (int c = 0; c < 2; ++c)
    {
        auto bar = juce::Rectangle<float> (r.getX() + (float) c * (bw + 4.0f), r.getY(), bw, r.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.06f));
        g.fillRoundedRectangle (bar, 2.0f);
        g.setGradientFill (juce::ColourGradient (colours::accent, 0, r.getY(), colours::text.withAlpha (0.85f), 0, r.getBottom(), false));
        g.fillRoundedRectangle (bar.withTop (toY (levels[c])), 2.0f);
        g.setColour (held[c] >= 0.999f ? colours::hot : colours::text);
        g.fillRect (bar.getX(), toY (held[c]) - 1.0f, bar.getWidth(), 2.0f);
    }
}

//==============================================================================
Pad::Pad (PowerDrumProcessor& p, int s) : proc (p), slot (s)
{
    setComponentID ("pad" + juce::String (s + 1));
    setTooltip (u8 ("Щелчок — сыграть и выбрать слот (Shift — только выбрать). Нота ") + pd::noteName (s));
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
}

void Pad::refresh()
{
    const auto n = proc.getSlotName (slot);
    auto v = [this] (const char* id) { return proc.state.getRawParameterValue (pd::slotId (slot, id))->load(); };
    const bool m = v (pd::id::mute) > 0.5f, so = v (pd::id::solo) > 0.5f;
    const int o = juce::roundToInt (v (pd::id::out)), t = juce::roundToInt (v (pd::id::type));
    if (n != name || m != muted || so != soloed || o != out || t != type)
    {
        name = n; muted = m; soloed = so; out = o; type = t;
        repaint();
    }
}

void Pad::decay()
{
    if (glow > 0.0f) { glow = std::max (0.0f, glow - 0.07f); repaint(); }
}

void Pad::mouseDown (const juce::MouseEvent& e)
{
    if (onPress) onPress (slot, ! e.mods.isShiftDown());
    glow = 1.0f;
    repaint();
}

void Pad::paint (juce::Graphics& g)
{
    const float s = uiScale (*this);
    auto r = getLocalBounds().toFloat().reduced (3.0f * s);
    const float radius = 8.0f * s;
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2b2626), r.getX(), r.getY(), juce::Colour (0xff171313), r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    if (glow > 0.0f)
    {
        g.setColour (colours::accent.withAlpha (0.55f * glow));
        g.fillRoundedRectangle (r, radius);
    }
    g.setColour (selected ? colours::accent : juce::Colours::white.withAlpha (0.08f));
    g.drawRoundedRectangle (r.reduced (selected ? 1.0f : 0.5f), radius, selected ? 2.0f * s : 1.0f);
    auto in = r.reduced (8.0f * s, 6.0f * s);
    auto top = in.removeFromTop (14.0f * s);
    g.setFont (uiFont (10.5f * s, true).withExtraKerningFactor (0.08f));
    g.setColour (colours::dim);
    g.drawText (pd::noteName (slot), top, juce::Justification::centredLeft);
    g.setColour (colours::faint);
    g.drawText (pd::typeNames()[juce::jlimit (0, pd::numTypes - 1, type)], top, juce::Justification::centredRight);
    auto bottom = in.removeFromBottom (13.0f * s);
    g.setFont (uiFont (10.0f * s, true));
    if (out > 0) { g.setColour (colours::dim); g.drawText ("OUT " + juce::String (out), bottom, juce::Justification::centredRight); }
    if (muted) { g.setColour (colours::hot); g.drawText ("MUTE", bottom, juce::Justification::centredLeft); }
    else if (soloed) { g.setColour (colours::accent); g.drawText ("SOLO", bottom, juce::Justification::centredLeft); }
    g.setFont (uiFont (12.5f * s, true));
    g.setColour (muted ? colours::faint : colours::text);
    g.drawFittedText (name.isEmpty() ? juce::String ("-") : name, in.toNearestInt(), juce::Justification::centred, 2, 0.85f);
}

//==============================================================================
void pd::ui::drawStar (juce::Graphics& g, juce::Rectangle<float> box, bool filled, juce::Colour c, float stroke)
{
    const float outer = std::min (box.getWidth(), box.getHeight()) * 0.5f;
    juce::Path star;
    star.addStar (box.getCentre().translated (0.0f, outer * 0.06f), 5, outer * 0.42f, outer, 0.0f);
    g.setColour (c);
    if (filled) g.fillPath (star);
    else g.strokePath (star, juce::PathStrokeType (stroke, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
}

void StarButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    getLookAndFeel().drawButtonBackground (g, *this, findColour (juce::TextButton::buttonColourId), over, down);
    const float d = std::min (getWidth(), getHeight()) * 0.5f;
    drawStar (g, getLocalBounds().toFloat().withSizeKeepingCentre (d, d), starred,
              starred ? colours::accent : (over && isEnabled() ? colours::text : colours::dim.withAlpha (isEnabled() ? 1.0f : 0.4f)),
              std::max (1.2f, d * 0.09f));
}

//==============================================================================
namespace
{
const juce::String userCategory { "User" };
void fillSelection (juce::Graphics& g, juce::Rectangle<float> r, float radius) { g.setColour (colours::accent); g.fillRoundedRectangle (r, radius); }
}

KitBrowser::StarSpot::StarSpot (KitBrowser& b) : owner (b)
{
    setMouseCursor (juce::MouseCursor::PointingHandCursor);
    setTooltip (u8 ("Звёздочка: кит попадёт в Favourites (во всех проектах). Щелчок ещё раз — убрать"));
}

juce::Component* KitBrowser::refreshComponentForRow (int row, bool, juce::Component* existing)
{
    auto* spot = dynamic_cast<StarSpot*> (existing);
    if (spot == nullptr) { delete existing; spot = new StarSpot (*this); }
    spot->row = row;
    return spot;
}

void KitBrowser::toggleFavourite (int row)
{
    if (row < 0 || row >= numShown()) return;
    proc.kits.toggleFavourite (shown[(size_t) row]);
    favouritesChanged();
    if (onFavouritesChanged) onFavouritesChanged();
}

void KitBrowser::favouritesChanged()
{
    if (category == pd::favouritesCategory) refilter();
    else { list.repaint(); categories.repaint(); }
}

KitBrowser::KitBrowser (PowerDrumProcessor& p) : proc (p)
{
    setWantsKeyboardFocus (true);
    search.setTextToShowWhenEmpty ("Search kits", colours::dim);
    search.setJustification (juce::Justification::centredLeft);
    search.setTooltip (u8 ("Слова из названия, стиля или группы"));
    search.onTextChange = [this] { refilter(); };
    search.onReturnKey = [this] { close(); };
    search.onEscapeKey = [this] { close(); };
    search.onKey = [this] (const juce::KeyPress& k) { return (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey) && keyPressed (k); };
    playButton.setClickingTogglesState (true);
    playButton.setTooltip (u8 ("Играть грув через выбранный кит, пока включено"));
    playButton.onClick = [this] { proc.grooveOn = playButton.getToggleState(); };
    closeButton.setTooltip (u8 ("Назад к пэдам (Enter или Esc)"));
    closeButton.onClick = [this] { close(); };
    allGroupsButton.setTooltip (u8 ("Показать все группы"));
    allGroupsButton.onClick = [this] { selectedGroups.clear(); refilter(); };
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
    list.setColour (juce::ListBox::outlineColourId, juce::Colours::transparentBlack);
    list.getVerticalScrollBar().setColour (juce::ScrollBar::thumbColourId, colours::faint);
    list.addMouseListener (this, true);
    list.setComponentID ("kitList");
    for (auto* c : std::initializer_list<juce::Component*> { &search, &playButton, &closeButton, &allGroupsButton, &categories, &list })
        addAndMakeVisible (c);
}

KitBrowser::~KitBrowser() { list.removeMouseListener (this); }

void KitBrowser::open()
{
    proc.kits.rescan();
    categoryNames.clear();
    for (const auto& k : proc.kits.all())
        if (k.category != userCategory) categoryNames.addIfNotAlreadyThere (k.category);
    if (category.isNotEmpty() && category != userCategory && category != pd::favouritesCategory && ! categoryNames.contains (category))
        category = {};
    playButton.setToggleState (proc.grooveOn.load(), juce::dontSendNotification);
    rebuildGroups();
    refilter();
    setVisible (true);
    if (isShowing()) search.grabKeyboardFocus();
}

void KitBrowser::setCategory (const juce::String& c) { category = c; refilter(); }
void KitBrowser::toggleGroup (const juce::String& g) { if (selectedGroups.contains (g)) selectedGroups.removeString (g); else selectedGroups.add (g); refilter(); }
void KitBrowser::setSearch (const juce::String& t) { search.setText (t, false); refilter(); }

void KitBrowser::rebuildGroups()
{
    groupNames.clear();
    for (const auto& k : proc.kits.all())
        if (k.group.isNotEmpty()) groupNames.addIfNotAlreadyThere (k.group);
    if (groupButtons.size() == groupNames.size()) return;
    groupButtons.clear();
    for (const auto& g : groupNames)
    {
        auto* b = groupButtons.add (new juce::TextButton());
        b->setTooltip (u8 ("Показать киты группы ") + g);
        b->onClick = [this, g] { toggleGroup (g); };
        addAndMakeVisible (b);
    }
    resized();
}

void KitBrowser::refilter()
{
    const auto& all = proc.kits.all();
    auto inCategory = pd::filterKits (all, category, search.getText());
    shown.clear();
    for (int i : inCategory)
        if (selectedGroups.isEmpty() || selectedGroups.contains (all[(size_t) i].group)) shown.push_back (i);
    const auto counted = pd::filterKits (all, category, {});
    for (int i = 0; i < groupNames.size(); ++i)
    {
        int n = 0;
        for (int k : counted) n += all[(size_t) k].group == groupNames[i] ? 1 : 0;
        auto* b = groupButtons[i];
        const bool on = selectedGroups.contains (groupNames[i]);
        b->setButtonText (groupNames[i].toUpperCase() + "  " + juce::String (n));
        b->setToggleState (on, juce::dontSendNotification);
        b->setEnabled (n > 0 || on);
    }
    allGroupsButton.setToggleState (selectedGroups.isEmpty(), juce::dontSendNotification);
    resized();
    hoverRow = -1;
    list.updateContent();
    selectCurrent();
    categories.repaint();
    repaint();
}

void KitBrowser::selectCurrent()
{
    const auto& all = proc.kits.all();
    const auto name = proc.getKitName();
    int row = -1;
    for (int r = 0; r < numShown() && row < 0; ++r)
        if (all[(size_t) shown[(size_t) r]].name == name) row = r;
    const juce::ScopedValueSetter<bool> guard (updating, true);
    if (row >= 0) list.selectRow (row);
    else { list.deselectAllRows(); list.scrollToEnsureRowIsOnscreen (0); }
}

void KitBrowser::choose (int row, bool closeAfter)
{
    if (row < 0 || row >= numShown()) return;
    proc.loadKit (shown[(size_t) row]);
    { const juce::ScopedValueSetter<bool> guard (updating, true); list.selectRow (row); }
    if (onLoaded) onLoaded();
    if (closeAfter) close();
}

void KitBrowser::selectedRowsChanged (int lastRow) { if (! updating && lastRow >= 0) choose (lastRow, false); }

bool KitBrowser::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey || k == juce::KeyPress::returnKey) { close(); return true; }
    if (k == juce::KeyPress::upKey || k == juce::KeyPress::downKey)
    {
        if (shown.empty()) return true;
        const int current = list.getSelectedRow();
        const int next = current < 0 ? 0 : juce::jlimit (0, numShown() - 1, current + (k == juce::KeyPress::downKey ? 1 : -1));
        if (next != current) choose (next, false);
        list.scrollToEnsureRowIsOnscreen (next);
        return true;
    }
    return false;
}

void KitBrowser::mouseMove (const juce::MouseEvent& e)
{
    int row = -1;
    bool star = false;
    if (e.eventComponent == &list || list.isParentOf (e.eventComponent))
    {
        const auto at = e.getEventRelativeTo (&list).getPosition();
        row = list.getRowContainingPosition (at.x, at.y);
        star = row >= 0 && at.x < starWidth();
    }
    if (row != hoverRow || star != hoverStar)
    {
        list.repaintRow (hoverRow);
        hoverRow = row; hoverStar = star;
        list.repaintRow (hoverRow);
    }
}

void KitBrowser::mouseExit (const juce::MouseEvent&)
{
    if (hoverRow >= 0) { list.repaintRow (hoverRow); hoverRow = -1; }
}

void KitBrowser::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= numShown()) return;
    const auto& k = proc.kits.all()[(size_t) shown[(size_t) row]];
    const float s = scale();
    const auto r = juce::Rectangle<float> (0.0f, 0.0f, (float) w, (float) h).reduced (2.0f * s, 1.5f * s);
    if (selected) fillSelection (g, r, 6.0f * s);
    else if (row == hoverRow) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (r, 6.0f * s); }
    juce::String meta = k.category == userCategory ? juce::String ("MY KIT") : k.group.toUpperCase();
    if ((category.isEmpty() || category == pd::favouritesCategory) && k.category != userCategory)
        meta = k.category.toUpperCase() + (meta.isNotEmpty() ? "   " + meta : juce::String());
    const float starW = (float) starWidth();
    const auto starBox = juce::Rectangle<float> (r.getX() + 4.0f * s, r.getY(), starW - 6.0f * s, r.getHeight()).withSizeKeepingCentre (13.0f * s, 13.0f * s);
    const bool overStar = row == hoverRow && hoverStar;
    const auto starColour = k.favourite ? (selected ? colours::ink : colours::accent)
                                        : (selected ? colours::ink.withAlpha (overStar ? 0.9f : 0.4f) : (overStar ? colours::text : colours::faint));
    drawStar (g, starBox, k.favourite, starColour, 1.3f * s);
    auto text = r.withTrimmedLeft (starW - 2.0f * s).reduced (8.0f * s, 0.0f);
    const auto metaFont = uiFont (10.5f * s, true).withExtraKerningFactor (0.08f);
    const float metaW = (float) juce::GlyphArrangement::getStringWidthInt (metaFont, meta);
    g.setFont (metaFont);
    g.setColour (selected ? colours::ink.withAlpha (0.65f) : colours::dim);
    g.drawText (meta, text.removeFromRight (metaW), juce::Justification::centredRight, false);
    text.removeFromRight (12.0f * s);
    g.setFont (uiFont (14.0f * s));
    g.setColour (selected ? colours::ink : colours::text);
    g.drawText (k.name, text, juce::Justification::centredLeft, true);
}

void KitBrowser::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    const float s = scale();
    g.setColour (juce::Colours::black.withAlpha (0.35f));
    g.fillRoundedRectangle (r.translated (0, 4), 12.0f);
    g.setGradientFill (juce::ColourGradient (juce::Colour (0xff2a2222), 0, r.getY(), juce::Colour (0xff120e0e), 0, r.getBottom(), false));
    g.fillRoundedRectangle (r, 12.0f);
    g.setColour (colours::accent.withAlpha (0.6f));
    g.drawRoundedRectangle (r.reduced (0.5f), 12.0f, 1.0f);
    g.setFont (uiFont (11.0f * s, true).withExtraKerningFactor (0.18f));
    g.setColour (colours::text);
    g.drawText ("KITS", titleArea, juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.fillRect (juce::Rectangle<int> (categories.getRight() + juce::roundToInt (8 * s), categories.getY(), 1, categories.getHeight()));
    if (shown.empty())
    {
        const bool unfiltered = search.getText().trim().isEmpty() && selectedGroups.isEmpty();
        g.setColour (colours::dim);
        g.setFont (uiFont (14.0f * s));
        g.drawFittedText (category == userCategory && unfiltered ? "No kits of your own yet.\nSave one with SAVE at the top."
                          : category == pd::favouritesCategory && unfiltered ? "No favourites yet.\nClick the star at the left of a kit."
                          : "No kits match.\nClear the search or choose another group.",
                          emptyArea, juce::Justification::centred, 2);
    }
}

void KitBrowser::paintOverChildren (juce::Graphics& g)
{
    const float s = scale();
    const auto b = search.getBounds().toFloat();
    const float d = 10.0f * s;
    const juce::Point<float> c (b.getX() + 15.0f * s, b.getCentreY() - 1.0f * s);
    g.setColour (colours::dim);
    g.drawEllipse (c.x - d * 0.5f, c.y - d * 0.5f, d, d, 1.4f * s);
    const auto from = c.getPointOnCircumference (d * 0.5f, juce::MathConstants<float>::pi * 0.75f);
    g.drawLine ({ from, from + juce::Point<float> (3.5f * s, 3.5f * s) }, 1.6f * s);
}

void KitBrowser::resized()
{
    const float s = scale();
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    auto r = getLocalBounds().reduced (S (16), S (12));
    auto top = r.removeFromTop (S (30));
    closeButton.setBounds (top.removeFromRight (S (72)).withSizeKeepingCentre (S (72), S (24)));
    top.removeFromRight (S (8));
    playButton.setBounds (top.removeFromRight (S (80)).withSizeKeepingCentre (S (80), S (24)));
    titleArea = top.removeFromLeft (S (200));
    const int searchW = std::min (top.getWidth() - S (16), S (340));
    search.setBounds (top.removeFromLeft (searchW).withSizeKeepingCentre (searchW, S (28)));
    const auto font = uiFont (14.0f * s);
    search.setFont (font);
    search.applyFontToAllText (font);
    search.setIndents (S (30), juce::jmax (0, (S (28) - juce::roundToInt (font.getHeight())) / 2));
    r.removeFromTop (S (12));
    auto left = r.removeFromLeft (S (230));
    r.removeFromLeft (S (17));
    const int pillH = S (26), gap = S (6);
    auto pillWidth = [&] (juce::TextButton& b) { return juce::GlyphArrangement::getStringWidthInt (getLookAndFeel().getTextButtonFont (b, pillH), b.getButtonText()) + S (20); };
    std::vector<juce::TextButton*> pills { &allGroupsButton };
    for (auto* b : groupButtons) pills.push_back (b);
    int x = r.getX(), y = r.getY();
    for (auto* b : pills)
    {
        const int w = pillWidth (*b);
        if (x > r.getX() && x + w > r.getRight()) { x = r.getX(); y += pillH + gap; }
        b->setBounds (x, y, w, pillH);
        x += w + gap;
    }
    r.setTop (y + pillH + S (12));
    list.setRowHeight (S (28));
    list.setBounds (r);
    emptyArea = r.withHeight (S (90));
    categories.setBounds (left);
}

int KitBrowser::Categories::rowAt (int y) const
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    if (y < rowH * n) return y / rowH;
    const int bottomTop = rowH * n + juce::roundToInt (14 * s);
    return y >= bottomTop && y < bottomTop + 2 * rowH ? n + (y - bottomTop) / rowH : -1;
}

void KitBrowser::Categories::paint (juce::Graphics& g)
{
    const float s = owner.scale();
    const int rowH = juce::roundToInt (30 * s), n = owner.categoryNames.size() + 1;
    const auto& all = owner.proc.kits.all();
    for (int i = 0; i <= n + 1; ++i)
    {
        const bool favourites = i == n, user = i == n + 1;
        const juce::String key = i == 0 ? juce::String() : (favourites ? pd::favouritesCategory : (user ? userCategory : owner.categoryNames[i - 1]));
        const juce::String label = i == 0 ? juce::String ("All kits") : (favourites ? juce::String ("Favourites") : (user ? juce::String ("My kits") : key));
        const int top = i >= n ? rowH * i + juce::roundToInt (14 * s) : rowH * i;
        if (favourites) { g.setColour (colours::line); g.fillRect (juce::Rectangle<int> (0, rowH * n + juce::roundToInt (7 * s), getWidth(), 1)); }
        auto r = juce::Rectangle<float> (0.0f, (float) top, (float) getWidth(), (float) rowH).reduced (0.0f, 1.5f * s);
        const bool selected = owner.category == key;
        if (selected) fillSelection (g, r, 6.0f * s);
        else if (i == hover) { g.setColour (juce::Colours::white.withAlpha (0.06f)); g.fillRoundedRectangle (r, 6.0f * s); }
        auto text = r.reduced (10.0f * s, 0.0f);
        const auto count = juce::String ((int) pd::filterKits (all, key, {}).size());
        g.setFont (uiFont (12.0f * s, true));
        g.setColour (selected ? colours::ink.withAlpha (0.65f) : colours::dim);
        g.drawText (count, text.removeFromRight (34.0f * s), juce::Justification::centredRight);
        if (favourites) drawStar (g, text.removeFromRight (16.0f * s).withSizeKeepingCentre (11.0f * s, 11.0f * s), true, selected ? colours::ink : colours::accent, 1.0f);
        g.setFont (uiFont (13.5f * s));
        g.setColour (selected ? colours::ink : colours::text);
        g.drawText (label, text, juce::Justification::centredLeft, true);
    }
}

void KitBrowser::Categories::mouseMove (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y);
    if (row != hover) { hover = row; repaint(); }
}

void KitBrowser::Categories::mouseDown (const juce::MouseEvent& e)
{
    const int row = rowAt (e.y), n = owner.categoryNames.size() + 1;
    if (row < 0) return;
    owner.setCategory (row == 0 ? juce::String() : (row == n ? pd::favouritesCategory : (row == n + 1 ? userCategory : owner.categoryNames[row - 1])));
}

//==============================================================================
PowerDrumEditor::PowerDrumEditor (PowerDrumProcessor& p)
    : AudioProcessorEditor (p), proc (p),
      typeSeg (p.state, pd::typeNames(), u8 ("Тип барабана в этом слоте — любой в любом слоте. Щелчок загружает первый звук этого типа из библиотеки; дальше — стрелки у имени звука")),
      modelSeg (p.state, { "1", "2", "3" }, u8 ("Модель: три разных устройства одного типа")),
      driveModeSeg (p.state, { "SAT", "CLIP", "FOLD", "CRUSH" }, u8 ("Тип перегруза: SAT — насыщение, CLIP — жёсткое ограничение, FOLD — складывание (металл), CRUSH — меньше бит и частоты (lo-fi)")),
      muteSeg (p.state, { "MUTE" }, u8 ("Выключить слот")),
      soloSeg (p.state, { "SOLO" }, u8 ("Слушать только этот слот (и другие с SOLO)")),
      tune (p.state, "TUNE", u8 ("Высота: для бочки, тома, рамбла, перкуссии — частота; для хэтов и тарелок — масштаб металла")),
      decay (p.state, "DECAY", u8 ("Длина удара")),
      tone (p.state, "TONE", u8 ("Яркость: у бочки — сколько середины и верха остаётся в теле после атаки")),
      knobA (p.state, "A", ""), knobB (p.state, "B", ""),
      drive (p.state, "DRIVE", u8 ("Перегруз слота (тип — над ручками). У бочки сильнее на атаке, низ остаётся чистым")),
      level (p.state, "LEVEL", u8 ("Громкость слота")),
      pan (p.state, "PAN", u8 ("Панорама")),
      width (p.state, "WIDTH", u8 ("Ширина стерео выше 250 Гц; моно-сумма не меняется")),
      filter (p.state, "FILTER", u8 ("Одна ручка: влево — срезает верх (ФНЧ), вправо — срезает низ (ФВЧ), в центре выключено")),
      rev (p.state, "REVERB", u8 ("Посыл в общую реверберацию (возврат — в MAIN)")),
      dly (p.state, "DELAY", u8 ("Посыл в общий дилей в темпе (возврат — в MAIN)")),
      vel (p.state, "VELOCITY", u8 ("Насколько сила удара меняет громкость")),
      gain (p.state, "GAIN", u8 ("Громкость самого звука (подобрана так, что при LEVEL 0 дБ удар достигает −1 dBFS)")),
      outButton (p.state, "", u8 ("Куда идёт слот: MAIN или отдельный выход OUT 1–16 (в Logic: Multi Output, каналы — «+» на канале инструмента)")),
      chokeButton (p.state, "CHOKE", u8 ("Группа заглушения: удар в слот группы обрывает остальные слоты той же группы (закрытый хэт глушит открытый)")),
      revSize (p.state, "REVERB", u8 ("Размер общей реверберации (посылы — ручки REVERB у слотов)")),
      revDamp (p.state, "DAMP", u8 ("Тёмный хвост реверберации")),
      revLevel (p.state, "REV LVL", u8 ("Громкость возврата реверберации в MAIN")),
      dlyTime (p.state, "DELAY", u8 ("Время общего дилея в долях такта (посылы — ручки DELAY у слотов)")),
      dlyFb (p.state, "FEEDBACK", u8 ("Повторы дилея")),
      dlyLevel (p.state, "DLY LVL", u8 ("Громкость возврата дилея в MAIN")),
      glue (p.state, "GLUE", u8 ("Склейка: компрессор на MAIN — плотнее и ровнее")),
      mDrive (p.state, "DRIVE", u8 ("Перегруз всего MAIN: громче и плотнее")),
      volume (p.state, "VOLUME", u8 ("Громкость всех выходов")),
      browser (p)
{
    setLookAndFeel (&lnf);
    for (int s = 0; s < pd::numSlots; ++s)
    {
        pads[(size_t) s] = std::make_unique<pd::ui::Pad> (proc, s);
        pads[(size_t) s]->onPress = [this] (int which, bool play)
        {
            selectSlot (which);
            if (play) { proc.playSlot (which); waveform.flash(); }
        };
        addAndMakeVisible (*pads[(size_t) s]);
    }
    pan.slider.getProperties().set ("bipolar", true);
    filter.slider.getProperties().set ("bipolar", true);
    tune.slider.getProperties().set ("bipolar", true);
    for (auto* k : { &decay, &level, &volume }) k->setPrimary (true);
    revSize.bind (pd::id::revSize); revDamp.bind (pd::id::revDamp); revLevel.bind (pd::id::revLevel);
    dlyTime.bind (pd::id::dlyTime); dlyFb.bind (pd::id::dlyFb); dlyLevel.bind (pd::id::dlyLevel);
    glue.bind (pd::id::glue); mDrive.bind (pd::id::mDrive); volume.bind (pd::id::volume);
    outButton.itemText = [] (int i) { return i == 0 ? juce::String ("MAIN") : "OUT " + juce::String (i); };
    typeSeg.onChoose = [this] (int t) { chooseType (t); };

    for (auto* c : std::initializer_list<juce::Component*> {
             &typeSeg, &modelSeg, &driveModeSeg, &muteSeg, &soloSeg, &tune, &decay, &tone, &knobA, &knobB, &drive, &level, &pan, &width,
             &filter, &rev, &dly, &vel, &gain, &outButton, &chokeButton, &waveform, &soundPrev, &soundNext, &soundButton,
             &revSize, &revDamp, &revLevel, &dlyTime, &dlyFb, &dlyLevel, &glue, &mDrive, &volume, &meter,
             &prevButton, &nextButton, &kitButton, &saveButton, &menuButton, &playButton, &multiButton, &favouriteButton })
        addAndMakeVisible (c);
    addChildComponent (browser);
    browser.setComponentID ("kitBrowser");
    kitButton.setComponentID ("kitButton");
    soundButton.setComponentID ("soundButton");
    playButton.setComponentID ("playButton");
    multiButton.setComponentID ("multiButton");
    waveform.setComponentID ("waveform");

    soundButton.setTooltip (u8 ("Звук слота: список подобранных звуков этого типа из всех китов (и своих)"));
    soundPrev.setTooltip (u8 ("Предыдущий звук этого типа"));
    soundNext.setTooltip (u8 ("Следующий звук этого типа"));
    soundButton.onClick = [this] { showSoundMenu(); };
    soundPrev.onClick = [this] { stepSound (-1); };
    soundNext.onClick = [this] { stepSound (1); };
    waveform.onClick = [this] { proc.playSlot (slot); waveform.flash(); };

    favouriteButton.onClick = [this]
    {
        const int i = proc.kits.indexOf (proc.getKitName());
        if (i < 0) return;
        proc.kits.toggleFavourite (i);
        refreshHeader();
        if (browser.isVisible()) browser.favouritesChanged();
    };
    favouriteButton.setTooltip (u8 ("Отметить кит звёздочкой: он появится в Favourites (во всех проектах)"));
    prevButton.onClick = [this] { stepKit (-1); };
    nextButton.onClick = [this] { stepKit (1); };
    kitButton.onClick = [this]
    {
        if (browser.isVisible()) { browser.onClose(); return; }
        kitButton.setToggleState (true, juce::dontSendNotification);
        browser.open();
    };
    browser.onLoaded = [this] { refreshHeader(); refreshSlotLabels(); };
    browser.onFavouritesChanged = [this] { refreshHeader(); };
    browser.onClose = [this] { browser.setVisible (false); kitButton.setToggleState (false, juce::dontSendNotification); playButton.setToggleState (proc.grooveOn.load(), juce::dontSendNotification); };
    saveButton.onClick = [this] { saveKit(); };
    menuButton.onClick = [this] { showMainMenu(); };
    playButton.setClickingTogglesState (true);
    playButton.onClick = [this] { proc.grooveOn = playButton.getToggleState(); };
    multiButton.onClick = [this] { pd::setMultiOut (proc.state, ! pd::isMultiOut (proc.state)); };
    kitButton.setTooltip (u8 ("Список китов"));
    saveButton.setTooltip (u8 ("Сохранить кит как свой"));
    menuButton.setTooltip (u8 ("Переименовать, удалить, импорт и экспорт китов, сохранить звук слота"));
    playButton.setTooltip (u8 ("Играть грув через кит (темп проекта, без него 140 BPM), пока включено"));
    multiButton.setTooltip (u8 ("Каждый слот — на свой выход (OUT 1–16) для сведения в DAW; ещё раз — всё обратно в MAIN. "
                                "В Logic: инструмент как Multi Output, каналы добавить «+» на канале инструмента. Возвраты реверберации и дилея остаются в MAIN"));

    analogMode = std::make_unique<pd::ui::Segment> (p.state, juce::StringArray { "DIGITAL", "ANALOGUE" },
                                                    u8 ("DIGITAL — каждый удар одинаковый, как сэмпл. ANALOGUE — каждый удар немного иной (высота, длина, громкость), тепло и мягче верх на MAIN"));
    analogMode->bind (pd::id::analog);
    analogMode->setComponentID ("analog");
    addAndMakeVisible (*analogMode);

    setResizable (true, true);
    setResizeLimits (864, 576, 1620, 1080);
    getConstrainer()->setFixedAspectRatio (1080.0 / 720.0);
    setSize (1080, 720);
    for (const auto& pid : pd::allParamIds()) proc.state.addParameterListener (pid, this);
    selectSlot (proc.selectedSlot.load());
    refreshHeader();
    startTimerHz (30);
}

PowerDrumEditor::~PowerDrumEditor()
{
    for (const auto& pid : pd::allParamIds()) proc.state.removeParameterListener (pid, this);
    setLookAndFeel (nullptr);
}

void PowerDrumEditor::selectSlot (int s)
{
    slot = juce::jlimit (0, pd::numSlots - 1, s);
    proc.selectedSlot = slot;
    for (int k = 0; k < pd::numSlots; ++k) pads[(size_t) k]->setSelected (k == slot);
    auto id = [this] (const char* n) { return pd::slotId (slot, n); };
    typeSeg.bind (id (pd::id::type)); modelSeg.bind (id (pd::id::model)); driveModeSeg.bind (id (pd::id::driveMode));
    muteSeg.bind (id (pd::id::mute)); soloSeg.bind (id (pd::id::solo));
    tune.bind (id (pd::id::tune)); decay.bind (id (pd::id::decay)); tone.bind (id (pd::id::tone)); knobA.bind (id (pd::id::knobA));
    knobB.bind (id (pd::id::knobB)); drive.bind (id (pd::id::drive)); level.bind (id (pd::id::level)); pan.bind (id (pd::id::pan));
    width.bind (id (pd::id::width)); filter.bind (id (pd::id::filter)); rev.bind (id (pd::id::rev)); dly.bind (id (pd::id::dly));
    vel.bind (id (pd::id::vel)); gain.bind (id (pd::id::trim));
    outButton.bind (id (pd::id::out)); chokeButton.bind (id (pd::id::choke));
    lastType = -1;
    refreshSlotLabels();
    renderWaveform();
    repaint();
}

void PowerDrumEditor::refreshSlotLabels()
{
    const int t = juce::roundToInt (proc.state.getRawParameterValue (pd::slotId (slot, pd::id::type))->load());
    const auto type = (pd::DrumType) juce::jlimit (0, pd::numTypes - 1, t);
    soundButton.setButtonText (proc.getSlotName (slot).isEmpty() ? juce::String ("-") : proc.getSlotName (slot));
    if (t != lastType)
    {
        lastType = t;
        modelSeg.setLabels (pd::modelNames (type));
        knobA.setTitle (pd::knobAName (type));
        knobB.setTitle (pd::knobBName (type));
        static const char* tipsA[pd::numTypes] = { "Глубина и скорость падения высоты, сила удара", "Хлёсткость: шум и щелчок против тела",
                                                   "Расстояние между хлопками", "Металл против шума", "Металл против шума",
                                                   "Падение высоты в начале", "Металл: FM и кольцевая модуляция", "Прямой удар перед гулом",
                                                   "Свип: < 50 % вверх, > 50 % вниз (у ZAP — глубина)" };
        static const char* tipsB[pd::numTypes] = { "Щелчок атаки", "Тело (тон и длина)", "Комната: хвост за хлопками", "Удержание перед затуханием (открытый хэт ровнее)",
                                                   "Колокол (звон райда)", "Шум кожи и щелчок", "Падение высоты в начале", "Плотность гула (обратная связь)",
                                                   "Грязь: шум, кольцевая модуляция, перегруз" };
        knobA.setTooltip (u8 (tipsA[t])); knobA.slider.setTooltip (u8 (tipsA[t]));
        knobB.setTooltip (u8 (tipsB[t])); knobB.slider.setTooltip (u8 (tipsB[t]));
        decay.valueText = [type] (double v) { return msText (pd::dsp::decaySeconds (type, v)); };
        const bool metal = type == pd::DrumType::hat || type == pd::DrumType::cymbal;
        tune.valueText = [type, metal] (double v)
        {
            if (metal) return (v > 0.05 ? "+" : "") + juce::String (v, 1) + " st";
            return hzText (pd::dsp::tunedPitch (type, v));
        };
        tune.repaint(); decay.repaint();
    }
    for (auto& pad : pads) pad->refresh();
}

void PowerDrumEditor::chooseType (int t)
{
    // TYPE: the first sound of that type from the library, so the slot sounds right at once.
    const auto list = proc.sounds.ofType ((pd::DrumType) t);
    if (! list.empty())
        proc.loadSound (slot, list.front());
    else if (auto* p = dynamic_cast<juce::RangedAudioParameter*> (proc.state.getParameter (pd::slotId (slot, pd::id::type))))
    {
        p->beginChangeGesture(); p->setValueNotifyingHost (p->convertTo0to1 ((float) t)); p->endChangeGesture();
        proc.setSlotName (slot, pd::typeNames()[t]);
    }
    refreshSlotLabels();
    proc.playSlot (slot);
}

void PowerDrumEditor::showSoundMenu()
{
    proc.sounds.rescan();
    const int t = juce::roundToInt (proc.state.getRawParameterValue (pd::slotId (slot, pd::id::type))->load());
    const auto list = proc.sounds.ofType ((pd::DrumType) t);
    juce::PopupMenu m;
    juce::String section;
    const auto current = proc.getSlotName (slot);
    for (int i : list)
    {
        const auto& s = proc.sounds.all()[(size_t) i];
        if (s.kit != section) { section = s.kit; m.addSectionHeader (section); }
        m.addItem (i + 1, s.name, true, s.name == current);
    }
    m.addSeparator();
    m.addItem (-2, "Save this sound...");
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&soundButton).withMaximumNumColumns (3), [this] (int r)
    {
        if (r == -2) { saveSound(); return; }
        if (r <= 0) return;
        proc.loadSound (slot, r - 1);
        refreshSlotLabels();
        proc.playSlot (slot);
    });
}

void PowerDrumEditor::stepSound (int delta)
{
    const int t = juce::roundToInt (proc.state.getRawParameterValue (pd::slotId (slot, pd::id::type))->load());
    const auto list = proc.sounds.ofType ((pd::DrumType) t);
    if (list.empty()) return;
    const int cur = proc.sounds.indexOf (proc.getSlotName (slot), (pd::DrumType) t);
    auto at = std::find (list.begin(), list.end(), cur);
    const int n = (int) list.size();
    const int pos = at == list.end() ? 0 : ((int) (at - list.begin()) + delta + n) % n;
    proc.loadSound (slot, list[(size_t) pos]);
    refreshSlotLabels();
    proc.playSlot (slot);
}

// The selected slot's sound alone (its sound and drive, no sends), rendered offline at 24 kHz for the waveform.
void PowerDrumEditor::renderWaveform()
{
    constexpr double sr = 24000.0;
    auto p = pd::readParams (proc.state);
    for (int s = 0; s < pd::numSlots; ++s) p.slots[(size_t) s].mute = s != slot;
    auto& sp = p.slots[(size_t) slot];
    sp.mute = false; sp.solo = false; sp.out = 0; sp.rev = sp.dly = 0.0f; sp.levelDb = 0.0f; sp.pan = 0.0f;
    p.glue = 0.0f; p.mDrive = 0.0f; p.volumeDb = -12.0f; p.analog = false;
    const double seconds = std::min (3.0, pd::dsp::decaySeconds (sp.type, sp.decay) * 1.2 + 0.15);
    const int total = (int) (seconds * sr), block = 512;
    pd::dsp::DrumEngine engine;
    engine.prepare (sr, block);
    juce::AudioBuffer<float> st (2, total), mono (1, total);
    st.clear();
    std::array<float*, pd::numOutputs> l {}, r {};
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        juce::MidiBuffer midi;
        if (pos == 0) midi.addEvent (juce::MidiMessage::noteOn (10, pd::firstNote + slot, 0.9f), 0);
        l[0] = st.getWritePointer (0, pos); r[0] = st.getWritePointer (1, pos);
        engine.process (l.data(), r.data(), pd::numOutputs, n, midi, p);
    }
    for (int i = 0; i < total; ++i) mono.setSample (0, i, 0.5f * (st.getSample (0, i) + st.getSample (1, i)));
    waveform.setAudio (mono, sr, seconds * 1000.0);
}

void PowerDrumEditor::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setGradientFill (juce::ColourGradient (colours::bgTop, 0, 0, colours::bgBottom, 0, r.getHeight(), false));
    g.fillAll();
    g.setGradientFill (juce::ColourGradient (juce::Colour (0x10ffe6e6), r.getWidth() * 0.15f, 0.0f, juce::Colour (0x00ffe6e6), r.getWidth() * 0.6f, r.getHeight() * 0.6f, true));
    g.fillAll();
    const float s = (float) getWidth() / 1080.0f;
    auto header = getLocalBounds().removeFromTop (juce::roundToInt (60 * s)).reduced (juce::roundToInt (24 * s), 0);
    const auto wordFont = uiFont (19.0f * s, true).withExtraKerningFactor (0.08f);
    g.setFont (wordFont);
    g.setColour (colours::text);
    g.drawText ("POWER", header, juce::Justification::centredLeft);
    g.setColour (colours::accent);
    g.drawText ("DRUM", header.withTrimmedLeft (juce::GlyphArrangement::getStringWidthInt (wordFont, "POWER") + 2), juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.drawHorizontalLine (juce::roundToInt (60 * s), 0.0f, (float) getWidth());
    for (const auto& sec : sections)
    {
        fillPanel (g, sec.area.toFloat(), 12.0f * s);
        g.setFont (uiFont (12.0f * s, true).withExtraKerningFactor (0.2f));
        g.setColour (colours::text);
        g.drawText (sec.title, sec.area.reduced (juce::roundToInt (16 * s), juce::roundToInt (10 * s)).removeFromTop (juce::roundToInt (18 * s)), juce::Justification::centredLeft);
    }
    // the selected slot: "SLOT 4 · D#1"
    g.setFont (uiFont (12.0f * s, true).withExtraKerningFactor (0.2f));
    g.setColour (colours::accent);
    g.drawText ("SLOT " + juce::String (slot + 1) + juce::String::fromUTF8 ("  \xc2\xb7  ") + pd::noteName (slot), slotTitleArea, juce::Justification::centredLeft);
    g.setColour (colours::faint);
    g.setFont (uiFont (10.0f * s, true).withExtraKerningFactor (0.2f));
    g.drawText ("SOUND", soundLabel, juce::Justification::centredLeft);
    g.drawText ("MIX", mixLabel, juce::Justification::centredLeft);
    g.setColour (colours::line);
    g.fillRect (soundLabel.withTrimmedLeft (juce::roundToInt (52 * s)).withHeight (1).translated (0, soundLabel.getHeight() / 2));
    g.fillRect (mixLabel.withTrimmedLeft (juce::roundToInt (34 * s)).withHeight (1).translated (0, mixLabel.getHeight() / 2));
}

void PowerDrumEditor::resized()
{
    const float s = (float) getWidth() / 1080.0f;
    auto S = [s] (float v) { return juce::roundToInt (v * s); };
    sections.clear();

    // Header: logo, DIGITAL | ANALOGUE, the kit in the centre, PLAY, MULTI OUT, menu.
    auto header = getLocalBounds().removeFromTop (S (60)).reduced (S (24), S (14));
    menuButton.setBounds (header.removeFromRight (S (40)));
    header.removeFromRight (S (8));
    multiButton.setBounds (header.removeFromRight (S (100)));
    header.removeFromRight (S (8));
    playButton.setBounds (header.removeFromRight (S (70)));
    if (analogMode != nullptr)
    {
        auto left = getLocalBounds().removeFromTop (S (60)).reduced (S (24), S (14));
        left.removeFromLeft (S (126));
        analogMode->setBounds (left.removeFromLeft (S (164)));
    }
    auto centre = getLocalBounds().removeFromTop (S (60)).withSizeKeepingCentre (S (390), S (32)).translated (S (6), 0);
    prevButton.setBounds (centre.removeFromLeft (S (32)));
    saveButton.setBounds (centre.removeFromRight (S (60)));
    centre.removeFromRight (S (6));
    nextButton.setBounds (centre.removeFromRight (S (32)));
    centre.removeFromLeft (S (6));
    favouriteButton.setBounds (centre.removeFromLeft (S (32)));
    kitButton.setBounds (centre.reduced (S (6), 0));

    auto body = getLocalBounds().withTrimmedTop (S (72)).reduced (S (20), 0).withTrimmedBottom (S (16));
    // Left: the pads (MPC order: slot 1 bottom left), the master under them.
    auto left = body.removeFromLeft (S (440));
    body.removeFromLeft (S (16));
    auto padArea = left.removeFromTop (S (392));
    const int pw = padArea.getWidth() / 4, ph = padArea.getHeight() / 4;
    for (int k = 0; k < pd::numSlots; ++k)
    {
        const int row = 3 - k / 4, col = k % 4;
        pads[(size_t) k]->setBounds (padArea.getX() + col * pw, padArea.getY() + row * ph, pw, ph);
    }
    left.removeFromTop (S (12));
    sections.push_back ({ "MASTER", left });
    auto m = left.reduced (S (12), S (8)).withTrimmedTop (S (22));
    meter.setBounds (m.removeFromRight (S (14)).reduced (0, S (6)));
    m.removeFromRight (S (6));
    const int mh = m.getHeight() / 2, mw = m.getWidth() / 5;
    auto mr1 = m.removeFromTop (mh), mr2 = m;
    for (auto* k : { &revSize, &revDamp, &revLevel, &glue, &mDrive }) k->setBounds (mr1.removeFromLeft (mw));
    for (auto* k : { &dlyTime, &dlyFb, &dlyLevel }) k->setBounds (mr2.removeFromLeft (mw));
    volume.setBounds (mr2);

    // Right: the selected slot.
    auto right = body;
    sections.push_back ({ "", right });
    auto e = right.reduced (S (14), S (10));
    auto titleRow = e.removeFromTop (S (28));
    slotTitleArea = titleRow.removeFromLeft (S (120));
    soundNext.setBounds (titleRow.removeFromRight (S (30)));
    titleRow.removeFromRight (S (4));
    soundPrev.setBounds (titleRow.removeFromLeft (S (30)));
    titleRow.removeFromLeft (S (4));
    soundButton.setBounds (titleRow);
    e.removeFromTop (S (8));
    typeSeg.setBounds (e.removeFromTop (S (26)));
    e.removeFromTop (S (6));
    auto modelRow = e.removeFromTop (S (26));
    modelSeg.setBounds (modelRow.removeFromLeft (modelRow.getWidth() / 2 - S (6)));
    modelRow.removeFromLeft (S (12));
    driveModeSeg.setBounds (modelRow);
    e.removeFromTop (S (8));
    waveform.setBounds (e.removeFromTop (S (118)));
    e.removeFromTop (S (4));
    auto bottomRow = e.removeFromBottom (S (28));
    e.removeFromBottom (S (6));
    const int kh = e.getHeight() / 2;
    auto soundRow = e.removeFromTop (kh), mixRow = e;
    soundLabel = soundRow.removeFromTop (S (16));
    mixLabel = mixRow.removeFromTop (S (16));
    const int sw = soundRow.getWidth() / 6;
    for (auto* k : { &tune, &decay, &tone, &knobA, &knobB, &drive }) k->setBounds (soundRow.removeFromLeft (sw));
    const int xw = mixRow.getWidth() / 8;
    for (auto* k : { &level, &pan, &width, &filter, &rev, &dly, &vel, &gain }) k->setBounds (mixRow.removeFromLeft (xw));
    const int bw = bottomRow.getWidth() / 4;
    outButton.setBounds (bottomRow.removeFromLeft (bw).reduced (S (2), 0));
    chokeButton.setBounds (bottomRow.removeFromLeft (bw).reduced (S (2), 0));
    muteSeg.setBounds (bottomRow.removeFromLeft (bw));
    soloSeg.setBounds (bottomRow);

    browser.setBounds (getLocalBounds().withTrimmedTop (S (60)).reduced (S (24), S (14)));
}

void PowerDrumEditor::timerCallback()
{
    meter.setLevels (proc.meterL.load(), proc.meterR.load());
    waveform.decay();
    for (int k = 0; k < pd::numSlots; ++k)
    {
        if (const float f = proc.slotFlash (k); f > 0.0f) pads[(size_t) k]->hit (f);
        pads[(size_t) k]->decay();
    }
    multiButton.setToggleState (pd::isMultiOut (proc.state), juce::dontSendNotification);
    if (playButton.getToggleState() != proc.grooveOn.load()) playButton.setToggleState (proc.grooveOn.load(), juce::dontSendNotification);
    if (soundDirty.exchange (false)) dirtyAge = 0;
    if (dirtyAge >= 0 && ++dirtyAge > 3)
    {
        dirtyAge = -1;
        refreshSlotLabels();
        renderWaveform();
        refreshHeader();
    }
}

void PowerDrumEditor::refreshHeader()
{
    kitButton.setButtonText (proc.getKitName());
    const int i = proc.kits.indexOf (proc.getKitName());
    favouriteButton.setEnabled (i >= 0);
    favouriteButton.setStarred (i >= 0 && proc.kits.all()[(size_t) i].favourite);
}

void PowerDrumEditor::stepKit (int delta)
{
    if (browser.isVisible()) { browser.keyPressed (juce::KeyPress (delta > 0 ? juce::KeyPress::downKey : juce::KeyPress::upKey)); return; }
    proc.kits.rescan();
    const auto order = pd::filterKits (proc.kits.all(), {}, {});
    if (order.empty()) return;
    const int current = proc.kits.indexOf (proc.getKitName());
    const auto at = std::find (order.begin(), order.end(), current);
    const int n = (int) order.size();
    const int pos = at == order.end() ? 0 : ((int) (at - order.begin()) + delta + n) % n;
    proc.loadKit (order[(size_t) pos]);
    refreshHeader();
    refreshSlotLabels();
}

void PowerDrumEditor::saveKit()
{
    askForName ("Save kit", proc.getKitName(), [this] (juce::String name)
    {
        juce::String e;
        if (! proc.kits.saveUser (name, pd::captureSnapshot (proc.state), proc.getSlotNames(), e)) { showError (e); return; }
        proc.setKitName (name.trim());
        refreshHeader();
    });
}

void PowerDrumEditor::saveSound()
{
    askForName ("Save sound", proc.getSlotName (slot), [this] (juce::String name)
    {
        juce::String e;
        if (! proc.sounds.saveUser (name, pd::captureSound (proc.state, slot), e)) { showError (e); return; }
        proc.setSlotName (slot, name.trim());
        refreshSlotLabels();
    });
}

void PowerDrumEditor::showMainMenu()
{
    const int current = proc.kits.indexOf (proc.getKitName());
    const bool isUser = current >= 0 && ! proc.kits.all()[(size_t) current].isFactory();
    juce::PopupMenu menu;
    menu.addItem (1, "Save kit as...");
    menu.addItem (2, "Rename kit...", isUser);
    menu.addItem (3, "Delete kit...", isUser);
    menu.addSeparator();
    menu.addItem (4, "Import kit...");
    menu.addItem (5, "Export kit...");
    menu.addItem (6, "Show kit folder");
    menu.addSeparator();
    menu.addItem (7, "Save sound of slot " + juce::String (slot + 1) + "...");
    menu.addItem (8, "All slots to MAIN");
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton), [this, current] (int r)
    {
        if (r == 1) saveKit();
        else if (r == 2)
            askForName ("Rename kit", proc.getKitName(), [this, current] (juce::String name)
            {
                juce::String e;
                if (! proc.kits.rename (current, name, e)) { showError (e); return; }
                proc.setKitName (name.trim());
                refreshHeader();
            });
        else if (r == 3)
            juce::AlertWindow::showOkCancelBox (juce::MessageBoxIconType::QuestionIcon, "Delete kit", "Move \"" + proc.getKitName() + "\" to the Trash?",
                                                "Delete", "Cancel", this,
                                                juce::ModalCallbackFunction::create ([this, current] (int ok)
                                                {
                                                    juce::String e;
                                                    if (ok == 1 && ! proc.kits.remove (current, e)) showError (e);
                                                    refreshHeader();
                                                }));
        else if (r == 4)
        {
            chooser = std::make_unique<juce::FileChooser> ("Import PowerDrum kit", juce::File(), "*.pdkit");
            chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File()) return;
                juce::String name, e;
                if (! proc.kits.importFile (fc.getResult(), name, e)) { showError (e); return; }
                proc.loadKit (proc.kits.indexOf (name));
                refreshHeader(); refreshSlotLabels();
            });
        }
        else if (r == 5)
        {
            chooser = std::make_unique<juce::FileChooser> ("Export PowerDrum kit",
                                                           juce::File::getSpecialLocation (juce::File::userDesktopDirectory)
                                                               .getChildFile (juce::File::createLegalFileName (proc.getKitName()) + ".pdkit"), "*.pdkit");
            chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::warnAboutOverwriting, [this] (const juce::FileChooser& fc)
            {
                if (fc.getResult() == juce::File()) return;
                juce::String e;
                if (! proc.kits.exportTo (fc.getResult().withFileExtension ("pdkit"), proc.getKitName(), pd::captureSnapshot (proc.state), proc.getSlotNames(), e))
                    showError (e);
            });
        }
        else if (r == 6) { auto dir = pd::KitLibrary::defaultUserDirectory(); dir.createDirectory(); dir.revealToUser(); }
        else if (r == 7) saveSound();
        else if (r == 8) pd::setMultiOut (proc.state, false);
    });
}

void PowerDrumEditor::askForName (const juce::String& title, const juce::String& initial, std::function<void (juce::String)> done)
{
    auto* w = new juce::AlertWindow (title, "Name", juce::MessageBoxIconType::NoIcon, this);
    w->setLookAndFeel (&lnf);
    w->addTextEditor ("name", initial);
    w->addButton ("OK", 1, juce::KeyPress (juce::KeyPress::returnKey));
    w->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    w->enterModalState (true, juce::ModalCallbackFunction::create ([w, done] (int r)
    {
        const auto name = w->getTextEditorContents ("name").trim();
        if (r == 1 && name.isNotEmpty()) done (name);
    }), true);
}

void PowerDrumEditor::showError (const juce::String& message)
{
    juce::AlertWindow::showMessageBoxAsync (juce::MessageBoxIconType::WarningIcon, "PowerDrum", message, "OK", this);
}
