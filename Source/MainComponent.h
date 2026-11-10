#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

// Vzhlad: tlmene ocelove tony, minimalne zaoblenie
class KorgusLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KorgusLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

// Jednoduchy testovaci zvuk, kym nie je nacitany VST nastroj
class TestSynthProcessor : public juce::AudioProcessor
{
public:
    TestSynthProcessor();
    const juce::String getName() const override { return "Testovací tón"; }
    void prepareToPlay (double sr, int) override { synth.setCurrentPlaybackSampleRate (sr); }
    void releaseResources() override {}
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    double getTailLengthSeconds() const override { return 0.3; }
    bool acceptsMidi() const override  { return true; }
    bool producesMidi() const override { return false; }
    juce::AudioProcessorEditor* createEditor() override { return nullptr; }
    bool hasEditor() const override { return false; }
    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock&) override {}
    void setStateInformation (const void*, int) override {}
private:
    juce::Synthesiser synth;
};

// Okno s GUI pluginu - vzdy cele na obrazovke, ak je plugin vacsi, da sa posuvat
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& p, std::function<void()> onClose);
    ~PluginWindow() override;
    void closeButtonPressed() override { if (onCloseFn) onCloseFn(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
        return DocumentWindow::keyPressed (k);
    }
private:
    std::function<void()> onCloseFn;
    std::unique_ptr<juce::Component> editorHolder;
};

// Okno so zoznamom pluginov a nastavenim priecinkov na hladanie
class PluginManagerWindow : public juce::DocumentWindow
{
public:
    PluginManagerWindow (juce::AudioPluginFormatManager&, juce::KnownPluginList&, const juce::File& deadMansPedal,
                         juce::PropertiesFile*, std::function<void()> onClose);
    void closeButtonPressed() override { if (onCloseFn) onCloseFn(); }
private:
    std::function<void()> onCloseFn;
};

class MainComponent : public juce::Component,
                      private juce::ListBoxModel,
                      private juce::MidiKeyboardState::Listener,
                      private juce::ChangeListener,
                      private juce::Timer
{
public:
    explicit MainComponent (juce::ApplicationProperties&);
    ~MainComponent() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void handleNoteOn (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void handleNoteOff (juce::MidiKeyboardState*, int ch, int note, float vel) override;
    void changeListenerCallback (juce::ChangeBroadcaster*) override;
    void timerCallback() override;

    int getNumRows() override;
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;

    void refreshPluginList();
    void showPluginManager();
    void chooseAndLoadPlugin();
    bool loadPluginFile (const juce::File&, juce::String& error);
    bool loadPluginDescription (const juce::PluginDescription&, juce::String& error);
    void unloadPlugin();
    void setProcessor (std::unique_ptr<juce::AudioProcessor>);
    void showPluginWindow();
    void saveSettings();
    void updateInfo();

    juce::ApplicationProperties& props;
    KorgusLookAndFeel lnf;

    juce::AudioDeviceManager deviceManager;
    juce::AudioProcessorPlayer player;
    juce::AudioPluginFormatManager formatManager;
    juce::KnownPluginList knownPlugins;
    juce::Array<juce::PluginDescription> pluginRows;
    juce::ListBox pluginList { "pluginy", this };
    std::unique_ptr<PluginManagerWindow> managerWindow;
    std::unique_ptr<juce::PluginDescription> currentDesc;
    std::unique_ptr<juce::AudioProcessor> processor;
    std::unique_ptr<PluginWindow> pluginWindow;
    juce::File pluginFile;
    bool ready = false;

    std::unique_ptr<juce::AudioDeviceSelectorComponent> deviceSelector;
    juce::MidiKeyboardState keyboardState;
    juce::MidiKeyboardComponent keyboard { keyboardState, juce::MidiKeyboardComponent::horizontalKeyboard };

    juce::Label title, pluginLabel, infoLabel, hintLabel, listLabel;
    juce::TextButton managerButton { juce::String::fromUTF8 ("Pluginy a priečinky…") },
                     loadButton { juce::String::fromUTF8 ("Zo súboru…") },
                     editorButton { juce::String::fromUTF8 ("Okno pluginu") },
                     unloadButton { juce::String::fromUTF8 ("Odstrániť plugin") };
    std::unique_ptr<juce::FileChooser> chooser;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MainComponent)
};
