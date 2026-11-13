#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Params.h"
#include "Lang.h"

// Ovladacie prvky Korgus Nova (ploche, tmava ocelova, zaoblenie 3 px)
namespace nova::ui
{
    inline const juce::Colour bg (0xff1c232c), panel (0xff252e39), panelHi (0xff2b3542), steel (0xff2d4362), steelHi (0xff46628a),
                              text (0xffe4e9ef), dim (0xff8b98a8), arc (0xff8fa6c4), accentB (0xff6f6496), amber (0xff9a7b45), line (0xff34404e);

    class Look : public juce::LookAndFeel_V4
    {
    public:
        Look()
        {
            setColour (juce::Slider::textBoxTextColourId, text);
            setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            setColour (juce::Label::textColourId, text);
            setColour (juce::ComboBox::backgroundColourId, steel);
            setColour (juce::ComboBox::textColourId, text);
            setColour (juce::ComboBox::outlineColourId, juce::Colours::transparentBlack);
            setColour (juce::ComboBox::arrowColourId, text);
            setColour (juce::PopupMenu::backgroundColourId, panelHi);
            setColour (juce::PopupMenu::textColourId, text);
            setColour (juce::PopupMenu::highlightedBackgroundColourId, steelHi);
            setColour (juce::TextEditor::backgroundColourId, juce::Colour (0xff1a2029));
            setColour (juce::TextEditor::textColourId, text);
            setColour (juce::TextEditor::outlineColourId, line);
            setColour (juce::TextEditor::focusedOutlineColourId, steelHi);
            setColour (juce::TextButton::buttonColourId, steel);
            setColour (juce::TextButton::buttonOnColourId, steelHi);
            setColour (juce::TextButton::textColourOffId, text);
            setColour (juce::TextButton::textColourOnId, juce::Colours::white);
            setColour (juce::AlertWindow::backgroundColourId, panelHi);
            setColour (juce::AlertWindow::textColourId, text);
            setColour (juce::AlertWindow::outlineColourId, line);
        }
        juce::Colour arcColour = arc;
        void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider& s) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
            const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
            const bool bip = s.getProperties()["bip"];
            const float ang = a0 + pos * (a1 - a0), mid = (a0 + a1) * 0.5f;
            const float th = juce::jmax (3.0f, rad * 0.16f);
            juce::Path track, val;
            track.addCentredArc (cx, cy, rad - th * 0.5f, rad - th * 0.5f, 0, a0, a1, true);
            if (bip) val.addCentredArc (cx, cy, rad - th * 0.5f, rad - th * 0.5f, 0, juce::jmin (mid, ang), juce::jmax (mid, ang), true);
            else val.addCentredArc (cx, cy, rad - th * 0.5f, rad - th * 0.5f, 0, a0, ang, true);
            g.setColour (steel); g.strokePath (track, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            const auto c = s.findColour (juce::Slider::rotarySliderFillColourId);
            g.setColour (c.isTransparent() ? arcColour : c);
            g.strokePath (val, juce::PathStrokeType (th, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
            const float tr = rad - th * 0.5f, tx = cx + tr * std::sin (ang), ty = cy - tr * std::cos (ang), dot = th * 1.6f;
            g.setColour (s.isEnabled() ? text : dim); g.fillEllipse (tx - dot * 0.5f, ty - dot * 0.5f, dot, dot);
        }
        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
            g.setColour (steel); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (arc); g.fillRoundedRectangle (r.withRight (pos), 3.0f);
            g.setColour (text); g.fillEllipse (pos - 7.0f, r.getCentreY() - 7.0f, 14.0f, 14.0f);
            (void) s;
        }
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const float d = juce::jmin (14.0f, r.getHeight() - 4.0f);
            auto box = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - d * 0.5f, d, d);
            g.setColour (hover ? text : dim); g.drawEllipse (box, 1.5f);
            if (b.getToggleState()) { g.setColour (b.findColour (juce::ToggleButton::tickColourId).isTransparent() ? arc : b.findColour (juce::ToggleButton::tickColourId)); g.fillEllipse (box.reduced (3.0f)); }
            g.setColour (text); g.setFont (juce::FontOptions (13.5f));
            g.drawText (b.getButtonText(), r.withTrimmedLeft (d + 8.0f), juce::Justification::centredLeft);
        }
        void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour& c, bool hover, bool down) override
        {
            auto col = b.getToggleState() ? b.findColour (juce::TextButton::buttonOnColourId) : c;
            if (hover) col = col.brighter (0.1f);
            if (down) col = col.darker (0.1f);
            g.setColour (b.isEnabled() ? col : col.withAlpha (0.5f));
            g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 3.0f);
        }
        juce::Font getTextButtonFont (juce::TextButton&, int h) override { return juce::FontOptions (juce::jmin (14.0f, (float) h * 0.6f)); }
        void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box) override
        {
            g.setColour (box.findColour (juce::ComboBox::backgroundColourId)); g.fillRoundedRectangle (0, 0, (float) w, (float) h, 3.0f);
            juce::Path p; const float ax = (float) w - 12.0f, ay = (float) h * 0.5f;
            p.addTriangle (ax - 4, ay - 2, ax + 4, ay - 2, ax, ay + 3);
            g.setColour (text); g.fillPath (p);
        }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
        juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
        void drawPopupMenuBackground (juce::Graphics& g, int w, int h) override { g.fillAll (panelHi); g.setColour (line); g.drawRect (0, 0, w, h); }
        juce::Label* createSliderTextBox (juce::Slider& s) override
        {
            auto* l = LookAndFeel_V4::createSliderTextBox (s);
            l->setFont (juce::FontOptions (12.5f));
            l->setColour (juce::Label::outlineColourId, juce::Colours::transparentBlack);
            l->setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
            l->setColour (juce::Label::textColourId, dim.brighter (0.3f));
            return l;
        }
    };

    // otocny gombik s nazvom nad a hodnotou pod
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& title = {})
        {
            const auto* sp = findSpec (id);
            label.setText (title.isNotEmpty() ? title : (sp != nullptr ? T (sp->name) : id), juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (juce::FontOptions (12.5f));
            label.setColour (juce::Label::textColourId, dim.brighter (0.2f));
            label.setInterceptsMouseClicks (false, false);
            addAndMakeVisible (label);
            s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
            s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 70, 16);
            s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
            s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
            s.setColour (juce::Slider::rotarySliderFillColourId, arc);
            if (sp != nullptr && (sp->unit == "bip" || sp->unit == "pan" || sp->unit == "ct" || sp->unit == "st" || sp->unit == "oct")) s.getProperties().set ("bip", true);
            addAndMakeVisible (s);
            att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (st, id, s);
            if (sp != nullptr) s.setDoubleClickReturnValue (true, sp->def);
            s.setTooltip (juce::String::fromUTF8 ("Ťahaj myšou · Shift = jemne · dvojklik = pôvodná hodnota"));
        }
        void setTitle (const juce::String& t) { label.setText (t, juce::dontSendNotification); }
        void setArc (juce::Colour c) { s.setColour (juce::Slider::rotarySliderFillColourId, c); }
        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (15));
            s.setBounds (r);
        }
        juce::Slider s; juce::Label label;
    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    // segmentove tlacidla pre parameter s vyberom
    class Seg : public juce::Component
    {
    public:
        Seg (juce::AudioProcessorValueTreeState& st, const juce::String& id, juce::StringArray labels, juce::Colour on = steelHi)
            : names (std::move (labels)), onCol (on)
        {
            auto* prm = st.getParameter (id);
            att = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v) { value = juce::roundToInt (v); repaint(); }, st.undoManager);
            att->sendInitialUpdate();
        }
        void paint (juce::Graphics& g) override
        {
            const int n = names.size(); if (n == 0) return;
            const float gap = 3.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
            for (int i = 0; i < n; ++i)
            {
                auto r = juce::Rectangle<float> ((float) i * (w + gap), 0, w, (float) getHeight());
                const bool sel = i == value, hov = i == hover;
                g.setColour (sel ? onCol : (hov ? steel.brighter (0.12f) : steel));
                g.fillRoundedRectangle (r, 3.0f);
                g.setColour (sel ? juce::Colours::white : text.withAlpha (0.85f));
                g.setFont (juce::FontOptions (juce::jmin (13.5f, (float) getHeight() * 0.55f)));
                g.drawFittedText (names[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
            }
        }
        int indexAt (juce::Point<int> p) const { const int n = names.size(); return n == 0 ? -1 : juce::jlimit (0, n - 1, p.x * n / juce::jmax (1, getWidth())); }
        void mouseDown (const juce::MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i >= 0) att->setValueAsCompleteGesture ((float) i); }
        void mouseMove (const juce::MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        int value = 0;
    private:
        juce::StringArray names; juce::Colour onCol; int hover = -1;
        std::unique_ptr<juce::ParameterAttachment> att;
    };

    // prepinac (kruzok + text) pre zapnute/vypnute
    class Toggle : public juce::ToggleButton
    {
    public:
        Toggle (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& title = {}, juce::Colour c = arc)
        {
            const auto* sp = findSpec (id);
            setButtonText (title.isNotEmpty() ? title : (sp != nullptr ? T (sp->name) : id));
            setColour (juce::ToggleButton::tickColourId, c);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (st, id, *this);
        }
    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> att;
    };

    class Combo : public juce::ComboBox
    {
    public:
        Combo (juce::AudioProcessorValueTreeState& st, const juce::String& id)
        {
            if (const auto* sp = findSpec (id)) for (int i = 0; i < sp->choices.size(); ++i) addItem (T (sp->choices[i]), i + 1);
            att = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (st, id, *this);
        }
    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> att;
    };

    // panel s nazvom
    inline void drawPanel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& title = {}, juce::Colour titleCol = dim)
    {
        g.setColour (panel); g.fillRoundedRectangle (r.toFloat(), 3.0f);
        if (title.isNotEmpty())
        {
            g.setColour (titleCol); g.setFont (juce::FontOptions (12.5f, juce::Font::bold));
            g.drawText (title, r.reduced (12, 8).removeFromTop (16), juce::Justification::centredLeft);
        }
    }
    inline void sectionLabel (juce::Graphics& g, juce::Rectangle<int> r, const juce::String& t)
    {
        g.setColour (dim); g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
        g.drawText (t, r, juce::Justification::centredLeft);
    }
}
