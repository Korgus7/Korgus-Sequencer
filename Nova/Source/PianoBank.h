#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <array>
#include <atomic>
#include <memory>
#include <vector>

// Klavirne vzorky (Salamander Grand Piano V3, Alexander Holm, CC-BY 3.0)
// Jedna banka pre vsetky instancie pluginu, nacita sa na pozadi.
class PianoBank
{
public:
    static constexpr int numLayers = 4;
    struct Sample
    {
        int root = 60;
        double rate = 48000;
        std::vector<int16_t> data;   // stereo, prekladane L R
        int length = 0;              // v ramcoch
    };

    static std::shared_ptr<PianoBank> get();
    bool ready() const { return isReady.load(); }
    bool failed() const { return isFailed.load(); }
    // vzorka najblizsia k note pre danu vrstvu (0 = najtichsia)
    const Sample* find (int note, int layer) const;

private:
    void load();
    std::vector<Sample> samples;
    std::array<std::array<const Sample*, numLayers>, 128> map {};
    std::atomic<bool> isReady { false }, isFailed { false };
};
