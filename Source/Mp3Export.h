#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>
#include <functional>

// Hlasitost podla ITU-R BS.1770 (LUFS, integrovana)
double measureLufs (const juce::AudioBuffer<float>& b, double sr);

// Dorovnanie na cielovu hlasitost (LUFS) + limiter na strop (dBFS). targetLufs >= 0 = bez zmeny hlasitosti.
// Vrati pouzity zisk v dB.
double normaliseLoudness (juce::AudioBuffer<float>& b, double sr, double targetLufs, double ceilingDb);

// MP3 (MPEG-1 Layer III, enkoder shine). kbps: 128..320
bool writeMp3 (const juce::AudioBuffer<float>& b, double sr, int kbps, const juce::File& out, juce::String& error,
               const std::function<bool (double)>& progress = {});
