#include "SoundMatch.h"
#include "CrashGuard.h"
#include <juce_dsp/juce_dsp.h>
#include <juce_audio_formats/juce_audio_formats.h>
#include <random>
#include <cmath>
#include <cstdlib>
#include <cstdarg>
#include <atomic>
#include <memory>
#include <mutex>
#include <map>
#include <set>

namespace SoundMatch
{
namespace
{
    //==========================================================================
    // diagnostika: Dokumenty\Korgus Sequencer\Ucenie (pevne nazvy suborov, kazdy beh ich prepise)
    struct Diag
    {
        juce::File dir;
        std::unique_ptr<juce::FileOutputStream> os;
        void open()
        {
            dir = diagFolder();
            dir.createDirectory();
            auto f = dir.getChildFile ("learn-log.txt");
            f.deleteFile();
            os = f.createOutputStream();
        }
        void line (const juce::String& s)
        {
            if (os != nullptr) { os->writeText (s + "\r\n", false, false, nullptr); os->flush(); }
        }
    };
    Diag* gDiag = nullptr;
    std::atomic<int> gTimeouts { 0 }, gDistNonFinite { 0 };
    void dlog (const juce::String& s) { if (gDiag != nullptr) gDiag->line (s); }
    void dbgf (const char* fmt, ...)
    {
        char buf[1536];
        va_list ap; va_start (ap, fmt); vsnprintf (buf, sizeof (buf), fmt, ap); va_end (ap);
        if (std::getenv ("SM_DEBUG") != nullptr) fputs (buf, stderr);
        dlog (juce::String::fromUTF8 (buf).trimEnd());
    }
    void writeWav (const juce::String& name, const juce::AudioBuffer<float>& b, double sr)
    {
        if (gDiag == nullptr || b.getNumSamples() < 1) return;
        auto f = gDiag->dir.getChildFile (name);
        f.deleteFile();
        std::unique_ptr<juce::OutputStream> os = f.createOutputStream();
        if (os == nullptr) return;
        juce::WavAudioFormat fmt;
        if (auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (juce::jmin (2, b.getNumChannels()))
                                                  .withBitsPerSample (32).withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint)))
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
    }

    // spusti funkciu na hlavnom vlakne a pocka (VST3 to vyzaduje pri zmene stavu a nastavenia zvukoveho vystupu)
    // pri vyprsani casu (20 s) sa funkcia uz nikdy nespusti (ziadne visiace odkazy) a vrati false
    bool onMessageThread (std::function<void()> f)
    {
        auto* mm = juce::MessageManager::getInstance();
        if (mm->isThisTheMessageThread()) { f(); return true; }
        struct St { std::mutex m; bool ran = false, abandoned = false; std::function<void()> f; juce::WaitableEvent done; };
        auto st = std::make_shared<St>();
        st->f = std::move (f);
        juce::MessageManager::callAsync ([st]
        {
            std::lock_guard<std::mutex> g (st->m);
            if (st->abandoned) return;
            st->f(); st->ran = true; st->done.signal();
        });
        if (st->done.wait (20000)) return true;
        std::lock_guard<std::mutex> g (st->m);       // ak funkcia prave bezi, pocka sa na jej koniec
        if (st->ran) return true;
        st->abandoned = true;
        ++gTimeouts;
        return false;
    }

    // vysledok prehratia: stereo + mono mix (M = 0.5 (L+R)); begin()/end() prechadzaju mono mix
    struct Rend
    {
        std::vector<float> L, R, M;
        int nonFinite = 0;                     // pocet nekonecnych/NaN vzoriek nahradenych nulou
        auto begin() const { return M.begin(); }
        auto end() const { return M.end(); }
    };

    // own = nas Inspire (smie sa resetovat); cudzi plugin sa len nechá doznieť tichom
    Rend renderNote (juce::AudioPluginInstance& p, int note, float vel, double hold, double tail, double sr,
                     juce::AudioBuffer<float>* keep = nullptr, bool own = true)
    {
        juce::ignoreUnused (own);
        const int nch = juce::jmax (2, p.getTotalNumInputChannels(), p.getTotalNumOutputChannels());
        int nOut = 0;
        if (p.getBusCount (false) > 0) if (auto* b = p.getBus (false, 0)) nOut = b->getNumberOfChannels();
        const int outCh = juce::jmax (1, nOut > 0 ? nOut : p.getTotalNumOutputChannels());
        auto chIdx = [&] (int c) { const int k = p.getChannelIndexInProcessBlockBuffer (false, 0, c); return (k >= 0 && k < nch) ? k : juce::jmin (c, nch - 1); };
        const int iL = chIdx (0), iR = outCh > 1 ? chIdx (1) : iL;
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
        Rend out;
        out.L.assign ((size_t) total, 0.0f); out.R.assign ((size_t) total, 0.0f); out.M.assign ((size_t) total, 0.0f);
        if (keep != nullptr) { keep->setSize (2, total); keep->clear(); }
        auto fin = [&] (float x) { if (std::isfinite (x)) return x; ++out.nonFinite; return 0.0f; };
        for (int pos = 0; pos < total; pos += kBS)
        {
            buf.clear(); mb.clear();
            if (pos == 0) mb.addEvent (juce::MidiMessage::noteOn (1, note, vel), 0);
            if (offAt >= pos && offAt < pos + kBS) mb.addEvent (juce::MidiMessage::noteOff (1, note), offAt - pos);
            { CrashGuard::Busy cg (&p); p.processBlock (buf, mb); }
            const int n = juce::jmin (kBS, total - pos);
            const float* pl = buf.getReadPointer (iL); const float* pr = buf.getReadPointer (iR);
            for (int i = 0; i < n; ++i)
            {
                const float l = fin (pl[i]), r = outCh > 1 ? fin (pr[i]) : l;
                out.L[(size_t) (pos + i)] = l; out.R[(size_t) (pos + i)] = r;
                out.M[(size_t) (pos + i)] = 0.5f * (l + r);
                if (keep != nullptr) { keep->setSample (0, pos + i, l); keep->setSample (1, pos + i, r); }
            }
        }
        // vsetky noty vypnut (pre istotu)
        buf.clear(); mb.clear();
        for (int c = 1; c <= 16; ++c) mb.addEvent (juce::MidiMessage::allNotesOff (c), 0);
        { CrashGuard::Busy cg (&p); p.processBlock (buf, mb); }
        return out;
    }

    // zdroj: odstranenie DC a hornej priepuste 20 Hz (48 kHz), potom prevzorkovanie 48 -> 32 kHz (okienkovy sinc, medza 14.5 kHz)
    Rend prepareSource (const Rend& in48)
    {
        Rend out;
        out.nonFinite = in48.nonFinite;
        const int n = (int) in48.L.size();
        const int m = (int) ((double) n * kSR / 48000.0);
        constexpr int W = 32;
        const double fc = 14500.0 / 48000.0;
        float ker[2][2 * W];
        for (int ph = 0; ph < 2; ++ph)
        {
            double sum = 0; double kk[2 * W];
            for (int j = 0; j < 2 * W; ++j)
            {
                const double d = (double) ph * 0.5 + (double) (W - 1 - j);      // t - index
                const double x = 2.0 * fc * d;
                const double sinc = std::abs (x) < 1.0e-9 ? 1.0 : std::sin (juce::MathConstants<double>::pi * x) / (juce::MathConstants<double>::pi * x);
                const double u = d / (double) W;
                const double win = std::abs (u) >= 1.0 ? 0.0 : 0.42 + 0.5 * std::cos (juce::MathConstants<double>::pi * u) + 0.08 * std::cos (2.0 * juce::MathConstants<double>::pi * u);
                kk[j] = 2.0 * fc * sinc * win; sum += kk[j];
            }
            for (int j = 0; j < 2 * W; ++j) ker[ph][j] = (float) (kk[j] / sum);
        }
        auto proc = [&] (const std::vector<float>& x, std::vector<float>& y)
        {
            std::vector<float> h (x.size());
            double mean = 0; for (float v : x) mean += v;
            mean = x.empty() ? 0.0 : mean / (double) x.size();
            const double R = 1.0 - juce::MathConstants<double>::twoPi * 20.0 / 48000.0;
            double xp = x.empty() ? 0.0 : x[0] - mean, yp = 0.0;
            for (size_t i = 0; i < x.size(); ++i)
            {
                const double xi = x[i] - mean;
                yp = xi - xp + R * yp; xp = xi;
                h[i] = (float) yp;
            }
            y.assign ((size_t) m, 0.0f);
            for (int i = 0; i < m; ++i)
            {
                const int base = (3 * i) / 2, ph = (3 * i) % 2;
                const int i0 = base - (W - 1);
                double a = 0;
                for (int j = 0; j < 2 * W; ++j)
                {
                    const int k = i0 + j;
                    if (k >= 0 && k < n) a += (double) h[(size_t) k] * ker[ph][j];
                }
                y[(size_t) i] = (float) a;
            }
        };
        proc (in48.L, out.L); proc (in48.R, out.R);
        out.M.resize ((size_t) m);
        for (int i = 0; i < m; ++i) out.M[(size_t) i] = 0.5f * (out.L[(size_t) i] + out.R[(size_t) i]);
        return out;
    }

    juce::AudioBuffer<float> toBuffer (const Rend& r)
    {
        juce::AudioBuffer<float> b (2, (int) r.L.size());
        for (int i = 0; i < (int) r.L.size(); ++i) { b.setSample (0, i, r.L[(size_t) i]); b.setSample (1, i, r.R[(size_t) i]); }
        return b;
    }

    //==========================================================================
    // zvukovy odtlacok tonu (od nabehu): spektra segmentov, obalka, modulacia, stereo, snimky
    constexpr int kOrder = 11, kN = 1 << kOrder, kHop = 512, kBands = 40;
    constexpr int kOrderA = 9, kNA = 1 << kOrderA, kHopA = 128;          // kratke FFT pre nabeh
    constexpr int kRmsHop = 160;                                          // 5 ms pri 32 kHz
    constexpr int kAttackLen = 3200, kEarlyEnd = 12800, kSustEnd = 44800; // 100 ms, 400 ms, 1.4 s
    // indexy snimkov (hop 512) cisto vo vnutri segmentov
    constexpr int kEarly0 = (kAttackLen + kHop - 1) / kHop, kEarly1 = (kEarlyEnd - kN) / kHop;
    constexpr int kSus0 = kEarlyEnd / kHop, kSus1 = (kSustEnd - kN) / kHop, kRel0 = (kSustEnd + kHop - 1) / kHop;

    struct Feat
    {
        std::vector<float> db;                 // snimky 2048/512 x 40 pasiem (dB), od nabehu
        int frames = 0;
        double energy = 0;                     // energia od nabehu v pasme 50 Hz - 12 kHz (sucet pasiem snimkov, ako spektra)
        int onset = 0;                         // index nabehu v nahravke
        float maxDb = -200, attMax = -200, maxRms = -200;
        float seg[4][kBands] = {};             // priemerne spektrum segmentu (dB z priemernej energie): nabeh, skory, sustain, doznenie
        bool have[4] = { false, false, false, false };
        std::vector<float> rms;                // RMS po 5 ms (dB)
        float st[4] = {};                      // side/mid (dB) v 4 pasmach
        double midE[4] = {};
        bool hasSt = false;
    };

    struct Bands
    {
        int edge[kBands + 1];
        void init (int n, double sr)
        {
            for (int b = 0; b <= kBands; ++b)
            {
                const double f = 50.0 * std::pow (12000.0 / 50.0, (double) b / kBands);
                edge[b] = juce::jlimit (1, n / 2 - 1, (int) std::round (f * n / sr));
            }
            for (int b = 1; b <= kBands; ++b) if (edge[b] <= edge[b - 1]) edge[b] = edge[b - 1] + 1;
        }
    };

    struct Analyzer
    {
        juce::dsp::FFT fft { kOrder }, fftA { kOrderA };
        std::vector<float> win, winA, work, work2;
        Bands bd, bdA;
        int sb[5];                              // hranice 4 hrubych stereo pasiem (biny 2048)
        explicit Analyzer (double sr) : win ((size_t) kN), winA ((size_t) kNA), work ((size_t) kN * 2), work2 ((size_t) kN * 2)
        {
            for (int i = 0; i < kN; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kN);
            for (int i = 0; i < kNA; ++i) winA[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) kNA);
            bd.init (kN, sr); bdA.init (kNA, sr);
            const double fr[5] = { 50.0, 250.0, 1000.0, 4000.0, 12000.0 };
            for (int i = 0; i < 5; ++i) sb[i] = juce::jlimit (1, kN / 2 - 1, (int) std::round (fr[i] * kN / sr));
        }

        Feat analyse (const Rend& r)
        {
            Feat f;
            const int total = (int) r.M.size();
            // nabeh: prvy vzorok nad (peak - 35 dB)
            float pk = 0;
            for (int i = 0; i < total; ++i) pk = juce::jmax (pk, std::abs (r.L[(size_t) i]), std::abs (r.R[(size_t) i]));
            int on = 0;
            if (pk > 1.0e-9f)
            {
                const float thr = pk * 0.017783f;
                while (on < total - 1 && std::abs (r.L[(size_t) on]) <= thr && std::abs (r.R[(size_t) on]) <= thr) ++on;
            }
            f.onset = on;
            const int len = total - on;
            const float* M = r.M.data() + on;
            const float* L = r.L.data() + on;
            const float* R = r.R.data() + on;

            // obalka
            for (int s = 0; s + kRmsHop <= len; s += kRmsHop)
            {
                double e = 0;
                for (int i = 0; i < kRmsHop; ++i) e += (double) M[s + i] * M[s + i];
                const float d = (float) (10.0 * std::log10 (e / kRmsHop + 1e-12));
                f.rms.push_back (d);
                f.maxRms = juce::jmax (f.maxRms, d);
            }

            // nabeh: kratke FFT
            {
                double acc[kBands] = {}; int cnt = 0;
                for (int s = 0; s + kNA <= juce::jmin (len, kAttackLen); s += kHopA)
                {
                    std::fill (work.begin(), work.begin() + kNA * 2, 0.0f);
                    for (int i = 0; i < kNA; ++i) work[(size_t) i] = M[s + i] * winA[(size_t) i];
                    fftA.performFrequencyOnlyForwardTransform (work.data(), true);
                    for (int b = 0; b < kBands; ++b)
                    {
                        double p = 0;
                        for (int k = bdA.edge[b]; k < bdA.edge[b + 1]; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
                        acc[b] += p;
                        f.attMax = juce::jmax (f.attMax, (float) (10.0 * std::log10 (p + 1e-9)));
                    }
                    ++cnt;
                }
                if (cnt > 0)
                {
                    f.have[0] = true;
                    for (int b = 0; b < kBands; ++b) f.seg[0][b] = (float) (10.0 * std::log10 (acc[b] / cnt + 1e-9));
                }
            }

            // dlhe FFT: snimky po 512 vzorkach od nabehu
            double acc[4][kBands] = {}; int cnt[4] = {};
            double ms[2][4] = {}; int nst = 0;
            for (int s = 0, fi = 0; s + kN <= len; s += kHop, ++fi)
            {
                std::fill (work.begin(), work.end(), 0.0f);
                for (int i = 0; i < kN; ++i) work[(size_t) i] = M[s + i] * win[(size_t) i];
                fft.performFrequencyOnlyForwardTransform (work.data(), true);
                const int sg = (fi >= kEarly0 && fi <= kEarly1) ? 1 : (fi >= kSus0 && fi <= kSus1) ? 2 : (fi >= kRel0 ? 3 : -1);
                for (int b = 0; b < kBands; ++b)
                {
                    double p = 0;
                    for (int k = bd.edge[b]; k < bd.edge[b + 1]; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
                    const float d = (float) (10.0 * std::log10 (p + 1e-9));
                    f.energy += p;
                    f.db.push_back (d);
                    f.maxDb = juce::jmax (f.maxDb, d);
                    if (sg > 0) acc[sg][b] += p;
                }
                if (sg > 0) ++cnt[sg];
                ++f.frames;
                // stereo: v sustain kazdy druhy snimok
                if (sg == 2 && ((fi - kSus0) & 1) == 0)
                {
                    for (int a = 0; a < 4; ++a)
                    {
                        double p = 0;
                        for (int k = sb[a]; k < sb[a + 1]; ++k) p += (double) work[(size_t) k] * work[(size_t) k];
                        ms[0][a] += p;
                    }
                    std::fill (work2.begin(), work2.end(), 0.0f);
                    for (int i = 0; i < kN; ++i) work2[(size_t) i] = 0.5f * (L[s + i] - R[s + i]) * win[(size_t) i];
                    fft.performFrequencyOnlyForwardTransform (work2.data(), true);
                    for (int a = 0; a < 4; ++a)
                    {
                        double p = 0;
                        for (int k = sb[a]; k < sb[a + 1]; ++k) p += (double) work2[(size_t) k] * work2[(size_t) k];
                        ms[1][a] += p;
                    }
                    ++nst;
                }
            }
            for (int g = 1; g < 4; ++g)
                if (cnt[g] > 0)
                {
                    f.have[g] = true;
                    for (int b = 0; b < kBands; ++b) f.seg[g][b] = (float) (10.0 * std::log10 (acc[g][b] / cnt[g] + 1e-9));
                }
            if (nst > 0)
            {
                f.hasSt = true;
                for (int a = 0; a < 4; ++a)
                {
                    f.midE[a] = ms[0][a];
                    f.st[a] = juce::jlimit (-40.0f, 20.0f, (float) (10.0 * std::log10 ((ms[1][a] + 1e-12) / (ms[0][a] + 1e-12))));
                }
            }
            return f;
        }
    };

    // vzdialenost dvoch zvukov (v jednotkach 10 dB): farba po segmentoch, obalka, modulacia, stereo, snimky
    // hlasitost sa vyrovna (globalny posun), nabeh je zarovnany podla prveho vzorku nad peak - 35 dB
    struct Terms { double tim = 0, env = 0, mod = 0, st = 0, fw = 0, es = 0, ec = 0; float shift = 0; };

    float distance (const std::vector<Feat>& S, const std::vector<Feat>& C, Terms* T = nullptr)
    {
        double es = 0, ec = 0;
        for (auto& f : S) es += f.energy;
        for (auto& f : C) ec += f.energy;
        if (T != nullptr) { *T = Terms(); T->es = es; T->ec = ec; }
        if (! std::isfinite (es) || ! std::isfinite (ec)) { ++gDistNonFinite; return 99.0f; }
        if (ec <= 1e-12 || es <= 1e-12) return 99.0f;    // ticho
        const float shift = (float) (10.0 * std::log10 (es / ec));
        const double shiftLin = es / ec;
        if (T != nullptr) T->shift = shift;

        double total = 0; int notes = 0;
        double aTim = 0, aEnv = 0, aMod = 0, aSt = 0, aFw = 0;
        for (size_t k = 0; k < S.size() && k < C.size(); ++k)
        {
            const Feat& s = S[k]; const Feat& c = C[k];
            const float floorDb = s.maxDb - 60.0f, floorA = s.attMax - 60.0f;
            const int fr = juce::jmin (s.frames, c.frames);

            // 1) farba: spektrum segmentov bez priemeru cez pasma
            double tim = 0; int nTim = 0;
            for (int g = 0; g < 4; ++g)
            {
                if (! s.have[g] || ! c.have[g]) continue;
                const float fl = g == 0 ? floorA : floorDb;
                float a[kBands], cc[kBands]; double ma = 0, mc = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    a[b] = juce::jmax (s.seg[g][b], fl); cc[b] = juce::jmax (c.seg[g][b] + shift, fl);
                    ma += a[b]; mc += cc[b];
                }
                ma /= kBands; mc /= kBands;
                double err = 0, ws = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    if (a[b] <= fl + 1.0e-3f && cc[b] <= fl + 1.0e-3f) continue;
                    const double w = juce::jlimit (0.1f, 1.0f, (a[b] - fl) / 40.0f);
                    err += w * std::abs ((a[b] - ma) - (cc[b] - mc));
                    ws += w;
                }
                if (ws > 0) { tim += err / ws; ++nTim; }
            }
            const double timbre = nTim > 0 ? tim / nTim / 10.0 : 0.0;

            // 2) obalka: RMS po 5 ms, nabeh 3x
            double env = 0, ew = 0;
            {
                const int nb = (int) juce::jmin (s.rms.size(), c.rms.size());
                const float fl = s.maxRms - 70.0f;
                for (int i = 0; i < nb; ++i)
                {
                    const float a = juce::jmax (s.rms[(size_t) i], fl), cc = juce::jmax (c.rms[(size_t) i] + shift, fl);
                    const double w = i < kAttackLen / kRmsHop ? 3.0 : 1.0;
                    env += w * juce::jmin (std::abs (a - cc), 30.0f);
                    ew += w;
                }
            }
            const double envelope = ew > 0 ? env / ew / 10.0 : 0.0;

            // 3) modulacia: casova smerodajna odchylka dB v pasmach cez sustain
            double modulation = 0;
            {
                const int f0 = kSus0, f1 = juce::jmin (kSus1, fr - 1);
                if (f1 - f0 >= 7)
                {
                    const int n = f1 - f0 + 1;
                    double err = 0, ws = 0;
                    for (int b = 0; b < kBands; ++b)
                    {
                        double sa = 0, sa2 = 0, sc = 0, sc2 = 0;
                        for (int t = f0; t <= f1; ++t)
                        {
                            const double a = juce::jmax (s.db[(size_t) (t * kBands + b)], floorDb);
                            const double cc = juce::jmax (c.db[(size_t) (t * kBands + b)] + shift, floorDb);
                            sa += a; sa2 += a * a; sc += cc; sc2 += cc * cc;
                        }
                        const double ma = sa / n, mc = sc / n;
                        if (ma <= floorDb + 0.5 && mc <= floorDb + 0.5) continue;
                        const double sdA = std::sqrt (juce::jmax (0.0, sa2 / n - ma * ma)), sdC = std::sqrt (juce::jmax (0.0, sc2 / n - mc * mc));
                        const double w = juce::jlimit (0.1, 1.0, (ma - floorDb) / 40.0);
                        err += w * std::abs (sdA - sdC);
                        ws += w;
                    }
                    if (ws > 0) modulation = err / ws / 10.0;
                }
            }

            // 4) stereo: side/mid v 4 pasmach
            double stereo = 0;
            if (s.hasSt && c.hasSt)
            {
                double mx = 0; for (double e : s.midE) mx = juce::jmax (mx, e);
                double err = 0; int n = 0;
                for (int a = 0; a < 4; ++a)
                {
                    if (s.midE[a] < mx * 1.0e-6 && c.midE[a] * shiftLin < mx * 1.0e-6) continue;
                    err += juce::jmin (std::abs (s.st[a] - c.st[a]), 20.0f);
                    ++n;
                }
                if (n > 0) stereo = err / n / 10.0;
            }

            // 5) snimok po snimku (zachytava pohyb filtra)
            double fwe = 0, fws = 0;
            for (int t = 0; t < fr; ++t)
            {
                const double w = t < 10 ? 2.0 : 1.0;
                for (int b = 0; b < kBands; ++b)
                {
                    const float a = juce::jmax (s.db[(size_t) (t * kBands + b)], floorDb);
                    const float cc = juce::jmax (c.db[(size_t) (t * kBands + b)] + shift, floorDb);
                    fwe += w * std::abs (a - cc);
                }
                fws += w * kBands;
            }
            const double framewise = fws > 0 ? fwe / fws / 10.0 : 0.0;

            total += 1.0 * timbre + 0.5 * envelope + 0.4 * modulation + 0.2 * stereo + 0.2 * framewise;
            aTim += timbre; aEnv += envelope; aMod += modulation; aSt += stereo; aFw += framewise;
            ++notes;
        }
        if (notes <= 0) return 99.0f;
        const double res = total / notes;
        if (! std::isfinite (res)) { ++gDistNonFinite; return 99.0f; }
        if (T != nullptr) { T->tim = aTim / notes; T->env = aEnv / notes; T->mod = aMod / notes; T->st = aSt / notes; T->fw = aFw / notes; }
        return (float) res;
    }

    //==========================================================================
    // parametre Inspire, ktore sa hladaju
    enum Kind { Lin, Log, Int, Choice };
    struct Dim { const char* id; float lo, hi; Kind kind; std::vector<float> ch; };

    std::vector<Dim> dims()
    {
        return {
            { "a_wave", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
            { "a_wt", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6, 7 } },
            { "a_oct", -2, 1, Int, {} },
            { "a_uni", 1, 8, Int, {} },
            { "a_spread", 0, 40, Lin, {} },
            { "a_color", 0, 1, Lin, {} },
            { "a_ratio", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9 } },
            { "a_cut", 80, 20000, Log, {} },
            { "a_reso", 0, 0.85f, Lin, {} },
            { "a_ftype", 0, 0, Choice, { 0, 1, 2 } },
            { "a_f24", 0, 0, Choice, { 0, 1 } },
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
            { "a_ltgt", 0, 0, Choice, { 0, 1, 2, 3 } },
            { "a_ldelay", 0, 1.5f, Lin, {} },
            { "a_penv", -12, 12, Lin, {} },       // skok vysky na zaciatku tonu
            { "a_ptime", 0.005f, 0.5f, Log, {} },
            { "a_ktrk", 0, 1, Lin, {} },          // filter podla klavesu
            { "a_noise", 0, 0.6f, Lin, {} },      // primiesany sum
            { "b_on", 0, 0, Choice, { 0, 1 } },
            { "b_wave", 0, 0, Choice, { 0, 1, 2, 3, 4, 5, 6 } },
            { "b_oct", -2, 3, Int, {} },
            { "b_semi", 0, 0, Choice, { 0, 7 } },
            { "b_ad", 0.01f, 8, Log, {} },
            { "b_as", 0, 1, Lin, {} },
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

    int dimIndex (const std::vector<Dim>& D, const char* id)
    {
        for (size_t i = 0; i < D.size(); ++i) if (juce::String (D[i].id) == id) return (int) i;
        return 0;
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
        std::set<juce::String> missing;       // id, ktore stav Inspire nepozna
        void set (const juce::String& id, float v)
        {
            auto it = byId.find (id);
            if (it != byId.end()) it->second.setProperty ("value", v, nullptr);
            else missing.insert (id);
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
    void applyFixed (Patch& p, const std::vector<Dim>& D, const std::vector<float>& x, float gainDb)
    {
        for (size_t i = 0; i < D.size(); ++i) p.set (D[i].id, decode (D[i], x[i]));
        auto v = [&] (const char* id) { for (size_t i = 0; i < D.size(); ++i) if (juce::String (D[i].id) == id) return decode (D[i], x[i]); return 0.0f; };
        const std::pair<const char*, float> fixed[] = {
            { "a_on", 1 }, { "a_level", 0.8f }, { "a_semi", 0 }, { "a_fine", 0 }, { "a_pan", 0 },
            { "a_lshape", 0 }, { "a_src", 0 }, { "a_fvel", 0.5f }, { "a_lsync", 0 }, { "b_lsync", 0 },
            { "b_penv", 0 }, { "b_ptime", 0.08f }, { "b_fvel", 0.5f }, { "b_noise", 0 },
            { "b_fine", 0 }, { "b_pan", 0 }, { "b_ftype", 0 }, { "b_f24", 0 }, { "b_ldepth", 0 }, { "b_src", 0 }, { "b_root", 60 }, { "b_loop", 0 },
            { "bright", 0.5f }, { "attack", 0.5f }, { "release", 0.5f }, { "space", 0.5f }, { "width", 0.5f }, { "volume", 0 }, { "p_gain", gainDb }, { "veltrack", 0.7f },
            { "morph_x", 0 }, { "morph_y", 0 }, { "dl_on", 0 }, { "arp_on", 0 }, { "mono", 0 }, { "glide", 0 }, { "mf_on", 0 }, { "comp_on", 0 },
            { "eq_on", 1 }, { "eq_mid", 0 }, { "fx_width", 1 }, { "fx_punch", 0.5f }, { "ch_fb", 0.3f }, { "ch_rate", 0.6f }, { "ch_depth", 0.5f },
            { "rv_pre", 0 }, { "rv_damp", 0.4f } };
        for (auto& f : fixed) p.set (f.first, f.second);
        for (auto pre : { "a_", "b_", "rv_", "dl_" })
            for (auto s : { "eql", "eqm", "eqh" }) p.set (juce::String (pre) + s, 0);
        // vrstva B zdiela filter-obalku, nabeh, doznenie a FM pomer s vrstvou A (pokles a drzanie ma vlastne)
        for (auto s : { "fenv", "fa", "fd", "fs", "fr", "aa", "ar", "ratio", "ktrk" })
            p.set (juce::String ("b_") + s, v ((juce::String ("a_") + s).toRawUTF8()));
        p.set ("ch_on", v ("ch_mix") > 0.03f ? 1.0f : 0.0f);
        p.set ("rv_on", v ("rv_mix") > 0.02f ? 1.0f : 0.0f);
        p.set ("drive_on", v ("drive") > 0.05f ? 1.0f : 0.0f);
    }

    const int kNotes[] = { 48, 60, 72 };
    constexpr double kHold = 1.4, kTail = 0.9;

    //==========================================================================
    // analyticke odhady zo zdroja (nota 60, dlhe drzanie): obalka podla modelu Inspire, vyska, orezanie, LFO
    // Inspire: nabeh linearny (cas = a), pokles a doznenie exponencialne s tau = 0.3 * parameter;
    // pluck: "Pokles" = T60 struny (tau = T60 / 6.9078), nasobok (220/f0)^0.4; FM: pokles * (220/f0)^0.3
    struct Est
    {
        bool env = false, oct = false, cut = false, lfo = false;
        float aa = 0.01f, ad = 1.0f, adPluck = 1.0f, s = 1.0f, ar = 0.3f;
        int octave = 0; float cutHz = 20000.0f, lfoHz = 5.0f, f0 = 0.0f;
    };

    bool lnSlope (const std::vector<double>& t, const std::vector<double>& y, double& slope)
    {
        const size_t n = t.size();
        if (n < 3) return false;
        double st = 0, sy = 0, stt = 0, sty = 0;
        for (size_t i = 0; i < n; ++i) { st += t[i]; sy += y[i]; stt += t[i] * t[i]; sty += t[i] * y[i]; }
        const double den = (double) n * stt - st * st;
        if (den <= 1.0e-12) return false;
        slope = ((double) n * sty - st * sy) / den;
        return true;
    }

    Est estimate (const Rend& r, double hold, double sr, const Feat& F, const Feat& F60)
    {
        Est e;
        const int total = (int) r.M.size();
        constexpr int W = 128, H = 32;
        if (total < W * 4) return e;
        float pkM = 0;
        for (float x : r.M) pkM = juce::jmax (pkM, std::abs (x));
        if (pkM < 1.0e-6f) return e;
        int on = 0;
        { const float thr = pkM * 0.017783f; while (on < total - 1 && std::abs (r.M[(size_t) on]) <= thr) ++on; }
        std::vector<double> cs ((size_t) total + 1, 0.0);
        for (int i = 0; i < total; ++i) cs[(size_t) i + 1] = cs[(size_t) i] + (double) r.M[(size_t) i] * r.M[(size_t) i];
        const int nEnv = (total - W) / H + 1;
        std::vector<float> env ((size_t) nEnv);
        for (int i = 0; i < nEnv; ++i) env[(size_t) i] = (float) std::sqrt ((cs[(size_t) (i * H + W)] - cs[(size_t) (i * H)]) / W);
        auto tOf = [&] (int i) { return ((double) i * H + W * 0.5) / sr; };
        auto iOf = [&] (double t) { return juce::jlimit (0, nEnv - 1, (int) ((t * sr - W * 0.5) / H)); };
        const double tOn = (double) on / sr, tOff = hold;

        // vrchol obalky pocas drzania
        int ip = iOf (tOn); float peak = 0;
        for (int i = iOf (tOn); i <= iOf (tOff - 0.02); ++i) if (env[(size_t) i] > peak) { peak = env[(size_t) i]; ip = i; }
        if (peak < 1.0e-6f) return e;

        // nabeh: 10-90 % cas / 0.8
        int i10 = -1, i90 = -1;
        for (int i = iOf (tOn); i <= ip; ++i)
        {
            if (i10 < 0 && env[(size_t) i] >= 0.1f * peak) i10 = i;
            if (env[(size_t) i] >= 0.9f * peak) { i90 = i; break; }
        }
        e.aa = (i10 >= 0 && i90 >= i10) ? (float) juce::jlimit (0.0005, 3.0, (tOf (i90) - tOf (i10) - 0.0025) / 0.8) : 0.005f;

        // drzanie a pokles
        double sum = 0; int cn = 0;
        for (int i = iOf (tOff - 0.35); i <= iOf (tOff - 0.05); ++i) { sum += env[(size_t) i]; ++cn; }
        const double sEnd = cn > 0 ? sum / cn / peak : 1.0;
        std::vector<double> tv, yv;
        for (int i = iOf (tOff - 1.2); i <= iOf (tOff - 0.05); ++i) { tv.push_back (tOf (i)); yv.push_back (std::log ((double) env[(size_t) i] + 1.0e-9)); }
        double tailSlope = 0;
        const bool decaying = lnSlope (tv, yv, tailSlope) && tailSlope < -0.35;
        double tau = 1.0;
        tv.clear(); yv.clear();
        const double tp = tOf (ip) + 0.01;
        if (decaying)
        {
            e.s = 0;
            for (int i = iOf (tp); i <= iOf (tOff - 0.05); ++i)
                if (env[(size_t) i] >= 0.03f * peak) { tv.push_back (tOf (i)); yv.push_back (std::log ((double) env[(size_t) i])); }
            double sl;
            tau = (lnSlope (tv, yv, sl) && sl < -1.0e-3) ? -1.0 / sl : 5.0;
        }
        else
        {
            const double s = juce::jlimit (0.0, 1.0, sEnd);
            e.s = (float) s;
            if (1.0 - s > 0.08)
            {
                for (int i = iOf (tp); i <= iOf (tOff - 0.05); ++i)
                {
                    const double ex = (double) env[(size_t) i] / peak - s;
                    if (ex >= 0.05 * (1.0 - s)) { tv.push_back (tOf (i)); yv.push_back (std::log (ex)); }
                }
                double sl;
                tau = (lnSlope (tv, yv, sl) && sl < -1.0e-3) ? -1.0 / sl : 0.3;
            }
        }
        tau = juce::jlimit (0.003, 20.0, tau);
        e.ad = (float) juce::jlimit (0.01, 8.0, tau / 0.3);
        e.adPluck = (float) juce::jlimit (0.01, 8.0, tau * 6.9078 / 0.933);

        // doznenie: pociatocny sklon po pusteni
        {
            double sumO = 0; int co = 0;
            for (int i = iOf (tOff - 0.012); i <= iOf (tOff - 0.002); ++i) { sumO += env[(size_t) i]; ++co; }
            const double lvOff = co > 0 ? sumO / co : 0.0;
            tv.clear(); yv.clear();
            for (int i = iOf (tOff + 0.004); i <= iOf (tOff + 0.5); ++i)
            {
                if (env[(size_t) i] < 0.03 * lvOff) break;
                tv.push_back (tOf (i)); yv.push_back (std::log ((double) env[(size_t) i]));
            }
            double sl, tauR = 5.0;
            if (tv.size() >= 3) { if (lnSlope (tv, yv, sl) && sl < -0.2) tauR = -1.0 / sl; }
            else tauR = 0.004;
            e.ar = (float) juce::jlimit (0.01, 6.0, tauR / 0.3);
        }
        e.env = true;

        // vyska: autokorelacia ustaleneho useku
        {
            const int Nw = 3072, lmin = (int) (sr / 1500.0), lmax = (int) (sr / 30.0);
            int s0 = on + (int) (0.25 * sr);
            if (s0 + Nw + lmax >= (int) (tOff * sr)) s0 = on + (int) (0.05 * sr);
            if (s0 + Nw + lmax < total)
            {
                const float* x = r.M.data() + s0;
                double e0 = 0; for (int i = 0; i < Nw; ++i) e0 += (double) x[i] * x[i];
                std::vector<double> rr ((size_t) lmax + 2, 0.0);
                for (int l = lmin; l <= lmax; ++l)
                {
                    double c = 0, el = 0;
                    for (int i = 0; i < Nw; ++i) { c += (double) x[i] * x[i + l]; el += (double) x[i + l] * x[i + l]; }
                    rr[(size_t) l] = c / std::sqrt (e0 * el + 1.0e-18);
                }
                int l0 = lmin; while (l0 <= lmax && rr[(size_t) l0] > 0.0) ++l0;
                double rmax = 0;
                for (int l = l0; l <= lmax; ++l) rmax = juce::jmax (rmax, rr[(size_t) l]);
                if (e0 > 1.0e-10 && rmax > 0.6)
                {
                    for (int l = l0; l < lmax; ++l)
                        if (rr[(size_t) l] >= 0.92 * rmax && rr[(size_t) l] >= rr[(size_t) l - 1] && rr[(size_t) l] >= rr[(size_t) l + 1])
                        {
                            e.f0 = (float) (sr / l);
                            e.octave = juce::roundToInt (std::log2 (e.f0 / 261.626f));
                            e.oct = true;
                            break;
                        }
                }
            }
        }

        // orezanie: najvyssie pasmo do -30 dB od vrcholu sustainu (pila + 12 dB filter => fc ~ fhi / 2.5)
        if (F60.have[2])
        {
            float mx = -1000; for (int b = 0; b < kBands; ++b) mx = juce::jmax (mx, F60.seg[2][b]);
            int hb = 0; for (int b = 0; b < kBands; ++b) if (F60.seg[2][b] >= mx - 30.0f) hb = b;
            const double fhi = 50.0 * std::pow (12000.0 / 50.0, ((double) hb + 0.5) / kBands);
            e.cutHz = fhi >= 8000.0 ? 20000.0f : (float) juce::jlimit (400.0, 20000.0, fhi / 2.5);
            e.cut = true;
        }

        // LFO: dominantna modulacia 0.6-10 Hz hladin pasiem v sustaine
        {
            const double hop = (double) kHop / sr;
            const int f0i = (int) std::ceil ((0.8 * sr - kN / 2) / kHop);
            const int f1i = juce::jmin (F.frames - 1, (int) std::floor (((tOff - tOn - 0.15) * sr - kN / 2) / kHop));
            const int N = f1i - f0i + 1;
            if (N >= 64)
            {
                std::vector<double> P (95, 0.0), yy ((size_t) N);
                int nb = 0;
                for (int b = 0; b < kBands; ++b)
                {
                    double mean = 0;
                    for (int t = 0; t < N; ++t) { yy[(size_t) t] = F.db[(size_t) ((f0i + t) * kBands + b)]; mean += yy[(size_t) t]; }
                    mean /= N;
                    if (mean < F.maxDb - 40.0f) continue;
                    // odstranenie linearneho trendu
                    double st = 0, sy = 0, stt = 0, sty = 0;
                    for (int t = 0; t < N; ++t) { st += t; sy += yy[(size_t) t]; stt += (double) t * t; sty += t * yy[(size_t) t]; }
                    const double den = N * stt - st * st, sl = (N * sty - st * sy) / den, ic = (sy - sl * st) / N;
                    for (int t = 0; t < N; ++t) yy[(size_t) t] -= ic + sl * t;
                    ++nb;
                    for (int k = 0; k < 95; ++k)
                    {
                        const double w = juce::MathConstants<double>::twoPi * (0.6 + 0.1 * k) * hop;
                        double re = 0, im = 0;
                        for (int t = 0; t < N; ++t) { re += yy[(size_t) t] * std::cos (w * t); im += yy[(size_t) t] * std::sin (w * t); }
                        P[(size_t) k] += re * re + im * im;
                    }
                }
                if (nb > 0)
                {
                    int kp = 0; double mp = 0, mean = 0;
                    for (int k = 0; k < 95; ++k) { mean += P[(size_t) k]; if (P[(size_t) k] > mp) { mp = P[(size_t) k]; kp = k; } }
                    mean /= 95.0;
                    const double A = 2.0 * std::sqrt (mp / nb) / N;
                    if (kp > 0 && kp < 94 && mean > 0 && mp / mean >= 6.0 && A >= 0.5) { e.lfo = true; e.lfoHz = (float) (0.6 + 0.1 * kp); }
                }
            }
        }
        return e;
    }

    struct Family { const char* name; std::vector<int> waves; };
    const std::vector<Family>& families()
    {
        static const std::vector<Family> f = {
            { "píla", { 2 } }, { "obdĺžnik", { 3 } }, { "FM", { 6 } }, { "pluck", { 5 } }, { "sínus a trojuholník", { 0, 1 } }, { "klavír", { 7 } }, { "roj", { 8 } }, { "wavetable", { 9 } } };
        return f;
    }
    int familyOf (int wave)
    {
        const auto& fs = families();
        for (size_t i = 0; i < fs.size(); ++i)
            for (int w : fs[i].waves) if (w == wave) return (int) i;
        return -1;
    }

    // sampel do FLAC (24 bit, 48 kHz, stereo) so zadanym ziskom (spolocny pre cely zdrojovy ton)
    juce::MemoryBlock makeFlac (juce::AudioBuffer<float> b, float gain)
    {
        juce::MemoryBlock flac;
        std::unique_ptr<juce::OutputStream> os (new juce::MemoryOutputStream (flac, false));
        juce::FlacAudioFormat fmt;
        if (auto w = fmt.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (48000.0).withNumChannels (2).withBitsPerSample (24)))
        {
            b.applyGain (gain);
            w->writeFromAudioSampleBuffer (b, 0, b.getNumSamples());
        }
        return flac;
    }
}

//==============================================================================
juce::File diagFolder()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("Korgus Sequencer").getChildFile ("Ucenie");
}

namespace
{
    struct FixedHead : juce::AudioPlayHead
    {
        juce::Optional<PositionInfo> getPosition() const override
        {
            PositionInfo i;
            i.setBpm (120.0); i.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            i.setIsPlaying (false); i.setIsRecording (false); i.setIsLooping (false);
            i.setTimeInSamples (0); i.setTimeInSeconds (0.0); i.setPpqPosition (0.0);
            return i;
        }
    };

    void logPlugin (const juce::String& tag, juce::AudioPluginInstance& p)
    {
        dlog (tag + ": name=" + p.getName() + "  sampleRate=" + juce::String (p.getSampleRate()) + "  blockSize=" + juce::String (p.getBlockSize())
              + "  latency=" + juce::String (p.getLatencySamples()) + "  totalIn=" + juce::String (p.getTotalNumInputChannels())
              + "  totalOut=" + juce::String (p.getTotalNumOutputChannels()) + "  isNonRealtime=" + juce::String ((int) p.isNonRealtime())
              + "  buses in/out=" + juce::String (p.getBusCount (true)) + "/" + juce::String (p.getBusCount (false)));
        for (int io = 0; io < 2; ++io)
            for (int i = 0; i < p.getBusCount (io == 0); ++i)
                if (auto* b = p.getBus (io == 0, i))
                    dlog (juce::String ("   ") + (io == 0 ? "in " : "out ") + juce::String (i) + " '" + b->getName() + "' enabled=" + juce::String ((int) b->isEnabled())
                          + " channels=" + juce::String (b->getNumberOfChannels()) + " layout=" + b->getCurrentLayout().getDescription());
        dlog ("   main out channel index in process buffer: L=" + juce::String (p.getChannelIndexInProcessBlockBuffer (false, 0, 0))
              + " R=" + juce::String (p.getChannelIndexInProcessBlockBuffer (false, 0, 1)));
    }

    void logRend (const juce::String& tag, const Rend& r, const Feat& f)
    {
        float pl = 0, pr = 0, pm = 0; double ss = 0, dc = 0;
        for (size_t i = 0; i < r.M.size(); ++i)
        {
            pl = juce::jmax (pl, std::abs (r.L[i])); pr = juce::jmax (pr, std::abs (r.R[i])); pm = juce::jmax (pm, std::abs (r.M[i]));
            ss += (double) r.M[i] * r.M[i]; dc += r.M[i];
        }
        const double n = juce::jmax (1.0, (double) r.M.size());
        dlog (tag + ": peakL=" + juce::String (pl, 5) + " peakR=" + juce::String (pr, 5) + " peakM=" + juce::String (pm, 5)
              + " rms=" + juce::String (std::sqrt (ss / n), 6) + " dcMean=" + juce::String (dc / n, 7) + " nonFinite=" + juce::String (r.nonFinite)
              + " onsetIdx=" + juce::String (f.onset) + " energy=" + juce::String (f.energy, 4) + " maxDb=" + juce::String (f.maxDb, 1)
              + " frames=" + juce::String (f.frames) + " samples=" + juce::String ((int) r.M.size()));
    }

    juce::String fmtTerms (const Terms& t, float loss, float pk)
    {
        return "loss=" + juce::String (loss, 4) + " [timbre=" + juce::String (t.tim, 4) + " envelope=" + juce::String (t.env, 4) + " modulation=" + juce::String (t.mod, 4)
               + " stereo=" + juce::String (t.st, 4) + " framewise=" + juce::String (t.fw, 4) + "] shift=" + juce::String (t.shift, 2) + "dB es=" + juce::String (t.es, 4)
               + " ec=" + juce::String (t.ec, 4) + " candPeak=" + juce::String (pk, 5);
    }
}

Result run (juce::AudioPluginInstance& src, juce::AudioPluginInstance& insp, const juce::MemoryBlock& inspState,
            bool allowSample, const juce::String& srcName, Progress progress)
{
    Result res;
    Diag diag; diag.open();
    gDiag = &diag; gTimeouts = 0; gDistNonFinite = 0;
    FixedHead head;
    src.setPlayHead (&head); insp.setPlayHead (&head);
    struct Guard { juce::AudioPluginInstance &a, &b; ~Guard() { a.setPlayHead (nullptr); b.setPlayHead (nullptr); gDiag = nullptr; } } guard { src, insp };
    res.diag = diag.dir.getFullPathName();
    juce::String fatalMsg;
    const juce::String timeoutMsg = juce::String::fromUTF8 ("Nástroj neodpovedá (vypršal časový limit). Skús to znova.");
    auto prog = [&] (double f, const juce::String& m) { return fatalMsg.isEmpty() && (progress == nullptr || progress (f, m)); };
    auto bail = [&] { res.error = fatalMsg.isNotEmpty() ? fatalMsg : juce::String ("cancel"); dlog ("END: " + res.error); return res; };
    const bool dbg = std::getenv ("SM_DEBUG") != nullptr;
    Analyzer an (kSR);
    dlog ("=== Korgus Sequencer - ucenie zvuku " + juce::Time::getCurrentTime().toString (true, true) + " ===");
    dlog ("zdrojovy plugin: " + srcName);
    logPlugin ("src (pred prepare 48k)", src);
    logPlugin ("inspire", insp);

    // 1) zdroj zahra tony (niektore pluginy nacitavaju zvuky na pozadi - pockame)
    //    cudzi plugin nebezi na 32 kHz: renderuje sa na 48 kHz a pre analyzu sa prevzorkuje na 32 kHz
    prog (0.0, juce::String::fromUTF8 ("Počúvam zdrojový nástroj…"));
    if (! onMessageThread ([&] { src.releaseResources(); src.setNonRealtime (true); src.setRateAndBufferSizeDetails (48000.0, kBS); src.prepareToPlay (48000.0, kBS); }))
    { fatalMsg = timeoutMsg; return bail(); }
    logPlugin ("src (po prepare 48k)", src);

    juce::AudioBuffer<float> raw48;
    auto capNote = [&] (int note, double hold, double tail, juce::AudioBuffer<float>* keep48)
    {
        auto r48 = renderNote (src, note, 0.8f, hold, tail, 48000.0, keep48, false);
        return prepareSource (r48);
    };
    std::vector<Rend> srcAudio;
    std::vector<Feat> S;
    {
        const auto t0 = juce::Time::getMillisecondCounter();
        juce::uint32 tSound = 0; bool sounding = false; int okRun = 0;
        Rend A, B; Feat fa, fb;
        for (int attempt = 1;; ++attempt)
        {
            A = capNote (60, kHold, kTail, nullptr);
            float pk = 0; for (float x : A.M) pk = juce::jmax (pk, std::abs (x));
            const auto now = juce::Time::getMillisecondCounter();
            juce::String wait;
            if (pk > 1.0e-4f)
            {
                if (! sounding) { sounding = true; tSound = now; }
                B = capNote (60, kHold, kTail, nullptr);
                fa = an.analyse (A); fb = an.analyse (B);
                const float dd = distance ({ fa }, { fb });
                dlog ("gate #" + juce::String (attempt) + " t=" + juce::String ((int) (now - t0)) + "ms: peak=" + juce::String (pk, 5) + " distance(60a,60b)=" + juce::String (dd, 4));
                // sum (nahodne fazy hlasov) dava pri dvoch rendroch tej istej noty vzdialenost 0.3 - 0.5: pri rovnakej hlasitosti
                // (do 3 dB) a vzdialenosti < 0.6 trikrat po sebe to povazujeme za ustalene
                const double dbDiff = 10.0 * std::log10 ((fa.energy + 1.0e-12) / (fb.energy + 1.0e-12));
                const bool noisyOk = dd < 0.6f && std::abs (dbDiff) < 3.0;
                okRun = noisyOk ? okRun + 1 : 0;
                if (dd < 0.3f || okRun >= 3) { if (dd >= 0.3f) dlog ("zdroj ustaleny (sumivy zvuk: 3x po sebe vzdialenost < 0.6 a rozdiel hlasitosti < 3 dB)"); break; }
                if (now - tSound > 30000)
                {
                    dlog ("VAROVANIE: zdroj sa neustalil (distance " + juce::String (dd, 4) + " >= 0.3), pokracujem aj tak");
                    break;
                }
                wait = juce::String::fromUTF8 ("Čakám, kým sa zvuk ustáli…");
            }
            else
            {
                dlog ("gate #" + juce::String (attempt) + ": ticho (peak=" + juce::String (pk, 7) + ")");
                if (now - t0 > 60000) { res.error = juce::String::fromUTF8 ("Zdrojová stopa nehrá žiadny zvuk."); dlog ("END: " + res.error); return res; }
                wait = juce::String::fromUTF8 ("Čakám, kým sa nástroj načíta…");
            }
            if (! prog (0.0, wait)) return bail();
            juce::Thread::sleep (300);
        }
        dlog ("zdroj nota 60 (gate): ");
        logRend ("  60a", A, fa); logRend ("  60b", B, fb);
        srcAudio.resize (3); S.resize (3);
        srcAudio[1] = A; S[1] = fa;
    }
    for (int k : { 0, 2 })
    {
        srcAudio[(size_t) k] = capNote (kNotes[k], kHold, kTail, nullptr);
        S[(size_t) k] = an.analyse (srcAudio[(size_t) k]);
    }
    for (int k = 0; k < 3; ++k)
    {
        logRend ("zdroj nota " + juce::String (kNotes[k]) + " (32 kHz analyza)", srcAudio[(size_t) k], S[(size_t) k]);
        writeWav ("learn-src-" + juce::String (kNotes[k]) + ".wav", toBuffer (srcAudio[(size_t) k]), kSR);
    }
    dlog ("distance(S,S) = " + juce::String (distance (S, S), 6) + "  (ma byt 0)");
    const std::vector<Feat> Sq { S[1] };    // rychle hodnotenie: iba nota 60

    // analyticke odhady z dlhsieho tonu 60 (drzanie 4 s + doznenie 3 s); rovnaky 48 kHz zaznam sa pouzije aj pre sampel
    if (! prog (0.01, juce::String::fromUTF8 ("Analyzujem obálku a výšku tónu…"))) return bail();
    Est est;
    {
        auto lg = capNote (60, 4.0, 3.0, &raw48);
        writeWav ("learn-src-48k.wav", raw48, 48000.0);
        const Feat Fl = an.analyse (lg);
        logRend ("zdroj nota 60 dlha (4 s + 3 s)", lg, Fl);
        est = estimate (lg, 4.0, kSR, Fl, S[1]);
        dbgf ("EST env=%d aa=%.4f ad=%.3f adP=%.3f s=%.3f ar=%.3f | oct=%d(%d) f0=%.1f | cut=%d %.0f | lfo=%d %.1f\n", est.env, est.aa, est.ad, est.adPluck, est.s, est.ar,
              est.octave, est.oct, est.f0, est.cut, est.cutHz, est.lfo, est.lfoHz);
        dlog ("autokorelacia f0 (nota 60, ocakavane ~261.6 Hz): " + (est.oct ? juce::String (est.f0, 1) + " Hz" : juce::String ("nenajdena")));
    }

    // 2) hladanie nastavenia Inspire
    StateCodec codec;
    auto tree0 = codec.decode (inspState);
    if (! tree0.isValid()) { res.error = juce::String::fromUTF8 ("Stav Inspirie sa nedá prečítať."); return res; }
    Patch patch (tree0);
    patch.codec = &codec;
    dlog ("Inspire stav: vst3=" + juce::String ((int) codec.vst3) + " strom=" + tree0.getType().toString() + " ver="
          + (tree0.hasProperty ("ver") ? tree0.getProperty ("ver").toString() : juce::String ("CHYBA")) + " pocet id=" + juce::String ((int) patch.byId.size()));
    if (! tree0.hasProperty ("ver")) { res.error = juce::String::fromUTF8 ("Inspiria má inú verziu – aktualizuj program."); dlog ("END: chyba 'ver' v stave Inspire"); return res; }
    patch.tree.setProperty ("smpB", juce::String(), nullptr);
    patch.tree.setProperty ("smpBName", juce::String(), nullptr);
    const auto D = dims();
    const int aw = dimIndex (D, "a_wave");
    float curGain = -10.0f;           // hladanie bezi s p_gain = -10 dB (pred-FX zisk pred tube/drive); konecna hlasitost sa nastavi na konci
    double cost = 0;                  // vykon v "plnych" vyhodnoteniach (3 tony); rychle = 1 ton
    int quickEvals = 0;

    Terms lastT; float lastPk = 0; int n99 = 0, nNonFiniteRend = 0;
    auto evalPatch = [&] (std::vector<Feat>* outC = nullptr, std::vector<Rend>* outAudio = nullptr, bool quick = false) -> float
    {
        if (fatalMsg.isNotEmpty()) return 99.0f;
        // cisty start: doznievajuce hlasy z predosleho vyhodnotenia by menili, ktory hlas (a jeho nahodne cisla) zahra novu notu
        // stav, reset aj prepare bezia na hlavnom vlakne; druhe volanie pocka, kym dobehne asynchronne spracovanie stavu
        const auto mb = patch.binary();
        bool okm = onMessageThread ([&] { insp.releaseResources(); insp.setNonRealtime (true); insp.prepareToPlay (kSR, kBS); insp.setStateInformation (mb.getData(), (int) mb.getSize()); });
        if (okm) okm = onMessageThread ([&] { insp.reset(); });
        if (! okm) { fatalMsg = timeoutMsg; ++n99; return 99.0f; }
        std::vector<Feat> C;
        lastPk = 0; bool nf = false;
        auto note = [&] (int n, std::vector<Rend>* keepA)
        {
            auto a = renderNote (insp, n, 0.8f, kHold, kTail, kSR);
            for (size_t i = 0; i < a.L.size(); ++i) lastPk = juce::jmax (lastPk, std::abs (a.L[i]), std::abs (a.R[i]));
            if (a.nonFinite > 0) nf = true;
            C.push_back (an.analyse (a));
            if (keepA != nullptr) keepA->push_back (std::move (a));
        };
        if (quick)
        {
            note (kNotes[1], nullptr);
            cost += 0.35; ++quickEvals;
        }
        else
        {
            for (int n : kNotes) note (n, outAudio);
            cost += 1.0;
        }
        if (nf) ++nNonFiniteRend;
        ++res.evals;
        const float d = distance (quick ? Sq : S, C, &lastT);
        if (d >= 99.0f) ++n99;
        if (outC != nullptr) *outC = std::move (C);
        return d;
    };
    auto evalX = [&] (const std::vector<float>& x) { applyFixed (patch, D, x, curGain); return evalPatch(); };
    auto evalXq = [&] (const std::vector<float>& x) { applyFixed (patch, D, x, curGain); return evalPatch (nullptr, nullptr, true); };

    struct Cand { std::vector<float> x; float loss; };
    std::mt19937 rng ((unsigned) juce::Time::getMillisecondCounter());
    std::uniform_real_distribution<float> U (0.0f, 1.0f);
    std::normal_distribution<float> N (0.0f, 1.0f);

    const int nProg = juce::jmin (60, insp.getNumPrograms());
    const int nFam = (int) families().size();
    const int famQuick = 55, famFull = 8, gens = 50, lambda = 14, mu = 6;
    const double totalCost = 1.0 + 0.35 * (1 + nProg + 18 + nFam * famQuick) + 2 * famFull + gens * lambda + (double) D.size() * 3.0 + 12 + (allowSample ? 80 : 0) + 20;
    auto tick = [&] (const juce::String& m) { return prog (0.03 + 0.92 * juce::jmin (1.0, cost / totalCost), m); };
    auto zhoda = [&] (const char* pre, float loss)
    { return juce::String::fromUTF8 (pre) + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - loss / 3.0f) * 100.0f)) + " %"; };

    // pomocne: dimenzie podla id, vstrekovanie odhadov, mutacia
    auto setX = [&] (std::vector<float>& x, const char* id, float v) { const int k = dimIndex (D, id); x[(size_t) k] = encode (D[(size_t) k], v); };
    auto getX = [&] (const std::vector<float>& x, const char* id) { const int k = dimIndex (D, id); return decode (D[(size_t) k], x[(size_t) k]); };
    auto injectEnv = [&] (std::vector<float>& x)
    {
        if (est.oct) setX (x, "a_oct", (float) juce::jlimit (-2, 1, est.octave));
        if (! est.env) return;
        const int w = (int) getX (x, "a_wave"), bw = (int) getX (x, "b_wave");
        setX (x, "a_aa", est.aa); setX (x, "a_ar", est.ar);
        if (w == 7) {}
        else if (w == 5) setX (x, "a_ad", est.adPluck);
        else { setX (x, "a_ad", w == 6 ? est.ad / 0.949f : est.ad); setX (x, "a_as", est.s); }
        setX (x, "b_ad", bw == 5 ? est.adPluck : (bw == 6 ? est.ad / 0.949f : est.ad));
        setX (x, "b_as", est.s);
    };
    auto injectTone = [&] (std::vector<float>& x)
    {
        if (est.cut) setX (x, "a_cut", est.cutHz);
        if (est.lfo) { setX (x, "a_lrate", est.lfoHz); setX (x, "a_ldepth", 0.08f); }
        else setX (x, "a_ldepth", 0.0f);
    };
    auto mutate = [&] (std::vector<float> x, float sigma, float prob)
    {
        bool any = false;
        for (size_t k = 0; k < x.size(); ++k)
            if ((int) k != aw && U (rng) < prob) { x[k] = juce::jlimit (0.0f, 1.0f, x[k] + sigma * N (rng)); any = true; }
        if (! any)
        {
            size_t k = (size_t) (U (rng) * (float) x.size()) % x.size();
            if ((int) k == aw) k = (k + 1) % x.size();
            x[k] = juce::jlimit (0.0f, 1.0f, x[k] + sigma * N (rng));
        }
        const int w = (int) decode (D[(size_t) aw], x[(size_t) aw]);
        if (w <= 1 && U (rng) < 0.08f) x[(size_t) aw] = encode (D[(size_t) aw], w == 0 ? 1.0f : 0.0f);
        return x;
    };
    auto famOfX = [&] (const std::vector<float>& x) { return familyOf ((int) decode (D[(size_t) aw], x[(size_t) aw])); };
    auto setFamily = [&] (std::vector<float>& x, int f)
    {
        const auto& ws = families()[(size_t) f].waves;
        x[(size_t) aw] = encode (D[(size_t) aw], (float) ws[(size_t) (U (rng) * (float) ws.size()) % ws.size()]);
    };

    // zaciatok: sucasny zvuk Inspire
    std::vector<float> x0;
    for (auto& d : D) x0.push_back (encode (d, patch.get (d.id, d.lo)));
    if (patch.get ("ch_on") < 0.5f) setX (x0, "ch_mix", 0);
    if (patch.get ("rv_on") < 0.5f) setX (x0, "rv_mix", 0);
    if (patch.get ("drive_on") < 0.5f) setX (x0, "drive", 0);
    std::vector<Cand> pop;
    {
        applyFixed (patch, D, x0, curGain);
        {
            juce::String ms; for (auto& i : patch.missing) ms += i + " ";
            dlog ("Inspire chybajuce id (" + juce::String ((int) patch.missing.size()) + "): " + (ms.isEmpty() ? juce::String ("ziadne") : ms.trim()));
            if (patch.missing.count ("p_gain") > 0 || patch.missing.count ("a_wave") > 0)
            {
                res.error = juce::String::fromUTF8 ("Inspiria má inú verziu – aktualizuj program."); dlog ("END: " + res.error); return res;
            }
        }
        const float l = evalPatch();
        if (fatalMsg.isNotEmpty()) return bail();
        pop.push_back ({ x0, l });
        res.startLoss = l;
        dlog ("startLoss: " + fmtTerms (lastT, l, lastPk));
    }

    // tovarenske zvuky Inspire ako startovacie body - rychle hodnotenie (nota 60)
    std::vector<Cand> seeds;
    {
        const juce::String msg = juce::String::fromUTF8 ("Porovnávam so zvukmi Inspirie…");
        for (int i = 0; i < nProg; ++i)
        {
            juce::MemoryBlock ps;
            // program sa nacita asynchronne (handleAsyncUpdate) - druhe volanie na hlavnom vlakne bezi az po nom
            if (! onMessageThread ([&] { insp.setCurrentProgram (i); }) || ! onMessageThread ([&] { insp.getStateInformation (ps); }))
            { fatalMsg = timeoutMsg; return bail(); }
            StateCodec pc;
            auto pt = pc.decode (ps);
            if (! pt.isValid()) continue;
            Patch pp (pt);
            std::vector<float> x;
            for (auto& d : D) x.push_back (encode (d, pp.get (d.id, d.lo)));
            if (pp.get ("ch_on") < 0.5f) setX (x, "ch_mix", 0);
            if (pp.get ("rv_on") < 0.5f) setX (x, "rv_mix", 0);
            if (pp.get ("drive_on") < 0.5f) setX (x, "drive", 0);
            if (famOfX (x) < 0) continue;
            seeds.push_back ({ x, evalXq (x) });
            dbgf ("SEEDQ %d %.3f\n", i, seeds.back().loss);
            if (! tick (msg)) return bail();
        }
        std::sort (seeds.begin(), seeds.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; });
        // varianty najlepsich seedov: obalka + oktava z odhadu; pre najlepsie aj orezanie a LFO
        const juce::String msg2 = juce::String::fromUTF8 ("Skúšam odhadnuté obálky…");
        const size_t nTop = juce::jmin ((size_t) 12, seeds.size());
        std::vector<Cand> var;
        for (size_t i = 0; i < nTop; ++i)
        {
            auto x = seeds[i].x; injectEnv (x);
            var.push_back ({ x, evalXq (x) });
            if (! tick (msg2)) return bail();
            if (i < 6)
            {
                auto x2 = x; injectTone (x2);
                var.push_back ({ x2, evalXq (x2) });
                if (! tick (msg2)) return bail();
            }
        }
        for (auto& v : var) seeds.push_back (v);
        std::sort (seeds.begin(), seeds.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; });
        if (! seeds.empty()) dbgf ("SEEDS best %.3f (fam %d)\n", seeds[0].loss, famOfX (seeds[0].x));
    }
    if (fatalMsg.isNotEmpty()) return bail();
    {
        // kontrola zachytenia zdroja: ak nic (ani zvuky Inspire) nie je blizko, zdroj sa zrejme nezachytil spravne
        float b0 = res.startLoss;
        for (auto& c : seeds) b0 = juce::jmin (b0, c.loss);
        dlog ("najlepsia strata po seedoch: " + juce::String (b0, 4) + " (start " + juce::String (res.startLoss, 4) + ", seedov " + juce::String ((int) seeds.size()) + ")");
        if (! (b0 < 2.9f))
        {
            res.error = juce::String::fromUTF8 ("Zvuk zdroja sa nepodarilo spoľahlivo zachytiť – pošli prosím súbory z priečinka Dokumenty\\Korgus Sequencer\\Ucenie");
            dlog ("END (sanity abort): " + res.error + "  counts: n99=" + juce::String (n99) + " nonFiniteDist=" + juce::String (gDistNonFinite.load()) + " nonFiniteRenders=" + juce::String (nNonFiniteRend)
                  + " timeouts=" + juce::String (gTimeouts.load()));
            return res;
        }
    }

    // 3) vyber typu zvuku: postupne preosievanie rodin (kazda ~55 rychlych hodnoteni), do pokracovania idu 2 najlepsie
    std::vector<std::vector<Cand>> famPool ((size_t) nFam);
    for (auto& s : seeds) famPool[(size_t) famOfX (s.x)].push_back (s);
    std::vector<float> famBest ((size_t) nFam, 1.0e9f);
    for (int f = 0; f < nFam; ++f)
    {
        auto& pl = famPool[(size_t) f];
        const juce::String msg = juce::String::fromUTF8 ("Skúšam typ zvuku: ") + juce::String::fromUTF8 (families()[(size_t) f].name) + juce::String::fromUTF8 ("…");
        auto bestOf = [&] { float b = 1.0e9f; for (auto& c : pl) b = juce::jmin (b, c.loss); return b; };
        for (int i = 0; i < famQuick; ++i)
        {
            std::vector<float> x;
            if (i < 12)
            {
                if (i % 3 == 2 && ! pl.empty())
                {
                    std::sort (pl.begin(), pl.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; });
                    x = mutate (pl[(size_t) (U (rng) * (float) juce::jmin ((size_t) 3, pl.size())) % pl.size()].x, 0.35f, 0.5f);
                }
                else
                {
                    for (size_t k = 0; k < D.size(); ++k) x.push_back (U (rng));
                    if (i % 2 == 0) { injectEnv (x); if (i % 4 == 0) injectTone (x); }
                }
                setFamily (x, f);
            }
            else
            {
                std::sort (pl.begin(), pl.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; });
                const float sigma = 0.25f * std::pow (0.25f, (float) (i - 12) / (float) (famQuick - 13));
                x = mutate (pl[(size_t) (U (rng) * (float) juce::jmin ((size_t) 3, pl.size())) % pl.size()].x, sigma, 0.3f);
            }
            pl.push_back ({ x, evalXq (x) });
            if (! tick (msg + juce::String::fromUTF8 (" · zhoda ") + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - bestOf() / 3.0f) * 100.0f)) + " %")) return bail();
        }
        std::sort (pl.begin(), pl.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; });
        famBest[(size_t) f] = pl.front().loss;
        dbgf ("FAM %-22s best quick %.3f\n", families()[(size_t) f].name, pl.front().loss);
        { const float lq = evalXq (pl.front().x); dlog (juce::String ("  rodina ") + juce::String::fromUTF8 (families()[(size_t) f].name) + " najlepsia (rychla): " + fmtTerms (lastT, lq, lastPk)); }
    }
    std::vector<int> famOrder;
    for (int f = 0; f < nFam; ++f) famOrder.push_back (f);
    std::sort (famOrder.begin(), famOrder.end(), [&] (int a, int b) { return famBest[(size_t) a] < famBest[(size_t) b]; });
    const int keep[2] = { famOrder[0], famOrder[1] };
    if (! tick (juce::String::fromUTF8 ("Vyberám najlepší typ zvuku…"))) return bail();
    for (int f : keep)
        for (int i = 0; i < famFull && i < (int) famPool[(size_t) f].size(); ++i)
        {
            auto x = famPool[(size_t) f][(size_t) i].x;
            pop.push_back ({ x, evalX (x) });
            if (! tick (juce::String::fromUTF8 ("Porovnávam typy zvuku… zhoda ") + juce::String (juce::roundToInt (juce::jlimit (0.0f, 1.0f, 1.0f - pop.back().loss / 3.0f) * 100.0f)) + " %")) return bail();
        }
    auto sortPop = [&] { std::sort (pop.begin(), pop.end(), [] (const Cand& a, const Cand& b) { return a.loss < b.loss; }); if (pop.size() > 30) pop.resize (30); };
    sortPop();
    dbgf ("KEEP %s + %s ; full best %.3f\n", families()[(size_t) keep[0]].name, families()[(size_t) keep[1]].name, pop[0].loss);

    // 4) evolucna strategia (3 tony) obmedzena na dve rodiny; po polovici sa slabsia rodina zahodi, ak zaostava
    bool dropped = false; int activeFam = -1;
    for (int g = 0; g < gens; ++g)
    {
        if (g == gens / 2)
        {
            float b0 = 1.0e9f, b1 = 1.0e9f;
            for (auto& c : pop) { const int f = famOfX (c.x); if (f == keep[0]) b0 = juce::jmin (b0, c.loss); else if (f == keep[1]) b1 = juce::jmin (b1, c.loss); }
            if (b0 < b1 * 0.97f) { dropped = true; activeFam = keep[0]; }
            else if (b1 < b0 * 0.97f) { dropped = true; activeFam = keep[1]; }
            dbgf ("HALF b0=%.3f b1=%.3f drop=%d\n", b0, b1, dropped);
        }
        const float sigma = 0.25f * std::pow (0.1f, (float) g / (float) (gens - 1));
        for (int c = 0; c < lambda; ++c)
        {
            const int wantFam = dropped ? activeFam : keep[c % 2];
            std::vector<size_t> idx;
            for (size_t i = 0; i < pop.size(); ++i) if (famOfX (pop[i].x) == wantFam) idx.push_back (i);
            if (idx.empty()) for (size_t i = 0; i < pop.size(); ++i) idx.push_back (i);
            const size_t pi = idx[juce::jmin (idx.size() - 1, (size_t) (std::pow (U (rng), 2.0f) * (float) juce::jmin ((size_t) mu, idx.size())))];
            auto x = mutate (pop[pi].x, sigma, 0.3f);
            pop.push_back ({ x, evalX (x) });
            if (! tick (zhoda ("Ladím zvuk… zhoda ", pop[0].loss < pop.back().loss ? pop[0].loss : pop.back().loss))) return bail();
        }
        sortPop();
    }
    // jemne doladenie: kazdy parameter zvlast trochu hore a dole
    {
        auto cur = pop[0];
        float step = 0.08f;
        for (int pass = 0; pass < 2; ++pass, step *= 0.5f)
            for (size_t k = 0; k < cur.x.size(); ++k)
            {
                if (D[k].kind == Choice) continue;
                for (float dir : { 1.0f, -1.0f })
                {
                    auto x = cur.x;
                    x[k] = juce::jlimit (0.0f, 1.0f, x[k] + dir * step);
                    if (x[k] == cur.x[k]) continue;
                    const float l = evalX (x);
                    if (l < cur.loss) { cur = { x, l }; break; }
                }
                if (! tick (zhoda ("Dolaďujem detaily… zhoda ", cur.loss))) return bail();
            }
        pop[0] = cur;
    }
    const auto best = pop[0];
    applyFixed (patch, D, best.x, curGain);
    float finalLoss = best.loss;
    if (dbg) { const float l = evalPatch(); const float l2 = evalX (best.x); fprintf (stderr, "CHECK best.loss %.4f re-eval %.4f evalX %.4f\n", best.loss, l, l2); }

    // fx_width: strana rastie linearne so sirkou => width = SM_zdroj / SM_kandidat (pomer side/mid v sustaine)
    auto sideMid = [] (const std::vector<Feat>& F, double& side, double& mid)
    {
        side = mid = 0;
        for (auto& f : F) if (f.hasSt) for (int a = 0; a < 4; ++a) { mid += f.midE[a]; side += f.midE[a] * std::pow (10.0, f.st[a] / 10.0); }
    };
    auto tryWidth = [&] (float refLoss) -> float
    {
        std::vector<Feat> C;
        const float l0 = juce::jmin (refLoss, evalPatch (&C));
        double ss, sm, cs, cm;
        sideMid (S, ss, sm); sideMid (C, cs, cm);
        if (sm <= 0 || cm <= 0 || cs <= 0) return l0;
        const double smS = std::sqrt (ss / sm), smC = std::sqrt (cs / cm);
        if (smC < 0.003) return l0;
        const float cw = patch.get ("fx_width", 1.0f);
        const float w = juce::jlimit (0.0f, 2.0f, (float) (cw * smS / smC));
        if (std::abs (w - cw) < 0.04f) return l0;
        patch.set ("fx_width", w);
        const float l1 = evalPatch();
        dbgf ("WIDTH %.2f -> %.2f loss %.3f -> %.3f\n", cw, w, l0, l1);
        if (l1 < l0) return l1;
        patch.set ("fx_width", cw);
        return l0;
    };
    if (! tick (juce::String::fromUTF8 ("Dolaďujem šírku…"))) return bail();
    finalLoss = tryWidth (finalLoss);
    res.synthLoss = finalLoss;
    if (fatalMsg.isNotEmpty()) return bail();
    {
        std::vector<Rend> au;
        const float lb = evalPatch (nullptr, &au);
        dlog ("najlepsi kandidat (iba synteza): " + fmtTerms (lastT, lb, lastPk) + "  (hladanie: best.loss=" + juce::String (best.loss, 4) + ")");
        if (au.size() > 1) writeWav ("learn-best-60.wav", toBuffer (au[1]), kSR);
    }

    auto restore = [&] (const juce::MemoryBlock& s) { StateCodec c2; patch = Patch (c2.decode (s)); patch.codec = &codec; };

    // 5) volitelne: sampel zdroja do vrstvy B (dva varianty: uder / zluckovany sustain), vyberie sa lepsi
    if (allowSample)
    {
        const juce::String msgS = juce::String::fromUTF8 ("Dopĺňam sampel do vrstvy B…");
        prog (0.92, msgS);
        const juce::AudioBuffer<float>& st = raw48;       // 48 kHz zaznam tonu 60 z uvodneho zachytenia (bez opakovaneho renderu)
        const int ns = st.getNumSamples();
        const int nsPk = juce::jmin (ns, 4 * 48000);
        const float smpGain = 0.9f / juce::jmax (0.05f, st.getMagnitude (0, nsPk));
        int on48 = 0;
        {
            float pk = st.getMagnitude (0, nsPk);
            const float thr = pk * 0.005f;
            while (on48 < ns - 1 && std::abs (st.getSample (0, on48)) <= thr && std::abs (st.getSample (1, on48)) <= thr) ++on48;
        }
        auto slice = [&] (int start, int len)
        {
            start = juce::jlimit (0, ns, start); len = juce::jmax (0, juce::jmin (len, ns - start));
            juce::AudioBuffer<float> b (2, juce::jmax (1, len)); b.clear();
            for (int c = 0; c < 2; ++c) if (len > 0) b.copyFrom (c, 0, st, c, start, len);
            return b;
        };

        const auto synthState = patch.binary();
        const float synthLoss = finalLoss;
        auto setB = [&] (const juce::MemoryBlock& flac, const juce::String& name, bool loop, float aa, float ar)
        {
            patch.tree.setProperty ("smpB", flac.toBase64Encoding(), nullptr);
            patch.tree.setProperty ("smpBName", srcName + name, nullptr);
            for (const auto& kv : std::initializer_list<std::pair<const char*, float>> {
                     { "b_on", 1 }, { "b_wave", 7 }, { "b_src", 1 }, { "b_root", 60 }, { "b_oct", 0 }, { "b_semi", 0 }, { "b_uni", 1 },
                     { "b_cut", 20000 }, { "b_reso", 0 }, { "b_fenv", 0 }, { "b_ad", 1 }, { "b_as", 1 }, { "b_color", 0.5f } })
                patch.set (kv.first, kv.second);
            patch.set ("b_loop", loop ? 1.0f : 0.0f);
            patch.set ("b_aa", aa); patch.set ("b_ar", ar);
        };
        // spolocne dolad'ovanie: urovne vrstiev (bez a_level = 0), potom a_cut a a_fenv
        auto refine = [&] (const juce::String& m, double winSec) -> float
        {
            // naturalna uroven B = rovnaka energia ako syntéza (uder: v prvych 250 ms)
            auto energy = [&] (float a, float b)
            {
                patch.set ("a_level", a); patch.set ("b_level", b);
                std::vector<Rend> au; evalPatch (nullptr, &au);
                double e = 0; const size_t nw = winSec > 0 ? (size_t) (winSec * kSR) : (size_t) -1;
                for (auto& r : au) for (size_t i = 0; i < r.M.size() && i < nw; ++i) e += (double) r.M[i] * r.M[i];
                return e;
            };
            const double eA = energy (0.8f, 0.0f), eB = energy (0.0f, 1.0f);
            const float bn = eB > 1.0e-12 && eA > 1.0e-12 ? juce::jlimit (0.005f, 1.0f, (float) std::sqrt (eA / eB)) : 0.3f;
            dbgf ("SAMPLE eA %.4g eB %.4g natural b_level %.3f smpGain %.2f\n", eA, eB, bn, smpGain);
            float bestH = 99, bestA = 0.8f, bestB = bn;
            for (float a : { 0.3f, 0.55f, 0.8f, 1.0f })
                for (float rel : { 0.2f, 0.45f, 1.0f, 1.5f })
                {
                    const float b = juce::jlimit (0.005f, 1.0f, bn * rel);
                    if (rel > 1.0f && bn * rel >= 1.0f && bn >= 1.0f) continue;     // uz sme na hornej hranici
                    patch.set ("a_level", a); patch.set ("b_level", b);
                    const float d = evalPatch();
                    dbgf ("  grid a %.2f b %.3f loss %.3f\n", a, b, d);
                    if (d < bestH) { bestH = d; bestA = a; bestB = b; }
                    if (! tick (m)) return -1.0f;
                }
            patch.set ("a_level", bestA); patch.set ("b_level", bestB);
            for (const char* id : { "a_cut", "a_fenv" })
            {
                const size_t k = (size_t) dimIndex (D, id);
                for (float dir : { 1.0f, -1.0f })
                {
                    bool moved = false;
                    for (int rep = 0; rep < 2; ++rep)
                    {
                        const float old = patch.get (id);
                        const float xn = juce::jlimit (0.0f, 1.0f, encode (D[k], old) + dir * 0.12f);
                        patch.set (id, decode (D[k], xn));
                        const float d = evalPatch();
                        if (! tick (m)) return -1.0f;
                        if (d < bestH) { bestH = d; moved = true; }
                        else { patch.set (id, old); break; }
                    }
                    if (moved) break;
                }
            }
            return bestH;
        };

        float lossT = 99, lossL = 99; juce::MemoryBlock stateT, stateL;
        // (a) iba uder: prvych 200 ms s 50 ms doznenim, B jednorazovo
        {
            const int len = 9600, fade = 2400;
            auto b = slice (on48 - 96, len);
            for (int k = 0; k < fade && k < b.getNumSamples(); ++k)
            {
                const int i = b.getNumSamples() - fade + k;
                if (i < 0) continue;
                b.applyGain (i, 1, 0.5f * (1.0f + std::cos (juce::MathConstants<float>::pi * (float) k / (float) fade)));
            }
            const auto flac = makeFlac (b, smpGain);
            if (flac.getSize() > 1000)
            {
                setB (flac, " C4 (úder)", false, 0.001f, 0.05f);
                lossT = refine (juce::String::fromUTF8 ("Dopĺňam sampel: úder…"), 0.25);
                if (lossT < 0) return bail();
                stateT = patch.binary();
                restore (synthState);
            }
        }
        // (b) zluckovany sustain 0.8-2.4 s, rovnomerny prekryv poslednych 200 ms do zaciatku
        {
            const int start = on48 + 38400;
            int Ns = juce::jmin (76800, ns - start);
            if (Ns >= 24000)
            {
                const int X = juce::jmin (9600, Ns / 4);
                auto seg = slice (start, Ns);
                const int L = Ns - X;
                juce::AudioBuffer<float> lp (2, L);
                for (int c = 0; c < 2; ++c)
                {
                    const float* s = seg.getReadPointer (c); float* o = lp.getWritePointer (c);
                    for (int i = 0; i < L; ++i) o[i] = s[i];
                    for (int i = 0; i < X; ++i)
                    {
                        const float th = juce::MathConstants<float>::halfPi * (float) i / (float) X;
                        o[i] = s[i] * std::sin (th) + s[L + i] * std::cos (th);
                    }
                }
                const auto flac = makeFlac (lp, smpGain);
                if (flac.getSize() > 1000)
                {
                    // B so zdrojovym dozvukom: vlastny dozvuk Inspire minimalny; B s odhadnutym nabehom a doznenim (vzorka je uz na urovni sustainu)
                    setB (flac, " C4 (slučka)", true, est.env ? est.aa : 0.05f, est.env ? est.ar : patch.get ("a_ar", 0.3f));
                    patch.set ("rv_mix", juce::jmin (patch.get ("rv_mix"), 0.04f)); patch.set ("rv_on", 1.0f);
                    lossL = refine (juce::String::fromUTF8 ("Dopĺňam sampel: pretrvávajúci tón…"), 0.0);
                    if (lossL < 0) return bail();
                    stateL = patch.binary();
                    restore (synthState);
                }
            }
        }
        dbgf ("SAMPLE synth %.3f  uder %.3f  slucka %.3f\n", synthLoss, lossT, lossL);
        const float bestS = juce::jmin (lossT, lossL);
        if (bestS < synthLoss * 0.95f)
        {
            restore (lossT <= lossL ? stateT : stateL);
            finalLoss = bestS; res.usedSample = true;
            dbgf ("SAMPLE chosen: %s\n", lossT <= lossL ? "uder" : "slucka");
            finalLoss = tryWidth (finalLoss);
        }
    }

    // 6) hlasitost ako zdroj (p_gain je pred-FX zisk): nastavit, a ak sa tym zhorsila zhoda o > 10 %, doladit drive/tube;
    //    ak ani to nepomoze, vratit p_gain na hodnotu z hladania a hlasitost dorovnat parametrom "volume" (za efektmi)
    auto normaliseLevel = [&] (const char* id, float lo, float hi)
    {
        for (int it = 0; it < 2; ++it)
        {
            std::vector<Feat> Cf;
            evalPatch (&Cf);
            double es = 0, ec = 0;     // energia v pasme 50 Hz - 12 kHz, ako vo vzdialenosti
            for (auto& f : S) es += f.energy;
            for (auto& f : Cf) ec += f.energy;
            if (std::isfinite (es) && std::isfinite (ec) && ec > 1e-12 && es > 1e-12)
                patch.set (id, juce::jlimit (lo, hi, patch.get (id) + (float) (10.0 * std::log10 (es / ec))));
        }
    };
    if (! tick (juce::String::fromUTF8 ("Dolaďujem hlasitosť…"))) return bail();
    {
        const auto state0 = patch.binary();
        const float loss0 = finalLoss;
        normaliseLevel ("p_gain", -30.0f, 6.0f);
        float l = evalPatch();
        dbgf ("GAIN p_gain=%.1f loss %.3f -> %.3f\n", patch.get ("p_gain"), loss0, l);
        if (l > loss0 * 1.10f)
        {
            for (const char* id : { "drive", "tube" })
            {
                const size_t k = (size_t) dimIndex (D, id);
                for (float dl : { 0.2f, -0.2f, 0.1f, -0.1f })
                {
                    const float old = patch.get (id);
                    const float xn = juce::jlimit (0.0f, 1.0f, encode (D[k], old) + dl);
                    if (std::abs (xn - encode (D[k], old)) < 1.0e-4f) continue;
                    const float nv = decode (D[k], xn);
                    patch.set (id, nv);
                    if (juce::String (id) == "drive") patch.set ("drive_on", nv > 0.05f ? 1.0f : 0.0f);
                    const float d = evalPatch();
                    if (! tick (juce::String::fromUTF8 ("Dolaďujem hlasitosť…"))) return bail();
                    if (d < l) l = d;
                    else { patch.set (id, old); if (juce::String (id) == "drive") patch.set ("drive_on", old > 0.05f ? 1.0f : 0.0f); }
                }
            }
            normaliseLevel ("p_gain", -30.0f, 6.0f);
            l = evalPatch();
            dbgf ("GAIN after drive/tube loss %.3f\n", l);
        }
        if (l > loss0 * 1.10f)
        {
            restore (state0);
            normaliseLevel ("volume", -24.0f, 6.0f);
            const float lv = evalPatch();
            dbgf ("GAIN fallback p_gain %.1f + volume %.1f loss %.3f\n", patch.get ("p_gain"), patch.get ("volume"), lv);
            l = lv;
        }
        finalLoss = l;
    }
    patch.tree.setProperty ("patchName", juce::String::fromUTF8 ("Naučený: ") + srcName, nullptr);
    patch.tree.setProperty ("patchKey", juce::String(), nullptr);
    if (fatalMsg.isNotEmpty()) return bail();
    res.finalLoss = finalLoss;
    res.state = patch.binary();
    {
        std::vector<Rend> au;
        const float lf = evalPatch (nullptr, &au);
        dlog ("konecny vysledok: " + fmtTerms (lastT, lf, lastPk) + "  (finalLoss=" + juce::String (finalLoss, 4) + ", synthLoss=" + juce::String (res.synthLoss, 4) + ", sampel=" + juce::String ((int) res.usedSample) + ")");
        if (au.size() > 1) writeWav ("learn-final-60.wav", toBuffer (au[1]), kSR);
        dlog ("pocty: vyhodnoteni=" + juce::String (res.evals) + " strata99=" + juce::String (n99) + " nekonecna_vzdialenost=" + juce::String (gDistNonFinite.load())
              + " renderov_s_nekonecnymi_vzorkami=" + juce::String (nNonFiniteRend) + " casove_limity_hlavneho_vlakna=" + juce::String (gTimeouts.load()));
    }
    res.ok = true;
    dbgf ("DONE evals=%d (quick %d) cost=%.0f/%.0f\n", res.evals, quickEvals, cost, totalCost);
    prog (1.0, juce::String::fromUTF8 ("Hotovo"));
    return res;
}

juce::ValueTree decodeInspireState (const juce::MemoryBlock& state)
{
    StateCodec c;
    return c.decode (state);
}
}
