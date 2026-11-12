#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <functional>
#include <vector>

// Analyza nahravky: doby a tempo, akordy, udery bicich. Casy su v sekundach od zaciatku bufferu.
struct AnalysisResult
{
    double bpm = 0;
    std::vector<double> beats;      // casy dob
    int downbeat = 0;               // index prvej doby v takte (prva "1")
    bool steady = true;             // tempo je stale (inak je vhodna tempova mapa)
    struct Chord { double t; juce::String name; };
    std::vector<Chord> chords;      // zmeny akordov
    struct Hit { double t; float v; };
    std::vector<Hit> kick, snare, hat;
    bool ok = false;
};

// what: bity 1 = doby, 2 = akordy, 4 = bicie (doby sa pocitaju vzdy)
AnalysisResult analyzeAudio (const juce::AudioBuffer<float>& in, double sr, int what,
                             const std::function<bool (double)>& progress = {});
