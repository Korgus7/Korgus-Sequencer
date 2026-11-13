#include <juce_gui_basics/juce_gui_basics.h>
#include "MainView.h"
#include "UiCommon.h"
#include "CrashGuard.h"

class KorgusApp : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override    { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override          { return false; }

    void initialise (const juce::String&) override
    {
        juce::PropertiesFile::Options o;
        o.applicationName     = "Korgus Sequencer";
        o.filenameSuffix      = ".settings";
        o.folderName          = "Korgus Sequencer";
        o.osxLibrarySubFolder = "Application Support";
        properties.setStorageParameters (o);

        // jazyk: ulozena volba, inak podla jazyka Windows
        {
            auto lang = properties.getUserSettings()->getValue ("lang");
            if (lang.isEmpty())
            {
                const auto ul = juce::SystemStats::getUserLanguage().toLowerCase();
                lang = (ul.startsWith ("sk") || ul.startsWith ("cs")) ? "sk" : "en";
            }
            appLang() = lang == "en" ? 1 : 0;
        }

        // slovenske texty pre standardne JUCE ovladacie prvky (vyber zvukovej karty a pod.)
        if (appLang() == 0)
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
            "\"(no MIDI inputs available)\" = \"(žiadne MIDI vstupy)\"\n"
            "\"Options...\" = \"Možnosti…\"\n"
            "\"Clear list\" = \"Vymazať zoznam\"\n"
            "\"Remove selected plug-in from list\" = \"Odstrániť vybraný plugin zo zoznamu\"\n"
            "\"Remove plug-in from list\" = \"Odstrániť plugin zo zoznamu\"\n"
            "\"Show folder containing selected plug-in\" = \"Otvoriť priečinok s vybraným pluginom\"\n"
            "\"Show folder containing plug-in\" = \"Otvoriť priečinok s pluginom\"\n"
            "\"Remove any plug-ins whose files no longer exist\" = \"Odstrániť pluginy, ktorých súbory už neexistujú\"\n"
            "\"Scan for new or updated XFMTX plug-ins\" = \"Hľadať nové alebo zmenené XFMTX pluginy\"\n"
            "\"Remove all XFMTX plug-ins\" = \"Odstrániť všetky XFMTX pluginy\"\n"
            "\"Select folders to scan...\" = \"Vyber priečinky, v ktorých hľadať pluginy…\"\n"
            "\"Scan\" = \"Hľadať\"\n"
            "\"Cancel\" = \"Zrušiť\"\n"
            "\"Scanning for plug-ins...\" = \"Hľadám pluginy…\"\n"
            "\"Searching for all possible plug-in files...\" = \"Hľadám súbory pluginov…\"\n"
            "\"Plugin Scanning\" = \"Hľadanie pluginov\"\n"
            "\"Scan complete\" = \"Hľadanie dokončené\"\n"
            "\"Testing\" = \"Testujem\"\n"
            "\"Name\" = \"Názov\"\n"
            "\"Format\" = \"Formát\"\n"
            "\"Category\" = \"Kategória\"\n"
            "\"Manufacturer\" = \"Výrobca\"\n"
            "\"Description\" = \"Popis\"\n"
            "\"Deactivated after failing to initialise correctly\" = \"Vypnutý – nepodarilo sa ho spustiť\"\n"
            ), false));

        mainWindow = std::make_unique<MainWindow> (getApplicationName(), properties);
    }

    void shutdown() override { mainWindow = nullptr; CrashGuard::cleanExit(); }
    void systemRequestedQuit() override { quit(); }

    class MainWindow : public juce::DocumentWindow
    {
    public:
        MainWindow (const juce::String& name, juce::ApplicationProperties& props)
            : DocumentWindow (name, juce::Colour (0xff2d4362), DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar (true);
            setContentOwned (new MainView (props), true);
            setResizable (true, true);
            setResizeLimits (1000, 640, 6000, 4000);
            centreWithSize (getWidth(), getHeight());
            setVisible (true);
        }
        void closeButtonPressed() override { juce::JUCEApplication::getInstance()->systemRequestedQuit(); }
        void activeWindowStatusChanged() override
        {
            // po navrate z okna pluginu / ineho programu dostane klavesnicu hlavne okno (medzernik = play)
            if (isActiveWindow())
                if (auto* mv = dynamic_cast<MainView*> (getContentComponent()))
                {
                    juce::Component::SafePointer<MainView> sp (mv);
                    juce::MessageManager::callAsync ([sp] { if (sp != nullptr) sp->focusWeb(); });
                }
        }
    };

private:
    juce::ApplicationProperties properties;
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION (KorgusApp)
