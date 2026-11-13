// lokalny test Korgus Nova: vyrenderuje kazdy zvuk do WAV a zmeria ho
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_dsp/juce_dsp.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
using namespace juce;

static void setParam (NovaProcessor& p, const char* id, float v) { auto* prm = p.params.getParameter (id); prm->setValueNotifyingHost (prm->convertTo0to1 (v)); }

int main (int argc, char** argv)
{
    ScopedJuceInitialiser_GUI gui;
    const double sr = 48000; const int bs = 256;
    if (argc > 1 && String (argv[1]) == "gui")
    {
        // okno s editorom na rucny test rozhrania (ovladanie cez xdotool)
        auto proc = std::make_unique<NovaProcessor>();
        proc->prepareToPlay (sr, bs);
        proc->uiMode = argc > 3 ? String (argv[3]).getIntValue() : 1;
        auto* w = new DocumentWindow ("Nova test", Colours::black, DocumentWindow::closeButton);
        w->setUsingNativeTitleBar (false);
        w->setContentNonOwned (proc->createEditorIfNeeded(), true);
        w->setTopLeftPosition (0, 0);
        w->setVisible (true);
        // volitelne: hra akord na pozadi (kvoli spektru)
        struct Player : Thread { NovaProcessor& pr; Player (NovaProcessor& p) : Thread ("play"), pr (p) {}
            void run() override { AudioBuffer<float> b (2, 256); int n = 0;
                while (! threadShouldExit()) { MidiBuffer mb; if (n % 700 == 0) for (int k : { 48, 55, 60, 64 }) mb.addEvent (MidiMessage::noteOn (1, k, 0.8f), 0);
                    if (n % 700 == 500) for (int k : { 48, 55, 60, 64 }) mb.addEvent (MidiMessage::noteOff (1, k), 0);
                    b.clear(); pr.processBlock (b, mb); ++n; Thread::sleep (5); } } };
        std::unique_ptr<Player> pl;
        if (argc > 4 && String (argv[4]) == "play") { pl = std::make_unique<Player> (*proc); pl->startThread(); }
        Timer::callAfterDelay (argc > 2 ? String (argv[2]).getIntValue() * 1000 : 60000, [] { MessageManager::getInstance()->stopDispatchLoop(); });
        MessageManager::getInstance()->runDispatchLoop();
        if (pl) pl->stopThread (1000);
        printf ("zvuk: %s, rezim %d, mierka %.2f\n", proc->currentName.toRawUTF8(), proc->uiMode, proc->uiScale);
        w->clearContentComponent(); delete proc->getActiveEditor(); delete w;
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "demo")
    {
        // ukazkove video: niekolko zvukov z "Prekvap ma", ku kazdemu kratka hudobna fraza (WAV) a snimka okna (PNG)
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        File dir ("/tmp/demo"); dir.createDirectory();
        struct N { double b, len; int note; float vel; };
        const double beat = 0.6;
        auto arp = [] (std::initializer_list<int> a, std::initializer_list<int> f, double step, float v)
        { std::vector<N> r; double t = 0; for (int n : a) { r.push_back ({ t, step * 0.95, n, v }); t += step; } for (int n : f) { r.push_back ({ t, step * 0.95, n, v }); t += step; } return r; };
        const int types[6] = { 7, 0, 1, 3, 4, 5 };
        File names (dir.getChildFile ("names.txt")); names.deleteFile();
        for (int si = 0; si < 6; ++si)
        {
            const int ty = types[si];
            q.surprise (ty);
            std::vector<N> ev;
            if (ty == 7) { ev = arp ({ 57, 64, 69, 72, 76, 72, 69, 64 }, { 53, 60, 65, 69, 72, 69, 65, 60 }, 0.5, 0.7f); ev.push_back ({ 0, 4, 45, 0.6f }); ev.push_back ({ 4, 4.5, 41, 0.6f }); }
            else if (ty == 0) { for (int n : { 57, 60, 64, 69 }) ev.push_back ({ 0, 4.2, n, 0.7f }); for (int n : { 53, 57, 60, 65 }) ev.push_back ({ 4, 4.6, n, 0.7f }); ev.push_back ({ 0, 4.2, 45, 0.6f }); ev.push_back ({ 4, 4.6, 41, 0.6f }); }
            else if (ty == 1) { ev = arp ({ 69, 72, 76, 69, 72, 76, 69, 72 }, { 65, 69, 72, 65, 69, 72, 65, 69 }, 0.5, 0.75f); for (size_t k = 0; k < ev.size(); ++k) ev[k].vel = k % 3 == 0 ? 0.9f : 0.6f; }
            else if (ty == 3) { ev = arp ({ 33, 33, 45, 33, 31, 33, 36, 33 }, { 29, 29, 41, 29, 28, 29, 33, 36 }, 0.5, 0.8f); }
            else if (ty == 4) { double t = 0; for (auto [n, d] : std::initializer_list<std::pair<int, double>> { {76,1},{74,.5},{72,.5},{74,1},{76,1},{77,1.5},{76,.5},{72,2.2} }) { ev.push_back ({ t, d * 0.97, n, 0.75f }); t += d; } }
            else { double t = 0; for (auto [n, d] : std::initializer_list<std::pair<int, double>> { {76,.5},{72,.5},{69,.5},{76,1.5},{77,.5},{72,.5},{69,.5},{72,2.5} }) { ev.push_back ({ t, d * 0.9, n, 0.8f }); t += d; } }
            const double dur = si == 5 ? 9.0 : 5.4;
            const int total = (int) (dur * sr);
            AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos);
                MidiBuffer mb;
                for (auto& e : ev)
                {
                    const int on = (int) (e.b * beat * sr), off = (int) ((e.b + e.len) * beat * sr);
                    if (on >= pos && on < pos + n) mb.addEvent (MidiMessage::noteOn (1, e.note, e.vel), on - pos);
                    if (off >= pos && off < pos + n) mb.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
                }
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            // vsetko zastavit pred dalsim zvukom
            { MidiBuffer mb; mb.addEvent (MidiMessage::allNotesOff (1), 0); AudioBuffer<float> blk (2, bs); for (int k = 0; k < 400; ++k) { blk.clear(); q.processBlock (blk, mb); mb.clear(); } }
            const int fo = (int) (0.12 * sr); for (int c = 0; c < 2; ++c) out.applyGainRamp (c, total - fo, fo, 1.0f, 0.0f);
            { File f (dir.getChildFile ("scene" + String (si) + ".wav")); f.deleteFile();
              std::unique_ptr<OutputStream> os (new FileOutputStream (f));
              auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
              w->writeFromAudioSampleBuffer (out, 0, total); }
            q.uiMode = 1;
            { std::unique_ptr<AudioProcessorEditor> ed (q.createEditor()); ed->setVisible (true); Thread::sleep (200);
              auto img = ed->createComponentSnapshot (ed->getLocalBounds());
              File f (dir.getChildFile ("scene" + String (si) + ".png")); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os); }
            names.appendText (q.currentName + "|" + q.currentCategory + "\n", false, false, "\n");
            printf ("scena %d: %s (%s) peak %.2f\n", si, q.currentName.toRawUTF8(), q.currentCategory.toRawUTF8(), out.getMagnitude (0, total));
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "calib")
    {
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        const auto& fp = nova::factoryPatches();
        for (int i = 0; i < (int) fp.size(); ++i)
        {
            nova::PatchData pd; q.presets.load ("f:" + fp[(size_t) i].name, pd);
            const float m = NovaProcessor::measureLevel (pd);
            const bool sus = fp[(size_t) i].category == String::fromUTF8 ("Pady") || fp[(size_t) i].category == String::fromUTF8 ("Sláčiky") || fp[(size_t) i].category == String::fromUTF8 ("Leady") || fp[(size_t) i].category == String::fromUTF8 ("Basy");
            const float tgt = sus ? -19.5f : -16.0f;
            printf ("%d|%s|%.1f|%.1f|%.1f\n", i, fp[(size_t) i].name.toRawUTF8(), m, pd.values["p_gain"], jlimit (-30.0f, 6.0f, tgt - m));
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "level")
    {
        // hlasitost tovarenskych zvukov a nahodnych zvukov "Prekvap ma"
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 200 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        for (auto& f : nova::factoryPatches())
        {
            nova::PatchData pd; q.presets.load ("f:" + f.name, pd);
            const float m = NovaProcessor::measureLevel (pd);
            printf ("%-18s %6.1f dB (s p_gain %6.1f)\n", f.name.toRawUTF8(), m + pd.values["p_gain"], pd.values["p_gain"]);
        }
        for (int t = 0; t < 8; ++t)
        {
            q.surprise (t);
            auto a = q.currentPatch(); const float la = NovaProcessor::measureLevel (a) + a.values.at ("p_gain");
            q.mutate(); q.mutate();
            auto b = q.currentPatch(); const float lb = NovaProcessor::measureLevel (b) + b.values.at ("p_gain");
            float diff = 0; int nd = 0; for (auto& [k, v] : a.values) if (std::abs (b.values[k] - v) > 1e-4f) { ++nd; }
            printf ("typ %d: %-22s %-9s %.1f dB -> obmena %-30s %.1f dB, zmenenych %d\n", t, a.name.toRawUTF8(), a.category.toRawUTF8(), la, b.name.toRawUTF8(), lb, nd);
        }
        for (int i = 0; i < 0; ++i)
        {
            q.surprise();
            const auto pd = q.currentPatch();
            const auto t0 = Time::getMillisecondCounterHiRes();
            const float m = NovaProcessor::measureLevel (pd);
            printf ("%-24s %-10s gain %6.1f -> %6.1f dB  (%.0f ms)\n", q.currentName.toRawUTF8(), q.currentCategory.toRawUTF8(), pd.values.at ("p_gain"), m + pd.values.at ("p_gain"), Time::getMillisecondCounterHiRes() - t0);
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "eq")
    {
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        auto run = [&] (std::vector<std::pair<const char*, float>> set)
        {
            q.loadPreset (String::fromUTF8 ("f:Sláčiky"));
            setParam (q, "drive_on", 0); setParam (q, "ch_on", 0);
            for (auto& [k, v] : set) setParam (q, k, v);
            double lo = 0, hi = 0, tailHi = 0;
            for (int pos = 0; pos < (int) (3 * sr); pos += bs)
            {
                MidiBuffer mb; if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 57, 0.8f), 0);
                if (pos == (int) (1 * sr) / bs * bs) mb.addEvent (MidiMessage::noteOff (1, 57), 0);
                AudioBuffer<float> b (2, bs); b.clear(); q.processBlock (b, mb);
                for (int i = 1; i < bs; ++i)
                {
                    const float x = b.getSample (0, i), d = x - b.getSample (0, i - 1);
                    if (pos < sr) { lo += x * x; hi += d * d; } else if (pos > 1.3 * sr) tailHi += d * d;
                }
            }
            printf ("  energia %.3f  vysky %.5f  vysky v dozvuku %.6f\n", lo, hi, tailHi);
        };
        {
            q.loadPreset (String::fromUTF8 ("f:Teplý pad")); q.synth.analyzer.tap.store (2);
            for (int pos = 0; pos < (int) (1.5 * sr); pos += bs) { MidiBuffer mb; if (pos == 0) for (int k : { 48, 55, 60, 64 }) mb.addEvent (MidiMessage::noteOn (1, k, 0.8f), 0);
                AudioBuffer<float> b (2, bs); b.clear(); q.processBlock (b, mb); }
            auto& an = q.synth.analyzer; const unsigned w = an.w.load(); float pk = 0; for (int i = 0; i < 4096; ++i) pk = std::max (pk, std::abs (an.buf[(w - 4096u + (unsigned) i) & (nova::Analyzer::N - 1)]));
            std::vector<float> d (8192, 0.0f); for (int i = 0; i < 4096; ++i) d[(size_t) i] = an.buf[(w - 4096u + (unsigned) i) & (nova::Analyzer::N - 1)] * (0.5f - 0.5f * std::cos (6.2831853f * i / 4095.0f));
            dsp::FFT f (12); f.performFrequencyOnlyForwardTransform (d.data()); float mx = 0; int mi = 0; for (int i = 1; i < 2048; ++i) if (d[(size_t) i] > mx) { mx = d[(size_t) i]; mi = i; }
            printf ("analyzer: w=%u peak %.3f, fft max %.1f dB pri %.0f Hz\n", w, pk, Decibels::gainToDecibels (mx / 1024.0f), mi * sr / 4096.0);
        }
        for (int ty = 0; ty < 5; ++ty)
        {
            q.loadPreset (String::fromUTF8 ("f:Teplý pad")); setParam (q, "ch_on", 1); setParam (q, "ch_type", (float) ty); setParam (q, "ch_mix", 0.6f);
            setParam (q, "dl_on", 1); setParam (q, "dl_eqh", -12); setParam (q, "rv_pre", 120);
            double e = 0; float pk = 0; bool bad = false;
            for (int pos = 0; pos < (int) (2 * sr); pos += bs) { MidiBuffer mb; if (pos == 0) for (int k : { 48, 55, 60, 64 }) mb.addEvent (MidiMessage::noteOn (1, k, 0.8f), 0);
                AudioBuffer<float> b (2, bs); b.clear(); q.processBlock (b, mb); pk = std::max (pk, b.getMagnitude (0, bs));
                for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float x = b.getSample (c, i); if (! std::isfinite (x)) bad = true; e += x * x; } }
            printf ("modulacia typ %d: spicka %.2f energia %.1f %s\n", ty, pk, e, bad ? "NAN!" : "ok");
        }
        {
            q.midiForget ("a_cut"); q.midiLearn ("a_cut");
            auto sendCc = [&] (int cc, int v) { MidiBuffer mb; mb.addEvent (MidiMessage::controllerEvent (1, cc, v), 0); AudioBuffer<float> b (2, bs); b.clear(); q.processBlock (b, mb); q.applyCc(); };
            sendCc (21, 10); const int c1 = q.ccFor ("a_cut");
            sendCc (21, 127); const float v1 = q.params.getRawParameterValue ("a_cut")->load();
            sendCc (21, 0); const float v0 = q.params.getRawParameterValue ("a_cut")->load();
            printf ("MIDI learn: CC %d, cutoff pri 127 = %.0f Hz, pri 0 = %.0f Hz, ulozene: %s\n", c1, v1, v0, q.presets.setting ("midimap").toRawUTF8());
            q.midiForget ("a_cut"); printf ("po zruseni: CC %d, ulozene '%s'\n", q.ccFor ("a_cut"), q.presets.setting ("midimap").toRawUTF8());
        }
        {
            q.loadPreset ("f:Organ"); const auto n0 = q.currentName;
            setParam (q, "x_rnd", 1); q.flushPending(); const auto n1 = q.currentName;
            setParam (q, "x_mut", 1); q.flushPending(); const auto n2 = q.currentName;
            printf ("spustace: %s -> %s -> %s\n", n0.toRawUTF8(), n1.toRawUTF8(), n2.toRawUTF8());
            for (auto* prm : q.getParameters()) if (prm->getName (64).contains ("Prekvap") || prm->getName (64).contains ("Obmena")) printf ("  parameter: %s\n", prm->getName (64).toRawUTF8());
        }
        printf ("bez EQ:\n"); run ({});
        printf ("A vysky +12:\n"); run ({ { "a_eqh", 12 } });
        printf ("B vysky +12 (B je vypnuta, nema sa nic zmenit):\n"); run ({ { "b_eqh", 12 } });
        printf ("dozvuk vysky -15 (zmena len v dozvuku):\n"); run ({ { "rv_eqh", -15 }, { "rv_eql", -15 }, { "rv_eqm", -15 }, { "rv_mix", 0.5f }, { "a_ar", 0.02f } });
        printf ("dozvuk bez EQ s mix 0.5:\n"); run ({ { "rv_mix", 0.5f }, { "a_ar", 0.02f } });
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "pitch")
    {
        // stabilita ladenia: drzany ton A3 (220 Hz) 10 s, efekty vypnute, vyska po 0.5 s
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 200 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        const int np = (int) nova::factoryPatches().size();
        for (int pi = 0; pi < np; ++pi)
        {
            q.loadPreset ("f:" + nova::factoryPatches()[(size_t) pi].name);
            if (argc > 2) setParam (q, "tube", String (argv[2]).getFloatValue());
            for (auto id : { "ch_on", "dl_on", "rv_on", "arp_on", "b_on" }) setParam (q, id, 0);
            setParam (q, "a_ldepth", 0);
            const int total = (int) (10.0 * sr);
            AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos);
                MidiBuffer mb; if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 57, 0.8f), 0);
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            dsp::FFT fft (15); const int N = 1 << 15;
            printf ("%-18s", nova::factoryPatches()[(size_t) pi].name.toRawUTF8());
            float mn = 1e9f, mx = 0;
            for (double t = 1.0; t + (double) N / sr <= 10.0; t += 0.5)
            {
                std::vector<float> d ((size_t) N * 2, 0.0f);
                const int s0 = (int) (t * sr);
                for (int i = 0; i < N; ++i) d[(size_t) i] = out.getSample (0, s0 + i) * (0.5f - 0.5f * std::cos (6.2831853f * (float) i / (float) N));
                fft.performFrequencyOnlyForwardTransform (d.data());
                const int lo = (int) (180.0 * N / sr), hi = (int) (260.0 * N / sr);
                int bi = lo; for (int i = lo; i <= hi; ++i) if (d[(size_t) i] > d[(size_t) bi]) bi = i;
                const float a = d[(size_t) bi - 1], b = d[(size_t) bi], c = d[(size_t) bi + 1];
                const float hz = ((float) bi + 0.5f * (a - c) / (a - 2 * b + c + 1e-12f)) * (float) sr / (float) N;
                if (b < 1e-3f) continue;
                mn = std::min (mn, hz); mx = std::max (mx, hz);
            }
            printf (" %.2f..%.2f Hz  (%.1f centov)\n", mn, mx, mx > 0 ? 1200.0 * std::log2 (mx / mn) : 0.0);
        }
        return 0;
    }
    NovaProcessor p;
    p.setPlayConfigDetails (0, 2, sr, bs);
    p.prepareToPlay (sr, bs);
    for (int i = 0; i < 200 && ! p.synth.pianoReady() && ! p.synth.pianoFailed(); ++i) Thread::sleep (50);
    printf ("piano ready %d failed %d\n", p.synth.pianoReady(), p.synth.pianoFailed());
    File dir ("/tmp/nova"); dir.createDirectory();
    const int np = (int) nova::factoryPatches().size();
    for (int pi = 0; pi < np; ++pi)
    {
        p.loadPreset ("f:" + nova::factoryPatches()[(size_t) pi].name);
        // sekvencia: tichy ton, silny ton, akord, vysoky a nizky ton
        struct Ev { double t; int note; float vel; double len; };
        std::vector<Ev> evs = { {0.1, 60, 0.3f, 1.2}, {1.6, 60, 0.95f, 1.2}, {3.1, 48, 0.7f, 2.0}, {3.1, 64, 0.7f, 2.0}, {3.1, 67, 0.7f, 2.0}, {3.1, 72, 0.7f, 2.0},
                                {6.0, 36, 0.8f, 1.5}, {7.8, 84, 0.8f, 1.5} };
        const double dur = 10.5; const int total = (int) (dur * sr);
        AudioBuffer<float> out (2, total); out.clear();
        double cpu = 0;
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos);
            MidiBuffer mb;
            for (auto& e : evs)
            {
                const int on = (int) (e.t * sr), off = (int) ((e.t + e.len) * sr);
                if (on >= pos && on < pos + n) mb.addEvent (MidiMessage::noteOn (1, e.note, e.vel), on - pos);
                if (off >= pos && off < pos + n) mb.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
            }
            AudioBuffer<float> blk (2, n); blk.clear();
            const auto t0 = Time::getHighResolutionTicks();
            p.processBlock (blk, mb);
            cpu += Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        // merania
        float peak = 0; double e = 0; bool nan = false; float maxJump = 0; int jumpAt = 0;
        for (int i = 0; i < total; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float v = out.getSample (c, i);
                if (! std::isfinite (v)) nan = true;
                peak = std::max (peak, std::abs (v)); e += v * v;
                if (i > 0) { const float j = std::abs (v - out.getSample (c, i - 1)); if (j > maxJump) { maxJump = j; jumpAt = i; } }
            }
        // ticho na konci (dozvuk by mal doznieť)
        float tail = 0; for (int i = total - (int) (0.2 * sr); i < total; ++i) tail = std::max (tail, std::abs (out.getSample (0, i)));
        // tazisko spektra akordu (3.2 - 4.2 s)
        dsp::FFT fft (13); std::vector<float> f (16384, 0); const int st = (int) (3.3 * sr);
        for (int i = 0; i < 8192; ++i) f[(size_t) i] = out.getSample (0, st + i) * (0.5f - 0.5f * std::cos (6.2831853f * i / 8192));
        fft.performFrequencyOnlyForwardTransform (f.data());
        double num = 0, den = 0; for (int b = 1; b < 4096; ++b) { const double hz = b * sr / 8192; num += hz * f[(size_t) b]; den += f[(size_t) b]; }
        printf ("%-18s peak %.2f rms %.3f tail %.4f maxJump %.3f@%.2fs centroid %5.0f Hz cpu x%.0f realtime %s\n",
                nova::factoryPatches()[(size_t) pi].name.toRawUTF8(), peak, std::sqrt (e / (2.0 * total)), tail, maxJump, jumpAt / sr, num / den, dur / cpu, nan ? "NAN!" : "");
        File wf = dir.getChildFile ("preset" + String (pi) + ".wav"); wf.deleteFile();
        WavAudioFormat w; std::unique_ptr<OutputStream> os (wf.createOutputStream().release());
        auto wr = w.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        wr->writeFromAudioSampleBuffer (out, 0, total);
    }

    {   // --- funkcne testy rozsirenej verzie
        auto render = [&] (std::function<void (int, MidiBuffer&)> ev, double secs)
        {
            const int total = (int) (secs * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs) { const int n = std::min (bs, total - pos); MidiBuffer mb; ev (pos, mb);
                AudioBuffer<float> blk (2, n); blk.clear(); p.processBlock (blk, mb); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n); }
            return out;
        };
        auto set = [&] (const char* id, float v) { setParam (p, id, v); };
        // arpeggiator: drzime akord C-E-G, 1/16 pri 120 bpm -> 8 krokov za sekundu
        p.loadPreset ("f:" + nova::factoryPatches()[7].name);
        set ("arp_on", 1); set ("arp_pat", 0); set ("arp_speed", 3); set ("arp_oct", 1);
        auto a = render ([&] (int pos, MidiBuffer& mb) { if (pos == 0) { mb.addEvent (MidiMessage::noteOn (1, 60, 0.8f), 0); mb.addEvent (MidiMessage::noteOn (1, 64, 0.8f), 0); mb.addEvent (MidiMessage::noteOn (1, 67, 0.8f), 0); }
                                                         if (pos == (int) (2.0 * sr) / bs * bs) { mb.addEvent (MidiMessage::noteOff (1, 60), 0); mb.addEvent (MidiMessage::noteOff (1, 64), 0); mb.addEvent (MidiMessage::noteOff (1, 67), 0); } }, 2.5);
        int onsets = 0; float prevE = 0;
        for (int i = 0; i + 240 < (int) (2.0 * sr); i += 240) { float e = 0; for (int k = 0; k < 240; ++k) e = std::max (e, std::abs (a.getSample (0, i + k))); if (e > 0.05f && e > prevE * 2.5f) ++onsets; prevE = e; }
        printf ("arp: %d nastupov za 2 s (cakame ~16)\n", onsets);
        set ("arp_on", 0);
        // mono + glide: dve noty legato, vyska by mala plynulo prejst
        p.loadPreset ("f:" + nova::factoryPatches()[11].name);
        set ("glide", 0.2f);
        auto m = render ([&] (int pos, MidiBuffer& mb) { if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 48, 0.8f), 0); if (pos == (int) (0.5 * sr) / bs * bs) mb.addEvent (MidiMessage::noteOn (1, 60, 0.8f), 0); }, 1.5);
        auto zc = [&] (int st, int len) { int z = 0; for (int i = st + 1; i < st + len; ++i) if ((m.getSample (0, i) >= 0) != (m.getSample (0, i - 1) >= 0)) ++z; return z * 0.5 * sr / len; };
        printf ("mono glide: f pred %.0f Hz, pocas %.0f Hz, po %.0f Hz (cakame 131 -> medzi -> 262)\n", zc ((int) (0.3 * sr), 4800), zc ((int) (0.55 * sr), 1200), zc ((int) (1.2 * sr), 4800));
        // vlastny WAV: sinus 440 Hz ako sampel vo vrstve A, zaklad A4 -> hrame A5 = 880 Hz
        File wav ("/tmp/nova/test440.wav"); wav.deleteFile();
        { AudioBuffer<float> b (1, (int) sr); for (int i = 0; i < (int) sr; ++i) b.setSample (0, i, 0.5f * std::sin (6.2831853 * 440 * i / sr));
          WavAudioFormat w; std::unique_ptr<OutputStream> os (wav.createOutputStream().release()); auto wr = w.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (1).withBitsPerSample (24)); wr->writeFromAudioSampleBuffer (b, 0, (int) sr); }
        p.loadPreset ("f:" + nova::factoryPatches()[11].name);
        set ("mono", 0); set ("b_on", 0);
        String err; const bool okW = p.loadSampleFile (0, wav, err);
        set ("a_root", 69);
        auto u = render ([&] (int pos, MidiBuffer& mb) { if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 81, 0.9f), 0); }, 0.6);
        int z = 0; for (int i = (int) (0.1 * sr) + 1; i < (int) (0.5 * sr); ++i) if ((u.getSample (0, i) >= 0) != (u.getSample (0, i - 1) >= 0)) ++z;
        printf ("vlastny WAV: nacitany %d (%s), frekvencia %.0f Hz (cakame 880)\n", okW, p.sampleName (0).toRawUTF8(), z * 0.5 / 0.4);
        // ulozenie a nacitanie vlastneho zvuku
        auto key = p.savePreset ("Test zvuk", "Moje zvuky");
        p.loadPreset ("f:" + nova::factoryPatches()[0].name);
        const bool okL = p.loadPreset (key);
        printf ("ulozenie zvuku: kluc %s, nacitanie %d, meno %s, sampel %s, a_root %.0f\n", key.toRawUTF8(), okL, p.currentName.toRawUTF8(), p.sampleName (0).toRawUTF8(), p.params.getRawParameterValue ("a_root")->load());
        // stav projektu
        MemoryBlock st; p.getStateInformation (st);
        NovaProcessor q; q.setStateInformation (st.getData(), (int) st.getSize());
        printf ("stav projektu: %d B, meno %s, sampel %s, a_wave %.0f\n", (int) st.getSize(), q.currentName.toRawUTF8(), q.sampleName (0).toRawUTF8(), q.params.getRawParameterValue ("a_wave")->load());
        // projekt z prvej verzie (iba makra) -> nacita tovarensky zvuk 4
        String v1 = "<NOVA><PARAM id=\"preset\" value=\"4\"/><PARAM id=\"bright\" value=\"0.7\"/><PARAM id=\"volume\" value=\"-3\"/></NOVA>";
        auto x = parseXML (v1); MemoryBlock vb; AudioProcessor::copyXmlToBinary (*x, vb);
        NovaProcessor r; r.setStateInformation (vb.getData(), (int) vb.getSize());
        printf ("projekt v1: zvuk %s, jas %.2f, hlasitost %.1f, a_wave %.0f (cakame Sklenený pad, 0.70, -3.0, 3)\n", r.currentName.toRawUTF8(), r.params.getRawParameterValue ("bright")->load(), r.params.getRawParameterValue ("volume")->load(), r.params.getRawParameterValue ("a_wave")->load());
        // undo
        p.undo.beginNewTransaction(); set ("a_cut", 500); p.undo.beginNewTransaction();
        const float before = p.params.getRawParameterValue ("a_cut")->load(); p.undo.undo();
        printf ("undo: cutoff %.0f -> %.0f\n", before, p.params.getRawParameterValue ("a_cut")->load());
        p.presets.remove (key);
    }
    if (argc > 1)
    {
        p.loadPreset ("f:" + nova::factoryPatches()[3].name);
        p.uiMode = 1;
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        Thread::sleep (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        { File f ("/tmp/nova/editor.png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os); }
        p.uiMode = 0;
        std::unique_ptr<AudioProcessorEditor> ed2 (p.createEditor()); ed2->setVisible (true); Thread::sleep (100);
        auto img2 = ed2->createComponentSnapshot (ed2->getLocalBounds());
        { File f ("/tmp/nova/editor_simple.png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img2, os); }
        printf ("editor %dx%d\n", ed->getWidth(), ed->getHeight());
    }
    return 0;
}
