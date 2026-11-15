#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <functional>

// "Nauc sa zvuk": Korgus Inspire napodobni zvuk ineho nastroja (napr. Omnisphere)
// - zdrojovy plugin zahra niekolko tonov, Inspire hlada nastavenie, pri ktorom znie co najpodobnejsie
// - volitelne doplni do vrstvy B sampel zdroja, ak syntéza sama nestaci
namespace SoundMatch
{
    constexpr double kSR = 32000.0;   // vzorkovacia frekvencia pri hladani (setri vykon)
    constexpr int kBS = 256;

    struct Result
    {
        bool ok = false;
        juce::String error;
        juce::MemoryBlock state;      // novy stav Inspire
        float startLoss = 0, synthLoss = 0, finalLoss = 0;
        bool usedSample = false;
        int evals = 0;
        juce::String diag;            // priecinok so zaznamom ucenia (diagnostika)
    };

    // progress (0..1, sprava) vracia false = zrusit
    using Progress = std::function<bool (double, const juce::String&)>;

    // priecinok diagnostiky: Dokumenty\Korgus Sequencer\Ucenie
    juce::File diagFolder();

    // src a insp musia byt pripravene (prepareToPlay); zdroj sa sam znova pripravi na 48 kHz (na hlavnom vlakne); vola sa na pozadi
    Result run (juce::AudioPluginInstance& src, juce::AudioPluginInstance& insp,
                const juce::MemoryBlock& inspState, bool allowSample, const juce::String& srcName, Progress progress);
}
