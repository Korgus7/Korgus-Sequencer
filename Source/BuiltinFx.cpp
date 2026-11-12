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
        return (dec == 0 ? String (roundToInt (x)) : String (x, dec)) + (unit.isEmpty() ? "" : " " + unit);
    }
    float getValueForText (const String& t) const override { return range.convertTo0to1 (jlimit (range.start, range.end, t.getFloatValue())); }

    const String id, name, unit;
    NormalisableRange<float> range;
    const int dec;
    float defNorm = 0;
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
            g.drawFittedText (labels[i], knobs[i]->getBounds().withY (8).withHeight (18), Justification::centred, 1);
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (8, 0).withTrimmedTop (28).withTrimmedBottom (8);
        const int w = r.getWidth() / jmax (1, (int) knobs.size());
        for (auto& k : knobs) k->setBounds (r.removeFromLeft (w).reduced (4, 0));
    }

private:
    void timerCallback() override
    {
        for (size_t i = 0; i < knobs.size(); ++i)
            if (! knobs[i]->isMouseButtonDown())
                knobs[i]->setValue (params[i]->get(), dontSendNotification);
    }

    std::vector<FxParam*> params;
    std::vector<std::unique_ptr<Slider>> knobs;
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

protected:
    virtual void prepare() = 0;
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
    for (auto* prm : params)
    {
        auto k = std::make_unique<Slider> (Slider::RotaryHorizontalVerticalDrag, Slider::TextBoxBelow);
        k->setTextBoxStyle (Slider::TextBoxBelow, false, 74, 18);
        k->setRange (prm->range.start, prm->range.end, 0.0);
        k->setSkewFactor (prm->range.skew);
        k->setValue (prm->get(), dontSendNotification);
        k->setDoubleClickReturnValue (true, prm->range.convertFrom0to1 (prm->defNorm));
        k->textFromValueFunction = [prm] (double v) { return prm->getText (prm->range.convertTo0to1 ((float) v), 0); };
        k->valueFromTextFunction = [] (const String& t) { return t.getDoubleValue(); };
        k->setColour (Slider::rotarySliderFillColourId, Colour (0xff6f8fb3));
        k->setColour (Slider::rotarySliderOutlineColourId, Colour (0xff2d4362));
        k->setColour (Slider::thumbColourId, Colour (0xffc9d2dc));
        k->setColour (Slider::textBoxOutlineColourId, Colours::transparentBlack);
        k->setColour (Slider::textBoxTextColourId, Colour (0xffc9d2dc));
        auto* raw = k.get();
        k->onDragStart = [prm] { prm->beginChangeGesture(); };
        k->onDragEnd   = [prm] { prm->endChangeGesture(); };
        k->onValueChange = [prm, raw] { prm->setValueNotifyingHost (prm->range.convertTo0to1 ((float) raw->getValue())); };
        k->updateText();
        addAndMakeVisible (*k);
        labels.push_back (prm->name);
        knobs.push_back (std::move (k));
    }
    setSize (jmax (260, 92 * (int) knobs.size() + 16), 150);
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
    }

private:
    void prepare() override { comp.prepare (spec); comp.reset(); dry.setSize ((int) spec.numChannels, (int) spec.maximumBlockSize); }

    void process (AudioBuffer<float>& b) override
    {
        comp.setThreshold (thr->get()); comp.setRatio (rat->get());
        comp.setAttack (att->get()); comp.setRelease (rel->get());
        const float m = mix->get() * 0.01f;
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        if (m < 0.999f)
        {
            if (dry.getNumSamples() < n || dry.getNumChannels() < ch) dry.setSize (ch, n, false, false, true);
            for (int c = 0; c < ch; ++c) dry.copyFrom (c, 0, b, c, 0, n);
        }
        dsp::AudioBlock<float> blk (b);
        comp.process (dsp::ProcessContextReplacing<float> (blk));
        const float g = Decibels::decibelsToGain (mak->get());
        b.applyGainRamp (0, n, lastG, g);
        lastG = g;
        if (m < 0.999f)
            for (int c = 0; c < ch; ++c)
            {
                b.applyGain (c, 0, n, m);
                b.addFrom (c, 0, dry, c, 0, n, 1.0f - m);
            }
    }

    FxParam *thr, *rat, *att, *rel, *mak, *mix;
    dsp::Compressor<float> comp;
    AudioBuffer<float> dry;
    float lastG = 1.0f;
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
        time = add ("time", "Čas", 10, 2000, 375, "ms", 0.5f, 0);
        fb   = add ("feedback", "Opakovanie", 0, 95, 35, "%", 1.0f, 0);
        tone = add ("tone", "Farba", 1000, 20000, 8000, "Hz", 0.4f, 0);
        pp   = add ("pingpong", "Ping-pong", 0, 1, 0, "", 1.0f, 0);
        mix  = add ("mix", "Mix", 0, 100, 25, "%", 1.0f, 0);
        tail = 4.0;
    }

private:
    void prepare() override
    {
        size = (int) (rate * 2.1) + 4;
        line.setSize (2, size); line.clear();
        w = 0; lp[0] = lp[1] = 0;
        curD = (float) (time->get() * 0.001 * rate);
    }

    void process (AudioBuffer<float>& b) override
    {
        const int n = b.getNumSamples(), ch = b.getNumChannels();
        const float target = (float) jlimit (1.0, (double) size - 3, time->get() * 0.001 * rate);
        const float f = fb->get() * 0.01f, m = mix->get() * 0.01f;
        const float a = (float) std::exp (-MathConstants<double>::twoPi * tone->get() / rate);
        const bool ping = pp->get() > 0.5f && ch > 1;
        auto* dl = line.getWritePointer (0);
        auto* dr = line.getWritePointer (1);
        auto* bl = b.getWritePointer (0);
        auto* br = ch > 1 ? b.getWritePointer (1) : nullptr;
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

    FxParam *time, *fb, *tone, *pp, *mix;
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

struct Entry { const char* key; const char* name; std::function<std::unique_ptr<AudioPluginInstance>()> make; };

const std::vector<Entry>& entries()
{
    static const std::vector<Entry> e {
        { "eq",      "Korgus EQ",        [] { return std::unique_ptr<AudioPluginInstance> (new EqFx()); } },
        { "comp",    "Korgus Kompresor", [] { return std::unique_ptr<AudioPluginInstance> (new CompFx()); } },
        { "reverb",  "Korgus Dozvuk",    [] { return std::unique_ptr<AudioPluginInstance> (new ReverbFx()); } },
        { "delay",   "Korgus Ozvena",    [] { return std::unique_ptr<AudioPluginInstance> (new DelayFx()); } },
        { "limiter", "Korgus Limiter",   [] { return std::unique_ptr<AudioPluginInstance> (new LimiterFx()); } },
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
