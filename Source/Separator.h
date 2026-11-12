#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// Oddelenie spevu od hudby modelom MDX-Net (ONNX), ONNX Runtime sa nacita az pri pouziti.
// in44: stereo zvuk so vzorkovanim 44,1 kHz; vocals: vysledny spev (rovnaka dlzka).
// progress (0..1) vrati false = zrusit.
bool separateVocals (const juce::File& ortLibrary, const juce::File& model,
                     const juce::AudioBuffer<float>& in44, juce::AudioBuffer<float>& vocals,
                     juce::String& error, const std::function<bool (double)>& progress);

// stiahnutie suboru s priebehom (v pracovnom vlakne)
bool downloadFile (const juce::String& url, const juce::File& dest, juce::String& error,
                   const std::function<bool (double)>& progress);
