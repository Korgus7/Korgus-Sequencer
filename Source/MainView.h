#pragma once
#include <set>
#include "UiCommon.h"
#include "Engine.h"
#include <juce_gui_extra/juce_gui_extra.h>

// Okno s nastavenim zvuku (zvukova karta, ASIO, MIDI vstupy, latencia)
class SettingsComponent : public juce::Component, private juce::Timer
{
public:
    SettingsComponent (juce::AudioDeviceManager&, std::function<void()> onPlugins);
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    juce::AudioDeviceManager& dm;
    juce::AudioDeviceSelectorComponent selector;
    juce::Label info, hint;
    juce::TextButton pluginsButton;
    std::function<void()> onPluginsFn;
};

// Hlavne okno: rozhranie sekvencera (HTML) + prepojenie so zvukovym motorom
juce::var readMidiFile (const juce::File&);

class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
    // klavesnica do WebView2 (F3, medzernik...) - aj ked ju JUCE uz ma, MoveFocus sa zavola znova
    void focusWeb()
    {
        if (web == nullptr || ! web->isShowing()) return;
        if (web->hasKeyboardFocus (true)) web->giveAwayKeyboardFocus();
        web->grabKeyboardFocus();
    }
    explicit MainView (juce::ApplicationProperties&);
    ~MainView() override;
    void resized() override;

private:
    juce::WebBrowserComponent::Options makeOptions();
    std::optional<juce::WebBrowserComponent::Resource> getResource (const juce::String& url);
    void emit (const juce::String& id, const juce::var& v);

    // udalosti z rozhrania
    void onSong (const juce::var&);
    void onTransport (const juce::var&);
    void onNote (const juce::var&);
    void onTrackPlugin (const juce::var&);
    void onTrackFx (const juce::var&);
    void onOpenFx (const juce::var&);
    void onSelTrack (const juce::var&);
    void sendDevices();
    std::unique_ptr<juce::AudioPluginInstance> createInstance (const juce::PluginDescription&, const juce::MemoryBlock* state, bool instrument);
    bool loadFxForTrack (const juce::String& trackId, int idx, const juce::PluginDescription&, const juce::MemoryBlock* state);
    void openWindow (const juce::String& key, juce::AudioPluginInstance*, const juce::String& title);
    void onOpenPlugin (const juce::var&);
    void onSaveFile (const juce::var&);
    void onOpenSong (const juce::var&);
    void onReady();
    // sledovanie pohybu gombikov v pluginoch (nahravanie automatizacie)
    struct ParamWatch : juce::AudioProcessorListener
    {
        ParamWatch (MainView& m, juce::String t, int w, juce::AudioProcessor* p) : mv (m), track (std::move (t)), which (w), proc (p) {}
        void audioProcessorParameterChanged (juce::AudioProcessor*, int i, float v) override { mv.paramTouched (track, which, i, v, 0); }
        void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override {}
        void audioProcessorParameterChangeGestureBegin (juce::AudioProcessor*, int i) override { mv.paramTouched (track, which, i, -1, 1); }
        void audioProcessorParameterChangeGestureEnd (juce::AudioProcessor*, int i) override { mv.paramTouched (track, which, i, -1, 2); }
        MainView& mv; juce::String track; int which; juce::AudioProcessor* proc;
    };
    void paramTouched (const juce::String& track, int which, int index, float v, int kind);
    void watchPlugin (const juce::String& track, int which, juce::AudioProcessor* p);
    void unwatchPlugin (const juce::String& track, int which);
    std::map<juce::String, std::unique_ptr<ParamWatch>> watches;
    std::atomic<bool> autoRecOn { false };
    void runBootStep();
    void bootStatus (const juce::String& text, double progress);
    void onAiRequest (const juce::var&);
    void sendAiState();
    void onImportAudio (const juce::var&);
    void onAudioChunk (const juce::var&);
    void onNeedPeaks (const juce::var&);
    void onRecAudio (const juce::var&);
    void onExportWav (const juce::var&);
    void onExportStems (const juce::var&);
    void onBounce (const juce::var&);
    void sendMeters();
    bool metersLive = false;
    void onExportMp3 (const juce::var&);
    void onExportVideo (const juce::var&);
    void onSeparate (const juce::var&);
    void onAutotune (const juce::var&);
    void finishImport (const juce::File&, const juce::String& track, double bar, bool deleteAfter);
    void finishRecording();
    std::unique_ptr<SongData> parseCurrent (const juce::var&);
    AudioBufPtr clipBuffer (const juce::String& id, double alpha, int semi);
    std::set<juce::String> stretchPending;

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void sendPluginList();
    juce::Array<juce::PluginDescription> allPluginTypes() const;
    void sendTrackPlugins();
    bool loadPluginForTrack (const juce::String& trackId, const juce::PluginDescription&, const juce::MemoryBlock* state);
    void showSettings();
    void showPluginManager();
    juce::var collectPluginStates();
    void restorePluginStates (const juce::var& vst);
    void saveSession();
    void loadSession();
    void saveSettings();

    juce::ApplicationProperties& props;
    KorgusLookAndFeel lnf;
    juce::AudioDeviceManager deviceManager;
    Engine engine;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::File dataDir;
    std::unique_ptr<AudioStore> store;
    juce::var lastSong;
    std::map<juce::String, juce::MemoryBlock> uploads;
    struct RecTrack { juce::String id; int a = 0, b = -1; std::vector<float> l, r; size_t pkDone = 0; };
    void sendLivePeaks();
    std::vector<RecTrack> recTracks;
    bool recOn = false, recFinishing = false;
    struct { bool on = false; double L = 0, R = 0, tempo = 120; } recLoop;   // cyklus pocas nahravania (takes)
    juce::uint32 recStopTime = 0;
    std::atomic<bool> exporting { false };

    std::unique_ptr<juce::WebBrowserComponent> web;
    std::map<juce::String, std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::DocumentWindow> settingsWindow;
    std::unique_ptr<PluginManagerWindow> managerWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    std::map<juce::String, juce::PluginDescription> trackDesc;   // ktory plugin ma ktora stopa
    std::map<juce::String, std::map<int, juce::PluginDescription>> fxDesc;   // efekty stop
    juce::var pendingSession;                                    // pluginy z minulého spustenia
    bool sessionApplied = false;
    bool booting = true, booted = false;
    std::vector<std::pair<juce::String, std::function<void()>>> bootSteps;
    size_t bootIndex = 0;
    double lastSentPos = -1;
    bool lastPlaying = false, lastCounting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
