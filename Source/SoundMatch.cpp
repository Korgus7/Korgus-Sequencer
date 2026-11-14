#include "SoundMatch.h"
#include "CrashGuard.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include <cmath>
#include <cstdlib>

namespace SoundMatch
{
namespace
{
    //==========================================================================
    // prehratie jedneho tonu (mono vysledok, volitelne aj stereo kopia)
    // spusti funkciu na hlavnom vlakne a pocka (VST3 to vyzaduje pri zmene nastavenia zvukoveho vystupu)
    void onMessageThread (std::function<void()> f)
    {
        auto* mm = juce::MessageManager::getInstance();
        if (mm->isThisTheMessageThread()) { f(); return; }
        juce::WaitableEvent done;
        juce::MessageManager::callAsync ([&] { f(); done.signal(); });
        done.wait (20000);
    }

    // own = nas Inspire (smie sa resetovat); cudzi plugin sa len nechá doznieť tichom
    std::vector<float> renderNote (juce::AudioPluginInstance& p, int note, float vel, double hold, double tail, double sr,
                                   juce::AudioBuffer<float>* keep = nullptr, bool own = true)
    {
        if (own) p.reset();
        const int nch = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        const int outCh = juce::jmax (1, p.getTotalNumOutputChannels());
        juce::AudioBuffer<float> buf (nch, kBS);
        juce::MidiBuffer mb;
        // predbeh - doznejú zmeny parametrov a vyhladzovanie
        for (int i = 0, n = (int) ((own ? 0.08 : 3.0) * sr / kBS) + 1; i < n; ++i)
        {
            buf.clear(); mb.clear();
            CrashGuard::Busy cg (&p);
            p.processBlock (buf, mb);
        }
        const int total = (int) ((hold + tail) * sr), offAt = (int) (hold * sr);
        std::vector<float> out ((size_t) total, 0.0f);
        if (keep != nullptr) { keep->setSize (2, total); keep->clear(); }
        for (int pos = 0; pos < total; pos += kBS)
        {
            buf.clear(); mb.clear();
            if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
            if (offAt >= pos && offAt < pos + kBS) mb.addEvent (juce::MidiMessage::noteOff (1, note), offAt - pos);
            { CrashGuard::Busy cg (&p); p.processBlock (buf, mb); }
            const int n = juce::jmin (kBS, total - pos);
            for (int i = 0; i < n; ++i)
            {
                const float l = buf.getSample (0, i), r = outCh > 1 ? buf.getSample (1, i) : l;
                out[(size_t) (pos + i)] = 0.5f * (l + r);
                if (keep != nullptr) { keep->setSample (0, pos + i, l); keep->setSample (1, pos + i, r); }
            }
        }
        // vsetky noty vypnut (pre istotu)
        buf.clear(); mb.clear();
        for (int c = 1; c <= 16; ++c) mb.addEvent (juce::MidiMessage::allNotesOff (c), 0);
        { CrashGuard::Busy cg (&p); p.processBlock (buf, mb); }
        return out;
    }

    //==========================================================================
    // spektralny odtlacok: log-energia v 40 pasmach po 16 ms
    constexpr int kOrder = 11, kN = 1 << kOrder, kHop = 512, kBands = 40;

    struct Feat { std::vector<float> db; int frames = 0; double energy = 0; float maxDb = -200; };

    struct Analyzer
    {
        juce::dsp::FFT fft { kOrder };
        std::vector<float> win, work;
        std::vector<int> edge;
        explicit Analyzer (double sr) : win ((size_t) kN), work ((size_t) kN * 2)
        {
            for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kN);
            for (int b = 0; b <= kBands; ++b)
            {
                const double f = 50.0 * std::pow (12000.0 / 50.0, (double) b / kBands);
                edge.push_back (juce::jlimit (1, kN / 2 - 1, (int) std::round (f * kN / sr)));
            }
            for (int b = 1; b <= kBands; ++b) if (edge[(size_t) b] <= edge[(size_t) b - 1]) edge[(size_t) b] = edge[(size_t) b - 1] + 1;
        }
        Feat analyse (const std::vector<float>& x)
        {
            Feat f;
            const int len = (int) x.size();
            for (int s = 0; s + kN <= len; s += kHop)
            {
                std::fill (work.begin(), work.end(), 0.0f);
                for (int i = 0; i < kN; ++i) work[(size_t) i] = x[(size_t) (s + i)] * win[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (work.data(), true);
                for (int b = 0; b < kBands; ++b)
                {
                    double p = 0;
                    for (int k = edge[(size_t) b]; k < edge[(size_t) b + 1]; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
                    f.energy += p;
                    const float d = (float) (10.0 * std::log10 (p + 1e-9));
                    f.db.push_back (d);
                    f.maxDb = juce::jmax (f.maxDb, d);
                }
                ++f.frames;
            }
            return f;
        }
    };

    // vzdialenost dvoch zvukov (hlasitost sa vyrovna, porovnava sa farba a priebeh v case)
    float distance (const std::vector<Feat>& S, const std::vector<Feat>& C)
    {
        double es = 0, ec = 0;
        for (auto& f : S) es += f.energy;
        for (auto& f : C) ec += f.energy;
        if (ec <= 1e-12) return 99.0f;    // ticho
        const float shift = (float) (10.0 * std::log10 (es / ec));
        double err = 0, wsum = 0;
        for (size_t k = 0; k < S.size() && k < C.size(); ++k)
        {
            const float floor = S[k].maxDb - 60.0f;
            const int fr = juce::jmin (S[k].frames, C[k].frames);
            for (int t = 0; t < fr; ++t)
            {
                const double w = t < 10 ? 2.0 : 1.0;    // nabeh je pre sluch dolezity
                for (int b = 0; b < kBands; ++b)
                {
                    const float a = juce::jmax (S[k].db[(size_t) (t * kBands + b)], floor);
                    const float c = juce::jmax (C[k].db[(size_t) (t * kBands + b)] + shift, floor);
                    err += w * std::abs (a - c);
                }
                wsum += w * kBands;
            }
        }
        return wsum > 0 ? (float) (err / wsum / 10.0) : 99.0f;
    }

    //==========================================================================
    // parametre Inspire, ktore sa hladaju
    enum Kind { Lin, Log, Int, Choice };
    struct Dim { const char* id; float lo, hi; Kind kind; std::vector<float> ch; };

    std::vector<Dim> dims()
    {
        return {
            { "a_wave", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6 } },
            { "a_oct", -2, 1, Int, {} },
            { "a_uni", 1, 7, Int, {} },
            { "a_spread", 0, 40, Lin, {} },
            { "a_color", 0, 1, Lin, {} },
            { "a_ratio", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
            { "a_cut", 80, 20000, Log, {} },
            { "a_reso", 0, 0.85f, Lin, {} },
            { "a_fenv", -0.5f, 1, Lin, {} },
            { "a_fa", 0.001f, 3, Log, {} },
            { "a_fd", 0.01f, 6, Log, {} },
            { "a_fs", 0, 1, Lin, {} },
            { "a_fr", 0.01f, 6, Log, {} },
            { "a_aa", 0.0005f, 3, Log, {} },
            { "a_ad", 0.01f, 8, Log, {} },
            { "a_as", 0, 1, Lin, {} },
            { "a_ar", 0.01f, 6, Log, {} },
            { "a_ldepth", 0, 0.25f, Lin, {} },
            { "a_lrate", 0.5f, 8, Log, {} },
            { "b_on", 0, 0, Choice, { 0, 1 } },
            { "b_wave", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6 } },
            { "b_oct", -2, 1, Int, {} },
            { "b_semi", 0, 0, Choice, { 0, 7 } },
            { "b_uni", 1, 7, Int, {} },
            { "b_spread", 0, 40, Lin, {} },
            { "b_color", 0, 1, Lin, {} },
            { "b_level", 0, 1, Lin, {} },
            { "b_cut", 80, 20000, Log, {} },
            { "b_reso", 0, 0.85f, Lin, {} },
            { "ch_mix", 0, 0.7f, Lin, {} },
            { "ch_type", 0, 0, Choice, { 0, 1, 2, 3 } },
            { "rv_mix", 0, 0.6f, Lin, {} },
            { "rv_size", 0, 1, Lin, {} },
            { "drive", 0, 0.7f, Lin, {} },
            { "tube", 0, 0.6f, Lin, {} },
            { "eq_low", -9, 9, Lin, {} },
            { "eq_high", -9, 9, Lin, {} },
        };
    }

    float decode (const Dim& d, float x)
    {
        x = juce::jlimit (0.0f, 1.0f, x);
        switch (d.kind)
        {
            case Lin: return d.lo + x * (d.hi - d.lo);
            case Log: return d.lo * std::pow (d.hi / d.lo, x);
            case Int: return (float) juce::roundToInt (d.lo + x * (d.hi - d.lo));
            case Choice: return d.ch[(size_t) juce::jmin ((int) d.ch.size() - 1, (int) (x * (float) d.ch.size()))];
        }
        return d.lo;
    }

    float encode (const Dim& d, float v)
    {
        switch (d.kind)
        {
            case Lin: case Int: return d.hi > d.lo ? juce::jlimit (0.0f, 1.0f, (v - d.lo) / (d.hi - d.lo)) : 0.5f;
            case Log: return juce::jlimit (0.0f, 1.0f, (float) (std::log (juce::jmax (v, d.lo) / d.lo) / std::log (d.hi / d.lo)));
            case Choice:
                for (size_t i = 0; i < d.ch.size(); ++i)
                    if (std::abs (d.ch[i] - v) < 0.01f) return ((float) i + 0.5f) / (float) d.ch.size();
                return 0.5f / (float) d.ch.size();
        }
        return 0.5f;
    }

    //==========================================================================
    // stav hostovaneho VST3: <VST3PluginState><IComponent>base64</IComponent>...</VST3PluginState>
    // vo vnutri je stav Inspire (copyXmlToBinary) + sukromne data JUCE na konci
    struct StateCodec
    {
        std::unique_ptr<juce::XmlElement> outer;
        juce::MemoryBlock tail;
        bool vst3 = false;

        juce::ValueTree decode (const juce::MemoryBlock& st)
        {
            auto x = juce::AudioProcessor::getXmlFromBinary (st.getData(), (int) st.getSize());
            if (x == nullptr) return {};
            if (x->hasTagName ("VST3PluginState"))
            {
                auto* comp = x->getChildByName ("IComponent");
                if (comp == nullptr) return {};
                juce::MemoryBlock inner;
                inner.fromBase64Encoding (comp->getAllSubText().trim());
                if (inner.getSize() < 9) return {};
                const auto* d = static_cast<const juce::uint8*> (inner.getData());
                const int len = (int) juce::ByteOrder::littleEndianInt (d + 4);
                const size_t used = (size_t) juce::jmin ((int) inner.getSize(), 8 + len + 1);
                if (inner.getSize() > used) tail.append (d + used, inner.getSize() - used);
                auto ix = juce::AudioProcessor::getXmlFromBinary (inner.getData(), (int) inner.getSize());
                if (ix == nullptr) return {};
                vst3 = true;
                if (auto* ec = x->getChildByName ("IEditController")) x->removeChildElement (ec, true);
                outer = std::move (x);
                return juce::ValueTree::fromXml (*ix);
            }
            return juce::ValueTree::fromXml (*x);
        }

        juce::MemoryBlock encode (const juce::ValueTree& t) const
        {
            juce::MemoryBlock inner;
            if (auto xml = t.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, inner);
            if (! vst3 || outer == nullptr) return inner;
            inner.append (tail.getData(), tail.getSize());
            juce::XmlElement o (*outer);
            if (auto* comp = o.getChildByName ("IComponent")) { comp->deleteAllChildElements(); comp->addTextElement (inner.toBase64Encoding()); }
            juce::MemoryBlock mb;
            juce::AudioProcessor::copyXmlToBinary (o, mb);
            return mb;
        }
    };

    //==========================================================================
    struct Patch
    {
        juce::ValueTree tree;
        std::map<juce::String, juce::ValueTree> byId;
        explicit Patch (juce::ValueTree t) : tree (t)
        {
            for (auto c : tree) if (c.hasProperty ("id")) byId[c.getProperty ("id").toString()] = c;
        }
        void set (const juce::String& id, float v)
        {
            auto it = byId.find (id);
            if (it != byId.end()) it->second.setProperty ("value", v, nullptr);
        }
        float get (const juce::String& id, float def = 0) const
        {
            auto it = byId.find (id);
            return it != byId.end() ? (float) (double) it->second.getProperty ("value", def) : def;
        }
        const StateCodec* codec = nullptr;
        juce::MemoryBlock binary() const
        {
            if (codec != nullptr) return codec->encode (tree);
            juce::MemoryBlock mb;
            if (auto xml = tree.createXml()) juce::AudioProcessor::copyXmlToBinary (*xml, mb);
            return mb;
        }
    };

    // pevne hodnoty a hodnoty odvodene od hladanych
    void applyFixed (Patch& p, const std::vector<Dim>& D, const std::vector<float>& x)
    {
        for (size_t i = 0; i < D.size(); ++i) p.set (D[i].id, decode (D[i], x[i]));
        auto v = [&] (const char* id) { for (size_t i = 0; i < D.size(); ++i) if (juce::String (D[i].id) == id) return decode (D[i], x[i]); return 0.0f; };
        const std::pair<const char*, float> fixed[] = {
            { "a_on", 1 }, { "a_level", 0.8f }, { "a_semi", 0 }, { "a_fine", 0 }, { "a_pan", 0 }, { "a_ftype", 0 }, { "a_f24", 0 },
            { "a_lshape", 0 }, { "a_ltgt", 0 }, { "a_ldelay", 0 }, { "a_src", 0 },
            { "b_fine", 0 }, { "b_pan", 0 }, { "b_ftype", 0 }, { "b_f24", 0 }, { "b_ldepth", 0 }, { "b_src", 0 }, { "b_root", 60 }, { "b_loop", 0 },
            { "bright", 0.5f }, { "attack", 0.5f }, { "release", 0.5f }, { "space", 0.5f }, { "width", 0.5f }, { "volume", 0 }, { "p_gain", 0 },
            { "morph_x", 0 }, { "morph_y", 0 }, { "dl_on", 0 }, { "arp_on", 0 }, { "mono", 0 }, { "glide", 0 }, { "mf_on", 0 }, { "comp_on", 0 },
            { "eq_on", 1 }, { "eq_mid", 0 }, { "fx_width", 1 }, { "fx_punch", 0.5f }, { "ch_fb", 0.3f }, { "ch_rate", 0.6f }, { "ch_depth", 0.5f },
            { "rv_pre", 0 }, { "rv_damp", 0.4f } };
        for (auto& f : fixed) p.set (f.first, f.second);
        for (auto pre : { "a_", "b_", "rv_", "dl_" })
            for (auto s : { "eql", "eqm", "eqh" }) p.set (juce::String (pre) + s, 0);
        // vrstva B zdiela obalky a filter-obalku s vrstvou A
        for (auto s : { "fenv", "fa", "fd", "fs", "fr", "aa", "ad", "as", "ar", "ratio" })
            p.set (juce::String ("b_") + s, v ((juce::String ("a_") + s).toRawUTF8()));
        p.set ("ch_on", v ("ch_mix") > 0.03f ? 1.0f : 0.0f);
        p.set ("rv_on", v ("rv_mix") > 0.02f ? 1.0f : 0.0f);
        p.set ("drive_on", v ("drive") > 0.05f ? 1.0f : 0.0f);
    }

    const int kNotes[] = { 48, 60, 72 };
    constexpr double kHold = 1.4, kTail = 0.9;
}

//==============================================================================
Result run (juce::AudioPluginInstance& src, juce::AudioPluginInstance& insp, const juce::MemoryBlock& inspState,
            bool allowSample, const juce::String& srcName, Progress progress)
{
    Result res;
    auto prog = [&] (double f, const juce::String& m) { return progress == nullptr || progress (f, m); };
    Analyzer an (kSR);

    // 1) zdroj zahra tony (niektore pluginy nacitavaju zvuky na pozadi - pockame)
    prog (0.0, juce::String::fromUTF8 ("Počúvam zdrojový nástroj…"));
    std::vector<std::vector<float>> srcAudio;
    for (int tries = 0; tries < 200; ++tries)
    {
        auto x = renderNote (src, 60, 0.8f, kHold, kTail, kSR, nullptr, false);
        float pk = 0; for (float s : x) pk = juce::jmax (pk, std::abs (s));
        if (pk > 1.0e-4f) break;
        if (tries == 199) { res.error = juce::String::fromUTF8 ("Zdrojová stopa nehrá žiadny zvuk."); return res; }
        if (! prog (0.0, juce::String::fromUTF8 ("Čakám, kým sa nástroj načíta…"))) { res.error = "cancel"; return res; }
        juce::Thread::sleep (300);
    }
    std::vector<Feat> S;
    for (int n : kNotes) { srcAudio.push_back (renderNote (src, n, 0.8f, kHold, kTail, kSR, nullptr, false)); S.push_back (an.analyse (srcAudio.back())); }

    // 2) hladanie nastavenia Inspire
    StateCodec codec;
    auto tree0 = codec.decode (inspState);
    if (! tree0.isValid()) { res.error = juce::String::fromUTF8 ("Stav Inspire sa nedá prečítať."); return res; }
    Patch patch (tree0);
    patch.codec = &codec;
    patch.tree.setProperty ("smpB", juce::String(), nullptr);
    patch.tree.setProperty ("smpBName", juce::String(), nullptr);
    const auto D = dims();

    auto evalPatch = [&] (std::vector<Feat>* outC = nullptr, std::vector<std::vector<float>>* outAudio = nullptr)
    {
        const auto mb = patch.binary();
        insp.setStateInformation (mb.getData(), (int) mb.getSize());
        std::vector<Feat> C;
        for (int n : kNotes)
        {
            auto a = renderNote (insp, n, 0.8f, kHold, kTail, kSR);
            C.push_back (an.analyse (a));
            if (outAudio != nullptr) outAudio->push_back (std::move (a));
        }
        ++res.evals;
        const float d = distance (S, C);
        if (outC != nullptr) *outC = std::move (C);
        return d;
    };
    auto evalX = [&] (const std::vector<float>& x) { applyFixed (patch, D, x); return evalPatch(); };

    struct Cand { std::vector<float> x; float loss; };
    std::vector<Cand> pop;
    std::mt19937 rng ((unsigned) juce::Time::getMillisecondCounter());
    std::uniform_real_distribution<float> U (0.0f, 1.0f);
    std::normal_distribution<float> N (0.0f, 1.0f);

    // zaciatok: sucasny zvuk Inspire + nahodne pokusy
    {
        std::vector<float> x0;
        for (auto& d : D) x0.push_back (encode (d, patch.get (d.id, d.lo)));
        pop.push_back ({ x0, evalX (x0) });
        res.startLoss = pop[0].loss;
    }
    const int nRandom = 60, gens = 40, lambda = 14, mu = 6;
    const int totalEvals = 1 + nRandom + gens * lambda;
    for (int i = 0; i < nRandom; ++i)
    {
        std::vector<float> x;
        for (size_t k = 0; k < D.size(); ++k) x.push_back (U (rng));
        pop.push_back ({ x, evalX (x) });
        if (! prog (0.05 + 0.85 * (double) res.evals / totalEvals, juce::String::fromUTF8 ("Skúšam zvuky…"))) { res.error = "cancel"; return res; }
    }
    auto sortPop = [&] { std::sort (pop.begin(), pop.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; }); if (pop.size() > 30) pop.resize (30); };
    sortPop();
    for (int g = 0; g < gens; ++g)
    {
        const float sigma = 0.3f * std::pow (0.1f, (float) g / (float) (gens - 1));
        for (int c = 0; c < lambda; ++c)
        {
            // rodic podla poradia (lepsi castejsie)
            const int pi = juce::jmin ((int) pop.size() - 1, (int) (std::pow (U (rng), 2.0f) * (float) mu));
            auto x = pop[(size_t) pi].x;
            bool any = false;
            for (size_t k = 0; k < x.size(); ++k)
                if (U (rng) < 0.3f) { x[k] = juce::jlimit (0.0f, 1.0f, x[k] + sigma * N (rng)); any = true; }
            if (! any) { const size_t k = (size_t) (U (rng) * (float) x.size()) % x.size(); x[k] = juce::jlimit (0.0f, 1.0f, x[k] + sigma * N (rng)); }
            pop.push_back ({ x, evalX (x) });
            if (! prog (0.05 + 0.85 * (double) res.evals / totalEvals,
                        juce::String::fromUTF8 ("Ladím zvuk… zhoda ") + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - pop[0].loss / 2.5f) * 100.0f)) + " %"))
            { res.error = "cancel"; return res; }
        }
        sortPop();
    }
    const auto best = pop[0];
    res.synthLoss = best.loss;
    applyFixed (patch, D, best.x);
    float finalLoss = best.loss;

    // 3) volitelne: sampel zdroja do vrstvy B
    if (allowSample && finalLoss > 0.35f)
    {
        prog (0.92, juce::String::fromUTF8 ("Dopĺňam sampel do vrstvy B…"));
        onMessageThread ([&] { src.releaseResources(); src.setRateAndBufferSizeDetails (48000.0, kBS); src.prepareToPlay (48000.0, kBS); });
        juce::AudioBuffer<float> st;
        renderNote (src, 60, 0.8f, 2.6, 1.6, 48000.0, &st, false);
        onMessageThread ([&] { src.releaseResources(); src.setRateAndBufferSizeDetails (kSR, kBS); src.prepareToPlay (kSR, kBS); });
        juce::MemoryBlock flac;
        {
            std::unique_ptr<juce::OutputStream> os (new juce::MemoryOutputStream (flac, false));
            juce::FlacAudioFormat fmt;
            if (auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24)))
            {
                st.applyGain (1.0f / juce::jmax (0.05f, st.getMagnitude (0, st.getNumSamples())) * 0.9f);
                w->writeFromAudioSampleBuffer (st, 0, st.getNumSamples());
            }
        }
        if (flac.getSize() > 1000)
        {
            const auto synthState = patch.binary();
            patch.tree.setProperty ("smpB", flac.toBase64Encoding(), nullptr);
            patch.tree.setProperty ("smpBName", srcName + " C4", nullptr);
            for (const auto& kv : std::initializer_list<std::pair<const char*, float>> {
                     { "b_on", 1 }, { "b_wave", 7 }, { "b_src", 1 }, { "b_root", 60 }, { "b_oct", 0 }, { "b_semi", 0 }, { "b_uni", 1 },
                     { "b_cut", 20000 }, { "b_reso", 0 }, { "b_fenv", 0 }, { "b_aa", 0.001f }, { "b_ad", 1 }, { "b_as", 1 }, { "b_color", 0.5f } })
                patch.set (kv.first, kv.second);
            patch.set ("b_ar", patch.get ("a_ar", 0.3f));
            float bestH = 99, bestA = 0.8f, bestB = 0.7f;
            for (float a : { 0.0f, 0.25f, 0.5f, 0.8f })
                for (float b : { 0.5f, 0.8f, 1.0f })
                {
                    patch.set ("a_level", a); patch.set ("b_level", b);
                    const float d = evalPatch();
                    if (d < bestH) { bestH = d; bestA = a; bestB = b; }
                    if (! prog (0.92 + 0.06 * (double) res.evals / (double) (totalEvals + 12), juce::String::fromUTF8 ("Dopĺňam sampel do vrstvy B…"))) { res.error = "cancel"; return res; }
                }
            if (bestH < finalLoss * 0.9f)
            {
                patch.set ("a_level", bestA); patch.set ("b_level", bestB);
                finalLoss = bestH; res.usedSample = true;
            }
            else
            {
                // syntéza bola lepsia - vratime ju
                StateCodec c2;
                patch = Patch (c2.decode (synthState));
                patch.codec = &codec;
            }
        }
    }

    // 4) hlasitost ako zdroj
    {
        std::vector<std::vector<float>> au;
        evalPatch (nullptr, &au);
        double es = 0, ec = 0;
        for (auto& a : srcAudio) for (float s : a) es += (double) s * s;
        for (auto& a : au) for (float s : a) ec += (double) s * s;
        if (ec > 1e-12 && es > 1e-12)
            patch.set ("p_gain", juce::jlimit (-30.0f, 6.0f, (float) (10.0 * std::log10 (es / ec))));
    }
    patch.tree.setProperty ("patchName", juce::String::fromUTF8 ("Naučený: ") + srcName, nullptr);
    patch.tree.setProperty ("patchKey", juce::String(), nullptr);
    res.finalLoss = finalLoss;
    res.state = patch.binary();
    res.ok = true;
    prog (1.0, juce::String::fromUTF8 ("Hotovo"));
    return res;
}
}
