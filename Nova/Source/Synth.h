#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"
#include "Fx.h"
#include "PianoBank.h"

// Zvukovy motor Korgus Nova: 2 vrstvy na hlas (sinus, trojuholnik, pila, obdlznik, sum, pluck, FM, sampel),
// filter s obalkou, LFO, mono/glide, arpeggiator a spolocne efekty
namespace nova
{
    struct UserSample
    {
        juce::AudioBuffer<float> buf;   // 1-2 kanaly
        double rate = 48000;
        juce::String name;
        juce::MemoryBlock flac;         // na ulozenie do projektu / zvuku
    };
    using UserSamplePtr = std::shared_ptr<const UserSample>;

    struct LayerSettings
    {
        bool on = true; int wave = 2; float oct = 0, semi = 0, fine = 0; int uni = 1; float spread = 10, level = 0.7f, pan = 0, color = 0.5f, ratio = 1;
        int ftype = 0; bool f24 = false; float cut = 20000, reso = 0.1f, fenv = 0, fa = 0.005f, fd = 0.3f, fs = 1, fr = 0.3f, aa = 0.003f, ad = 0.3f, as = 1, ar = 0.3f;
        int lshape = 0, ltgt = 0; float lrate = 5, ldepth = 0, ldelay = 0; int src = 0; int root = 60; bool loop = false;
    };

    struct Settings
    {
        LayerSettings L[2];
        FxSettings fx;
        bool arpOn = false, arpHold = false; int arpPat = 0, arpSpeed = 3, arpOct = 1; float arpLen = 0.6f;
        bool mono = false; float glide = 0, veltrack = 0.7f, morphX = 0, morphY = 0;
        // makra jednoduchej verzie
        float bright = 0.5f, attack = 0.5f, release = 0.5f, space = 0.5f, width = 0.5f;
        bool playing = false; double ppq = 0;
    };

    class Synth
    {
    public:
        Synth();
        void prepare (double sampleRate, int maxBlock);
        void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Settings& s);
        void setUserSample (int layer, UserSamplePtr p) { const juce::SpinLock::ScopedLockType l (smpLock); userSmp[layer & 1] = std::move (p); }
        UserSamplePtr getUserSample (int layer) const { const juce::SpinLock::ScopedLockType l (smpLock); return userSmp[layer & 1]; }
        void fadeAll() { fadeRequest = true; }
        bool pianoReady() const { return bank != nullptr && bank->ready(); }
        bool pianoFailed() const { return bank != nullptr && bank->failed(); }
        float gainReduction() const { return fx.gainReduction.load(); }
        int activeVoices() const { return active.load(); }

    private:
        struct LayerVoice
        {
            bool active = false; int wave = 2;
            Env amp, fenv;
            float ph[8] {}, det[8] {}, pl[8] {}, pr[8] {};
            Svf f1L, f1R, f2L, f2R, body[5]; int cnt = 0;
            float lfoPh = 0, lfoHold = 0, lfoPrev = 0; float t = 0;
            OnePole noiseL, tiltL, tiltR;
            // pluck
            std::vector<float> dl; int dlen = 100, w = 0; float apC = 0, apIn = 0, apOut = 0, yPrev = 0, loopG = 0.99f, loopS = 0.3f, plPanL = 0.7f, plPanR = 0.7f;
            // FM
            float cPh = 0, mPh = 0;
            // sampel
            const PianoBank::Sample* smp = nullptr; UserSamplePtr user; double pos = 0; float gain = 1, level = 1, dampCoef = 1, atk = 1, atkInc = 1; bool damping = false, noDamper = false;
            float velGain = 1, peak = 0;
        };
        struct Voice
        {
            int note = -1; float vel = 0; bool held = false, sustained = false; uint64_t age = 0;
            float curNote = 60, targetNote = 60, drift = 0, driftTarget = 0;
            LayerVoice lv[2];
            bool isActive() const { return lv[0].active || lv[1].active; }
        };

        void handleNoteOn (int note, float vel);
        void handleNoteOff (int note);
        void noteOn (int note, float vel);
        void noteOff (int note);
        void setSustain (bool on);
        Voice* allocVoice();
        void startVoice (Voice& v, int note, float vel, float fromNote);
        void startLayer (Voice& v, int li);
        void releaseVoice (Voice& v);
        void renderLayer (Voice& v, int li, float* L, float* R, int n);
        void arpTick (int samples);
        void arpStep();
        void arpStopNotes();

        std::array<Voice, 48> voices;
        std::shared_ptr<PianoBank> bank;
        UserSamplePtr userSmp[2];
        juce::SpinLock smpLock;
        Settings st;                       // nastavenia aktualneho bloku
        double sr = 48000;
        uint64_t ageCounter = 0;
        bool sustain = false, fadeRequest = false;
        float bend = 0, modWheel = 0;
        int lastNote = -1;
        std::vector<int> monoStack;
        std::vector<float> bufL, bufR;
        FxChain fx;
        Noise noise;
        std::atomic<int> active { 0 };
        float bright = 0, atkScale = 1, relScale = 1;
        // arpeggiator
        std::vector<int> arpHeld, arpVel, physDown; std::vector<int> arpPlaying; float arpLastVel = 0.8f;
        double beat = 0, stepStartBeat = 0; int64_t lastStepIndex = -1; int arpIndex = 0; bool arpRunning = false, arpWasOn = false, arpLatchedRelease = false;
    };
}
