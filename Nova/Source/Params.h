#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <map>

// Vsetky parametre Korgus Nova a tovarenske zvuky
namespace nova
{
    struct ParamSpec
    {
        juce::String id, name;
        int kind = 0;                 // 0 float, 1 vyber, 2 zapnute/vypnute
        float lo = 0, hi = 1, def = 0, skew = 1;
        juce::StringArray choices;
        juce::String unit;            // "%", "ms", "Hz", "ct", "dB", "st", "bip" (-100..100 %), "x", ""
    };

    const std::vector<ParamSpec>& paramSpecs();
    const ParamSpec* findSpec (const juce::String& id);
    juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    juce::String formatValue (const ParamSpec& s, float v);

    // hodnoty FM pomeru pre vyber "ratio"
    float ratioValue (int index);

    struct FactoryPatch
    {
        juce::String name, category;
        std::map<juce::String, float> values;   // len odlisnosti od zakladneho zvuku
    };
    const std::vector<FactoryPatch>& factoryPatches();
    const juce::StringArray& categories();      // poradie kategorii (posledna "Moje zvuky")

    // ID parametrov, ktore patria zvuku (nie makra jednoduchej verzie)
    bool isPatchParam (const juce::String& id);
}
