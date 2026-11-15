#pragma once
// Uprava spevu pred vocoderom (modulator): hornopriepustny filter, zvyraznenie zrozumitelnosti,
// brana proti dozvuku / sumu a silny kompresor, aby boli slova rovnako silne.
// Bez JUCE, bez alokacii v process().
#include <cmath>
#include <algorithm>

namespace korgus
{
struct VoxPrep
{
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        hp.setHighpass (sr, 95.0f, 0.707f);
        pres.setPeak (sr, 2600.0f, 1.0f, 2.5f);
        aAtt = coef (0.003f); aRel = coef (0.070f); aPkRel = coef (0.9f); aGate = coef (0.012f);
        reset();
    }
    void reset() { hp.reset(); pres.reset(); env = 0; peak = 0.0f; gateG = 0; }

    // mono vstup -> upraveny mono vystup (moze byt in-place)
    void process (const float* in, float* out, int n)
    {
        for (int i = 0; i < n; ++i)
        {
            float x = in[i];
            if (! std::isfinite (x)) x = 0.0f;
            x = pres.process (hp.process (x));
            const float a = std::abs (x);
            env += (a > env ? aAtt : aRel) * (a - env);
            peak = std::max (peak * (1.0f - aPkRel) + 0.0f, env);   // pomaly klesajuca spicka hlasu
            peak = std::max (peak, 1.0e-5f);
            // brana: pod -26 dB od spicky (chvost dozvuku, sum) sa hlas stisi
            const float rel = env / peak;
            const float gt = rel > 0.05f ? 1.0f : (rel < 0.012f ? 0.0f : (rel - 0.012f) / 0.038f);
            gateG += aGate * (gt - gateG);
            // kompresor: nad -26 dBFS pomer 5:1, automaticke zosilnenie
            const float lv = std::max (env, 1.0e-6f);
            const float thr = 0.05f;
            const float g = lv > thr ? std::pow (lv / thr, 1.0f / 5.0f - 1.0f) : 1.0f;
            float y = x * g * gateG * 4.0f;
            y = y / (1.0f + std::abs (y) * 0.25f);   // makke obmedzenie
            if (! std::isfinite (y)) { reset(); y = 0.0f; }
            out[i] = y;
        }
    }
    float level() const { return env; }

private:
    float coef (float sec) const { return 1.0f - std::exp (-1.0f / (sec * sr)); }

    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        void reset() { z1 = z2 = 0; }
        float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void setHighpass (float fs, float f, float q)
        {
            const float w = 6.2831853f * f / fs, c = std::cos (w), al = std::sin (w) / (2.0f * q), a0 = 1.0f + al;
            b0 = (1.0f + c) * 0.5f / a0; b1 = -(1.0f + c) / a0; b2 = b0; a1 = -2.0f * c / a0; a2 = (1.0f - al) / a0;
        }
        void setPeak (float fs, float f, float q, float db)
        {
            const float A = std::pow (10.0f, db / 40.0f), w = 6.2831853f * f / fs, c = std::cos (w), al = std::sin (w) / (2.0f * q), a0 = 1.0f + al / A;
            b0 = (1.0f + al * A) / a0; b1 = -2.0f * c / a0; b2 = (1.0f - al * A) / a0; a1 = -2.0f * c / a0; a2 = (1.0f - al / A) / a0;
        }
    } hp, pres;

    float sr = 48000.0f, env = 0, peak = 0, gateG = 0;
    float aAtt = 0.01f, aRel = 0.001f, aPkRel = 0.0001f, aGate = 0.01f;
};

// De-esser: rozdeli signal na nizsie a vysoke pasmo (od ~5 kHz); ked su vysky (S, C, Z) silne
// voci celku, vysoke pasmo sa stisi. Stereo, bez alokacii.
struct DeEsser
{
    void prepare (double sampleRate)
    {
        sr = (float) sampleRate;
        const float w = 1.0f - std::exp (-6.2831853f * 5200.0f / sr);
        a = w;
        att = 1.0f - std::exp (-1.0f / (0.0015f * sr)); rel = 1.0f - std::exp (-1.0f / (0.06f * sr));
        reset();
    }
    void reset() { for (auto& z : lp) z = 0; eH = eF = 0; g = 1; }
    // amount 0..1 (0 = vypnute)
    void process (float* L, float* R, int n, float amount)
    {
        if (amount <= 0.001f) return;
        const float thr = 0.5f - 0.32f * amount;   // podiel vysok, nad ktorym sa zacne tlmit
        for (int i = 0; i < n; ++i)
        {
            float l = L[i], r = R[i];
            lp[0] += a * (l - lp[0]); lp[1] += a * (lp[0] - lp[1]);
            lp[2] += a * (r - lp[2]); lp[3] += a * (lp[2] - lp[3]);
            const float hl = l - lp[1], hr = r - lp[3];
            const float h = std::abs (hl) + std::abs (hr), f = std::abs (l) + std::abs (r);
            eH += (h > eH ? att : rel) * (h - eH);
            eF += (f > eF ? att : rel) * (f - eF);
            float tg = 1.0f;
            if (eF > 1.0e-6f && eH > thr * eF) tg = std::max (0.15f, thr * eF / eH);
            g += (tg < g ? att : rel) * (tg - g);
            L[i] = lp[1] + hl * g; R[i] = lp[3] + hr * g;
        }
    }
    float sr = 48000.0f, a = 0.5f, att = 0.01f, rel = 0.001f, eH = 0, eF = 0, g = 1, lp[4] {};
};
} // namespace korgus
