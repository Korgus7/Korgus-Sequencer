#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// Oddelenie spevu od hudby modelom MDX-Net (ONNX), ONNX Runtime sa nacita az pri pouziti.
// in44: stereo zvuk so vzorkovanim 44,1 kHz; vocals: vysledny spev (rovnaka dlzka).
// progress (0..1) vrati false = zrusit.
bool separateVocals (const juce::File& ortLibrary, const juce::File& model,
                     const juce::AudioBuffer<float>& in44, juce::AudioBuffer<float>& vocals,
                     juce::String& error, const std::function<bool (double)>& progress);

// vseobecny model MDX-Net (spev, bicie, bas...) - vystup = predpovedana zlozka
struct MdxParams { int nFft = 7680, dimF = 3072, dimT = 256; float compensate = 1.009f; };
bool separateMdx (const juce::File& ortLibrary, const juce::File& model, const MdxParams& params,
                  const juce::AudioBuffer<float>& in44, juce::AudioBuffer<float>& out,
                  juce::String& error, const std::function<bool (double)>& progress);

// stiahnutie suboru s priebehom (v pracovnom vlakne)
bool downloadFile (const juce::String& url, const juce::File& dest, juce::String& error,
                   const std::function<bool (double)>& progress);
