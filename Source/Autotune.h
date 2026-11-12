#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>

// Doladenie spevu (autotune).
// key: -1 = automaticky, 0..11 = C..H; scale: -1 = automaticky, 0 = dur, 1 = mol, 2 = chromaticka
// speed: 0 = prirodzene, 1 = stredne, 2 = robot
struct AutotuneResult
{
    int key = 0, scale = 0;   // pouzita tonina
    bool ok = false;
};

AutotuneResult autotune (const juce::AudioBuffer<float>& in, double sr, int key, int scale, int speed,
                         juce::AudioBuffer<float>& out, std::function<bool (double)> progress = {});
