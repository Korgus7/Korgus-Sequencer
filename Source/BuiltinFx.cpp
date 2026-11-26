#include "GatePatterns.h"
#include "../Nova/Source/SuperVerb.h"
#include "KorgusMaster.h"
#include "KorgusVocoder.h"
#include <array>
#include "BuiltinFx.h"
#include "UiCommon.h"
#include "KorgusEqView.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>

namespace
{
using namespace juce;

//==============================================================================
struct FxParam final : public AudioPluginInstance::HostedParameter
{
    FxParam (String i, String n, float lo, float hi, float def, String u, float skew = 1.0f, int decimals = 1)
        : id (std::move (i)), name (std::move (n)), unit (std::move (u)), range (lo, hi, 0.0f, skew), dec (decimals)
    {
        defNorm = range.convertTo0to1 (def);
        value.store (defNorm);
    }

    float get() const               { return range.convertFrom0to1 (value.load()); }
    float getValue() const override { return value.load(); }
    void setValue (float v) override { value.store (jlimit (0.0f, 1.0f, v)); }
    float getDefaultValue() const override { return defNorm; }
    String getName (int maxLen) const override { return name.substring (0, maxLen); }
    String getLabel() const override { return unit; }
    String getParameterID() const override { return id; }
    String getText (float v, int) const override
    {
        const auto x = range.convertFrom0to1 (v);
        if (textFn) if (auto t = textFn (x); t.isNotEmpty()) return t;
        if (! choices.isEmpty()) return choices[jlimit (0, choices.size() - 1, roundToInt (x))];
        return (dec == 0 ? String (roundToInt (x)) : String (x, dec)) + (unit.isEmpty() ? "" : " " + unit);
    }
    bool isDiscrete() const override { return toggle || ! choices.isEmpty(); }
    bool isBoolean() const override { return toggle; }
    int getNumSteps() const override { return toggle ? 2 : (choices.isEmpty() ? AudioProcessor::getDefaultNumParameterSteps() : choices.size()); }
    float getValueForText (const String& t) const override { return range.convertTo0to1 (jlimit (range.start, range.end, t.getFloatValue())); }

    const String id, name, unit;
    NormalisableRange<float> range;
    const int dec;
    float defNorm = 0;
    StringArray choices;                     // vyber zo zoznamu (rozbalovaci zoznam)
    bool toggle = false;                     // zap/vyp (prepinac)
    bool inEditor = true;                    // false = ovlada sa inde (napr. ikony v grafe)
    bool transient = false;                  // neuklada sa do stavu piesne (napr. A/B porovnanie)
    bool wide = false;                       // dlhsie texty vo vyberovom zozname: siroky stlpec v okne
    std::function<String (float)> textFn;    // vlastny text hodnoty
    String tip;                              // popis pri prejdeni mysou
    std::atomic<float> value { 0 };

    // zmena z grafu (ako tahanie gombika)
    void setFromUi (float v) { setValueNotifyingHost (range.convertTo0to1 (jlimit (range.start, range.end, v))); }
};

//==============================================================================
// kruhovy zasobnik zvuku pre grafy (spektrum); zapisuje audio vlakno, cita okno grafu
struct ScopeFifo
{
    static constexpr int N = 8192;
    std::array<float, N> buf {};
    std::atomic<int> w { 0 };
    std::atomic<int> users { 0 };
    void push (const float* l, const float* r, int n)
    {
        if (users.load() <= 0 || l == nullptr) return;
        int p = w.load();
        for (int i = 0; i < n; ++i) { buf[(size_t) p] = r != nullptr ? 0.5f * (l[i] + r[i]) : l[i]; p = (p + 1) & (N - 1); }
        w.store (p);
    }
    void read (float* out, int n) const
    {
        const int p = w.load();
        for (int i = 0; i < n; ++i) out[i] = buf[(size_t) ((p - n + i) & (N - 1))];
    }
};

// spektrum z ScopeFifo (FFT 2048, vyhladene), v dB
struct Spectrum
{
    static constexpr int order = 11, size = 1 << order;
    dsp::FFT fft { order };
    dsp::WindowingFunction<float> win { (size_t) size, dsp::WindowingFunction<float>::hann, false };
    std::vector<float> data = std::vector<float> ((size_t) size * 2, 0.0f);
    std::vector<float> db = std::vector<float> ((size_t) size / 2, -100.0f);
    void update (const ScopeFifo& f, float smooth = 0.7f)
    {
        f.read (data.data(), size);
        std::fill (data.begin() + size, data.end(), 0.0f);
        win.multiplyWithWindowingTable (data.data(), (size_t) size);
        fft.performFrequencyOnlyForwardTransform (data.data());
        const float norm = 4.0f / (float) size;
        for (int i = 0; i < size / 2; ++i)
        {
            const float v = Decibels::gainToDecibels (data[(size_t) i] * norm, -100.0f);
            db[(size_t) i] = v > db[(size_t) i] ? v : db[(size_t) i] * smooth + v * (1.0f - smooth);
        }
    }
    float at (float hz, double sr) const
    {
        const float bin = hz / (float) sr * (float) size;
        const int i = jlimit (1, size / 2 - 2, (int) bin);
        const float fr = jlimit (0.0f, 1.0f, bin - (float) i);
        return db[(size_t) i] + (db[(size_t) i + 1] - db[(size_t) i]) * fr;
    }
};

// spolocne farby grafov (tlmene ocelove tony)
namespace GCol
{
    const Colour bg (0xff0e131b), panel (0xff151c27), grid (0xff1e2938), steel (0xff2d4362), text (0xffc9d2dc),
                 dim (0xff8a96a6), gold (0xffc9a35a), line (0xff8fb3d9), green (0xff7fb38a);
}

//==============================================================================
// prepinac namiesto rozbalovacieho zoznamu (2-4 kratke volby): rad tlacidiel, zvolene je ocelove
class ChoiceSeg final : public Component, public SettableTooltipClient
{
public:
    explicit ChoiceSeg (const StringArray& names) : items (names) {}
    static int neededWidth (const StringArray& names)
    {
        int w = 0; const Font f (FontOptions (13.0f));
        for (auto& n : names) w += GlyphArrangement::getStringWidthInt (f, n) + 14;
        return w + 3 * (names.size() - 1);
    }
    std::function<void (int)> onPick;
    void setSelected (int i) { if (i != sel) { sel = i; repaint(); } }
    void paint (Graphics& g) override
    {
        const int n = items.size(); if (n == 0) return;
        const float gap = 3.0f, w = ((float) getWidth() - gap * (float) (n - 1)) / (float) n;
        for (int i = 0; i < n; ++i)
        {
            auto rc = Rectangle<float> ((float) i * (w + gap), 0.0f, w, (float) getHeight());
            g.setColour (i == sel ? Colour (0xff46628a) : (i == hover ? Colour (0xff34496a) : Colour (0xff2d4362).withAlpha (0.55f)));
            g.fillRoundedRectangle (rc, 3.0f);
            g.setColour (i == sel ? Colours::white : Colour (0xffc9d2dc));
            g.setFont (FontOptions (13.0f));
            g.drawFittedText (items[i], rc.toNearestInt().reduced (3, 0), Justification::centred, 1, 0.75f);
        }
    }
    int at (Point<int> p) const { const int n = items.size(); return n == 0 ? -1 : jlimit (0, n - 1, p.x * n / jmax (1, getWidth())); }
    void mouseDown (const MouseEvent& e) override { const int i = at (e.getPosition()); if (i >= 0 && onPick) { setSelected (i); onPick (i); } }
    void mouseMove (const MouseEvent& e) override { const int i = at (e.getPosition()); if (i != hover) { hover = i; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
private:
    StringArray items; int sel = -1, hover = -1;
};

class FxBase;

class KnobEditor final : public AudioProcessorEditor, private Timer
{
public:
    KnobEditor (FxBase& p, const std::vector<FxParam*>& ps);
    ~KnobEditor() override { stopTimer(); }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff151c27));
        g.setColour (Colour (0xff1c2532));
        if (! comps.empty()) g.fillRoundedRectangle (getLocalBounds().toFloat().reduced (4.0f).withTrimmedTop ((float) top + 2.0f), 3.0f);
        if (head > 0) paintHeader (g);
        g.setColour (Colour (0xffc9d2dc));
        g.setFont (FontOptions (13.0f));
        for (size_t i = 0; i < labels.size(); ++i)
            if (! params[i]->toggle)
                g.drawFittedText (labels[i], comps[i]->getBounds().withY (top + 8).withHeight (18), Justification::centred, 1);
    }

    void resized() override
    {
        layoutHeader();
        if (display != nullptr) display->setBounds (8, head + 8, getWidth() - 16, top - head - 12);
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (top + 28).withTrimmedBottom (8);
        int tot = 0;
        for (size_t i = 0; i < comps.size(); ++i) tot += colWidth (i);
        const int full = r.getWidth();
        for (size_t i = 0; i < comps.size(); ++i)
        {
            auto cell = r.removeFromLeft (full * colWidth (i) / jmax (1, tot)).reduced (4, 0);
            if (params[i]->toggle) comps[i]->setBounds (cell.withY (top + 4).withHeight (26));
            else if (! params[i]->choices.isEmpty()) comps[i]->setBounds (cell.withHeight (26).withY (cell.getCentreY() - 20));
            else comps[i]->setBounds (cell);
        }
    }

private:
    int colWidth (size_t i) const { return segW[i] > 0 ? segW[i] : (params[i]->wide ? 128 : 92); }
    std::vector<int> segW;    // sirka stlpca pre prepinac (0 = bezny stlpec)
    void buildHeader();
    void layoutHeader();
    void paintHeader (Graphics& g);
    void timerCallback() override
    {
        if (head > 0) { updateHeader(); repaint (meterArea); }
        for (size_t i = 0; i < comps.size(); ++i)
        {
            auto* p = params[i];
            if (auto* k = dynamic_cast<Slider*> (comps[i].get()))
            {
                if (! k->isMouseButtonDown()) k->setValue (p->get(), dontSendNotification);
                k->updateText();
            }
            else if (auto* c = dynamic_cast<ComboBox*> (comps[i].get()))
                c->setSelectedId (roundToInt (p->get()) + 1, dontSendNotification);
            else if (auto* sg = dynamic_cast<ChoiceSeg*> (comps[i].get()))
                sg->setSelected (roundToInt (p->get()));
            else if (auto* t = dynamic_cast<ToggleButton*> (comps[i].get()))
                t->setToggleState (p->get() > 0.5f, dontSendNotification);
        }
    }

    void updateHeader();
    FxBase& fx;
    std::vector<FxParam*> params;
    std::unique_ptr<Component> display;   // graf (napr. krivka PulseDip)
    int top = 0, head = 0;
    TooltipWindow tips { this, 500 };
    ComboBox presetBox;
    TextButton abA { "A" }, abB { "B" }, byp;
    Rectangle<int> meterArea;
    std::vector<std::unique_ptr<Component>> comps;
    std::vector<String> labels;
};

//==============================================================================
class FxBase : public AudioPluginInstance
{
public:
    FxBase (String nm, String key) : AudioPluginInstance (BusesProperties()
                                         .withInput ("In", AudioChannelSet::stereo())
                                         .withOutput ("Out", AudioChannelSet::stereo())),
                                     fxName (std::move (nm)), fxKey (std::move (key)) {}
    FxBase (String nm, String key, bool instrument) : AudioPluginInstance (BusesProperties().withOutput ("Out", AudioChannelSet::stereo())),
                                                      fxName (std::move (nm)), fxKey (std::move (key)), isInst (instrument) {}

    FxParam* add (String id, const char* utf8Name, float lo, float hi, float def, String u, float skew = 1.0f, int dec = 1)
    {
        auto p = std::make_unique<FxParam> (std::move (id), sk (utf8Name), lo, hi, def, std::move (u), skew, dec);
        auto* raw = p.get();
        addHostedParameter (std::move (p));
        knobs.push_back (raw);
        return raw;
    }

    static PluginDescription describe (const String& name, const String& key, bool instrument = false)
    {
        PluginDescription d;
        d.name = name; d.descriptiveName = name;
        d.pluginFormatName = BuiltinFx::formatName;
        d.category = "Effect";
        d.manufacturerName = "Korgus";
        d.version = "1.0";
        d.fileOrIdentifier = "korgus:" + key;
        d.uniqueId = d.deprecatedUid = (int) (String ("korgus:" + key).hashCode() & 0x7fffffff);
        d.isInstrument = instrument;
        d.numInputChannels = instrument ? 0 : 2; d.numOutputChannels = 2;
        if (instrument) d.category = "Instrument";
        return d;
    }

    void fillInPluginDescription (PluginDescription& d) const override { d = describe (fxName, fxKey, isInst); }
    const String getName() const override { return fxName; }

    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return (out == AudioChannelSet::mono() || out == AudioChannelSet::stereo()) && l.getMainInputChannelSet() == out;
    }

    // --- spolocne pre vsetky efekty: vypnutie, merace, predvolby, A/B, spektrum
    std::atomic<bool> bypass { false };
    std::atomic<float> meterIn { -60.0f }, meterOut { -60.0f };
    ScopeFifo scope;                                          // vystup efektu pre grafy
    struct Preset { const char* name; std::vector<std::pair<const char*, float>> v; };
    virtual std::vector<Preset> presets() const { return {}; }
    virtual bool presetsInHeader() const { return true; }     // false = predvolby su tlacidla v grafe
    virtual bool showHeader() const { return ! isInst; }
    double sampleRate() const { return rate; }
    FxParam* find (const String& id) const { for (auto* p : knobs) if (p->id == id) return p; return nullptr; }
    void applyPreset (const Preset& pr) { for (auto& [id, v] : pr.v) if (auto* q = find (id)) q->setFromUi (v); }
    int abSlot = 0;
    void switchAB (int to)
    {
        if (to == abSlot) return;
        auto& cur = ab[(size_t) abSlot];
        cur.clear();
        for (auto* p : knobs) cur.push_back (p->getValue());
        abSlot = to;
        auto& nx = ab[(size_t) to];
        if (nx.size() != knobs.size()) { nx = cur; return; }
        for (size_t i = 0; i < knobs.size(); ++i) if (! knobs[i]->transient) knobs[i]->setValueNotifyingHost (nx[i]);
    }

    void prepareToPlay (double sr, int bs) override
    {
        rate = sr;
        spec = { sr, (uint32) jmax (1, bs), (uint32) jmax (1, getTotalNumOutputChannels()) };
        prepare();
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buf, MidiBuffer& midi) override
    {
        ScopedNoDenormals nd;
        const int ch = jmin (buf.getNumChannels(), (int) spec.numChannels);
        if (ch <= 0 || buf.getNumSamples() == 0) return;
        AudioBuffer<float> view (buf.getArrayOfWritePointers(), ch, buf.getNumSamples());
        curMidi = acceptsMidi() ? &midi : nullptr;
        const int n = buf.getNumSamples();
        const float dec = (float) std::exp (-(double) n / (0.3 * rate));
        auto peakDb = [&] { float pk = 0; for (int c = 0; c < ch; ++c) pk = jmax (pk, view.getMagnitude (c, 0, n)); return Decibels::gainToDecibels (pk, -60.0f); };
        const float in = isInst ? -60.0f : peakDb();
        meterIn = jmax (in, -60.0f + (meterIn.load() + 60.0f) * dec);
        if (! bypass.load() || isInst || acceptsMidi()) process (view);
        curMidi = nullptr;
        meterOut = jmax (peakDb(), -60.0f + (meterOut.load() + 60.0f) * dec);
        scope.push (view.getReadPointer (0), ch > 1 ? view.getReadPointer (1) : nullptr, n);
    }

    double getTailLengthSeconds() const override { return tail; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override    { return true; }
    AudioProcessorEditor* createEditor() override { return new KnobEditor (*this, knobs); }
    virtual std::unique_ptr<Component> makeDisplay() { return nullptr; }
    virtual int displayHeight() const { return 150; }
    virtual int displayWidth() const { return 460; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX");
        x.setAttribute ("type", fxKey);
        for (auto* p : knobs) if (! p->transient) x.setAttribute (p->id, (double) p->get());
        if (bypass.load()) x.setAttribute ("bypass", 1);
        copyXmlToBinary (x, mb);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            bypass = x->getIntAttribute ("bypass", 0) != 0;
            for (auto* p : knobs)
                if (! p->transient && x->hasAttribute (p->id))
                    p->setValueNotifyingHost (p->range.convertTo0to1 ((float) x->getDoubleAttribute (p->id)));
        }
    }

    void setKey (const float* l, const float* r, int n) { scL = l; scR = r != nullptr ? r : l; scN = l != nullptr ? n : 0; }
    void setSong (int tonic, int scale, const std::vector<BuiltinFx::ChordSpan>* chords) { songKey = tonic; songScale = scale; songChords = chords; }
    virtual void newSong() {}                                 // nova / nacitana pieseň (hlavne vlakno)
    virtual bool wantsPrepass() const { return false; }       // export: potrebuje predbezny prechod
    virtual void exportPhase (int) {}                         // 0 normal, 1 ucenie, 2 zaverecny prechod

protected:
    virtual void prepare() = 0;
    const float* scL = nullptr; const float* scR = nullptr; int scN = 0;
    int songKey = -1, songScale = -1;                         // tonina piesne (-1 = nezname)
    const std::vector<BuiltinFx::ChordSpan>* songChords = nullptr;
    const MidiBuffer* curMidi = nullptr;                      // MIDI stopy (len pre efekty s acceptsMidi)
    virtual void process (AudioBuffer<float>&) = 0;

    double rate = 44100;
    dsp::ProcessSpec spec { 44100, 512, 2 };
    double tail = 0;
    std::vector<FxParam*> knobs;

private:
    String fxName, fxKey;
    bool isInst = false;
    std::array<std::vector<float>, 2> ab;
};

KnobEditor::KnobEditor (FxBase& p, const std::vector<FxParam*>& ps) : AudioProcessorEditor (p), fx (p)
{
    for (auto* q : ps) if (q->inEditor) params.push_back (q);
    const Colour text (0xffc9d2dc), steel (0xff2d4362), fill (0xff6f8fb3);
    for (auto* prm : params)
    {
        std::unique_ptr<Component> comp;
        int segWidth = 0;
        if (prm->toggle)
        {
            auto t = std::make_unique<ToggleButton> (prm->name);
            t->setColour (ToggleButton::textColourId, text);
            t->setColour (ToggleButton::tickColourId, text);
            t->setToggleState (prm->get() > 0.5f, dontSendNotification);
            auto* raw = t.get();
            t->onClick = [prm, raw] { prm->setValueNotifyingHost (raw->getToggleState() ? 1.0f : 0.0f); };
            comp = std::move (t);
        }
        else if (! prm->choices.isEmpty() && prm->choices.size() <= 4 && ! prm->choices.contains (String())
                 && ChoiceSeg::neededWidth (prm->choices) <= 230)
        {
            auto c = std::make_unique<ChoiceSeg> (prm->choices);
            c->setSelected (roundToInt (prm->get()));
            c->onPick = [prm] (int i) { prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) i)); };
            segWidth = jmax (92, ChoiceSeg::neededWidth (prm->choices) + 8);
            comp = std::move (c);
        }
        else if (! prm->choices.isEmpty())
        {
            auto c = std::make_unique<ComboBox>();
            for (int i = 0; i < prm->choices.size(); ++i) if (prm->choices[i].isNotEmpty()) c->addItem (prm->choices[i], i + 1);
            c->setSelectedId (roundToInt (prm->get()) + 1, dontSendNotification);
            c->setColour (ComboBox::backgroundColourId, steel);
            c->setColour (ComboBox::textColourId, text);
            c->setColour (ComboBox::outlineColourId, Colours::transparentBlack);
            auto* raw = c.get();
            c->onChange = [prm, raw] { prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) (raw->getSelectedId() - 1))); };
            comp = std::move (c);
        }
        else
        {
            auto k = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
            k->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 18);
            k->setRange (prm->range.start, prm->range.end, 0.0);
            k->setSkewFactor (prm->range.skew);
            k->setValue (prm->get(), dontSendNotification);
            k->setDoubleClickReturnValue (true, prm->range.convertFrom0to1 (prm->defNorm));
            k->textFromValueFunction = [prm] (double v) { return prm->getText (prm->range.convertTo0to1 ((float) v), 0); };
            k->valueFromTextFunction = [] (const String& t) { return t.getDoubleValue(); };
            k->setColour (Slider::rotarySliderFillColourId, fill);
            k->setColour (Slider::rotarySliderOutlineColourId, steel);
            k->setColour (Slider::thumbColourId, text);
            k->setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
            k->setColour (Slider::textBoxTextColourId, text);
            auto* raw = k.get();
            k->onDragStart = [prm] { prm->beginChangeGesture(); };
            k->onDragEnd   = [prm] { prm->endChangeGesture(); };
            k->onValueChange = [prm, raw] { prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) raw->getValue())); };
            k->updateText();
            comp = std::move (k);
        }
        if (prm->tip.isNotEmpty()) if (auto* tc = dynamic_cast<SettableTooltipClient*> (comp.get())) tc->setTooltip (prm->tip);
        addAndMakeVisible (*comp);
        labels.push_back (prm->name);
        comps.push_back (std::move (comp));
        segW.push_back (segWidth);
    }
    if (p.showHeader()) { head = 34; buildHeader(); }
    display = p.makeDisplay();
    top = head;
    if (display != nullptr) { top += p.displayHeight(); addAndMakeVisible (*display); }
    int totW = 16;
    for (size_t i = 0; i < comps.size(); ++i) totW += colWidth (i);
    setSize (jmax (display != nullptr ? p.displayWidth() : (head > 0 ? 420 : 260), totW), (comps.empty() ? 8 : 150) + top);
    startTimerHz (20);
}

void KnobEditor::buildHeader()
{
    const Colour text (0xffc9d2dc), steel (0xff2d4362);
    const auto pr = fx.presets();
    if (! pr.empty() && fx.presetsInHeader())
    {
        presetBox.setTextWhenNothingSelected (sk ("Predvoľba…"));
        for (int i = 0; i < (int) pr.size(); ++i) presetBox.addItem (sk (pr[(size_t) i].name), i + 1);
        presetBox.setColour (ComboBox::backgroundColourId, steel);
        presetBox.setColour (ComboBox::textColourId, text);
        presetBox.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
        presetBox.setTooltip (sk ("Hotové nastavenia – vyber a dolaď gombíkmi"));
        presetBox.onChange = [this]
        {
            const auto all = fx.presets();
            const int i = presetBox.getSelectedId() - 1;
            if (juce::isPositiveAndBelow (i, (int) all.size())) fx.applyPreset (all[(size_t) i]);
        };
        addAndMakeVisible (presetBox);
    }
    for (auto* b : { &abA, &abB, &byp })
    {
        b->setColour (TextButton::buttonColourId, Colour (0xff1c2532));
        b->setColour (TextButton::buttonOnColourId, steel);
        b->setColour (TextButton::textColourOffId, text);
        b->setColour (TextButton::textColourOnId, Colours::white);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    abA.setTooltip (sk ("A/B: dve nastavenia na porovnanie – prepni a počúvaj rozdiel"));
    abB.setTooltip (sk ("A/B: dve nastavenia na porovnanie – prepni a počúvaj rozdiel"));
    abA.onClick = [this] { fx.switchAB (0); updateHeader(); };
    abB.onClick = [this] { fx.switchAB (1); updateHeader(); };
    byp.setTooltip (sk ("Vypnúť efekt – zvuk ide bez úprav (na porovnanie)"));
    byp.onClick = [this] { fx.bypass = ! fx.bypass.load(); updateHeader(); };
    updateHeader();
}

void KnobEditor::updateHeader()
{
    abA.setToggleState (fx.abSlot == 0, dontSendNotification);
    abB.setToggleState (fx.abSlot == 1, dontSendNotification);
    const bool off = fx.bypass.load();
    byp.setButtonText (off ? sk ("Vypnuté") : sk ("Zapnuté"));
    byp.setColour (TextButton::buttonColourId, off ? Colour (0xff8a5a4a) : Colour (0xff1c2532));
}

void KnobEditor::layoutHeader()
{
    if (head <= 0) return;
    auto r = Rectangle<int> (8, 6, getWidth() - 16, 24);
    if (presetBox.isVisible()) presetBox.setBounds (r.removeFromLeft (170)), r.removeFromLeft (8);
    abA.setBounds (r.removeFromLeft (28)); r.removeFromLeft (2);
    abB.setBounds (r.removeFromLeft (28)); r.removeFromLeft (8);
    byp.setBounds (r.removeFromLeft (78)); r.removeFromLeft (10);
    r.removeFromRight (30);   // miesto pre ikonu velkosti okna
    meterArea = r.removeFromRight (jmin (190, r.getWidth()));
}

void KnobEditor::paintHeader (Graphics& g)
{
    auto r = meterArea.toFloat();
    auto row = [&] (const String& name, float db, float y)
    {
        auto line = Rectangle<float> (r.getX(), y, r.getWidth(), 10.0f);
        g.setColour (Colour (0xff8a96a6)); g.setFont (FontOptions (11.0f));
        g.drawText (name, line.removeFromLeft (34.0f).expanded (0, 2), Justification::centredLeft);
        g.setColour (Colour (0xff0e131b)); g.fillRoundedRectangle (line, 2.0f);
        const float f = jlimit (0.0f, 1.0f, (db + 60.0f) / 60.0f);
        g.setColour (db > -0.5f ? Colour (0xffb5654f) : Colour (0xff4d6f96));
        g.fillRoundedRectangle (line.withWidth (line.getWidth() * f), 2.0f);
    };
    row (sk ("Vstup"), fx.meterIn.load(), r.getY() + 1.0f);
    row (sk ("Výstup"), fx.meterOut.load(), r.getY() + 13.0f);
}

//==============================================================================
class EqFx final : public FxBase
{
public:
    EqFx() : FxBase ("Korgus EQ", "eq")
    {
        lowCut  = add ("lowcut", "Low Cut", 20, 1000, 20, "Hz", 0.35f, 0);
        lowG    = add ("lowgain", "Basy", -15, 15, 0, "dB");
        lowF    = add ("lowfreq", "Basy Hz", 40, 500, 120, "Hz", 0.5f, 0);
        midG    = add ("midgain", "Stredy", -15, 15, 0, "dB");
        midF    = add ("midfreq", "Stredy Hz", 200, 8000, 1000, "Hz", 0.35f, 0);
        midQ    = add ("midq", "Šírka Q", 0.3f, 6, 1, "", 0.5f, 2);
        highG   = add ("highgain", "Výšky", -15, 15, 0, "dB");
        highF   = add ("highfreq", "Výšky Hz", 2000, 16000, 8000, "Hz", 0.5f, 0);
        outG    = add ("output", "Výstup", -18, 18, 0, "dB");
        // dalsie pasma (ako v Inspirii) - ovladaju sa v grafe
        static const float defF[3] = { 300, 3000, 6000 };
        for (int k = 0; k < 3; ++k)
        {
            const String n (k + 2);
            pG[k] = add ("p" + n + "gain", "Pásmo", -15, 15, 0, "dB");
            pF[k] = add ("p" + n + "freq", "Pásmo Hz", 40, 16000, defF[k], "Hz", 0.3f, 0);
            pQ[k] = add ("p" + n + "q", "Pásmo šírka", 0.3f, 8, 1, "", 0.4f, 2);
            for (auto* x : { pG[k], pF[k], pQ[k] }) x->inEditor = false;
        }
        highCut = add ("highcut", "High Cut", 1000, 20000, 20000, "Hz", 0.3f, 0);
        highCut->inEditor = false;
        lowCut->tip = sk ("Odreže hlboké basy pod touto frekvenciou (hučanie, dunenie). V grafe: bod 1.");
        lowG->tip   = sk ("Zosilní alebo stíši basy. V grafe ťahaj bod 2 hore / dole.");
        lowF->tip   = sk ("Odkiaľ začínajú basy. V grafe ťahaj bod 2 do strany.");
        midG->tip   = sk ("Zosilní alebo stíši stredy (hlas, gitara). V grafe ťahaj bod 3.");
        midF->tip   = sk ("Ktoré stredy sa menia. V grafe ťahaj bod 3 do strany.");
        midQ->tip   = sk ("Šírka pásma: malé číslo = široké, veľké = úzke. V grafe pravé tlačidlo alebo koliesko na bode.");
        highG->tip  = sk ("Zosilní alebo stíši výšky (jas, vzduch). V grafe ťahaj bod 7.");
        highF->tip  = sk ("Odkiaľ začínajú výšky. V grafe ťahaj bod 7 do strany.");
        outG->tip   = sk ("Celková hlasitosť po ekvalizácii.");
    }

    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 210; }
    std::vector<Preset> presets() const override
    {
        std::vector<Preset> v = {
                 { "Bez úprav", { { "lowcut", 20 }, { "lowgain", 0 }, { "midgain", 0 }, { "highgain", 0 }, { "output", 0 } } },
                 { "Spev – čistý", { { "lowcut", 100 }, { "lowgain", -2 }, { "lowfreq", 200 }, { "midgain", -2 }, { "midfreq", 400 }, { "midq", 1.2f }, { "highgain", 3 }, { "highfreq", 9000 } } },
                 { "Spev – teplý", { { "lowcut", 80 }, { "lowgain", 2 }, { "lowfreq", 180 }, { "midgain", -1.5f }, { "midfreq", 2500 }, { "midq", 1.0f }, { "highgain", 1 }, { "highfreq", 10000 } } },
                 { "Akustická gitara", { { "lowcut", 90 }, { "lowgain", -3 }, { "lowfreq", 180 }, { "midgain", 2 }, { "midfreq", 3000 }, { "midq", 1.0f }, { "highgain", 2 }, { "highfreq", 10000 } } },
                 { "Basa – viac úderu", { { "lowcut", 30 }, { "lowgain", 3 }, { "lowfreq", 80 }, { "midgain", 3 }, { "midfreq", 900 }, { "midq", 1.5f }, { "highgain", 0 } } },
                 { "Bicie – úder", { { "lowcut", 30 }, { "lowgain", 3 }, { "lowfreq", 70 }, { "midgain", -3 }, { "midfreq", 400 }, { "midq", 1.0f }, { "highgain", 3 }, { "highfreq", 8000 } } },
                 { "Telefón", { { "lowcut", 500 }, { "lowgain", 0 }, { "midgain", 8 }, { "midfreq", 1500 }, { "midq", 1.0f }, { "highgain", -15 }, { "highfreq", 3500 } } },
                 { "Menej hučania", { { "lowcut", 120 }, { "lowgain", 0 }, { "midgain", 0 }, { "highgain", 0 } } } };
        for (auto& p : v) for (auto* id : { "p2gain", "p3gain", "p4gain" }) p.v.push_back ({ id, 0.0f });
        for (auto& p : v) p.v.push_back ({ "highcut", 20000.0f });
        return v;
    }

private:
    friend class EqDisplay;
    using Filter = dsp::ProcessorDuplicator<dsp::IIR::Filter<float>, dsp::IIR::Coefficients<float>>;

    void prepare() override
    {
        for (auto& f : flt) { f.prepare (spec); f.reset(); }
        last.fill (-1e9f);
        update (true);
    }
    std::array<float, 18> values() const
    {
        return { lowCut->get(), lowG->get(), lowF->get(), midG->get(), midF->get(), midQ->get(), pG[0]->get(), pF[0]->get(), pQ[0]->get(),
                 pG[1]->get(), pF[1]->get(), pQ[1]->get(), pG[2]->get(), pF[2]->get(), pQ[2]->get(), highG->get(), highF->get(), highCut->get() };
    }
    // koeficienty pasma b (0 LC, 1 basy, 2-5 spicky, 6 vysky, 7 HC)
    static dsp::IIR::Coefficients<float>::Ptr coef (int b, const std::array<float, 18>& v, double rate)
    {
        using C = dsp::IIR::Coefficients<float>;
        const double ny = rate * 0.45;
        auto fq = [ny] (float f) { return jmin ((double) f, ny); };
        if (b == 0) return C::makeHighPass (rate, fq (v[0]), 0.707f);
        if (b == 1) return C::makeLowShelf (rate, fq (v[2]), 0.707f, Decibels::decibelsToGain (v[1]));
        if (b >= 2 && b <= 5) { const int o = 3 + (b - 2) * 3; return C::makePeakFilter (rate, fq (v[(size_t) o + 1]), v[(size_t) o + 2], Decibels::decibelsToGain (v[(size_t) o])); }
        if (b == 6) return C::makeHighShelf (rate, fq (v[16]), 0.707f, Decibels::decibelsToGain (v[15]));
        return C::makeLowPass (rate, fq (v[17]), 0.707f);
    }
    static bool active (int b, const std::array<float, 18>& v)
    {
        if (b == 0) return v[0] > 20.5f;
        if (b == 1) return std::abs (v[1]) > 0.01f;
        if (b >= 2 && b <= 5) return std::abs (v[(size_t) (3 + (b - 2) * 3)]) > 0.01f;
        if (b == 6) return std::abs (v[15]) > 0.01f;
        return v[17] < 19900.0f;
    }
    void update (bool force)
    {
        const auto now = values();
        if (! force && now == last) return;
        for (int b = 0; b < 8; ++b) *flt[(size_t) b].state = *coef (b, now, rate);
        last = now;
    }

    void process (AudioBuffer<float>& b) override
    {
        update (false);
        dsp::AudioBlock<float> blk (b);
        dsp::ProcessContextReplacing<float> ctx (blk);
        for (int k = 0; k < 8; ++k) if (active (k, last)) flt[(size_t) k].process (ctx);
        const float g = Decibels::decibelsToGain (outG->get());
        b.applyGainRamp (0, b.getNumSamples(), lastOut, g);
        lastOut = g;
    }

    FxParam *lowCut, *lowG, *lowF, *midG, *midF, *midQ, *highG, *highF, *outG, *highCut;
    FxParam *pG[3], *pF[3], *pQ[3];
    std::array<Filter, 8> flt;
    std::array<float, 18> last {};
    float lastOut = 1.0f;
};

// EQ: spolocny graf (rovnaky ako v Inspirii) - body pasiem, uchytky sirky, spektrum
class EqDisplay final : public Component, public korgus::EqModel, private Timer
{
public:
    explicit EqDisplay (EqFx& f) : fx (f), view (*this)
    {
        ++fx.scope.users; startTimerHz (25);
        view.tr = [] (const String& t) { return sk (t.toRawUTF8()); };
        view.setTooltip (sk ("Ťahaj body: hore / dole = hlasnejšie / tichšie, do strany = výška (Shift = jemne). Klik na bod ukáže uchytky – ťahaním do strán meníš šírku pásma. Šírku mení aj koliesko alebo pravé tlačidlo + ťahanie. Dvojklik = vynulovať."));
        addAndMakeVisible (view);
    }
    ~EqDisplay() override { stopTimer(); --fx.scope.users; }
    void resized() override { view.setBounds (getLocalBounds()); }

    int numBands() const override { return 8; }
    korgus::EqBand band (int i) const override
    {
        auto [f, gn, q] = trio (i); korgus::EqBand b;
        b.type = i == 0 ? 3 : i == 1 ? 0 : i == 6 ? 2 : i == 7 ? 4 : 1;
        b.f = f->get(); b.g = gn != nullptr ? gn->get() : 0.0f; b.q = q != nullptr ? q->get() : 0.707f;
        b.hasF = true; b.hasG = gn != nullptr; b.hasQ = q != nullptr;
        b.fMin = f->range.start; b.fMax = f->range.end; b.fDef = f->range.convertFrom0to1 (f->getDefaultValue());
        if (q != nullptr) { b.qMin = q->range.start; b.qMax = q->range.end; b.qDef = q->range.convertFrom0to1 (q->getDefaultValue()); }
        b.active = EqFx::active (i, fx.values());
        return b;
    }
    void setF (int i, float v) override { std::get<0> (trio (i))->setFromUi (v); }
    void setG (int i, float v) override { if (auto* p = std::get<1> (trio (i))) p->setFromUi (v); }
    void setQ (int i, float v) override { if (auto* p = std::get<2> (trio (i))) p->setFromUi (v); }
    void begin (int i) override { auto [a, b, c] = trio (i); for (auto* p : { a, b, c }) if (p != nullptr) p->beginChangeGesture(); }
    void end (int i) override { auto [a, b, c] = trio (i); for (auto* p : { a, b, c }) if (p != nullptr) p->endChangeGesture(); }
    double bandMagnitudeAt (int i, float hz, float gainDb) const override
    {
        auto v = fx.values();
        if (i == 1) v[1] = gainDb; else if (i >= 2 && i <= 5) v[(size_t) (3 + (i - 2) * 3)] = gainDb; else if (i == 6) v[15] = gainDb;
        const double sr = jmax (8000.0, fx.sampleRate());
        return EqFx::coef (i, v, sr)->getMagnitudeForFrequency (hz, sr);
    }
    double bandMagnitude (int i, float hz) const override
    {
        const auto v = fx.values(); const double sr = jmax (8000.0, fx.sampleRate());
        return EqFx::coef (i, v, sr)->getMagnitudeForFrequency (hz, sr);
    }
    double magnitude (float hz) const override
    {
        const auto v = fx.values(); const double sr = jmax (8000.0, fx.sampleRate());
        double m = 1.0;
        for (int b = 0; b < 8; ++b) if (EqFx::active (b, v)) m *= EqFx::coef (b, v, sr)->getMagnitudeForFrequency (hz, sr);
        return m * Decibels::decibelsToGain (fx.outG->get());
    }
    float spectrumDb (float hz) const override { return spec.at (hz, fx.sampleRate()); }

private:
    std::tuple<FxParam*, FxParam*, FxParam*> trio (int i) const
    {
        if (i == 0) return { fx.lowCut, nullptr, nullptr };
        if (i == 1) return { fx.lowF, fx.lowG, nullptr };
        if (i == 2) return { fx.midF, fx.midG, fx.midQ };
        if (i >= 3 && i <= 5) return { fx.pF[i - 3], fx.pG[i - 3], fx.pQ[i - 3] };
        if (i == 6) return { fx.highF, fx.highG, nullptr };
        return { fx.highCut, nullptr, nullptr };
    }
    void timerCallback() override { spec.update (fx.scope); view.repaint(); }
    EqFx& fx;
    Spectrum spec;
    korgus::EqView view;
};
std::unique_ptr<Component> EqFx::makeDisplay() { return std::make_unique<EqDisplay> (*this); }

//==============================================================================
class CompFx final : public FxBase
{
public:
    CompFx() : FxBase ("Korgus Kompresor", "comp")
    {
        thr  = add ("threshold", "Prah", -60, 0, -18, "dB");
        rat  = add ("ratio", "Pomer", 1, 20, 4, ":1", 0.5f, 1);
        att  = add ("attack", "Nábeh", 0.1f, 100, 10, "ms", 0.4f, 1);
        rel  = add ("release", "Dozvuk", 10, 1000, 120, "ms", 0.4f, 0);
        mak  = add ("makeup", "Zisk", 0, 24, 0, "dB");
        mix  = add ("mix", "Mix", 0, 100, 100, "%", 1.0f, 0);
        key  = add ("key", "Riadenie", 0, 1, 0, "", 1.0f, 0);
        key->range.interval = 1.0f;
        key->choices = StringArray { sk ("Vlastný"), String ("Sidechain") };
        thr->tip = sk ("Od akej hlasitosti začne kompresor stláčať. V grafe ťahaj zlatý bod do strany.");
        rat->tip = sk ("Ako silno stláča nad prahom (4:1 = o 4 dB viac na vstupe dá len 1 dB na výstupe). V grafe ťahaj čiaru vpravo hore / dole.");
        att->tip = sk ("Ako rýchlo začne stláčať. Pomalší nábeh nechá prejsť úder (bicie znejú údernejšie).");
        rel->tip = sk ("Ako rýchlo sa po stlačení vráti späť.");
        mak->tip = sk ("Zosilní výsledok, aby bol rovnako hlasný ako predtým.");
        mix->tip = sk ("Pomer stlačeného a pôvodného zvuku (paralelná kompresia).");
        key->tip = sk ("Sidechain: stláča podľa inej stopy (napr. basa uhne kopáku).");
    }

    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 160; }
    std::vector<Preset> presets() const override
    {
        return { { "Spev – jemne", { { "threshold", -20 }, { "ratio", 2.5f }, { "attack", 10 }, { "release", 150 }, { "makeup", 3 }, { "mix", 100 } } },
                 { "Spev – vyrovnaný", { { "threshold", -24 }, { "ratio", 4 }, { "attack", 5 }, { "release", 120 }, { "makeup", 6 }, { "mix", 100 } } },
                 { "Bicie – úder", { { "threshold", -18 }, { "ratio", 4 }, { "attack", 25 }, { "release", 100 }, { "makeup", 3 }, { "mix", 100 } } },
                 { "Bicie – paralelne", { { "threshold", -30 }, { "ratio", 8 }, { "attack", 2 }, { "release", 80 }, { "makeup", 8 }, { "mix", 40 } } },
                 { "Basa – vyrovnaná", { { "threshold", -20 }, { "ratio", 5 }, { "attack", 15 }, { "release", 150 }, { "makeup", 4 }, { "mix", 100 } } },
                 { "Mix – lepidlo", { { "threshold", -14 }, { "ratio", 2 }, { "attack", 30 }, { "release", 200 }, { "makeup", 1.5f }, { "mix", 100 } } },
                 { "Pumpa (sidechain)", { { "threshold", -30 }, { "ratio", 10 }, { "attack", 1 }, { "release", 180 }, { "makeup", 0 }, { "mix", 100 }, { "key", 1 } } } };
    }
    std::atomic<float> grNow { 0 }, inNow { -60 }, outNow { -60 };   // pre ukazovatel (dB)

private:
    void prepare() override { env = 0; dry.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize); }

    void process (AudioBuffer<float>& b) override
    {
        const float m = mix->get() * 0.01f;
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        if (m < 0.999f)
        {
            if (dry.getNumSamples() < n || dry.getNumChannels() < ch) dry.setSize (ch, n, false, false, true);
            for (int c = 0; c < ch; ++c) dry.copyFrom (c, 0, b, c, 0, n);
        }
        const bool useKey = key->get() > 0.5f && scL != nullptr && scN >= n;
        const float* dL = useKey ? scL : b.getReadPointer (0);
        const float* dR = useKey ? scR : b.getReadPointer (ch > 1 ? 1 : 0);
        const float aA = (float) std::exp (-1.0 / (att->get() * 0.001 * rate));
        const float aR = (float) std::exp (-1.0 / (rel->get() * 0.001 * rate));
        const float t = thr->get(), slope = 1.0f - 1.0f / rat->get(), knee = 6.0f;
        const float mg0 = lastG, mg1 = Decibels::decibelsToGain (mak->get());
        float grMax = 0, inPk = 0, outPk = 0;
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (dL[i]), std::abs (dR[i]));
            inPk = jmax (inPk, jmax (std::abs (b.getSample (0, i)), std::abs (b.getSample (ch > 1 ? 1 : 0, i))));
            env = x > env ? x + (env - x) * aA : x + (env - x) * aR;
            const float lev = Decibels::gainToDecibels (env, -120.0f), over = lev - t;
            float gr = 0;
            if (over > knee * 0.5f) gr = over * slope;
            else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; gr = slope * k * k / (2.0f * knee); }
            const float g = Decibels::decibelsToGain (-gr) * (mg0 + (mg1 - mg0) * (float) i / (float) n);
            grMax = jmax (grMax, gr);
            for (int c = 0; c < ch; ++c) { b.getWritePointer (c)[i] *= g; outPk = jmax (outPk, std::abs (b.getSample (c, i))); }
        }
        lastG = mg1;
        // ukazovatel: rychly nabeh, pomaly navrat
        const float dec = (float) std::exp (-(double) n / (0.35 * rate));
        grNow = jmax (grMax, grNow.load() * dec);
        inNow = jmax (Decibels::gainToDecibels (inPk, -60.0f), -60.0f + (inNow.load() + 60.0f) * dec);
        outNow = jmax (Decibels::gainToDecibels (outPk, -60.0f), -60.0f + (outNow.load() + 60.0f) * dec);
        if (m < 0.999f)
            for (int c = 0; c < ch; ++c)
            {
                b.applyGain (c, 0, n, m);
                b.addFrom (c, 0, dry, c, 0, n, 1.0f - m);
            }
    }

    friend class CompMeter;
    FxParam *thr, *rat, *att, *rel, *mak, *mix, *key;
    AudioBuffer<float> dry;
    float env = 0, lastG = 1.0f;
};

// kompresor: prenosova krivka (vstup -> vystup) s bodkou podla hlasitosti + ukazovatele
class CompMeter final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit CompMeter (CompFx& f) : fx (f) { startTimerHz (30); setTooltip (sk ("Krivka: ťahaj zlatý bod (prah) do strany, čiaru vpravo hore / dole (pomer). Bodka ukazuje aktuálnu hlasitosť.")); }
    ~CompMeter() override { stopTimer(); }
    void paint (Graphics& g) override
    {
        g.setColour (GCol::bg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        const auto a = sq();
        g.setColour (GCol::panel); g.fillRect (a);
        g.setFont (FontOptions (10.0f));
        for (float d : { -48.0f, -36.0f, -24.0f, -12.0f })
        {
            g.setColour (GCol::grid); g.drawVerticalLine ((int) xOf (d), a.getY(), a.getBottom()); g.drawHorizontalLine ((int) yOf (d), a.getX(), a.getRight());
        }
        g.setColour (GCol::steel); g.drawLine (a.getX(), a.getBottom(), a.getRight(), a.getY(), 1.0f);
        Path p;
        for (float d = -60.0f; d <= 0.01f; d += 0.5f) { const float x = xOf (d), y = yOf (outFor (d)); if (d == -60.0f) p.startNewSubPath (x, y); else p.lineTo (x, y); }
        g.setColour (GCol::gold); g.strokePath (p, PathStrokeType (2.0f));
        const float t = fx.thr->get();
        g.setColour (drag == 1 ? Colours::white : GCol::gold); g.fillRoundedRectangle (xOf (t) - 6, yOf (outFor (t)) - 6, 12, 12, 2.0f);
        const float in = jlimit (-60.0f, 0.0f, fx.inNow.load());
        g.setColour (GCol::line); g.fillRoundedRectangle (xOf (in) - 4, yOf (outFor (in)) - 4, 8, 8, 2.0f);
        g.setColour (GCol::dim); g.drawText (sk ("vstup"), a.withTop (a.getBottom() - 12).toNearestInt(), Justification::centredRight);
        g.drawText (sk ("prah") + " " + String (t, 1) + " dB  " + String (fx.rat->get(), 1) + ":1", a.withHeight (14).reduced (4, 0).toNearestInt(), Justification::centredLeft);
        // ukazovatele vpravo
        auto r = getLocalBounds().toFloat().withTrimmedLeft (a.getRight() + 14).reduced (8, 14);
        auto row = [&] (const String& name, float frac, Colour c, const String& val, bool fromRight)
        {
            auto line = r.removeFromTop (18.0f); r.removeFromTop (14.0f);
            g.setColour (GCol::text); g.setFont (FontOptions (12.0f));
            g.drawText (name, line.removeFromLeft (70.0f), Justification::centredLeft);
            g.drawText (val, line.removeFromRight (60.0f), Justification::centredRight);
            line.removeFromRight (6.0f);
            g.setColour (GCol::bg.brighter (0.05f)); g.fillRoundedRectangle (line, 3.0f);
            const float w = line.getWidth() * jlimit (0.0f, 1.0f, frac);
            g.setColour (c);
            g.fillRoundedRectangle (fromRight ? line.withLeft (line.getRight() - w) : line.withWidth (w), 3.0f);
        };
        const float inD = fx.inNow.load(), out = fx.outNow.load(), gr = fx.grNow.load();
        row (sk ("Vstup"), (inD + 60.0f) / 60.0f, Colour (0xff4d6f96), String (inD, 1) + " dB", false);
        row (sk ("Stlačenie"), gr / 24.0f, GCol::gold, (gr > 0.05f ? "-" : "") + String (gr, 1) + " dB", true);
        row (sk ("Výstup"), (out + 60.0f) / 60.0f, Colour (0xff6f8fb3), String (out, 1) + " dB", false);
    }
    void mouseDown (const MouseEvent& e) override
    {
        const float t = fx.thr->get();
        drag = e.position.getDistanceFrom ({ xOf (t), yOf (outFor (t)) }) < 12.0f ? 1 : (e.position.x > xOf (t) && sq().contains (e.position) ? 2 : 0);
        if (drag == 1) fx.thr->beginChangeGesture(); else if (drag == 2) fx.rat->beginChangeGesture();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        const auto a = sq();
        const float din = jmap (e.position.x, a.getX(), a.getRight(), -60.0f, 0.0f);
        if (drag == 1) fx.thr->setFromUi (std::round (din * 2.0f) / 2.0f);
        else if (drag == 2)
        {
            const float t = fx.thr->get(), x = jmax (t + 3.0f, din), outY = jmap (e.position.y, a.getBottom(), a.getY(), -60.0f, 0.0f);
            const float rise = jmax (0.05f, outY - (t - 0.0f));
            fx.rat->setFromUi (jlimit (1.0f, 20.0f, (x - t) / rise));
        }
        repaint();
    }
    void mouseUp (const MouseEvent&) override { if (drag == 1) fx.thr->endChangeGesture(); else if (drag == 2) fx.rat->endChangeGesture(); drag = 0; }
private:
    Rectangle<float> sq() const { const float h = (float) getHeight() - 16.0f; return { 8.0f, 8.0f, h, h }; }
    float xOf (float d) const { const auto a = sq(); return jmap (d, -60.0f, 0.0f, a.getX(), a.getRight()); }
    float yOf (float d) const { const auto a = sq(); return jmap (jlimit (-60.0f, 0.0f, d), -60.0f, 0.0f, a.getBottom(), a.getY()); }
    float outFor (float in) const   // bez zisku (makeup), rovnake makke koleno ako zvuk
    {
        const float t = fx.thr->get(), slope = 1.0f - 1.0f / fx.rat->get(), knee = 6.0f, over = in - t;
        float gr = 0;
        if (over > knee * 0.5f) gr = over * slope;
        else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; gr = slope * k * k / (2.0f * knee); }
        return in - gr;
    }
    void timerCallback() override { repaint(); }
    CompFx& fx;
    int drag = 0;
};
std::unique_ptr<Component> CompFx::makeDisplay() { return std::make_unique<CompMeter> (*this); }

//==============================================================================
class ReverbFx final : public FxBase
{
public:
    ReverbFx() : FxBase ("Korgus Dozvuk", "reverb")
    {
        size  = add ("size", "Veľkosť", 0, 100, 50, "%", 1.0f, 0);
        decay = add ("decay", "Dĺžka", 0, 100, 45, "%", 1.0f, 0);
        damp  = add ("damping", "Tlmenie", 0, 100, 50, "%", 1.0f, 0);
        pre   = add ("predelay", "Predozvuk", 0, 250, 20, "ms", 1.0f, 0);
        width = add ("width", "Šírka", 0, 100, 100, "%", 1.0f, 0);
        mix   = add ("mix", "Mix", 0, 100, 25, "%", 1.0f, 0);
        tail = 12.0;
        size->tip  = sk ("Veľkosť priestoru – od malej izby po veľkú halu (rozostup prvých odrazov).");
        decay->tip = sk ("Ako dlho dozvuk doznieva. V grafe ťahaj koniec chvosta.");
        damp->tip  = sk ("Tlmenie výšok: viac = tmavší, mäkší dozvuk (ako v miestnosti so závesmi).");
        pre->tip   = sk ("Medzera pred dozvukom – spev ostane zreteľný a dozvuk príde až za ním. V grafe ťahaj začiatok chvosta.");
        width->tip = sk ("Šírka dozvuku v stereu.");
        mix->tip   = sk ("Koľko dozvuku pridať k pôvodnému zvuku.");
    }
    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 168; }
    bool presetsInHeader() const override { return false; }
    std::vector<Preset> presets() const override
    {
        auto P = [] (const char* n, float sz, float dc, float dm, float pd, float wd, float mx) { return Preset { n, { { "size", sz }, { "decay", dc }, { "damping", dm }, { "predelay", pd }, { "width", wd }, { "mix", mx } } }; };
        return { P ("Izba", 25, 25, 55, 5, 80, 18), P ("Štúdio", 40, 35, 45, 15, 100, 20), P ("Sála", 70, 60, 40, 30, 100, 25),
                 P ("Kostol", 95, 85, 30, 45, 100, 30), P ("Plech", 45, 65, 15, 0, 100, 22), P ("Spev", 55, 45, 50, 60, 90, 18) };
    }
    std::atomic<float> tailNow { 2.0f };

private:
    friend class ReverbDisplay;
    void prepare() override { rv.prepare (rate, 8192); rv.reset(); }

    void process (AudioBuffer<float>& b) override
    {
        rv.setParams (size->get() * 0.01f, decay->get() * 0.01f, damp->get() * 0.01f, pre->get() / 250.0f, width->get() * 0.01f);
        tail = rv.getTailSeconds();
        tailNow = (float) tail;
        rv.process (b.getWritePointer (0), b.getNumChannels() > 1 ? b.getWritePointer (1) : nullptr, b.getNumSamples(), mix->get() * 0.01f);
    }

    FxParam *size, *decay, *damp, *pre, *width, *mix;
    korgus::SuperVerb rv;
};

// dozvuk: graf doznievania (predozvuk, prve odrazy, chvost), tlacidla miestnosti
class ReverbDisplay final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit ReverbDisplay (ReverbFx& f) : fx (f)
    {
        setTooltip (sk ("Ťahaj začiatok chvosta (predozvuk) alebo jeho koniec (dĺžka). Tlačidlá dole = hotové miestnosti."));
        const auto pr = fx.presets();
        for (int i = 0; i < (int) pr.size(); ++i)
        {
            auto* b = btns.add (new TextButton (sk (pr[(size_t) i].name)));
            b->setColour (TextButton::buttonColourId, Colour (0xff1c2532));
            b->setColour (TextButton::textColourOffId, GCol::text);
            b->onClick = [this, i] { const auto all = fx.presets(); fx.applyPreset (all[(size_t) i]); repaint(); };
            addAndMakeVisible (b);
        }
        startTimerHz (20);
    }
    ~ReverbDisplay() override { stopTimer(); }
    void resized() override
    {
        auto r = getLocalBounds().removeFromBottom (26).reduced (6, 2);
        const int w = r.getWidth() / jmax (1, btns.size());
        for (auto* b : btns) b->setBounds (r.removeFromLeft (w).reduced (2, 0));
    }
    void paint (Graphics& g) override
    {
        g.setColour (GCol::bg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        const auto a = area();
        const float xp = xPre(), xe = xEnd(), base = a.getBottom(), topY = a.getY() + 12;
        // povodny zvuk
        g.setColour (GCol::dim); g.fillRect (a.getX(), topY, 3.0f, base - topY);
        // prve odrazy
        const float spread = jmap (fx.size->get(), 0.0f, 100.0f, 8.0f, 40.0f);
        for (int k = 0; k < 6; ++k)
        {
            const float x = xp + spread * (0.3f + (float) k * 0.55f) * (1.0f + 0.15f * (float) (k % 2));
            const float h = (base - topY) * (0.75f - (float) k * 0.09f);
            g.setColour (GCol::line.withAlpha (0.8f)); g.fillRect (x, base - h, 2.0f, h);
        }
        // chvost: spodna vrstva (basy) a horna (vysky) – tlmenie skracuje vysky
        auto tailPath = [&] (float len, float hFrac)
        {
            Path p; p.startNewSubPath (xp, base);
            const float h = (base - topY) * hFrac;
            for (float x = xp; x <= xp + len; x += 2.0f) { const float t = (x - xp) / jmax (1.0f, len); p.lineTo (x, base - h * std::pow (1.0f - t, 2.2f) * (0.6f + 0.4f * std::exp (-t * 8.0f))); }
            p.lineTo (xp + len, base); p.closeSubPath(); return p;
        };
        const float len = xe - xp;
        g.setColour (GCol::steel.withAlpha (0.9f)); g.fillPath (tailPath (len, 0.62f));
        const float hiLen = len * jmap (fx.damp->get(), 0.0f, 100.0f, 1.0f, 0.3f);
        g.setGradientFill (ColourGradient (GCol::line.withAlpha (0.55f), xp, topY, GCol::line.withAlpha (0.05f), xp + hiLen, base, false));
        g.fillPath (tailPath (hiLen, 0.45f));
        // uchopy
        for (int i = 0; i < 2; ++i)
        {
            const float x = i == 0 ? xp : xe;
            g.setColour (drag == i + 1 || hover == i + 1 ? GCol::gold : GCol::gold.withAlpha (0.7f));
            g.drawVerticalLine ((int) x, topY - 8, base);
            g.fillRoundedRectangle (x - 5, topY - 12, 10, 10, 2.0f);
        }
        g.setColour (GCol::text); g.setFont (FontOptions (11.0f));
        g.drawText (sk ("predozvuk") + " " + String (roundToInt (fx.pre->get())) + " ms", Rectangle<float> (xp + 8, a.getY(), 130, 12), Justification::centredLeft);
        g.drawText (sk ("dĺžka") + String::fromUTF8 (" ≈ ") + String (korgus::SuperVerb::decayToSeconds (fx.decay->get() * 0.01f), 1) + " s",
                    Rectangle<float> (jmin (xe + 8, a.getRight() - 90), a.getY(), 90, 12), Justification::centredLeft);
    }
    void mouseMove (const MouseEvent& e) override { const int h = hit (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = 0; repaint(); }
    void mouseDown (const MouseEvent& e) override { drag = hit (e.position); if (drag == 1) fx.pre->beginChangeGesture(); if (drag == 2) fx.decay->beginChangeGesture(); }
    void mouseDrag (const MouseEvent& e) override
    {
        const auto a = area();
        if (drag == 1) fx.pre->setFromUi (jmap (e.position.x, a.getX() + 6, a.getX() + 6 + preW(), 0.0f, 250.0f));
        else if (drag == 2) fx.decay->setFromUi (jmap (e.position.x - xPre(), minLen(), maxLen(), 0.0f, 100.0f));
        repaint();
    }
    void mouseUp (const MouseEvent&) override { if (drag == 1) fx.pre->endChangeGesture(); if (drag == 2) fx.decay->endChangeGesture(); drag = 0; }
private:
    Rectangle<float> area() const { return getLocalBounds().toFloat().reduced (10, 8).withTrimmedBottom (26); }
    float preW() const { return area().getWidth() * 0.18f; }
    float minLen() const { return 30.0f; }
    float maxLen() const { return area().getRight() - (area().getX() + 6 + preW()) - 10; }
    float xPre() const { return area().getX() + 6 + preW() * fx.pre->get() / 250.0f; }
    float xEnd() const { return xPre() + jmap (fx.decay->get(), 0.0f, 100.0f, minLen(), maxLen()); }
    int hit (Point<float> p) const { if (std::abs (p.x - xPre()) < 8) return 1; if (std::abs (p.x - xEnd()) < 8) return 2; return 0; }
    void timerCallback() override { repaint(); }
    ReverbFx& fx;
    OwnedArray<TextButton> btns;
    int drag = 0, hover = 0;
};
std::unique_ptr<Component> ReverbFx::makeDisplay() { return std::make_unique<ReverbDisplay> (*this); }

//==============================================================================
class DelayFx final : public FxBase
{
public:
    DelayFx() : FxBase ("Korgus Ozvena", "delay")
    {
        sync = add ("sync", "Synchro", 0, 5, 2, "", 1.0f, 0);
        sync->range.interval = 1.0f;
        sync->choices = StringArray::fromTokens (sk ("Voľne|1/4|1/8 bodka|1/8|1/8 triola|1/16"), "|", "");
        time = add ("time", "Čas", 10, 2000, 375, "ms", 0.5f, 0);
        time->textFn = [this] (float) { const int sy = roundToInt (sync->get()); return sy > 0 ? sync->choices[sy] : String(); };
        fb   = add ("feedback", "Opakovanie", 0, 95, 35, "%", 1.0f, 0);
        tone = add ("tone", "Farba", 1000, 20000, 8000, "Hz", 0.4f, 0);
        mix  = add ("mix", "Mix", 0, 100, 25, "%", 1.0f, 0);
        pp   = add ("pingpong", "Ping-pong", 0, 1, 0, "", 1.0f, 0);
        pp->toggle = true;
        tail = 4.0;
        sync->inEditor = false;   // tlacidla v grafe
        time->tip = sk ("Čas medzi opakovaniami (pri Synchro podľa tempa piesne). V grafe ťahaj prvú čiarku do strany.");
        fb->tip   = sk ("Koľkokrát sa ozvena opakuje. V grafe ťahaj prvú čiarku hore / dole.");
        tone->tip = sk ("Farba ozveny: menej = tmavšia, každé ďalšie opakovanie tmavne.");
        mix->tip  = sk ("Koľko ozveny pridať k pôvodnému zvuku.");
        pp->tip   = sk ("Ping-pong: ozvena skáče striedavo vľavo a vpravo.");
    }
    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 150; }
    std::vector<Preset> presets() const override
    {
        return { { "Spev – jemná ozvena", { { "sync", 3 }, { "feedback", 25 }, { "tone", 6000 }, { "mix", 15 }, { "pingpong", 0 } } },
                 { "Slapback (rokenrol)", { { "sync", 0 }, { "time", 110 }, { "feedback", 5 }, { "tone", 9000 }, { "mix", 25 }, { "pingpong", 0 } } },
                 { "Ping-pong", { { "sync", 2 }, { "feedback", 40 }, { "tone", 7000 }, { "mix", 25 }, { "pingpong", 1 } } },
                 { "Dlhá do priestoru", { { "sync", 1 }, { "feedback", 60 }, { "tone", 4000 }, { "mix", 22 }, { "pingpong", 1 } } },
                 { "Dub (tmavá)", { { "sync", 2 }, { "feedback", 70 }, { "tone", 2000 }, { "mix", 30 }, { "pingpong", 0 } } } };
    }
    std::atomic<float> msNow { 375.0f };

private:
    friend class DelayDisplay;
    void prepare() override
    {
        size = (int) (rate * 2.1) + 4;
        line.setSize (2, size); line.clear();
        w = 0; lp[0] = lp[1] = 0;
        curD = (float) (time->get() * 0.001 * rate);
        jumpD = true;
    }

    void process (AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        double ms = time->get();
        if (const int sy = roundToInt (sync->get()); sy > 0)
        {
            static const double beats[6] = { 0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
            double bpm = 120;
            if (auto* ph = getPlayHead())
                if (auto pos = ph->getPosition())
                    if (auto b = pos->getBpm()) bpm = jlimit (20.0, 400.0, *b);
            ms = jmin (2000.0, 60000.0 / bpm * beats[sy]);
        }
        msNow = (float) ms;
        const float target = (float) jlimit (1.0, (double) size - 3, ms * 0.001 * rate);
        const float f = fb->get() * 0.01f, m = mix->get() * 0.01f;
        const float a = (float) std::exp (-MathConstants<double>::twoPi * tone->get() / rate);
        const bool ping = pp->get() > 0.5f && ch > 1;
        auto* dl = line.getWritePointer (0);
        auto* dr = line.getWritePointer (1);
        auto* bl = b.getWritePointer (0);
        auto* br = ch > 1 ? b.getWritePointer (1) : nullptr;
        if (jumpD) { curD = target; jumpD = false; }
        for (int i = 0; i < n; ++i)
        {
            curD += (target - curD) * 0.0005f;
            float rp = (float) w - curD;
            if (rp < 0) rp += (float) size;
            int i0 = (int) rp;
            const float fr = rp - (float) i0;
            if (i0 >= size) i0 -= size; if (i0 < 0) i0 = 0;   // float zaokruhlenie moze dat presne size
            const int i1 = (i0 + 1) % size;
            float yl = dl[i0] + (dl[i1] - dl[i0]) * fr;
            float yr = dr[i0] + (dr[i1] - dr[i0]) * fr;
            lp[0] = yl + (lp[0] - yl) * a; lp[1] = yr + (lp[1] - yr) * a;
            yl = lp[0]; yr = lp[1];
            const float xl = bl[i], xr = br != nullptr ? br[i] : xl;
            if (ping)
            {
                dl[w] = 0.5f * (xl + xr) + yr * f;   // vstup do ľavej, spätná väzba prekrížene
                dr[w] = yl * f;
            }
            else
            {
                dl[w] = xl + yl * f;
                dr[w] = xr + yr * f;
            }
            bl[i] = xl * (1.0f - m) + yl * m;
            if (br != nullptr) br[i] = xr * (1.0f - m) + yr * m;
            if (++w >= size) w = 0;
        }
    }

    FxParam *sync, *time, *fb, *tone, *pp, *mix;
    bool jumpD = true;
    AudioBuffer<float> line;
    int size = 1, w = 0;
    float lp[2] {}, curD = 0;
};

// ozvena: opakovania ako ciarky v case, tlacidla synchronizacie
class DelayDisplay final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit DelayDisplay (DelayFx& f) : fx (f)
    {
        setTooltip (sk ("Ťahaj prvú zlatú čiarku: do strany = čas, hore / dole = počet opakovaní."));
        for (int i = 0; i < fx.sync->choices.size(); ++i)
        {
            auto* b = btns.add (new TextButton (fx.sync->choices[i]));
            b->setColour (TextButton::buttonColourId, Colour (0xff1c2532));
            b->setColour (TextButton::buttonOnColourId, GCol::steel);
            b->setColour (TextButton::textColourOffId, GCol::text);
            b->setColour (TextButton::textColourOnId, Colours::white);
            b->setTooltip (i == 0 ? sk ("Čas nastavíš voľne v milisekundách") : sk ("Čas podľa tempa piesne"));
            b->onClick = [this, i] { fx.sync->setFromUi ((float) i); repaint(); };
            addAndMakeVisible (b);
        }
        startTimerHz (20);
    }
    ~DelayDisplay() override { stopTimer(); }
    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (26).reduced (6, 2);
        const int w = r.getWidth() / jmax (1, btns.size());
        for (auto* b : btns) b->setBounds (r.removeFromLeft (w).reduced (2, 0));
    }
    void paint (Graphics& g) override
    {
        const int sy = roundToInt (fx.sync->get());
        for (int i = 0; i < btns.size(); ++i) btns[i]->setToggleState (i == sy, dontSendNotification);
        g.setColour (GCol::bg); g.fillRoundedRectangle (getLocalBounds().toFloat().withTrimmedTop (28), 3.0f);
        const auto a = area();
        const bool ping = fx.pp->get() > 0.5f;
        const float mid = ping ? a.getCentreY() : a.getBottom(), hMax = ping ? a.getHeight() * 0.5f - 4 : a.getHeight() - 6;
        g.setColour (GCol::grid); g.drawHorizontalLine ((int) mid, a.getX(), a.getRight());
        g.setFont (FontOptions (10.0f));
        for (float sec = 0.5f; sec < axis; sec += 0.5f) { const float x = xOf (sec * 1000.0f); g.setColour (GCol::grid); g.drawVerticalLine ((int) x, a.getY(), a.getBottom()); g.setColour (GCol::dim); g.drawText (String (sec, 1) + " s", (int) x + 2, (int) a.getY(), 40, 11, Justification::centredLeft); }
        g.setColour (GCol::dim); g.fillRect (a.getX(), mid - hMax, 3.0f, hMax * (ping ? 2.0f : 1.0f));
        const float ms = fx.msNow.load(), f = fx.fb->get() * 0.01f;
        const float bright = jmap (std::log (fx.tone->get() / 1000.0f) / std::log (20.0f), 0.0f, 1.0f, 0.35f, 1.0f);
        float lev = 1.0f;
        for (int k = 1; k < 64; ++k)
        {
            const float x = xOf (ms * (float) k);
            if (x > a.getRight() || lev < 0.02f) break;
            const float h = hMax * lev;
            const Colour c = (k == 1 ? GCol::gold : GCol::line).withMultipliedBrightness (k == 1 ? 1.0f : std::pow (bright, (float) (k - 1) * 0.5f));
            g.setColour (c);
            if (ping && (k % 2 == 0)) g.fillRect (x - 1.5f, mid, 3.0f, h);
            else g.fillRect (x - 1.5f, mid - h, 3.0f, h);
            lev *= f;
        }
        g.setColour (GCol::text); g.setFont (FontOptions (11.0f));
        g.drawText (String (roundToInt (ms)) + String::fromUTF8 (" ms · ") + String (roundToInt (fx.fb->get())) + " %", a.toNearestInt().removeFromBottom (14).reduced (4, 0), Justification::centredRight);
        if (ping) { g.setColour (GCol::dim); g.drawText ("L", a.toNearestInt().removeFromTop (14).reduced (4, 0), Justification::centredRight); g.drawText ("R", a.toNearestInt().removeFromBottom (28).removeFromTop (14).reduced (4, 0), Justification::centredRight); }
    }
    void mouseDown (const MouseEvent& e) override
    {
        drag = std::abs (e.position.x - xOf (fx.msNow.load())) < 10 && area().contains (e.position);
        if (! drag) return;
        y0 = e.position.y; fb0 = fx.fb->get(); x0 = e.position.x; ms0 = fx.msNow.load(); axisLock = 0;
        fx.time->beginChangeGesture(); fx.fb->beginChangeGesture();
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (! drag) return;
        if (axisLock == 0) { const float dx = std::abs (e.position.x - x0), dy = std::abs (e.position.y - y0); if (dx < 4 && dy < 4) return; axisLock = dx > dy ? 1 : 2; }
        if (axisLock == 1)
        {
            const float ms = jlimit (10.0f, 2000.0f, msOf (e.position.x));
            if (roundToInt (fx.sync->get()) != 0) fx.sync->setFromUi (0.0f);
            fx.time->setFromUi (ms);
            fx.msNow = ms;
        }
        else fx.fb->setFromUi (jlimit (0.0f, 95.0f, fb0 + (y0 - e.position.y) * 0.6f));
        repaint();
    }
    void mouseUp (const MouseEvent&) override { if (drag) { fx.time->endChangeGesture(); fx.fb->endChangeGesture(); } drag = false; }
private:
    Rectangle<float> area() const { return getLocalBounds().toFloat().withTrimmedTop (30).reduced (10, 6); }
    float xOf (float ms) const { const auto a = area(); return a.getX() + a.getWidth() * ms / (axis * 1000.0f); }
    float msOf (float x) const { const auto a = area(); return (x - a.getX()) / a.getWidth() * axis * 1000.0f; }
    void timerCallback() override { repaint(); }
    DelayFx& fx;
    OwnedArray<TextButton> btns;
    const float axis = 2.5f;
    bool drag = false;
    int axisLock = 0;
    float y0 = 0, x0 = 0, fb0 = 0, ms0 = 0;
};
std::unique_ptr<Component> DelayFx::makeDisplay() { return std::make_unique<DelayDisplay> (*this); }

//==============================================================================
class LimiterFx final : public FxBase
{
public:
    LimiterFx() : FxBase ("Korgus Limiter", "limiter")
    {
        gain = add ("gain", "Zisk", 0, 24, 0, "dB");
        ceil = add ("ceiling", "Strop", -12, 0, -0.3f, "dB");
        rel  = add ("release", "Uvoľnenie", 1, 500, 80, "ms", 0.5f, 0);
        gain->tip = sk ("Zosilní zvuk pred limiterom – výsledok bude hlasnejší, špičky sa zrežú na strop.");
        ceil->tip = sk ("Najvyššia povolená úroveň. −0,3 až −1 dB je bezpečné pre MP3 a streamovanie.");
        rel->tip  = sk ("Ako rýchlo sa limiter uvoľní. Kratší = hlasnejšie, ale môže skresľovať.");
    }
    std::vector<Preset> presets() const override
    {
        return { { "Bezpečný strop", { { "gain", 0 }, { "ceiling", -1 }, { "release", 80 } } },
                 { "Hlasnejšie", { { "gain", 4 }, { "ceiling", -0.5f }, { "release", 60 } } },
                 { "Veľmi hlasno", { { "gain", 8 }, { "ceiling", -0.3f }, { "release", 40 } } } };
    }

private:
    void prepare() override { lim.prepare (spec); lim.reset(); }

    void process (AudioBuffer<float>& b) override
    {
        const float c = ceil->get();
        lim.setThreshold (c);          // juce Limiter normalizuje strop na 0 dBFS…
        lim.setRelease (rel->get());
        const float pre = Decibels::decibelsToGain (gain->get());
        b.applyGainRamp (0, b.getNumSamples(), lastPre, pre);
        lastPre = pre;
        dsp::AudioBlock<float> blk (b);
        lim.process (dsp::ProcessContextReplacing<float> (blk));
        b.applyGain (Decibels::decibelsToGain (c));   // …tak ho vrátime na strop
        for (int ch = 0; ch < b.getNumChannels(); ++ch)
            FloatVectorOperations::clip (b.getWritePointer (ch), b.getReadPointer (ch), -1.0f, 1.0f, b.getNumSamples());
    }

    FxParam *gain, *ceil, *rel;
    dsp::Limiter<float> lim;
    float lastPre = 1.0f;
};


//==============================================================================
// De-esser - stisi sykavky (s, s, c) v speve
class DeEsserFx final : public FxBase
{
public:
    DeEsserFx() : FxBase ("Korgus De-esser", "deesser")
    {
        freq  = add ("freq", "Frekvencia", 3000, 10000, 6000, "Hz", 0.6f, 0);
        thr   = add ("threshold", "Prah", -50, 0, -28, "dB");
        range = add ("range", "Max. stíšenie", 0, 24, 10, "dB");
        listen = add ("listen", "Počúvať sykavky", 0, 1, 0, "", 1.0f, 0);
        listen->range.interval = 1.0f;
        listen->toggle = true;
        freq->tip   = sk ("Od akej frekvencie sú sykavky (s, š, c). V grafe ťahaj zlatú čiaru alebo stlač Nájdi sykavky.");
        thr->tip    = sk ("Od akej hlasitosti sykaviek sa začne stišovať. V grafe vpravo ťahaj zlatú značku.");
        range->tip  = sk ("Najviac o koľko sa sykavky stíšia.");
        listen->tip = sk ("Počuješ len to, čo sa stišuje – pomôže nájsť správnu frekvenciu.");
    }
    std::atomic<float> grDb { 0.0f };   // aktualne stisenie pre zobrazenie
    std::atomic<float> hiDb { -100.0f }; // uroven sykaviek (vysoke pasmo) pre zobrazenie
    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 176; }
    std::vector<Preset> presets() const override
    {
        return { { "Mužský hlas", { { "freq", 5000 }, { "threshold", -30 }, { "range", 8 } } },
                 { "Ženský hlas", { { "freq", 7000 }, { "threshold", -30 }, { "range", 8 } } },
                 { "Jemne", { { "freq", 6000 }, { "threshold", -24 }, { "range", 5 } } },
                 { "Silno", { { "freq", 5500 }, { "threshold", -36 }, { "range", 14 } } } };
    }

private:
    friend class DeEssMeter;
    void prepare() override
    {
        xo.prepare (spec); xo.setType (dsp::LinkwitzRileyFilterType::lowpass);
        xo.setCutoffFrequency (lastF = freq->get()); xo.reset();
        env = 0.0f;
        att = std::exp (-1.0f / (0.0008f * (float) rate));
        relC = std::exp (-1.0f / (0.06f * (float) rate));
    }
    void process (AudioBuffer<float>& b) override
    {
        const float f = freq->get();
        if (std::abs (f - lastF) > 1.0f) { xo.setCutoffFrequency (f); lastF = f; }
        const float t = thr->get(), maxR = range->get();
        const bool lis = listen->get() > 0.5f;
        const int nch = b.getNumChannels(), n = b.getNumSamples();
        float peakGr = 0.0f, envPk = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float lo[2], hi[2], det = 0.0f;
            for (int c = 0; c < nch && c < 2; ++c)
            {
                xo.processSample (c, b.getSample (c, i), lo[c], hi[c]);
                det = jmax (det, std::abs (hi[c]));
            }
            env = det > env ? att * env + (1.0f - att) * det : relC * env + (1.0f - relC) * det;
            envPk = jmax (envPk, env);
            const float over = Decibels::gainToDecibels (env, -100.0f) - t;
            const float gr = over > 0.0f ? jmin (maxR, over * 0.75f) : 0.0f;
            peakGr = jmax (peakGr, gr);
            const float g = Decibels::decibelsToGain (-gr);
            for (int c = 0; c < nch && c < 2; ++c)
                b.setSample (c, i, lis ? hi[c] : lo[c] + hi[c] * g);
        }
        grDb.store (jmax (peakGr, grDb.load() * 0.85f));
        hiDb.store (jmax (Decibels::gainToDecibels (envPk, -100.0f), hiDb.load() - 1.5f));
    }

    FxParam *freq, *thr, *range, *listen;
    dsp::LinkwitzRileyFilter<float> xo;
    float lastF = 6000.0f, env = 0.0f, att = 0.9f, relC = 0.999f;
};

// de-esser: spektrum sykaviek s frekvenciou, uroven sykaviek s prahom, stisenie, automaticke hladanie
class DeEssMeter final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit DeEssMeter (DeEsserFx& p) : fx (p)
    {
        ++fx.scope.users;
        setTooltip (sk ("Ťahaj zlatú čiaru v spektre (frekvencia) a zlatú značku vpravo (prah). Asistent nastaví všetko sám."));
        find.setButtonText (String::fromUTF8 ("✦ ") + sk ("Asistent"));
        find.setColour (TextButton::buttonColourId, GCol::steel);
        find.setColour (TextButton::textColourOffId, Colours::white);
        find.setTooltip (sk ("Pusti spev a stlač – asistent si vypočuje spev, nájde sykavky (s, š, c) a nastaví frekvenciu, prah aj stíšenie."));
        find.onClick = [this]
        {
            if (learn > 0) { learn = 0; msg.clear(); find.setButtonText (String::fromUTF8 ("✦ ") + sk ("Asistent")); repaint(); return; }
            learn = 1; heard = 0; sibAcc.assign (sibAcc.size(), 0.0); sibLv.clear(); restLv.clear(); msg.clear();
            find.setButtonText (sk ("Zastaviť"));
        };
        addAndMakeVisible (find);
        startTimerHz (25);
    }
    ~DeEssMeter() override { stopTimer(); --fx.scope.users; }
    void resized() override { find.setBounds (getWidth() - 132, getHeight() - 30, 124, 24); }
    void paint (Graphics& g) override
    {
        g.setColour (GCol::bg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        const auto a = spArea();
        g.setFont (FontOptions (10.0f));
        for (float hz : { 2000.0f, 3000.0f, 5000.0f, 7000.0f, 10000.0f, 14000.0f })
        {
            const float x = xOf (hz); g.setColour (GCol::grid); g.drawVerticalLine ((int) x, a.getY(), a.getBottom());
            g.setColour (GCol::dim); g.drawText (String ((int) (hz / 1000)) + "k", (int) x - 12, (int) a.getBottom() + 1, 24, 11, Justification::centred);
        }
        const double sr = fx.sampleRate();
        const float f = fx.freq->get(), xf = xOf (f);
        Path sp; sp.startNewSubPath (a.getX(), a.getBottom());
        for (float x = a.getX(); x <= a.getRight(); x += 2.0f) sp.lineTo (x, jmap (jlimit (-100.0f, -10.0f, spec.at (hzOf (x), sr)), -100.0f, -10.0f, a.getBottom(), a.getY()));
        sp.lineTo (a.getRight(), a.getBottom()); sp.closeSubPath();
        g.saveState(); g.reduceClipRegion (a.withLeft (xf).toNearestInt()); g.setColour (GCol::gold.withAlpha (0.35f)); g.fillPath (sp); g.restoreState();
        g.saveState(); g.reduceClipRegion (a.withRight (xf).toNearestInt()); g.setColour (GCol::steel.withAlpha (0.8f)); g.fillPath (sp); g.restoreState();
        g.setColour (drag == 1 ? Colours::white : GCol::gold); g.drawLine (xf, a.getY(), xf, a.getBottom(), 2.0f);
        g.setColour (GCol::text); g.setFont (FontOptions (11.0f));
        g.drawText (sk ("sykavky od") + " " + String (f / 1000.0f, 1) + " kHz", Rectangle<float> (xf + 5, a.getY(), 120, 12), Justification::centredLeft);
        // uroven sykaviek + prah
        const auto m = lvArea();
        g.setColour (GCol::panel); g.fillRoundedRectangle (m, 3.0f);
        const float hi = jlimit (-60.0f, 0.0f, fx.hiDb.load()), t = fx.thr->get();
        const float yHi = yLv (hi), yT = yLv (t);
        g.setColour (hi > t ? GCol::gold : GCol::line); g.fillRoundedRectangle (m.withTop (yHi), 3.0f);
        g.setColour (drag == 2 ? Colours::white : GCol::gold); g.fillRect (m.getX() - 4, yT - 1.5f, m.getWidth() + 8, 3.0f);
        g.setColour (GCol::dim); g.setFont (FontOptions (10.0f));
        g.drawText (sk ("prah"), Rectangle<float> (m.getX() - 46, yT - 6, 40, 12), Justification::centredRight);
        g.drawText (sk ("sykavky"), m.withY (m.getBottom() + 1).withHeight (11).expanded (14, 0), Justification::centred);
        // stisenie
        const float v = jlimit (0.0f, 24.0f, fx.grDb.load());
        auto bar = Rectangle<float> (8, (float) getHeight() - 28, (float) getWidth() - 152, 20);
        g.setColour (GCol::text); g.setFont (FontOptions (12.0f));
        g.drawText (sk ("Stíšenie sykaviek"), bar.removeFromLeft (120), Justification::centredLeft);
        g.setColour (GCol::panel); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (Colour (0xff6f8fb8)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v / 24.0f), 3.0f);
        g.setColour (GCol::text); g.drawText ("-" + String (v, 1) + " dB", bar.reduced (6, 0), Justification::centredRight);
        if (learn > 0 || ! msg.isEmpty())
        {
            auto box = a.reduced (40, 26);
            g.setColour (GCol::bg.withAlpha (0.9f)); g.fillRoundedRectangle (box, 3.0f);
            g.setColour (GCol::text); g.setFont (FontOptions (13.0f));
            if (learn > 0)
            {
                const float pr = (float) heard / (float) kNeed;
                g.drawFittedText (heard == 0 ? sk ("Asistent čaká – pusti spev") : sk ("Asistent počúva spev…") + " " + String (roundToInt (pr * 100.0f)) + " %",
                                  box.removeFromTop (box.getHeight() * 0.6f).toNearestInt().reduced (6, 0), Justification::centredBottom, 1);
                auto bar = box.reduced (20, 0).withHeight (8).translated (0, 6);
                g.setColour (GCol::panel); g.fillRoundedRectangle (bar, 2.0f);
                g.setColour (GCol::gold); g.fillRoundedRectangle (bar.withWidth (jmax (4.0f, bar.getWidth() * pr)), 2.0f);
            }
            else g.drawFittedText (msg, box.reduced (6, 0).toNearestInt(), Justification::centred, 2);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        msg.clear();
        const auto m = lvArea();
        if (std::abs (e.position.x - xOf (fx.freq->get())) < 8 && spArea().contains (e.position)) { drag = 1; fx.freq->beginChangeGesture(); }
        else if (m.expanded (10, 6).contains (e.position)) { drag = 2; fx.thr->beginChangeGesture(); }
        else drag = 0;
    }
    void mouseDrag (const MouseEvent& e) override
    {
        if (drag == 1) fx.freq->setFromUi (hzOf (e.position.x));
        else if (drag == 2) fx.thr->setFromUi (std::round (jmap (e.position.y, lvArea().getBottom(), lvArea().getY(), -60.0f, 0.0f)));
        repaint();
    }
    void mouseUp (const MouseEvent&) override { if (drag == 1) fx.freq->endChangeGesture(); if (drag == 2) fx.thr->endChangeGesture(); drag = 0; }
private:
    Rectangle<float> spArea() const { return getLocalBounds().toFloat().reduced (10, 8).withTrimmedBottom (40).withTrimmedRight (70); }
    Rectangle<float> lvArea() const { const auto a = spArea(); return { a.getRight() + 34, a.getY(), 18, a.getHeight() }; }
    float xOf (float hz) const { const auto a = spArea(); return a.getX() + a.getWidth() * std::log (hz / 1500.0f) / std::log (16000.0f / 1500.0f); }
    float hzOf (float x) const { const auto a = spArea(); return 1500.0f * std::pow (16000.0f / 1500.0f, jlimit (0.0f, 1.0f, (x - a.getX()) / a.getWidth())); }
    float yLv (float db) const { const auto m = lvArea(); return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, m.getBottom(), m.getY()); }
    void timerCallback() override
    {
        spec.update (fx.scope, learn > 0 ? 0.0f : 0.7f);
        if (learn > 0) learnStep();
        repaint();
    }
    // asistent: kazdy snimok spektra zaradi ako sykavku (prevazuju vysky) alebo bezny spev
    static constexpr int kNeed = 200;   // asi 8 s znejuceho spevu
    void learnStep()
    {
        const double sr = fx.sampleRate();
        auto band = [&] (float lo, float hi) { double p = 0; for (int i = 1; i < Spectrum::size / 2; ++i) { const float hz = (float) i * (float) sr / (float) Spectrum::size; if (hz >= lo && hz <= hi) p += std::pow (10.0, spec.db[(size_t) i] / 10.0); } return 10.0 * std::log10 (p + 1.0e-12); };
        const double lo = band (200, 3000), hi = band (4000, 11000);
        if (jmax (lo, hi) < -45.0) return;   // ticho
        const bool sib = hi - lo > -4.0;
        if (sib)
        {
            for (size_t i = 0; i < sibAcc.size(); ++i) sibAcc[i] += std::pow (10.0, spec.db[i] / 10.0);
            sibLv.push_back (fx.hiDb.load());
        }
        else restLv.push_back (fx.hiDb.load());
        if (++heard >= kNeed) finishLearn();
    }
    static float median (std::vector<float> v) { if (v.empty()) return -100.0f; std::sort (v.begin(), v.end()); return v[v.size() / 2]; }
    void finishLearn()
    {
        learn = 0; find.setButtonText (String::fromUTF8 ("✦ ") + sk ("Asistent"));
        const double sr = fx.sampleRate();
        if (sibLv.size() < 6) { msg = sk ("Nenašiel som skoro žiadne sykavky – spev je v poriadku, alebo pusti časť so slovami s, š, c."); return; }
        int best = -1; double bv = -1;
        for (int i = 0; i < (int) sibAcc.size(); ++i)
        {
            const float hz = (float) i * (float) sr / (float) Spectrum::size;
            if (hz < 3500.0f || hz > 10000.0f) continue;
            if (sibAcc[(size_t) i] > bv) { bv = sibAcc[(size_t) i]; best = i; }
        }
        const float peakHz = best >= 0 ? (float) best * (float) sr / (float) Spectrum::size : 6500.0f;
        const float f = jlimit (3000.0f, 10000.0f, std::round (peakHz * 0.8f / 100.0f) * 100.0f);
        const float ms = median (sibLv), mr = median (restLv);
        const float t = jlimit (-50.0f, 0.0f, std::round (jmax (ms - 4.0f, mr + 2.0f)));
        const float d = ms - mr;
        const float rg = d > 14.0f ? 12.0f : (d > 8.0f ? 9.0f : 6.0f);
        auto set = [] (FxParam* p, float v) { p->beginChangeGesture(); p->setFromUi (v); p->endChangeGesture(); };
        set (fx.freq, f); set (fx.thr, t); set (fx.range, rg);
        msg = sk ("Hotovo: sykavky od") + " " + String (f / 1000.0f, 1) + " kHz, " + sk ("prah") + " " + String ((int) t) + " dB, " + sk ("stíšenie do") + " " + String ((int) rg) + " dB";
        Timer::callAfterDelay (5000, [safe = Component::SafePointer<DeEssMeter> (this)] { if (safe != nullptr && safe->learn == 0) { safe->msg.clear(); safe->repaint(); } });
    }
    DeEsserFx& fx;
    Spectrum spec;
    TextButton find;
    std::vector<double> sibAcc = std::vector<double> ((size_t) Spectrum::size / 2, 0.0);
    std::vector<float> sibLv, restLv;
    int learn = 0, heard = 0;
    String msg;
    int drag = 0;
};
std::unique_ptr<Component> DeEsserFx::makeDisplay() { return std::make_unique<DeEssMeter> (*this); }


//==============================================================================
// PulseDip - rytmicke stisenie hlasitosti synchronizovane s tempom
namespace Pulse
{
    inline float smooth (float x) { x = jlimit (0.0f, 1.0f, x); return x * x * (3.0f - 2.0f * x); }
    inline float classic (float f) { return (1.0f - std::exp (-6.0f * f)) / (1.0f - std::exp (-6.0f)); }

    // hlasitost 0..1 v case f (0..1 = jedna perioda)
    inline float shape (int s, float f)
    {
        f = f - std::floor (f);
        const float pi = MathConstants<float>::pi;
        switch (s)
        {
            case 0:  { const float x = std::sin (0.5f * pi * f); return x * x; }   // makky nabeh
            case 1:  return classic (f);                                          // klasika
            case 2:  return f * f * f;                                            // neskory nastup
            case 3:  return smooth ((f - 0.25f) / 0.5f);                          // hlboka pumpa
            case 4:  return smooth (f / 0.25f);                                   // rychly nabeh
            case 5:  return classic (1.0f - f);                                   // opacne
            case 6:  { const float x = std::cos (0.5f * pi * f); return x * x; }  // makky pokles
            case 7:  return std::sin (pi * f);                                    // vrchol
            case 8:  return smooth ((f - 0.5f) / 0.1f);                           // dlhe podrzanie
            case 9:  return smooth (f);                                           // plynuly nabeh
            case 10: return smooth ((f - 0.15f) / 0.03f);                         // tvrda pumpa
            case 11: return f < 0.5f ? 1.0f : 0.0f;                               // brana
            case 12: return jmin (smooth (f / 0.2f), smooth ((1.0f - f) / 0.2f)); // plato
            case 13: return f;                                                    // svah
            case 14: return 1.0f - std::sin (pi * f);                             // udolie
            case 15: return classic (2.0f * f - std::floor (2.0f * f));           // dvojita
            default: return 1.0f;
        }
    }
}

class PulseFx;

class PulseDisplay final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit PulseDisplay (PulseFx& p);
    ~PulseDisplay() override { stopTimer(); }
    void paint (Graphics& g) override;
    void resized() override { custBtn.setBounds (96, 1, 80, 19); }
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
    void mouseUp (const MouseEvent& e) override;
    void mouseDoubleClick (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override;
    void mouseExit (const MouseEvent&) override { hover = -1; hoverPt = -1; repaint(); }
private:
    Rectangle<float> graphArea() const { return getLocalBounds().toFloat().withTrimmedBottom ((float) gridH).reduced (10, 0).withTrimmedTop (22).withTrimmedBottom (8); }
    Point<float> ptPos (int i) const;
    int ptAt (Point<float> p) const;
    float gvAt (float f) const;
    TextButton custBtn;
    int hoverPt = -1, dragPt = -1;
    bool dragOff = false;
    float off0 = 0, x0 = 0;
    static constexpr int gridH = 84;   // 2 riadky ikon tvarov
    Rectangle<float> cellRect (int i) const
    {
        auto g = getLocalBounds().toFloat().removeFromBottom ((float) gridH).reduced (6, 4);
        const float w = g.getWidth() / 8.0f, h = g.getHeight() / 2.0f;
        return { g.getX() + (float) (i % 8) * w + 2, g.getY() + (float) (i / 8) * h + 2, w - 4, h - 4 };
    }
    int cellAt (Point<float> p) const;
    // rezim Kroky: 16 stlpcov cez cely graf
    Rectangle<float> stepArea() const { return getLocalBounds().toFloat().reduced (10, 0).withTrimmedTop (24).withTrimmedBottom (10); }
    void setStepAt (Point<float> p);
    void timerCallback() override;
    PulseFx& fx;
    int hover = -1, dragStep = -1;
};

class PulseFx final : public FxBase
{
public:
    PulseFx() : FxBase ("Korgus PulseDip", "pulse")
    {
        rateP = add ("rate", "Rytmus", 0, 7, 1, "", 1.0f, 0);
        rateP->range.interval = 1.0f;
        rateP->choices = StringArray { "1/8", "1/4", "1/2", "1/1", "1/16", "1/8 T", "1/4 T", "1/16 T" };   // T = trioly (poradie kvoli starym piesnam)
        shp = add ("shape", "Tvar", 0, 15, 1, "", 1.0f, 0);
        shp->range.interval = 1.0f;
        shp->inEditor = false;
        shp->choices = StringArray::fromTokens (sk ("Mäkký nábeh|Klasika|Neskorý nástup|Hlboká pumpa|Rýchly nábeh|Opačne|Mäkký pokles|"
                                                                 "Vrchol|Dlhé podržanie|Plynulý nábeh|Tvrdá pumpa|Brána|Plató|Svah|Údolie|Dvojitá"), "|", "");
        mix = add ("mix", "Hĺbka", 0, 100, 100, "%", 1.0f, 0);
        out = add ("output", "Výstup", -12, 12, 0, "dB");
        mode = add ("mode", "Riadenie", 0, 2, 0, "", 1.0f, 0);
        mode->range.interval = 1.0f;
        mode->choices = StringArray { String ("Tempo"), String ("Sidechain"), sk ("Kroky") };
        on  = add ("active", "Zapnuté", 0, 1, 1, "", 1.0f, 0);
        on->toggle = true;
        // 16 krokov s vlastnou hlasitostou (rezim Kroky) - kreslia sa mysou v grafe
        static const float defStep[16] = { 1, .15f, 1, 1, .15f, 1, 1, .15f, 1, .15f, 1, 1, .15f, 1, .6f, .15f };
        for (int i = 0; i < 16; ++i)
        {
            static const char* nm[16] = { "Krok 1", "Krok 2", "Krok 3", "Krok 4", "Krok 5", "Krok 6", "Krok 7", "Krok 8",
                                          "Krok 9", "Krok 10", "Krok 11", "Krok 12", "Krok 13", "Krok 14", "Krok 15", "Krok 16" };
            steps[i] = add ("step" + String (i + 1), nm[i], 0, 1, defStep[i], "", 1.0f, 2);
            steps[i]->inEditor = false;
        }
        // nove (na konci, aby sedeli indexy starych parametrov)
        offset = add ("offset", "Posun", 0, 100, 0, "%", 1.0f, 0);
        bassOnly = add ("bassonly", "Len basy", 0, 1, 0, "", 1.0f, 0);
        bassOnly->toggle = true;
        custom = add ("custom", "Vlastný tvar", 0, 1, 0, "", 1.0f, 0);
        custom->toggle = true; custom->inEditor = false;
        static const float defX[6] = { 0.0f, 0.1f, 0.25f, 0.5f, 0.75f, 1.0f }, defY[6] = { 0.0f, 0.55f, 0.85f, 1.0f, 1.0f, 1.0f };
        static const char* nx[6] = { "Bod 1 X", "Bod 2 X", "Bod 3 X", "Bod 4 X", "Bod 5 X", "Bod 6 X" };
        static const char* ny[6] = { "Bod 1 Y", "Bod 2 Y", "Bod 3 Y", "Bod 4 Y", "Bod 5 Y", "Bod 6 Y" };
        for (int i = 0; i < 6; ++i)
        {
            cx[i] = add ("c" + String (i) + "x", nx[i], 0, 1, defX[i], "", 1.0f, 2); cx[i]->inEditor = false;
            cy[i] = add ("c" + String (i) + "y", ny[i], 0, 1, defY[i], "", 1.0f, 2); cy[i]->inEditor = false;
        }
        rateP->tip  = sk ("Ako často pumpuje: 1/4 = na každú dobu (ako kopák), 1/8 = dvakrát rýchlejšie, T = trioly.");
        mix->tip    = sk ("Ako hlboko stíši: 100 % = úplne, 30 % = jemné dýchanie.");
        out->tip    = sk ("Hlasitosť po efekte.");
        mode->tip   = sk ("Tempo = podľa tempa piesne, Sidechain = podľa inej stopy (kopák), Kroky = 16 krokov kreslených myšou.");
        on->tip     = sk ("Zapne / vypne pumpovanie.");
        offset->tip = sk ("Posunie krivku v čase – napr. pumpa začne chvíľu po kopáku. V grafe ťahaj krivku do strany.");
        bassOnly->tip = sk ("Stišuje len basy (pod 180 Hz) – spev, hi-haty a výšky ostanú bez pumpovania.");
    }
    FxParam* steps[16] {};
    FxParam *offset = nullptr, *bassOnly = nullptr, *custom = nullptr, *cx[6] {}, *cy[6] {};
    static constexpr int nBins = 64;
    std::array<std::atomic<float>, nBins> binIn {}, binOut {};   // zivy zvuk podla polohy v krivke
    float customShape (float f) const
    {
        f = f - std::floor (f);
        float px = cx[0]->get(), py = cy[0]->get();
        for (int i = 1; i < 6; ++i)
        {
            const float nx = jmax (px, cx[i]->get()), ny = cy[i]->get();
            if (f <= nx || i == 5)
            {
                const float t = nx - px > 1e-4f ? jlimit (0.0f, 1.0f, (f - px) / (nx - px)) : 1.0f;
                return py + (ny - py) * t * t * (3.0f - 2.0f * t);
            }
            px = nx; py = ny;
        }
        return py;
    }
    float shapeAt (float f) const { return custom->get() > 0.5f ? customShape (f) : Pulse::shape (roundToInt (shp->get()), f); }
    std::vector<Preset> presets() const override
    {
        auto P = [] (const char* n, float rt, float sh, float mx, float md, float of, float bo) { return Preset { n, { { "rate", rt }, { "shape", sh }, { "mix", mx }, { "mode", md }, { "offset", of }, { "bassonly", bo }, { "custom", 0 }, { "active", 1 } } }; };
        return { P ("Klasická pumpa", 1, 1, 100, 0, 0, 0), P ("Jemné dýchanie", 1, 0, 45, 0, 0, 0), P ("Len basy – pumpa", 1, 3, 100, 0, 0, 1),
                 P ("Trance brána 1/16", 4, 11, 100, 0, 0, 0), P ("Rýchle osminy", 0, 1, 80, 0, 0, 0), P ("Pomalá vlna 1/2", 2, 9, 70, 0, 0, 0),
                 P ("Neskorá pumpa", 1, 1, 100, 0, 12, 0), P ("Sidechain z kopáku", 1, 1, 100, 1, 0, 0) };
    }
    std::atomic<int> stepNow { -1 };
    bool stepMode() const { return roundToInt (mode->get()) == 2; }

    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<PulseDisplay> (*this); }
    int displayHeight() const override { return 236; }

    std::atomic<float> phaseNow { 0 }, bpmNow { 120 };
    FxParam *rateP, *shp, *mix, *out, *mode, *on;
    std::atomic<bool> keyMode { false };

private:
    void prepare() override
    {
        freePpq = 0; gSm = 1; ogSm = 1;
        xo.prepare (spec); xo.setType (dsp::LinkwitzRileyFilterType::lowpass); xo.setCutoffFrequency (180.0f); xo.reset();
    }

    void process (AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        double bpm = 120, ppq = freePpq;
        bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto v = pos->getBpm()) bpm = jlimit (20.0, 400.0, *v);
                playing = pos->getIsPlaying();
                if (playing) if (auto q = pos->getPpqPosition()) ppq = *q;
            }
        static const double beats[8] = { 0.5, 1.0, 2.0, 4.0, 0.25, 1.0 / 3.0, 2.0 / 3.0, 1.0 / 6.0 };
        const double per = beats[jlimit (0, 7, roundToInt (rateP->get()))];
        const bool stepM = stepMode();
        float stv[16];
        for (int k = 0; k < 16; ++k) stv[k] = steps[k]->get();
        const int s = roundToInt (shp->get());
        const float depth = on->get() > 0.5f ? mix->get() * 0.01f : 0.0f;
        const float og = Decibels::decibelsToGain (out->get());
        const double offs = offset->get() * 0.01;
        const bool cust = custom->get() > 0.5f, bass = bassOnly->get() > 0.5f;
        float cX[6], cY[6];
        for (int k = 0; k < 6; ++k) { cX[k] = cx[k]->get(); cY[k] = cy[k]->get(); }
        auto custF = [&] (float f)
        {
            float px = cX[0], py = cY[0];
            for (int k = 1; k < 6; ++k)
            {
                const float nx = jmax (px, cX[k]), ny = cY[k];
                if (f <= nx || k == 5) { const float t = nx - px > 1e-4f ? jlimit (0.0f, 1.0f, (f - px) / (nx - px)) : 1.0f; return py + (ny - py) * t * t * (3.0f - 2.0f * t); }
                px = nx; py = ny;
            }
            return py;
        };
        float bIn[nBins] {}, bOut[nBins] {};
        const double dq = bpm / 60.0 / rate;
        const float a = (float) std::exp (-1.0 / (0.002 * rate));   // 2 ms proti lupancom
        const bool useKey = roundToInt (mode->get()) == 1 && scL != nullptr && scN >= n;
        keyMode = roundToInt (mode->get()) == 1;
        const float kA = (float) std::exp (-1.0 / (0.002 * rate)), kR = (float) std::exp (-1.0 / (0.180 * rate));
        for (int i = 0; i < n; ++i)
        {
            const double q = ppq + i * dq, qp = q / per + offs;
            const float f = (float) (qp - std::floor (qp));
            float shapeG;
            if (useKey)
            {
                const float x = jmax (std::abs (scL[i]), std::abs (scR[i]));
                kEnv = x > kEnv ? x + (kEnv - x) * kA : x + (kEnv - x) * kR;
                shapeG = 1.0f - jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (kEnv, -120.0f) + 36.0f) / 30.0f);
            }
            else if (stepM) { const auto si = (int) (((long long) std::floor (q / per)) & 15); shapeG = stv[si]; }
            else shapeG = cust ? custF (f) : Pulse::shape (s, f);
            const float target = 1.0f - depth * (1.0f - shapeG);
            gSm = target + (gSm - target) * a;
            ogSm = og + (ogSm - og) * a;
            const int bin = jlimit (0, nBins - 1, (int) (f * (float) nBins));
            for (int c = 0; c < ch; ++c)
            {
                float* d = b.getWritePointer (c);
                const float x = d[i];
                float y;
                if (bass && c < 2) { float lo, hi; xo.processSample (c, x, lo, hi); y = (lo * gSm + hi) * ogSm; }
                else y = x * gSm * ogSm;
                d[i] = y;
                bIn[bin] = jmax (bIn[bin], std::abs (x));
                bOut[bin] = jmax (bOut[bin], std::abs (y));
            }
        }
        for (int k = 0; k < nBins; ++k)
        {
            if (bIn[k] > binIn[(size_t) k].load()) binIn[(size_t) k] = bIn[k];
            if (bOut[k] > binOut[(size_t) k].load()) binOut[(size_t) k] = bOut[k];
        }
        freePpq = ppq + n * dq;
        if (freePpq > 1.0e6) freePpq = 0;
        phaseNow = (float) (ppq / per + offs - std::floor (ppq / per + offs));
        stepNow = stepM ? (int) (((long long) std::floor (ppq / per)) & 15) : -1;
        bpmNow = (float) bpm;
    }


    double freePpq = 0;
    float gSm = 1, ogSm = 1, kEnv = 0;
    dsp::LinkwitzRileyFilter<float> xo;
};

int PulseDisplay::cellAt (Point<float> p) const
{
    if (fx.stepMode()) return -1;
    for (int i = 0; i < 16; ++i) if (cellRect (i).contains (p)) return i;
    return -1;
}

void PulseDisplay::setStepAt (Point<float> p)
{
    const auto a = stepArea();
    const int i = jlimit (0, 15, (int) ((p.x - a.getX()) / a.getWidth() * 16.0f));
    const float v = jlimit (0.0f, 1.0f, (a.getBottom() - p.y) / a.getHeight());
    auto* prm = fx.steps[i];
    if (dragStep != i) { if (dragStep >= 0) fx.steps[dragStep]->endChangeGesture(); prm->beginChangeGesture(); dragStep = i; }
    prm->setValueNotifyingHost (prm->range.convertTo0to1 (v));
    repaint();
}

PulseDisplay::PulseDisplay (PulseFx& p) : fx (p)
{
    setTooltip (sk ("Krivka = ako sa mení hlasitosť počas jednej doby. Ťahaj krivku do strany = posun. Vlastný = guličky ťaháš myšou. Pod krivkou je živý zvuk (sivý = pred, zlatý = po)."));
    custBtn.setButtonText (sk ("Vlastný"));
    custBtn.setTooltip (sk ("Vlastný tvar: guličky na krivke ťaháš myšou"));
    custBtn.setColour (TextButton::buttonColourId, Colour (0xff1c2532));
    custBtn.setColour (TextButton::buttonOnColourId, GCol::steel);
    custBtn.setColour (TextButton::textColourOffId, GCol::text);
    custBtn.setColour (TextButton::textColourOnId, Colours::white);
    custBtn.onClick = [this]
    {
        const bool on = fx.custom->get() < 0.5f;
        if (on)   // zaciatok z aktualneho tvaru
        {
            static const float xs[6] = { 0.0f, 0.2f, 0.4f, 0.6f, 0.8f, 1.0f };
            const int s = roundToInt (fx.shp->get());
            for (int i = 0; i < 6; ++i) { fx.cx[i]->setFromUi (xs[i]); fx.cy[i]->setFromUi (Pulse::shape (s, jmin (0.999f, xs[i]))); }
        }
        fx.custom->setFromUi (on ? 1.0f : 0.0f);
        repaint();
    };
    addAndMakeVisible (custBtn);
    startTimerHz (30);
}

void PulseDisplay::timerCallback()
{
    for (int k = 0; k < PulseFx::nBins; ++k) { fx.binIn[(size_t) k] = fx.binIn[(size_t) k].load() * 0.9f; fx.binOut[(size_t) k] = fx.binOut[(size_t) k].load() * 0.9f; }
    custBtn.setVisible (! fx.stepMode() && ! fx.keyMode.load());
    custBtn.setToggleState (fx.custom->get() > 0.5f, dontSendNotification);
    repaint();
}

float PulseDisplay::gvAt (float f) const
{
    const float depth = fx.on->get() > 0.5f ? fx.mix->get() * 0.01f : 0.0f;
    return 1.0f - depth * (1.0f - fx.shapeAt (f));
}

// krivka sa kresli od polohy 0 v case (posun = krivka sa posunie doprava)
Point<float> PulseDisplay::ptPos (int i) const
{
    const auto a = graphArea();
    const float depth = fx.on->get() > 0.5f ? fx.mix->get() * 0.01f : 0.0f, off = fx.offset->get() * 0.01f;
    float t = fx.cx[i]->get() - off; t -= std::floor (t);
    if (i == 5 && off < 0.001f) t = 1.0f;
    const float gv = 1.0f - depth * (1.0f - fx.cy[i]->get());
    return { a.getX() + t * a.getWidth(), a.getBottom() - gv * a.getHeight() };
}

int PulseDisplay::ptAt (Point<float> p) const
{
    if (fx.custom->get() < 0.5f || fx.stepMode() || fx.keyMode.load()) return -1;
    int best = -1; float bd = 10.0f;
    for (int i = 0; i < 6; ++i) { const float d = ptPos (i).getDistanceFrom (p); if (d < bd) { bd = d; best = i; } }
    return best;
}

void PulseDisplay::mouseMove (const MouseEvent& e)
{
    const int h = cellAt (e.position), hp = ptAt (e.position);
    setMouseCursor (hp >= 0 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
    if (h != hover || hp != hoverPt) { hover = h; hoverPt = hp; repaint(); }
}

void PulseDisplay::mouseDrag (const MouseEvent& e)
{
    if (fx.stepMode()) { setStepAt (e.position); return; }
    const auto a = graphArea();
    if (dragPt >= 0)
    {
        const float off = fx.offset->get() * 0.01f, depth = jmax (0.05f, fx.on->get() > 0.5f ? fx.mix->get() * 0.01f : 0.0f);
        float x = (e.position.x - a.getX()) / a.getWidth() + off; x -= std::floor (x);
        const float lo = dragPt > 0 ? fx.cx[dragPt - 1]->get() : 0.0f, hi = dragPt < 5 ? fx.cx[dragPt + 1]->get() : 1.0f;
        if (dragPt > 0 && dragPt < 5) fx.cx[dragPt]->setFromUi (jlimit (lo + 0.01f, hi - 0.01f, x));
        const float gv = jlimit (0.0f, 1.0f, (a.getBottom() - e.position.y) / a.getHeight());
        fx.cy[dragPt]->setFromUi (jlimit (0.0f, 1.0f, 1.0f - (1.0f - gv) / depth));
        repaint(); return;
    }
    if (dragOff)
    {
        float o = off0 - (e.position.x - x0) / a.getWidth() * 100.0f;
        o -= 100.0f * std::floor (o / 100.0f);
        fx.offset->setFromUi (std::round (o));
        repaint();
    }
}
void PulseDisplay::mouseUp (const MouseEvent&)
{
    if (dragStep >= 0) { fx.steps[dragStep]->endChangeGesture(); dragStep = -1; }
    if (dragPt >= 0) { fx.cx[dragPt]->endChangeGesture(); fx.cy[dragPt]->endChangeGesture(); dragPt = -1; }
    if (dragOff) { fx.offset->endChangeGesture(); dragOff = false; }
}
void PulseDisplay::mouseDoubleClick (const MouseEvent& e)
{
    if (! fx.stepMode() && graphArea().contains (e.position) && ptAt (e.position) < 0) { fx.offset->setFromUi (0.0f); repaint(); }
}

void PulseDisplay::mouseDown (const MouseEvent& e)
{
    if (fx.stepMode())
    {
        if (e.mods.isRightButtonDown() || e.getNumberOfClicks() > 1)   // pravy klik / dvojklik = krok naplno alebo potichu
        {
            const auto a = stepArea();
            const int i = jlimit (0, 15, (int) ((e.position.x - a.getX()) / a.getWidth() * 16.0f));
            auto* prm = fx.steps[i];
            prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->get() > 0.5f ? 0.0f : 1.0f); prm->endChangeGesture();
            repaint(); return;
        }
        setStepAt (e.position);
        return;
    }
    if (const int pi = ptAt (e.position); pi >= 0) { dragPt = pi; fx.cx[pi]->beginChangeGesture(); fx.cy[pi]->beginChangeGesture(); return; }
    if (graphArea().contains (e.position) && ! fx.keyMode.load()) { dragOff = true; off0 = fx.offset->get(); x0 = e.position.x; fx.offset->beginChangeGesture(); return; }
    const int i = cellAt (e.position);
    if (i < 0) return;
    fx.shp->beginChangeGesture();
    fx.shp->setValueNotifyingHost (fx.shp->range.convertTo0to1 ((float) i));
    fx.shp->endChangeGesture();
    fx.custom->setFromUi (0.0f);
    repaint();
}

void PulseDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
    if (fx.stepMode())
    {
        const auto a = stepArea();
        const float w = a.getWidth() / 16.0f, depth = fx.on->get() > 0.5f ? fx.mix->get() * 0.01f : 0.0f;
        const int cur = fx.stepNow.load();
        for (int i = 0; i < 16; ++i)
        {
            auto col = Rectangle<float> (a.getX() + (float) i * w + 2, a.getY(), w - 4, a.getHeight());
            g.setColour (i % 4 == 0 ? Colour (0xff222b36) : Colour (0xff1c232c));
            g.fillRoundedRectangle (col, 3.0f);
            const float v = fx.steps[i]->get(), gv = 1.0f - depth * (1.0f - v);
            auto bar = col.withTrimmedTop (col.getHeight() * (1.0f - gv));
            g.setColour (i == cur ? Colour (0xffc9a35a) : Colour (0xff4d6f96));
            g.fillRoundedRectangle (bar, 3.0f);
            g.setColour (Colour (0xff8fb0d0));
            g.fillRect (bar.withHeight (2.0f));
        }
        g.setFont (FontOptions (12.0f));
        g.setColour (Colour (0xff8a96a3));
        g.drawText (sk ("PULSEDIP – KROKY  (ťahaj myšou, pravý klik = zap/vyp)"), r.reduced (10, 4).removeFromTop (14), Justification::centredLeft);
        g.drawText (String (fx.bpmNow.load(), 1) + " BPM  /  " + fx.rateP->choices[roundToInt (fx.rateP->get())], r.reduced (10, 4).removeFromTop (14), Justification::centredRight);
        return;
    }
    // ikony tvarov
    {
        const int cur = roundToInt (fx.shp->get());
        for (int i = 0; i < 16; ++i)
        {
            auto c = cellRect (i);
            const bool isCur = i == cur && fx.custom->get() < 0.5f;
            g.setColour (isCur ? Colour (0xff2d4362) : (i == hover ? Colour (0xff27313d) : Colour (0xff1f2630)));
            g.fillRoundedRectangle (c, 3.0f);
            if (isCur) { g.setColour (Colour (0xff8fb0d0)); g.drawRoundedRectangle (c.reduced (0.5f), 3.0f, 1.0f); }
            auto a = c.reduced (5, 6);
            Path pth;
            const int W = jmax (2, (int) a.getWidth());
            for (int x = 0; x <= W; ++x)
            {
                const float f = (float) x / (float) W, py = a.getBottom() - Pulse::shape (i, f) * a.getHeight();
                if (x == 0) pth.startNewSubPath (a.getX(), py); else pth.lineTo (a.getX() + (float) x, py);
            }
            g.setColour (isCur ? Colour (0xffe4e9ef) : Colour (0xff8fb0d0));
            g.strokePath (pth, PathStrokeType (1.4f));
        }
        r.removeFromBottom ((float) gridH);
    }
    const auto area = graphArea();
    const float off = fx.offset->get() * 0.01f;
    g.setColour (Colour (0xff10161e)); g.fillRoundedRectangle (area, 3.0f);
    for (int k = 1; k < 4; ++k) { g.setColour (Colour (0xff1e2938)); g.drawVerticalLine ((int) (area.getX() + area.getWidth() * (float) k / 4.0f), area.getY(), area.getBottom()); }
    // zivy zvuk: svetly = pred efektom, tmavsi = po (podla polohy v krivke)
    {
        const int nB = PulseFx::nBins;
        const float bw = area.getWidth() / (float) nB;
        for (int k = 0; k < nB; ++k)
        {
            float t = (float) k / (float) nB - off; t -= std::floor (t);
            const float x = area.getX() + t * area.getWidth();
            const float hi = jlimit (0.0f, 1.0f, fx.binIn[(size_t) k].load()) * area.getHeight();
            const float ho = jlimit (0.0f, 1.0f, fx.binOut[(size_t) k].load()) * area.getHeight();
            g.setColour (Colour (0xff3a4a60).withAlpha (0.55f)); g.fillRect (x, area.getCentreY() - hi * 0.5f, jmax (1.0f, bw - 1.0f), hi);
            g.setColour (GCol::gold.withAlpha (0.5f)); g.fillRect (x, area.getCentreY() - ho * 0.5f, jmax (1.0f, bw - 1.0f), ho);
        }
    }
    // jedna perioda krivky (posunuta o Posun)
    Path fill, line;
    const int W = jmax (2, (int) area.getWidth());
    for (int x = 0; x <= W; ++x)
    {
        const float f = (float) x / (float) W + off;
        const float px = area.getX() + (float) x, py = area.getBottom() - gvAt (f - std::floor (f)) * area.getHeight();
        if (x == 0) { line.startNewSubPath (px, py); fill.startNewSubPath (px, area.getBottom()); }
        else line.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setColour (Colour (0xff2d4362).withAlpha (0.55f));
    g.fillPath (fill);
    g.setColour (Colour (0xff8fb0d0));
    g.strokePath (line, PathStrokeType (2.0f));
    if (fx.custom->get() > 0.5f && ! fx.keyMode.load())
        for (int i = 0; i < 6; ++i)
        {
            const auto p = ptPos (i);
            g.setColour (i == hoverPt || i == dragPt ? Colours::white : GCol::gold);
            g.fillRoundedRectangle (p.x - 5.5f, p.y - 5.5f, 11.0f, 11.0f, 2.0f);
        }
    // poloha prehravania
    if (! fx.keyMode.load())
    {
        float ph = fx.phaseNow.load() - off; ph -= std::floor (ph);
        const float gx = area.getX() + ph * area.getWidth();
        g.setColour (GCol::gold.withAlpha (0.8f)); g.drawLine (gx, area.getY(), gx, area.getBottom(), 1.5f);
        g.setColour (Colours::white); g.fillRoundedRectangle (gx - 4.0f, area.getBottom() - gvAt (fx.phaseNow.load()) * area.getHeight() - 4.0f, 8.0f, 8.0f, 2.0f);
    }
    g.setFont (FontOptions (12.0f));
    g.setColour (Colour (0xff8a96a3));
    auto head = r.reduced (10, 4).removeFromTop (14);
    String left = "PULSEDIP";
    if (off > 0.001f) left << String::fromUTF8 ("  ·  ") << sk ("posun") << " " << String (roundToInt (off * 100.0f)) << " %";
    if (fx.bassOnly->get() > 0.5f) left << String::fromUTF8 ("  ·  ") << sk ("len basy");
    g.drawText (left, head, Justification::centredLeft);
    if (fx.keyMode.load())
        g.drawText (sk ("SIDECHAIN – riadi kľúčová stopa"), head, Justification::centredRight);
    else
        g.drawText (String (fx.bpmNow.load(), 1) + " BPM  /  " + fx.rateP->choices[roundToInt (fx.rateP->get())] + "  /  "
                    + (fx.custom->get() > 0.5f && hover < 0 ? sk ("Vlastný") : fx.shp->choices[hover >= 0 ? hover : roundToInt (fx.shp->get())]),
                    head.withTrimmedRight (0), Justification::centredRight);
}


//==============================================================================
// Korgus Sampler - jeden zvuk (wav, mp3...) hrany po tonoch z klaves
class SamplerInst;

class SamplerDisplay final : public Component, public FileDragAndDropTarget, private Timer
{
public:
    explicit SamplerDisplay (SamplerInst& s);
    ~SamplerDisplay() override { stopTimer(); }
    void paint (Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const StringArray& f) override;
    void filesDropped (const StringArray& f, int, int) override;
    void mouseDown (const MouseEvent&) override;
    void mouseDrag (const MouseEvent&) override;
    void mouseUp (const MouseEvent&) override;
    void mouseMove (const MouseEvent&) override;
    void mouseWheelMove (const MouseEvent&, const MouseWheelDetails&) override;
    void mouseDoubleClick (const MouseEvent&) override { v0 = 0.0f; v1 = 1.0f; repaint(); }
private:
    float v0 = 0.0f, v1 = 1.0f, panFrom = 0.0f;   // zoom: zobrazeny usek zvuku
    float pxPerF() const { return waveArea().getWidth() / (v1 - v0); }
    float xOf (float f) const { return waveArea().getX() + (f - v0) * pxPerF(); }
    float fOf (float x) const { return jlimit (0.0f, 1.0f, v0 + (x - waveArea().getX()) / pxPerF()); }
    void timerCallback() override;
    void choose();
    Rectangle<float> waveArea() const { return getLocalBounds().toFloat().reduced (10, 0).withTrimmedTop (38).withTrimmedBottom (44); }
    int hitEdge (float x, float fadeY = -1.0f) const;
    SamplerInst& sm;
    TextButton load, loopB, scanB, fromB, help { "?" };
    Slider rateS { Slider::LinearBar, Slider::TextBoxLeft }, rangeS { Slider::LinearBar, Slider::TextBoxLeft };
    ComboBox shapeC;
    std::unique_ptr<FileChooser> chooser;
    int dragMode = 0;            // 1 = zaciatok, 2 = koniec, 3 = cela slucka, 6 = nabeh, 7 = stisenie
    float dLs = 0, dLe = 0, dFi = 0, dFo = 0;
    bool hadSample = false;
    // stvorceky nabehu / stisenia slucky (hore v slucke)
    Rectangle<float> fadeBox (bool out) const;
};

class SamplerInst final : public FxBase
{
public:
    SamplerInst() : FxBase ("Korgus Sampler", "sampler", true)
    {
        root = add ("root", "Kľúčová nota", 24, 96, 60, "", 1.0f, 0);
        root->range.interval = 1.0f;
        root->textFn = [] (float v) { return MidiMessage::getMidiNoteName (roundToInt (v), true, true, 3); };
        tune = add ("tune", "Ladenie", -100, 100, 0, "cent", 1.0f, 0);
        att  = add ("attack", "Nábeh", 0, 2000, 2, "ms", 0.35f, 0);
        rel  = add ("release", "Uvoľnenie", 5, 5000, 250, "ms", 0.35f, 0);
        velS = add ("velocity", "Citlivosť", 0, 100, 70, "%", 1.0f, 0);
        vol  = add ("volume", "Hlasitosť", -24, 12, 0, "dB");
        mode = add ("mode", "Hrať", 0, 1, 0, "", 1.0f, 0);
        mode->range.interval = 1.0f;
        mode->choices = StringArray { sk ("Kým držím klávesu"), sk ("Celý zvuk") };
        // glide (portamento): ton sa plynulo presunie z vysky predchadzajuceho; Mono = jeden ton naraz, legato bez noveho nabehu (808 basy)
        glideP = add ("glide", "Glide", 0, 1000, 0, "ms", 0.35f, 0);
        monoP  = add ("mono", "Mono", 0, 1, 0, "", 1.0f, 0);  monoP->toggle = true;
        // slucka a skenovanie - ovladaju sa v grafe
        loopP  = add ("loop", "Slučka", 0, 1, 0, "", 1.0f, 0);          loopP->toggle = true;
        lsP    = add ("loopStart", "Začiatok slučky", 0, 1, 0.30f, "", 1.0f, 3);
        leP    = add ("loopEnd", "Koniec slučky", 0, 1, 0.70f, "", 1.0f, 3);
        scanP  = add ("scan", "Skenovať", 0, 1, 0, "", 1.0f, 0);        scanP->toggle = true;
        sRate  = add ("scanRate", "Rýchlosť skenovania", 0.02f, 4.0f, 0.25f, "Hz", 0.35f, 2);
        sRange = add ("scanRange", "Rozsah skenovania", 0, 100, 50, "%", 1.0f, 0);
        sShape = add ("scanShape", "Tvar skenovania", 0, 2, 0, "", 1.0f, 0);
        sShape->range.interval = 1.0f;
        sShape->choices = StringArray { sk ("Tam a späť"), sk ("Dopredu"), sk ("Náhodne") };
        // nabeh a stisenie slucky (cast dlzky slucky): stvorceky hore v grafe; prekryvaju sa ako prelinanie
        lfiP   = add ("loopFadeIn", "Nábeh slučky", 0, 0.5f, 0, "", 1.0f, 3);
        lfoP   = add ("loopFadeOut", "Stíšenie slučky", 0, 0.5f, 0, "", 1.0f, 3);
        // so sluckou: zapnute = ton zacne od zaciatku zvuku a potom cykluje, vypnute = hra hned v slucke
        fromP  = add ("loopFromStart", "Od začiatku", 0, 1, 0, "", 1.0f, 0);  fromP->toggle = true;
        for (auto* q : { loopP, lsP, leP, scanP, sRate, sRange, sShape, lfiP, lfoP, fromP }) q->inEditor = false;
    }
    int displayHeight() const override { return 206; }
    FxParam *loopP, *lsP, *leP, *scanP, *sRate, *sRange, *sShape, *lfiP, *lfoP, *fromP;

    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<SamplerDisplay> (*this); }
    bool isBusesLayoutSupported (const BusesLayout& l) const override
    {
        const auto out = l.getMainOutputChannelSet();
        return out == AudioChannelSet::mono() || out == AudioChannelSet::stereo();
    }
    bool acceptsMidi() const override { return true; }
    double getTailLengthSeconds() const override { return 1.0; }

    bool loadFile (const File& f, String& err)
    {
        AudioFormatManager fm;
        fm.registerBasicFormats();
        std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (f));
        if (r == nullptr || r->lengthInSamples <= 0) { err = sk ("Tento súbor sa nedá prečítať."); return false; }
        if (r->lengthInSamples > (int64) (r->sampleRate * 120)) { err = sk ("Zvuk je dlhší ako 2 minúty."); return false; }
        auto b = std::make_shared<AudioBuffer<float>> (jlimit (1, 2, (int) r->numChannels), (int) r->lengthInSamples);
        r->read (b.get(), 0, (int) r->lengthInSamples, 0, true, b->getNumChannels() > 1);
        MemoryBlock bytes;
        if (f.getSize() < 6 * 1024 * 1024) f.loadFileAsData (bytes);
        {
            const SpinLock::ScopedLockType l (lock);
            sample = b; sampleRate = r->sampleRate;
            for (auto& v : voices) v.on = false;
        }
        fileName = f.getFileName(); filePath = f.getFullPathName(); fileData = bytes;
        return true;
    }

    std::shared_ptr<AudioBuffer<float>> getSample() const { const SpinLock::ScopedLockType l (lock); return sample; }
    String fileName, filePath;

    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX");
        x.setAttribute ("type", "sampler");
        for (auto* p : knobs) x.setAttribute (p->id, (double) p->get());
        x.setAttribute ("file", filePath);
        x.setAttribute ("name", fileName);
        if (fileData.getSize() > 0) x.setAttribute ("data", fileData.toBase64Encoding());
        copyXmlToBinary (x, mb);
    }

    void setStateInformation (const void* data, int size) override
    {
        auto x = getXmlFromBinary (data, size);
        if (x == nullptr) return;
        for (auto* p : knobs)
            if (x->hasAttribute (p->id)) p->setValueNotifyingHost (p->range.convertTo0to1 ((float) x->getDoubleAttribute (p->id)));
        String err;
        const File f (x->getStringAttribute ("file"));
        if (f.existsAsFile()) { loadFile (f, err); return; }
        MemoryBlock mb;
        if (mb.fromBase64Encoding (x->getStringAttribute ("data")) && mb.getSize() > 0)
        {
            // zvuk ulozeny v projekte (napr. na inom pocitaci) - rozbalit do docasneho suboru
            const auto name = x->getStringAttribute ("name", "sample.wav");
            const auto tmp = File::getSpecialLocation (File::tempDirectory).getChildFile ("korgus_sampler").getChildFile (name);
            tmp.getParentDirectory().createDirectory();
            tmp.replaceWithData (mb.getData(), mb.getSize());
            if (loadFile (tmp, err)) filePath = f.getFullPathName().isNotEmpty() ? f.getFullPathName() : tmp.getFullPathName();
        }
    }

    void processBlock (AudioBuffer<float>& buf, MidiBuffer& midi) override
    {
        ScopedNoDenormals nd;
        const int n = buf.getNumSamples(), ch = buf.getNumChannels();
        buf.clear();
        const SpinLock::ScopedTryLockType l (lock);
        if (! l.isLocked() || sample == nullptr) return;
        const auto& smp = *sample;
        const int sl = smp.getNumSamples(), sc = smp.getNumChannels();
        const float aS = (float) jmax (1.0, att->get() * 0.001 * rate);
        const float rCoef = (float) std::exp (-1.0 / jmax (1.0, rel->get() * 0.001 * rate / 4.6));
        const float g = Decibels::decibelsToGain (vol->get()), vs = velS->get() * 0.01f;
        const bool loopOn = loopP->get() > 0.5f && sl > 64, scanOn = loopOn && scanP->get() > 0.5f;
        const bool whole = mode->get() > 0.5f;   // Cely zvuk: pustenie klavesy ton nezastavi (so sluckou ju opusti a dohra zvuk do konca)
        const double lsS = jlimit (0.0, sl - 8.0, (double) lsP->get() * sl);
        const double leS = jlimit (lsS + 4.0, sl - 3.0, (double) leP->get() * sl);
        const double xfS = 0.008 * sampleRate;                 // prelinanie konca slucky (8 ms)
        const double scanRateHz = sRate->get(), scanInc = scanRateHz / rate, range = sRange->get() * 0.01;
        const double lfi = lfiP->get(), lfo = lfoP->get();
        const bool fades = loopOn && (lfi > 0.0005 || lfo > 0.0005);   // vlastny nabeh / stisenie slucky
        const int shape = roundToInt (sShape->get());
        int pos = 0;
        auto rd = [&] (int c, double p)
        {
            p = jlimit (0.0, sl - 2.0, p);
            const int k = (int) p;
            const float fr = (float) (p - k);
            const float* d = smp.getReadPointer (jmin (c, sc - 1));
            const float x0 = d[jmax (0, k - 1)], x1 = d[k], x2 = d[k + 1], x3 = d[jmin (sl - 1, k + 2)];
            const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            return ((c3 * fr + c2) * fr + c1) * fr + x1;
        };
        auto render = [&] (int from, int to)
        {
            for (auto& v : voices)
            {
                if (! v.on) continue;
                const bool vLoop = loopOn && ! v.outLoop;   // "Cely zvuk": po pusteni klavesy dohra zvuk za sluckou
                for (int i = from; i < to; ++i)
                {
                    double a = lsS, b = leS;
                    auto scanJump = [&]
                    {   // okno slucky sa posunie pri kazdom prechode; skok podla rychlosti (pomaly = vyska drzi ton)
                        const double room = jmax (0.0, (sl - 3.0) - leS) * range;
                        double f;
                        if (shape == 0) { const double q = std::fmod (v.scanPh, 1.0); f = q < 0.5 ? q * 2.0 : 2.0 - q * 2.0; }
                        else if (shape == 1) f = std::fmod (v.scanPh, 1.0);
                        else { const int st = (int) v.scanPh; if (st != v.scanStep) { v.scanStep = st; v.scanRnd = rng.nextFloat(); } f = v.scanRnd; }
                        const double maxStep = (leS - lsS) * (0.005 + 0.08 * scanRateHz);
                        const double nOff = v.o0 + jlimit (-maxStep, maxStep, room * f - v.o0);
                        v.pos += nOff - v.o0; a += nOff - v.o0; b += nOff - v.o0; v.o0 = nOff;
                    };
                    float fadeG = 1.0f;
                    if (vLoop && fades)
                    {   // stisenie na konci slucky dohra druha hlava, hlavna zacne od zaciatku slucky s nabehom
                        if (scanOn) { v.scanPh += scanInc; a += v.o0; b += v.o0; }
                        const double L = b - a, foS = lfo * L, fiS = lfi * L;
                        if (v.pos >= b - foS)
                        {
                            if (foS > 1.0) { v.tailOn = true; v.tailPos = v.pos; v.tailLeft = b - v.pos; v.tailLen = foS; }
                            v.pos = a + (v.pos - (b - foS));
                            v.inLoop = true;
                            if (scanOn) scanJump();
                        }
                        if (v.inLoop && fiS > 1.0) fadeG = (float) jlimit (0.0, 1.0, (v.pos - a) / fiS);
                    }
                    else if (vLoop)
                    {
                        if (scanOn) { v.scanPh += scanInc; a += v.o0; b += v.o0; }
                        if (v.pos >= b)
                        {
                            v.pos -= (b - a);
                            if (scanOn) scanJump();
                        }
                    }
                    else if (v.pos >= sl - 2) { v.on = false; break; }
                    float e;
                    if (v.releasing) { v.env *= rCoef; e = v.env; if (e < 1.0e-4f) { v.on = false; break; } }
                    else { v.env = jmin (1.0f, v.env + 1.0f / aS); e = v.env; }
                    const double X = vLoop && ! fades ? jmin (xfS, (b - a) * 0.5) : 0.0;
                    const float w = (X > 1.0 && v.pos > b - X) ? (float) ((v.pos - (b - X)) / X) : 0.0f;
                    for (int c = 0; c < ch; ++c)
                    {
                        float y = rd (c, v.pos);
                        if (w > 0.0f) y += w * (rd (c, v.pos - (b - a)) - y);   // bez svu na konci slucky
                        y *= fadeG;
                        if (v.tailOn) y += rd (c, v.tailPos) * (float) jlimit (0.0, 1.0, v.tailLeft / v.tailLen);
                        buf.getWritePointer (c)[i] += y * e * v.gain;
                    }
                    if (v.tailOn) { v.tailPos += v.step; v.tailLeft -= v.step; if (v.tailLeft <= 0.0) v.tailOn = false; }
                    if (v.step != v.target) { v.step += (v.target - v.step) * v.gl; if (std::abs (v.target - v.step) < 1.0e-7) v.step = v.target; }
                    v.pos += v.step;
                }
            }
        };
        for (const auto md : midi)
        {
            const auto m = md.getMessage();
            const int at = jlimit (0, n, md.samplePosition);
            render (pos, at);
            pos = at;
            if (m.isNoteOn())
            {
                const double semis0 = m.getNoteNumber() - root->get() + tune->get() / 100.0;
                const double newStep = std::pow (2.0, semis0 / 12.0) * sampleRate / rate;
                const double gms = glideP->get();
                const double glc = gms > 0.5 ? 1.0 - std::exp (-1.0 / (jmax (1.0, gms * 0.001 * sampleRate / 3.0))) : 1.0;   // ~95 % za cas Glide
                if (monoP->get() > 0.5f)
                {
                    Voice* held = nullptr;
                    for (auto& x : voices) if (x.on && ! x.releasing && (held == nullptr || x.age > held->age)) held = &x;
                    if (held != nullptr)
                    {   // legato: ten isty ton sa len presunie na novu vysku (bez noveho nabehu)
                        for (auto& x : voices) if (&x != held && x.on) x.releasing = true;
                        held->note = m.getNoteNumber(); held->target = newStep; held->gl = glc; if (glc >= 1.0) held->step = newStep;
                        held->age = ++counter; lastStep = newStep;
                        continue;
                    }
                    for (auto& x : voices) if (x.on) x.releasing = true;
                }
                Voice* v = nullptr;
                for (auto& x : voices) if (! x.on) { v = &x; break; }
                if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
                const double semis = m.getNoteNumber() - root->get() + tune->get() / 100.0;
                v->on = true; v->releasing = false; v->note = m.getNoteNumber(); v->pos = 0; v->env = 0;
                v->scanPh = 0; v->scanStep = -1; v->inLoop = false; v->ph = 0; v->o0 = v->o1 = 0; v->xw = 1.0f; v->tailOn = false; v->outLoop = false;
                if (loopOn && fromP->get() < 0.5f) { v->pos = lsS; v->inLoop = true; }   // hned v slucke
                v->step = std::pow (2.0, semis / 12.0) * sampleRate / rate;
                v->target = v->step; v->gl = glc;
                if (glc < 1.0 && lastStep > 0.0) v->step = lastStep;   // glide z vysky predchadzajuceho tonu
                lastStep = v->target;
                v->gain = g * (1.0f - vs + vs * m.getFloatVelocity());
                v->age = ++counter;
            }
            else if (m.isNoteOff())
            {
                for (auto& x : voices) if (x.on && ! x.releasing && x.note == m.getNoteNumber())
                {
                    if (! whole) x.releasing = true;
                    else if (loopOn) x.outLoop = true;
                }
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& x : voices) x.releasing = true;
        }
        render (pos, n);
        lastActive = 0;
        const Voice* newest = nullptr;
        for (auto& v : voices) if (v.on) { ++lastActive; if (! v.releasing && (newest == nullptr || v.age > newest->age)) newest = &v; }   // ukazovatel len pre drzany ton
        playFrac.store (newest != nullptr ? (float) (newest->pos / (double) jmax (1, sl)) : -1.0f);
    }

    int lastActive = 0;
    std::atomic<float> playFrac { -1.0f };   // kde v zvuku prave hra najnovsi ton (ukazovatel v okne)

private:
    void prepare() override { for (auto& v : voices) v.on = false; }
    void process (AudioBuffer<float>&) override {}

    struct Voice { bool on = false, releasing = false; int note = 60; double pos = 0, step = 1, target = 1, gl = 1, scanPh = 0, ph = 0, o0 = 0, o1 = 0; float env = 0, gain = 1, scanRnd = 0, xw = 1.0f; int scanStep = -1; bool inLoop = false, outLoop = false; uint32 age = 0;
                   bool tailOn = false; double tailPos = 0, tailLeft = 0, tailLen = 1; };
    Random rng;
    std::array<Voice, 24> voices;
    uint32 counter = 0;
    FxParam *root, *tune, *att, *rel, *velS, *vol, *mode, *glideP, *monoP;
    double lastStep = 0;   // vyska posledneho tonu (odkial ide glide)
    std::shared_ptr<AudioBuffer<float>> sample;
    double sampleRate = 44100;
    MemoryBlock fileData;
    mutable SpinLock lock;
};

Rectangle<float> SamplerDisplay::fadeBox (bool out) const
{
    const float xs = xOf (sm.lsP->get()), xe = xOf (sm.leP->get()), L = xe - xs;
    const float x = out ? xe - sm.lfoP->get() * L : xs + sm.lfiP->get() * L;
    const float cx = out ? jmin (x, xe - 9.0f) : jmax (x, xs + 9.0f);
    return { cx - 4.5f, waveArea().getY() + 9.0f, 9.0f, 9.0f };
}

SamplerDisplay::SamplerDisplay (SamplerInst& s) : sm (s)
{
    const Colour text (0xffe4e9ef), steel (0xff2d4362), off (0xff1f2833);
    load.setButtonText (sk ("Načítať zvuk…"));
    load.setColour (TextButton::buttonColourId, steel);
    load.setColour (TextButton::textColourOffId, text);
    load.onClick = [this] { choose(); };
    addAndMakeVisible (load);
    auto tog = [&] (TextButton& b, const char* t, FxParam* prm)
    {
        b.setButtonText (sk (t));
        b.setClickingTogglesState (true);
        b.setColour (TextButton::buttonColourId, off);
        b.setColour (TextButton::buttonOnColourId, steel);
        b.setColour (TextButton::textColourOffId, Colour (0xff9aa6b4));
        b.setColour (TextButton::textColourOnId, text);
        b.onClick = [&b, prm] { prm->setFromUi (b.getToggleState() ? 1.0f : 0.0f); };
        addAndMakeVisible (b);
    };
    tog (loopB, "Slučka", sm.loopP);
    tog (fromB, "Od začiatku", sm.fromP);
    fromB.setTooltip (sk ("Zapnuté: tón hrá zvuk od začiatku a potom v slučke. Vypnuté: tón hrá hneď v slučke."));
    tog (scanB, "≋ Skenovať", sm.scanP);
    scanB.onClick = [this]
    {
        sm.scanP->setFromUi (scanB.getToggleState() ? 1.0f : 0.0f);
        if (scanB.getToggleState() && sm.loopP->get() < 0.5f) sm.loopP->setFromUi (1.0f);   // skenovanie potrebuje slucku
    };
    auto bar = [&] (Slider& k, FxParam* prm)
    {
        k.setRange (prm->range.start, prm->range.end, 0.0);
        k.setSkewFactor (prm->range.skew);
        k.setValue (prm->get(), dontSendNotification);
        k.setDoubleClickReturnValue (true, prm->range.convertFrom0to1 (prm->defNorm));
        k.textFromValueFunction = [prm] (double v) { return prm->getText (prm->range.convertTo0to1 ((float) v), 0); };
        k.setColour (Slider::trackColourId, steel);
        k.setColour (Slider::backgroundColourId, off);
        k.setColour (Slider::textBoxTextColourId, text);
        k.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        k.onDragStart = [prm] { prm->beginChangeGesture(); };
        k.onDragEnd   = [prm] { prm->endChangeGesture(); };
        k.onValueChange = [prm, &k] { prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) k.getValue())); };
        addAndMakeVisible (k);
        k.updateText();
    };
    bar (rateS, sm.sRate);
    bar (rangeS, sm.sRange);
    for (int i = 0; i < sm.sShape->choices.size(); ++i) shapeC.addItem (sm.sShape->choices[i], i + 1);
    shapeC.setSelectedId (roundToInt (sm.sShape->get()) + 1, dontSendNotification);
    shapeC.setColour (ComboBox::backgroundColourId, off);
    shapeC.setColour (ComboBox::textColourId, text);
    shapeC.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    shapeC.onChange = [this] { sm.sShape->setFromUi ((float) (shapeC.getSelectedId() - 1)); };
    addAndMakeVisible (shapeC);
    help.setColour (TextButton::buttonColourId, off);
    help.setColour (TextButton::textColourOffId, text);
    help.onClick = [] {
        AlertWindow::showMessageBoxAsync (MessageBoxIconType::NoIcon, sk ("Slučka a skenovanie"),
            sk ("Slučka: zapni ju a na obrázku vyber časť zvuku, ktorá sa bude opakovať, kým držíš klávesu. "
                "Ťahaním okrajov meníš začiatok a koniec, ťahaním stredu posúvaš celú slučku, kliknutím mimo nej vyberieš novú. "
                "So Shiftom sa ťahá jemne (na veľmi krátku slučku). Veľmi krátka slučka dáva bzučivý tón.\n\n"
                "Biele štvorčeky hore v slučke: ľavým ťahaním nastavíš nábeh, pravým stíšenie na konci slučky. Keď nastavíš oba, slučka sa plynulo prelína.\n\n"
                "Od začiatku (so slučkou): zapnuté = tón zahrá zvuk od začiatku a potom cykluje, vypnuté = hrá hneď v slučke.\n\n"
                "Hrať: Kým držím klávesu = pustením klávesy tón dozneje (Uvoľnenie). Celý zvuk = pustenie klávesy tón nezastaví; "
                "so slučkou ju opustí a dohrá zvuk až do konca.\n\n"
                "Glide: tón sa plynulo presunie z výšky predchádzajúceho (napr. 808 basy). Mono: hrá jeden tón naraz, "
                "pri hraní legato sa len posunie výška bez nového nábehu.\n\n"
                "Zoom: koliesko myši priblíži zvuk okolo kurzora, Shift + koliesko ho posúva, dvojklik ukáže celý zvuk.\n\n"
                "Skenovať: slučka sa sama pomaly posúva po zvuku, takže sa mení farba zvuku, ale výška ostáva podľa tónu. "
                "Rýchlosť = ako rýchlo sa posúva (pomaly výška presne drží tón, rýchlo je pohyb divokejší), Rozsah = kam až za slučku zájde (zlaté pole), "
                "Tvar = tam a späť, stále dopredu alebo náhodne."));
    };
    addAndMakeVisible (help);
    timerCallback();
    startTimerHz (30);   // aj ukazovatel prehravania
}

void SamplerDisplay::resized()
{
    // bez zvuku je tlacidlo v strede plochy na zvuk, so zvukom hore v strede (vpravo hore je ikona velkosti okna)
    hadSample = sm.getSample() != nullptr;
    if (hadSample) load.setBounds (getWidth() / 2 - 62, 6, 124, 24);
    else load.setBounds (waveArea().toNearestInt().withSizeKeepingCentre (150, 28).translated (0, -12));
    fromB.setBounds (load.getRight() + 10, 6, 104, 24);
    auto row = getLocalBounds().reduced (10, 0).removeFromBottom (36).withTrimmedBottom (8);
    loopB.setBounds (row.removeFromLeft (74)); row.removeFromLeft (6);
    scanB.setBounds (row.removeFromLeft (100));
    help.setBounds (row.removeFromRight (28)); row.removeFromRight (6);
    shapeC.setBounds (row.removeFromRight (112)); row.removeFromRight (48);   // miesto na "Tvar"
    row.removeFromLeft (62);                                                 // miesto na "Rychlost"
    const int w = (row.getWidth() - 54) / 2;
    rateS.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (54);                                                 // miesto na "Rozsah"
    rangeS.setBounds (row.removeFromLeft (w));
}

void SamplerDisplay::timerCallback()
{
    if (hadSample != (sm.getSample() != nullptr)) resized();
    loopB.setToggleState (sm.loopP->get() > 0.5f, dontSendNotification);
    fromB.setToggleState (sm.fromP->get() > 0.5f, dontSendNotification);
    fromB.setVisible (sm.getSample() != nullptr && sm.loopP->get() > 0.5f);
    scanB.setToggleState (sm.scanP->get() > 0.5f, dontSendNotification);
    if (! rateS.isMouseButtonDown()) rateS.setValue (sm.sRate->get(), dontSendNotification);
    if (! rangeS.isMouseButtonDown()) rangeS.setValue (sm.sRange->get(), dontSendNotification);
    shapeC.setSelectedId (roundToInt (sm.sShape->get()) + 1, dontSendNotification);
    const bool sc = sm.scanP->get() > 0.5f;
    rateS.setEnabled (sc); rangeS.setEnabled (sc); shapeC.setEnabled (sc);
    repaint();
}

int SamplerDisplay::hitEdge (float x, float fadeY) const
{
    if (sm.loopP->get() < 0.5f || sm.getSample() == nullptr) return 0;
    const float xs = xOf (sm.lsP->get()), xe = xOf (sm.leP->get());
    if (fadeY >= 0.0f)
    {
        if (fadeBox (true).expanded (3.0f).contains (x, fadeY)) return 7;
        if (fadeBox (false).expanded (3.0f).contains (x, fadeY)) return 6;
    }
    if (std::abs (x - xe) <= 6.0f) return 2;
    if (std::abs (x - xs) <= 6.0f) return 1;
    if (x > xs && x < xe) return 3;
    return 0;
}

void SamplerDisplay::mouseMove (const MouseEvent& e)
{
    const int h = waveArea().contains (e.position) ? hitEdge (e.position.x, e.position.y) : 0;
    setMouseCursor (h == 1 || h == 2 || h == 6 || h == 7 ? MouseCursor::LeftRightResizeCursor : h == 3 ? MouseCursor::DraggingHandCursor : MouseCursor::NormalCursor);
}

void SamplerDisplay::mouseDown (const MouseEvent& e)
{
    if (sm.getSample() == nullptr) { choose(); return; }
    const auto a = waveArea();
    dragMode = 0; panFrom = v0;
    if (! a.contains (e.position)) return;
    if (sm.loopP->get() < 0.5f) { dragMode = 5; return; }   // bez slucky tahanie posuva pohlad
    dLs = sm.lsP->get(); dLe = sm.leP->get(); dFi = sm.lfiP->get(); dFo = sm.lfoP->get();
    dragMode = hitEdge (e.position.x, e.position.y);
    if (dragMode == 6 || dragMode == 7) { (dragMode == 6 ? sm.lfiP : sm.lfoP)->beginChangeGesture(); return; }
    if (dragMode == 0)
    {   // nova slucka od miesta kliknutia
        dLs = dLe = fOf (e.position.x);
        dragMode = 4;
    }
    sm.lsP->beginChangeGesture(); sm.leP->beginChangeGesture();
}

void SamplerDisplay::mouseDrag (const MouseEvent& e)
{
    if (dragMode == 0) return;
    if (dragMode == 5)
    {
        const float span = v1 - v0;
        v0 = jlimit (0.0f, 1.0f - span, panFrom - (float) e.getDistanceFromDragStartX() / pxPerF()); v1 = v0 + span;
        repaint(); return;
    }
    if (dragMode == 6 || dragMode == 7)
    {   // nabeh / stisenie ako cast dlzky slucky (spolu najviac cela slucka)
        const float L = jmax (1.0f, xOf (sm.leP->get()) - xOf (sm.lsP->get()));
        float d = (float) e.getDistanceFromDragStartX() / L;
        if (e.mods.isShiftDown()) d *= 0.1f;
        if (dragMode == 6) sm.lfiP->setFromUi (jlimit (0.0f, 0.5f, dFi + d));
        else sm.lfoP->setFromUi (jlimit (0.0f, 0.5f, dFo - d));
        repaint(); return;
    }
    const float minGap = 0.00005f;
    float d = (float) e.getDistanceFromDragStartX() / pxPerF();
    if (e.mods.isShiftDown()) d *= 0.03f;          // jemne tahanie
    float ls = dLs, le = dLe;
    if (dragMode == 1) ls = jlimit (0.0f, le - minGap, dLs + d);
    else if (dragMode == 2) le = jlimit (ls + minGap, 1.0f, dLe + d);
    else if (dragMode == 3) { const float len = dLe - dLs; ls = jlimit (0.0f, 1.0f - len, dLs + d); le = ls + len; }
    else
    {
        const float x = jlimit (0.0f, 1.0f, dLs + d);
        ls = jmin (dLs, x); le = jmax (dLs, x);
        if (le - ls < minGap) le = jmin (1.0f, ls + minGap);
    }
    sm.lsP->setFromUi (ls); sm.leP->setFromUi (le);
    repaint();
}

void SamplerDisplay::mouseWheelMove (const MouseEvent& e, const MouseWheelDetails& w)
{
    auto smp = sm.getSample();
    if (smp == nullptr) return;
    const float span = v1 - v0;
    if (e.mods.isShiftDown() || std::abs (w.deltaX) > std::abs (w.deltaY))
    {   // posun pohladu
        const float d = (std::abs (w.deltaX) > std::abs (w.deltaY) ? -w.deltaX : -w.deltaY) * span * 0.5f;
        v0 = jlimit (0.0f, 1.0f - span, v0 + d); v1 = v0 + span;
    }
    else
    {   // zoom okolo kurzora
        const float at = fOf (e.position.x), minSpan = jmin (1.0f, 64.0f / (float) smp->getNumSamples());
        const float ns = jlimit (minSpan, 1.0f, span * std::pow (0.8f, w.deltaY * 4.0f));
        const float rel = (at - v0) / span;
        v0 = jlimit (0.0f, 1.0f - ns, at - rel * ns); v1 = v0 + ns;
    }
    repaint();
}

void SamplerDisplay::mouseUp (const MouseEvent&)
{
    if (dragMode == 6 || dragMode == 7) (dragMode == 6 ? sm.lfiP : sm.lfoP)->endChangeGesture();
    else if (dragMode != 0 && dragMode != 5) { sm.lsP->endChangeGesture(); sm.leP->endChangeGesture(); }
    dragMode = 0;
}

void SamplerDisplay::choose()
{
    if (BuiltinFx::requestFile)
    {
        SafePointer<SamplerDisplay> sp (this);
        BuiltinFx::requestFile ([sp] (const File& f)
        {
            if (sp == nullptr || ! f.existsAsFile()) return;
            String err;
            if (! sp->sm.loadFile (f, err)) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Korgus Sampler", err);
            sp->repaint();
            if (auto* t = sp->getTopLevelComponent()) t->toFront (true);
        });
        return;
    }
    chooser = std::make_unique<FileChooser> (sk ("Vyber zvuk pre sampler"), File (sm.filePath).existsAsFile() ? File (sm.filePath) : File::getSpecialLocation (File::userMusicDirectory),
                                             "*.wav;*.aif;*.aiff;*.flac;*.ogg;*.mp3");
    SafePointer<SamplerDisplay> sp (this);
    chooser->launchAsync (FileBrowserComponent::openMode | FileBrowserComponent::canSelectFiles, [sp] (const FileChooser& fc)
    {
        if (sp == nullptr || fc.getResult() == File()) return;
        String err;
        if (! sp->sm.loadFile (fc.getResult(), err)) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Korgus Sampler", err);
        sp->repaint();
    });
}

bool SamplerDisplay::isInterestedInFileDrag (const StringArray& f)
{
    return f.size() == 1 && File (f[0]).hasFileExtension ("wav;aif;aiff;flac;ogg;mp3");
}

void SamplerDisplay::filesDropped (const StringArray& f, int, int)
{
    String err;
    if (! sm.loadFile (File (f[0]), err)) AlertWindow::showMessageBoxAsync (MessageBoxIconType::WarningIcon, "Korgus Sampler", err);
    repaint();
}


void SamplerDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
    g.setFont (FontOptions (12.0f));
    g.setColour (Colour (0xff8a96a6));
    g.drawText ("SAMPLER", r.reduced (10, 6).removeFromTop (18), Justification::centredLeft);
    auto smp = sm.getSample();
    auto area = waveArea();
    // popisy posuvnikov v spodnom riadku
    g.setColour (Colour (0xffc9d2dc));
    g.setFont (FontOptions (13.0f));
    g.drawText (sk ("Rýchlosť"), rateS.getBounds().translated (-62, 0).withWidth (58), Justification::centredRight);
    g.drawText (sk ("Rozsah"), rangeS.getBounds().translated (-54, 0).withWidth (50), Justification::centredRight);
    g.drawText (sk ("Tvar"), shapeC.getBounds().translated (-48, 0).withWidth (44), Justification::centredRight);
    if (smp == nullptr)
    {
        g.setColour (Colour (0xff8a96a6));
        g.drawText (sk ("alebo sem pretiahni zvuk (wav, mp3…)"), area.withTrimmedTop (area.getHeight() * 0.5f + 8.0f).withHeight (20.0f), Justification::centredTop);
        return;
    }
    g.setFont (FontOptions (12.0f));
    g.drawText (sm.fileName, r.reduced (10, 6).removeFromTop (18).withTrimmedLeft (70).withRight ((float) load.getX() - 8.0f), Justification::centredLeft);
    const bool loopOn = sm.loopP->get() > 0.5f;
    const float xs = xOf (sm.lsP->get()), xe = xOf (sm.leP->get());
    Graphics::ScopedSaveState clipSt (g);
    g.reduceClipRegion (area.toNearestInt());
    if (loopOn)
    {
        if (sm.scanP->get() > 0.5f)
        {   // kam az skenovanie zajde
            const float room = (xOf (1.0f) - xe) * sm.sRange->get() * 0.01f;
            g.setColour (Colour (0x28c8a04a));
            g.fillRect (Rectangle<float> (xe, area.getY(), room, area.getHeight()));
        }
        g.setColour (Colour (0x402d4362).brighter (0.6f));
        g.fillRect (Rectangle<float> (xs, area.getY(), jmax (1.0f, xe - xs), area.getHeight()));
    }
    const int W = jmax (2, (int) area.getWidth()), n = smp->getNumSamples();
    const float* d = smp->getReadPointer (0);
    g.setColour (Colour (0xff6f8fb3));
    const float mid = area.getCentreY(), hh = area.getHeight() * 0.5f;
    for (int x = 0; x < W; ++x)
    {
        const int a = jlimit (0, n - 1, (int) ((v0 + (float) x / pxPerF()) * n));
        const int b = jlimit (a + 1, n, (int) ((v0 + (float) (x + 1) / pxPerF()) * n) + 1);
        float mn = 0, mx = 0;
        for (int i = a; i < b; i += jmax (1, (b - a) / 64)) { mn = jmin (mn, d[i]); mx = jmax (mx, d[i]); }
        g.drawVerticalLine ((int) area.getX() + x, mid - mx * hh, mid - mn * hh + 1.0f);
    }
    if (loopOn)
    {
        g.setColour (Colour (0xffd8c27a));
        g.fillRect (Rectangle<float> (xs - 1.0f, area.getY(), 2.0f, area.getHeight()));
        g.fillRect (Rectangle<float> (xe - 1.0f, area.getY(), 2.0f, area.getHeight()));
        g.fillRect (Rectangle<float> (xs - 4.0f, area.getY(), 8.0f, 6.0f));
        g.fillRect (Rectangle<float> (xe - 4.0f, area.getY(), 8.0f, 6.0f));
        // nabeh a stisenie: ciary ako pri klipe a stvorceky na tahanie
        const float L = xe - xs, fi = sm.lfiP->get() * L, fo = sm.lfoP->get() * L, y0 = area.getY() + 13.5f, y1 = area.getBottom() - 1.0f;
        g.setColour (Colour (0xffe4e9ef));
        if (fi > 0.5f) g.drawLine (xs, y1, xs + fi, y0, 1.2f);
        if (fo > 0.5f) g.drawLine (xe, y1, xe - fo, y0, 1.2f);
        g.fillRect (fadeBox (false));
        g.fillRect (fadeBox (true));
    }
    // ukazovatel prehravania: kde v zvuku prave hra drzany ton
    if (const float ph = sm.playFrac.load(); ph >= 0.0f && ph >= v0 && ph <= v1)
    {
        g.setColour (Colours::white.withAlpha (0.85f));
        g.fillRect (Rectangle<float> (xOf (ph) - 0.75f, area.getY(), 1.5f, area.getHeight()));
    }
}



//==============================================================================
// Korgus Dvojhlas - zdvojenie spevu (rozladene kopie) a harmonizer (druhy hlas o interval)
struct DelayShifter
{
    // dve citacie hlavy s prelinanim; pri vymene sa nova hlava zarovna podla podobnosti priebehu (menej "kvakania")
    void prepare (double sr) { n = jmax (256, (int) (sr * 0.040)); buf.assign ((size_t) n * 3 + 8, 0.0f); w = 0; ph = 0; d[0] = 2.0 + n; d[1] = 2.0 + n * 0.5; }
    float tap (double dl) const
    {
        const int sz = (int) buf.size();
        double rp = (double) w - dl;
        while (rp < 0) rp += sz;
        const int i0 = (int) rp % sz, i1 = (i0 + 1) % sz;
        const float fr = (float) (rp - std::floor (rp));
        return buf[(size_t) i0] + (buf[(size_t) i1] - buf[(size_t) i0]) * fr;
    }
    void resync (int which, double ratio)
    {
        const double nominal = ratio > 1.0 ? 2.0 + n : 2.0;
        const double other = d[1 - which];
        const int L = jmin (256, n / 4), range = n / 3;
        double best = -1.0e30, bestD = nominal;
        for (int k = 0; k <= range; k += 2)
        {
            const double cand = ratio > 1.0 ? nominal - k : nominal + k;
            if (cand < 2.0 || cand > (double) buf.size() - L - 4) continue;
            double c = 0, e = 1.0e-9;
            for (int j = 0; j < L; j += 2) { const float a = tap (cand + j), o = tap (other + j); c += a * o; e += a * a; }
            const double score = c / std::sqrt (e);
            if (score > best) { best = score; bestD = cand; }
        }
        d[which] = bestD;
    }
    float process (float x, double ratio)
    {
        buf[(size_t) w] = x;
        const double step = (1.0 - ratio) / n;
        const double old = ph;
        ph += step;
        if (ph >= 1.0) { ph -= 1.0; resync (0, ratio); }
        else if (ph < 0.0) { ph += 1.0; resync (0, ratio); }
        if ((old < 0.5) != (ph < 0.5) && std::abs (ph - old) < 0.5) resync (1, ratio);
        const float g1 = (float) std::pow (std::sin (MathConstants<double>::pi * ph), 2.0), g2 = 1.0f - g1;
        const float y = tap (d[0]) * g1 + tap (d[1]) * g2;
        for (auto& dd : d) dd = jlimit (2.0, (double) buf.size() - 4, dd + (1.0 - ratio));
        if (++w >= (int) buf.size()) w = 0;
        return y;
    }
    std::vector<float> buf;
    int n = 1024, w = 0;
    double ph = 0, d[2] { 0, 0 };
};

class HarmonyFx final : public FxBase
{
public:
    HarmonyFx() : FxBase ("Korgus Dvojhlas", "harmony")
    {
        mode = add ("mode", "Režim", 0, 2, 0, "", 1.0f, 0);
        mode->range.interval = 1.0f;
        mode->choices = StringArray::fromTokens (sk ("Dvojhlas|Harmonizér|Oboje"), "|", "");
        voices = add ("voices", "Hlasy", 0, 2, 1, "", 1.0f, 0);
        voices->range.interval = 1.0f;
        voices->choices = StringArray::fromTokens (sk ("2 hlasy|4 hlasy|6 hlasov"), "|", "");
        interval = add ("interval", "Interval", 0, 7, 4, "", 1.0f, 0);
        interval->range.interval = 1.0f;
        interval->choices = StringArray::fromTokens (sk ("−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva"), "|", "");
        interval2 = add ("interval2", "Interval 2", 0, 8, 0, "", 1.0f, 0);
        interval2->range.interval = 1.0f;
        interval2->choices = StringArray::fromTokens (sk ("žiadny|−12 oktáva dole|−7 kvinta dole|−5 kvarta dole|+3 malá terca|+4 veľká terca|+5 kvarta|+7 kvinta|+12 oktáva"), "|", "");
        detune = add ("detune", "Rozladenie", 0, 40, 14, "cent", 1.0f, 0);
        delayMs = add ("delay", "Oneskorenie", 5, 60, 25, "ms", 1.0f, 0);
        width = add ("width", "Šírka", 0, 100, 100, "%", 1.0f, 0);
        mix = add ("mix", "Mix", 0, 100, 50, "%", 1.0f, 0);
    }

private:
    static constexpr int maxV = 6;
    void prepare() override
    {
        for (auto& d : dv) d.prepare (rate);
        h1.prepare (rate); h2.prepare (rate);
        const int maxD = (int) (rate * 0.12) + 4;
        for (auto& l : line) l.assign ((size_t) maxD, 0.0f);
        lw = 0;
        for (int v = 0; v < maxV; ++v) lfo[v] = v * 0.37;
        hpL = hpR = hpX = 0;
    }

    void process (AudioBuffer<float>& b) override
    {
        static const int semis[8] = { -12, -7, -5, 3, 4, 5, 7, 12 };
        // hlasy: rozladenie (nasobok), oneskorenie (nasobok), panorama
        static const float vDet[maxV] = { 1.0f, -1.0f, 0.55f, -0.6f, 0.3f, -0.35f };
        static const float vDel[maxV] = { 1.0f, 1.31f, 0.62f, 0.83f, 1.55f, 0.45f };
        static const float vPan[maxV] = { -1.0f, 1.0f, -0.55f, 0.6f, -0.25f, 0.3f };
        const int md = roundToInt (mode->get());
        const bool dbl = md != 1, har = md != 0;
        const int nv = 2 + 2 * jlimit (0, 2, roundToInt (voices->get()));
        const double cents = detune->get();
        const double rH1 = std::pow (2.0, semis[jlimit (0, 7, roundToInt (interval->get()))] / 12.0);
        const int i2 = roundToInt (interval2->get());
        const double rH2 = i2 > 0 ? std::pow (2.0, semis[jlimit (0, 7, i2 - 1)] / 12.0) : 0.0;
        const float wd = width->get() * 0.01f, m = mix->get() * 0.01f;
        const float dryG = jlimit (0.0f, 1.0f, 2.0f * (1.0f - m)), wetG = jlimit (0.0f, 1.0f, 2.0f * m);
        const int sz = (int) line[0].size();
        const double baseDel = delayMs->get() * 0.001 * rate;
        const double lfoInc = 2.0 * MathConstants<double>::pi / rate;
        const float hpA = (float) std::exp (-2.0 * MathConstants<double>::pi * 140.0 / rate);   // basy z hlasov prec
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        float* L = b.getWritePointer (0);
        float* R = ch > 1 ? b.getWritePointer (1) : nullptr;
        const float voiceNorm = 1.1f / std::sqrt ((float) nv);
        for (int i = 0; i < n; ++i)
        {
            const float x0 = R != nullptr ? 0.5f * (L[i] + R[i]) : L[i];
            hpX = x0 + (hpX - x0) * hpA;
            const float x = x0 - hpX;
            float wl = 0, wr = 0;
            if (dbl)
                for (int v = 0; v < nv; ++v)
                {
                    lfo[v] += lfoInc * (0.13 + 0.07 * v);
                    const double c = cents * vDet[v] * (0.75 + 0.25 * std::sin (lfo[v]));
                    const float y = dv[v].process (x, std::pow (2.0, c / 1200.0));
                    line[(size_t) v][(size_t) lw] = y;
                    const int dd = jlimit (1, sz - 1, (int) (baseDel * vDel[v] + 0.0015 * rate * std::sin (lfo[v] * 0.7)));
                    const float out = line[(size_t) v][(size_t) ((lw - dd + sz) % sz)] * voiceNorm;
                    const float pan = vPan[v] * wd;
                    wl += out * (0.5f - 0.5f * pan) * 1.41f;
                    wr += out * (0.5f + 0.5f * pan) * 1.41f;
                }
            if (har)
            {
                const float y1 = h1.process (x0, rH1) * 0.85f;
                wl += y1 * (0.5f + 0.25f * wd); wr += y1 * (0.5f - 0.25f * wd);
                if (rH2 > 0)
                {
                    const float y2 = h2.process (x0, rH2) * 0.75f;
                    wl += y2 * (0.5f - 0.25f * wd); wr += y2 * (0.5f + 0.25f * wd);
                }
            }
            if (++lw >= sz) lw = 0;
            const float dl = L[i], dr = R != nullptr ? R[i] : L[i];
            L[i] = dl * dryG + wl * wetG;
            if (R != nullptr) R[i] = dr * dryG + wr * wetG;
        }
        for (auto& f : lfo) if (f > 1.0e6) f = std::fmod (f, 2.0 * MathConstants<double>::pi);
    }

    FxParam *mode, *voices, *interval, *interval2, *detune, *delayMs, *width, *mix;
    DelayShifter dv[maxV], h1, h2;
    std::vector<float> line[maxV];
    double lfo[maxV] {};
    int lw = 0;
    float hpL = 0, hpR = 0, hpX = 0;
};

//==============================================================================
// Korgus Arp - MIDI arpeggiator so 16-krokovym vzorom (sila, pauzy, oktava na krok)
class ArpFx;

class ArpGrid final : public Component, private Timer
{
public:
    explicit ArpGrid (ArpFx& a) : arp (a) { startTimerHz (30); }
    ~ArpGrid() override { stopTimer(); }
    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseDrag (const MouseEvent& e) override;
private:
    void timerCallback() override { repaint(); }
    Rectangle<float> velArea() const { return getLocalBounds().toFloat().reduced (8, 0).withTrimmedTop (22).withTrimmedBottom (34); }
    Rectangle<float> octArea() const { return getLocalBounds().toFloat().reduced (8, 0).removeFromBottom (28).withTrimmedBottom (4); }
    int stepAt (float x) const { const auto a = velArea(); return jlimit (0, 15, (int) ((x - a.getX()) / (a.getWidth() / 16.0f))); }
    ArpFx& arp;
    int dragMode = 0;   // 1 = sila, 2 = vypnut, 3 = zapnut
};

class ArpFx final : public FxBase
{
public:
    ArpFx() : FxBase ("Korgus Arp", "arp")
    {
        rateP = add ("rate", "Rýchlosť", 0, 6, 4, "", 1.0f, 0);
        rateP->range.interval = 1.0f;
        rateP->choices = StringArray::fromTokens (sk ("1/4|1/8|1/8 triola|1/8 bodka|1/16|1/16 triola|1/32"), "|", "");
        modeP = add ("mode", "Smer", 0, 6, 0, "", 1.0f, 0);
        modeP->range.interval = 1.0f;
        modeP->choices = StringArray::fromTokens (sk ("Hore|Dole|Hore-dole|Dole-hore|Náhodne|Ako hráš|Akord"), "|", "");
        octP = add ("octaves", "Oktávy", 1, 4, 1, "", 1.0f, 0);
        octP->range.interval = 1.0f;
        octP->choices = StringArray ("", "1", "2", "3", "4");
        lenP = add ("length", "Kroky", 1, 16, 16, "", 1.0f, 0);
        lenP->range.interval = 1.0f;
        for (int i = 0; i <= 16; ++i) lenP->choices.add (i == 0 ? String() : String (i));
        gateP = add ("gate", "Dĺžka tónu", 5, 100, 60, "%", 1.0f, 0);
        swingP = add ("swing", "Swing", 0, 75, 0, "%", 1.0f, 0);
        velP = add ("velmode", "Sila", 0, 2, 2, "", 1.0f, 0);
        velP->range.interval = 1.0f;
        velP->choices = StringArray::fromTokens (sk ("Z hry|Zo vzoru|Kombinácia"), "|", "");
        latchP = add ("latch", "Podržať", 0, 1, 0, "", 1.0f, 0);
        latchP->toggle = true;
        patP = add ("pattern", "Vzor", 0, 1, 1, "", 1.0f, 0);
        patP->toggle = true;
        for (int i = 0; i < 16; ++i) { stepVel[i] = (i % 4 == 0) ? 120 : (i % 2 == 0 ? 95 : 80); stepOn[i] = true; stepOct[i] = 0; }
    }

    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return true; }
    bool isMidiEffect() const override { return true; }
    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<ArpGrid> (*this); }
    int displayHeight() const override { return 190; }

    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX");
        x.setAttribute ("type", "arp");
        for (auto* p : knobs) x.setAttribute (p->id, (double) p->get());
        String v, o, a;
        for (int i = 0; i < 16; ++i) { v << stepVel[i].load() << " "; o << stepOct[i].load() << " "; a << (stepOn[i].load() ? 1 : 0) << " "; }
        x.setAttribute ("stepvel", v.trim()); x.setAttribute ("stepoct", o.trim()); x.setAttribute ("stepon", a.trim());
        x.setAttribute ("preset", presetIdx.load());
        copyXmlToBinary (x, mb);
    }
    void setStateInformation (const void* data, int size) override
    {
        FxBase::setStateInformation (data, size);
        if (auto x = getXmlFromBinary (data, size))
        {
            auto v = StringArray::fromTokens (x->getStringAttribute ("stepvel"), " ", ""), o = StringArray::fromTokens (x->getStringAttribute ("stepoct"), " ", ""),
                 a = StringArray::fromTokens (x->getStringAttribute ("stepon"), " ", "");
            for (int i = 0; i < 16; ++i)
            {
                if (i < v.size()) stepVel[i] = jlimit (1, 127, v[i].getIntValue());
                if (i < o.size()) stepOct[i] = jlimit (-1, 1, o[i].getIntValue());
                if (i < a.size()) stepOn[i] = a[i].getIntValue() != 0;
            }
            presetIdx = x->getIntAttribute ("preset", -1);
        }
    }

    void processBlock (AudioBuffer<float>& buf, MidiBuffer& midi) override
    {
        out.clear();
        double bpm = 120, ppq = 0; bool playing = false;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto b = pos->getBpm()) bpm = jlimit (20.0, 400.0, *b);
                playing = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition()) ppq = *q;
            }
        static const double rates[7] = { 4.0, 2.0, 4.0 / 3.0, 3.0, 1.0, 2.0 / 3.0, 0.5 };
        const double rs = rates[jlimit (0, 6, roundToInt (rateP->get()))];
        const double sps = bpm * 4.0 / 60.0 / jmax (1.0, rate);
        const bool usePat = patP->get() > 0.5f;
        const int len = usePat ? jlimit (1, 16, roundToInt (lenP->get())) : 16;
        const int mode = roundToInt (modeP->get()), octs = jlimit (1, 4, roundToInt (octP->get())), velMode = roundToInt (velP->get());
        const bool latch = latchP->get() > 0.5f;
        const double swing = swingP->get() * 0.01;
        const int ch = lastCh;
        if (playing) clock = ppq * 4.0;
        const int numSamples = jmax (1, buf.getNumSamples());

        auto offAll = [&] (int at) { for (int p : sounding) out.addEvent (MidiMessage::noteOff (ch, p), jlimit (0, numSamples - 1, at)); sounding.clear(); offAt = -1; };
        auto tick = [&] (int at, long long k)
        {
            const int stepPos = (int) (((k % len) + len) % len);
            curStep = stepPos;
            if (held.empty() && ! (latch && ! latched.empty())) return;
            const auto& src = held.empty() ? latched : held;
            offAll (at);
            if (usePat && ! stepOn[stepPos].load()) return;
            std::vector<std::pair<int, int>> base (src);
            if (mode != 5) std::sort (base.begin(), base.end());
            seq.clear();
            for (int o = 0; o < octs; ++o) for (const auto& h : base) seq.push_back ({ h.first + 12 * o, h.second });
            const int m = (int) seq.size();
            if (m == 0) return;
            const int shift = usePat ? 12 * stepOct[stepPos].load() : 0;
            auto velFor = [&] (int played) { const int pv = stepVel[stepPos].load(); return jlimit (1, 127, (velMode == 0 || ! usePat) ? played : velMode == 1 ? pv : played * pv / 110); };
            auto play = [&] (int pitch, int v) { const int p = jlimit (0, 127, pitch + shift); out.addEvent (MidiMessage::noteOn (ch, p, (uint8) velFor (v)), at); sounding.push_back (p); };
            if (mode == 6) { for (const auto& h : seq) play (h.first, h.second); }
            else
            {
                const int i = idx++;
                int pick = 0;
                switch (mode)
                {
                    case 1: pick = m - 1 - (i % m); break;
                    case 2: { const int per = jmax (1, 2 * m - 2); const int q = i % per; pick = m == 1 ? 0 : (q < m ? q : per - q); break; }
                    case 3: { const int per = jmax (1, 2 * m - 2); const int q = i % per; pick = m == 1 ? 0 : (q < m ? m - 1 - q : q - (m - 2)); break; }
                    case 4: pick = rnd.nextInt (m); break;
                    default: pick = i % m; break;
                }
                play (seq[(size_t) pick].first, seq[(size_t) pick].second);
            }
            offAt = at + jmax (1, (int) (gateP->get() * 0.01 * rs / sps));
        };

        // kroky v bloku (swing posuva neparne kroky)
        auto tickTime = [&] (long long k) { return ((double) k * rs + ((k & 1) ? swing * 0.5 * rs : 0.0) - clock) / sps; };
        long long k = (long long) std::ceil (clock / rs - 0.5 - 1.0e-9);
        while (tickTime (k) < -0.5) ++k;
        auto it = midi.cbegin();
        const auto end = midi.cend();
        for (;;)
        {
            const int tAt = (int) std::floor (tickTime (k) + 0.5);
            const int eAt = it != end ? (*it).samplePosition : numSamples;
            const int oAt = (! sounding.empty() && offAt >= 0) ? offAt : numSamples;
            const int at = jmin (tAt, eAt, oAt);
            if (at >= numSamples) break;
            if (oAt == at) { offAll (at); continue; }
            if (eAt == at)
            {
                const auto msg = (*it).getMessage();
                ++it;
                lastCh = msg.getChannel() > 0 ? msg.getChannel() : lastCh;
                if (msg.isNoteOn())
                {
                    const bool first = held.empty();
                    const int p = msg.getNoteNumber();
                    if (first) { idx = 0; latched.clear(); }
                    if (std::find_if (held.begin(), held.end(), [p] (const auto& h) { return h.first == p; }) == held.end())
                        held.push_back ({ p, (int) msg.getVelocity() });
                    latched = held;
                    if (first && ! playing) { clock = -at * sps; k = 0; }   // bez prehravania: zacat hned
                }
                else if (msg.isNoteOff())
                {
                    const int p = msg.getNoteNumber();
                    held.erase (std::remove_if (held.begin(), held.end(), [p] (const auto& h) { return h.first == p; }), held.end());
                    if (held.empty() && ! latch) offAll (at);
                }
                else if (msg.isAllNotesOff() || msg.isAllSoundOff())
                {
                    held.clear(); latched.clear(); offAll (at); out.addEvent (msg, at);
                }
                else out.addEvent (msg, at);
                continue;
            }
            tick (at, k);
            ++k;
        }
        if (! sounding.empty() && offAt >= 0) offAt -= numSamples;
        clock += numSamples * sps;
        if (! playing && held.empty() && ! latch) curStep = -1;
        midi.swapWith (out);
    }

    std::atomic<int> stepVel[16], stepOct[16];
    std::atomic<bool> stepOn[16];
    std::atomic<int> curStep { -1 };
    std::atomic<int> presetIdx { -1 };   // vybrany hotovy vzor (pamata sa aj v projekte)
    FxParam *rateP, *modeP, *octP, *lenP, *gateP, *swingP, *velP, *latchP, *patP;
    AudioProcessorEditor* createEditor() override;

private:
    void prepare() override { held.clear(); latched.clear(); sounding.clear(); offAt = -1; clock = 0; idx = 0; out.ensureSize (2048); }
    void process (AudioBuffer<float>&) override {}

    MidiBuffer out;
    std::vector<std::pair<int, int>> held, latched, seq;
    std::vector<int> sounding;
    double clock = 0;
    int idx = 0, offAt = -1, lastCh = 1;
    Random rnd;
};

void ArpGrid::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
    g.setFont (FontOptions (12.0f));
    g.setColour (Colour (0xff8a96a3));
    g.drawText (sk ("klik = krok zap/vyp · ťahanie = sila · dolný riadok = oktáva"), r.reduced (10, 4).removeFromTop (14), Justification::centredLeft);
    if (arp.patP->get() < 0.5f) { g.setColour (Colour (0xcc161c24)); g.fillRoundedRectangle (r, 3.0f); g.setColour (Colour (0xff8a96a3)); g.drawText (sk ("Vzor vypnutý – hrá každý krok, sila z hry"), r, Justification::centred); return; }
    const auto va = velArea(), oa = octArea();
    const float w = va.getWidth() / 16.0f;
    const int len = jlimit (1, 16, roundToInt (arp.lenP->get())), cur = arp.curStep.load();
    for (int i = 0; i < 16; ++i)
    {
        auto cell = Rectangle<float> (va.getX() + w * (float) i + 2, va.getY(), w - 4, va.getHeight());
        const bool inLen = i < len, on = arp.stepOn[i].load();
        g.setColour (i == cur ? Colour (0xff27384d) : Colour (0xff1f2630));
        g.fillRoundedRectangle (cell, 3.0f);
        if (on && inLen)
        {
            const float h = cell.getHeight() * (float) arp.stepVel[i].load() / 127.0f;
            g.setColour (i == cur ? Colour (0xffc9d2dc) : Colour (0xff6f8fb3));
            g.fillRoundedRectangle (cell.withTop (cell.getBottom() - h), 3.0f);
        }
        else if (inLen)
        {
            g.setColour (Colour (0xff3a4656));
            g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 1.0f);
        }
        if (! inLen) { g.setColour (Colour (0xaa161c24)); g.fillRoundedRectangle (cell, 3.0f); }
        if (i == cur && inLen) { g.setColour (Colour (0xffe4e9ef)); g.drawRoundedRectangle (cell.reduced (0.5f), 3.0f, 2.0f); }
        auto oc = Rectangle<float> (oa.getX() + w * (float) i + 2, oa.getY(), w - 4, oa.getHeight());
        const int o = arp.stepOct[i].load();
        g.setColour (o == 0 ? Colour (0xff1f2630) : Colour (0xff2d4362));
        g.fillRoundedRectangle (oc, 3.0f);
        g.setColour (inLen ? Colour (0xffc9d2dc) : Colour (0xff5a6472));
        g.drawText (o > 0 ? "+12" : (o < 0 ? "-12" : String (i + 1)), oc, Justification::centred);
    }
}

void ArpGrid::mouseDown (const MouseEvent& e)
{
    const int i = stepAt (e.position.x);
    if (octArea().contains (e.position))
    {
        const int o = arp.stepOct[i].load();
        arp.stepOct[i] = o == 0 ? 1 : (o == 1 ? -1 : 0);
        dragMode = 0;
    }
    else if (velArea().contains (e.position))
    {
        if (e.mods.isRightButtonDown() || ! arp.stepOn[i].load()) { const bool v = ! arp.stepOn[i].load(); arp.stepOn[i] = v; dragMode = v ? 3 : 2; }
        else dragMode = 1;
        mouseDrag (e);
    }
    repaint();
}

void ArpGrid::mouseDrag (const MouseEvent& e)
{
    const int i = stepAt (e.position.x);
    const auto va = velArea();
    if (dragMode == 1)
    {
        arp.stepOn[i] = true;
        arp.stepVel[i] = jlimit (1, 127, (int) ((va.getBottom() - e.position.y) / va.getHeight() * 127.0f));
    }
    else if (dragMode == 2) arp.stepOn[i] = false;
    else if (dragMode == 3) arp.stepOn[i] = true;
    repaint();
}


//==============================================================================
// okno arpeggiatora (rozlozenie ako v Studio One, nase farby)
class ArpModeButtons final : public Component
{
public:
    explicit ArpModeButtons (FxParam* p) : prm (p) {}
    void paint (Graphics& g) override
    {
        const int cur = roundToInt (prm->get());
        const float w = (float) getWidth() / 7.0f;
        for (int i = 0; i < 7; ++i)
        {
            auto c = Rectangle<float> (w * (float) i, 0, w, (float) getHeight()).reduced (1.5f, 0);
            g.setColour (i == cur ? Colour (0xff6f8fb3) : Colour (0xff2a3442));
            g.fillRoundedRectangle (c, 3.0f);
            g.setColour (i == cur ? Colour (0xff161c24) : Colour (0xffc9d2dc));
            auto a = c.reduced (c.getWidth() * 0.22f, c.getHeight() * 0.26f);
            auto pt = [&a] (float x, float y) { return Point<float> (a.getX() + x * a.getWidth(), a.getBottom() - y * a.getHeight()); };
            Path p;
            static const float shapes[6][5] = { { 0, .25f, .5f, .75f, 1 }, { 1, .75f, .5f, .25f, 0 }, { 0, .5f, 1, .5f, 0 }, { 1, .5f, 0, .5f, 1 }, { .3f, .9f, .1f, .7f, .4f }, { .1f, .8f, .4f, 1, .6f } };
            if (i < 4 || i == 4)
            {
                for (int k = 0; k < 5; ++k) g.fillRoundedRectangle (Rectangle<float> (3.5f, 3.5f).withCentre (pt ((float) k / 4.0f, shapes[i][k])), 1.0f);
                for (int k = 0; k < 5; ++k) { if (k == 0) p.startNewSubPath (pt (0, shapes[i][0])); else p.lineTo (pt ((float) k / 4.0f, shapes[i][k])); }
                if (i != 4) g.strokePath (p, PathStrokeType (1.0f));
            }
            else if (i == 5)
            {
                for (int k = 0; k < 5; ++k) g.fillRoundedRectangle (Rectangle<float> (3.5f, 3.5f).withCentre (pt ((float) k / 4.0f, shapes[5][k])), 1.0f);
                g.drawText ("1", a.withTrimmedTop (a.getHeight() * 0.6f).translated (0, a.getHeight() * 0.35f), Justification::centred);
            }
            else
                for (int k = 0; k < 3; ++k) g.fillRoundedRectangle (Rectangle<float> (a.getWidth() * 0.18f, a.getHeight()).withCentre (pt (0.2f + 0.3f * (float) k, 0.5f)), 1.0f);
        }
    }
    void mouseDown (const MouseEvent& e) override
    {
        const int i = jlimit (0, 6, (int) (e.position.x / ((float) getWidth() / 7.0f)));
        prm->beginChangeGesture(); prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) i)); prm->endChangeGesture();
        repaint();
    }
private:
    FxParam* prm;
};

class ArpEditor final : public AudioProcessorEditor, private Timer
{
public:
    explicit ArpEditor (ArpFx& a) : AudioProcessorEditor (a), arp (a), grid (a), modes (a.modeP)
    {
        knob (rate, arp.rateP, true); knob (swing, arp.swingP, false); knob (gate, arp.gateP, false);
        rate.setTextBoxStyle (Slider::TextBoxBelow, true, 90, 20);
        for (int i = 1; i <= 4; ++i) oct.addItem (String (i), i);
        oct.onChange = [this] { set (arp.octP, (float) oct.getSelectedId()); };
        style (oct);
        toggle (hold, arp.latchP, sk ("Podržať"));
        toggle (pat, arp.patP, sk ("Vzor"));
        const char* vm[3] = { "Z hry", "Vzor", "Mix" };
        for (int i = 0; i < 3; ++i)
        {
            auto& b = velB[i];
            b.setButtonText (sk (vm[i]));
            b.setClickingTogglesState (false);
            b.setColour (TextButton::buttonColourId, Colour (0xff2a3442));
            b.setColour (TextButton::buttonOnColourId, Colour (0xff6f8fb3));
            b.setColour (TextButton::textColourOffId, Colour (0xffc9d2dc));
            b.setColour (TextButton::textColourOnId, Colour (0xff161c24));
            b.onClick = [this, i] { set (arp.velP, (float) i); };
            addAndMakeVisible (b);
        }
        len.setSliderStyle (Slider::LinearBar);
        len.setRange (1, 16, 1);
        len.setColour (Slider::trackColourId, Colour (0xff2d4362));
        len.setColour (Slider::backgroundColourId, Colour (0xff1f2630));
        len.setColour (Slider::textBoxTextColourId, Colour (0xffe4e9ef));
        len.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        len.textFromValueFunction = [] (double v) { return sk ("Dĺžka vzoru: ") + String ((int) v) + sk (" krokov"); };
        len.setTextBoxIsEditable (false);   // inak sa po otvoreni okna ukaze svetle editovacie pole s oznacenym textom
        len.setWantsKeyboardFocus (false);
        len.onValueChange = [this] { set (arp.lenP, (float) len.getValue()); };
        {
            int n = 0; const auto* pl = presetList (n);
            for (int i = 0; i < n; ++i) presets.addItem (sk (pl[i].name), i + 1);
            presets.setTextWhenNothingSelected (sk ("Hotové vzory"));
            presets.onChange = [this] { const int id = presets.getSelectedId(); if (id > 0) applyPreset (id - 1); };
            presets.setSelectedId (arp.presetIdx.load() + 1, dontSendNotification);
            style (presets);
            dice.setButtonText (sk ("Kocka"));
            dice.setTooltip (sk ("Náhodný vzor – sila, pauzy a oktávy"));
            dice.setColour (TextButton::buttonColourId, Colour (0xff2a3442));
            dice.setColour (TextButton::textColourOffId, Colour (0xffc9d2dc));
            dice.onClick = [this] { randomPattern(); };
        }
        for (auto* c : std::initializer_list<Component*> { &rate, &swing, &gate, &oct, &hold, &pat, &len, &grid, &modes, &presets, &dice }) addAndMakeVisible (c);
        setSize (960, 330);
        sync();
        startTimerHz (15);
    }
    ~ArpEditor() override { stopTimer(); }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff161c24));
        g.setColour (Colour (0xffe4e9ef));
        g.setFont (FontOptions (22.0f, Font::bold));
        g.drawText ("KORGUS ARP", 16, 10, 250, 30, Justification::centredLeft);
        g.setColour (Colour (0xff1f2630));
        g.fillRoundedRectangle (left.toFloat(), 3.0f);
        g.fillRoundedRectangle (mid.toFloat(), 3.0f);
        g.setColour (Colour (0xffc9d2dc));
        g.setFont (FontOptions (13.0f));
        g.drawText (sk ("Oktávy"), left.getX() + 10, left.getY() + 8, 70, 22, Justification::centredLeft);
        g.drawText (sk ("Rýchlosť"), left.getX(), left.getY() + 38, left.getWidth(), 18, Justification::centred);
        g.drawText ("Swing", swing.getX(), swing.getBottom() - 2, swing.getWidth(), 18, Justification::centred);
        g.drawText (sk ("Dĺžka tónu"), gate.getX() - 10, gate.getBottom() - 2, gate.getWidth() + 20, 18, Justification::centred);
        g.drawText (sk ("Sila"), mid.getX(), velB[0].getY() - 22, mid.getWidth(), 18, Justification::centred);
    }

    void resized() override
    {
        left = { 16, 52, 190, 264 };
        mid = { 216, 52, 96, 264 };
        oct.setBounds (left.getRight() - 70, left.getY() + 8, 60, 22);
        rate.setBounds (left.getX() + 40, left.getY() + 56, 110, 122);
        swing.setBounds (left.getX() + 14, left.getBottom() - 82, 64, 62);
        gate.setBounds (left.getRight() - 78, left.getBottom() - 82, 64, 62);
        hold.setBounds (mid.getX() + 8, mid.getY() + 8, mid.getWidth() - 16, 26);
        for (int i = 0; i < 3; ++i) velB[i].setBounds (mid.getX() + 10, mid.getY() + 96 + i * 34, mid.getWidth() - 20, 26);
        modes.setBounds (324, 12, 300, 30);
        presets.setBounds (640, 14, 160, 26);
        dice.setBounds (808, 14, 56, 26);
        pat.setBounds (getWidth() - 88, 14, 76, 26);
        grid.setBounds (324, 52, getWidth() - 340, 214);
        len.setBounds (324, 276, getWidth() - 340, 40);
    }

private:
    void set (FxParam* p, float v) { p->beginChangeGesture(); p->setValueNotifyingHost (p->range.convertTo0to1 (v)); p->endChangeGesture(); }
    void style (ComboBox& c)
    {
        c.setColour (ComboBox::backgroundColourId, Colour (0xff2d4362));
        c.setColour (ComboBox::textColourId, Colour (0xffe4e9ef));
        c.setColour (ComboBox::outlineColourId, Colours::transparentBlack);
    }
    void knob (Slider& k, FxParam* p, bool big)
    {
        k.setSliderStyle (Slider::RotaryHorizontalVerticalDrag);
        k.setTextBoxStyle (big ? Slider::TextBoxBelow : Slider::NoTextBox, true, 80, 18);
        k.setRange (p->range.start, p->range.end, big ? 1.0 : 0.0);
        k.setColour (Slider::rotarySliderFillColourId, Colour (0xff6f8fb3));
        k.setColour (Slider::rotarySliderOutlineColourId, Colour (0xff2d4362));
        k.setColour (Slider::thumbColourId, Colour (0xffe4e9ef));
        k.setColour (Slider::textBoxTextColourId, Colour (0xffe4e9ef));
        k.setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        k.textFromValueFunction = [p] (double v) { return p->getText (p->range.convertTo0to1 ((float) v), 0); };
        k.setDoubleClickReturnValue (true, p->range.convertFrom0to1 (p->defNorm));
        k.onValueChange = [this, &k, p] { if (! syncing) p->setValueNotifyingHost (p->range.convertTo0to1 ((float) k.getValue())); };
        k.setTooltip (p->name);
    }
    void toggle (ToggleButton& t, FxParam* p, const String& text)
    {
        t.setButtonText (text);
        t.setColour (ToggleButton::textColourId, Colour (0xffe4e9ef));
        t.setColour (ToggleButton::tickColourId, Colour (0xffe4e9ef));
        t.onClick = [this, &t, p] { if (! syncing) set (p, t.getToggleState() ? 1.0f : 0.0f); };
    }
    void sync()
    {
        syncing = true;
        if (! rate.isMouseButtonDown()) rate.setValue (arp.rateP->get(), dontSendNotification);
        if (! swing.isMouseButtonDown()) swing.setValue (arp.swingP->get(), dontSendNotification);
        if (! gate.isMouseButtonDown()) gate.setValue (arp.gateP->get(), dontSendNotification);
        if (! len.isMouseButtonDown()) len.setValue (arp.lenP->get(), dontSendNotification);
        oct.setSelectedId (roundToInt (arp.octP->get()), dontSendNotification);
        hold.setToggleState (arp.latchP->get() > 0.5f, dontSendNotification);
        pat.setToggleState (arp.patP->get() > 0.5f, dontSendNotification);
        const int vm = roundToInt (arp.velP->get());
        for (int i = 0; i < 3; ++i) velB[i].setToggleState (i == vm, dontSendNotification);
        rate.updateText(); len.updateText();
        modes.repaint();
        syncing = false;
    }
    void timerCallback() override { sync(); }

    ArpFx& arp;
    ArpGrid grid;
    ArpModeButtons modes;
    Slider rate, swing, gate, len;
    ComboBox oct, presets;
    ToggleButton hold, pat;
    TextButton velB[3], dice;
    Rectangle<int> left, mid;
    bool syncing = false;

    // hotove vzory: smer, rychlost, dlzka tonu, kroky zap (bity), sila, oktava
    struct ArpPreset { const char* name; int mode, rate, gate; const char* on; const char* vel; const char* oct; };
    static const ArpPreset* presetList (int& n)
    {
        static const ArpPreset p[] = {
            { "Pop arpeggio",     0, 4, 60, "1111111111111111", "IAEAIAEAIAEAIAEA", "0000000000000000" },
            { "Trance gate",      6, 4, 45, "1011010110110101", "IAIIAIAIIAIIAIAI", "0000000000000000" },
            { "Basa v oktávach",  0, 4, 50, "1111111111111111", "IEGEIEGEIEGEIEGE", "0101010101010101" },
            { "Folk pluck",      2, 4, 35, "1111111111111111", "IAAIAAIAIAAIAAIA", "0000000000000000" },
            { "Hore a dole",      2, 4, 70, "1111111111111111", "GGGGGGGGGGGGGGGG", "0000000000000000" },
            { "Akordy na osminy", 6, 1, 50, "1111111111111111", "IEGEIEGEIEGEIEGE", "0000000000000000" },
            { "Synth 80s",        0, 4, 55, "1111111111111111", "IEGEIEGEIEGEIEGE", "0000000010000000" },
            { "Zimmer pulz",      6, 4, 80, "1111111111111111", "IEEEIEEEIEEEIEEE", "0000000000000000" } };
        n = (int) (sizeof (p) / sizeof (p[0]));
        return p;
    }
    void applyPreset (int i)
    {
        int n = 0; const auto* p = presetList (n);
        if (i < 0 || i >= n) return;
        const auto& q = p[i];
        set (arp.modeP, (float) q.mode); set (arp.rateP, (float) q.rate); set (arp.gateP, (float) q.gate);
        set (arp.lenP, 16.0f); set (arp.patP, 1.0f);
        for (int k = 0; k < 16; ++k)
        {
            arp.stepOn[k] = q.on[k] == '1';
            arp.stepVel[k] = jlimit (1, 127, 50 + (q.vel[k] - 'A') * 9);   // A = 50 ... I = 122
            arp.stepOct[k] = q.oct[k] == '1' ? 1 : 0;
        }
        arp.presetIdx = i;
        grid.repaint(); sync();
    }
    void randomPattern()
    {
        Random r;
        for (int k = 0; k < 16; ++k)
        {
            arp.stepOn[k] = k == 0 || r.nextFloat() < 0.75f;
            arp.stepVel[k] = (k % 4 == 0) ? 110 + r.nextInt (18) : 60 + r.nextInt (50);
            const float o = r.nextFloat();
            arp.stepOct[k] = o < 0.12f ? 1 : (o < 0.18f ? -1 : 0);
        }
        set (arp.patP, 1.0f);
        arp.presetIdx = -1; presets.setSelectedId (0, dontSendNotification);
        grid.repaint(); sync();
    }
};

AudioProcessorEditor* ArpFx::createEditor() { return new ArpEditor (*this); }

//==============================================================================
// Korgus Master (mastering v jednom efekte) a Korgus Vocoder
namespace mvcol
{
    inline const Colour panel (0xff161c24), tile (0xff1f2630), steel (0xff2d4362), hover (0xff3a5478),
                        gold (0xffc9a35a), text (0xffc9d2dc), dim (0xff8a96a6), warn (0xffd08070);
}

inline const char* langText (const char* skText, const char* enText) { return appLang() == 1 ? enText : skText; }

// plochy prepinac s minimalnym zaoblenim (3 px)
struct FlatBtn final : public Component
{
    String text;
    bool on = false;
    std::function<void()> onClick;
    void paint (Graphics& g) override
    {
        g.setColour (on ? mvcol::gold : (isMouseOver() ? mvcol::hover : mvcol::steel));
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        g.setColour (on ? Colour (0xff1f2630) : mvcol::text);
        g.setFont (FontOptions (13.0f));
        g.drawFittedText (text, getLocalBounds(), Justification::centred, 1);
    }
    void mouseDown (const MouseEvent&) override { if (onClick) onClick(); }
    void mouseEnter (const MouseEvent&) override { repaint(); }
    void mouseExit (const MouseEvent&) override { repaint(); }
};

class MasterFx;

//==============================================================================
// Korgus Meter - merac hlasitosti podla EBU R128 / ITU-R BS.1770 (radar, LUFS, True Peak, LRA, korelacia, Mid/Side)
class MeterFx final : public FxBase
{
public:
    MeterFx() : FxBase ("Korgus Meter", "meter")
    {
        target = add ("target", "Cieľ", 0, 3, 0, "", 1.0f, 0);
        target->range.interval = 1.0f;
        target->choices = StringArray { sk ("Štandard −14 LUFS"), sk ("Jemne −16 LUFS"), sk ("Hlasno −9 LUFS"), sk ("Rádio / TV −23 LUFS") };
        target->inEditor = false;
        momP.resize (maxBlocks); stP.resize (maxBlocks);
    }
    static constexpr int maxBlocks = 72000;          // 2 hodiny po 100 ms
    static constexpr int radarN = 1200;              // radar: 2 minuty po 100 ms
    static float targetLufs (int i) { static const float t[4] = { -14.0f, -16.0f, -9.0f, -23.0f }; return t[jlimit (0, 3, i)]; }
    float tgt() const { const float e = extTarget.load(); return e < 0.0f ? e : targetLufs (roundToInt (target->get())); }
    std::atomic<float> extTarget { 0.0f };   // ciel z Korgus Master (zluceny merac), 0 = vlastny vyber

    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 392; }
    int displayWidth() const override { return 860; }
    bool showHeader() const override { return false; }
    double getTailLengthSeconds() const override { return 0; }

    // pre zobrazenie (zapisuje audio vlakno)
    std::atomic<float> momNow { -100 }, stNow { -100 }, momMax { -100 }, tpMax { -100 }, corr { 1 };
    std::atomic<float> pkL { -100 }, pkR { -100 };
    std::atomic<int> nBlocks { 0 }, radarW { 0 };
    std::array<std::atomic<float>, radarN> radar {};
    std::vector<float> momP, stP;                    // vykony blokov (400 ms a 3 s okno), kazdych 100 ms
    std::atomic<bool> resetReq { false };
    FxParam* target = nullptr;
    struct XY { float x, y; };
    std::array<XY, 2048> vs {};                      // Mid/Side (vektorskop)
    std::atomic<int> vsW { 0 };

private:
    void prepare() override
    {
        using C = dsp::IIR::Coefficients<float>;
        for (int c = 0; c < 2; ++c)
        {
            kpre[(size_t) c].coefficients = C::makeHighShelf (rate, 1681.97, 0.7072f, Decibels::decibelsToGain (4.0f));
            krlb[(size_t) c].coefficients = C::makeHighPass (rate, 38.13, 0.5003f);
            kpre[(size_t) c].reset(); krlb[(size_t) c].reset();
        }
        blockLen = jmax (1, (int) std::round (rate * 0.1));
        clearAll();
        for (auto& r : radar) r = -100.0f;
    }
    void clearAll()
    {
        acc = 0; accN = 0; ring.fill (0.0); ringN = 0; ringW = 0;
        nBlocks = 0; momMax = -100.0f; tpMax = -100.0f; radarW = 0;
        for (auto& r : radar) r = -100.0f;
        sLR = sLL = sRR = 0;
        for (auto& h : hist) h.fill (0.0f);
    }
    static float lufs (double p) { return p > 1.0e-12 ? (float) (-0.691 + 10.0 * std::log10 (p)) : -100.0f; }

    void process (AudioBuffer<float>& b) override
    {
        if (resetReq.exchange (false)) clearAll();
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        const float* L = b.getReadPointer (0);
        const float* R = b.getReadPointer (ch > 1 ? 1 : 0);
        float pl = 0, pr = 0, tp = 0;
        for (int i = 0; i < n; ++i)
        {
            const float l = L[i], r = R[i];
            const float kl = krlb[0].processSample (kpre[0].processSample (l));
            const float kr = ch > 1 ? krlb[1].processSample (kpre[1].processSample (r)) : 0.0f;
            acc += (double) kl * kl + (double) kr * kr;
            ++accN;
            pl = jmax (pl, std::abs (l)); pr = jmax (pr, std::abs (r));
            // True Peak priblizne: 4x nadvzorkovanie Catmull-Rom medzi vzorkami
            for (int c = 0; c < (ch > 1 ? 2 : 1); ++c)
            {
                auto& h = hist[(size_t) c];
                h[0] = h[1]; h[1] = h[2]; h[2] = h[3]; h[3] = c == 0 ? l : r;
                for (float t : { 0.25f, 0.5f, 0.75f })
                {
                    const float a0 = -0.5f * h[0] + 1.5f * h[1] - 1.5f * h[2] + 0.5f * h[3];
                    const float a1 = h[0] - 2.5f * h[1] + 2.0f * h[2] - 0.5f * h[3];
                    const float a2 = -0.5f * h[0] + 0.5f * h[2];
                    tp = jmax (tp, std::abs (((a0 * t + a1) * t + a2) * t + h[1]));
                }
                tp = jmax (tp, std::abs (h[3]));
            }
            sLR += (double) l * r; sLL += (double) l * l; sRR += (double) r * r;
            if ((i & 3) == 0) { const int w = vsW.load(); vs[(size_t) w] = { l, r }; vsW = (w + 1) & 2047; }
            if (accN >= blockLen) block();
        }
        const float dl = Decibels::gainToDecibels (pl, -100.0f), dr = Decibels::gainToDecibels (pr, -100.0f);
        pkL = jmax (dl, pkL.load() - 0.6f); pkR = jmax (dr, pkR.load() - 0.6f);
        const float tpd = Decibels::gainToDecibels (tp, -100.0f);
        if (tpd > tpMax.load()) tpMax = tpd;
    }
    void block()
    {
        const double p = acc / (double) accN;
        acc = 0; accN = 0;
        ring[(size_t) ringW] = p; ringW = (ringW + 1) % 30; ringN = jmin (30, ringN + 1);
        double m = 0, s = 0;
        for (int k = 0; k < jmin (4, ringN); ++k) m += ring[(size_t) ((ringW - 1 - k + 30) % 30)];
        for (int k = 0; k < ringN; ++k) s += ring[(size_t) k];
        m /= (double) jmin (4, ringN); s /= (double) ringN;
        const float mL = lufs (m), sL = lufs (s);
        momNow = mL; stNow = sL;
        if (mL > -70.0f)
        {
            if (mL > momMax.load()) momMax = mL;
            const int k = nBlocks.load();
            if (k < maxBlocks) { momP[(size_t) k] = (float) m; stP[(size_t) k] = ringN >= 30 ? (float) s : 0.0f; nBlocks.store (k + 1, std::memory_order_release); }
            const int w = radarW.load(); radar[(size_t) w] = sL; radarW = (w + 1) % radarN;
        }
        const double d = std::sqrt (sLL * sRR);
        const float c = d > 1.0e-12 ? (float) (sLR / d) : 1.0f;
        corr = corr.load() * 0.6f + c * 0.4f;
        sLR = sLL = sRR = 0;
    }

    std::array<dsp::IIR::Filter<float>, 2> kpre, krlb;
    std::array<std::array<float, 4>, 2> hist {};
    std::array<double, 30> ring {};
    int ringN = 0, ringW = 0, blockLen = 4410, accN = 0;
    double acc = 0, sLR = 0, sLL = 0, sRR = 0;
};

class MeterDisplay final : public Component, public SettableTooltipClient, private Timer
{
public:
    explicit MeterDisplay (MeterFx& f, bool embedded = false) : fx (f), emb (embedded)
    {
        setTooltip (sk ("Merač hlasitosti: radar = hlasitosť za posledné 2 minúty, zelená = v cieli, žltá = hlasnejšie, modrá = tichšie."));
        for (int i = 0; i < 4; ++i)
        {
            auto* b = tg.add (new TextButton (fx.target->choices[i] + "  " + String ((int) MeterFx::targetLufs (i))));
            style (*b);
            b->setTooltip (sk ("Cieľová hlasitosť (LUFS) pre túto službu"));
            b->onClick = [this, i] { fx.target->setFromUi ((float) i); repaint(); };
            if (! emb) addAndMakeVisible (b);   // v Korgus Master urcuje ciel gombik Hlasitost
        }
        reset.setButtonText (sk ("Reset"));
        style (reset);
        reset.setTooltip (sk ("Vynuluje meranie (začne odznova)"));
        reset.onClick = [this] { fx.resetReq = true; holdL = holdR = -100; };
        addAndMakeVisible (reset);
        startTimerHz (20);
    }
    ~MeterDisplay() override { stopTimer(); }
    void resized() override
    {
        auto r = getLocalBounds().removeFromBottom (30).reduced (8, 3).withTrimmedLeft (radarSize() + 16);
        reset.setBounds (r.removeFromRight (70));
        r.removeFromRight (8);
        const int w = r.getWidth() / 4;
        for (auto* b : tg) b->setBounds (r.removeFromLeft (w).reduced (2, 0));
    }
    void paint (Graphics& g) override
    {
        g.setColour (GCol::bg); g.fillRoundedRectangle (getLocalBounds().toFloat(), 3.0f);
        const int ti = roundToInt (fx.target->get());
        for (int i = 0; i < tg.size(); ++i) tg[i]->setToggleState (i == ti, dontSendNotification);
        paintRadar (g);
        paintNumbers (g);
        paintBars (g);
        paintScope (g);
    }
private:
    static void style (TextButton& b)
    {
        b.setColour (TextButton::buttonColourId, Colour (0xff1c2532)); b.setColour (TextButton::buttonOnColourId, GCol::steel);
        b.setColour (TextButton::textColourOffId, GCol::text); b.setColour (TextButton::textColourOnId, Colours::white);
    }
    int radarSize() const { return getHeight() - 16; }
    Colour levelColour (float v, float T) const
    {
        if (v > T + 1.0f) return Colour (0xffc9b45a);        // hlasnejsie ako ciel
        if (v > T - 6.0f) return Colour (0xff6fa77c);        // v cieli
        return Colour (0xff4d7f96);                          // tichsie
    }
    void paintRadar (Graphics& g)
    {
        const float S = (float) radarSize(), cx = 8 + S * 0.5f, cy = 8 + S * 0.5f, R = S * 0.5f - 18, r0 = R * 0.2f;
        const float T = fx.tgt(), lo = T - 30.0f, hi = T + 9.0f;
        auto rad = [&] (float v) { return r0 + (R - r0) * jlimit (0.0f, 1.0f, (v - lo) / (hi - lo)); };
        g.setColour (Colour (0xff10161e)); g.fillEllipse (cx - R, cy - R, 2 * R, 2 * R);
        // historia (posledne 2 minuty), najnovsie pri ruciCke
        const int w = fx.radarW.load();
        const float step = MathConstants<float>::twoPi / (float) MeterFx::radarN;
        for (int k = 0; k < MeterFx::radarN; ++k)
        {
            const float v = fx.radar[(size_t) k].load();
            if (v < -90.0f) continue;
            const float a0 = (float) k * step, rr = rad (v);
            Path p; p.addPieSegment (cx - rr, cy - rr, 2 * rr, 2 * rr, a0, a0 + step * 1.05f, r0 / rr);
            const int age = (w - k + MeterFx::radarN) % MeterFx::radarN;
            g.setColour (levelColour (v, T).withAlpha (0.45f + 0.55f * (1.0f - (float) age / (float) MeterFx::radarN)));
            g.fillPath (p);
        }
        // kruhy: ciel, -6, -12, -18 a popisy
        g.setFont (FontOptions (11.0f));
        for (float d : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f })
        {
            const float rr = rad (T + d);
            g.setColour (d == 0 ? GCol::gold.withAlpha (0.9f) : Colour (0xff2a3a50));
            g.drawEllipse (cx - rr, cy - rr, 2 * rr, 2 * rr, d == 0 ? 1.4f : 1.0f);
            g.setColour (d == 0 ? GCol::gold : GCol::dim);
            g.drawText (String ((int) (T + d)), Rectangle<float> (cx + 3, cy - rr - 1, 34, 12), Justification::centredLeft);
        }
        for (int k = 0; k < 12; ++k) { const float a = (float) k * MathConstants<float>::twoPi / 12.0f; g.setColour (Colour (0xff1e2938)); g.drawLine (cx + r0 * std::sin (a), cy - r0 * std::cos (a), cx + R * std::sin (a), cy - R * std::cos (a), 1.0f); }
        // rucicka
        const float aw = (float) w * step;
        g.setColour (Colours::white.withAlpha (0.7f)); g.drawLine (cx + r0 * std::sin (aw), cy - r0 * std::cos (aw), cx + R * std::sin (aw), cy - R * std::cos (aw), 1.5f);
        // vonkajsi prstenec = okamzita hlasitost (Momentary)
        const float m = fx.momNow.load(), frac = jlimit (0.0f, 1.0f, (m - lo) / (hi - lo));
        const int segs = 60;
        for (int k = 0; k < segs; ++k)
        {
            const float a = (float) k / (float) segs * MathConstants<float>::twoPi, ro = R + 12, ri = R + 5;
            const bool on = (float) k / (float) segs < frac;
            g.setColour (on ? levelColour (lo + (float) k / (float) segs * (hi - lo), T) : Colour (0xff222c3a));
            const float a2 = a + MathConstants<float>::twoPi / (float) segs * 0.7f;
            Path p; p.addPieSegment (cx - ro, cy - ro, 2 * ro, 2 * ro, a, a2, ri / ro); g.fillPath (p);
        }
        g.setColour (Colour (0xff151c27)); g.fillEllipse (cx - r0 + 2, cy - r0 + 2, 2 * r0 - 4, 2 * r0 - 4);
        g.setColour (GCol::text); g.setFont (FontOptions (15.0f, Font::bold));
        const float st = fx.stNow.load();
        g.drawText (st > -90 ? String (st, 1) : String::fromUTF8 ("–"), Rectangle<float> (cx - r0, cy - 12, 2 * r0, 18), Justification::centred);
        g.setFont (FontOptions (9.0f)); g.setColour (GCol::dim);
        g.drawText ("LUFS S", Rectangle<float> (cx - r0, cy + 5, 2 * r0, 12), Justification::centred);
    }
    void paintNumbers (Graphics& g)
    {
        const float x = (float) radarSize() + 24, w = 230;
        float y = 10;
        auto row = [&] (const String& name, const String& val, const String& unit, Colour c)
        {
            g.setColour (GCol::dim); g.setFont (FontOptions (12.0f)); g.drawText (name, Rectangle<float> (x, y, w, 14), Justification::centredLeft);
            g.setColour (c); g.setFont (FontOptions (26.0f, Font::bold)); g.drawText (val, Rectangle<float> (x, y + 14, w - 52, 30), Justification::centredRight);
            g.setColour (GCol::dim); g.setFont (FontOptions (12.0f)); g.drawText (unit, Rectangle<float> (x + w - 48, y + 26, 48, 14), Justification::centredLeft);
            y += 50;
        };
        auto f1 = [] (float v) { return v > -90 ? String (v, 1) : String::fromUTF8 ("–"); };
        const float T = fx.tgt();
        row (sk ("Celková hlasitosť (Integrated)"), f1 (integ), "LUFS", integ > -90 ? levelColour (integ, T).brighter (0.3f) : GCol::text);
        row (sk ("Najvyššia špička (True Peak)"), f1 (fx.tpMax.load()), "dBTP", fx.tpMax.load() > -1.0f ? Colour (0xffd07a62) : GCol::gold);
        row (sk ("Najhlasnejšie miesto"), f1 (fx.momMax.load()), "LUFS", GCol::gold);
        row (sk ("Rozsah hlasitosti (LRA)"), lra > 0 ? String (lra, 1) : String::fromUTF8 ("–"), "LU", GCol::gold);
        row (sk ("Špička – hlasitosť (PLR)"), (integ > -90 && fx.tpMax.load() > -90) ? String (fx.tpMax.load() - integ, 1) : String::fromUTF8 ("–"), "dB", GCol::gold);
        // korelacia
        g.setColour (GCol::dim); g.setFont (FontOptions (12.0f)); g.drawText (sk ("Korelácia (fáza)"), Rectangle<float> (x, y, w, 14), Justification::centredLeft);
        auto bar = Rectangle<float> (x, y + 18, w, 10);
        g.setColour (GCol::panel); g.fillRoundedRectangle (bar, 2.0f);
        g.setColour (GCol::dim); g.drawVerticalLine ((int) bar.getCentreX(), bar.getY() - 2, bar.getBottom() + 2);
        const float c = jlimit (-1.0f, 1.0f, fx.corr.load()), cxp = bar.getCentreX() + c * bar.getWidth() * 0.5f;
        g.setColour (c < 0 ? Colour (0xffd07a62) : Colour (0xff6fa77c));
        g.fillRect (Rectangle<float>::leftTopRightBottom (jmin (cxp, bar.getCentreX()), bar.getY(), jmax (cxp, bar.getCentreX()), bar.getBottom()));
        g.setColour (GCol::dim); g.setFont (FontOptions (10.0f));
        g.drawText ("-1", bar.translated (0, 12).withHeight (12), Justification::centredLeft); g.drawText ("+1", bar.translated (0, 12).withHeight (12), Justification::centredRight);
        // cas merania
        const int secs = fx.nBlocks.load() / 10;
        g.setColour (GCol::text); g.setFont (FontOptions (13.0f));
        g.drawText (sk ("Meranie") + "  " + String (secs / 60) + ":" + String (secs % 60).paddedLeft ('0', 2), Rectangle<float> (x, y + 36, w, 16), Justification::centredLeft);
    }
    void paintBars (Graphics& g)
    {
        const float x0 = (float) radarSize() + 270, top = 22, bot = 200;
        auto yOf = [&] (float db) { return jmap (jlimit (-60.0f, 0.0f, db), -60.0f, 0.0f, bot, top); };
        g.setFont (FontOptions (10.0f));
        for (float d : { 0.0f, -6.0f, -12.0f, -18.0f, -24.0f, -36.0f, -48.0f, -60.0f }) { g.setColour (GCol::dim); g.drawText (String ((int) d), Rectangle<float> (x0, yOf (d) - 6, 26, 12), Justification::centredRight); }
        const float l = fx.pkL.load(), r = fx.pkR.load();
        holdL = jmax (l, holdL - 0.25f); holdR = jmax (r, holdR - 0.25f);
        int i = 0;
        for (float v : { l, r })
        {
            const float bx = x0 + 32 + (float) i * 30, hold = i == 0 ? holdL : holdR;
            auto col = Rectangle<float> (bx, top, 22, bot - top);
            g.setColour (GCol::panel); g.fillRect (col);
            const float y = yOf (v);
            g.setColour (Colour (0xff6f8fb3)); g.fillRect (Rectangle<float> (bx, jmax (y, yOf (-6)), 22, bot - jmax (y, yOf (-6))));
            if (v > -6) { g.setColour (GCol::gold); g.fillRect (Rectangle<float> (bx, jmax (y, yOf (-1)), 22, yOf (-6) - jmax (y, yOf (-1)))); }
            if (v > -1) { g.setColour (Colour (0xffd07a62)); g.fillRect (Rectangle<float> (bx, y, 22, yOf (-1) - y)); }
            g.setColour (Colours::white.withAlpha (0.8f)); g.fillRect (bx, yOf (hold) - 1, 22.0f, 2.0f);
            g.setColour (GCol::text); g.drawText (i == 0 ? "L" : "R", Rectangle<float> (bx, 6, 22, 12), Justification::centred);
            ++i;
        }
        g.setColour (GCol::dim); g.drawText ("dBFS", Rectangle<float> (x0 + 26, bot + 2, 60, 12), Justification::centred);
    }
    void paintScope (Graphics& g)
    {
        const float x0 = (float) radarSize() + 380, w = (float) getWidth() - x0 - 10, top = 10, h = 200;
        if (w < 80) return;
        const float cx = x0 + w * 0.5f, by = top + h - 8, R = jmin (w * 0.5f, h - 20);
        g.setColour (Colour (0xff10161e)); g.fillRoundedRectangle (Rectangle<float> (x0, top, w, h), 3.0f);
        Path arc; arc.addCentredArc (cx, by, R, R, 0, -MathConstants<float>::halfPi, MathConstants<float>::halfPi, true);
        g.setColour (Colour (0xff2a3a50)); g.strokePath (arc, PathStrokeType (1.0f));
        g.drawLine (cx, by, cx, by - R); g.drawLine (cx - R, by, cx + R, by);
        g.setColour (GCol::line.withAlpha (0.55f));
        for (const auto& p : fx.vs)
        {
            float px = (p.y - p.x) * 0.7071f, py = std::abs ((p.x + p.y) * 0.7071f);
            const float m = std::sqrt (px * px + py * py);
            if (m < 1.0e-4f) continue;
            const float sc = R * jmin (1.0f, std::sqrt (m)) / m;   // odmocnina: aj tiche casti su vidiet
            g.fillRect (cx + px * sc, by - py * sc, 1.5f, 1.5f);
        }
        g.setColour (GCol::dim); g.setFont (FontOptions (11.0f));
        g.drawText ("Mid / Side", Rectangle<float> (x0, top + 2, w, 12), Justification::centred);
        g.drawText (sk ("Ľavý"), Rectangle<float> (x0 + 4, by - 14, 50, 12), Justification::centredLeft);
        g.drawText (sk ("Pravý"), Rectangle<float> (x0 + w - 54, by - 14, 50, 12), Justification::centredRight);
    }
    void timerCallback() override
    {
        if (++calc >= 10) { calc = 0; compute(); }
        repaint();
    }
    void compute()   // Integrated (brana -70 / -10) a LRA (brana -70 / -20, percentily 10 a 95)
    {
        const int n = fx.nBlocks.load (std::memory_order_acquire);
        if (n <= 0) { integ = -100; lra = 0; return; }
        double s = 0; int c = 0;
        for (int i = 0; i < n; ++i) { const double p = fx.momP[(size_t) i]; if (p > 1.17e-7) { s += p; ++c; } }   // -70 LUFS
        if (c == 0) { integ = -100; lra = 0; return; }
        const double gate = s / c * 0.1;   // -10 LU
        double s2 = 0; int c2 = 0;
        for (int i = 0; i < n; ++i) { const double p = fx.momP[(size_t) i]; if (p > 1.17e-7 && p > gate) { s2 += p; ++c2; } }
        integ = c2 > 0 ? (float) (-0.691 + 10.0 * std::log10 (s2 / c2)) : -100.0f;
        std::vector<float> st; st.reserve ((size_t) n);
        double ss = 0; int sc = 0;
        for (int i = 0; i < n; ++i) { const double p = fx.stP[(size_t) i]; if (p > 1.17e-7) { ss += p; ++sc; } }
        if (sc > 0)
        {
            const double g20 = ss / sc * 0.01;   // -20 LU
            for (int i = 0; i < n; ++i) { const double p = fx.stP[(size_t) i]; if (p > 1.17e-7 && p > g20) st.push_back ((float) (-0.691 + 10.0 * std::log10 (p))); }
        }
        if (st.size() < 2) { lra = 0; return; }
        std::sort (st.begin(), st.end());
        lra = st[(size_t) (0.95 * (double) (st.size() - 1))] - st[(size_t) (0.10 * (double) (st.size() - 1))];
    }
    MeterFx& fx;
    OwnedArray<TextButton> tg;
    TextButton reset;
    float integ = -100, lra = 0, holdL = -100, holdR = -100;
    int calc = 0;
    bool emb = false;
};
std::unique_ptr<Component> MeterFx::makeDisplay() { return std::make_unique<MeterDisplay> (*this); }

class MasterDisplay final : public Component, private Timer
{
public:
    explicit MasterDisplay (MasterFx& f);
    ~MasterDisplay() override;
    void paint (Graphics& g) override;
    void resized() override;
    void mouseDoubleClick (const MouseEvent&) override;
private:
    void timerCallback() override;
    // asistent: pocuva piesen a nastavi styl a gombiky
    FlatBtn refPick, refAB;
    FlatBtn asst; bool listening = false; int heard = 0; double acc[31] {}; double crestSum = 0; int crestN = 0; String result;
    Spectrum inSpec;
    std::unique_ptr<Component> meterView;
    void asstToggle();
    void asstStep();
    void asstFinish();
    static String num (float v, int dec) { return v <= -99.0f ? String::fromUTF8 ("\xe2\x80\x93") : String (v, dec); }
    MasterFx& fx;
    FlatBtn btn;
    korgus::KorgusMaster::Meters m;
    String tTitle[4], tBands, tTarget, tReset;
};

class MasterFx final : public FxBase
{
public:
    MasterFx() : FxBase ("Korgus Master", "master")
    {
        using M = korgus::KorgusMaster;
        const bool en = appLang() == 1;
        auto kn = [en] (int i) { return en ? M::knobNameEn (i) : M::knobNameSk (i); };
        style = add ("style", en ? M::styleLabelEn() : M::styleLabelSk(), 0, (float) (M::kNumStyles - 1), 0, "", 1.0f, 0);
        style->range.interval = 1.0f;
        style->wide = true;
        for (int i = 0; i < M::kNumStyles; ++i) style->choices.add (String::fromUTF8 (en ? M::styleNameEn (i) : M::styleNameSk (i)));
        loud   = add ("loudness", kn (M::Loudness), -18, -8, -14, "LUFS", 1.0f, 1);
        punch  = add ("punch",    kn (M::Punch),      0, 100, 50, "%", 1.0f, 0);
        bright = add ("bright",   kn (M::Bright),  -100, 100, 0, "%", 1.0f, 0);
        bass   = add ("bass",     kn (M::Bass),    -100, 100, 0, "%", 1.0f, 0);
        width  = add ("width",    kn (M::Width),      0, 150, 100, "%", 1.0f, 0);
        style->inEditor = false;   // styl vybera asistent (tlacidlo v okne)
        cmp = add ("compare", "Porovnať", 0, 1, 0, "", 1.0f, 0);   // A/B, neuklada sa
        cmp->toggle = true; cmp->transient = true; cmp->inEditor = false;
    }

    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<MasterDisplay> (*this); }
    int displayHeight() const override { return kTopH + 8 + 392; }
    int displayWidth() const override { return 860; }
    static constexpr int kTopH = 150;   // horna cast: mastering, pod nou merac
    bool showHeader() const override { return false; }   // ma vlastne porovnanie

    void newSong() override { learnReq = true; meter.resetReq = true; }
    bool wantsPrepass() const override { return true; }
    void exportPhase (int ph) override
    {
        // volane z exportneho vlakna, ked audio vlakno tento efekt nespracuva
        exporting = ph != 0;
        if (ph == 1) { mm.setLearningFrozen (false); mm.resetLearning(); mm.reset(); }
        else if (ph == 2) { mm.reset(); mm.setLearningFrozen (true); }
        else mm.setLearningFrozen (false);
    }
    void reset() override { resetReq = true; }

    korgus::KorgusMaster mm;
    MeterFx meter;   // zluceny Korgus Meter (meria vystup mastra)
    FxParam *style, *loud, *punch, *bright, *bass, *width, *cmp;
    void getStateInformation (MemoryBlock& mb) override
    {
        FxBase::getStateInformation (mb);
        if (auto x = getXmlFromBinary (mb.getData(), (int) mb.getSize())) { x->setAttribute ("mtarget", (double) meter.target->get()); mb.reset(); copyXmlToBinary (*x, mb); }
    }
    void setStateInformation (const void* data, int size) override
    {
        FxBase::setStateInformation (data, size);
        if (auto x = getXmlFromBinary (data, size)) if (x->hasAttribute ("mtarget")) meter.target->setValueNotifyingHost (meter.target->range.convertTo0to1 ((float) x->getDoubleAttribute ("mtarget")));
    }
    std::atomic<bool> learnReq { false }, resetReq { false }, exporting { false };
    ScopeFifo inScope;   // vstup pred masteringom (pre asistenta)

private:
    void prepare() override
    {
        mm.prepare (rate, (int) spec.maximumBlockSize, 2);
        setLatencySamples (mm.getLatencySamples());
        meter.setPlayConfigDetails (2, 2, rate, (int) spec.maximumBlockSize); meter.prepareToPlay (rate, (int) spec.maximumBlockSize);
    }

    void process (AudioBuffer<float>& b) override
    {
        if (learnReq.exchange (false)) mm.resetLearning();
        if (resetReq.exchange (false)) mm.reset();
        mm.setParams (roundToInt (style->get()), (loud->get() + 18.0f) * 0.1f, punch->get() * 0.01f,
                      (bright->get() + 100.0f) * 0.005f, (bass->get() + 100.0f) * 0.005f, width->get() / 150.0f);
        mm.setBypass (cmp->get() > 0.5f && ! exporting.load(), true);
        inScope.push (b.getReadPointer (0), b.getNumChannels() > 1 ? b.getReadPointer (1) : nullptr, b.getNumSamples());
        mm.process (b.getWritePointer (0), b.getNumChannels() > 1 ? b.getWritePointer (1) : nullptr, b.getNumSamples());
        meter.extTarget = loud->get();
        if (! exporting.load()) { MidiBuffer none; meter.processBlock (b, none); }
    }
};

MasterDisplay::MasterDisplay (MasterFx& f) : fx (f)
{
    tTitle[0] = sk ("Krátkodobá (LUFS)"); tTitle[1] = sk ("Celková (LUFS)");
    tTitle[2] = sk ("Špička (dBTP)");     tTitle[3] = sk ("Limiter (dB)");
    tBands = sk ("Redukcia v pásmach (dB)");
    tTarget = sk ("Cieľ: ");
    tReset = sk ("Dvojklik: vynulovať merače");
    btn.text = sk ("Porovnať");
    btn.onClick = [this] { fx.cmp->beginChangeGesture(); fx.cmp->setValueNotifyingHost (fx.cmp->get() > 0.5f ? 0.0f : 1.0f); fx.cmp->endChangeGesture(); };
    addAndMakeVisible (btn);
    asst.text = String::fromUTF8 ("✦ ") + sk ("Asistent");
    asst.onClick = [this] { asstToggle(); };
    addAndMakeVisible (asst);
    refPick.text = sk ("Referencia…"); refAB.text = "A/B";
    refPick.onClick = [] { if (BuiltinFx::hostAction) BuiltinFx::hostAction ("refPick"); };
    refAB.onClick = [] { if (BuiltinFx::hostAction) BuiltinFx::hostAction (BuiltinFx::refLoaded.load() ? "refToggle" : "refPick"); };
    addAndMakeVisible (refPick); addAndMakeVisible (refAB);
    meterView = std::make_unique<MeterDisplay> (fx.meter, true);
    addAndMakeVisible (*meterView);
    startTimerHz (30);
}

void MasterDisplay::resized()
{
    btn.setBounds (getWidth() - 146, 8, 108, 26); asst.setBounds (getWidth() - 146, 38, 108, 24);
    refPick.setBounds (getWidth() - 146, 66, 72, 22); refAB.setBounds (getWidth() - 70, 66, 32, 22);
    if (meterView != nullptr) meterView->setBounds (0, MasterFx::kTopH + 8, getWidth(), getHeight() - MasterFx::kTopH - 8);
}

MasterDisplay::~MasterDisplay() { stopTimer(); if (listening) --fx.inScope.users; }

void MasterDisplay::asstToggle()
{
    if (listening) { listening = false; --fx.inScope.users; asst.on = false; asst.text = String::fromUTF8 ("✦ ") + sk ("Asistent"); result = {}; repaint(); return; }
    listening = true; heard = 0; crestSum = 0; crestN = 0; for (auto& v : acc) v = 0; result = {};
    ++fx.inScope.users; asst.on = true; asst.text = sk ("Zastaviť");
    repaint();
}

void MasterDisplay::asstStep()
{
    std::vector<float> x (2048); fx.inScope.read (x.data(), 2048);
    float pk = 0; double ss = 0; for (float v : x) { pk = jmax (pk, std::abs (v)); ss += (double) v * v; }
    const double rms = std::sqrt (ss / 2048.0);
    if (rms < 0.003) return;   // ticho nepocitat
    inSpec.update (fx.inScope, 0.0f);
    const double sr = fx.sampleRate();
    for (int k = 0; k < 31; ++k)
    {
        double s2 = 0;
        for (int j = -1; j <= 1; ++j) s2 += std::pow (10.0, inSpec.at ((float) (20.0 * std::pow (2.0, k / 3.0 + j / 9.0)), sr) / 10.0);
        acc[k] += s2 / 3.0;
    }
    crestSum += Decibels::gainToDecibels ((double) pk / rms); ++crestN;
    if (++heard >= 450) asstFinish();
}

void MasterDisplay::asstFinish()
{
    listening = false; --fx.inScope.users; asst.on = false; asst.text = String::fromUTF8 ("✦ ") + sk ("Asistent");
    float D[31]; float mean = 0; int cnt = 0;
    for (int k = 0; k < 31; ++k)
    {
        const float hz = 20.0f * std::pow (2.0f, (float) k / 3.0f);
        D[k] = 10.0f * (float) std::log10 (jmax (1.0e-12, acc[k] / heard)) + 4.5f * std::log2 (hz / 1000.0f);   // voci ruzovemu sumu -4.5 dB/okt
        if (hz >= 100 && hz <= 8000) { mean += D[k]; ++cnt; }
    }
    mean /= (float) jmax (1, cnt);
    auto avg = [&] (float lo, float hi) { float s2 = 0; int c = 0; for (int k = 0; k < 31; ++k) { const float hz = 20.0f * std::pow (2.0f, (float) k / 3.0f); if (hz >= lo && hz <= hi) { s2 += D[k] - mean; ++c; } } return c ? s2 / (float) c : 0.0f; };
    const float bassDev = avg (40, 120), midDev = avg (1000, 4000), highDev = avg (5000, 12000);
    const float crest = crestN ? (float) (crestSum / crestN) : 14.0f;
    int st = 0;
    if (crest < 11.0f && bassDev > 1.5f) st = 3;
    else if (crest > 17.0f && bassDev < 0.0f) st = midDev > 1.0f ? 5 : 4;
    else if (highDev > 1.0f && midDev > 0.5f && crest < 15.0f) st = 2;
    else if (crest < 14.0f) st = 1;
    const float br = jlimit (-60.0f, 60.0f, std::round (-highDev * 15.0f / 5.0f) * 5.0f);
    const float ba = jlimit (-60.0f, 60.0f, std::round (-bassDev * 15.0f / 5.0f) * 5.0f);
    const float pu = crest > 17.0f ? 60.0f : (crest < 10.0f ? 35.0f : 50.0f);
    auto set = [] (FxParam* p, float v) { p->beginChangeGesture(); p->setFromUi (v); p->endChangeGesture(); };
    set (fx.style, (float) st); set (fx.bright, br); set (fx.bass, ba); set (fx.punch, pu);
    const bool en = appLang() == 1;
    auto pct = [] (float v) { return (v > 0 ? "+" : "") + String ((int) v) + " %"; };
    result = (en ? String ("Style: ") : sk ("Štýl: ")) + String::fromUTF8 (en ? korgus::KorgusMaster::styleNameEn (st) : korgus::KorgusMaster::styleNameSk (st))
           + "  " + sk ("Jas") + " " + pct (br) + "  " + sk ("Basy") + " " + pct (ba) + "  " + sk ("Dynamika") + " " + String ((int) pu) + " %";
    repaint();
}

void MasterDisplay::timerCallback()
{
    m = fx.mm.getMeters();
    if (listening) asstStep();
    if (refAB.on != BuiltinFx::refOn.load()) { refAB.on = BuiltinFx::refOn.load(); refAB.repaint(); }
    btn.on = fx.cmp->get() > 0.5f;
    asst.repaint();
    btn.repaint();
    repaint();
}

void MasterDisplay::mouseDoubleClick (const MouseEvent&) { fx.mm.resetMeters(); }

void MasterDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat().withHeight ((float) MasterFx::kTopH);
    g.setColour (mvcol::panel);
    g.fillRoundedRectangle (r, 3.0f);

    auto row = r.reduced (8, 8).withHeight (52);
    row.removeFromRight (154);
    const float tw = row.getWidth() / 4.0f;
    const bool onTarget = m.outIntegrated > -99.0f && std::abs (m.outIntegrated - m.targetLufs) <= 1.0f;
    const String vals[4] = { num (m.outShortTerm, 1), num (m.outIntegrated, 1), num (m.truePeakDb, 1),
                             m.limiterGrDb > 0.05f ? "-" + String (m.limiterGrDb, 1) : String ("0.0") };
    const Colour cols[4] = { mvcol::text, onTarget ? mvcol::gold : mvcol::text,
                             m.truePeakDb > -0.5f ? mvcol::warn : mvcol::text, m.limiterGrDb > 6.0f ? mvcol::warn : mvcol::text };
    for (int i = 0; i < 4; ++i)
    {
        auto c = Rectangle<float> (row.getX() + tw * (float) i, row.getY(), tw - 6.0f, row.getHeight());
        g.setColour (mvcol::tile);
        g.fillRoundedRectangle (c, 3.0f);
        g.setColour (mvcol::dim);
        g.setFont (FontOptions (11.0f));
        g.drawFittedText (tTitle[i], c.toNearestInt().removeFromTop (18).reduced (6, 0), Justification::centredLeft, 1);
        g.setColour (cols[i]);
        g.setFont (FontOptions (22.0f).withStyle ("Bold"));
        g.drawFittedText (vals[i], c.toNearestInt().withTrimmedTop (16).reduced (6, 0), Justification::centredLeft, 1);
    }

    // redukcia v 4 pasmach
    static const char* const bandName[4] = { "< 120 Hz", "120 - 800 Hz", "0.8 - 5 kHz", "> 5 kHz" };
    auto bands = r.reduced (8, 0).withTrimmedTop (68);
    g.setColour (mvcol::dim);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (tBands, bands.removeFromTop (16).toNearestInt(), Justification::centredLeft, 1);
    const float bw = (bands.getWidth() - 154.0f) / 4.0f;
    for (int i = 0; i < 4; ++i)
    {
        auto c = Rectangle<float> (bands.getX() + bw * (float) i, bands.getY() + 2, bw - 6.0f, 8.0f);
        g.setColour (mvcol::steel);
        g.fillRoundedRectangle (c, 3.0f);
        const float f = jlimit (0.0f, 1.0f, m.bandGrDb[i] / 9.0f);
        g.setColour (mvcol::gold);
        if (f > 0.01f) g.fillRoundedRectangle (c.withWidth (jmax (3.0f, c.getWidth() * f)), 3.0f);
        g.setColour (mvcol::dim);
        g.drawFittedText (String (bandName[i]) + "  " + String (m.bandGrDb[i], 1), c.translated (0, 10).withHeight (14).toNearestInt(), Justification::centredLeft, 1);
    }
    // cielova hlasitost a napoveda k meracom
    auto right = r.reduced (8, 0).withTrimmedTop (92).removeFromRight (146).toNearestInt();
    g.setColour (mvcol::text);
    g.drawFittedText (tTarget + String (m.targetLufs, 1) + " LUFS", right.removeFromTop (16), Justification::centredRight, 1);
    g.setColour (mvcol::dim);
    g.setFont (FontOptions (10.0f));
    if (listening || result.isNotEmpty())
    {   // stav asistenta: pocas pocuvania ukazovatel priebehu, potom vysledok
        auto st = Rectangle<float> (8.0f, (float) MasterFx::kTopH - 20.0f, (float) getWidth() - 16.0f, 16.0f);
        if (listening)
        {
            const float pr = (float) heard / 450.0f;
            g.setColour (mvcol::tile); g.fillRoundedRectangle (st, 3.0f);
            g.setColour (mvcol::steel); g.fillRoundedRectangle (st.withWidth (jmax (6.0f, st.getWidth() * pr)), 3.0f);
            g.setColour (mvcol::text); g.setFont (FontOptions (11.5f));
            g.drawFittedText (heard == 0 ? sk ("Asistent čaká – pusti prehrávanie piesne") : sk ("Asistent počúva pieseň… ") + String (roundToInt (pr * 100.0f)) + " %",
                              st.toNearestInt().reduced (8, 0), Justification::centred, 1);
        }
        else
        {
            g.setColour (mvcol::tile); g.fillRoundedRectangle (st, 3.0f);
            g.setColour (mvcol::gold); g.setFont (FontOptions (11.5f));
            g.drawFittedText (result, st.toNearestInt().reduced (8, 0), Justification::centred, 1);
        }
    }
    else g.drawFittedText (tReset, right.withTrimmedTop (4).removeFromTop (28), Justification::topRight, 2);
}

//==============================================================================
class VocoderFx;

class VocoderDisplay final : public Component, private Timer
{
public:
    explicit VocoderDisplay (VocoderFx& f);
    ~VocoderDisplay() override { stopTimer(); }
    void paint (Graphics& g) override;
private:
    void timerCallback() override { repaint(); }
    VocoderFx& fx;
    String hSidechain, hSidechain2, hNotes, hNotes2, hRobot, hTrack2, hRobot2;
};

class VocoderFx final : public FxBase
{
public:
    // robot = "Korgus Robot hlas": len rezimy Robot / Robot trojhlas, na spevovu stopu (bez hrania)
    explicit VocoderFx (bool robotOnly = false) : FxBase (robotOnly ? "Korgus Robot hlas" : "Korgus Vocoder", robotOnly ? "robotvox" : "vocoder"), robot (robotOnly)
    {
        using V = korgus::KorgusVocoder;
        const bool en = appLang() == 1;
        if (robot)
        {
            mode = add ("carrier", en ? "Voice" : "Hlas", 0, 1, 1, "", 1.0f, 0);
            mode->choices = StringArray { "Robot", String::fromUTF8 (en ? "Robot harmony (3 voices)" : "Robot trojhlas") };
        }
        else
        {
            mode = add ("carrier", en ? "Carrier" : "Nosič", 0, 3, 0, "", 1.0f, 0);
            mode->choices = StringArray { String::fromUTF8 (en ? "Track" : "Stopa"), String::fromUTF8 (en ? "Internal synth" : "Intern\xc3\xbd syntez\xc3\xa1tor"), "Robot", String::fromUTF8 (en ? "Robot harmony" : "Robot trojhlas") };
        }
        mode->range.interval = 1.0f;
        mode->wide = true;
        bands   = add ("bands", en ? "Bands" : "Pásma", (float) V::kMinBands, (float) V::kMaxBands, robot ? 20 : 32, "", 1.0f, 0);
        bands->range.interval = 1.0f;
        formant = add ("formant", "Formant", -12, 12, 0, "st", 1.0f, 1);
        formant->textFn = [] (float x) { return String::fromUTF8 ((x >= 0.05f ? "+" : "")) + String (x, 1) + " st"; };
        clarity = add ("clarity", en ? "Clarity" : "Zrozumiteľnosť", 0, 100, 60, "%", 1.0f, 0);
        release = add ("release", en ? "Release" : "Dozvuk", 0, 1, 0.35f, "", 1.0f, 2);
        release->textFn = [] (float x) { char b[24]; V::formatParam (4, x, b, 24); return String (b); };
        mix     = add ("mix", "Mix", 0, 100, robot ? 55 : 100, "%", 1.0f, 0);
        tail = 0.2;
    }

    int modeIdx() const { return robot ? 2 + jlimit (0, 1, roundToInt (mode->get())) : jlimit (0, 3, roundToInt (mode->get())); }
    const bool robot;
    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<VocoderDisplay> (*this); }
    int displayHeight() const override { return 136; }
    bool acceptsMidi() const override { return true; }

    int keyOf() const { return songKey; }
    int scaleOf() const { return songScale; }
    korgus::KorgusVocoder voc;
    FxParam *mode, *bands, *formant, *clarity, *release, *mix;
    std::atomic<bool> uiMissing { false };
    std::atomic<int> uiVoices { 0 };

private:
    struct Ev { int t, type, a; float v; };   // 0 nota vypnuta, 1 nota zapnuta, 2 vsetky noty vypnute, 3 akord (a = index)

    void prepare() override
    {
        voc.prepare (rate, (int) spec.maximumBlockSize);
        midiOn.fill (false); chordOn.fill (false); midiCnt = 0;
        setLatencySamples (0);
    }

    int chordIndexAt (double step) const
    {
        if (songChords == nullptr || songChords->empty()) return -1;
        auto it = std::upper_bound (songChords->begin(), songChords->end(), step + 1.0e-6,
                                    [] (double v, const BuiltinFx::ChordSpan& c) { return v < c.step; });
        return (int) (it - songChords->begin()) - 1;
    }

    void setChord (int idx)
    {
        std::array<bool, 128> want {};
        if (idx >= 0 && songChords != nullptr && idx < (int) songChords->size() && midiCnt == 0)
            for (int p : (*songChords)[(size_t) idx].notes) if (p >= 0 && p < 128) want[(size_t) p] = true;
        for (int p = 0; p < 128; ++p)
        {
            const auto k = (size_t) p;
            if (chordOn[k] && ! want[k]) { chordOn[k] = false; if (! midiOn[k]) voc.noteOff (p); }
            else if (! chordOn[k] && want[k]) { chordOn[k] = true; if (! midiOn[k]) voc.noteOn (p, 0.8f); }
        }
    }

    void apply (const Ev& e)
    {
        switch (e.type)
        {
            case 0: if (isPositiveAndBelow (e.a, 128)) { midiOn[(size_t) e.a] = false; if (! chordOn[(size_t) e.a]) voc.noteOff (e.a); } break;
            case 1: if (isPositiveAndBelow (e.a, 128)) { midiOn[(size_t) e.a] = true; voc.noteOn (e.a, e.v); } break;
            case 2: midiOn.fill (false); chordOn.fill (false); voc.allNotesOff(); break;
            case 3: setChord (e.a); break;
            default: break;
        }
        midiCnt = 0;
        for (bool b : midiOn) midiCnt += b ? 1 : 0;
    }

    void process (AudioBuffer<float>& b) override
    {
        using V = korgus::KorgusVocoder;
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        float* L = b.getWritePointer (0);
        float* R = ch > 1 ? b.getWritePointer (1) : nullptr;
        const int md = modeIdx();

        voc.setParams (md, V::bandsTo01 (roundToInt (bands->get())), V::semitonesToFormant (formant->get()),
                       clarity->get() * 0.01f, release->get(), mix->get() * 0.01f);
        if (songKey >= 0) voc.setKey (songKey, songScale >= 0 ? songScale : 0);
        else voc.setKey (0, -1);

        // MIDI stopy (vlastne noty, klavesy) a akordy piesne -> udalosti v bloku
        int nev = 0;
        auto push = [&nev, this] (int t, int type, int a, float v) { if (nev < (int) evs.size()) evs[(size_t) nev++] = { t, type, a, v }; };
        if (curMidi != nullptr)
            for (const auto meta : *curMidi)
            {
                const auto m = meta.getMessage();
                const int t = jlimit (0, n - 1, meta.samplePosition);
                if (m.isNoteOn()) push (t, 1, m.getNoteNumber(), m.getFloatVelocity());
                else if (m.isNoteOff()) push (t, 0, m.getNoteNumber(), 0.0f);
                else if (m.isAllNotesOff() || m.isAllSoundOff()) push (t, 2, 0, 0.0f);
            }

        bool playing = false;
        double step0 = 0, bpm = 120;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                playing = pos->getIsPlaying();
                if (auto q = pos->getPpqPosition()) step0 = *q * 4.0;
                if (auto v = pos->getBpm()) bpm = jlimit (20.0, 400.0, *v);
            }
        const bool chords = md == 1 && playing;
        const int idx0 = chords ? chordIndexAt (step0) : -1;
        if (chords && songChords != nullptr)
        {
            const double stepsPerSample = bpm / 15.0 / rate;
            const double end = step0 + n * stepsPerSample;
            for (int k = idx0 + 1; k < (int) songChords->size() && (*songChords)[(size_t) k].step < end; ++k)
                push (jlimit (0, n - 1, (int) (((*songChords)[(size_t) k].step - step0) / stepsPerSample)), 3, k, 0.0f);
        }
        for (int i = 1; i < nev; ++i)   // radenie podla casu (stabilne)
        {
            const Ev x = evs[(size_t) i];
            int j = i - 1;
            while (j >= 0 && evs[(size_t) j].t > x.t) { evs[(size_t) j + 1] = evs[(size_t) j]; --j; }
            evs[(size_t) j + 1] = x;
        }

        const bool haveKey = scL != nullptr && scN >= n;
        int pos = 0;
        auto run = [&] (int to)
        {
            if (to <= pos) return;
            voc.process (L + pos, R != nullptr ? R + pos : nullptr, haveKey ? scL + pos : nullptr, haveKey ? scR + pos : nullptr,
                         L + pos, R != nullptr ? R + pos : nullptr, to - pos);
            pos = to;
        };
        setChord (idx0);   // zaciatok bloku: akord podla polohy (alebo ticho, ked sa nehra)
        for (int i = 0; i < nev; ++i) { run (evs[(size_t) i].t); apply (evs[(size_t) i]); }
        run (n);

        uiMissing = md == 0 && voc.sidechainMissing();
        uiVoices = voc.getActiveVoices();
        tail = voc.getTailSeconds();
    }

    std::array<Ev, 512> evs {};
    std::array<bool, 128> midiOn {}, chordOn {};
    int midiCnt = 0;
};

VocoderDisplay::VocoderDisplay (VocoderFx& f) : fx (f)
{
    hSidechain  = sk ("Vyber stopu so spevom ako kľúč (sidechain)");
    hSidechain2 = sk ("Pravý klik na stopu so spevom → Kľúčová stopa (sidechain). Efekt dáš na stopu s nástrojom (nosič).");
    hNotes      = sk ("Zahraj akordy (MIDI) alebo zvoľ Robot");
    hNotes2     = sk ("Noty: MIDI na tejto stope, klávesy (vybraná stopa) alebo akordová stopa piesne. Efekt patrí na stopu so spevom.");
    hRobot      = sk ("Robot: nosič sleduje výšku hlasu");
    hRobot2     = sk ("Tónina piesne: ");
    hTrack2     = sk ("Hlas zo stopy označenej ako kľúčová; jej fader ho neovplyvní (kľúč ide pred hlasitosťou).");
    startTimerHz (30);
}

void VocoderDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (mvcol::panel);
    g.fillRoundedRectangle (r, 3.0f);

    const int md = fx.modeIdx();
    String h1, h2;
    bool warn = false;
    if (md == 0)
    {
        if (fx.uiMissing.load()) { h1 = hSidechain; h2 = hSidechain2; warn = true; }
        else { h1 = sk ("Nosič: zvuk tejto stopy, modulátor: kľúčová stopa (spev)"); h2 = hTrack2; }
    }
    else if (md == 1)
    {
        if (fx.uiVoices.load() == 0) { h1 = hNotes; warn = true; }
        else h1 = sk ("Interný syntezátor hrá");
        h2 = hNotes2;
    }
    else
    {
        h1 = md == 3 ? sk ("Robot trojhlas: hlas + terca + kvinta") : hRobot;
        h2 = hRobot2;
        const int note = fx.voc.getRobotNote();
        static const char* const nm[12] = { "C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B" };
        if (fx.keyOf() >= 0) h2 << String (nm[fx.keyOf() % 12]) << (fx.scaleOf() == 1 ? sk (" moll") : sk (" dur"));
        else h2 << sk ("neznáma (voľná výška)");
        if (note >= 0) h1 << "  (" << nm[note % 12] << String (note / 12 - 1) << ")";
    }
    auto area = r.reduced (10, 8).toNearestInt();
    g.setColour (warn ? mvcol::gold : mvcol::text);
    g.setFont (FontOptions (14.0f).withStyle ("Bold"));
    g.drawFittedText (h1, area.removeFromTop (20), Justification::centredLeft, 1);
    g.setColour (mvcol::dim);
    g.setFont (FontOptions (11.0f));
    g.drawFittedText (h2, area.removeFromTop (30), Justification::topLeft, 2);

    // hladiny pasiem modulatora
    area.removeFromTop (4);
    const int nb = jmax (1, fx.voc.getNumBands());
    const float bw = (float) area.getWidth() / (float) nb;
    for (int i = 0; i < nb; ++i)
    {
        const float lv = jlimit (0.0f, 1.0f, std::sqrt (fx.voc.getBandLevel (i)) * 0.6f);
        const float hgt = jmax (2.0f, lv * (float) area.getHeight());
        auto c = Rectangle<float> ((float) area.getX() + bw * (float) i + 1.0f, (float) area.getBottom() - hgt, jmax (1.0f, bw - 2.0f), hgt);
        g.setColour (mvcol::steel.interpolatedWith (mvcol::gold, lv * lv));
        g.fillRoundedRectangle (c, jmin (3.0f, c.getWidth() * 0.5f));
    }
}


//==============================================================================
// Korgus Gater - rytmicke sekanie zvuku podla tempa (aj na audio stopu, napr. spev alebo pad)
class GaterFx final : public FxBase
{
public:
    GaterFx() : FxBase ("Korgus Gater", "gater")
    {
        const bool en = appLang() == 1;
        pat = add ("pattern", en ? "Pattern" : "Rytmus", 0, (float) (korgus::GatePatterns::count - 1), 0, "", 1.0f, 0);
        pat->range.interval = 1.0f; pat->wide = true;
        for (int k = 0; k < korgus::GatePatterns::count; ++k)
            pat->choices.add (en ? String (korgus::GatePatterns::nameEn (k)) : String::fromUTF8 (korgus::GatePatterns::nameSk (k)));
        rateP = add ("rate", en ? "Step" : "Krok", 0, 4, 3, "", 1.0f, 0);
        rateP->range.interval = 1.0f;
        rateP->choices = StringArray { "1/4", "1/8", "1/8T", "1/16", "1/32" };
        depth  = add ("depth", en ? "Depth" : "Hĺbka", 0, 100, 85, "%", 1.0f, 0);
        smooth = add ("smooth", en ? "Smooth" : "Hladkosť", 0, 100, 25, "%", 1.0f, 0);
        swing  = add ("swing", "Swing", 0, 50, 0, "%", 1.0f, 0);
        on     = add ("active", en ? "On" : "Zapnuté", 0, 1, 1, "", 1.0f, 0);
        on->toggle = true;
    }

private:
    void prepare() override { freePpq = 0; g = 1; r = 0; }

    void process (AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        double bpm = 120, ppq = freePpq;
        if (auto* ph = getPlayHead())
            if (auto pos = ph->getPosition())
            {
                if (auto v = pos->getBpm()) bpm = jlimit (20.0, 400.0, *v);
                if (pos->getIsPlaying()) if (auto q = pos->getPpqPosition()) ppq = *q;
            }
        static const double stepB[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
        const double sl = stepB[jlimit (0, 4, roundToInt (rateP->get()))];
        const char* bits = korgus::GatePatterns::bits (jlimit (0, korgus::GatePatterns::count - 1, roundToInt (pat->get())));
        bool pt[16]; for (int k = 0; k < 16; ++k) pt[k] = bits[k] == '1';
        const float dep = on->get() > 0.5f ? depth->get() * 0.01f : 0.0f;
        const float sw = swing->get() * 0.01f;
        const float a = 1.0f - (float) std::exp (-1.0 / ((0.0005 + 0.025 * smooth->get() * 0.01) * rate));
        const float ga = 1.0f - (float) std::exp (-1.0 / (0.0015 * rate));
        const double dq = bpm / 60.0 / rate;
        for (int i = 0; i < n; ++i)
        {
            r += a * (korgus::gateTarget (ppq + i * dq, sl, pt, sw) - r);
            const float tgt = 1.0f - dep * (1.0f - r);
            g += ga * (tgt - g);
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= g;
        }
        freePpq = ppq + n * dq;
        if (freePpq > 1.0e6) freePpq = 0;
    }

    FxParam *pat, *rateP, *depth, *smooth, *swing, *on;
    double freePpq = 0;
    float g = 1, r = 0;
};


//==============================================================================
// Korgus Hlas: dvojhlas a robot hlas v jednom efekte (rezim Dvojhlas / Robot / Robot + dvojhlas)
class VoiceFx final : public FxBase
{
public:
    VoiceFx() : FxBase ("Korgus Hlas", "voice")
    {
        vmode = add ("vmode", "Efekt", 0, 2, 0, "", 1.0f, 0);
        vmode->range.interval = 1.0f; vmode->inEditor = false;
        vmode->choices = StringArray { sk ("Dvojhlas"), sk ("Robot"), sk ("Robot + dvojhlas") };
        tail = 0.3;
    }
    int modeIdx() const { return jlimit (0, 2, roundToInt (vmode->get())); }
    HarmonyFx harm; VocoderFx robot { true };
    FxParam* vmode;
    AudioProcessorEditor* createEditor() override;
    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX"); x.setAttribute ("type", "voice"); x.setAttribute ("vmode", modeIdx());
        MemoryBlock a, b; harm.getStateInformation (a); robot.getStateInformation (b);
        x.setAttribute ("harm", a.toBase64Encoding()); x.setAttribute ("robot", b.toBase64Encoding());
        copyXmlToBinary (x, mb);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            vmode->setValueNotifyingHost (vmode->range.convertTo0to1 ((float) x->getIntAttribute ("vmode", 0)));
            MemoryBlock a, b;
            if (a.fromBase64Encoding (x->getStringAttribute ("harm")) && a.getSize() > 0) harm.setStateInformation (a.getData(), (int) a.getSize());
            if (b.fromBase64Encoding (x->getStringAttribute ("robot")) && b.getSize() > 0) robot.setStateInformation (b.getData(), (int) b.getSize());
        }
    }
private:
    void prepare() override
    {
        for (FxBase* f : { (FxBase*) &harm, (FxBase*) &robot }) { f->setPlayConfigDetails (2, 2, rate, (int) spec.maximumBlockSize); f->prepareToPlay (rate, (int) spec.maximumBlockSize); }
        setLatencySamples (jmax (harm.getLatencySamples(), robot.getLatencySamples()));
    }
    void process (AudioBuffer<float>& b) override
    {
        MidiBuffer none; const int m = modeIdx();
        for (FxBase* f : { (FxBase*) &harm, (FxBase*) &robot }) { f->setPlayHead (getPlayHead()); f->setSong (songKey, songScale, songChords); f->setKey (scL, scR, scN); }
        if (m >= 1) robot.processBlock (b, none);
        if (m != 1) harm.processBlock (b, none);
    }
};

// okno Korgus Hlas: prepinac efektu hore, pod nim okno zvoleneho efektu
class VoiceEditor final : public AudioProcessorEditor, private Timer
{
public:
    explicit VoiceEditor (VoiceFx& f) : AudioProcessorEditor (f), fx (f)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& b = btn[i]; b.setButtonText (fx.vmode->choices[i]); b.setClickingTogglesState (false);
            b.setColour (TextButton::buttonColourId, Colour (0xff2a3442)); b.setColour (TextButton::buttonOnColourId, Colour (0xff2d4362));
            b.setColour (TextButton::textColourOffId, Colour (0xffc9d2dc)); b.setColour (TextButton::textColourOnId, Colours::white);
            b.onClick = [this, i] { fx.vmode->beginChangeGesture(); fx.vmode->setFromUi ((float) i); fx.vmode->endChangeGesture(); rebuild(); };
            addAndMakeVisible (b);
        }
        rebuild(); startTimerHz (5);
    }
    ~VoiceEditor() override { stopTimer(); }
    void paint (Graphics& g) override { g.fillAll (Colour (0xff151c27)); }
    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (kTop).reduced (8, 6).withTrimmedRight (28);   // vpravo ikona velkosti
        const int w = r.getWidth() / 3;
        for (int i = 0; i < 3; ++i) btn[i].setBounds (r.removeFromLeft (w).reduced (2, 0));
        int y = kTop;
        for (auto& e : eds) if (e != nullptr) { e->setTopLeftPosition (0, y); y += e->getHeight(); }
    }
private:
    static constexpr int kTop = 40;
    void timerCallback() override { if (cur != fx.modeIdx()) rebuild(); }
    void rebuild()
    {
        cur = fx.modeIdx();
        for (int i = 0; i < 3; ++i) btn[i].setToggleState (i == cur, dontSendNotification);
        for (auto& e : eds) if (e != nullptr) removeChildComponent (e.get());
        eds[0].reset(); eds[1].reset();
        if (cur >= 1) eds[0].reset (fx.robot.createEditor());
        if (cur != 1) eds[1].reset (fx.harm.createEditor());
        int w = 460, h = kTop;
        for (auto& e : eds) if (e != nullptr) { addAndMakeVisible (*e); w = jmax (w, e->getWidth()); h += e->getHeight(); }
        setSize (w, h); resized();
    }
    VoiceFx& fx; TextButton btn[3]; int cur = -1;
    std::unique_ptr<AudioProcessorEditor> eds[2];
};
AudioProcessorEditor* VoiceFx::createEditor() { return new VoiceEditor (*this); }

//==============================================================================
// Korgus Dynamika: kompresor a limiter v jednom efekte (rezim Kompresor / Limiter / Kompresor + limiter)
class DynFx final : public FxBase
{
public:
    DynFx() : FxBase ("Korgus Dynamika", "dyn")
    {
        vmode = add ("vmode", "Efekt", 0, 2, 0, "", 1.0f, 0);
        vmode->range.interval = 1.0f; vmode->inEditor = false;
        vmode->choices = StringArray { sk ("Kompresor"), sk ("Limiter"), sk ("Kompresor + limiter") };
        tail = 0.0;
    }
    int modeIdx() const { return jlimit (0, 2, roundToInt (vmode->get())); }
    CompFx comp; LimiterFx lim;
    FxParam* vmode;
    AudioProcessorEditor* createEditor() override;
    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX"); x.setAttribute ("type", "dyn"); x.setAttribute ("vmode", modeIdx());
        MemoryBlock a, b; comp.getStateInformation (a); lim.getStateInformation (b);
        x.setAttribute ("comp", a.toBase64Encoding()); x.setAttribute ("lim", b.toBase64Encoding());
        copyXmlToBinary (x, mb);
    }
    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
        {
            vmode->setValueNotifyingHost (vmode->range.convertTo0to1 ((float) x->getIntAttribute ("vmode", 0)));
            MemoryBlock a, b;
            if (a.fromBase64Encoding (x->getStringAttribute ("comp")) && a.getSize() > 0) comp.setStateInformation (a.getData(), (int) a.getSize());
            if (b.fromBase64Encoding (x->getStringAttribute ("lim")) && b.getSize() > 0) lim.setStateInformation (b.getData(), (int) b.getSize());
        }
    }
private:
    void prepare() override
    {
        for (FxBase* f : { (FxBase*) &comp, (FxBase*) &lim }) { f->setPlayConfigDetails (2, 2, rate, (int) spec.maximumBlockSize); f->prepareToPlay (rate, (int) spec.maximumBlockSize); }
        setLatencySamples (jmax (comp.getLatencySamples(), lim.getLatencySamples()));
    }
    void process (AudioBuffer<float>& b) override
    {
        MidiBuffer none; const int m = modeIdx();
        for (FxBase* f : { (FxBase*) &comp, (FxBase*) &lim }) { f->setPlayHead (getPlayHead()); f->setSong (songKey, songScale, songChords); f->setKey (scL, scR, scN); }
        if (m != 1) comp.processBlock (b, none);
        if (m >= 1) lim.processBlock (b, none);
    }
};

// okno Korgus Hlas: prepinac efektu hore, pod nim okno zvoleneho efektu
class DynEditor final : public AudioProcessorEditor, private Timer
{
public:
    explicit DynEditor (DynFx& f) : AudioProcessorEditor (f), fx (f)
    {
        for (int i = 0; i < 3; ++i)
        {
            auto& b = btn[i]; b.setButtonText (fx.vmode->choices[i]); b.setClickingTogglesState (false);
            b.setColour (TextButton::buttonColourId, Colour (0xff2a3442)); b.setColour (TextButton::buttonOnColourId, Colour (0xff2d4362));
            b.setColour (TextButton::textColourOffId, Colour (0xffc9d2dc)); b.setColour (TextButton::textColourOnId, Colours::white);
            b.onClick = [this, i] { fx.vmode->beginChangeGesture(); fx.vmode->setFromUi ((float) i); fx.vmode->endChangeGesture(); rebuild(); };
            addAndMakeVisible (b);
        }
        rebuild(); startTimerHz (5);
    }
    ~DynEditor() override { stopTimer(); }
    void paint (Graphics& g) override { g.fillAll (Colour (0xff151c27)); }
    void resized() override
    {
        auto r = getLocalBounds().removeFromTop (kTop).reduced (8, 6).withTrimmedRight (28);   // vpravo ikona velkosti
        const int w = r.getWidth() / 3;
        for (int i = 0; i < 3; ++i) btn[i].setBounds (r.removeFromLeft (w).reduced (2, 0));
        int y = kTop;
        for (auto& e : eds) if (e != nullptr) { e->setTopLeftPosition (0, y); y += e->getHeight(); }
    }
private:
    static constexpr int kTop = 40;
    void timerCallback() override { if (cur != fx.modeIdx()) rebuild(); }
    void rebuild()
    {
        cur = fx.modeIdx();
        for (int i = 0; i < 3; ++i) btn[i].setToggleState (i == cur, dontSendNotification);
        for (auto& e : eds) if (e != nullptr) removeChildComponent (e.get());
        eds[0].reset(); eds[1].reset();
        if (cur != 1) eds[0].reset (fx.comp.createEditor());
        if (cur >= 1) eds[1].reset (fx.lim.createEditor());
        int w = 460, h = kTop;
        for (auto& e : eds) if (e != nullptr) { addAndMakeVisible (*e); w = jmax (w, e->getWidth()); h += e->getHeight(); }
        setSize (w, h); resized();
    }
    DynFx& fx; TextButton btn[3]; int cur = -1;
    std::unique_ptr<AudioProcessorEditor> eds[2];
};
AudioProcessorEditor* DynFx::createEditor() { return new DynEditor (*this); }

struct Entry { const char* key; const char* name; std::function<std::unique_ptr<AudioPluginInstance>()> make; bool inst = false; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e {
        { "eq",      "Korgus EQ",        [] { return std::unique_ptr<AudioPluginInstance> (new EqFx()); } },
        { "dyn",     "Korgus Dynamika",  [] { return std::unique_ptr<AudioPluginInstance> (new DynFx()); } },
        { "comp",    "Korgus Kompresor", [] { return std::unique_ptr<AudioPluginInstance> (new CompFx()); } },
        { "reverb",  "Korgus Dozvuk",    [] { return std::unique_ptr<AudioPluginInstance> (new ReverbFx()); } },
        { "delay",   "Korgus Ozvena",    [] { return std::unique_ptr<AudioPluginInstance> (new DelayFx()); } },
        { "limiter", "Korgus Limiter",   [] { return std::unique_ptr<AudioPluginInstance> (new LimiterFx()); } },
        { "pulse",   "Korgus PulseDip",  [] { return std::unique_ptr<AudioPluginInstance> (new PulseFx()); } },
        { "voice",   "Korgus Hlas",      [] { return std::unique_ptr<AudioPluginInstance> (new VoiceFx()); } },
        { "harmony", "Korgus Dvojhlas",  [] { return std::unique_ptr<AudioPluginInstance> (new HarmonyFx()); } },
        { "deesser", "Korgus De-esser",  [] { return std::unique_ptr<AudioPluginInstance> (new DeEsserFx()); } },
        { "sampler", "Korgus Sampler",   [] { return std::unique_ptr<AudioPluginInstance> (new SamplerInst()); }, true },
        { "arp",     "Korgus Arp",       [] { return std::unique_ptr<AudioPluginInstance> (new ArpFx()); } },
        { "master",  "Korgus Master",    [] { return std::unique_ptr<AudioPluginInstance> (new MasterFx()); } },
        { "vocoder", "Korgus Vocoder",   [] { return std::unique_ptr<AudioPluginInstance> (new VocoderFx()); } },
        { "robotvox", "Korgus Robot hlas", [] { return std::unique_ptr<AudioPluginInstance> (new VocoderFx (true)); } },
        { "gater",   "Korgus Gater",     [] { return std::unique_ptr<AudioPluginInstance> (new GaterFx()); } },
        { "meter",   "Korgus Meter",     [] { return std::unique_ptr<AudioPluginInstance> (new MeterFx()); } },
    };
    return e;
}
} // namespace

namespace BuiltinFx
{
juce::Array<juce::PluginDescription> descriptions()
{
    juce::Array<juce::PluginDescription> a;
    // arp a vocoder maju vlastne miesto; gater nahradil PulseDip (Kroky) - ostava len kvoli starym piesnam
    for (const auto& e : entries()) if (juce::String (e.key) != "arp" && juce::String (e.key) != "vocoder" && juce::String (e.key) != "gater"
                                          && juce::String (e.key) != "harmony" && juce::String (e.key) != "robotvox" && juce::String (e.key) != "meter"
                                          && juce::String (e.key) != "comp" && juce::String (e.key) != "limiter")
            a.add (FxBase::describe (e.name, e.key, e.inst));   // dvojhlas, robot hlas a merac su zlucene do Korgus Hlas / Korgus Master
    return a;
}

juce::PluginDescription arpDescription() { auto d = FxBase::describe ("Korgus Arp", "arp"); d.category = "MIDI"; return d; }

void setSidechain (juce::AudioPluginInstance* p, const float* l, const float* r, int n)
{
    if (auto* f = dynamic_cast<FxBase*> (p)) f->setKey (l, r, n);
}

void setSong (juce::AudioPluginInstance* p, int keyTonic, int keyScale, const std::vector<ChordSpan>* chords)
{
    if (auto* f = dynamic_cast<FxBase*> (p)) f->setSong (keyTonic, keyScale, chords);
}

juce::Array<juce::AudioProcessorParameter*> extraParameters (juce::AudioPluginInstance* p)
{
    if (auto* d = dynamic_cast<DynFx*> (p)) return d->comp.getParameters();
    if (auto* v = dynamic_cast<VoiceFx*> (p)) return v->harm.getParameters();
    return {};
}

void newSong (juce::AudioPluginInstance* p)
{
    if (auto* f = dynamic_cast<FxBase*> (p)) f->newSong();
}

bool wantsExportPrepass (juce::AudioPluginInstance* p)
{
    auto* f = dynamic_cast<FxBase*> (p);
    return f != nullptr && f->wantsPrepass();
}

void setExportPhase (juce::AudioPluginInstance* p, int phase)
{
    if (auto* f = dynamic_cast<FxBase*> (p)) f->exportPhase (phase);
}

bool isBuiltin (const juce::PluginDescription& d) { return d.pluginFormatName == formatName && d.fileOrIdentifier.startsWith ("korgus:"); }

std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription& d)
{
    if (! isBuiltin (d)) return nullptr;
    const auto key = d.fileOrIdentifier.fromFirstOccurrenceOf ("korgus:", false, false);
    for (const auto& e : entries())
        if (key == e.key) return e.make();
    return nullptr;
}
}
