#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>

// Rozpoznana nota z nahravky hlasu / piskania
struct MelNote
{
    double start = 0, end = 0;   // sekundy od zaciatku useku
    int pitch = 60;
    float vel = 0.8f;
};

// Jednohlasna melodia z audia (YIN): usek [off, off + dur] sekund v buffri so vzorkovanim sr
std::vector<MelNote> detectMelody (const juce::AudioBuffer<float>& buf, double sr, double off, double dur);
