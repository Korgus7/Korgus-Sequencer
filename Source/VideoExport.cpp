#include "VideoExport.h"
#include <juce_dsp/juce_dsp.h>
#include <cmath>

#ifdef _WIN32
bool mfEncodeMp4 (const wchar_t* path, int W, int H, int fps, int numFrames, int sampleRate, const int16_t* pcm, int64_t numSampleFrames,
                  const std::function<bool (int, uint8_t*, int)>& getFrame, std::string& err);
#endif

namespace
{
    constexpr int fftOrder = 11, fftSize = 1 << fftOrder, numBars = 40;
    const juce::Colour cText (0xffe4e9ef), cSub (0xffc9d2dc), cAccent (0xff8fb0d0), cSteel (0xff2d4362);

    juce::String fmtTime (double s)
    {
        const int t = juce::jmax (0, (int) s);
        return juce::String (t / 60) + ":" + juce::String (t % 60).paddedLeft ('0', 2);
    }
}

struct VideoFrameRenderer::Impl
{
    Impl (const VideoOptions& o, const juce::AudioBuffer<float>& a, double s) : opt (o), audio (a), sr (s), fft (fftOrder),
          window ((size_t) fftSize, juce::dsp::WindowingFunction<float>::hann)
    {
        W = opt.width(); H = opt.height();
        total = audio.getNumSamples() / sr;
        work.resize ((size_t) fftSize * 2);
        bars.assign (numBars, 0.0f);
        // obrazok pozadia: vyplnit (orezat) a stmavit - raz
        bg = juce::Image (juce::Image::RGB, W, H, true);
        juce::Graphics g (bg);
        if (opt.background.isValid())
        {
            const auto& im = opt.background;
            const float sc = juce::jmax ((float) W / (float) im.getWidth(), (float) H / (float) im.getHeight());
            const float w = (float) im.getWidth() * sc, h = (float) im.getHeight() * sc;
            g.setImageResamplingQuality (juce::Graphics::highResamplingQuality);
            g.drawImage (im, juce::Rectangle<float> (((float) W - w) * 0.5f, ((float) H - h) * 0.5f, w, h));
            g.setGradientFill (juce::ColourGradient (juce::Colours::black.withAlpha (0.25f), 0, 0, juce::Colours::black.withAlpha (0.70f), 0, (float) H, false));
            g.fillAll();
        }
        else
        {
            g.setGradientFill (juce::ColourGradient (juce::Colour (0xff121821), 0, 0, cSteel, 0, (float) H, false));
            g.fillAll();
        }
    }

    float monoAt (int i) const
    {
        if (i < 0 || i >= audio.getNumSamples()) return 0.0f;
        float v = 0;
        for (int c = 0; c < audio.getNumChannels(); ++c) v += audio.getSample (c, i);
        return v / (float) juce::jmax (1, audio.getNumChannels());
    }

    void updateBars (double t)
    {
        const int centre = (int) (t * sr);
        for (int i = 0; i < fftSize; ++i) work[(size_t) i] = monoAt (centre - fftSize / 2 + i);
        std::fill (work.begin() + fftSize, work.end(), 0.0f);
        window.multiplyWithWindowingTable (work.data(), (size_t) fftSize);
        fft.performFrequencyOnlyForwardTransform (work.data());
        const double fLo = 40.0, fHi = juce::jmin (14000.0, sr * 0.45);
        for (int b = 0; b < numBars; ++b)
        {
            const double f0 = fLo * std::pow (fHi / fLo, (double) b / numBars), f1 = fLo * std::pow (fHi / fLo, (double) (b + 1) / numBars);
            const int k0 = juce::jlimit (1, fftSize / 2 - 1, (int) (f0 / sr * fftSize)), k1 = juce::jlimit (k0 + 1, fftSize / 2, (int) (f1 / sr * fftSize) + 1);
            float m = 0;
            for (int k = k0; k < k1; ++k) m = juce::jmax (m, work[(size_t) k]);
            const float db = juce::Decibels::gainToDecibels (m / (fftSize * 0.25f), -80.0f) + 4.5f * std::log2 ((float) (b + 4) / 4.0f);  // vysky trochu zdvihnut
            const float v = juce::jlimit (0.0f, 1.0f, (db + 58.0f) / 54.0f);
            bars[(size_t) b] = v > bars[(size_t) b] ? v : bars[(size_t) b] * 0.82f + v * 0.18f;
        }
    }

    void render (juce::Image& img, double t)
    {
        if (img.getWidth() != W || img.getHeight() != H) img = juce::Image (juce::Image::ARGB, W, H, false);
        juce::Graphics g (img);
        g.drawImageAt (bg, 0, 0);
        const float fw = (float) W, fh = (float) H, u = juce::jmin (fw, fh) / 1080.0f;
        const float margin = 80.0f * u;

        // nazov a podnadpis
        const float titleY = opt.portrait ? fh * 0.16f : fh * 0.12f;
        g.setColour (cText);
        g.setFont (juce::FontOptions (86.0f * u, juce::Font::bold));
        g.drawFittedText (opt.title, juce::Rectangle<int> ((int) margin, (int) titleY, (int) (fw - 2 * margin), (int) (220 * u)), juce::Justification::centred, 2, 0.8f);
        if (opt.subtitle.isNotEmpty())
        {
            g.setColour (cSub);
            g.setFont (juce::FontOptions (46.0f * u));
            g.drawFittedText (opt.subtitle, juce::Rectangle<int> ((int) margin, (int) (titleY + 230 * u), (int) (fw - 2 * margin), (int) (70 * u)), juce::Justification::centred, 1);
        }

        // vizualizacia
        const float vTop = opt.portrait ? fh * 0.50f : fh * 0.47f, vH = opt.portrait ? fh * 0.22f : fh * 0.28f;
        const float midY = vTop + vH * 0.70f;
        if (opt.style == 0)
        {
            updateBars (t);
            const float area = fw - 2 * margin, slot = area / numBars, bw = slot * 0.62f;
            for (int b = 0; b < numBars; ++b)
            {
                const float h = juce::jmax (6.0f * u, bars[(size_t) b] * vH * 0.70f);
                const float x = margin + slot * (float) b + (slot - bw) * 0.5f;
                g.setColour (cAccent);
                g.fillRoundedRectangle (x, midY - h, bw, h, 3.0f * u);
                g.setColour (cAccent.withAlpha (0.28f));
                g.fillRoundedRectangle (x, midY + 6 * u, bw, h * 0.42f, 3.0f * u);
            }
        }
        else
        {
            const int span = (int) (sr * 0.05);   // 50 ms okno
            const int c0 = (int) (t * sr) - span / 2;
            juce::Path p;
            const int pts = 360;
            for (int i = 0; i <= pts; ++i)
            {
                const int s0 = c0 + i * span / pts;
                float v = 0; for (int k = 0; k < 4; ++k) v += monoAt (s0 + k); v *= 0.25f;
                const float x = margin + (fw - 2 * margin) * (float) i / pts, y = vTop + vH * 0.5f - juce::jlimit (-1.0f, 1.0f, v * 1.6f) * vH * 0.45f;
                if (i == 0) p.startNewSubPath (x, y); else p.lineTo (x, y);
            }
            g.setColour (cAccent.withAlpha (0.25f));
            g.strokePath (p, juce::PathStrokeType (14.0f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
            g.setColour (cAccent);
            g.strokePath (p, juce::PathStrokeType (4.5f * u, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        }

        // priebeh
        const float pY = opt.portrait ? fh * 0.84f : fh * 0.86f, pW = fw - 2 * margin;
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (margin, pY, pW, 8 * u, 3 * u);
        g.setColour (cAccent);
        g.fillRoundedRectangle (margin, pY, pW * (float) juce::jlimit (0.0, 1.0, total > 0 ? t / total : 0.0), 8 * u, 3 * u);
        g.setColour (cSub);
        g.setFont (juce::FontOptions (36.0f * u));
        g.drawText (fmtTime (t), juce::Rectangle<float> (margin, pY + 22 * u, 300 * u, 46 * u), juce::Justification::centredLeft);
        g.drawText (fmtTime (total), juce::Rectangle<float> (fw - margin - 300 * u, pY + 22 * u, 300 * u, 46 * u), juce::Justification::centredRight);
    }

    VideoOptions opt;
    const juce::AudioBuffer<float>& audio;
    double sr, total = 0;
    int W = 1080, H = 1920;
    juce::dsp::FFT fft;
    juce::dsp::WindowingFunction<float> window;
    std::vector<float> work, bars;
    juce::Image bg;
};

VideoFrameRenderer::VideoFrameRenderer (const VideoOptions& o, const juce::AudioBuffer<float>& audio, double sampleRate)
    : impl (std::make_unique<Impl> (o, audio, sampleRate)) {}
VideoFrameRenderer::~VideoFrameRenderer() = default;
void VideoFrameRenderer::render (juce::Image& img, double t) { impl->render (img, t); }

bool writeMp4 (const juce::AudioBuffer<float>& audio, double sr, const VideoOptions& o, const juce::File& out,
               juce::String& error, const std::function<bool (double)>& progress)
{
#ifdef _WIN32
    const int outSr = 48000, fps = 30;
    // prevzorkovat na 48 kHz, 16 bit interleaved
    const double ratio = sr / outSr;
    const int64_t n = (int64_t) std::floor (audio.getNumSamples() / ratio);
    std::vector<int16_t> pcm ((size_t) n * 2);
    for (int c = 0; c < 2; ++c)
    {
        const int src = juce::jmin (c, audio.getNumChannels() - 1);
        std::vector<float> tmp ((size_t) n), padded ((size_t) audio.getNumSamples() + 256, 0.0f);
        std::copy (audio.getReadPointer (src), audio.getReadPointer (src) + audio.getNumSamples(), padded.begin());
        if (std::abs (ratio - 1.0) < 1.0e-9) std::copy (padded.begin(), padded.begin() + n, tmp.begin());
        else { juce::WindowedSincInterpolator ip; ip.process (ratio, padded.data(), tmp.data(), (int) n); }
        for (int64_t i = 0; i < n; ++i) pcm[(size_t) (i * 2 + c)] = (int16_t) juce::jlimit (-32767, 32767, (int) std::lround (tmp[(size_t) i] * 32767.0f));
    }
    const int frames = juce::jmax (1, (int) std::ceil ((double) n / outSr * fps));
    VideoFrameRenderer r (o, audio, sr);
    juce::Image img (juce::Image::ARGB, o.width(), o.height(), false);
    std::string err;
    const bool ok = mfEncodeMp4 (out.getFullPathName().toWideCharPointer(), o.width(), o.height(), fps, frames, outSr, pcm.data(), n,
        [&] (int f, uint8_t* dst, int stride) -> bool
        {
            if (progress && ! progress ((double) f / frames)) return false;
            r.render (img, (double) f / fps);
            const juce::Image::BitmapData bd (img, juce::Image::BitmapData::readOnly);
            for (int y = 0; y < img.getHeight(); ++y)
            {
                const uint8_t* s = bd.getLinePointer (y);
                uint8_t* d = dst + (size_t) y * (size_t) stride;
                if (bd.pixelStride == 4) memcpy (d, s, (size_t) img.getWidth() * 4);
                else for (int x = 0; x < img.getWidth(); ++x) { d[x * 4] = s[x * bd.pixelStride]; d[x * 4 + 1] = s[x * bd.pixelStride + 1]; d[x * 4 + 2] = s[x * bd.pixelStride + 2]; d[x * 4 + 3] = 255; }
            }
            return true;
        }, err);
    if (! ok) error = juce::String::fromUTF8 (err.c_str());
    return ok;
#else
    juce::ignoreUnused (audio, sr, o, out, progress);
    error = juce::String::fromUTF8 ("Export videa funguje len vo Windows.");
    return false;
#endif
}
