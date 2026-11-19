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
            l->setMinimumHorizontalScale (0.55f);
            return l;
        }
    };

    // editor poskytuje gombikom MIDI learn (prave tlacidlo)
    struct KnobHost
    {
        virtual ~KnobHost() = default;
        virtual void knobMenu (juce::Component& knob, const juce::String& paramId) = 0;
        virtual int knobCc (const juce::String& paramId) = 0;
        virtual bool knobLearning (const juce::String& paramId) = 0;
    };

    // otocny gombik s nazvom nad a hodnotou pod
    class Knob : public juce::Component
    {
    public:
        Knob (juce::AudioProcessorValueTreeState& st, const juce::String& id, const juce::String& title = {}) : pid (id)
        {
            const auto* sp = findSpec (id);
            label.setText (title.isNotEmpty() ? title : (sp != nullptr ? T (sp->name) : id), juce::dontSendNotification);
            label.setJustificationType (juce::Justification::centred);
            label.setFont (juce::FontOptions (12.5f));
            label.setColour (juce::Label::textColourId, dim.brighter (0.2f));
            label.setInterceptsMouseClicks (false, false);
            label.setMinimumHorizontalScale (0.6f);
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
            s.addMouseListener (this, false);
        }
        void mouseDown (const juce::MouseEvent& e) override
        {
            if (e.mods.isPopupMenu())
                if (auto* h = findParentComponentOfClass<KnobHost>()) h->knobMenu (*this, pid);
        }
        void paintOverChildren (juce::Graphics& g) override
        {
            auto* h = findParentComponentOfClass<KnobHost>();
            if (h == nullptr) return;
            const bool learning = h->knobLearning (pid);
            const int cc = h->knobCc (pid);
            if (! learning && cc < 0) return;
            const juce::String t = learning ? juce::String ("MIDI?") : "CC" + juce::String (cc);
            g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
            const int w = juce::GlyphArrangement::getStringWidthInt (g.getCurrentFont(), t) + 6;
            auto r = juce::Rectangle<int> (getWidth() - w, 15, w, 13);
            g.setColour (learning ? amber.brighter (0.3f) : amber.withAlpha (0.85f)); g.fillRoundedRectangle (r.toFloat(), 3.0f);
            g.setColour (bg); g.drawText (t, r, juce::Justification::centred);
        }
        juce::String pid;
        void setTitle (const juce::String& t) { label.setText (t, juce::dontSendNotification); }
        void setArc (juce::Colour c) { s.setColour (juce::Slider::rotarySliderFillColourId, c); }
        void resized() override
        {
            auto r = getLocalBounds();
            label.setBounds (r.removeFromTop (15));
            s.setBounds (r);
            // uzky gombik: mensie pismo hodnoty, nech sa zmesti (napr. 20.0 kHz)
            for (auto* c : s.getChildren())
                if (auto* l = dynamic_cast<juce::Label*> (c))
                {
                    l->setFont (juce::FontOptions (getWidth() < 54 ? 11.5f : 12.5f));
                    l->setMinimumHorizontalScale (0.55f);
                    l->setBorderSize ({ 1, 1, 1, 1 });
                }
        }
        juce::Slider s; juce::Label label;
    private:
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att;
    };

    // ikony do prepinacov (tenka linka 1.4 px, bez farieb)
    enum SegIcon { icSine = 0, icTri, icSaw, icSquare, icRandom, icLow = 10, icBand, icHigh, icBody,
                   icUp = 20, icDown, icUpDown, icPlayed, icRandArp, icChord };
    inline void drawSegIcon (juce::Graphics& g, juce::Rectangle<float> r, int kind, juce::Colour c)
    {
        const float s = juce::jmin (r.getHeight() - 6.0f, 18.0f), w = juce::jmin (r.getWidth() - 6.0f, s * 1.5f);
        auto b = juce::Rectangle<float> (w, s).withCentre (r.getCentre());
        const float x0 = b.getX(), x1 = b.getRight(), y0 = b.getY(), y1 = b.getBottom(), ym = b.getCentreY(), bw = b.getWidth(), bh = b.getHeight();
        juce::Path p;
        auto curve = [&] (std::function<float (float)> f)   // f: 0..1 -> 0 (dole) .. 1 (hore)
        {
            for (int i = 0; i <= 32; ++i) { const float t = (float) i / 32.0f, x = x0 + t * bw, y = y1 - f (t) * bh; if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        };
        switch (kind)
        {
            case icSine:   curve ([] (float t) { return 0.5f + 0.5f * std::sin (t * juce::MathConstants<float>::twoPi); }); break;
            case icTri:    p.startNewSubPath (x0, ym); p.lineTo (x0 + bw * 0.25f, y0); p.lineTo (x0 + bw * 0.75f, y1); p.lineTo (x1, ym); break;
            case icSaw:    p.startNewSubPath (x0, y1); p.lineTo (x0 + bw * 0.5f, y0); p.lineTo (x0 + bw * 0.5f, y1); p.lineTo (x1, y0); p.lineTo (x1, y1); break;
            case icSquare: p.startNewSubPath (x0, y1); p.lineTo (x0, y0); p.lineTo (x0 + bw * 0.5f, y0); p.lineTo (x0 + bw * 0.5f, y1); p.lineTo (x1, y1); p.lineTo (x1, y0); break;
            case icRandom: { const float h[5] = { 0.3f, 0.9f, 0.1f, 0.65f, 0.4f }; p.startNewSubPath (x0, y1 - h[0] * bh);
                             for (int i = 0; i < 5; ++i) { const float xa = x0 + bw * (float) i / 5.0f, xb = x0 + bw * (float) (i + 1) / 5.0f; p.lineTo (xa, y1 - h[i] * bh); p.lineTo (xb, y1 - h[i] * bh); } } break;
            case icLow:    curve ([] (float t) { return t < 0.55f ? 0.62f + 0.1f * std::exp (-std::pow ((t - 0.5f) * 9.0f, 2.0f)) : juce::jmax (0.0f, 0.68f - (t - 0.55f) * 1.6f); }); break;
            case icHigh:   curve ([] (float t) { return t > 0.45f ? 0.62f + 0.1f * std::exp (-std::pow ((t - 0.5f) * 9.0f, 2.0f)) : juce::jmax (0.0f, 0.68f - (0.45f - t) * 1.6f); }); break;
            case icBand:   curve ([] (float t) { return 0.85f * std::exp (-std::pow ((t - 0.5f) * 4.2f, 2.0f)); }); break;
            case icBody:   curve ([] (float t) { return 0.15f + 0.6f * std::exp (-std::pow ((t - 0.3f) * 9.0f, 2.0f)) + 0.45f * std::exp (-std::pow ((t - 0.72f) * 10.0f, 2.0f)); }); break;
            case icUp: case icDown: case icUpDown: case icPlayed: case icRandArp: case icChord:
            {
                float hs[4] = { 0, 1, 2, 3 }; int n = 4;
                if (kind == icDown) { hs[0] = 3; hs[1] = 2; hs[2] = 1; hs[3] = 0; }
                if (kind == icUpDown) { hs[0] = 0; hs[1] = 2; hs[2] = 3; hs[3] = 1; }
                if (kind == icPlayed) { hs[0] = 1; hs[1] = 3; hs[2] = 0; hs[3] = 2; }
                const float d = 3.0f;
                if (kind == icChord)
                {
                    for (int col = 0; col < 2; ++col) for (int k = 0; k < 3; ++k)
                        p.addEllipse (x0 + bw * (col == 0 ? 0.22f : 0.78f) - d * 0.5f, y0 + bh * (0.1f + 0.4f * (float) k) - d * 0.5f, d, d);
                    g.setColour (c); g.fillPath (p); return;
                }
                if (kind == icRandArp)
                {
                    const float px[5] = { 0.1f, 0.35f, 0.6f, 0.85f, 0.5f }, py[5] = { 0.3f, 0.85f, 0.15f, 0.6f, 0.5f };
                    for (int k = 0; k < 5; ++k) p.addEllipse (x0 + bw * px[k] - d * 0.5f, y0 + bh * py[k] - d * 0.5f, d, d);
                    g.setColour (c); g.fillPath (p); return;
                }
                juce::Path ln, dots;
                for (int k = 0; k < n; ++k)
                {
                    const float x = x0 + bw * (0.08f + 0.28f * (float) k), y = y1 - bh * (0.1f + 0.8f * hs[k] / 3.0f);
                    dots.addEllipse (x - d * 0.5f, y - d * 0.5f, d, d);
                    if (k == 0) ln.startNewSubPath (x, y); else ln.lineTo (x, y);
                }
                g.setColour (c.withAlpha (0.55f)); g.strokePath (ln, juce::PathStrokeType (1.0f));
                g.setColour (c); g.fillPath (dots);
                return;
            }
            default: break;
        }
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.4f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }

    // segmentove tlacidla pre parameter s vyberom (text alebo ikona, tip pri kazdom)
    class Seg : public juce::Component, public juce::TooltipClient
    {
    public:
        Seg (juce::AudioProcessorValueTreeState& st, const juce::String& id, juce::StringArray labels, juce::Colour on = steelHi)
            : names (std::move (labels)), onCol (on)
        {
            auto* prm = st.getParameter (id);
            att = std::make_unique<juce::ParameterAttachment> (*prm, [this] (float v) { value = juce::roundToInt (v); repaint(); }, st.undoManager);
            att->sendInitialUpdate();
        }
        // ikony: -1 = text; vahy sirky; tipy; skupina = segmenty od 0 po groupEnd su spojene (mensia medzera)
        void setIcons (std::initializer_list<int> ic) { icons.clear(); for (int v : ic) icons.add (v); repaint(); }
        void setWeights (std::initializer_list<float> wt) { weights.clear(); for (float v : wt) weights.add (v); repaint(); }
        void setTips (const juce::StringArray& t) { tips = t; }
        void setGroupEnd (int g) { groupEnd = g; }
        juce::String getTooltip() override
        {
            if (hover < 0) return {};
            if (tips.size() == 1) return tips[0];
            if (hover < tips.size() && tips[hover].isNotEmpty()) return tips[hover];
            return icon (hover) >= 0 ? names[hover] : juce::String();
        }
        juce::Rectangle<float> segRect (int i) const
        {
            const int n = names.size();
            float total = 0, gaps = 0;
            for (int k = 0; k < n; ++k) { total += wt (k); if (k < n - 1) gaps += gapAfter (k); }
            const float unit = ((float) getWidth() - gaps) / juce::jmax (0.001f, total);
            float x = 0;
            for (int k = 0; k < i; ++k) x += wt (k) * unit + gapAfter (k);
            return { x, 0.0f, wt (i) * unit, (float) getHeight() };
        }
        void paint (juce::Graphics& g) override
        {
            const int n = names.size(); if (n == 0) return;
            for (int i = 0; i < n; ++i)
            {
                auto r = segRect (i);
                const bool sel = i == value, hov = i == hover;
                g.setColour (sel ? onCol : (hov ? steel.brighter (0.12f) : steel));
                g.fillRoundedRectangle (r, 3.0f);
                const auto tc = sel ? juce::Colours::white : text.withAlpha (0.85f);
                if (icon (i) >= 0) { drawSegIcon (g, r, icon (i), tc); continue; }
                g.setColour (tc);
                g.setFont (juce::FontOptions (juce::jmin (13.5f, (float) getHeight() * 0.55f)));
                g.drawFittedText (names[i], r.toNearestInt().reduced (2, 0), juce::Justification::centred, 1, 0.7f);
            }
        }
        int indexAt (juce::Point<int> p) const
        {
            const int n = names.size(); if (n == 0) return -1;
            for (int i = 0; i < n; ++i) if ((float) p.x < segRect (i).getRight() + gapAfter (i) * 0.5f) return i;
            return n - 1;
        }
        void mouseDown (const juce::MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i >= 0) att->setValueAsCompleteGesture ((float) i); }
        void mouseMove (const juce::MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        int value = 0;
    private:
        int icon (int i) const { return i < icons.size() ? icons[i] : -1; }
        float wt (int i) const { return i < weights.size() ? weights[i] : 1.0f; }
        float gapAfter (int i) const { return i < groupEnd ? 1.0f : 3.0f; }
        juce::StringArray names, tips; juce::Colour onCol; int hover = -1, groupEnd = -1;
        juce::Array<int> icons; juce::Array<float> weights;
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
            setWantsKeyboardFocus (false);   // sipky prepinaju zvuky
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
