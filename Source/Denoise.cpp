#include "Denoise.h"
#include <juce_dsp/juce_dsp.h>
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
    constexpr int order = 11, N = 1 << order, hop = N / 4, bins = N / 2 + 1;

    struct Stft
    {
        juce::dsp::FFT fft { order };
        std::vector<float> win, buf;
        Stft() : win ((size_t) N), buf ((size_t) N * 2)
        {
            for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
        }
        // okno zacinajuce na pozicii pos (mimo nahravky su nuly)
        void forward (const float* x, int len, int pos)
        {
            std::fill (buf.begin(), buf.end(), 0.0f);
            for (int i = 0; i < N; ++i)
            {
                const int k = pos + i;
                if (k >= 0 && k < len) buf[(size_t) i] = x[k] * win[(size_t) i];
            }
            fft.performRealOnlyForwardTransform (buf.data(), true);
        }
        float power (int b) const { const float re = buf[(size_t) b * 2], im = buf[(size_t) b * 2 + 1]; return re * re + im * im; }
    };
}

bool denoise (const juce::AudioBuffer<float>& in, double, float amountDb, int profStart, int profLen,
              juce::AudioBuffer<float>& out, std::function<bool (double)> progress)
{
    const int len = in.getNumSamples(), nch = juce::jmax (1, in.getNumChannels());
    if (len < N) return false;
    Stft st;
    const int first = -N + hop, nFrames = (len - first) / hop + 1;
    auto frameAt = [&] (int k) { return first + k * hop; };
    auto report = [&] (double p) { return ! progress || progress (p); };

    // 1) energia kazdeho okna (na hladanie tichych miest)
    std::vector<float> noise ((size_t) bins, 0.0f);
    int used = 0;
    if (profLen >= N)
    {
        for (int pos = juce::jmax (0, profStart); pos + N <= juce::jmin (len, profStart + profLen); pos += hop)
        {
            for (int c = 0; c < nch; ++c)
            {
                st.forward (in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1)), len, pos);
                for (int b = 0; b < bins; ++b) noise[(size_t) b] += st.power (b) / (float) nch;
            }
            ++used;
        }
    }
    if (used == 0)
    {
        // automaticky: priemer najtichsich 10 % okien (iba cele okna vnutri nahravky)
        std::vector<std::pair<float, int>> en;
        for (int pos = 0; pos + N <= len; pos += hop)
        {
            double e = 0;
            for (int c = 0; c < nch; ++c)
            {
                const float* x = in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1));
                for (int i = 0; i < N; i += 4) e += (double) x[pos + i] * x[pos + i];
            }
            en.push_back ({ (float) e, pos });
        }
        std::sort (en.begin(), en.end());
        const int take = juce::jlimit (1, (int) en.size(), juce::jmax (8, (int) en.size() / 10));
        for (int k = 0; k < take; ++k)
        {
            for (int c = 0; c < nch; ++c)
            {
                st.forward (in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1)), len, en[(size_t) k].second);
                for (int b = 0; b < bins; ++b) noise[(size_t) b] += st.power (b) / (float) nch;
            }
            ++used;
        }
    }
    if (used == 0) return false;
    for (auto& v : noise) v /= (float) used;
    if (! report (0.1)) return false;

    // 2) zisk pre kazde okno a pasmo (spolocny pre vsetky kanaly, aby zostal stereo obraz)
    const float floorG = std::pow (10.0f, -juce::jlimit (3.0f, 40.0f, amountDb) / 20.0f);
    const float over = 3.0f;   // nadmerne odcitanie proti "hudobnemu sumu"
    out.setSize (nch, len);
    out.clear();
    std::vector<float> norm ((size_t) len, 0.0f);
    std::vector<float> g ((size_t) bins), prev ((size_t) bins, 1.0f), sm ((size_t) bins);
    std::vector<std::vector<float>> spec ((size_t) nch, std::vector<float> ((size_t) N * 2));
    std::vector<float> pw ((size_t) bins);

    for (int k = 0; k < nFrames; ++k)
    {
        const int pos = frameAt (k);
        std::fill (pw.begin(), pw.end(), 0.0f);
        for (int c = 0; c < nch; ++c)
        {
            st.forward (in.getReadPointer (juce::jmin (c, in.getNumChannels() - 1)), len, pos);
            spec[(size_t) c] = st.buf;
            for (int b = 0; b < bins; ++b) pw[(size_t) b] += st.power (b) / (float) nch;
        }
        for (int b = 0; b < bins; ++b)
        {
            const float p = pw[(size_t) b];
            g[(size_t) b] = p > 1.0e-20f ? std::sqrt (juce::jmax (0.0f, 1.0f - over * noise[(size_t) b] / p)) : 0.0f;
        }
        // vyhladenie po frekvenciach a v case (rychly nabeh, pomalsi dozvuk)
        for (int b = 0; b < bins; ++b)
        {
            const float a = g[(size_t) juce::jmax (0, b - 1)], c = g[(size_t) juce::jmin (bins - 1, b + 1)];
            float v = 0.25f * a + 0.5f * g[(size_t) b] + 0.25f * c;
            if (v < prev[(size_t) b]) v = 0.65f * prev[(size_t) b] + 0.35f * v;
            sm[(size_t) b] = v;
        }
        prev = sm;
        for (int c = 0; c < nch; ++c)
        {
            auto& s = spec[(size_t) c];
            for (int b = 0; b < bins; ++b)
            {
                const float gg = floorG + (1.0f - floorG) * sm[(size_t) b];
                s[(size_t) b * 2] *= gg; s[(size_t) b * 2 + 1] *= gg;
            }
            for (int b = bins; b < N; ++b) { s[(size_t) b * 2] = 0.0f; s[(size_t) b * 2 + 1] = 0.0f; }
            st.fft.performRealOnlyInverseTransform (s.data());
            float* o = out.getWritePointer (c);
            for (int i = 0; i < N; ++i)
            {
                const int t = pos + i;
                if (t < 0 || t >= len) continue;
                o[t] += s[(size_t) i] * st.win[(size_t) i];
                if (c == 0) norm[(size_t) t] += st.win[(size_t) i] * st.win[(size_t) i];
            }
        }
        if ((k & 63) == 0 && ! report (0.1 + 0.9 * (double) k / nFrames)) return false;
    }
    for (int c = 0; c < nch; ++c)
    {
        float* o = out.getWritePointer (c);
        for (int t = 0; t < len; ++t) o[t] = norm[(size_t) t] > 1.0e-4f ? o[t] / norm[(size_t) t] : 0.0f;
    }
    return true;
}
