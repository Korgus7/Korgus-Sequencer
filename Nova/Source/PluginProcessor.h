#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "Synth.h"
#include "Params.h"
#include "PresetStore.h"

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

    const juce::String getName() const override { return "Korgus Inspire"; }
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
    nova::PresetStore presets;
    juce::MidiKeyboardState keyboard;

    bool loadPreset (const juce::String& key);            // zo zoznamu (tovarensky alebo vlastny)
    juce::String savePreset (const juce::String& name, const juce::String& category);
    nova::PatchData currentPatch();                      // aktualne hodnoty ako zvuk
    void applyPatch (const nova::PatchData& p);
    bool loadSampleFile (int layer, const juce::File& f, juce::String& error);
    juce::String sampleName (int layer) const;
    void surprise (int type = -1);                       // "Prekvap ma": novy nahodny zvuk (typ -1 = cokolvek)
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
};
