#pragma once
#include <functional>
#include <vector>
#include <juce_audio_processors/juce_audio_processors.h>

// Vstavané efekty Korgus (EQ, kompresor, dozvuk, ozvena, limiter).
namespace BuiltinFx
{
    inline const juce::String formatName { "Korgus" };
    // ak je nastavene, sampler vybera subor cez prehliadac programu (namiesto okna Windows)
    inline std::function<void (std::function<void (const juce::File&)>)> requestFile;

    juce::Array<juce::PluginDescription> descriptions();
    bool isBuiltin (const juce::PluginDescription&);
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&);
    // kluc pre sidechain (kompresor, PulseDip); nullptr = bez kluca
    void setSidechain (juce::AudioPluginInstance*, const float* left, const float* right, int n);
    // tonina a akordy piesne (Korgus Vocoder: Robot a Interny syntezator); chords moze byt nullptr
    struct ChordSpan { double step = 0; std::vector<int> notes; };   // od kroku (1/16) znie akord s tymito MIDI notami
    void setSong (juce::AudioPluginInstance*, int keyTonic, int keyScale, const std::vector<ChordSpan>* chords);
    // nacitana / nova pieseň (Korgus Master zabudne naucenu hlasitost) - volat z hlavneho vlakna
    void newSong (juce::AudioPluginInstance*);
    // export (offline): efekt, ktory potrebuje predbezny prechod (Korgus Master), a faza 0 = normal, 1 = ucenie, 2 = zaverecny prechod
    bool wantsExportPrepass (juce::AudioPluginInstance*);
    void setExportPhase (juce::AudioPluginInstance*, int phase);
    // arpeggiator (MIDI efekt pred nastrojom)
    juce::PluginDescription arpDescription();
}
