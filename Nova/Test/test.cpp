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
    NovaProcessor p;
    p.setPlayConfigDetails (0, 2, sr, bs);
    p.prepareToPlay (sr, bs);
    for (int i = 0; i < 200 && ! p.synth.pianoReady() && ! p.synth.pianoFailed(); ++i) Thread::sleep (50);
    printf ("piano ready %d failed %d\n", p.synth.pianoReady(), p.synth.pianoFailed());
    File dir ("/tmp/nova"); dir.createDirectory();
    const int np = (int) nova::presets().size();
    for (int pi = 0; pi < np; ++pi)
    {
        setParam (p, "preset", (float) pi);
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
                nova::presets()[(size_t) pi].name, peak, std::sqrt (e / (2.0 * total)), tail, maxJump, jumpAt / sr, num / den, dur / cpu, nan ? "NAN!" : "");
        File wf = dir.getChildFile ("preset" + String (pi) + ".wav"); wf.deleteFile();
        WavAudioFormat w; std::unique_ptr<OutputStream> os (wf.createOutputStream().release());
        auto wr = w.createWriterFor (os, AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (2).withBitsPerSample (24));
        wr->writeFromAudioSampleBuffer (out, 0, total);
    }
    if (argc > 1)
    {
        setParam (p, "preset", 3.0f);
        std::unique_ptr<AudioProcessorEditor> ed (p.createEditor());
        ed->setVisible (true);
        Thread::sleep (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds());
        File f ("/tmp/nova/editor.png"); f.deleteFile(); FileOutputStream os (f); PNGImageFormat().writeImageToStream (img, os);
        printf ("editor %dx%d\n", ed->getWidth(), ed->getHeight());
    }
    return 0;
}
