#include "Melody.h"
#include <algorithm>
#include <cmath>

namespace
{
    float median (std::vector<float> v)
    {
        if (v.empty()) return 0;
        const auto m = v.begin() + (long) v.size() / 2;
        std::nth_element (v.begin(), m, v.end());
        return *m;
    }
}

std::vector<MelNote> detectMelody (const juce::AudioBuffer<float>& buf, double sr, double off, double dur)
{
    std::vector<MelNote> out;
    const int len = buf.getNumSamples();
    const int a = juce::jlimit (0, len, (int) (off * sr));
    const int b = juce::jlimit (a, len, (int) ((off + dur) * sr));
    if (b - a < (int) (sr * 0.2)) return out;

    // mono, dolna priepust a prevzorkovanie na 16 kHz
    std::vector<float> mono ((size_t) (b - a));
    const float* c0 = buf.getReadPointer (0);
    const float* c1 = buf.getNumChannels() > 1 ? buf.getReadPointer (1) : nullptr;
    for (int i = a; i < b; ++i) mono[(size_t) (i - a)] = c1 != nullptr ? 0.5f * (c0[i] + c1[i]) : c0[i];
    {
        juce::IIRFilter f1, f2;
        f1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 6000.0));
        f2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 6000.0));
        f1.processSamples (mono.data(), (int) mono.size());
        f2.processSamples (mono.data(), (int) mono.size());
    }
    const double tsr = 16000.0, ratio = sr / tsr;
    const int n = (int) ((double) mono.size() / ratio) - 1;
    std::vector<float> x ((size_t) juce::jmax (0, n));
    for (int i = 0; i < n; ++i)
    {
        const double p = i * ratio;
        const int k = (int) p;
        const float fr = (float) (p - k);
        x[(size_t) i] = mono[(size_t) k] * (1 - fr) + mono[(size_t) juce::jmin ((int) mono.size() - 1, k + 1)] * fr;
    }

    // YIN po ramcoch
    const int W = 512, hop = 160;
    const int tauMin = (int) (tsr / 1800.0), tauMax = (int) (tsr / 60.0);
    const int frames = (n - W - tauMax) / hop;
    if (frames < 5) return out;
    std::vector<float> midi ((size_t) frames, -1.0f), rms ((size_t) frames, 0.0f);
    std::vector<double> d ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    float maxRms = 0;
    for (int f = 0; f < frames; ++f)
    {
        const float* s = x.data() + f * hop;
        double e = 0;
        for (int j = 0; j < W; ++j) e += s[j] * s[j];
        rms[(size_t) f] = (float) std::sqrt (e / W);
        maxRms = juce::jmax (maxRms, rms[(size_t) f]);

        for (int tau = 1; tau <= tauMax; ++tau)
        {
            double sum = 0;
            const float* t = s + tau;
            for (int j = 0; j < W; ++j) { const double df = s[j] - t[j]; sum += df * df; }
            d[(size_t) tau] = sum;
        }
        double run = 0;
        cm[0] = 1;
        for (int tau = 1; tau <= tauMax; ++tau)
        {
            run += d[(size_t) tau];
            cm[(size_t) tau] = run > 0 ? d[(size_t) tau] * tau / run : 1;
        }
        int best = -1;
        for (int tau = tauMin; tau < tauMax; ++tau)
            if (cm[(size_t) tau] < 0.15)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0)
        {
            int g = tauMin;
            for (int tau = tauMin; tau < tauMax; ++tau) if (cm[(size_t) tau] < cm[(size_t) g]) g = tau;
            if (cm[(size_t) g] < 0.3) best = g;
        }
        if (best <= tauMin || best >= tauMax - 1) continue;
        // parabolicka interpolacia
        const double y0 = cm[(size_t) best - 1], y1 = cm[(size_t) best], y2 = cm[(size_t) best + 1];
        const double den = y0 - 2 * y1 + y2;
        const double tb = best + (std::abs (den) > 1e-12 ? 0.5 * (y0 - y2) / den : 0.0);
        const double f0 = tsr / tb;
        if (f0 < 60 || f0 > 1800) continue;
        midi[(size_t) f] = (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
    }

    // ticho
    const float thr = juce::jmax (0.004f, maxRms * 0.08f);
    for (int f = 0; f < frames; ++f) if (rms[(size_t) f] < thr) midi[(size_t) f] = -1;

    // median z 5 ramcov (len zneniene)
    std::vector<float> sm = midi;
    for (int f = 0; f < frames; ++f)
    {
        if (midi[(size_t) f] < 0) continue;
        std::vector<float> w;
        for (int k = juce::jmax (0, f - 2); k <= juce::jmin (frames - 1, f + 2); ++k) if (midi[(size_t) k] > 0) w.push_back (midi[(size_t) k]);
        sm[(size_t) f] = median (w);
    }

    // rozdelenie na noty
    auto tOf = [&] (int f) { return (f * hop + W * 0.5) / tsr; };
    int start = -1, gap = 0;
    float peak = 0;
    bool dipped = false;
    std::vector<float> ps, pending, rs;
    auto close = [&] (int endFrame)
    {
        if (start >= 0 && ! ps.empty())
        {
            MelNote nt;
            nt.start = tOf (start);
            nt.end = tOf (endFrame);
            nt.pitch = juce::jlimit (0, 127, (int) std::lround (median (ps)));
            float m = 0; for (auto r : rs) m += r;
            m /= (float) juce::jmax ((size_t) 1, rs.size());
            nt.vel = juce::jlimit (0.35f, 1.0f, 0.45f + 0.55f * m / juce::jmax (1e-6f, maxRms));
            if (nt.end - nt.start >= 0.06) out.push_back (nt);
        }
        start = -1; ps.clear(); pending.clear(); rs.clear(); peak = 0; dipped = false;
    };
    for (int f = 0; f < frames; ++f)
    {
        const float m = sm[(size_t) f], r = rms[(size_t) f];
        if (m < 0)
        {
            if (start >= 0 && ++gap >= 3) close (f - gap + 1);
            continue;
        }
        gap = 0;
        if (start < 0) { start = f; ps = { m }; rs = { r }; peak = r; continue; }

        // opakovana nota rovnakej vysky: pokles hlasitosti a novy nastup
        if (r < peak * 0.35f) dipped = true;
        else if (dipped && r > peak * 0.6f) { close (f); start = f; ps = { m }; rs = { r }; peak = r; continue; }

        std::vector<float> recent (ps.end() - (long) juce::jmin ((size_t) 15, ps.size()), ps.end());
        const float cur = median (recent);
        if (std::abs (m - cur) > 0.8f)
        {
            pending.push_back (m);
            if (pending.size() >= 3)
            {
                const auto keep = pending;
                const int ns = f - (int) keep.size() + 1;
                close (ns);
                start = ns; ps = keep; rs = { r }; peak = r;
            }
            continue;
        }
        pending.clear();
        ps.push_back (m); rs.push_back (r);
        peak = juce::jmax (peak, r);
    }
    close (frames);
    return out;
}
