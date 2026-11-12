#include "Mp3Export.h"
#include <cmath>
#include <deque>
#include <vector>

extern "C"
{
#include "../third_party/shine/layer3.h"
}

namespace
{
struct Biquad
{
    double b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
    double process (double x)
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

// K-filter (BS.1770) pre lubovolne vzorkovanie
void kFilter (double fs, Biquad& shelf, Biquad& hp)
{
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.b0 = (Vh + Vb * K / Q + K * K) / a0;
        shelf.b1 = 2.0 * (K * K - Vh) / a0;
        shelf.b2 = (Vh - Vb * K / Q + K * K) / a0;
        shelf.a1 = 2.0 * (K * K - 1.0) / a0;
        shelf.a2 = (1.0 - K / Q + K * K) / a0;
    }
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (juce::MathConstants<double>::pi * f0 / fs);
        const double a0 = 1.0 + K / Q + K * K;
        hp.b0 = 1.0; hp.b1 = -2.0; hp.b2 = 1.0;
        hp.a1 = 2.0 * (K * K - 1.0) / a0;
        hp.a2 = (1.0 - K / Q + K * K) / a0;
    }
}
}

double measureLufs (const juce::AudioBuffer<float>& b, double sr)
{
    const int ch = juce::jmin (2, b.getNumChannels()), n = b.getNumSamples();
    const int step = (int) (sr * 0.1);           // 100 ms (bloky 400 ms s prekryvom 75 %)
    if (ch <= 0 || n < step * 4) return -70.0;
    const int nSteps = n / step;
    std::vector<double> stepSum ((size_t) nSteps, 0.0);
    for (int c = 0; c < ch; ++c)
    {
        Biquad s, h;
        kFilter (sr, s, h);
        const float* x = b.getReadPointer (c);
        for (int k = 0; k < nSteps; ++k)
        {
            double acc = 0;
            for (int i = k * step; i < (k + 1) * step; ++i)
            {
                const double y = h.process (s.process (x[i]));
                acc += y * y;
            }
            stepSum[(size_t) k] += acc;
        }
    }
    std::vector<double> blocks;
    for (int k = 0; k + 3 < nSteps; ++k)
        blocks.push_back ((stepSum[(size_t) k] + stepSum[(size_t) k + 1] + stepSum[(size_t) k + 2] + stepSum[(size_t) k + 3]) / (4.0 * step));
    auto lufsOf = [] (double ms) { return -0.691 + 10.0 * std::log10 (juce::jmax (1.0e-12, ms)); };
    double sum = 0; int cnt = 0;
    for (double z : blocks) if (lufsOf (z) > -70.0) { sum += z; ++cnt; }
    if (cnt == 0) return -70.0;
    const double rel = lufsOf (sum / cnt) - 10.0;
    sum = 0; cnt = 0;
    for (double z : blocks) if (lufsOf (z) > -70.0 && lufsOf (z) > rel) { sum += z; ++cnt; }
    return cnt > 0 ? lufsOf (sum / cnt) : -70.0;
}

double normaliseLoudness (juce::AudioBuffer<float>& b, double sr, double targetLufs, double ceilingDb)
{
    const int ch = b.getNumChannels(), n = b.getNumSamples();
    double gainDb = 0;
    if (targetLufs < 0)
    {
        const double now = measureLufs (b, sr);
        if (now > -69.0) gainDb = juce::jlimit (-30.0, 30.0, targetLufs - now);
        b.applyGain ((float) juce::Decibels::decibelsToGain (gainDb));
    }

    // limiter s predstihom 5 ms na strop
    const float ceil = juce::Decibels::decibelsToGain ((float) ceilingDb);
    const int L = juce::jmax (8, (int) (sr * 0.005));
    std::vector<float> req ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float pk = 0;
        for (int c = 0; c < ch; ++c) pk = juce::jmax (pk, std::abs (b.getSample (c, i)));
        req[(size_t) i] = pk > ceil ? ceil / pk : 1.0f;
    }
    // minimum v okne [i, i + L] (posuvne minimum)
    std::vector<float> win ((size_t) n);
    std::deque<int> dq;
    for (int i = n - 1; i >= 0; --i)
    {
        while (! dq.empty() && req[(size_t) dq.back()] >= req[(size_t) i]) dq.pop_back();
        dq.push_back (i);
        while (dq.front() > i + L) dq.pop_front();
        win[(size_t) i] = req[(size_t) dq.front()];
    }
    const float att = 1.0f - std::exp (-5.0f / (float) L), rel = 1.0f - std::exp (-1.0f / (float) (sr * 0.12));
    float g = 1.0f;
    for (int i = 0; i < n; ++i)
    {
        const float t = win[(size_t) i];
        g += (t - g) * (t < g ? att : rel);
        const float gg = g;
        for (int c = 0; c < ch; ++c)
        {
            float v = b.getSample (c, i) * gg;
            v = juce::jlimit (-ceil, ceil, v);   // istota
            b.setSample (c, i, v);
        }
    }
    return gainDb;
}

bool writeMp3 (const juce::AudioBuffer<float>& src, double sr, int kbps, const juce::File& out, juce::String& error,
               const std::function<bool (double)>& progress)
{
    // shine: 32 / 44.1 / 48 kHz
    juce::AudioBuffer<float> rs;
    const juce::AudioBuffer<float>* b = &src;
    int rate = (int) std::lround (sr);
    if (rate != 44100 && rate != 48000 && rate != 32000)
    {
        const double ratio = sr / 44100.0;
        const int outN = (int) (src.getNumSamples() / ratio);
        rs.setSize (2, outN);
        for (int c = 0; c < 2; ++c)
        {
            juce::WindowedSincInterpolator ip;
            ip.process (ratio, src.getReadPointer (juce::jmin (c, src.getNumChannels() - 1)), rs.getWritePointer (c), outN);
        }
        b = &rs;
        rate = 44100;
    }

    shine_config_t cfg;
    shine_set_config_mpeg_defaults (&cfg.mpeg);
    cfg.wave.channels = PCM_STEREO;
    cfg.wave.samplerate = rate;
    cfg.mpeg.mode = STEREO;
    cfg.mpeg.bitr = kbps;
    if (shine_check_config (rate, kbps) < 0) { error = juce::String::fromUTF8 ("MP3: nepodporované nastavenie."); return false; }
    shine_t enc = shine_initialise (&cfg);
    if (enc == nullptr) { error = juce::String::fromUTF8 ("MP3: enkóder sa nespustil."); return false; }

    out.deleteFile();
    juce::FileOutputStream os (out);
    if (! os.openedOk()) { shine_close (enc); error = juce::String::fromUTF8 ("Nedá sa zapísať: ") + out.getFullPathName(); return false; }

    const int spp = shine_samples_per_pass (enc), n = b->getNumSamples();
    const float* L = b->getReadPointer (0);
    const float* R = b->getReadPointer (b->getNumChannels() > 1 ? 1 : 0);
    std::vector<int16_t> pcm ((size_t) spp * 2);
    juce::Random rnd (12345);
    for (int pos = 0; pos < n; pos += spp)
    {
        for (int i = 0; i < spp; ++i)
        {
            const int k = pos + i;
            for (int c = 0; c < 2; ++c)
            {
                const float v = k < n ? (c == 0 ? L[k] : R[k]) : 0.0f;
                const float d = (rnd.nextFloat() - rnd.nextFloat()) / 32768.0f;   // TPDF dither
                // shine zosilni o 9/8 (+1 dB) - vyrovnane tu
                pcm[(size_t) (i * 2 + c)] = (int16_t) juce::jlimit (-32768, 32767, (int) std::lround ((v * (8.0f / 9.0f) + d) * 32767.0f));
            }
        }
        int written = 0;
        const unsigned char* data = shine_encode_buffer_interleaved (enc, pcm.data(), &written);
        if (written > 0) os.write (data, (size_t) written);
        if (progress && (pos / spp) % 64 == 0 && ! progress ((double) pos / n)) { shine_close (enc); os.flush(); out.deleteFile(); error = "cancel"; return false; }
    }
    int written = 0;
    const unsigned char* data = shine_flush (enc, &written);
    if (written > 0) os.write (data, (size_t) written);
    shine_close (enc);
    os.flush();
    return true;
}
