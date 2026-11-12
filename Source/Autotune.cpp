#include "Autotune.h"
#include <algorithm>
#include <cmath>
#include <vector>

namespace
{
constexpr double tsr = 16000.0;   // vzorkovanie pre detekciu vysky
constexpr int hop = 80;           // 5 ms

// vyska tonu po ramcoch (MIDI, -1 = bez tonu) a hlasitost
void pitchTrack (const std::vector<float>& mono, double sr, std::vector<float>& midi, std::vector<float>& level,
                 const std::function<bool (double)>& progress)
{
    std::vector<float> lp (mono);
    {
        juce::IIRFilter f1, f2;
        f1.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 4000.0));
        f2.setCoefficients (juce::IIRCoefficients::makeLowPass (sr, 4000.0));
        f1.processSamples (lp.data(), (int) lp.size());
        f2.processSamples (lp.data(), (int) lp.size());
    }
    const double ratio = sr / tsr;
    const int n = juce::jmax (0, (int) ((double) lp.size() / ratio) - 1);
    std::vector<float> x ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        const double p = i * ratio;
        const int k = (int) p;
        const float fr = (float) (p - k);
        x[(size_t) i] = lp[(size_t) k] * (1 - fr) + lp[(size_t) juce::jmin ((int) lp.size() - 1, k + 1)] * fr;
    }

    const int W = 640, tauMin = (int) (tsr / 1200.0), tauMax = (int) (tsr / 60.0);
    const int frames = n / hop + 1;
    midi.assign ((size_t) frames, -1.0f);
    level.assign ((size_t) frames, 0.0f);
    std::vector<double> d ((size_t) tauMax + 2), cm ((size_t) tauMax + 2);
    std::vector<float> fr0 ((size_t) (W + tauMax + 2));
    for (int f = 0; f < frames; ++f)
    {
        if ((f & 63) == 0 && progress && ! progress (0.35 * f / frames)) return;
        const int st = f * hop - (W + tauMax) / 2;
        for (int j = 0; j < W + tauMax + 1; ++j)
        {
            const int k = st + j;
            fr0[(size_t) j] = (k >= 0 && k < n) ? x[(size_t) k] : 0.0f;
        }
        const float* s = fr0.data();
        double e = 0;
        for (int j = 0; j < W; ++j) e += s[j] * s[j];
        level[(size_t) f] = (float) std::sqrt (e / W);
        if (e < 1.0e-7 * W) continue;

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
            if (cm[(size_t) tau] < 0.12)
            {
                while (tau + 1 < tauMax && cm[(size_t) tau + 1] < cm[(size_t) tau]) ++tau;
                best = tau;
                break;
            }
        if (best < 0) continue;
        double tb = best;
        if (best > 1 && best < tauMax)
        {
            const double a = cm[(size_t) best - 1], b = cm[(size_t) best], c = cm[(size_t) best + 1];
            const double den = a - 2 * b + c;
            if (std::abs (den) > 1.0e-12) tb = best + 0.5 * (a - c) / den;
        }
        const double f0 = tsr / tb;
        midi[(size_t) f] = (float) (69.0 + 12.0 * std::log2 (f0 / 440.0));
    }

    // tiche ramce bez tonu
    float mx = 0;
    for (auto l : level) mx = juce::jmax (mx, l);
    for (size_t i = 0; i < midi.size(); ++i)
        if (level[i] < mx * 0.03f) midi[i] = -1.0f;

    // median 5 proti preskokom o oktavu
    std::vector<float> m2 (midi);
    for (size_t i = 2; i + 2 < midi.size(); ++i)
    {
        float v[5]; int c = 0;
        for (int k = -2; k <= 2; ++k) if (midi[i + (size_t) k] >= 0) v[c++] = midi[i + (size_t) k];
        if (midi[i] < 0 || c < 3) continue;
        std::sort (v, v + c);
        m2[i] = v[c / 2];
    }
    midi.swap (m2);
}

const int majorSteps[7] = { 0, 2, 4, 5, 7, 9, 11 };
const int minorSteps[7] = { 0, 2, 3, 5, 7, 8, 10 };

void detectKey (const std::vector<float>& midi, const std::vector<float>& level, int& key, int& scale)
{
    static const double prof[2][12] = {
        { 6.35, 2.23, 3.48, 2.33, 4.38, 4.09, 2.52, 5.19, 2.39, 3.66, 2.29, 2.88 },
        { 6.33, 2.68, 3.52, 5.38, 2.60, 3.53, 2.54, 4.75, 3.98, 2.69, 3.34, 3.17 } };
    double h[12] = {};
    for (size_t i = 0; i < midi.size(); ++i)
        if (midi[i] >= 0) h[((int) std::lround (midi[i]) % 12 + 12) % 12] += level[i];
    double best = -1.0e30; int bk = key < 0 ? 0 : key, bs = scale < 0 || scale > 1 ? 0 : scale;
    for (int s = 0; s < 2; ++s)
    {
        if (scale == 0 || scale == 1) if (s != scale) continue;
        for (int k = 0; k < 12; ++k)
        {
            if (key >= 0 && k != key) continue;
            double mh = 0, mp = 0;
            for (int i = 0; i < 12; ++i) { mh += h[i]; mp += prof[s][i]; }
            mh /= 12; mp /= 12;
            double num = 0, dh = 0, dp = 0;
            for (int i = 0; i < 12; ++i)
            {
                const double a = h[(i + k) % 12] - mh, b = prof[s][i] - mp;
                num += a * b; dh += a * a; dp += b * b;
            }
            const double r = num / std::sqrt (dh * dp + 1.0e-12);
            if (r > best) { best = r; bk = k; bs = s; }
        }
    }
    key = bk; scale = bs;
}

// najblizsia nota stupnice
float snap (float m, int key, int scale)
{
    if (scale == 2) return std::round (m);
    const int* st = scale == 1 ? minorSteps : majorSteps;
    float best = m, bd = 1.0e9f;
    const int base = (int) std::floor (m) - 2;
    for (int n = base; n <= base + 4; ++n)
    {
        const int pc = ((n - key) % 12 + 12) % 12;
        bool in = false;
        for (int i = 0; i < 7; ++i) in |= st[i] == pc;
        if (! in) continue;
        const float dd = std::abs (m - (float) n);
        if (dd < bd) { bd = dd; best = (float) n; }
    }
    return best;
}

inline float hermite (const float* x, int n, double pos)
{
    const int i = (int) pos;
    const float t = (float) (pos - i);
    auto at = [x, n] (int k) { return x[juce::jlimit (0, n - 1, k)]; };
    const float xm1 = at (i - 1), x0 = at (i), x1 = at (i + 1), x2 = at (i + 2);
    const float c1 = 0.5f * (x1 - xm1), c2 = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, c3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
    return ((c3 * t + c2) * t + c1) * t + x0;
}
} // namespace

AutotuneResult autotune (const juce::AudioBuffer<float>& in, double sr, int key, int scale, int speed,
                         juce::AudioBuffer<float>& out, std::function<bool (double)> progress)
{
    AutotuneResult res;
    const int ch = in.getNumChannels(), n = in.getNumSamples();
    if (ch <= 0 || n < (int) (sr * 0.2)) return res;

    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float v = 0;
        for (int c = 0; c < ch; ++c) v += in.getSample (c, i);
        mono[(size_t) i] = v / (float) ch;
    }
    std::vector<float> midi, level;
    pitchTrack (mono, sr, midi, level, progress);
    if (progress && ! progress (0.35)) return res;

    if (scale != 2 && (key < 0 || scale < 0)) detectKey (midi, level, key, scale);
    if (key < 0) key = 0;
    if (scale < 0) scale = 0;
    res.key = key; res.scale = scale;

    // korekcia v poltonoch po ramcoch (s hysteréziou a vyhladenim podla rychlosti)
    const int frames = (int) midi.size();
    std::vector<float> corr ((size_t) frames, 0.0f);
    const double frameSec = hop / tsr;
    const double tau = speed >= 2 ? 0.0 : (speed == 1 ? 0.035 : 0.110);
    const float a = tau <= 0 ? 0.0f : (float) std::exp (-frameSec / tau);
    float target = -1, c = 0, cTarget = 0;
    for (int f = 0; f < frames; ++f)
    {
        const float m = midi[(size_t) f];
        if (m < 0) { target = -1; cTarget = 0; }
        else
        {
            const float s = snap (m, key, scale);
            if (target < 0 || std::abs (m - target) > 0.62f) target = s;   // hysterezia
            cTarget = juce::jlimit (-2.0f, 2.0f, target - m);
        }
        c = cTarget + (c - cTarget) * a;
        corr[(size_t) f] = c;
    }
    // jemne prechody pri zaciatku a konci tonu (bez lupnutia)
    {
        std::vector<float> sm (corr);
        const int R = 3;
        for (int f = 0; f < frames; ++f)
        {
            double s = 0; int k = 0;
            for (int j = -R; j <= R; ++j) if (f + j >= 0 && f + j < frames) { s += corr[(size_t) (f + j)]; ++k; }
            sm[(size_t) f] = (float) (s / k);
        }
        if (speed < 2) corr.swap (sm);
    }

    auto ratioAt = [&] (double pos) -> double
    {
        const double fpos = pos / sr * tsr / hop;
        const int f = juce::jlimit (0, frames - 1, (int) fpos);
        const int g = juce::jmin (frames - 1, f + 1);
        const double t = juce::jlimit (0.0, 1.0, fpos - f);
        const double cc = corr[(size_t) f] * (1 - t) + corr[(size_t) g] * t;
        return std::pow (2.0, cc / 12.0);
    };

    // 1) premenliva rychlost citania = posun vysky (casovanie sa zmeni)
    std::vector<double> posOf;
    posOf.reserve ((size_t) (n * 1.15) + 16);
    for (double p = 0; p < n - 1; p += ratioAt (p)) posOf.push_back (p);
    const int m1 = (int) posOf.size();
    juce::AudioBuffer<float> y (ch, m1);
    for (int cc = 0; cc < ch; ++cc)
    {
        const float* src = in.getReadPointer (cc);
        float* dst = y.getWritePointer (cc);
        for (int k = 0; k < m1; ++k) dst[k] = hermite (src, n, posOf[(size_t) k]);
    }
    if (progress && ! progress (0.55)) return res;

    // 2) WSOLA - vrati povodne casovanie
    const int N = sr > 60000 ? 2048 : 1024, Hs = N / 2, delta = N / 3;
    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    std::vector<float> ym ((size_t) m1);
    for (int k = 0; k < m1; ++k)
    {
        float v = 0;
        for (int cc = 0; cc < ch; ++cc) v += y.getSample (cc, k);
        ym[(size_t) k] = v;
    }
    out.setSize (ch, n + N);
    out.clear();
    auto kFor = [&] (double origPos) -> int
    {
        const auto it = std::lower_bound (posOf.begin(), posOf.end(), origPos);
        return (int) (it - posOf.begin());
    };
    int prev = 0;
    const int frameCount = n / Hs + 1;
    for (int fr = 0; fr < frameCount; ++fr)
    {
        if ((fr & 255) == 0 && progress && ! progress (0.55 + 0.45 * fr / frameCount)) return res;
        const int op = fr * Hs - Hs;            // prvy ramec zacina pred nulou (plna obalka)
        const int nominal = kFor ((double) (op + N / 2)) - N / 2;
        int best = nominal;
        if (fr > 0)
        {
            const int tgt = prev + Hs;
            double bestC = -1.0e30;
            for (int dd = -delta; dd <= delta; dd += 2)
            {
                const int cand = nominal + dd;
                double cc = 0, e = 1.0e-9;
                for (int j = 0; j < Hs; j += 2)
                {
                    const int ia = tgt + j, ib = cand + j;
                    const float av = (ia >= 0 && ia < m1) ? ym[(size_t) ia] : 0.0f;
                    const float bv = (ib >= 0 && ib < m1) ? ym[(size_t) ib] : 0.0f;
                    cc += av * bv; e += bv * bv;
                }
                const double score = cc / std::sqrt (e) - 1.0e-7 * std::abs (dd);
                if (score > bestC) { bestC = score; best = cand; }
            }
        }
        for (int cc = 0; cc < ch; ++cc)
        {
            const float* src = y.getReadPointer (cc);
            float* dst = out.getWritePointer (cc);
            for (int j = 0; j < N; ++j)
            {
                const int o = op + j, s = best + j;
                if (o < 0 || o >= n + N || s < 0 || s >= m1) continue;
                dst[o] += src[s] * win[(size_t) j];
            }
        }
        prev = best;
    }
    out.setSize (ch, n, true);
    res.ok = true;
    return res;
}
