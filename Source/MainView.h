#pragma once
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
class MainView : public juce::Component,
                 private juce::ChangeListener,
                 private juce::Timer
{
public:
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
    void onOpenPlugin (const juce::var&);
    void onSaveFile (const juce::var&);
    void onOpenSong (const juce::var&);
    void onReady();

    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    void sendPluginList();
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

    std::unique_ptr<juce::WebBrowserComponent> web;
    std::map<juce::String, std::unique_ptr<PluginWindow>> pluginWindows;
    std::unique_ptr<juce::DocumentWindow> settingsWindow;
    std::unique_ptr<PluginManagerWindow> managerWindow;
    std::unique_ptr<juce::FileChooser> chooser;

    std::map<juce::String, juce::PluginDescription> trackDesc;   // ktory plugin ma ktora stopa
    juce::var pendingSession;                                    // pluginy z minulého spustenia
    bool sessionApplied = false;
    double lastSentPos = -1;
    bool lastPlaying = false, lastCounting = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainView)
};
