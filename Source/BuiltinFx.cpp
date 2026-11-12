#include "BuiltinFx.h"
#include <juce_dsp/juce_dsp.h>

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

    FxParam* add (String id, const char* utf8Name, float lo, float hi, float def, String u, float skew = 1.0f, int dec = 1)
    {
        auto p = std::make_unique<FxParam> (std::move (id), String::fromUTF8 (utf8Name), lo, hi, def, std::move (u), skew, dec);
        auto* raw = p.get();
        addHostedParameter (std::move (p));
        knobs.push_back (raw);
        return raw;
    }

    static PluginDescription describe (const String& name, const String& key)
    {
        PluginDescription d;
        d.name = name; d.descriptiveName = name;
        d.pluginFormatName = BuiltinFx::formatName;
        d.category = "Effect";
        d.manufacturerName = "Korgus";
        d.version = "1.0";
        d.fileOrIdentifier = "korgus:" + key;
        d.uniqueId = d.deprecatedUid = (int) (String ("korgus:" + key).hashCode() & 0x7fffffff);
        d.isInstrument = false;
        d.numInputChannels = 2; d.numOutputChannels = 2;
        return d;
    }

    void fillInPluginDescription (PluginDescription& d) const override { d = describe (fxName, fxKey); }
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
};

KnobEditor::KnobEditor (FxBase& p, const std::vector<FxParam*>& ps) : AudioProcessorEditor (p), params (ps)
{
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
            for (int i = 0; i < prm->choices.size(); ++i) c->addItem (prm->choices[i], i + 1);
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
    if (display != nullptr) { top = 150; addAndMakeVisible (*display); }
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
        key->choices = StringArray { String::fromUTF8 ("Vlastný signál"), String::fromUTF8 ("Sidechain (kľúčová stopa)") };
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
        sync->choices = StringArray::fromTokens (String::fromUTF8 ("Voľne|1/4|1/8 bodka|1/8|1/8 triola|1/16"), "|", "");
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
private:
    void timerCallback() override { repaint(); }
    PulseFx& fx;
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
        shp->choices = StringArray::fromTokens (String::fromUTF8 ("Mäkký nábeh|Klasika|Neskorý nástup|Hlboká pumpa|Rýchly nábeh|Opačne|Mäkký pokles|"
                                                                 "Vrchol|Dlhé podržanie|Plynulý nábeh|Tvrdá pumpa|Brána|Plató|Svah|Údolie|Dvojitá"), "|", "");
        mix = add ("mix", "Hĺbka", 0, 100, 100, "%", 1.0f, 0);
        out = add ("output", "Výstup", -12, 12, 0, "dB");
        mode = add ("mode", "Riadenie", 0, 1, 0, "", 1.0f, 0);
        mode->range.interval = 1.0f;
        mode->choices = StringArray { String::fromUTF8 ("Tempo (tvar)"), String::fromUTF8 ("Sidechain (kľúč)") };
        on  = add ("active", "Zapnuté", 0, 1, 1, "", 1.0f, 0);
        on->toggle = true;
    }

    std::unique_ptr<Component> makeDisplay() override { return std::make_unique<PulseDisplay> (*this); }

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

void PulseDisplay::paint (Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (Colour (0xff161c24));
    g.fillRoundedRectangle (r, 3.0f);
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
        g.drawText (String::fromUTF8 ("SIDECHAIN – riadi kľúčová stopa"), r.reduced (10, 4).removeFromTop (14), Justification::centredRight);
    else
    g.drawText (String (fx.bpmNow.load(), 1) + " BPM  /  " + fx.rateP->choices[roundToInt (fx.rateP->get())] + "  /  " + fx.shp->choices[s],
                r.reduced (10, 4).removeFromTop (14), Justification::centredRight);
}

struct Entry { const char* key; const char* name; std::function<std::unique_ptr<AudioPluginInstance>()> make; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e {
        { "eq",      "Korgus EQ",        [] { return std::unique_ptr<AudioPluginInstance> (new EqFx()); } },
        { "comp",    "Korgus Kompresor", [] { return std::unique_ptr<AudioPluginInstance> (new CompFx()); } },
        { "reverb",  "Korgus Dozvuk",    [] { return std::unique_ptr<AudioPluginInstance> (new ReverbFx()); } },
        { "delay",   "Korgus Ozvena",    [] { return std::unique_ptr<AudioPluginInstance> (new DelayFx()); } },
        { "limiter", "Korgus Limiter",   [] { return std::unique_ptr<AudioPluginInstance> (new LimiterFx()); } },
        { "pulse",   "Korgus PulseDip",  [] { return std::unique_ptr<AudioPluginInstance> (new PulseFx()); } },
    };
    return e;
}
} // namespace

namespace BuiltinFx
{
juce::Array<juce::PluginDescription> descriptions()
{
    juce::Array<juce::PluginDescription> a;
    for (const auto& e : entries()) a.add (FxBase::describe (e.name, e.key));
    return a;
}

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
