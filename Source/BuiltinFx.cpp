#include "BuiltinFx.h"
#include "UiCommon.h"
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
    std::function<String (float)> textFn;    // vlastny text hodnoty
    std::atomic<float> value { 0 };
};

//==============================================================================
class FxBase;

class KnobEditor final : public AudioProcessorEditor, private Timer
{
public:
    KnobEditor (FxBase& p, const std::vector<FxParam*>& ps);
    ~KnobEditor() override { stopTimer(); }

    void paint (Graphics& g) override
    {
        g.fillAll (Colour (0xff1f2630));
        g.setColour (Colour (0xffc9d2dc));
        g.setFont (FontOptions (13.0f));
        for (size_t i = 0; i < labels.size(); ++i)
            if (! params[i]->toggle)
                g.drawFittedText (labels[i], comps[i]->getBounds().withY (top + 8).withHeight (18), Justification::centred, 1);
    }

    void resized() override
    {
        if (display != nullptr) display->setBounds (8, 8, getWidth() - 16, top - 12);
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (top + 28).withTrimmedBottom (8);
        const int w = r.getWidth() / jmax (1, (int) comps.size());
        for (size_t i = 0; i < comps.size(); ++i)
        {
            auto cell = r.removeFromLeft (w).reduced (4, 0);
            if (params[i]->toggle) comps[i]->setBounds (cell.withY (top + 4).withHeight (26));
            else if (! params[i]->choices.isEmpty()) comps[i]->setBounds (cell.withHeight (26).withY (cell.getCentreY() - 20));
            else comps[i]->setBounds (cell);
        }
    }

private:
    void timerCallback() override
    {
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
            else if (auto* t = dynamic_cast<ToggleButton*> (comps[i].get()))
                t->setToggleState (p->get() > 0.5f, dontSendNotification);
        }
    }

    std::vector<FxParam*> params;
    std::unique_ptr<Component> display;   // graf (napr. krivka PulseDip)
    int top = 0;
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

    void prepareToPlay (double sr, int bs) override
    {
        rate = sr;
        spec = { sr, (uint32) jmax (1, bs), (uint32) jmax (1, getTotalNumOutputChannels()) };
        prepare();
    }
    void releaseResources() override {}

    void processBlock (AudioBuffer<float>& buf, MidiBuffer&) override
    {
        ScopedNoDenormals nd;
        const int ch = jmin (buf.getNumChannels(), (int) spec.numChannels);
        if (ch <= 0 || buf.getNumSamples() == 0) return;
        AudioBuffer<float> view (buf.getArrayOfWritePointers(), ch, buf.getNumSamples());
        process (view);
    }

    double getTailLengthSeconds() const override { return tail; }
    bool acceptsMidi() const override  { return false; }
    bool producesMidi() const override { return false; }
    bool hasEditor() const override    { return true; }
    AudioProcessorEditor* createEditor() override { return new KnobEditor (*this, knobs); }
    virtual std::unique_ptr<Component> makeDisplay() { return nullptr; }
    virtual int displayHeight() const { return 150; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const String getProgramName (int) override { return {}; }
    void changeProgramName (int, const String&) override {}

    void getStateInformation (MemoryBlock& mb) override
    {
        XmlElement x ("KORGUSFX");
        x.setAttribute ("type", fxKey);
        for (auto* p : knobs) x.setAttribute (p->id, (double) p->get());
        copyXmlToBinary (x, mb);
    }

    void setStateInformation (const void* data, int size) override
    {
        if (auto x = getXmlFromBinary (data, size))
            for (auto* p : knobs)
                if (x->hasAttribute (p->id))
                    p->setValueNotifyingHost (p->range.convertTo0to1 ((float) x->getDoubleAttribute (p->id)));
    }

    void setKey (const float* l, const float* r, int n) { scL = l; scR = r != nullptr ? r : l; scN = l != nullptr ? n : 0; }

protected:
    virtual void prepare() = 0;
    const float* scL = nullptr; const float* scR = nullptr; int scN = 0;
    virtual void process (AudioBuffer<float>&) = 0;

    double rate = 44100;
    dsp::ProcessSpec spec { 44100, 512, 2 };
    double tail = 0;
    std::vector<FxParam*> knobs;

private:
    String fxName, fxKey;
    bool isInst = false;
};

KnobEditor::KnobEditor (FxBase& p, const std::vector<FxParam*>& ps) : AudioProcessorEditor (p)
{
    for (auto* q : ps) if (q->inEditor) params.push_back (q);
    const Colour text (0xffc9d2dc), steel (0xff2d4362), fill (0xff6f8fb3);
    for (auto* prm : params)
    {
        std::unique_ptr<Component> comp;
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
        addAndMakeVisible (*comp);
        labels.push_back (prm->name);
        comps.push_back (std::move (comp));
    }
    display = p.makeDisplay();
    if (display != nullptr) { top = p.displayHeight(); addAndMakeVisible (*display); }
    setSize (jmax (display != nullptr ? 460 : 260, 92 * (int) comps.size() + 16), 150 + top);
    startTimerHz (20);
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
    }

private:
    using Filter = dsp::ProcessorDuplicator<dsp::IIR::Filter<float>, dsp::IIR::Coefficients<float>>;

    void prepare() override
    {
        for (auto* f : { &hp, &ls, &pk, &hs }) { f->prepare (spec); f->reset(); }
        last.fill (-1e9f);
        update (true);
    }

    void update (bool force)
    {
        const std::array<float, 8> now { lowCut->get(), lowG->get(), lowF->get(), midG->get(), midF->get(), midQ->get(), highG->get(), highF->get() };
        if (! force && now == last) return;
        const double ny = rate * 0.45;
        *hp.state = *dsp::IIR::Coefficients<float>::makeHighPass (rate, jmin ((double) now[0], ny), 0.707f);
        *ls.state = *dsp::IIR::Coefficients<float>::makeLowShelf (rate, jmin ((double) now[2], ny), 0.707f, Decibels::decibelsToGain (now[1]));
        *pk.state = *dsp::IIR::Coefficients<float>::makePeakFilter (rate, jmin ((double) now[4], ny), now[5], Decibels::decibelsToGain (now[3]));
        *hs.state = *dsp::IIR::Coefficients<float>::makeHighShelf (rate, jmin ((double) now[7], ny), 0.707f, Decibels::decibelsToGain (now[6]));
        last = now;
    }

    void process (AudioBuffer<float>& b) override
    {
        update (false);
        dsp::AudioBlock<float> blk (b);
        dsp::ProcessContextReplacing<float> ctx (blk);
        if (lowCut->get() > 20.5f) hp.process (ctx);
        if (std::abs (lowG->get()) > 0.01f) ls.process (ctx);
        if (std::abs (midG->get()) > 0.01f) pk.process (ctx);
        if (std::abs (highG->get()) > 0.01f) hs.process (ctx);
        const float g = Decibels::decibelsToGain (outG->get());
        b.applyGainRamp (0, b.getNumSamples(), lastOut, g);
        lastOut = g;
    }

    FxParam *lowCut, *lowG, *lowF, *midG, *midF, *midQ, *highG, *highF, *outG;
    Filter hp, ls, pk, hs;
    std::array<float, 8> last {};
    float lastOut = 1.0f;
};

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
    }

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
        for (int i = 0; i < n; ++i)
        {
            const float x = jmax (std::abs (dL[i]), std::abs (dR[i]));
            env = x > env ? x + (env - x) * aA : x + (env - x) * aR;
            const float lev = Decibels::gainToDecibels (env, -120.0f), over = lev - t;
            float gr = 0;
            if (over > knee * 0.5f) gr = over * slope;
            else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; gr = slope * k * k / (2.0f * knee); }
            const float g = Decibels::decibelsToGain (-gr) * (mg0 + (mg1 - mg0) * (float) i / (float) n);
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= g;
        }
        lastG = mg1;
        if (m < 0.999f)
            for (int c = 0; c < ch; ++c)
            {
                b.applyGain (c, 0, n, m);
                b.addFrom (c, 0, dry, c, 0, n, 1.0f - m);
            }
    }

    FxParam *thr, *rat, *att, *rel, *mak, *mix, *key;
    AudioBuffer<float> dry;
    float env = 0, lastG = 1.0f;
};

//==============================================================================
class ReverbFx final : public FxBase
{
public:
    ReverbFx() : FxBase ("Korgus Dozvuk", "reverb")
    {
        size = add ("size", "Veľkosť", 0, 100, 50, "%", 1.0f, 0);
        damp = add ("damping", "Tlmenie", 0, 100, 50, "%", 1.0f, 0);
        width = add ("width", "Šírka", 0, 100, 100, "%", 1.0f, 0);
        mix  = add ("mix", "Mix", 0, 100, 25, "%", 1.0f, 0);
        tail = 6.0;
    }

private:
    void prepare() override { rv.prepare (spec); rv.reset(); }

    void process (AudioBuffer<float>& b) override
    {
        dsp::Reverb::Parameters p;
        p.roomSize = size->get() * 0.01f; p.damping = damp->get() * 0.01f; p.width = width->get() * 0.01f;
        const float m = mix->get() * 0.01f;
        p.wetLevel = m; p.dryLevel = 1.0f - m;
        rv.setParameters (p);
        dsp::AudioBlock<float> blk (b);
        rv.process (dsp::ProcessContextReplacing<float> (blk));
    }

    FxParam *size, *damp, *width, *mix;
    dsp::Reverb rv;
};

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
    }

private:
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
            const int i0 = (int) rp, i1 = (i0 + 1) % size;
            const float fr = rp - (float) i0;
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

//==============================================================================
class LimiterFx final : public FxBase
{
public:
    LimiterFx() : FxBase ("Korgus Limiter", "limiter")
    {
        gain = add ("gain", "Zisk", 0, 24, 0, "dB");
        ceil = add ("ceiling", "Strop", -12, 0, -0.3f, "dB");
        rel  = add ("release", "Uvoľnenie", 1, 500, 80, "ms", 0.5f, 0);
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
    }
    std::atomic<float> grDb { 0.0f };   // aktualne stisenie pre zobrazenie
    std::unique_ptr<Component> makeDisplay() override;
    int displayHeight() const override { return 44; }

private:
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
        float peakGr = 0.0f;
        for (int i = 0; i < n; ++i)
        {
            float lo[2], hi[2], det = 0.0f;
            for (int c = 0; c < nch && c < 2; ++c)
            {
                xo.processSample (c, b.getSample (c, i), lo[c], hi[c]);
                det = jmax (det, std::abs (hi[c]));
            }
            env = det > env ? att * env + (1.0f - att) * det : relC * env + (1.0f - relC) * det;
            const float over = Decibels::gainToDecibels (env, -100.0f) - t;
            const float gr = over > 0.0f ? jmin (maxR, over * 0.75f) : 0.0f;
            peakGr = jmax (peakGr, gr);
            const float g = Decibels::decibelsToGain (-gr);
            for (int c = 0; c < nch && c < 2; ++c)
                b.setSample (c, i, lis ? hi[c] : lo[c] + hi[c] * g);
        }
        grDb.store (jmax (peakGr, grDb.load() * 0.85f));
    }

    FxParam *freq, *thr, *range, *listen;
    dsp::LinkwitzRileyFilter<float> xo;
    float lastF = 6000.0f, env = 0.0f, att = 0.9f, relC = 0.999f;
};

class DeEssMeter final : public Component, private Timer
{
public:
    explicit DeEssMeter (DeEsserFx& p) : fx (p) { startTimerHz (30); }
    ~DeEssMeter() override { stopTimer(); }
    void paint (Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (12, 10);
        g.setColour (Colour (0xffe4e9ef));
        g.setFont (FontOptions (13.0f));
        g.drawText (sk ("Stíšenie sykaviek"), r.removeFromLeft (130), Justification::centredLeft);
        const float v = jlimit (0.0f, 24.0f, fx.grDb.load());
        auto bar = r.reduced (0, 4);
        g.setColour (Colour (0xff1c2430)); g.fillRoundedRectangle (bar, 3.0f);
        g.setColour (Colour (0xff6f8fb8)); g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * v / 24.0f), 3.0f);
        g.setColour (Colour (0xffe4e9ef));
        g.drawText ("-" + String (v, 1) + " dB", bar.reduced (6, 0), Justification::centredRight);
    }
private:
    void timerCallback() override { repaint(); }
    DeEsserFx& fx;
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

class PulseDisplay final : public Component, private Timer
{
public:
    explicit PulseDisplay (PulseFx& p) : fx (p) { startTimerHz (30); }
    ~PulseDisplay() override { stopTimer(); }
    void paint (Graphics& g) override;
    void mouseDown (const MouseEvent& e) override;
    void mouseMove (const MouseEvent& e) override { const int h = cellAt (e.position); if (h != hover) { hover = h; repaint(); } }
    void mouseExit (const MouseEvent&) override { hover = -1; repaint(); }
private:
    static constexpr int gridH = 84;   // 2 riadky ikon tvarov
    Rectangle<float> cellRect (int i) const
    {
        auto g = getLocalBounds().toFloat().removeFromBottom ((float) gridH).reduced (6, 4);
        const float w = g.getWidth() / 8.0f, h = g.getHeight() / 2.0f;
        return { g.getX() + (float) (i % 8) * w + 2, g.getY() + (float) (i / 8) * h + 2, w - 4, h - 4 };
    }
    int cellAt (Point<float> p) const { for (int i = 0; i < 16; ++i) if (cellRect (i).contains (p)) return i; return -1; }
    void timerCallback() override { repaint(); }
    PulseFx& fx;
    int hover = -1;
};

class PulseFx final : public FxBase
{
public:
    PulseFx() : FxBase ("Korgus PulseDip", "pulse")
    {
        rateP = add ("rate", "Rytmus", 0, 3, 1, "", 1.0f, 0);
        rateP->range.interval = 1.0f;
        rateP->choices = StringArray { "1/8", "1/4", "1/2", "1/1" };
        shp = add ("shape", "Tvar", 0, 15, 1, "", 1.0f, 0);
        shp->range.interval = 1.0f;
        shp->inEditor = false;
        shp->choices = StringArray::fromTokens (sk ("Mäkký nábeh|Klasika|Neskorý nástup|Hlboká pumpa|Rýchly nábeh|Opačne|Mäkký pokles|"
                                                                 "Vrchol|Dlhé podržanie|Plynulý nábeh|Tvrdá pumpa|Brána|Plató|Svah|Údolie|Dvojitá"), "|", "");
        mix = add ("mix", "Hĺbka", 0, 100, 100, "%", 1.0f, 0);
        out = add ("output", "Výstup", -12, 12, 0, "dB");
        mode = add ("mode", "Riadenie", 0, 1, 0, "", 1.0f, 0);
        mode->range.interval = 1.0f;
        mode->choices = StringArray { String ("Tempo"), String ("Sidechain") };
        on  = add ("active", "Zapnuté", 0, 1, 1, "", 1.0f, 0);
        on->toggle = true;
    }

    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<PulseDisplay> (*this); }
    int displayHeight() const override { return 236; }

    std::atomic<float> phaseNow { 0 }, bpmNow { 120 };
    FxParam *rateP, *shp, *mix, *out, *mode, *on;
    std::atomic<bool> keyMode { false };

private:
    void prepare() override { freePpq = 0; gSm = 1; }

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
        static const double beats[4] = { 0.5, 1.0, 2.0, 4.0 };
        const double per = beats[jlimit (0, 3, roundToInt (rateP->get()))];
        const int s = roundToInt (shp->get());
        const float depth = on->get() > 0.5f ? mix->get() * 0.01f : 0.0f;
        const float og = Decibels::decibelsToGain (out->get());
        const double dq = bpm / 60.0 / rate;
        const float a = (float) std::exp (-1.0 / (0.002 * rate));   // 2 ms proti lupancom
        const bool useKey = mode->get() > 0.5f && scL != nullptr && scN >= n;
        keyMode = mode->get() > 0.5f;
        const float kA = (float) std::exp (-1.0 / (0.002 * rate)), kR = (float) std::exp (-1.0 / (0.180 * rate));
        for (int i = 0; i < n; ++i)
        {
            const double q = ppq + i * dq;
            const float f = (float) (q / per - std::floor (q / per));
            float shapeG;
            if (useKey)
            {
                const float x = jmax (std::abs (scL[i]), std::abs (scR[i]));
                kEnv = x > kEnv ? x + (kEnv - x) * kA : x + (kEnv - x) * kR;
                shapeG = 1.0f - jlimit (0.0f, 1.0f, (Decibels::gainToDecibels (kEnv, -120.0f) + 36.0f) / 30.0f);
            }
            else shapeG = Pulse::shape (s, f);
            const float target = (1.0f - depth * (1.0f - shapeG)) * og;
            gSm = target + (gSm - target) * a;
            for (int c = 0; c < ch; ++c) b.getWritePointer (c)[i] *= gSm;
        }
        freePpq = ppq + n * dq;
        if (freePpq > 1.0e6) freePpq = 0;
        phaseNow = (float) (ppq / per - std::floor (ppq / per));
        bpmNow = (float) bpm;
    }


    double freePpq = 0;
    float gSm = 1, kEnv = 0;
};

void PulseDisplay::mouseDown (const MouseEvent& e)
{
    const int i = cellAt (e.position);
    if (i < 0) return;
    fx.shp->beginChangeGesture();
    fx.shp->setValueNotifyingHost (fx.shp->range.convertTo0to1 ((float) i));
    fx.shp->endChangeGesture();
    repaint();
}

void PulseDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
    // ikony tvarov
    {
        const int cur = roundToInt (fx.shp->get());
        for (int i = 0; i < 16; ++i)
        {
            auto c = cellRect (i);
            g.setColour (i == cur ? Colour (0xff2d4362) : (i == hover ? Colour (0xff27313d) : Colour (0xff1f2630)));
            g.fillRoundedRectangle (c, 3.0f);
            if (i == cur) { g.setColour (Colour (0xff8fb0d0)); g.drawRoundedRectangle (c.reduced (0.5f), 3.0f, 1.0f); }
            auto a = c.reduced (5, 6);
            Path pth;
            const int W = jmax (2, (int) a.getWidth());
            for (int x = 0; x <= W; ++x)
            {
                const float f = (float) x / (float) W, py = a.getBottom() - Pulse::shape (i, f) * a.getHeight();
                if (x == 0) pth.startNewSubPath (a.getX(), py); else pth.lineTo (a.getX() + (float) x, py);
            }
            g.setColour (i == cur ? Colour (0xffe4e9ef) : Colour (0xff8fb0d0));
            g.strokePath (pth, PathStrokeType (1.4f));
        }
        r.removeFromBottom ((float) gridH);
    }
    auto area = r.reduced (10, 22);
    const int s = roundToInt (fx.shp->get());
    const float depth = fx.on->get() > 0.5f ? fx.mix->get() * 0.01f : 0.0f;
    // dve periody krivky
    Path fill, line;
    const int W = jmax (2, (int) area.getWidth());
    for (int x = 0; x <= W; ++x)
    {
        const float f = 2.0f * (float) x / (float) W;
        const float gv = 1.0f - depth * (1.0f - Pulse::shape (s, f));
        const float px = area.getX() + (float) x, py = area.getBottom() - gv * area.getHeight();
        if (x == 0) { line.startNewSubPath (px, py); fill.startNewSubPath (px, area.getBottom()); }
        else line.lineTo (px, py);
        fill.lineTo (px, py);
    }
    fill.lineTo (area.getRight(), area.getBottom());
    fill.closeSubPath();
    g.setColour (Colour (0xff2d4362).withAlpha (0.85f));
    g.fillPath (fill);
    g.setColour (Colour (0xff8fb0d0));
    g.strokePath (line, PathStrokeType (1.6f));
    g.setColour (Colour (0xff3a4656));
    g.drawVerticalLine ((int) area.getCentreX(), area.getY(), area.getBottom());
    // poloha prehravania
    const float ph = fx.phaseNow.load();
    const float gx = area.getX() + ph * 0.5f * area.getWidth();
    const float gy = area.getBottom() - (1.0f - depth * (1.0f - Pulse::shape (s, ph))) * area.getHeight();
    g.setColour (Colour (0xffc9d2dc));
    g.fillEllipse (gx - 3.5f, gy - 3.5f, 7.0f, 7.0f);
    g.setFont (FontOptions (12.0f));
    g.setColour (Colour (0xff8a96a3));
    g.drawText ("PULSEDIP", r.reduced (10, 4).removeFromTop (14), Justification::centredLeft);
    if (fx.keyMode.load())
        g.drawText (sk ("SIDECHAIN – riadi kľúčová stopa"), r.reduced (10, 4).removeFromTop (14), Justification::centredRight);
    else
    g.drawText (String (fx.bpmNow.load(), 1) + " BPM  /  " + fx.rateP->choices[roundToInt (fx.rateP->get())] + "  /  " + fx.shp->choices[hover >= 0 ? hover : s],
                r.reduced (10, 4).removeFromTop (14), Justification::centredRight);
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
    void resized() override { load.setBounds (getWidth() - 132, 6, 124, 24); }
    bool isInterestedInFileDrag (const StringArray& f) override;
    void filesDropped (const StringArray& f, int, int) override;
    void mouseDown (const MouseEvent&) override;
private:
    void timerCallback() override { repaint(); }
    void choose();
    SamplerInst& sm;
    TextButton load;
    std::unique_ptr<FileChooser> chooser;
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
    }

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
        const bool whole = mode->get() > 0.5f;
        int pos = 0;
        auto render = [&] (int from, int to)
        {
            for (auto& v : voices)
            {
                if (! v.on) continue;
                for (int i = from; i < to; ++i)
                {
                    if (v.pos >= sl - 2) { v.on = false; break; }
                    float e;
                    if (v.releasing) { v.env *= rCoef; e = v.env; if (e < 1.0e-4f) { v.on = false; break; } }
                    else { v.env = jmin (1.0f, v.env + 1.0f / aS); e = v.env; }
                    const int k = (int) v.pos;
                    const float fr = (float) (v.pos - k);
                    for (int c = 0; c < ch; ++c)
                    {
                        const float* d = smp.getReadPointer (jmin (c, sc - 1));
                        const float x0 = d[jmax (0, k - 1)], x1 = d[k], x2 = d[k + 1], x3 = d[jmin (sl - 1, k + 2)];
                        const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
                        buf.getWritePointer (c)[i] += (((c3 * fr + c2) * fr + c1) * fr + x1) * e * v.gain;
                    }
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
                Voice* v = nullptr;
                for (auto& x : voices) if (! x.on) { v = &x; break; }
                if (v == nullptr) { v = &voices[0]; for (auto& x : voices) if (x.age < v->age) v = &x; }
                const double semis = m.getNoteNumber() - root->get() + tune->get() / 100.0;
                v->on = true; v->releasing = false; v->note = m.getNoteNumber(); v->pos = 0; v->env = 0;
                v->step = std::pow (2.0, semis / 12.0) * sampleRate / rate;
                v->gain = g * (1.0f - vs + vs * m.getFloatVelocity());
                v->age = ++counter;
            }
            else if (m.isNoteOff() && ! whole)
            {
                for (auto& x : voices) if (x.on && ! x.releasing && x.note == m.getNoteNumber()) x.releasing = true;
            }
            else if (m.isAllNotesOff() || m.isAllSoundOff())
                for (auto& x : voices) x.releasing = true;
        }
        render (pos, n);
        lastActive = 0;
        for (auto& v : voices) if (v.on) ++lastActive;
    }

    int lastActive = 0;

private:
    void prepare() override { for (auto& v : voices) v.on = false; }
    void process (AudioBuffer<float>&) override {}

    struct Voice { bool on = false, releasing = false; int note = 60; double pos = 0, step = 1; float env = 0, gain = 1; uint32 age = 0; };
    std::array<Voice, 24> voices;
    uint32 counter = 0;
    FxParam *root, *tune, *att, *rel, *velS, *vol, *mode;
    std::shared_ptr<AudioBuffer<float>> sample;
    double sampleRate = 44100;
    MemoryBlock fileData;
    mutable SpinLock lock;
};

SamplerDisplay::SamplerDisplay (SamplerInst& s) : sm (s)
{
    load.setButtonText (sk ("Načítať zvuk…"));
    load.setColour (TextButton::buttonColourId, Colour (0xff2d4362));
    load.setColour (TextButton::textColourOffId, Colour (0xffe4e9ef));
    load.onClick = [this] { choose(); };
    addAndMakeVisible (load);
    startTimerHz (10);
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

void SamplerDisplay::mouseDown (const MouseEvent&) { if (sm.getSample() == nullptr) choose(); }

void SamplerDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
    g.setFont (FontOptions (12.0f));
    g.setColour (Colour (0xff8a96a6));
    g.drawText ("SAMPLER", r.reduced (10, 6).removeFromTop (18), Justification::centredLeft);
    auto smp = sm.getSample();
    auto area = r.reduced (10, 0).withTrimmedTop (38).withTrimmedBottom (10);
    if (smp == nullptr)
    {
        g.setColour (Colour (0xffc9d2dc));
        g.drawText (sk ("Pretiahni sem zvuk (wav, mp3…) alebo klikni na „Načítať zvuk…“"), area, Justification::centred);
        return;
    }
    g.setColour (Colour (0xffc9d2dc));
    g.drawText (sm.fileName, r.reduced (10, 6).removeFromTop (18).withTrimmedLeft (70).withTrimmedRight (140), Justification::centredLeft);
    const int W = jmax (2, (int) area.getWidth()), n = smp->getNumSamples();
    const float* d = smp->getReadPointer (0);
    g.setColour (Colour (0xff6f8fb3));
    const float mid = area.getCentreY(), hh = area.getHeight() * 0.5f;
    for (int x = 0; x < W; ++x)
    {
        const int a = (int) ((int64) x * n / W), b = jmax (a + 1, (int) ((int64) (x + 1) * n / W));
        float mn = 0, mx = 0;
        for (int i = a; i < b && i < n; i += jmax (1, (b - a) / 64)) { mn = jmin (mn, d[i]); mx = jmax (mx, d[i]); }
        g.drawVerticalLine ((int) area.getX() + x, mid - mx * hh, mid - mn * hh + 1.0f);
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
        len.onValueChange = [this] { set (arp.lenP, (float) len.getValue()); };
        for (auto* c : std::initializer_list<Component*> { &rate, &swing, &gate, &oct, &hold, &pat, &len, &grid, &modes }) addAndMakeVisible (c);
        setSize (840, 330);
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
        pat.setBounds (getWidth() - 100, 14, 84, 26);
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
    ComboBox oct;
    ToggleButton hold, pat;
    TextButton velB[3];
    Rectangle<int> left, mid;
    bool syncing = false;
};

AudioProcessorEditor* ArpFx::createEditor() { return new ArpEditor (*this); }

struct Entry { const char* key; const char* name; std::function<std::unique_ptr<AudioPluginInstance>()> make; bool inst = false; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e {
        { "eq",      "Korgus EQ",        [] { return std::unique_ptr<AudioPluginInstance> (new EqFx()); } },
        { "comp",    "Korgus Kompresor", [] { return std::unique_ptr<AudioPluginInstance> (new CompFx()); } },
        { "reverb",  "Korgus Dozvuk",    [] { return std::unique_ptr<AudioPluginInstance> (new ReverbFx()); } },
        { "delay",   "Korgus Ozvena",    [] { return std::unique_ptr<AudioPluginInstance> (new DelayFx()); } },
        { "limiter", "Korgus Limiter",   [] { return std::unique_ptr<AudioPluginInstance> (new LimiterFx()); } },
        { "pulse",   "Korgus PulseDip",  [] { return std::unique_ptr<AudioPluginInstance> (new PulseFx()); } },
        { "harmony", "Korgus Dvojhlas",  [] { return std::unique_ptr<AudioPluginInstance> (new HarmonyFx()); } },
        { "deesser", "Korgus De-esser",  [] { return std::unique_ptr<AudioPluginInstance> (new DeEsserFx()); } },
        { "sampler", "Korgus Sampler",   [] { return std::unique_ptr<AudioPluginInstance> (new SamplerInst()); }, true },
        { "arp",     "Korgus Arp",       [] { return std::unique_ptr<AudioPluginInstance> (new ArpFx()); } },
    };
    return e;
}
} // namespace

namespace BuiltinFx
{
juce::Array<juce::PluginDescription> descriptions()
{
    juce::Array<juce::PluginDescription> a;
    for (const auto& e : entries()) if (juce::String (e.key) != "arp") a.add (FxBase::describe (e.name, e.key, e.inst));
    return a;
}

juce::PluginDescription arpDescription() { auto d = FxBase::describe ("Korgus Arp", "arp"); d.category = "MIDI"; return d; }

void setSidechain (juce::AudioPluginInstance* p, const float* l, const float* r, int n)
{
    if (auto* f = dynamic_cast<FxBase*> (p)) f->setKey (l, r, n);
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
