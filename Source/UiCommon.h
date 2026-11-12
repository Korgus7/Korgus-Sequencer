#pragma once
#include <juce_audio_utils/juce_audio_utils.h>
#include <juce_audio_processors/juce_audio_processors.h>

juce::String sk (const char* utf8);

namespace Col
{
    extern const juce::Colour steel, panel, panelLo, button, border, text, lcdBg, lcdFg;
}

// Vzhlad: tlmene ocelove tony, minimalne zaoblenie
class KorgusLookAndFeel : public juce::LookAndFeel_V4
{
public:
    KorgusLookAndFeel();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& bg, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
};

// Okno s GUI pluginu - vzdy cele na obrazovke, ak je plugin vacsi, da sa posuvat
class PluginWindow : public juce::DocumentWindow
{
public:
    // scale = velkost okna pluginu (1.0 = 100 %), onScale = ulozenie zvolenej velkosti
    PluginWindow (juce::AudioProcessor& p, const juce::String& title, std::function<void()> onClose,
                  float scale = 1.0f, std::function<void (float)> onScale = {});
    ~PluginWindow() override;
    void closeButtonPressed() override { if (onCloseFn) onCloseFn(); }
    bool keyPressed (const juce::KeyPress& k) override
    {
        if (k == juce::KeyPress::escapeKey) { closeButtonPressed(); return true; }
        if (k == juce::KeyPress::spaceKey && hostKey) { hostKey (k); return true; }   // play/stop aj z okna pluginu
        return DocumentWindow::keyPressed (k);
    }
    static inline std::function<void (const juce::KeyPress&)> hostKey;
private:
    std::function<void()> onCloseFn;
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
