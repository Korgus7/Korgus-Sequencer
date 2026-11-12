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
        Timer::callAfterDelay (argc > 2 ? String (argv[2]).getIntValue() * 1000 : 60000, [] { MessageManager::getInstance()->stopDispatchLoop(); });
        MessageManager::getInstance()->runDispatchLoop();
        printf ("zvuk: %s, rezim %d, mierka %.2f\n", proc->currentName.toRawUTF8(), proc->uiMode, proc->uiScale);
        w->clearContentComponent(); delete proc->getActiveEditor(); delete w;
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
