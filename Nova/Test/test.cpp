#include <thread>
#include "../../Source/KorgusEqView.h"
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
            { std::unique_ptr<AudioProcessorEditor> ed (q.createEditor()); Thread::sleep (200);
              auto img = ed->createComponentSnapshot (ed->getLocalBounds());
              File f (dir.getChildFile ("scene" + String (si) + ".png")); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os); }
            names.appendText (q.currentName + "|" + q.currentCategory + "\n", false, false, "\n");
            printf ("scena %d: %s (%s) peak %.2f\n", si, q.currentName.toRawUTF8(), q.currentCategory.toRawUTF8(), out.getMagnitude (0, total));
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "eqview")
    {   // test spolocneho EQ: klik na bod, tahanie uchytky sirky, pravym tlacidlom sirka, Shift jemne
        ScopedJuceInitialiser_GUI gui;
        struct M : korgus::EqModel
        {
            korgus::EqBand b; M() { b.type = 1; b.f = 1000; b.g = 6; b.q = 1; b.fMin = 40; b.fMax = 16000; }
            int numBands() const override { return 1; } korgus::EqBand band (int) const override { return b; }
            void setF (int, float v) override { b.f = v; } void setG (int, float v) override { b.g = v; } void setQ (int, float v) override { b.q = v; }
            void begin (int) override {} void end (int) override {}
            double magnitude (float) const override { return 1; } double bandMagnitude (int, float) const override { return 1; }
        } m;
        korgus::EqView v (m); v.setBounds (0, 0, 600, 200);
        auto src = Desktop::getInstance().getMainMouseSource();
        auto ev = [&] (float x, float y, ModifierKeys mk) { return MouseEvent (src, { x, y }, mk, 0, 0, 0, 0, 0, &v, &v, Time::getCurrentTime(), { x, y }, Time::getCurrentTime(), 1, false); };
        // poloha bodu: x pre 1 kHz, y pre +6 dB
        const float ax = 26, aw = 600 - 26 - 6, ay = 6, ah = 200 - 6 - 16;
        const float px = ax + aw * std::log (1000.0f / 20.0f) / std::log (1000.0f), py = ay + ah * (18.0f - 6.0f) / 36.0f;
        v.mouseDown (ev (px, py, ModifierKeys::leftButtonModifier)); v.mouseUp (ev (px, py, {}));
        printf ("vybrane %d\n", v.selected());
        // uchytka vpravo: f * 2^(bw/2), bw pre Q 1 = 1.39 okt
        const float bw = 2.0f * std::asinh (0.5f) / std::log (2.0f), hx = ax + aw * std::log (1000.0f * std::pow (2.0f, bw / 2) / 20.0f) / std::log (1000.0f), hy = ay + ah * (18.0f - 3.0f) / 36.0f;
        v.mouseDown (ev (hx, hy, ModifierKeys::leftButtonModifier)); v.mouseDrag (ev (hx + 40, hy, ModifierKeys::leftButtonModifier)); v.mouseUp (ev (hx + 40, hy, {}));
        printf ("po tahani uchytky von: Q %.2f (sirsie = mensie Q)\n", m.b.q);
        const float q1 = m.b.q;
        v.mouseDown (ev (px, py, ModifierKeys::rightButtonModifier)); v.mouseDrag (ev (px, py - 40, ModifierKeys::rightButtonModifier)); v.mouseUp (ev (px, py - 40, {}));
        printf ("pravym tlacidlom hore: Q %.2f -> %.2f (uzsie)\n", q1, m.b.q);
        v.mouseDown (ev (px, py, ModifierKeys::leftButtonModifier)); v.mouseDrag (ev (px + 100, py - 30, ModifierKeys (ModifierKeys::leftButtonModifier | ModifierKeys::shiftModifier))); v.mouseUp (ev (px + 100, py - 30, {}));
        printf ("Shift jemne: f %.0f Hz, g %.1f dB\n", m.b.f, m.b.g);
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "eqasst")
    {   // asistent EQ: zvuk s nadbytkom 250-400 Hz (zahuhnany), chybajucimi vyskami a rezonanciou 2 kHz
        ScopedJuceInitialiser_GUI gui;
        struct M : korgus::EqModel
        {
            korgus::EqBand b[8];
            M() { const int t[8] = { 3, 0, 1, 1, 1, 1, 2, 4 }; const float f[8] = { 20, 120, 1000, 300, 3000, 6000, 8000, 20000 };
                  for (int i = 0; i < 8; ++i) { b[i].type = t[i]; b[i].f = f[i]; b[i].g = 0; b[i].q = 1; b[i].hasG = t[i] < 3; b[i].hasQ = t[i] == 1; b[i].fMin = 20; b[i].fMax = 20000; b[i].active = false; } }
            int numBands() const override { return 8; } korgus::EqBand band (int i) const override { return b[i]; }
            void setF (int i, float v) override { b[i].f = v; } void setG (int i, float v) override { b[i].g = v; } void setQ (int i, float v) override { b[i].q = v; }
            void begin (int) override {} void end (int) override {}
            double magnitude (float) const override { return 1; } double bandMagnitude (int, float) const override { return 1; }
            float spectrumDb (float hz) const override
            {
                const float o = std::log2 (hz / 1000.0f); float d = -30.0f - 4.5f * o;
                d += 6.0f * std::exp (-std::pow (std::log2 (hz / 320.0f) / 0.6f, 2.0f));   // zahuhnane
                if (hz > 5000) d -= 4.0f * std::log2 (hz / 5000.0f);                        // chybaju vysky
                d += 7.0f * std::exp (-std::pow (std::log2 (hz / 2000.0f) / 0.08f, 2.0f));  // rezonancia
                if (hz < 60) d -= 12.0f * std::log2 (60.0f / hz);
                return d;
            }
        } m;
        korgus::EqView v (m); v.setBounds (0, 0, 600, 200);
        v.startListening (0); for (int i = 0; i < 400; ++i) v.stepForTest();
        for (int i = 0; i < 8; ++i) printf ("pasmo %d: f %.0f Hz  g %+.1f dB  Q %.2f\n", i + 1, m.b[i].f, m.b[i].g, m.b[i].q);
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "race")
    {   // audio vlakno bezi suhlasne, hlavne vlakno prepina vsetky presety ako klik v UI
        ScopedJuceInitialiser_GUI gui;
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (20);
        q.uiMode = 0;
        std::unique_ptr<AudioProcessorEditor> ed (q.createEditor());
        std::atomic<bool> run { true };
        std::thread au ([&] {
            int k = 0;
            while (run) {
                MidiBuffer mb; AudioBuffer<float> blk (2, bs); blk.clear();
                if (k % 40 == 0) mb.addEvent (MidiMessage::noteOn (1, 48 + (k / 40) % 24, (uint8) 100), 0);
                if (k % 40 == 30) mb.addEvent (MidiMessage::allNotesOff (1), 0);
                q.processBlock (blk, mb); ++k; Thread::sleep (1);
            }
        });
        std::vector<String> keys; for (auto& e : q.presets.entries()) keys.push_back (e.key);
        printf ("presetov: %d\n", (int) keys.size()); fflush (stdout);
        for (int rep = 0; rep < 2; ++rep)
            for (auto& k : keys) { printf ("%s\n", k.toRawUTF8()); fflush (stdout); q.loadPreset (k); q.preview(); MessageManager::getInstance()->runDispatchLoopUntil (40); }
        run = false; au.join();
        printf ("race ok\n"); return 0;
    }
    if (argc > 1 && String (argv[1]) == "clickall")
    {   // NovaTest clickall: jednoducha verzia, klikanie na vsetky zvuky (Nastroje aj Syntezator) a hranie medzi tym
        ScopedJuceInitialiser_GUI gui;
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (20);
        q.uiMode = 0;
        std::unique_ptr<AudioProcessorEditor> ed (q.createEditor());
        auto play = [&] (int ms)
        {
            const int blocks = (int) (ms * 0.001 * sr / bs);
            for (int k = 0; k < blocks; ++k)
            {
                MidiBuffer mb; if (k == 0) for (int n : { 48, 60, 64, 67 }) mb.addEvent (MidiMessage::noteOn (1, n, (uint8) 100), 0);
                if (k == blocks - 1) for (int n : { 48, 60, 64, 67 }) mb.addEvent (MidiMessage::noteOff (1, n), 0);
                AudioBuffer<float> blk (2, bs); blk.clear(); q.processBlock (blk, mb);
            }
        };
        std::vector<Button*> all;
        std::function<void (Component*)> collect = [&] (Component* c) { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<Button*> (ch)) all.push_back (b); collect (ch); } };
        int clicked = 0;
        for (const char* section : { "Instruments", "Nástroje", "Synthesizer", "Syntezátor" })
        {
            all.clear(); collect (ed.get());
            for (auto* b : all) if (b->getButtonText() == String::fromUTF8 (section) && b->onClick) b->onClick();
            for (int pass = 0; pass < 2; ++pass)
            {
                all.clear(); collect (ed.get());
                std::vector<Component::SafePointer<Button>> list; for (auto* b : all) if (b->isVisible() && b->getWidth() > 0) list.push_back (b);
                for (auto& sp : list)
                {
                    if (sp == nullptr) continue;
                    const auto t = sp->getButtonText();
                    if (t.isEmpty() || t == "size" || t == "FX" || t.containsIgnoreCase ("Rozšír") || t.containsIgnoreCase ("Advanced") || t.containsIgnoreCase ("Nauč") || t.containsIgnoreCase ("Learn")) continue;
                    if (sp->onClick) { printf ("klik %s\n", t.toRawUTF8()); fflush (stdout); sp->onClick(); ++clicked; }
                    for (int k = 0; k < 3; ++k) { MessageManager::getInstance()->runDispatchLoopUntil (5); }
                    play (150);
                }
            }
        }
        printf ("kliknute: %d, bez pada\n", clicked);
        return 0;
    }
    if (argc > 2 && String (argv[1]) == "eqshot")
    {   // NovaTest eqshot <vystup.png> [zvuk]: rozsirena verzia, zalozka EQ, zvuk "Zvuk"
        ScopedJuceInitialiser_GUI gui;
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        if (argc > 3) q.loadPreset ("f:" + String::fromUTF8 (argv[3]));
        q.uiMode = 1;
        std::unique_ptr<AudioProcessorEditor> ed (q.createEditor());

        std::function<void (Component*, const String&)> click = [&] (Component* c, const String& t)
        { for (auto* ch : c->getChildren()) { if (auto* b = dynamic_cast<Button*> (ch)) if (b->getButtonText() == t && b->onClick) { b->onClick(); } click (ch, t); } };
        click (ed.get(), "EQ"); Thread::sleep (300);
        { const String tg = std::getenv ("EQTARGET") != nullptr ? String::fromUTF8 (std::getenv ("EQTARGET")) : String ("Zvuk"); click (ed.get(), tg); click (ed.get(), "Sound"); }
        Thread::sleep (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        File f (argv[2]); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        return 0;
    }
    if (argc > 3 && String (argv[1]) == "knova")
    {   // NovaTest knova <subor.knova> <vystup.wav> [nota]: nacita zvuk a zahra notu 2 s
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        const auto k = q.presets.importFile (File (argv[2]));
        if (k.isNotEmpty()) q.loadPreset (k);
        const int note = argc > 4 ? String (argv[4]).getIntValue() : 60;
        const int total = (int) (2.5 * sr); AudioBuffer<float> out (2, total); out.clear();
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos); MidiBuffer mb;
            static const int ch[5] = { 36, 43, 52, 55, 60 };   // nota 0 = akord C dur (C2 G2 E3 G3 C4) ako v nahravke z Dune
            if (pos == 0) { if (note == 0) for (int c : ch) mb.addEvent (MidiMessage::noteOn (1, c, (uint8) 100), 0); else mb.addEvent (MidiMessage::noteOn (1, note, (uint8) 100), 0); }
            const int off = (int) (2.0 * sr); if (pos <= off && pos + n > off) { if (note == 0) for (int c : ch) mb.addEvent (MidiMessage::noteOff (1, c), off - pos); else mb.addEvent (MidiMessage::noteOff (1, note), off - pos); }
            AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        printf ("peak %.3f\n", out.getMagnitude (0, total));
        File f (argv[3]); f.deleteFile(); std::unique_ptr<OutputStream> os (new FileOutputStream (f));
        auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        w->writeFromAudioSampleBuffer (out, 0, total);
        return 0;
    }
    if (argc > 3 && String (argv[1]) == "render")
    {   // NovaTest render "<nazov tovarenskeho zvuku>" <plnost 0..1> <vystup.wav>: akord C dur 2.5 s
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        if (! q.loadPreset ("f:" + String::fromUTF8 (argv[2]))) { printf ("zvuk nenajdeny\n"); return 1; }
        if (auto* prm = q.params.getParameter ("full")) prm->setValueNotifyingHost (String (argv[3]).getFloatValue());
        { MidiBuffer mb; AudioBuffer<float> blk (2, bs); for (int k = 0; k < 40; ++k) { blk.clear(); q.processBlock (blk, mb); Thread::sleep (100); } }   // nastroj sa nacitava na pozadi
        const int total = (int) (3.5 * sr); AudioBuffer<float> out (2, total); out.clear();
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos); MidiBuffer mb;
            if (pos == 0) for (int nt : { 48, 60, 64, 67 }) mb.addEvent (MidiMessage::noteOn (1, nt, (uint8) 100), 0);
            const int off = (int) (2.5 * sr); if (pos <= off && pos + n > off) for (int nt : { 48, 60, 64, 67 }) mb.addEvent (MidiMessage::noteOff (1, nt), off - pos);
            AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
            for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
        }
        double m2 = 0, s2 = 0; for (int i = 0; i < total; ++i) { const float l = out.getSample (0, i), r = out.getSample (1, i); m2 += (l + r) * (l + r) * 0.25; s2 += (l - r) * (l - r) * 0.25; }
        printf ("%s full=%s peak %.3f rms %.3f sirka(side/mid) %.2f\n", argv[2], argv[3], out.getMagnitude (0, total), std::sqrt (m2 / total), std::sqrt (s2 / std::max (1e-12, m2)));
        if (argc > 4) { File f (argv[4]); f.deleteFile(); std::unique_ptr<OutputStream> os (new FileOutputStream (f));
            auto w = WavAudioFormat().createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24)); w->writeFromAudioSampleBuffer (out, 0, total); }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "inspire")
    {
        // Inspiracia: vytiahnutie poslednej frazy, MIDI, WAV, stav pre hostitela
        int fails = 0;
        auto check = [&fails] (bool ok, const char* what) { printf ("%s  %s\n", ok ? "OK  " : "CHYBA", what); if (! ok) ++fails; };
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        long long clock = 0;
        auto run = [&] (double seconds, std::vector<std::tuple<double, int, int, int>> evs)   // (cas od zaciatku useku, 0 off/1 on/2 pedal, nota, vel)
        {
            const int total = (int) (seconds * sr);
            for (int pos = 0; pos < total; pos += bs)
            {
                MidiBuffer mb;
                for (auto& [t, kind, n, v] : evs)
                {
                    const int sp = (int) (t * sr);
                    if (sp >= pos && sp < pos + bs)
                    {
                        if (kind == 1) mb.addEvent (MidiMessage::noteOn (1, n, (uint8) v), sp - pos);
                        else if (kind == 0) mb.addEvent (MidiMessage::noteOff (1, n), sp - pos);
                        else mb.addEvent (MidiMessage::controllerEvent (1, 64, v), sp - pos);
                    }
                }
                AudioBuffer<float> blk (2, bs); blk.clear(); q.processBlock (blk, mb); clock += bs;
            }
        };
        check (q.lastPhrase().notes.empty(), "nic nezahrane -> prazdna fraza");
        // fraza A: 12 not, potom 6 s ticha, potom fraza B: 10 not
        std::vector<std::tuple<double, int, int, int>> a, b;
        for (int i = 0; i < 12; ++i) { a.push_back ({ 0.5 + i * 0.4, 1, 60 + i, 90 }); a.push_back ({ 0.5 + i * 0.4 + 0.3, 0, 60 + i, 0 }); }
        run (6.0, a);
        run (6.0, {});
        for (int i = 0; i < 10; ++i) { b.push_back ({ 0.2 + i * 0.5, 1, 72 + (i % 5), 70 + i }); b.push_back ({ 0.2 + i * 0.5 + 0.35, 0, 72 + (i % 5), 0 }); }
        run (6.0, b);
        auto ph = q.lastPhrase();
        check (ph.notes.size() == 10, "po 6 s pauze sa zachyti len posledna fraza (10 not)");
        check (! ph.notes.empty() && ph.notes[0].t == 0.0 && ph.notes[0].k == 72, "prva nota frazy: t = 0, vyska 72");
        check (! ph.notes.empty() && std::abs (ph.notes[1].t - 0.5) < 0.001 && std::abs (ph.notes[1].d - 0.35) < 0.002, "casovanie na vzorku: odstup 0.5 s, dlzka 0.35 s");
        check (ph.bpm == 0 && ph.ppq < 0, "bez prehravania hostitela nie je bpm/ppq");
        // len 3 nove noty po dlhej pauze -> doplni sa na aspon 8
        run (6.0, {});
        std::vector<std::tuple<double, int, int, int>> c;
        for (int i = 0; i < 3; ++i) { c.push_back ({ 0.2 + i * 0.5, 1, 80 + i, 80 }); c.push_back ({ 0.2 + i * 0.5 + 0.3, 0, 80 + i, 0 }); }
        run (3.0, c);
        check (q.lastPhrase().notes.size() == 13, "po pauze len 3 noty -> siaha sa spat po dalsiu pauzu, kym je aspon 8 not (3 + 10)");
        // pedal predlzi notu
        {
            std::vector<nova::CapEvent> ev;
            auto E = [&] (double t, int kind, int n, int v) { nova::CapEvent e; e.t = t; e.kind = (uint8) kind; e.note = (uint8) n; e.vel = (uint8) v; ev.push_back (e); };
            E (0, 3, 0, 127); E (1, 1, 60, 80); E (1.5, 2, 60, 0); E (3, 3, 0, 0);
            auto nn = nova::CaptureBuffer::pairNotes (ev, 10);
            check (nn.size() == 1 && std::abs (nn[0].d - 2.0) < 1e-9, "pedal CC64 predlzi notu do uvolnenia pedalu");
        }
        // cistenie
        {
            std::vector<nova::CapNote> in; nova::CapNote n; n.k = 60; n.v = 90; n.d = 0.3; n.t = 0.02; in.push_back (n);
            n.k = 62; n.v = 20; n.d = 0.01; n.t = 0.5; in.push_back (n);                // nahodna
            n.k = 60; n.v = 90; n.d = 0.3; n.t = 0.03; in.push_back (n);                // duplikat
            n.k = 64; n.v = 90; n.d = 0.3; n.t = 0.59; in.push_back (n);
            auto cl = nova::cleanupPhrase (in, 120, true);
            check (cl.size() == 2 && cl[0].t == 0.0, "cistenie: nahodna a duplicitna nota prec, zaciatok 0");
            check (cl.size() == 2 && std::abs (cl[1].t - 0.5975) < 0.002, "kvantizacia 50 %: 0.57 -> 0.5975 (mriezka 0.625, 1/16 = 0.125 s)");
        }
        // stav pre hostitela
        const int seq0 = (int) q.params.getRawParameterValue ("capture_seq")->load();
        q.publishCapture (ph);
        MemoryBlock mb; q.getStateInformation (mb);
        auto xml = AudioProcessor::getXmlFromBinary (mb.getData(), (int) mb.getSize());
        auto* cx = xml != nullptr ? xml->getChildByName ("capture") : nullptr;
        check (cx != nullptr && cx->getIntAttribute ("id") == seq0 + 1 && cx->getNumChildElements() == 10, "stav obsahuje <capture id=N> s 10 <n>");
        check ((int) q.params.getRawParameterValue ("capture_seq")->load() == seq0 + 1, "parameter capture_seq sa zvysil");
        { NovaProcessor r; r.setStateInformation (mb.getData(), (int) mb.getSize()); MemoryBlock m2; r.getStateInformation (m2);
          auto x2 = AudioProcessor::getXmlFromBinary (m2.getData(), (int) m2.getSize());
          check (x2 != nullptr && x2->getChildByName ("capture") == nullptr, "po nacitani projektu zachytenie nezostane"); }
        // MIDI
        File dir ("/tmp/inspire"); dir.createDirectory();
        auto mf = dir.getChildFile ("t.mid");
        check (nova::writePhraseMidi (ph, mf), "MIDI zapisane");
        { FileInputStream is (mf); MidiFile r; check (r.readFrom (is) && r.getNumTracks() == 1, "MIDI sa da znova nacitat");
          int on = 0; if (r.getNumTracks() == 1) for (auto* e : *r.getTrack (0)) if (e->message.isNoteOn()) ++on;
          check (on == 10, "MIDI ma 10 not"); }
        // WAV
        auto wf = dir.getChildFile ("t.wav");
        const bool okw = nova::renderPhraseWav (ph, q.currentSettings(), { q.synth.getUserSample (0), q.synth.getUserSample (1) }, wf);
        check (okw && wf.existsAsFile(), "WAV zapisany");
        { AudioFormatManager fm; fm.registerBasicFormats(); std::unique_ptr<AudioFormatReader> r (fm.createReaderFor (wf));
          check (r != nullptr && r->sampleRate == 48000 && r->numChannels == 2 && r->bitsPerSample == 24, "WAV 48 kHz stereo 24 bit");
          if (r) { AudioBuffer<float> b (2, (int) r->lengthInSamples); r->read (&b, 0, (int) r->lengthInSamples, 0, true, true);
            bool fin = true; for (int ch = 0; ch < 2; ++ch) for (int i = 0; i < b.getNumSamples(); ++i) if (! std::isfinite (b.getSample (ch, i))) fin = false;
            const float pk = b.getMagnitude (0, b.getNumSamples());
            printf ("     WAV %.2f s, peak %.3f\n", r->lengthInSamples / 48000.0, pk);
            check (fin && pk > 0.01f && pk <= 1.0f && r->lengthInSamples > 48000 * 7, "WAV nie je ticho, konecne hodnoty, + dozvuk"); } }
        // editor: tlacidlo v oboch rezimoch
        for (int m = 0; m < 2; ++m)
        {
            q.uiMode = m;
            std::unique_ptr<AudioProcessorEditor> ed (q.createEditor()); Thread::sleep (150);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            File f (dir.getChildFile (m ? "ed_adv.png" : "ed_simple.png")); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        }
        printf (fails == 0 ? "INSPIRE: vsetko v poriadku\n" : "INSPIRE: %d chyb\n", fails);
        return fails;
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
    if (argc > 1 && String (argv[1]) == "rojbench")
    {
        // CPU pre Roj: jeden hlas s N alikvotmi (uni 1..8 = 4..32), efekty vypnute
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        q.loadPreset ("f:Roj Mellotron");
        for (auto id : { "ch_on", "dl_on", "rv_on", "b_on" }) setParam (q, id, 0);
        for (int voices : { 1, 4 })
            for (int uni : { 5, 8 })
            {
                setParam (q, "a_uni", (float) uni);
                const int total = (int) (6.0 * sr); double cpu = 0; float pk = 0; bool nan = false;
                for (int pos = 0; pos < total; pos += bs)
                {
                    MidiBuffer mb; if (pos == 0) for (int v = 0; v < voices; ++v) mb.addEvent (MidiMessage::noteOn (1, 48 + v * 4, 0.8f), 0);
                    AudioBuffer<float> blk (2, bs); blk.clear();
                    const auto t0 = Time::getHighResolutionTicks();
                    q.processBlock (blk, mb);
                    if (pos > (int) (2.0 * sr)) cpu += Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
                    for (int c = 0; c < 2; ++c) for (int i = 0; i < bs; ++i) { const float x = blk.getSample (c, i); if (! std::isfinite (x)) nan = true; pk = std::max (pk, std::abs (x)); }
                }
                const double secs = (double) (total - (int) (2.0 * sr)) / sr;
                printf ("voices %d partials %d: %.2f %% CPU (x%.0f realtime) peak %.2f %s\n", voices, uni * 4, 100.0 * cpu / secs, secs / cpu, pk, nan ? "NAN!" : "");
            }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "rojsur")
    {
        // Prekvap ma: kolko z 40 pokusov pre pad / lead / zvonky vyberie Roj, a ci su hlasitosti v norme
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int ty : { 0, 4, 5 })
        {
            int roj = 0; float lo = 99, hi = -99;
            for (int i = 0; i < 40; ++i)
            {
                q.surprise (ty); auto p = q.currentPatch();
                if (p.values.at ("a_wave") == 8.0f)
                {
                    ++roj; const float l = NovaProcessor::measureLevel (p) + p.values.at ("p_gain"); lo = std::min (lo, l); hi = std::max (hi, l);
                    if (roj <= 2) printf ("  %s %s: color %.2f spread %.1f uni %.0f fenv %.2f fa %.2f ad %.2f as %.2f p_gain %.1f\n", p.name.toRawUTF8(), p.category.toRawUTF8(), p.values.at ("a_color"), p.values.at ("a_spread"), p.values.at ("a_uni"), p.values.at ("a_fenv"), p.values.at ("a_fa"), p.values.at ("a_ad"), p.values.at ("a_as"), p.values.at ("p_gain"));
                }
            }
            printf ("typ %d: Roj %d/40, uroven %.1f .. %.1f dB\n", ty, roj, lo, hi);
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "voctest")
    {
        NovaProcessor q;
        auto lay = q.getBusesLayout();
        if (lay.inputBuses.size() > 1) lay.inputBuses.getReference (1) = AudioChannelSet::stereo();
        printf ("sidechain layout ok: %d\n", (int) q.setBusesLayout (lay));
        q.prepareToPlay (sr, bs);
        auto set = [&] (const char* id, float v) { if (auto* pr = q.params.getParameter (id)) pr->setValueNotifyingHost (pr->convertTo0to1 (v)); };
        q.loadPreset ("f:Roj Mellotron");
        set ("vc_on", 1.0f);
        const int total = (int) (3.0 * sr); double e[2] = { 0, 0 }; bool nan = false;
        double ph = 0;
        for (int pos = 0; pos < total; pos += bs)
        {
            const int n = std::min (bs, total - pos); MidiBuffer mb;
            if (pos == 0) { mb.addEvent (MidiMessage::noteOn (1, 60, 0.8f), 0); mb.addEvent (MidiMessage::noteOn (1, 64, 0.8f), 0); mb.addEvent (MidiMessage::noteOn (1, 67, 0.8f), 0); }
            AudioBuffer<float> blk (q.getTotalNumInputChannels() > q.getTotalNumOutputChannels() ? q.getTotalNumInputChannels() : q.getTotalNumOutputChannels(), n); blk.clear();
            // "hlas": pulzy 140 Hz cez formant, slabiky 4x za sekundu, druha polovica ticho
            for (int i = 0; i < n; ++i)
            {
                const double t = (pos + i) / sr; ph += 140.0 / sr; if (ph >= 1) ph -= 1;
                const float syl = t < 1.5 ? (float) (0.5 + 0.5 * std::sin (6.2831853 * 4.0 * t)) : 0.0f;
                const float v = (float) ((ph < 0.05 ? 1.0 : -0.05) * std::sin (6.2831853 * 800.0 * t) * 0.3 * syl);
                for (int c = 0; c < blk.getNumChannels(); ++c) blk.setSample (c, i, v);
            }
            q.processBlock (blk, mb);
            for (int i = 0; i < n; ++i) { const float x = blk.getSample (0, i); if (! std::isfinite (x)) nan = true; e[(pos + i) < total / 2 ? 0 : 1] += x * x; }
        }
        printf ("vocoder: RMS s hlasom %.1f dB, bez hlasu %.1f dB %s\n", 10 * std::log10 (e[0] / (total / 2) + 1e-12), 10 * std::log10 (e[1] / (total / 2) + 1e-12), nan ? "NAN!" : "");
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "gatetest")
    {
        // gater + modulacna matica: drzany ton 3 s, meria RMS v 1/16 krokoch (120 BPM -> 6000 vzoriek pri 48k)
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        auto set = [&] (const char* id, float v) { if (auto* pr = q.params.getParameter (id)) pr->setValueNotifyingHost (pr->convertTo0to1 (v)); };
        q.loadPreset ("f:Roj Mellotron");
        for (int pass = 0; pass < 3; ++pass)
        {
            set ("gt_on", pass >= 1 ? 1.0f : 0.0f);
            set ("mm1_src", pass == 2 ? 1.0f : 0.0f); set ("mm1_dst", 2); set ("mm1_amt", 0.8f); set ("a_ldepth", 0.0f); set ("a_lrate", 2.0f);
            const int total = (int) (3.0 * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos); MidiBuffer mb;
                if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, 60, 0.8f), 0);
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            bool nan = false; const int st = (int) (sr * 60.0 / 120.0 / 4.0);
            printf ("pass %d (gt %d, matrix %d): ", pass, pass >= 1, pass == 2);
            for (int k = 4; k < 20; ++k)
            {
                double e = 0; for (int i = k * st; i < (k + 1) * st && i < total; ++i) { const float x = out.getSample (0, i); if (! std::isfinite (x)) nan = true; e += x * x; }
                printf ("%.0f ", 10.0 * std::log10 (e / st + 1e-12));
            }
            printf ("%s\n", nan ? "NAN!" : "");
            MidiBuffer off; off.addEvent (MidiMessage::allNotesOff (1), 0); AudioBuffer<float> b2 (2, bs); for (int z = 0; z < 1500; ++z) { b2.clear(); q.processBlock (b2, off); off.clear(); }
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "rojwav")
    {
        // ukazky Roj: akordy / melodie do /home/claude/roj/inspire_*.wav (cez celu efektovu retaz zvuku)
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        struct Ev { double t, len; int note; float vel; };
        auto render = [&] (const char* preset, const std::vector<Ev>& evs, double dur, const char* file)
        {
            q.loadPreset (String ("f:") + preset);
            const int total = (int) (dur * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos); MidiBuffer mb;
                for (auto& e : evs)
                {
                    const int on = (int) (e.t * sr), off = (int) ((e.t + e.len) * sr);
                    if (on >= pos && on < pos + n) mb.addEvent (MidiMessage::noteOn (1, e.note, e.vel), on - pos);
                    if (off >= pos && off < pos + n) mb.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
                }
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            float pk = 0; bool nan = false;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float x = out.getSample (c, i); if (! std::isfinite (x)) nan = true; pk = std::max (pk, std::abs (x)); }
            printf ("%-16s -> %s peak %.2f %s\n", preset, file, pk, nan ? "NAN!" : "");
            File wf (file); wf.deleteFile();
            WavAudioFormat w; std::unique_ptr<OutputStream> os (wf.createOutputStream().release());
            auto wr = w.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
            wr->writeFromAudioSampleBuffer (out, 0, total);
        };
        std::vector<Ev> pad;
        const int ch[4][4] = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } };
        for (int c = 0; c < 4; ++c) for (int m : ch[c]) pad.push_back ({ c * 2.6, 2.4, m, m < 50 ? 0.6f : 0.75f });
        render ("Roj Mellotron", pad, 12.0, "/home/claude/roj/inspire_pad.wav");
        std::vector<Ev> bells; const int mel[] = { 76, 79, 81, 79, 76, 74, 72, 74 };
        for (int i = 0; i < 8; ++i) bells.push_back ({ i * 0.55, 0.5, mel[i], 0.8f });
        render ("Roj zvony", bells, 9.0, "/home/claude/roj/inspire_zvony.wav");
        std::vector<Ev> keys; const int kc[3][3] = { { 57, 60, 64 }, { 57, 62, 65 }, { 56, 59, 64 } };
        for (int c = 0; c < 3; ++c) for (int m : kc[c]) keys.push_back ({ c * 1.2, 1.1, m, 0.75f });
        render ("Roj klávesy", keys, 6.0, "/home/claude/roj/inspire_klavesy.wav");
        std::vector<Ev> lead; const int lm[] = { 69, 72, 74, 76, 74, 72, 74, 69 }; const double ld[] = { 0.4, 0.4, 0.4, 1.2, 0.4, 0.4, 0.8, 1.4 };
        double t = 0; for (int i = 0; i < 8; ++i) { lead.push_back ({ t, ld[i] * 0.95, lm[i], 0.8f }); t += ld[i]; }
        render ("Roj lead", lead, 7.0, "/home/claude/roj/inspire_lead.wav");
        std::vector<Ev> cloud; for (int m : { 57, 60, 64 }) cloud.push_back ({ 0.0, 9.0, m, 0.8f });
        render ("Roj oblak", cloud, 16.0, "/home/claude/roj/inspire_oblak.wav");
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "wttest")
    {
        // Wavetable (wave 9): cas stavby banky, NaN a urovne cez klaviaturu, alias pri C7/C8, CPU na hlas s unisonom 8
        const auto tb = Time::getMillisecondCounterHiRes();
        const auto& bank = nova::WtBank::get();
        printf ("banka: %.0f ms, %.1f MB\n", Time::getMillisecondCounterHiRes() - tb, (double) bank.data.size() * 4.0 / 1048576.0);
        auto make = [] (NovaProcessor& q, double rate)
        {
            q.setPlayConfigDetails (0, 2, rate, 256); q.prepareToPlay (rate, 256);
            for (auto id : { "ch_on", "dl_on", "rv_on", "b_on", "tube", "veltrack" }) setParam (q, id, 0);
            setParam (q, "a_wave", 9); setParam (q, "a_level", 1); setParam (q, "a_uni", 1); setParam (q, "a_as", 1); setParam (q, "a_aa", 0.002f);
        };
        auto render = [] (NovaProcessor& q, double rate, std::initializer_list<int> notes, double secs, double* cpu = nullptr)
        {
            const int total = (int) (secs * rate); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += 256)
            {
                const int n = std::min (256, total - pos); MidiBuffer mb;
                if (pos == 0) for (int k : notes) mb.addEvent (MidiMessage::noteOn (1, k, 0.8f), 0);
                AudioBuffer<float> blk (2, n); blk.clear();
                const auto t0 = Time::getHighResolutionTicks();
                q.processBlock (blk, mb);
                if (cpu != nullptr && pos > total / 4) *cpu += Time::highResolutionTicksToSeconds (Time::getHighResolutionTicks() - t0);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            MidiBuffer mb; mb.addEvent (MidiMessage::allSoundOff (1), 0); mb.addEvent (MidiMessage::allNotesOff (1), 0);
            for (int k : notes) mb.addEvent (MidiMessage::noteOff (1, k), 0);
            AudioBuffer<float> blk (2, 256); for (int k = 0; k < 300; ++k) { blk.clear(); q.processBlock (blk, mb); mb.clear(); }
            return out;
        };
        // 1) vsetky tabulky x pozicie x noty: NaN, spicka, RMS
        {
            NovaProcessor q; make (q, sr);
            bool nan = false; float rLo = 99, rHi = -99, pkMax = 0; int cnt = 0;
            for (int tab = 0; tab < 8; ++tab)
            {
                float tLo = 99, tHi = -99;
                for (float pos : { 0.0f, 0.33f, 0.66f, 1.0f })
                    for (int note = 24; note <= 108; note += 12)
                    {
                        setParam (q, "a_wt", (float) tab); setParam (q, "a_color", pos);
                        auto o = render (q, sr, { note }, 0.6);
                        const int n0 = (int) (0.2 * sr), nn = o.getNumSamples() - n0;
                        for (int c = 0; c < 2; ++c) for (int i = 0; i < o.getNumSamples(); ++i) if (! std::isfinite (o.getSample (c, i))) nan = true;
                        const float rms = Decibels::gainToDecibels (o.getRMSLevel (0, n0, nn)); pkMax = std::max (pkMax, o.getMagnitude (0, o.getNumSamples()));
                        tLo = std::min (tLo, rms); tHi = std::max (tHi, rms); ++cnt;
                    }
                printf ("tabulka %d: RMS %.1f .. %.1f dB\n", tab, tLo, tHi); rLo = std::min (rLo, tLo); rHi = std::max (rHi, tHi);
            }
            for (int w : { 2, 3, 0 }) { setParam (q, "a_wave", (float) w); auto o = render (q, sr, { 60 }, 0.6); printf ("porovnanie wave %d C4: RMS %.1f dB\n", w, Decibels::gainToDecibels (o.getRMSLevel (0, (int) (0.2 * sr), (int) (0.4 * sr)))); }
            printf ("renderov %d, NaN: %s, RMS %.1f .. %.1f dB, max spicka %.2f\n", cnt, nan ? "ANO!" : "nie", rLo, rHi, pkMax);
        }
        // 2) alias: pilovity ramec (Analog, pozicia 0), energia mimo harmonickych voci celku
        auto alias = [&] (double rate, int note, int tab, float pos, int uni, bool sweep)
        {
            NovaProcessor q; make (q, rate);
            setParam (q, "a_wt", (float) tab); setParam (q, "a_color", pos); setParam (q, "a_uni", (float) uni); setParam (q, "a_spread", 0);
            if (sweep) { setParam (q, "a_ldepth", 1); setParam (q, "a_lrate", 0.5f); }
            const int order = 16, N = 1 << order;
            auto o = render (q, rate, { note }, 0.3 + (double) N / rate + 0.05);
            std::vector<float> d ((size_t) N * 2, 0.0f);
            const float* x = o.getReadPointer (0) + (int) (0.3 * rate);
            for (int i = 0; i < N; ++i)
            {
                const double a = 2.0 * MathConstants<double>::pi * i / (N - 1);
                d[(size_t) i] = x[i] * (float) (0.35875 - 0.48829 * std::cos (a) + 0.14128 * std::cos (2 * a) - 0.01168 * std::cos (3 * a));
            }
            dsp::FFT fft (order); fft.performFrequencyOnlyForwardTransform (d.data());
            const double f0 = 440.0 * std::pow (2.0, (note - 69) / 12.0), binHz = rate / N;
            double tot = 0, off = 0, offPk = 0, pk = 0;
            for (int b = 4; b < N / 2; ++b)
            {
                const double f = b * binHz, e = (double) d[(size_t) b] * d[(size_t) b];
                const double hn = std::round (f / f0), dist = std::abs (f - hn * f0) / binHz;
                tot += e; pk = std::max (pk, e);
                if (hn < 1 || dist > 8.0) { off += e; offPk = std::max (offPk, e); }
            }
            return std::pair<double, double> (10.0 * std::log10 (off / tot + 1e-30), 10.0 * std::log10 (offPk / pk + 1e-30));
        };
        for (double rate : { 22050.0, 44100.0, 48000.0, 96000.0, 192000.0 })
            for (int note : { 36, 84, 96, 108 })
            {
                const auto r = alias (rate, note, 0, 0.0f, 1, false);
                printf ("alias %6.0f Hz nota %3d (pila): mimo harmonickych %.1f dB, najvyssia zlozka %.1f dB\n", rate, note, r.first, r.second);
            }
        for (int tab = 1; tab < 8; ++tab) { const auto r = alias (48000.0, 96, tab, 1.0f, 1, false); const auto r2 = alias (48000.0, 108, tab, 0.5f, 1, false);
            printf ("alias 48000 tabulka %d: C7 poz 1.0 %.1f dB, C8 poz 0.5 %.1f dB\n", tab, r.first, r2.first); }
        // 4) CPU: 1 a 8 hlasov, unison 8; porovnanie s pilou (wave 2)
        for (int wave : { 9, 2 })
            for (int voices : { 1, 8 })
            {
                NovaProcessor q; make (q, sr); setParam (q, "a_wave", (float) wave); setParam (q, "a_uni", 8); setParam (q, "a_spread", 20); setParam (q, "a_color", 0.37f);
                double cpu = 0; const double secs = 8.0;
                std::initializer_list<int> one { 48 }, eight { 36, 43, 48, 52, 55, 60, 64, 67 };
                render (q, sr, voices == 1 ? one : eight, secs, &cpu);
                printf ("CPU wave %d, hlasov %d x unison 8: %.2f %% (x%.0f realtime)\n", wave, voices, 100.0 * cpu / (secs * 0.75), secs * 0.75 / cpu);
            }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "wtsur")
    {
        // Prekvap ma: kolko zo 60 pokusov pre pad / bas / lead vyberie WT, urovne; Obmena meni poziciu
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int ty : { 0, 3, 4 })
        {
            int wt = 0; float lo = 99, hi = -99, dpos = 0;
            for (int i = 0; i < 60; ++i)
            {
                q.surprise (ty); auto p = q.currentPatch();
                if (p.values.at ("a_wave") == 9.0f)
                {
                    ++wt; const float l = NovaProcessor::measureLevel (p) + p.values.at ("p_gain"); lo = std::min (lo, l); hi = std::max (hi, l);
                    q.mutate(); dpos += std::abs (q.currentPatch().values.at ("a_color") - p.values.at ("a_color"));
                }
            }
            printf ("typ %d: WT %d/60, uroven %.1f .. %.1f dB, priemerna zmena pozicie pri Obmene %.3f\n", ty, wt, lo, hi, wt > 0 ? dpos / (float) wt : 0.0f);
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "savename")
    {
        NovaProcessor q;
        q.loadPreset (String ("f:") + nova::factoryPatches()[0].name);
        auto k1 = q.savePreset ("Test zvuk Z", q.currentCategory); auto n1 = q.currentName;
        auto k1b = q.savePreset ("Test zvuk Z", q.currentCategory);           // ten isty zvuk -> prepis
        q.loadPreset (String ("f:") + nova::factoryPatches()[1].name);
        auto k2 = q.savePreset ("Test zvuk Z", q.currentCategory); auto n2 = q.currentName;   // iny zvuk -> cislo
        printf ("%s | %s | same=%d | %s | %s\n", k1.toRawUTF8(), n1.toRawUTF8(), (int) (k1 == k1b), k2.toRawUTF8(), n2.toRawUTF8());
        for (auto k : { k1, k2 }) q.presets.remove (k);
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "instr")
    {
        // realisticke nastroje: nacitanie, hlasitost, slucky (praskanie), ladenie + ukazky do /home/claude/iw/
        File ("/home/claude/iw").createDirectory();
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        for (int i = 0; i < 300 && ! q.synth.pianoReady(); ++i) Thread::sleep (50);
        const auto& fp = nova::factoryPatches();
        auto render = [&] (const std::vector<std::tuple<double,int,float,double>>& ev, double secs)   // (start, note, vel, len)
        {
            const int total = (int) (secs * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos); MidiBuffer mb;
                for (auto& [t0, nt, v, len] : ev)
                {
                    const int on = (int) (t0 * sr), off = (int) ((t0 + len) * sr);
                    if (on >= pos && on < pos + n) mb.addEvent (MidiMessage::noteOn (1, nt, v), on - pos);
                    if (off >= pos && off < pos + n) mb.addEvent (MidiMessage::noteOff (1, nt), off - pos);
                }
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            for (int k = 0; k < 400; ++k) { MidiBuffer mb; AudioBuffer<float> blk (2, bs); blk.clear(); q.processBlock (blk, mb); }
            return out;
        };
        auto maxRms = [&] (const AudioBuffer<float>& b) { const int w = (int) (0.4 * sr); double acc = 0, best = 0; std::vector<double> sq;
            for (int i = 0; i < b.getNumSamples(); ++i) { const double v = 0.5 * (b.getSample (0, i) * b.getSample (0, i) + b.getSample (1, i) * b.getSample (1, i)); sq.push_back (v); acc += v; if (i >= w) acc -= sq[(size_t) (i - w)]; best = std::max (best, acc / w); }
            return 10.0 * std::log10 (best + 1e-12); };
        auto clicks = [&] (const AudioBuffer<float>& b, double from, double to)   // max druha diferencia / typicka
        {
            const float* x = b.getReadPointer (0); std::vector<float> d;
            for (int i = (int) (from * sr) + 1; i < std::min (b.getNumSamples() - 1, (int) (to * sr)); ++i) d.push_back (std::abs (x[i + 1] - 2 * x[i] + x[i - 1]));
            if (d.empty()) return 0.0f; auto e = d; std::nth_element (e.begin(), e.begin() + (long) (e.size() * 0.99), e.end());
            const float p99 = e[(size_t) (e.size() * 0.99)] + 1e-9f; return *std::max_element (d.begin(), d.end()) / p99;
        };
        for (int idx = 0; idx < (int) fp.size(); ++idx)
        {
            const auto& f = fp[(size_t) idx];
            if (nova::categorySection (f.category) != 0) continue;
            q.loadPreset ("f:" + f.name);
            const int inst = (int) f.values.at ("a_inst");
            q.synth.requestInstr (inst);
            const auto t0 = Time::getMillisecondCounterHiRes();
            for (int i = 0; i < 400 && ! q.synth.instrReady (inst); ++i) Thread::sleep (25);
            const double loadMs = Time::getMillisecondCounterHiRes() - t0;
            const bool sus = (inst >= 8 && inst <= 16) || inst == 21 || (inst >= 23 && inst <= 28) || inst == 34 || inst == 35;
            const bool mono = (inst >= 9 && inst <= 16 && inst != 10) || inst == 6 || inst == 7 || inst == 21 || (inst >= 24 && inst <= 27) || inst == 34 || inst == 35;
            static const int centers[36] = { 60, 60, 60, 60, 55, 55, 40, 40, 62, 74, 52, 70, 70, 62, 50, 76, 64, 70, 70, 88,
                                             62, 64, 60, 60, 52, 70, 45, 36, 62, 62, 64, 76, 76, 60, 65, 65 };
            const int c = centers[inst];
            // hlasitost: akord (alebo jeden ton pri melodickych nastrojoch)
            std::vector<std::tuple<double,int,float,double>> ev;
            if (mono) ev.push_back ({ 0.0, c, 0.8f, 1.2 }); else for (int d : { -12, -5, 0, 4 }) ev.push_back ({ 0.0, c + d, 0.8f, 1.2 });
            const auto lv = render (ev, 2.0);
            const float m = (float) maxRms (lv) - f.values.at ("p_gain");
            const float tgt = mono ? -18.0f : (sus ? -19.0f : -16.0f);
            // drzany ton 6 s (slucky) pri dvoch vyskach
            float ck = 0;
            if (sus) for (int d : { -5, 3 }) { auto h = render ({ { 0.0, c + d, 0.8f, 6.0 } }, 6.5); ck = std::max (ck, clicks (h, 0.6, 5.8));
                WavAudioFormat hf; File ho ("/home/claude/iw/held_" + String (inst) + "_" + String (c + d) + ".wav"); ho.deleteFile();
                if (auto w = std::unique_ptr<AudioFormatWriter> (hf.createWriterFor (new FileOutputStream (ho), sr, 2, 24, {}, 0))) w->writeFromAudioSampleBuffer (h, 0, h.getNumSamples()); }
            // ukazka: stupnica + akordy
            std::vector<std::tuple<double,int,float,double>> demo; double t = 0;
            const int sc[] = { 0, 2, 4, 5, 7, 9, 11, 12 };
            for (int k = 0; k < 8; ++k) { demo.push_back ({ t, c - 7 + sc[k], 0.45f + 0.06f * k, sus ? 0.5 : 0.35 }); t += 0.42; }
            if (! mono) { for (int d : { -12, -5, 0, 4 }) demo.push_back ({ t + 0.2, c + d, 0.75f, 2.0 }); t += 2.6; }
            else { demo.push_back ({ t + 0.2, c, 0.8f, 2.4 }); t += 3.0; }
            auto dm = render (demo, t + 1.5);
            WavAudioFormat wf; File out ("/home/claude/iw/" + String (inst).paddedLeft ('0', 2) + "_" + f.name.replace (" ", "_") + ".wav"); out.deleteFile();
            if (auto w = std::unique_ptr<AudioFormatWriter> (wf.createWriterFor (new FileOutputStream (out), sr, 2, 16, {}, 0))) w->writeFromAudioSampleBuffer (dm, 0, dm.getNumSamples());
            printf ("%d|%s|inst %d|load %.0f ms|level %.1f dB|gain %.1f|click %.1f\n", idx, f.name.toRawUTF8(), inst, loadMs, m, jlimit (-30.0f, 6.0f, tgt - m), ck);
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "shapes")
    {
        // nove parametre vrstvy: skok vysky, sum, filter podla klavesu + snimky editora -> /home/claude/shp/
        File ("/home/claude/shp").createDirectory();
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        auto renderN = [&] (int note, double secs) { const int total = (int) (secs * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs) { const int n = std::min (bs, total - pos); MidiBuffer mb; if (pos == 0) mb.addEvent (MidiMessage::noteOn (1, note, 0.8f), 0);
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb); for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n); }
            for (int k = 0; k < 20; ++k) { MidiBuffer mb; for (int n = 0; n < 128; ++n) mb.addEvent (MidiMessage::noteOff (1, n), 0); AudioBuffer<float> blk (2, bs); q.processBlock (blk, mb); }
            for (int k = 0; k < 300; ++k) { MidiBuffer mb; AudioBuffer<float> blk (2, bs); blk.clear(); q.processBlock (blk, mb); }
            return out; };
        auto zc = [&] (const AudioBuffer<float>& b, double t0, double t1) { int z = 0; const int a = (int) (t0 * sr), e = (int) (t1 * sr);
            for (int i = a + 1; i < e; ++i) if (b.getSample (0, i - 1) <= 0 && b.getSample (0, i) > 0) ++z; return z / (t1 - t0); };
        auto rms = [&] (const AudioBuffer<float>& b) { double s = 0; for (int i = 0; i < b.getNumSamples(); ++i) s += b.getSample (0, i) * b.getSample (0, i); return Decibels::gainToDecibels ((float) std::sqrt (s / b.getNumSamples()), -120.0f); };
        auto base = [&] { q.loadPreset (String ("f:") + nova::factoryPatches()[0].name);
            for (auto id : { "ch_on", "dl_on", "rv_on", "arp_on", "b_on", "drive_on", "comp_on" }) setParam (q, id, 0);
            setParam (q, "a_wave", 0); setParam (q, "a_ldepth", 0); setParam (q, "a_uni", 1); setParam (q, "a_cut", 20000); setParam (q, "a_fenv", 0); setParam (q, "tube", 0); };
        base(); { auto b = renderN (57, 1.0); printf ("sinus A3: zc 0-0.03 %.0f Hz, 0.5-0.9 %.0f Hz, %.1f dB\n", zc (b, 0.0, 0.03), zc (b, 0.5, 0.9), rms (b)); }
        base(); setParam (q, "a_penv", 12); setParam (q, "a_ptime", 0.1f);
        { auto b = renderN (57, 1.0); printf ("skok +12: zc 0-0.03 %.0f Hz, 0.5-0.9 %.0f Hz, %.1f dB\n", zc (b, 0.0, 0.03), zc (b, 0.5, 0.9), rms (b)); }
        base(); setParam (q, "a_noise", 1);
        { auto b = renderN (57, 1.0); float pk = 0; for (int i = 0; i < b.getNumSamples(); ++i) pk = std::max (pk, std::abs (b.getSample (0, i))); printf ("sum 1.0: %.1f dB, spicka %.2f\n", rms (b), pk); }
        for (float kt : { 0.0f, 0.5f, 1.0f })
        {
            base(); setParam (q, "a_wave", 2); setParam (q, "a_cut", 600); setParam (q, "a_ktrk", kt);
            auto lo = renderN (36, 0.6), hi = renderN (84, 0.6);
            printf ("pila cut 600, kláves %.1f: C2 %.1f dB, C6 %.1f dB\n", kt, rms (lo), rms (hi));
        }
        // zabudovane vzorky: 5 s drzany ton, hlasitost na zaciatku a v 4. sekunde
        for (int i = 0; i < NovaProcessor::builtinSampleCount(); ++i)
        {
            base(); q.loadBuiltinSample (0, i);
            auto b = renderN (i == 6 || i == 7 || i == 8 ? 36 : 60, 5.0);
            auto rmsAt = [&] (double t0) { double s2 = 0; const int a0 = (int) (t0 * sr), n0 = (int) (0.2 * sr); for (int j = a0; j < a0 + n0; ++j) s2 += b.getSample (0, j) * b.getSample (0, j); return Decibels::gainToDecibels ((float) std::sqrt (s2 / n0), -120.0f); };
            printf ("vzorka %-20s 0.3s %6.1f dB  2.5s %6.1f dB  4.5s %6.1f dB  zc %.0f Hz\n", NovaProcessor::builtinSampleName (i).toRawUTF8(), rmsAt (0.3), rmsAt (2.5), rmsAt (4.5), zc (b, 0.5, 1.0));
        }
        // LFO podla tempa: 1/8 pri 120 BPM = 4 Hz
        base(); setParam (q, "a_wave", 0); setParam (q, "a_ltgt", 2); setParam (q, "a_ldepth", 1.0f); setParam (q, "a_lsync", argc > 2 ? 0.0f : 4.0f); setParam (q, "a_lrate", 4);
        { auto b = renderN (69, 2.0); const int h = (int) (0.01 * sr); std::vector<float> e; for (int j = 0; j + h < b.getNumSamples(); j += h) { double s2 = 0; for (int u = 0; u < h; ++u) s2 += b.getSample (0, j + u) * b.getSample (0, j + u); e.push_back ((float) std::sqrt (s2 / h)); }
          float mean = 0; for (float v : e) mean += v; mean /= (float) e.size(); int cr = 0; for (size_t j = 21; j < e.size(); ++j) if (e[j - 1] < mean && e[j] >= mean) ++cr;
          printf ("LFO 1/8 pri 120 BPM: %.1f vlnení za sekundu\n", cr / ((e.size() - 21) * 0.005)); }
        q.uiMode = 1; int k = 0;
        for (int w : { 2, 7, 9, 77, 78 })
        {
            base(); if (w == 7) { setParam (q, "a_wave", 7); setParam (q, "a_src", 0); setParam (q, "a_inst", 13); } else if (w == 77) q.loadBuiltinSample (0, 2); else if (w == 78) { setParam (q, "a_wave", 7); q.setSourceMode (0, 1); } else setParam (q, "a_wave", (float) w);
            std::unique_ptr<AudioProcessorEditor> ed (q.createEditor()); Thread::sleep (150);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            File f ("/home/claude/shp/shot_" + String (k++) + ".png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        }
        q.uiMode = 0;
        for (const char* nm : { "Koncertný klavír", "Trúbka s dusítkom", "Teplý pad", "Synth pluck" })
        {
            q.loadPreset (String ("f:") + String::fromUTF8 (nm));
            std::unique_ptr<AudioProcessorEditor> ed (q.createEditor()); Thread::sleep (150);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            File f ("/home/claude/shp/simple_" + String (k++) + ".png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "wtshot")
    {
        // snimky rozsireneho editora s displejom hlasov: WT, Roj, pila s unisonom, klavir, FM  -> /home/claude/wt/shot_*.png
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        q.uiMode = 1;
        int k = 0;
        for (const char* nm : { "WT Vokál pad", "Roj Mellotron", "Teplý pad", "Koncertné piano", "Elektrické piano", "WT Analog bas" })
        {
            q.loadPreset (String ("f:") + String::fromUTF8 (nm));
            std::unique_ptr<AudioProcessorEditor> ed (q.createEditor());
            Thread::sleep (100);
            auto img = ed->createComponentSnapshot (ed->getLocalBounds());
            File f ("/home/claude/wt/shot_" + String (k++) + ".png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
            printf ("%s -> %s (%d x %d)\n", nm, f.getFullPathName().toRawUTF8(), img.getWidth(), img.getHeight());
        }
        return 0;
    }
    if (argc > 1 && String (argv[1]) == "wtwav")
    {
        // ukazky WT zvukov do /home/claude/wt/*.wav (cez celu efektovu retaz)
        NovaProcessor q; q.setPlayConfigDetails (0, 2, sr, bs); q.prepareToPlay (sr, bs);
        struct Ev { double t, len; int note; float vel; };
        auto render = [&] (const char* preset, const std::vector<Ev>& evs, double dur, const char* file)
        {
            q.loadPreset (String ("f:") + String::fromUTF8 (preset));
            const int total = (int) (dur * sr); AudioBuffer<float> out (2, total); out.clear();
            for (int pos = 0; pos < total; pos += bs)
            {
                const int n = std::min (bs, total - pos); MidiBuffer mb;
                for (auto& e : evs)
                {
                    const int on = (int) (e.t * sr), off = (int) ((e.t + e.len) * sr);
                    if (on >= pos && on < pos + n) mb.addEvent (MidiMessage::noteOn (1, e.note, e.vel), on - pos);
                    if (off >= pos && off < pos + n) mb.addEvent (MidiMessage::noteOff (1, e.note), off - pos);
                }
                AudioBuffer<float> blk (2, n); blk.clear(); q.processBlock (blk, mb);
                for (int c = 0; c < 2; ++c) out.copyFrom (c, pos, blk, c, 0, n);
            }
            float pk = 0; bool nan = false;
            for (int c = 0; c < 2; ++c) for (int i = 0; i < total; ++i) { const float x = out.getSample (c, i); if (! std::isfinite (x)) nan = true; pk = std::max (pk, std::abs (x)); }
            printf ("%-16s -> %s zvuk '%s' spicka %.2f RMS %.1f dB %s\n", preset, file, q.currentName.toRawUTF8(), pk, Decibels::gainToDecibels (out.getRMSLevel (0, 0, total)), nan ? "NAN!" : "");
            File wf (file); wf.deleteFile();
            WavAudioFormat w; std::unique_ptr<OutputStream> os (wf.createOutputStream().release());
            auto wr = w.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
            wr->writeFromAudioSampleBuffer (out, 0, total);
        };
        std::vector<Ev> pad;
        const int ch[4][4] = { { 45, 57, 60, 64 }, { 41, 57, 60, 65 }, { 48, 55, 60, 64 }, { 43, 55, 59, 62 } };
        for (int c = 0; c < 4; ++c) for (int m : ch[c]) pad.push_back ({ c * 3.2, 3.0, m, m < 50 ? 0.6f : 0.75f });
        render ("WT Vokál pad", pad, 16.0, "/home/claude/wt/wt_vokal_pad.wav");
        std::vector<Ev> lead; const int lm[] = { 69, 72, 74, 76, 74, 72, 74, 69, 81, 79, 76, 72 }; const double ld[] = { 0.4, 0.4, 0.4, 1.2, 0.4, 0.4, 0.8, 1.2, 0.4, 0.4, 0.4, 1.6 };
        double t = 0; for (int i = 0; i < 12; ++i) { lead.push_back ({ t, ld[i] * 0.95, lm[i], 0.8f }); t += ld[i]; }
        render ("WT Digitál lead", lead, 10.0, "/home/claude/wt/wt_digital_lead.wav");
        std::vector<Ev> bass; const int bm[] = { 33, 33, 45, 33, 31, 31, 43, 36, 29, 29, 41, 29, 28, 28, 40, 35 };
        for (int i = 0; i < 16; ++i) bass.push_back ({ i * 0.3, 0.26, bm[i], i % 4 == 0 ? 0.9f : 0.7f });
        render ("WT Analog bas", bass, 5.6, "/home/claude/wt/wt_analog_bas.wav");
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
