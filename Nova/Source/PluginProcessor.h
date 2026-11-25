#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Synth.h"
#include "Params.h"
#include "PresetStore.h"

// ---- Inspiracia: sledovanie hrania (MIDI) a vytiahnutie poslednej frazy
namespace nova
{
    struct CapEvent { double t = 0, bpm = 0, ppq = -1; juce::uint8 kind = 0, note = 0, vel = 0; };   // kind: 1 zapnutie, 2 vypnutie, 3 pedal (vel >= 64 = stlaceny)
    struct CapNote { double t = 0, d = 0; int k = 60, v = 100; double bpm = 0, ppq = -1; };            // t = cas zaciatku (s), d = dlzka (s)
    struct CapPhrase
    {
        std::vector<CapNote> notes;    // t relativne k prvej note (prva ma t = 0)
        double bpm = 0, ppq = -1;      // tempo hostitela a poloha prvej noty v takte (ppq), ak hostitel bezal; inak bpm 0 / ppq -1
        double length() const { double e = 0; for (auto& n : notes) e = juce::jmax (e, n.t + n.d); return e; }
    };

    // kruhovy zasobnik udalosti (bez alokacii v processBlock)
    class CaptureBuffer
    {
    public:
        static constexpr int capacity = 20000;
        static constexpr double keepSeconds = 600.0;
        CaptureBuffer() : ev ((size_t) capacity) {}
        void push (const CapEvent& e) { const juce::SpinLock::ScopedLockType l (lock); ev[(size_t) head] = e; head = (head + 1) % capacity; if (count < capacity) ++count; }
        void clear() { const juce::SpinLock::ScopedLockType l (lock); head = 0; count = 0; }
        std::vector<CapEvent> snapshot() const
        {
            const juce::SpinLock::ScopedLockType l (lock);
            std::vector<CapEvent> out; out.reserve ((size_t) count);
            for (int i = 0; i < count; ++i) out.push_back (ev[(size_t) ((head - count + i + capacity * 2) % capacity)]);
            return out;
        }
        // zo zoznamu udalosti (podla casu) urobi noty; `now` ukonci drzane noty
        static std::vector<CapNote> pairNotes (const std::vector<CapEvent>& events, double now);
        // posledna fraza: konci poslednym vypnutim tonu, zacina po poslednej pauze > gapSec (aspon 8 not), max 3 minuty
        static CapPhrase extractPhrase (const std::vector<CapEvent>& events, double now, double gapSec = 4.0);
    private:
        mutable juce::SpinLock lock;
        std::vector<CapEvent> ev; int head = 0, count = 0;
    };

    // uprava frazy: odstrani nahodne tony, zarovna zaciatky na 1/16 (50 %)
    std::vector<CapNote> cleanupPhrase (const std::vector<CapNote>& notes, double bpm, bool quantize);
    bool writePhraseMidi (const CapPhrase& ph, const juce::File& f);
    // offline vykreslenie frazy so zvukom podla `settings` (48 kHz, stereo, 24 bit, + dozvuk)
    bool renderPhraseWav (const CapPhrase& ph, const Settings& settings, const std::array<UserSamplePtr, 2>& samples, const juce::File& f, double tailSeconds = 3.0);
}

class NovaProcessor : public juce::AudioProcessor,
                      private juce::AudioProcessorValueTreeState::Listener,
                      private juce::AsyncUpdater,
                      private juce::Timer
{
public:
    NovaProcessor();
    ~NovaProcessor() override;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Korgus Inspiria"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // --- pre rozhranie
    juce::UndoManager undo;
    juce::AudioProcessorValueTreeState params;
    nova::Synth synth;
    std::vector<float> scL, scR;   // hlas zo sidechainu (vocoder)
    nova::PresetStore presets;
    juce::MidiKeyboardState keyboard;
    // ukazka zvuku pri vybere zo zoznamu (kratka fraza cez klaviaturu)
    void preview();
    // citlivost kláves: 0 jemna (aj slabsi uder znie silno), 1 normalna, 2 tvrda; nastavenie pouzivatela, nie zvuku
    std::atomic<int> velCurve { 1 };
    void setVelCurve (int c) { velCurve = juce::jlimit (0, 2, c); presets.setSetting ("velcurve", juce::String (velCurve.load())); }
    static void previewWait (NovaProcessor*, std::shared_ptr<std::atomic<bool>>, int, std::function<void()>, int);
    bool previewOn() { return presets.setting ("preview") != "0"; }
    std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>> (true);
    std::atomic<int> previewGen { 0 };

    bool loadPreset (const juce::String& key);            // zo zoznamu (tovarensky alebo vlastny)
    juce::String savePreset (const juce::String& name, const juce::String& category);
    nova::PatchData currentPatch();                      // aktualne hodnoty ako zvuk
    void applyPatch (const nova::PatchData& p);
    bool loadSampleFile (int layer, const juce::File& f, juce::String& error);
    static float detectPitch (const juce::AudioBuffer<float>& b, double sr);   // MIDI nota (desatinne), 0 = nie je ton
    static juce::String encodeSmpEdit (const nova::SmpEdit& e);
    static nova::SmpEdit decodeSmpEdit (const juce::String& t);
    // predchadzajuci / dalsi zvuk z toho isteho priecinka (prehadzovac samplov); vrati false, ak nie je odkial
    bool stepSample (int layer, int dir, juce::String& error);
    // zabudovane vzorky nastrojov
    static int builtinSampleCount();
    static juce::String builtinSampleName (int i);
    bool loadBuiltinSample (int layer, int idx);
    int builtinSample (int layer) const { return smpBuiltin[layer & 1]; }
    void setSourceMode (int layer, int mode);   // 0 klavir, 1 WAV, 2 vzorky (pri prepnuti vrati posledny zvuk daneho druhu)
    juce::File sampleFile (int layer) const { return smpFile[layer & 1]; }
    void copyLayer (int from, int to);   // skopiruje celu vrstvu (oscilator, filter, obalky, LFO, sampel)
    juce::String sampleName (int layer) const;
    void surprise (int type = -1, bool keepLevel = false);   // keepLevel: rovnaka hlasitost ako doterajsi zvuk                       // "Prekvap ma": novy nahodny zvuk (typ -1 = cokolvek)
    void mutate();                                       // "Obmena": jemne pozmeni aktualny zvuk
    static const juce::StringArray& surpriseTypes();     // nazvy typov pre vyber (index = typ)
    void newSound (const juce::String& category);        // cisty zakladny zvuk na vlastnu tvorbu
    static float measureLevel (const nova::PatchData& p); // hlasitost zvuku (dB) pri p_gain 0
    static void settingsFrom (nova::Settings& s, const std::function<float (const juce::String&)>& g);

    // ukazka zvuku v prehliadaci WAV (hra cez vystup pluginu)
    void previewFile (const juce::File& f);
    void stopPreview();
    bool previewPlaying() const { return prevPlaying.load(); }

    // MIDI learn: gombik <- CC z klaviatury (plati vo vsetkych projektoch)
    void midiLearn (const juce::String& paramId);
    void midiForget (const juce::String& paramId);
    int ccFor (const juce::String& paramId) const;
    bool isLearning (const juce::String& paramId) const;
    std::atomic<int> ccVersion { 0 };   // zvysi sa pri kazdej zmene priradeni (rozhranie prekresli)
    void applyCc();                     // (volane casovacom na hlavnom vlakne)
    void flushPending() { handleUpdateNowIfNeeded(); }

    // Inspiracia
    nova::CapPhrase lastPhrase() const;                  // posledna zahrana fraza (prazdna = nic nezahral)
    std::vector<nova::CapNote> capturedNotes (double& now) const;   // vsetko zahrane za poslednych 10 minut (absolutne casy)
    int lastPhraseCount() const;
    void clearCapture() { capBuf.clear(); }                // Inspiracia od zaciatku: zabudne doterajsie hranie                         // kolko not ma posledna fraza (su na konci capturedNotes)
    void publishCapture (const nova::CapPhrase& ph);
    // "Nauc sa zvuk": v Korgus Sequenceri poziada hostitela o okno ucenia; inde vrati false
    bool requestLearn();
    static bool inKorgusSequencer();     // preda frazu hostitelovi (Korgus Sequencer) cez stav + parameter capture_seq
    nova::Settings currentSettings();                    // aktualny zvuk ako nastavenia syntetizatora (hlavne vlakno)
    static bool hostedInKorgus();
    nova::CaptureBuffer capBuf;

    juce::String currentKey, currentName, currentCategory;
    int uiMode = 0;          // 0 jednoducha, 1 rozsirena
    float uiScale = 1.0f;
    std::function<void()> onPatchChanged;                // rozhranie obnovi nazov zvuku

private:
    void parameterChanged (const juce::String& id, float v) override;
    void handleAsyncUpdate() override;
    void timerCallback() override { stopTimer(); undo.clearUndoHistory(); }   // po prvom zapise parametrov do stavu
    void fillSettings (nova::Settings& s);
    void setSample (int layer, const juce::MemoryBlock& flac, const juce::String& name);
    std::map<juce::String, std::atomic<float>*> raw;
    std::atomic<float>* rp (const char* id) { return raw[id]; }
    juce::MemoryBlock smpFlac[2]; juce::String smpName[2];
    juce::File smpFile[2];   // odkial bol zvuk nacitany (pre prehadzovac)
    int smpBuiltin[2] { -1, -1 };   // cislo zabudovanej vzorky (-1 = vlastny zvuk)
    int lastBuiltin[2] { 0, 0 };    // posledna zvolena vzorka
    juce::File lastWav[2];          // posledny nacitany WAV
    std::atomic<int> pendingPreset { -1 };
    std::atomic<int> pendingAction { 0 };   // 1 = prekvap ma, 2 = obmena (zo Sequencera)
    // ukazka
    juce::SpinLock prevLock; std::shared_ptr<juce::AudioBuffer<float>> prevBuf; double prevRate = 44100, prevPos = 0, curRate = 48000;
    std::atomic<bool> prevPlaying { false }; std::atomic<int> prevReq { 0 };
    // MIDI learn
    std::atomic<int> ccMap[128]; std::atomic<int> learnIdx { -1 };
    juce::AbstractFifo ccFifo { 256 }; int ccFifoIdx[256] {}; float ccFifoVal[256] {};
    struct CcApplier : juce::Timer { NovaProcessor& p; CcApplier (NovaProcessor& pr) : p (pr) {} void timerCallback() override { p.applyCc(); } } ccApplier { *this };
    void saveCcMap();
    void loadCcMap();
    std::atomic<bool> ccDirty { false };
    bool restoring = false;
    int lastPresetParam = 0;
    // Inspiracia
    std::atomic<juce::int64> capSamples { 0 };
    juce::CriticalSection capLock; nova::CapPhrase capPhrase; int capId = 0, capCounter = 0;
    void recordCapture (const juce::MidiBuffer& midi, int numSamples);
};
