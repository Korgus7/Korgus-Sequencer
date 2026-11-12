#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// Odsumovanie nahravky (spektralne odcitanie naucenej vzorky sumu).
// amountDb: o kolko najviac stisit sum (6 .. 30 dB)
// profStart/profLen: usek so samotnym sumom v samploch; profLen <= 0 = automaticky (najtichsie miesta)
bool denoise (const juce::AudioBuffer<float>& in, double sr, float amountDb, int profStart, int profLen,
              juce::AudioBuffer<float>& out, std::function<bool (double)> progress = {});
