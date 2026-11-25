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
            // ryska namiesto gulatej bodky
            const float tr = rad - th * 0.5f;
            juce::Path tick; tick.startNewSubPath (cx + tr * 0.35f * std::sin (ang), cy - tr * 0.35f * std::cos (ang));
            tick.lineTo (cx + (tr + th * 0.5f) * std::sin (ang), cy - (tr + th * 0.5f) * std::cos (ang));
            g.setColour (s.isEnabled() ? text : dim);
            g.strokePath (tick, juce::PathStrokeType (juce::jmax (2.0f, th * 0.7f), juce::PathStrokeType::mitered, juce::PathStrokeType::butt));
        }
        void drawLinearSlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float, float, juce::Slider::SliderStyle, juce::Slider& s) override
        {
            const auto r = juce::Rectangle<float> ((float) x, (float) y + (float) h * 0.5f - 3.0f, (float) w, 6.0f);
            g.setColour (steel); g.fillRoundedRectangle (r, 3.0f);
            g.setColour (arc); g.fillRoundedRectangle (r.withRight (pos), 3.0f);
            g.setColour (s.isEnabled() ? text : dim); g.fillRoundedRectangle (juce::Rectangle<float> (8.0f, 16.0f).withCentre ({ pos, r.getCentreY() }), 2.0f);
            (void) s;
        }
        void drawToggleButton (juce::Graphics& g, juce::ToggleButton& b, bool hover, bool) override
        {
            auto r = b.getLocalBounds().toFloat();
            const float d = juce::jmin (14.0f, r.getHeight() - 4.0f);
            auto box = juce::Rectangle<float> (r.getX() + 2.0f, r.getCentreY() - d * 0.5f, d, d);
            g.setColour (hover ? text : dim); g.drawRoundedRectangle (box, 2.0f, 1.5f);
            if (b.getToggleState()) { g.setColour (b.findColour (juce::ToggleButton::tickColourId).isTransparent() ? arc : b.findColour (juce::ToggleButton::tickColourId)); g.fillRoundedRectangle (box.reduced (3.0f), 1.5f); }
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
        void drawScrollbar (juce::Graphics& g, juce::ScrollBar&, int x, int y, int w, int h, bool vertical, int ts, int tz, bool over, bool down) override
        {
            const auto t = vertical ? juce::Rectangle<int> (x, ts, w, tz) : juce::Rectangle<int> (ts, y, tz, h);
            g.setColour ((over || down) ? steelHi : steel); g.fillRoundedRectangle (t.reduced (2).toFloat(), 2.0f);
        }
        juce::Font getComboBoxFont (juce::ComboBox&) override { return juce::FontOptions (13.0f); }
        juce::Font getPopupMenuFont() override { return juce::FontOptions (14.0f); }
        // okno editora (bublina): zaoblenie 3 px, tlmeny okraj, bez bieleho
        float getCallOutBoxCornerSize (const juce::CallOutBox&) override { return 3.0f; }
        void drawCallOutBoxBackground (juce::CallOutBox&, juce::Graphics& g, const juce::Path& path, juce::Image&) override
        {
            g.setColour (juce::Colours::black.withAlpha (0.35f)); g.fillPath (path, juce::AffineTransform::translation (0.0f, 2.0f));
            g.setColour (bg); g.fillPath (path);
            g.setColour (line); g.strokePath (path, juce::PathStrokeType (1.0f));
        }
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
        // jednotny vzhlad: rovnaky ramcek ikony (pomer 8:5) s okrajom od hrany tlacidla, rovnaka hrubka ciary
        const float h = juce::jmin (r.getHeight() * 0.42f, (r.getWidth() * 0.62f) * 5.0f / 8.0f, 10.0f);
        const float w = h * 1.6f;
        const auto b = juce::Rectangle<float> (w, h).withCentre (r.getCentre());
        const float x0 = b.getX(), y0 = b.getY(), bw = b.getWidth(), bh = b.getHeight(), y1 = b.getBottom(), ym = b.getCentreY();
        auto X = [&] (float t) { return x0 + t * bw; };
        auto Y = [&] (float v) { return y1 - v * bh; };   // v: 0 dole .. 1 hore
        juce::Path p;
        auto curve = [&] (std::function<float (float)> f)
        {
            for (int i = 0; i <= 40; ++i) { const float t = (float) i / 40.0f; if (i == 0) p.startNewSubPath (X (t), Y (f (t))); else p.lineTo (X (t), Y (f (t))); }
        };
        auto steps = [&] (std::initializer_list<float> lv)   // schodiky (arpeggio): kazdy ton = vodorovna ciarka
        {
            const int n = (int) lv.size(); int k = 0;
            for (float v : lv)
            {
                const float xa = X ((float) k / (float) n + 0.04f), xb = X ((float) (k + 1) / (float) n - 0.04f);
                p.startNewSubPath (xa, Y (v)); p.lineTo (xb, Y (v)); ++k;
            }
        };
        auto shelf = [] (float t) { return 0.62f + 0.12f * std::exp (-std::pow ((t - 0.55f) * 10.0f, 2.0f)); };
        switch (kind)
        {
            case icSine:   curve ([] (float t) { return 0.5f + 0.5f * std::sin (t * juce::MathConstants<float>::twoPi); }); break;
            case icTri:    p.startNewSubPath (X (0), Y (0.5f)); p.lineTo (X (0.25f), Y (1)); p.lineTo (X (0.75f), Y (0)); p.lineTo (X (1), Y (0.5f)); break;
            case icSaw:    p.startNewSubPath (X (0), Y (0.5f)); p.lineTo (X (0.5f), Y (1)); p.lineTo (X (0.5f), Y (0)); p.lineTo (X (1), Y (0.5f)); break;
            case icSquare: p.startNewSubPath (X (0), Y (0.5f)); p.lineTo (X (0), Y (1)); p.lineTo (X (0.5f), Y (1)); p.lineTo (X (0.5f), Y (0)); p.lineTo (X (1), Y (0)); p.lineTo (X (1), Y (0.5f)); break;
            case icRandom: { const float hv[4] = { 0.55f, 1.0f, 0.0f, 0.7f }; p.startNewSubPath (X (0), Y (hv[0]));
                             for (int i = 0; i < 4; ++i) { p.lineTo (X ((float) i / 4.0f), Y (hv[i])); p.lineTo (X ((float) (i + 1) / 4.0f), Y (hv[i])); } } break;
            case icLow:    curve ([shelf] (float t) { return t < 0.55f ? shelf (t) : juce::jmax (0.0f, shelf (t) - std::pow ((t - 0.55f) / 0.45f, 1.6f) * 0.74f); }); break;
            case icHigh:   curve ([shelf] (float t) { const float u = 1.0f - t; return u < 0.55f ? shelf (u) : juce::jmax (0.0f, shelf (u) - std::pow ((u - 0.55f) / 0.45f, 1.6f) * 0.74f); }); break;
            case icBand:   curve ([] (float t) { return 0.85f * std::exp (-std::pow ((t - 0.5f) * 4.0f, 2.0f)); }); break;
            case icBody:   curve ([] (float t) { return 0.08f + 0.8f * std::exp (-std::pow ((t - 0.3f) * 8.0f, 2.0f)) + 0.55f * std::exp (-std::pow ((t - 0.72f) * 9.0f, 2.0f)); }); break;
            case icUp:      steps ({ 0.0f, 0.33f, 0.67f, 1.0f }); break;
            case icDown:    steps ({ 1.0f, 0.67f, 0.33f, 0.0f }); break;
            case icUpDown:  steps ({ 0.0f, 0.5f, 1.0f, 0.5f }); break;
            case icPlayed:  steps ({ 0.33f, 1.0f, 0.0f, 0.67f }); break;
            case icRandArp: steps ({ 0.67f, 0.0f, 1.0f, 0.33f });
                            p.startNewSubPath (X (0.45f), Y (0.5f)); p.lineTo (X (0.55f), Y (0.5f)); break;
            case icChord:   for (float v : { 0.0f, 0.5f, 1.0f }) { p.startNewSubPath (X (0.06f), Y (v)); p.lineTo (X (0.42f), Y (v)); p.startNewSubPath (X (0.58f), Y (v)); p.lineTo (X (0.94f), Y (v)); } break;
            default: break;
        }
        (void) ym;
        g.setColour (c);
        g.strokePath (p, juce::PathStrokeType (1.5f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
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
        // poradie zobrazenia: segment i = hodnota parametra map[i]
        void setValueMap (std::initializer_list<int> m) { vmap.clear(); for (int v : m) vmap.add (v); repaint(); }
        std::function<void (int)> onPick;   // po kliku (hodnota parametra)
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
                const bool sel = valOf (i) == value, hov = i == hover;
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
        void mouseDown (const juce::MouseEvent& e) override
        {
            const int i = indexAt (e.getPosition()); if (i < 0) return;
            att->setValueAsCompleteGesture ((float) valOf (i));
            if (onPick) onPick (valOf (i));
        }
        void mouseMove (const juce::MouseEvent& e) override { const int i = indexAt (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
        void mouseExit (const juce::MouseEvent&) override { hover = -1; repaint(); }
        int value = 0;
    private:
        int icon (int i) const { return i < icons.size() ? icons[i] : -1; }
        int valOf (int i) const { return i < vmap.size() ? vmap[i] : i; }
        float wt (int i) const { return i < weights.size() ? weights[i] : 1.0f; }
        float gapAfter (int i) const { return i < groupEnd ? 1.0f : 3.0f; }
        juce::StringArray names, tips; juce::Colour onCol; int hover = -1, groupEnd = -1;
        juce::Array<int> icons, vmap; juce::Array<float> weights;
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
