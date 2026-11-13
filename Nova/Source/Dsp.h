#pragma once
#include <cmath>
#include <vector>
#include <array>
#include <algorithm>
#include <cstdint>
#include <atomic>

// Male DSP stavebne kamene pre Korgus Nova
namespace nova
{
    constexpr float pi = 3.14159265358979f;
    constexpr float twoPi = 6.28318530717959f;

    inline float clampf (float x, float lo, float hi) { return x < lo ? lo : (x > hi ? hi : x); }
    inline float dbToGain (float db) { return std::pow (10.0f, db / 20.0f); }
    inline float noteHz (float n) { return 440.0f * std::pow (2.0f, (n - 69.0f) / 12.0f); }

    // rychly sum
    struct Noise
    {
        uint32_t s = 0x12345678u;
        float next() { s = s * 1664525u + 1013904223u; return (float) (int32_t) s * (1.0f / 2147483648.0f); }
    };

    // obalka ADSR s exponencialnymi usekmi
    struct Env
    {
        enum Stage { idle, attack, decay, sustain, release } stage = idle;
        float v = 0, a = 0.01f, d = 0.2f, s = 0.7f, r = 0.3f, sr = 48000;
        float aInc = 0, dCoef = 0, rCoef = 0;
        void set (float A, float D, float S, float R, float rate)
        {
            a = std::max (A, 0.0005f); d = std::max (D, 0.001f); s = S; r = std::max (R, 0.002f); sr = rate;
            aInc = 1.0f / (a * sr);
            dCoef = std::exp (-1.0f / (d * 0.3f * sr));   // 0.3: tau ~ tretina casu (do -26 dB)
            rCoef = std::exp (-1.0f / (r * 0.3f * sr));
        }
        void setRelease (float R) { r = std::max (R, 0.002f); rCoef = std::exp (-1.0f / (r * 0.3f * sr)); }
        void on() { stage = attack; }
        void off() { if (stage != idle) stage = release; }
        void kill (float sec) { setRelease (sec); stage = release; }
        bool active() const { return stage != idle; }
        float next()
        {
            switch (stage)
            {
                case attack:  v += aInc; if (v >= 1.0f) { v = 1.0f; stage = decay; } break;
                case decay:   v = s + (v - s) * dCoef; if (std::abs (v - s) < 1.0e-4f) { v = s; stage = sustain; } break;
                case sustain: v = s; if (s <= 1.0e-5f) stage = idle; break;
                case release: v *= rCoef; if (v < 1.0e-5f) { v = 0; stage = idle; } break;
                default: break;
            }
            return v;
        }
    };

    // stavovo-premenny filter (TPT, Zavalishin) - dolna priepust aj pasmova
    struct Svf
    {
        float ic1 = 0, ic2 = 0, g = 0.1f, k = 1.4f, a1 = 0, a2 = 0, a3 = 0;
        void set (float hz, float q, float sr)
        {
            hz = clampf (hz, 20.0f, sr * 0.45f);
            g = std::tan (pi * hz / sr); k = 1.0f / std::max (q, 0.3f);
            a1 = 1.0f / (1.0f + g * (g + k)); a2 = g * a1; a3 = g * a2;
        }
        void reset() { ic1 = ic2 = 0; }
        // vrati dolnu priepust, do bp zapise pasmovu
        float process (float x, float& bp)
        {
            const float v3 = x - ic2, v1 = a1 * ic1 + a2 * v3, v2 = ic2 + a2 * ic1 + a3 * v3;
            ic1 = 2 * v1 - ic1; ic2 = 2 * v2 - ic2;
            bp = v1;
            return v2;
        }
        float lp (float x) { float b; return process (x, b); }
        float bpf (float x) { float b; process (x, b); return b; }
    };

    struct OnePole
    {
        float z = 0, a = 0.5f;
        void setHz (float hz, float sr) { a = 1.0f - std::exp (-twoPi * hz / sr); }
        float lp (float x) { z += a * (x - z); return z; }
    };

    // pilovity priebeh s PolyBLEP (bez aliasu)
    inline float polyBlep (float t, float dt)
    {
        if (t < dt) { t /= dt; return t + t - t * t - 1.0f; }
        if (t > 1.0f - dt) { t = (t - 1.0f) / dt; return t * t + t + t + 1.0f; }
        return 0.0f;
    }

    // stereo chorus (2 moduolvane oneskorenia)
    struct Chorus
    {
        std::vector<float> bl, br; int w = 0, n = 1; float sr = 48000, ph1 = 0, ph2 = 0.25f;
        void prepare (float rate) { sr = rate; n = (int) (rate * 0.05f) + 4; bl.assign ((size_t) n, 0); br.assign ((size_t) n, 0); w = 0; }
        float tap (const std::vector<float>& b, float delaySmp) const
        {
            float rp = (float) w - delaySmp; while (rp < 0) rp += (float) n;
            const int i0 = (int) rp; const float f = rp - (float) i0;
            return b[(size_t) i0] + f * (b[(size_t) ((i0 + 1) % n)] - b[(size_t) i0]);
        }
        void process (float& l, float& r, float mix, float depthMs, float rate = 0.31f)
        {
            bl[(size_t) w] = l; br[(size_t) w] = r;
            ph1 += rate / sr; if (ph1 >= 1) ph1 -= 1; ph2 += rate * 1.39f / sr; if (ph2 >= 1) ph2 -= 1;
            const float base = 0.012f * sr, dep = depthMs * 0.001f * sr;
            const float d1 = base + dep * (0.5f + 0.5f * std::sin (twoPi * ph1));
            const float d2 = base * 1.3f + dep * (0.5f + 0.5f * std::sin (twoPi * ph2 + 1.7f));
            const float wl = tap (bl, d1) * 0.7f + tap (br, d2) * 0.3f;
            const float wr = tap (br, d1 * 1.07f) * 0.7f + tap (bl, d2 * 0.93f) * 0.3f;
            if (++w >= n) w = 0;
            l = l * (1 - 0.5f * mix) + wl * mix; r = r * (1 - 0.5f * mix) + wr * mix;
        }
    };

    // zber vzoriek pre zobrazenie spektra (zapisuje audio vlakno, cita rozhranie; len zvoleny bod)
    struct Analyzer
    {
        static constexpr int N = 8192;
        std::atomic<int> tap { -1 };          // 0 vrstva A, 1 vrstva B, 2 cely zvuk, 3 dozvuk, -1 nic
        std::atomic<unsigned> w { 0 };
        std::atomic<float> rate { 48000 };
        float buf[N] {};
        void push (int t, float x) noexcept
        {
            if (tap.load (std::memory_order_relaxed) != t) return;
            const unsigned i = w.load (std::memory_order_relaxed);
            buf[i & (N - 1)] = x;
            w.store (i + 1, std::memory_order_release);
        }
        bool wants (int t) const noexcept { return tap.load (std::memory_order_relaxed) == t; }
    };

    // dozvuk: vstupna difuzia (4 allpass) + FDN s 8 linkami (Hadamard), v kazdej linke allpass (hustota ozvien),
    // jemna modulacia vsetkych liniek a tlmenie vysok - bez kovoveho "spievania"
    struct Reverb
    {
        struct Ap { std::vector<float> b; int w = 0, len = 1; float g = 0.6f;
            void init (int n, float gain) { b.assign ((size_t) n + 1, 0); len = n; w = 0; g = gain; }
            float process (float x) { const float y = b[(size_t) w]; const float v = x + g * y; b[(size_t) w] = v; if (++w >= len) w = 0; return y - g * v; } };
        struct Line { std::vector<float> b; int w = 0, len = 1; float lp = 0, ph = 0, rate = 0.3f; Ap ap; };
        std::array<Line, 8> lines; std::array<Ap, 4> aps; std::vector<float> pre; int pw = 0, preLen = 1;
        float sr = 48000, size = -1, damp = 0.4f, g[8] {}, dampA = 0.5f;
        void prepare (float rate)
        {
            sr = rate;
            static const float apMs[4] = { 4.77f, 3.59f, 12.73f, 9.31f };
            for (int i = 0; i < 4; ++i) aps[(size_t) i].init ((int) (apMs[i] * 0.001f * sr), 0.7f);
            static const float rates[8] = { 0.13f, 0.21f, 0.29f, 0.37f, 0.17f, 0.43f, 0.11f, 0.33f };
            static const float inAp[8] = { 2.31f, 3.17f, 1.93f, 4.07f, 2.71f, 3.61f, 1.57f, 4.43f };
            for (int i = 0; i < 8; ++i)
            {
                auto& l = lines[(size_t) i];
                l.b.assign ((size_t) (0.25f * sr), 0); l.w = 0; l.lp = 0; l.ph = (float) i / 8.0f; l.rate = rates[i];
                l.ap.init ((int) (inAp[i] * 0.001f * sr), 0.5f);
            }
            pre.assign ((size_t) (0.06f * sr) + 2, 0); pw = 0;
            const float s0 = size < 0 ? 0.5f : size; size = -1;
            setSize (s0, damp);
        }
        void setSize (float s, float d)
        {
            size = s; damp = d;
            static const float ms[8] = { 31.3f, 37.9f, 44.9f, 51.1f, 58.7f, 66.3f, 73.9f, 83.1f };
            const float scale = 0.5f + 1.1f * s, t60 = 0.8f + 4.2f * s * s + 0.6f * s;
            for (int i = 0; i < 8; ++i)
            {
                auto& l = lines[(size_t) i];
                l.len = std::min ((int) l.b.size() - 16, (int) (ms[i] * scale * 0.001f * sr));
                const float total = (float) (l.len + l.ap.len);
                g[i] = std::pow (10.0f, -3.0f * total / (t60 * sr));
            }
            dampA = 1.0f - std::exp (-twoPi * (9000.0f - 7000.0f * d) / sr);
            preLen = std::min ((int) pre.size() - 1, (int) ((0.008f + 0.02f * s) * sr));
        }
        void process (float inL, float inR, float& outL, float& outR)
        {
            pre[(size_t) pw] = 0.5f * (inL + inR);
            int rp = pw - preLen; if (rp < 0) rp += (int) pre.size();
            float x = pre[(size_t) rp]; if (++pw >= (int) pre.size()) pw = 0;
            const float side = 0.5f * (inL - inR);
            for (auto& a : aps) x = a.process (x);
            float o[8];
            for (int i = 0; i < 8; ++i)
            {
                auto& l = lines[(size_t) i];
                l.ph += l.rate / sr; if (l.ph >= 1) l.ph -= 1;
                const float d = (float) l.len + 2.5f * std::sin (twoPi * l.ph);
                float r = (float) l.w - d; while (r < 0) r += (float) l.b.size();
                const int i0 = (int) r; const float f = r - (float) i0;
                const float s = l.b[(size_t) i0] + f * (l.b[(size_t) ((i0 + 1) % (int) l.b.size())] - l.b[(size_t) i0]);
                l.lp += dampA * (s - l.lp);
                o[i] = l.ap.process (l.lp) * g[i];
            }
            float h[8];
            for (int i = 0; i < 8; i += 2) { h[i] = o[i] + o[i + 1]; h[i + 1] = o[i] - o[i + 1]; }
            for (int i = 0; i < 8; i += 4) { float a = h[i], b = h[i + 1], c = h[i + 2], e = h[i + 3]; h[i] = a + c; h[i + 1] = b + e; h[i + 2] = a - c; h[i + 3] = b - e; }
            for (int i = 0; i < 4; ++i) { float a = h[i], b = h[i + 4]; h[i] = a + b; h[i + 4] = a - b; }
            const float nrm = 0.35355339f;
            // vstup s roznymi znamienkami: rovnaky vstup do vsetkych liniek je vlastny vektor Hadamardovej matice
            static const float inG[8] = { 0.62f, -0.48f, 0.55f, 0.41f, -0.6f, 0.52f, -0.45f, -0.58f };
            static const float sdG[8] = { 0.3f, 0.25f, -0.28f, 0.22f, 0.3f, -0.26f, -0.24f, 0.29f };
            for (int i = 0; i < 8; ++i)
            {
                auto& l = lines[(size_t) i];
                l.b[(size_t) l.w] = h[i] * nrm + x * inG[i] + side * sdG[i];
                if (++l.w >= (int) l.b.size()) l.w = 0;
            }
            outL = (o[0] - 0.8f * o[1] + o[2] + 0.6f * o[3] - o[4] + 0.7f * o[5] + o[6] - 0.5f * o[7]) * 0.42f;
            outR = (0.6f * o[0] + o[1] - 0.7f * o[2] + o[3] + 0.5f * o[4] - o[5] - 0.8f * o[6] + o[7]) * 0.42f;
        }
    };
}
