#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <map>
#include <memory>

using AudioBufPtr = std::shared_ptr<const juce::AudioBuffer<float>>;

// Zvukove subory audio stop: ulozene v priecinku programu, dekodovane a prevzorkovane na frekvenciu zvukovky
// WSOLA: zmena dlzky bez zmeny vysky tonu
AudioBufPtr timeStretch (const juce::AudioBuffer<float>& in, double alpha);
// tempo (alpha) aj vyska tonu (semi = poltony) naraz
AudioBufPtr stretchAndShift (const juce::AudioBuffer<float>& in, double alpha, int semi);

class AudioStore
{
public:
    explicit AudioStore (const juce::File& dir);

    void setSampleRate (double sr);
    double getSampleRate() const { return sampleRate; }

    // nacita subor (WAV/MP3/...) do skladu, vrati id alebo prazdny retazec
    juce::String importFile (const juce::File& src);
    // ulozi nahravku ako WAV (right = nullptr -> mono)
    juce::String addRecording (const float* left, const float* right, int n, double sr);
    // ulozi data zo suboru piesne (.s92) pod danym id
    void addFromData (const juce::String& id, const juce::String& name, const juce::MemoryBlock& data);

    AudioBufPtr get (const juce::String& id);      // dekoduje pri prvom pouziti
    AudioBufPtr peek (const juce::String& id) const { auto it = cache.find (id); return it != cache.end() ? it->second : nullptr; }
    juce::File fileFor (const juce::String& id);
    juce::var peaksFor (const juce::String& id);   // tvar vlny pre rozhranie
    bool detectTempo (const juce::String& id, double& bpm, double& phase);

    // nahravka natiahnuta / skratena na iny cas bez zmeny vysky tonu (alpha = nova dlzka / povodna)
    AudioBufPtr peekStretched (const juce::String& id, double alpha, int semi) const;
    void putStretched (const juce::String& id, double alpha, int semi, AudioBufPtr b);
    static juce::String stretchKey (const juce::String& id, double alpha, int semi) { return id + "@" + juce::String (alpha, 4) + "#" + juce::String (semi); }
    juce::String addReversed (const juce::String& id);   // otocena kopia nahravky (nove id)

private:
    juce::String newId() const;
    AudioBufPtr decode (const juce::File&);

    juce::File dir;
    juce::AudioFormatManager formats;
    double sampleRate = 44100;
    std::map<juce::String, AudioBufPtr> cache;
    std::map<juce::String, juce::var> peaks;
    std::map<juce::String, AudioBufPtr> stretched;
};
