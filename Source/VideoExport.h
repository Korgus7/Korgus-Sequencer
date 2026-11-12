#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_graphics/juce_graphics.h>
#include <functional>
#include <memory>

// Video k piesni (MP4: H.264 + AAC) - obrazok/farba, nazov, pohyblive stlpce alebo vlna, priebeh.
struct VideoOptions
{
    juce::String title, subtitle;
    juce::Image background;     // prazdny = farebny prechod
    int style = 0;              // 0 = stlpce, 1 = vlna
    bool portrait = true;       // 1080x1920 (TikTok) / 1920x1080 (YouTube)
    int width() const  { return portrait ? 1080 : 1920; }
    int height() const { return portrait ? 1920 : 1080; }
};

// kreslenie snimok (spolocne pre vsetky platformy)
class VideoFrameRenderer
{
public:
    VideoFrameRenderer (const VideoOptions& o, const juce::AudioBuffer<float>& audio, double sampleRate);
    ~VideoFrameRenderer();
    // snimka v case t (sekundy); volat postupne (vyhladzovanie stlpcov)
    void render (juce::Image& img, double t);
private:
    struct Impl;
    std::unique_ptr<Impl> impl;
};

// zapis MP4 (len Windows - Media Foundation). audio: stereo, sr lubovolne (prevzorkuje sa na 48 kHz)
bool writeMp4 (const juce::AudioBuffer<float>& audio, double sr, const VideoOptions& o, const juce::File& out,
               juce::String& error, const std::function<bool (double)>& progress = {});
