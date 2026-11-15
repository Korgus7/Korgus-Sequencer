// Korgus Master - mastering ako samostatny VST3 plugin (jadro: Source/KorgusMaster.h, rovnake ako vstavany efekt v Sequenceri)
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_basics/juce_gui_basics.h>
#include "../../Source/KorgusMaster.h"

namespace
{
    // jazyk ako v Inspirii: Dokumenty\Korgus Inspiria\jazyk.txt ("en" = anglicky), inak slovensky
    bool isEnglish()
    {
        static const bool en = juce::File::getSpecialLocation (juce::File::userDocumentsDirectory)
                                   .getChildFile ("Korgus Inspiria").getChildFile ("jazyk.txt")
                                   .loadFileAsString().trim().startsWithIgnoreCase ("en");
        return en;
    }
    juce::String u8 (const char* s) { return juce::String::fromUTF8 (s); }
    juce::String tr (const char* sk, const char* en) { return isEnglish() ? juce::String (en) : u8 (sk); }

    const char* kIds[5] = { "loud", "punch", "bright", "bass", "width" };

    const juce::Colour cBg (0xff1b2330), cPanel (0xff232d3b), cSteel (0xff2d4362), cGold (0xffc9a35a),
                       cText (0xffd8dee8), cDim (0xff8b97a8);
}

class MasterProcessor final : public juce::AudioProcessor
{
public:
    MasterProcessor()
        : AudioProcessor (BusesProperties().withInput ("Input", juce::AudioChannelSet::stereo(), true)
                                           .withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
          apvts (*this, nullptr, "KorgusMaster", layout())
    {
        style = apvts.getRawParameterValue ("style");
        for (int i = 0; i < 5; ++i) knob[i] = apvts.getRawParameterValue (kIds[i]);
    }

    static juce::AudioProcessorValueTreeState::ParameterLayout layout()
    {
        using namespace juce;
        AudioProcessorValueTreeState::ParameterLayout l;
        StringArray styles;
        for (int i = 0; i < korgus::KorgusMaster::kNumStyles; ++i)
            styles.add (isEnglish() ? String (korgus::KorgusMaster::styleNameEn (i)) : u8 (korgus::KorgusMaster::styleNameSk (i)));
        l.add (std::make_unique<AudioParameterChoice> (ParameterID { "style", 1 }, tr ("Štýl", "Style"), styles, 0));
        for (int i = 0; i < 5; ++i)
        {
            auto name = isEnglish() ? String (korgus::KorgusMaster::knobNameEn (i)) : u8 (korgus::KorgusMaster::knobNameSk (i));
            auto attr = AudioParameterFloatAttributes().withStringFromValueFunction ([i] (float v, int)
            {
                if (i == 0) return String (korgus::KorgusMaster::loudnessToLufs (v), 1) + " LUFS";
                if (i == 4) return String (roundToInt (korgus::KorgusMaster::widthToPercent (v))) + " %";
                if (i == 1) return String (roundToInt (v * 100.0f)) + " %";
                const auto d = (v - 0.5f) * 2.0f;
                return (d > 0.005f ? "+" : "") + String (roundToInt (d * 100.0f));
            });
            l.add (std::make_unique<AudioParameterFloat> (ParameterID { kIds[i], 1 }, name, NormalisableRange<float> (0.0f, 1.0f),
                                                           korgus::KorgusMaster::knobDefault (i), attr));
        }
        return l;
    }

    const juce::String getName() const override { return "Korgus Master"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto in = l.getMainInputChannelSet(), out = l.getMainOutputChannelSet();
        return in == out && (out == juce::AudioChannelSet::mono() || out == juce::AudioChannelSet::stereo());
    }

    void prepareToPlay (double sr, int block) override
    {
        dsp.prepare (sr, block, getTotalNumOutputChannels());
        setLatencySamples (dsp.getLatencySamples());
        pushParams();
    }
    void releaseResources() override {}

    void processBlock (juce::AudioBuffer<float>& b, juce::MidiBuffer&) override
    {
        juce::ScopedNoDenormals nd;
        for (int c = getTotalNumInputChannels(); c < b.getNumChannels(); ++c) b.clear (c, 0, b.getNumSamples());
        pushParams();
        dsp.setBypass (compare.load(), true);
        dsp.process (b.getWritePointer (0), b.getNumChannels() > 1 ? b.getWritePointer (1) : nullptr, b.getNumSamples());
    }

    void pushParams()
    {
        dsp.setParams ((int) style->load(), knob[0]->load(), knob[1]->load(), knob[2]->load(), knob[3]->load(), knob[4]->load());
    }

    bool hasEditor() const override { return true; }
    juce::AudioProcessorEditor* createEditor() override;

    void getStateInformation (juce::MemoryBlock& d) override
    {
        if (auto x = apvts.copyState().createXml()) copyXmlToBinary (*x, d);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
            if (x->hasTagName (apvts.state.getType())) apvts.replaceState (juce::ValueTree::fromXml (*x));
        dsp.resetLearning();
    }

    juce::AudioProcessorValueTreeState apvts;
    korgus::KorgusMaster dsp;
    std::atomic<bool> compare { false };

private:
    std::atomic<float>* style = nullptr;
    std::atomic<float>* knob[5] {};
    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MasterProcessor)
};

//==============================================================================
class MasterLook final : public juce::LookAndFeel_V4
{
public:
    MasterLook()
    {
        setColour (juce::ComboBox::backgroundColourId, cSteel);
        setColour (juce::ComboBox::outlineColourId, cSteel.brighter (0.2f));
        setColour (juce::ComboBox::textColourId, cText);
        setColour (juce::ComboBox::arrowColourId, cText);
        setColour (juce::PopupMenu::backgroundColourId, cPanel);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, cSteel);
        setColour (juce::PopupMenu::textColourId, cText);
        setColour (juce::TextButton::buttonColourId, cSteel);
        setColour (juce::TextButton::buttonOnColourId, cGold);
        setColour (juce::TextButton::textColourOffId, cText);
        setColour (juce::TextButton::textColourOnId, cBg);
    }
    void drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override
    {
        const auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (6.0f);
        const float rad = juce::jmin (r.getWidth(), r.getHeight()) * 0.5f;
        const auto c = r.getCentre();
        juce::Path bg, fg;
        bg.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a1, true);
        fg.addCentredArc (c.x, c.y, rad, rad, 0.0f, a0, a0 + pos * (a1 - a0), true);
        g.setColour (cSteel); g.strokePath (bg, juce::PathStrokeType (4.0f));
        g.setColour (cGold);  g.strokePath (fg, juce::PathStrokeType (4.0f));
        const float ang = a0 + pos * (a1 - a0);
        g.setColour (cText);
        g.drawLine (c.x + std::sin (ang) * rad * 0.35f, c.y - std::cos (ang) * rad * 0.35f,
                    c.x + std::sin (ang) * rad * 0.85f, c.y - std::cos (ang) * rad * 0.85f, 2.0f);
    }
    void drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down) override
    {
        auto col = b.getToggleState() ? cGold : cSteel;
        if (over || down) col = col.brighter (0.12f);
        g.setColour (col);
        g.fillRoundedRectangle (b.getLocalBounds().toFloat().reduced (0.5f), 3.0f);
    }
    void drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox&) override
    {
        g.setColour (cSteel); g.fillRoundedRectangle (0.0f, 0.0f, (float) w, (float) h, 3.0f);
        juce::Path p; const float ax = (float) w - 16.0f, ay = (float) h * 0.5f;
        p.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
        g.setColour (cText); g.fillPath (p);
    }
};

class MasterEditor final : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MasterEditor (MasterProcessor& p) : AudioProcessorEditor (p), proc (p)
    {
        setLookAndFeel (&look);
        addAndMakeVisible (styleBox);
        for (int i = 0; i < korgus::KorgusMaster::kNumStyles; ++i)
            styleBox.addItem (isEnglish() ? juce::String (korgus::KorgusMaster::styleNameEn (i)) : u8 (korgus::KorgusMaster::styleNameSk (i)), i + 1);
        styleAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (p.apvts, "style", styleBox);

        for (int i = 0; i < 5; ++i)
        {
            auto& s = knobs[i];
            s.setSliderStyle (juce::Slider::RotaryVerticalDrag);
            s.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
            s.setDoubleClickReturnValue (true, korgus::KorgusMaster::knobDefault (i));
            s.setPopupDisplayEnabled (false, false, nullptr);
            addAndMakeVisible (s);
            atts[i] = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.apvts, kIds[i], s);
            s.onValueChange = [this] { repaint(); };
        }
        knobs[0].setTooltip (tr ("Cieľová hlasitosť. −14 LUFS = Spotify, YouTube, TikTok. Vyššie číslo = hlasnejšie.",
                                 "Target loudness. −14 LUFS = Spotify, YouTube, TikTok. Higher = louder."));
        knobs[1].setTooltip (tr ("Viac = zhutnenejší, údernejší zvuk. 0 = bez kompresie.", "More = denser, punchier sound. 0 = no compression."));
        knobs[2].setTooltip (tr ("Tmavší / jasnejší zvuk.", "Darker / brighter sound."));
        knobs[3].setTooltip (tr ("Menej / viac basov.", "Less / more bass."));
        knobs[4].setTooltip (tr ("Šírka sterea. Basy ostávajú v strede.", "Stereo width. Bass stays centred."));

        compareBtn.setButtonText (tr ("Porovnať", "Compare"));
        compareBtn.setClickingTogglesState (true);
        compareBtn.setTooltip (tr ("Prepne na pôvodný zvuk v rovnakej hlasitosti, aby si počul rozdiel.",
                                   "Switches to the original sound at the same loudness so you hear the difference."));
        compareBtn.onClick = [this] { proc.compare.store (compareBtn.getToggleState()); };
        compareBtn.setToggleState (proc.compare.load(), juce::dontSendNotification);
        addAndMakeVisible (compareBtn);

        setSize (560, 330);
        startTimerHz (30);
    }
    ~MasterEditor() override { setLookAndFeel (nullptr); }

    void resized() override
    {
        styleBox.setBounds (20, 52, 200, 28);
        compareBtn.setBounds (getWidth() - 130, 52, 110, 28);
        const int kw = 96;
        for (int i = 0; i < 5; ++i) knobs[i].setBounds (20 + i * (kw + 10), 112, kw, kw - 16);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (cBg);
        g.setColour (cText);
        g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
        g.drawText ("KORGUS MASTER", 20, 12, 300, 28, juce::Justification::centredLeft);
        g.setFont (juce::FontOptions (12.0f));
        g.setColour (cDim);
        g.drawText (tr ("Štýl", "Style"), 230, 52, 60, 28, juce::Justification::centredLeft);

        auto& ap = proc.apvts;
        for (int i = 0; i < 5; ++i)
        {
            const auto kb = knobs[i].getBounds();
            auto* prm = ap.getParameter (kIds[i]);
            g.setColour (cDim);
            g.setFont (juce::FontOptions (12.5f));
            g.drawText (prm->getName (40), kb.getX(), kb.getY() - 18, kb.getWidth(), 16, juce::Justification::centred);
            g.setColour (cText);
            g.drawText (prm->getCurrentValueAsText(), kb.getX(), kb.getBottom(), kb.getWidth(), 16, juce::Justification::centred);
        }

        // merace
        auto area = juce::Rectangle<int> (20, 238, getWidth() - 40, 76);
        g.setColour (cPanel); g.fillRoundedRectangle (area.toFloat(), 3.0f);
        area.reduce (12, 8);
        auto col = [&] (juce::Rectangle<int> r, const juce::String& label, const juce::String& val, juce::Colour vc)
        {
            g.setColour (cDim); g.setFont (juce::FontOptions (11.5f));
            g.drawText (label, r.removeFromTop (16), juce::Justification::centredLeft);
            g.setColour (vc); g.setFont (juce::FontOptions (20.0f, juce::Font::bold));
            g.drawText (val, r.removeFromTop (28), juce::Justification::centredLeft);
        };
        auto fmt = [] (float v, const char* unit) { return v <= -99.0f ? juce::String ("–") : juce::String (v, 1) + unit; };
        const int cw = area.getWidth() / 5;
        col (area.withWidth (cw), tr ("Hlasitosť teraz", "Loudness now"), fmt (m.outShortTerm, " LUFS"), cText);
        col (area.withX (area.getX() + cw).withWidth (cw), tr ("Celková", "Integrated"), fmt (m.outIntegrated, " LUFS"),
             std::abs (m.outIntegrated - m.targetLufs) < 1.0f ? cGold : cText);
        col (area.withX (area.getX() + 2 * cw).withWidth (cw), tr ("Špička", "True peak"), fmt (m.truePeakMaxDb, " dB"),
             m.truePeakMaxDb > -0.5f ? juce::Colour (0xffd07a5a) : cText);
        col (area.withX (area.getX() + 3 * cw).withWidth (cw), tr ("Limiter", "Limiter"),
             juce::String ("-") + juce::String (m.limiterGrDb, 1) + " dB", cText);

        // pasma kompresora
        auto br = area.withX (area.getX() + 4 * cw).withWidth (cw);
        g.setColour (cDim); g.setFont (juce::FontOptions (11.5f));
        g.drawText (tr ("Pásma", "Bands"), br.removeFromTop (16), juce::Justification::centredLeft);
        const int bw = (br.getWidth() - 12) / 4;
        for (int b = 0; b < 4; ++b)
        {
            auto r = juce::Rectangle<int> (br.getX() + b * (bw + 4), br.getY() + 2, bw - 2, br.getHeight() - 4);
            g.setColour (cSteel); g.fillRoundedRectangle (r.toFloat(), 2.0f);
            const float f = juce::jlimit (0.0f, 1.0f, m.bandGrDb[b] / 9.0f);
            g.setColour (cGold);
            g.fillRoundedRectangle (r.withHeight (juce::roundToInt (r.getHeight() * f)).toFloat(), 2.0f);
        }
    }

private:
    void timerCallback() override
    {
        m = proc.dsp.getMeters();
        repaint (0, 230, getWidth(), getHeight() - 230);
    }
    void mouseDoubleClick (const juce::MouseEvent& e) override
    {
        if (e.y > 230) proc.dsp.resetMeters();
    }

    MasterProcessor& proc;
    MasterLook look;
    juce::ComboBox styleBox;
    juce::Slider knobs[5];
    juce::TextButton compareBtn;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> styleAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> atts[5];
    korgus::KorgusMaster::Meters m;
};

juce::AudioProcessorEditor* MasterProcessor::createEditor() { return new MasterEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new MasterProcessor(); }
