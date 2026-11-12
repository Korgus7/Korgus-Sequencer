#include "PluginEditor.h"

namespace
{
    const juce::Colour bg (0xff1f2630), panel (0xff27303c), steel (0xff2d4362), steelHi (0xff46628a),
                       text (0xffe4e9ef), dim (0xff8b98a8), arc (0xff8fa6c4);
}

NovaLook::NovaLook()
{
    setColour (juce::Slider::textBoxTextColourId, text);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Label::textColourId, text);
}

void NovaLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&)
{
    const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
    const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f, cx = r.getCentreX(), cy = r.getCentreY();
    const float ang = a0 + pos * (a1 - a0);
    juce::Path track, val;
    track.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, a1, true);
    val.addCentredArc (cx, cy, rad - 3, rad - 3, 0, a0, ang, true);
    g.setColour (steel); g.strokePath (track, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    g.setColour (arc);   g.strokePath (val, juce::PathStrokeType (5.0f, juce::PathStrokeType::curved, juce::PathStrokeType::butt));
    const float tx = cx + (rad - 3) * std::sin (ang), ty = cy - (rad - 3) * std::cos (ang);
    g.setColour (text); g.fillEllipse (tx - 5.0f, ty - 5.0f, 10.0f, 10.0f);
}

void NovaEditor::PresetButton::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (selected ? steelHi : (hover ? steel.brighter (0.12f) : steel));
    g.fillRoundedRectangle (r, 3.0f);
    g.setColour (selected ? juce::Colours::white : text.withAlpha (0.9f));
    g.setFont (juce::FontOptions (15.0f, selected ? juce::Font::bold : juce::Font::plain));
    g.drawFittedText (name, getLocalBounds().reduced (6, 2), juce::Justification::centred, 2);
}

NovaEditor::NovaEditor (NovaProcessor& p) : AudioProcessorEditor (p), proc (p)
{
    setLookAndFeel (&look);
    const auto& list = nova::presets();
    for (int i = 0; i < (int) list.size(); ++i)
    {
        auto b = std::make_unique<PresetButton>();
        b->name = juce::String::fromUTF8 (list[(size_t) i].name);
        b->onClick = [this, i]
        {
            if (auto* prm = proc.params.getParameter ("preset"))
            {
                prm->beginChangeGesture();
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) i));
                prm->endChangeGesture();
            }
        };
        addAndMakeVisible (*b);
        buttons.push_back (std::move (b));
    }
    const char* ids[6] = { "bright", "attack", "release", "space", "width", "volume" };
    const char* names[6] = { "Jas", "Nábeh", "Dozvuk", "Priestor", "Šírka", "Hlasitosť" };
    for (int i = 0; i < 6; ++i)
    {
        auto& k = knobs[(size_t) i];
        k.s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        k.s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 80, 18);
        k.s.setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        k.s.setColour (juce::Slider::textBoxTextColourId, text);
        k.s.setRotaryParameters (juce::MathConstants<float>::pi * 1.25f, juce::MathConstants<float>::pi * 2.75f, true);
        addAndMakeVisible (k.s);
        k.att = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, ids[i], k.s);
        k.s.setDoubleClickReturnValue (true, i == 5 ? 0.0 : 0.5);
        k.l.setText (juce::String::fromUTF8 (names[i]), juce::dontSendNotification);
        k.l.setJustificationType (juce::Justification::centred);
        k.l.setFont (juce::FontOptions (14.0f));
        addAndMakeVisible (k.l);
    }
    setSize (780, 350);
    startTimerHz (10);
    timerCallback();
}

NovaEditor::~NovaEditor() { setLookAndFeel (nullptr); }

void NovaEditor::timerCallback()
{
    const int sel = proc.synth.getPreset();
    const int want = (int) proc.params.getRawParameterValue ("preset")->load();
    const int cur = want >= 0 ? want : sel;
    if (cur != shownPreset)
    {
        shownPreset = cur;
        for (int i = 0; i < (int) buttons.size(); ++i) { buttons[(size_t) i]->selected = i == cur; buttons[(size_t) i]->repaint(); }
    }
    juce::String st;
    if (nova::presets()[(size_t) juce::jlimit (0, (int) nova::presets().size() - 1, cur)].engine == 0)
        st = proc.synth.pianoFailed() ? juce::String::fromUTF8 ("Klavírne zvuky sa nepodarilo načítať")
           : (proc.synth.pianoReady() ? juce::String() : juce::String::fromUTF8 ("Načítavam klavír…"));
    if (st != status) { status = st; repaint(); }
}

void NovaEditor::paint (juce::Graphics& g)
{
    g.fillAll (bg);
    auto r = getLocalBounds();
    auto head = r.removeFromTop (44);
    g.setColour (panel); g.fillRect (head);
    g.setColour (text); g.setFont (juce::FontOptions (19.0f, juce::Font::bold));
    g.drawText ("KORGUS NOVA", head.reduced (16, 0), juce::Justification::centredLeft);
    g.setColour (dim); g.setFont (juce::FontOptions (13.0f));
    g.drawText (status, head.reduced (16, 0), juce::Justification::centredRight);
    g.setColour (dim); g.setFont (juce::FontOptions (11.5f));
    g.drawText (juce::String::fromUTF8 ("Klavír: Salamander Grand Piano V3 – Alexander Holm, licencia CC-BY 3.0"),
                getLocalBounds().removeFromBottom (22).reduced (16, 0), juce::Justification::centredLeft);
}

void NovaEditor::resized()
{
    auto r = getLocalBounds().reduced (16, 0);
    r.removeFromTop (44 + 14);
    auto grid = r.removeFromTop (2 * 46 + 8);
    const int cols = 5, gap = 8;
    const int bw = (grid.getWidth() - gap * (cols - 1)) / cols;
    for (int i = 0; i < (int) buttons.size(); ++i)
    {
        const int row = i / cols, col = i % cols;
        buttons[(size_t) i]->setBounds (grid.getX() + col * (bw + gap), grid.getY() + row * (46 + 8), bw, 46);
    }
    r.removeFromTop (14);
    auto kr = r.removeFromTop (150);
    const int kw = kr.getWidth() / 6;
    for (int i = 0; i < 6; ++i)
    {
        auto c = kr.removeFromLeft (kw);
        knobs[(size_t) i].l.setBounds (c.removeFromTop (20));
        knobs[(size_t) i].s.setBounds (c.reduced (14, 0));
    }
}
