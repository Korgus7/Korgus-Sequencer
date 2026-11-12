#pragma once
#include <juce_audio_processors/juce_audio_processors.h>

// Vstavané efekty Korgus (EQ, kompresor, dozvuk, ozvena, limiter).
namespace BuiltinFx
{
    inline const juce::String formatName { "Korgus" };

    juce::Array<juce::PluginDescription> descriptions();
    bool isBuiltin (const juce::PluginDescription&);
    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&);
    // kluc pre sidechain (kompresor, PulseDip); nullptr = bez kluca
    void setSidechain (juce::AudioPluginInstance*, const float* left, const float* right, int n);
    // arpeggiator (MIDI efekt pred nastrojom)
    juce::PluginDescription arpDescription();
}
