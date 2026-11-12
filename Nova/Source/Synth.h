#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"
#include "PianoBank.h"

// Zvukovy motor Korgus Nova: klavir zo vzoriek, analogove pady a slacikly, brnknutie (Karplus-Strong), FM elektricke piano a celesta
namespace nova
{
    struct Preset
    {
        const char* name = "";
        int engine = 1;                    // 0 klavir, 1 analog, 2 brnknutie, 3 FM
        float a = 0.01f, d = 0.5f, s = 0.8f, r = 0.4f;
        // analog
        int uni = 1; float detune = 10, saw = 1, pulse = 0, pw = 0.5f, sub = 0, noise = 0, octUp = 0;
        float cut = 3000, reso = 0.7f, kt = 0.5f, fAmt = 0, fa = 0.01f, fd = 0.5f, fs = 0.5f, fr = 0.5f, velCut = 1.0f;
        float vibRate = 5, vibDepth = 0, vibDelay = 0.3f, drift = 0; bool body = false; float bowNoise = 0;
        // brnknutie
        float plBright = 0.6f, plT60 = 3, plPick = 0.15f, plRel = 0.25f, plDouble = 0;
        // FM
        float fmRatio = 1, fmIndex = 1, fmIdxT = 0.5f, fmIdxFloor = 0.1f, fmTine = 0, fmTineRatio = 14, fmDecay = 3, trem = 0;
        // klavir
        float pnLayerBias = 0, pnTone = 0;
        // efekty
        float gain = 1, chorus = 0, chorusDepth = 3, reverb = 0.2f, revSize = 0.5f, revDamp = 0.4f;
    };

    const std::vector<Preset>& presets();

    struct Knobs { float bright = 0.5f, attack = 0.5f, release = 0.5f, space = 0.5f, width = 0.5f, volumeDb = 0; };

    class Synth
    {
    public:
        Synth();
        void prepare (double sampleRate, int maxBlock);
        void setPreset (int index);
        int getPreset() const { return presetIndex; }
        void setKnobs (const Knobs& k) { knobs = k; }
        void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi);
        bool pianoReady() const { return bank != nullptr && bank->ready(); }
        bool pianoFailed() const { return bank != nullptr && bank->failed(); }
        bool presetNeedsPiano() const { return presets()[(size_t) presetIndex].engine == 0; }
        void allNotesOff();

    private:
        struct Voice
        {
            int note = -1; float vel = 0; bool held = false, sustained = false; uint64_t age = 0; int engine = 1;
            float sr = 48000, freq = 440, t = 0;
            Env amp, fenv;
            // analog
            float ph[8] {}, inc[8] {}, pl[8] {}, pr[8] {}, drPh[8] {}, drRate[8] {}, subPh = 0, vibPh = 0;
            Svf fL, fR, body[5]; int cnt = 0; float bowEnv = 0;
            // brnknutie
            std::vector<float> dl; int dlen = 100, w = 0; float apC = 0, apIn = 0, apOut = 0, yPrev = 0, loopG = 0.99f, loopS = 0.3f, relG = 0.9f;
            float plLev = 1, panL = 0.7f, panR = 0.7f; int dl2len = 0; std::vector<float> dl2; int w2 = 0; float yPrev2 = 0, apIn2 = 0, apOut2 = 0, apC2 = 0;
            // FM
            float cPh = 0, mPh = 0, tPh = 0, idx0 = 1, tineIdx = 0, fmDec = 1;
            // klavir
            const PianoBank::Sample* smp = nullptr; double pos = 0, step = 1; float gain = 1, dampTau = 0.1f, dampCoef = 1, level = 1; bool noDamper = false, damping = false;
            float atkInc = 1, atk = 1;
        };

        void noteOn (int note, float vel);
        void noteOff (int note);
        void setSustain (bool on);
        Voice* allocVoice (int note);
        void startVoice (Voice& v, int note, float vel);
        void renderVoice (Voice& v, float* L, float* R, int n);
        void renderAnalog (Voice& v, float* L, float* R, int n);
        void renderPluck (Voice& v, float* L, float* R, int n);
        void renderFm (Voice& v, float* L, float* R, int n);
        void renderPiano (Voice& v, float* L, float* R, int n);
        void releaseVoice (Voice& v);
        float attackScale() const { return std::pow (2.0f, (knobs.attack - 0.5f) * 5.0f); }
        float releaseScale() const { return std::pow (2.0f, (knobs.release - 0.5f) * 5.0f); }

        std::array<Voice, 64> voices;
        std::shared_ptr<PianoBank> bank;
        int presetIndex = 0;
        Knobs knobs;
        double sr = 48000;
        uint64_t ageCounter = 0;
        bool sustain = false;
        float bend = 0, modWheel = 0;
        std::vector<float> bufL, bufR;
        Chorus chorus; Reverb reverb; OnePole tiltL, tiltR; float tremPh = 0;
        float volSm = 1, widthSm = 1, revSm = 0.2f, chSm = 0; int fadeOut = 0;
        Noise noise;
    };
}
