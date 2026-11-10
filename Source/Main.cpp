#include <juce_gui_basics/juce_gui_basics.h>
#include "MainComponent.h"

class KorgusApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        // slovenske texty pre standardne JUCE ovladacie prvky (vyber zvukovej karty a pod.)
        juce::LocalisedStrings::setCurrentMappings (new juce::LocalisedStrings (juce::String::fromUTF8 (
            "language: Slovak\n"
            "\"Audio device type:\" = \"Typ zariadenia:\"\n"
            "\"Output:\" = \"Výstup:\"\n"
            "\"Input:\" = \"Vstup:\"\n"
            "\"Device:\" = \"Zariadenie:\"\n"
            "\"Sample rate:\" = \"Vzorkovanie:\"\n"
            "\"Audio buffer size:\" = \"Veľkosť bufferu:\"\n"
            "\"Active output channels:\" = \"Výstupné kanály:\"\n"
            "\"Active input channels:\" = \"Vstupné kanály:\"\n"
            "\"Active MIDI inputs:\" = \"MIDI vstupy:\"\n"
            "\"MIDI Output:\" = \"MIDI výstup:\"\n"
            "\"Control Panel\" = \"Ovládací panel\"\n"
            "\"Reset Device\" = \"Reštartovať zariadenie\"\n"
            "\"Test\" = \"Test\"\n"
            "\"samples\" = \"vzoriek\"\n"
            "\"<< none >>\" = \"<< žiadne >>\"\n"
            "\"(no audio output channels found)\" = \"(žiadne výstupné kanály)\"\n"
            "\"(no MIDI inputs available)\" = \"(žiadne MIDI vstupy)\"\n"), false));

        juce::PropertiesFile::Options o;
        o.applicationName     = "Korgus Sequencer";
        o.filenameSuffix      = ".settings";
        o.folderName          = "Korgus Sequencer";
        o.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (o);

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), properties);
    }

    void shutdown() override { mainWindow = nullptr; }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::ApplicationProperties& props)
            : DocumentWindow (name, juce::Colour (0xff2d4362), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainComponent (props), true);
            setResizable (true, true);
            setResizeLimits (900, 600, 4000, 3000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
    };

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (KorgusApp)
