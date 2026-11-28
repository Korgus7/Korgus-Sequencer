#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"
#include "Fx.h"
#include "PianoBank.h"
#include "InstrBank.h"
#include "Wavetable.h"
#include "VoxPrep.h"
#include "../../Source/KorgusVocoder.h"

// Zvukovy motor Korgus Nova: 2 vrstvy na hlas (sinus, trojuholnik, pila, obdlznik, sum, pluck, FM, sampel, Roj, wavetable),
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
    // editor samplu (nedestruktivne): zaciatok/koniec a slucka ako cast dlzky 0..1, nabeh/doznenie v sekundach, prelinanie slucky v s, odzadu
    struct SmpEdit { float start = 0, end = 1, fin = 0, fout = 0, ls = 0.3f, le = 0.7f, xf = 0.01f; bool rev = false;
                     // skenovanie: slucka sa sama posuva po sampli (tvar 0 tam a spat, 1 dopredu, 2 nahodne)
                     bool scan = false; float scanRate = 0.25f, scanRange = 0.5f; int scanShape = 0;
                     bool fromStart = false;  // so sluckou: ton zacne od zaciatku vyberu (inak hned v slucke) – ako Korgus Sampler
                   };

    struct LayerSettings
    {
        bool on = true; int wave = 2; float oct = 0, semi = 0, fine = 0; int uni = 1; float spread = 10, level = 0.7f, pan = 0, color = 0.5f, ratio = 1;
        int ftype = 0; bool f24 = false; float cut = 20000, reso = 0.1f, fenv = 0, fa = 0.005f, fd = 0.3f, fs = 1, fr = 0.3f, aa = 0.003f, ad = 0.3f, as = 1, ar = 0.3f;
        int lshape = 0, ltgt = 0; float lrate = 5, ldepth = 0, ldelay = 0; int src = 0; int root = 60; bool loop = false;
        float eqLow = 0, eqMid = 0, eqMidF = 1000, eqHigh = 0; bool eqEnabled = true; float eq8[18] { 20, 0, 150, 0, 1000, 0.8f, 0, 300, 1, 0, 3000, 1, 0, 6000, 1, 0, 2500, 20000 };
        int wt = 0;                     // wavetable (wave 9): cislo tabulky; pozicia = color
        // tvarovanie zvuku: obalka vysky (skok na zaciatku tonu), filter podla klavesu a sily uderu, primiesany sum
        float penv = 0, ptime = 0.08f;  // poltony, s
        float ktrk = 0.5f, fvel = 0.5f; // 0..1 (0.5 = povodne spravanie)
        float noise = 0;                // 0..1
        int lsync = 0;                  // LFO podla tempa: 0 volne, inak 1/1 1/2 1/4 1/8 1/16 1/4T 1/8T 1/16T
        int inst = 0;                   // nastroj pri zdroji 0: 0 koncertny klavir, inak InstrBank
    };

    struct Settings
    {
        LayerSettings L[2];
        FxSettings fx;
        bool arpOn = false, arpHold = false; int arpPat = 0, arpSpeed = 3, arpOct = 1; float arpLen = 0.6f;
        bool mono = false; float glide = 0, veltrack = 0.7f, morphX = 0, morphY = 0;
        // makra jednoduchej verzie
        float bright = 0.5f, attack = 0.5f, release = 0.5f, space = 0.5f, width = 0.5f, full = 0;
        bool playing = false; double ppq = 0;
        // rytmicky gater
        bool gtOn = false; int gtRate = 3; float gtDepth = 0.85f, gtSmooth = 0.25f, gtSwing = 0; bool gtPat[16] {};
        // modulacna matica
        struct ModSlot { int src = 0, dst = 2; float amt = 0; } mm[4];
        // vocoder
        bool vcOn = false; float vcBands = 20, vcFormant = 0, vcClarity = 0.65f, vcRelease = 80, vcDry = 0.15f;
        float vcMix = 1.0f;
        bool vcEqOn = true; float vcEqLow = 0, vcEqMid = 0, vcEqMidF = 1000, vcEqHigh = 0;
    };

    class Synth
    {
    public:
        Synth();
        void prepare (double sampleRate, int maxBlock);
        void process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Settings& s);
        void setUserSample (int layer, UserSamplePtr p) { const juce::SpinLock::ScopedLockType l (smpLock); userSmp[layer & 1] = std::move (p); }
        UserSamplePtr getUserSample (int layer) const { const juce::SpinLock::ScopedLockType l (smpLock); return userSmp[layer & 1]; }
        void setSmpEdit (int layer, const SmpEdit& e) { const juce::SpinLock::ScopedLockType l (smpLock); smpEd[layer & 1] = e; }
        SmpEdit getSmpEdit (int layer) const { const juce::SpinLock::ScopedLockType l (smpLock); return smpEd[layer & 1]; }
        // kde vo vlastnom sampli prave hra ton (0..1) – ukazovatel v editore samplu
        std::atomic<float> playPos[2] { { -1.0f }, { -1.0f } };
        std::atomic<int> playTick[2] { { 0 }, { 0 } };
        void fadeAll() { fadeRequest = true; }
        bool pianoReady() const { return bank != nullptr && bank->ready(); }
        bool pianoFailed() const { return bank != nullptr && bank->failed(); }
        // ma nastroj na tejto klavese zvuk? (bicie a tympany hraju len na niektorych klavesoch)
        bool instrKeyMap (int id, std::array<bool, 128>& out, bool& full) const
        {
            full = true; out.fill (false);
            if (id <= 0 || id == InstrBank::oldPiano || ibank == nullptr) return false;
            const auto* I = ibank->find (id); if (I == nullptr) return false;
            for (int n = 0; n < 128; ++n) { bool on = false; for (int l = 0; l < I->nLayers; ++l) on = on || I->map[(size_t) n][(size_t) l] != nullptr; out[(size_t) n] = on; full = full && on; }
            return true;
        }
        int instrStatus (int id, float& prog) const { prog = 0; if (id <= 0 || id == InstrBank::oldPiano || ibank == nullptr) return 0; if (ibank->downloading (id)) { prog = ibank->progress (id); return 4; } return ibank->failed (id) ? 3 : 0; }
        bool instrReady (int id) const { if (id == InstrBank::oldPiano) { auto* b = bank2Raw.load(); return b != nullptr && b->ready(); } return id == 0 ? pianoReady() : ibank != nullptr && ibank->ready (id); }
        // volat z hlavneho vlakna (nacitanie povodneho klavira az ked je treba)
        void requestInstr (int id) { if (id == InstrBank::oldPiano) { if (bank2 == nullptr) { bank2 = PianoBank::get (1); bank2Raw.store (bank2.get()); } return; } if (ibank != nullptr) ibank->request (id); }
        float gainReduction() const { return fx.gainReduction.load(); }
        int activeVoices() const { return active.load(); }
        Analyzer analyzer;                 // spektrum pre EQ v rozhrani

    private:
        struct LayerVoice
        {
            bool active = false; int wave = 2;
            Env amp, fenv;
            float ph[16] {}, det[16] {}, pl[16] {}, pr[16] {}; float osL[32] {}, osR[32] {}; int osW = 0;   // 2x prevzorkovanie oscilatorov (cistejsie vysky)
            Svf f1L, f1R, f2L, f2R, body[5]; int cnt = 0;
            float lfoPh = 0, lfoHold = 0, lfoPrev = 0; float t = 0; float lfoOut = 0;
            OnePole noiseL, tiltL, tiltR;
            // pluck
            std::vector<float> dl; int dlen = 100, w = 0; float apC = 0, apIn = 0, apOut = 0, yPrev = 0, loopG = 0.99f, loopS = 0.3f, plPanL = 0.7f, plPanR = 0.7f;
            // FM
            float cPh = 0, mPh = 0;
            // Roj (harmonicky roj): 32 sinusovych alikvot s injekcnym zamykanim na hlavnu fazu
            float rojTh[32] {}, rojPsi[32] {}, rojG[32] {}, rojC[32] {}; float rojPhi = 0, rojFocus = -1; int rojN = 0, rojTog = 0;   // rojC: vazba (obnovuje sa kazdy 2. vzorok)
            // wavetable: vyhladena pozicia, aktualna uroven harmonickych a tabulka (pri zmene sa v bloku prelinaju)
            float wtPos = -1; int wtMip = -1, wtTab = -1;
            // sampel
            const PianoBank::Sample* smp = nullptr; UserSamplePtr user; SmpEdit ued; float human = 0; double pos = 0, played = 0, scanPh = 0, lph = 0, o0 = 0, o1 = 0; float scanRnd = 0, xw = 1.0f; int scanStep = -1; bool inLoop = false; float gain = 1, level = 1, dampCoef = 1, atk = 1, atkInc = 1; bool damping = false, noDamper = false;
            float velGain = 1, peak = 0;
        };
        struct Voice
        {
            int note = -1; float vel = 0; bool held = false, sustained = false; uint64_t age = 0;
            float curNote = 60, targetNote = 60, drift = 0, driftTarget = 0;
            Noise rng;                     // vlastny generator hlasu (deterministicky seed v Synth::prepare)
            float mmRand = 0;              // zdroj "Nahoda" modulacnej matice (novy pri kazdom tone)
            float press = 0;               // tlak na pad/klaves (polyfonny aftertouch) - jasnejsie a vibrato
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
        float dcX[2] {}, dcY[2] {};
        std::shared_ptr<PianoBank> bank, bank2; std::atomic<PianoBank*> bank2Raw { nullptr };
        std::shared_ptr<InstrBank> ibank;
        UserSamplePtr userSmp[2];
        SmpEdit smpEd[2];
        juce::SpinLock smpLock;
        Settings st;                       // nastavenia aktualneho bloku
        double sr = 48000;
        uint64_t ageCounter = 0;
        bool sustain = false, fadeRequest = false;
        int tailState = 0; float tailG = 1.0f; uint64_t fadeAge = 0;   // prepnutie zvuku: 1 = stlmovanie, 2 = nabeh
        float bend = 0, modWheel = 0, chanPress = 0;   // chanPress = tlak (channel aftertouch)
        int lastNote = -1;
        std::vector<int> monoStack;
        std::vector<float> bufL, bufR, bufBL, bufBR, gateBuf;
        double gtFree = 0; float gtR = 0, curRhythm = 0;   // rytmicky gater: volny beh, vyhladeny vzor
    public:
        // vocoder: spev zo sidechainu (nastavuje procesor pred process(); nullptr = ziadny hlas)
        const float* vocInL = nullptr; const float* vocInR = nullptr; int vocInN = 0;
        std::atomic<float> vocLevel { 0.0f };   // uroven hlasu pre ukazovatel v okne
    private:
        korgus::KorgusVocoder vocoder; korgus::VoxPrep voxPrep; korgus::DeEsser deEss; Eq3 vcEq; std::vector<float> voxBuf; bool vocWasOn = false;
        Eq8 layerEq[2];
        FxChain fx;
        Noise noise;
        std::atomic<int> active { 0 };
        float bright = 0, atkScale = 1, relScale = 1;
        // arpeggiator
        std::vector<int> arpHeld, arpVel, physDown; std::vector<int> arpPlaying; float arpLastVel = 0.8f;
        double beat = 0, stepStartBeat = 0; int64_t lastStepIndex = -1; int arpIndex = 0; bool arpRunning = false, arpWasOn = false, arpLatchedRelease = false;
    };
}
