// =============================================================================
//  Korgus SuperVerb - algorithmic reverb (single header, C++17, no dependencies)
//
//  Signal flow (all wet):
//    in -> sanitize -> low-cut (150 Hz) + DC block -> pre-delay (0..250 ms)
//       -> early reflections : 2 x 8 stereo taps (size-scaled) -> 2 allpass / ch
//       -> input diffusion   : 4 allpass / ch
//       -> late tail         : 16-line FDN, Hadamard mixing matrix,
//                              cubic-Lagrange interpolated, LFO + random
//                              modulated delay lines, per-line 3-band decay
//                              (low / mid / high T60), per-line allpass,
//                              soft saturator on every line write
//       -> high-cut -> DC block -> width (M/S) -> soft limiter (|wet| <= 2)
//
//  Safety properties:
//    * loop gain < 1 at every frequency for every parameter value
//    * every delay line write is soft-limited to |x| <= 4  (state is bounded
//      by construction, energy can never build up)
//    * non-finite input samples and parameters are replaced, state is checked
//      every control tick and reset if it ever becomes non-finite
//    * denormal-safe without FTZ/DAZ (tiny constant injected in the loops)
//    * no allocation / locks / system calls after prepare()
//    * all parameter changes are smoothed, delay changes are slewed (no clicks)
//
//  Six user parameters, all normalised 0..1:
//    size, decay, damping, predelay, width  (setParams)   + mix (process)
// =============================================================================
#pragma once

#include <vector>
#include <array>
#include <cmath>
#include <algorithm>
#include <cstdint>

namespace korgus
{

class SuperVerb
{
public:
    static constexpr int kLines = 16;
    static constexpr int kErTaps = 8;

    // ---- parameter ranges (for labels / displays) ---------------------------
    static float decayToSeconds (float decay01) noexcept   { return 0.3f * std::pow (40.0f, clamp01 (decay01)); }          // 0.3 .. 12 s
    static float secondsToDecay (float seconds) noexcept   { return clamp01 (std::log (std::max (0.3f, seconds) / 0.3f) / std::log (40.0f)); }
    static float predelayToMs (float pre01) noexcept       { return 250.0f * clamp01 (pre01); }                             // 0 .. 250 ms
    static float sizeToScale (float size01) noexcept       { return 0.2f * std::pow (5.0f, clamp01 (size01)); }             // 0.2 .. 1.0

    // ---- setup --------------------------------------------------------------
    void prepare (double sampleRate, int /*maxBlock*/ = 512)
    {
        double s = sampleRate;
        if (! (s >= 8000.0)) s = 44100.0;
        if (s > 384000.0) s = 384000.0;
        sr = s;

        ctl = 32;
        while ((double) ctl * 48000.0 < sr * 32.0 * 0.75 && ctl < 256) ctl *= 2;   // 32 @48k, 64 @96k, 128 @192k
        dt = (double) ctl / sr;

        // pre-delay / early reflection buffer
        preMask = allocPow2 (preL, (int) (sr * 0.36) + 64);
        allocPow2 (preR, (int) (sr * 0.36) + 64);

        // FDN lines
        for (int i = 0; i < kLines; ++i)
        {
            lineMask = allocPow2 (line[(size_t) i], (int) (sr * (kLineMs[kLines - 1] * 0.001 + 0.004)) + 64);
            int n = (int) (kApMs[i] * 0.001 * sr); n |= 1;
            lap[(size_t) i].init (n);
            apSec[i] = (double) n / sr;
        }
        for (int i = 0; i < 4; ++i)
        {
            difL[(size_t) i].init ((int) (kDifMsL[i] * 0.001 * sr) | 1);
            difR[(size_t) i].init ((int) (kDifMsR[i] * 0.001 * sr) | 1);
        }
        for (int i = 0; i < 2; ++i)
        {
            erApL[(size_t) i].init ((int) (kErApMsL[i] * 0.001 * sr) | 1);
            erApR[(size_t) i].init ((int) (kErApMsR[i] * 0.001 * sr) | 1);
        }

        // fixed filters
        lcA   = onePole (150.0);     // wet low-cut (mud / rumble)
        dcA   = onePole (20.0);      // DC block on input
        odA   = onePole (8.0);       // DC block on output
        lowA  = onePole (250.0);     // bass-multiply crossover
        const double kr = 1.0 - std::exp (-6.283185307 * 1.2 * dt);   // random LFO bandwidth ~1.2 Hz (2 poles)
        rndK = (float) kr;
        rndNorm = (float) (1.0 / std::sqrt ((kr / (2.0 - kr)) / 3.0 * 0.5 + 1e-12));
        mixK = (float) (1.0 - std::exp (-1.0 / (0.02 * sr)));

        reset();
    }

    // Clears all audio state (parameters are kept and re-applied without smoothing).
    void reset() noexcept
    {
        std::fill (preL.begin(), preL.end(), 0.0f);
        std::fill (preR.begin(), preR.end(), 0.0f);
        for (auto& l : line) std::fill (l.begin(), l.end(), 0.0f);
        for (auto& a : lap) a.clear();
        for (auto& a : difL) a.clear();
        for (auto& a : difR) a.clear();
        for (auto& a : erApL) a.clear();
        for (auto& a : erApR) a.clear();
        pw = 0; lw = 0;
        for (int i = 0; i < kLines; ++i)
        {
            lpH[i] = lpL[i] = 0.0f; o[i] = 0.0f;
            lfoPh[i] = (double) ((i * 7) % kLines) / (double) kLines;
            rnd1[i] = rnd2[i] = 0.0f;
        }
        lcL = lcR = dcL = dcR = odL = odR = hcL = hcR = erLpL = erLpR = 0.0f;
        guard = 0.0f;
        ctlCount = 0; snap = true; mixSnap = true;
    }

    // ---- parameters (all 0..1; safe to call from the audio thread at any rate)
    void setParams (float size, float decay, float damping, float predelay, float width) noexcept
    {
        tSize = san (size, 0.5f); tDecay = san (decay, 0.5f); tDamp = san (damping, 0.5f);
        tPre = san (predelay, 0.0f); tWidth = san (width, 1.0f);
    }

    // Mapping of the old Korgus Inspire reverb (rv_size 0..1, rv_damp 0..1, rv_pre in ms).
    // Old engine: T60 = 0.8 + 0.6 s + 4.2 s^2 seconds, delay scale 0.5 + 1.1 s.
    void setFromLegacy (float size, float damp, float preMs, float width = 1.0f) noexcept
    {
        const float s = san (size, 0.5f), d = san (damp, 0.4f);
        const float t60 = 0.8f + 0.6f * s + 4.2f * s * s;
        setParams (0.28f + 0.72f * s, secondsToDecay (t60), 0.15f + 0.7f * d,
                   san (preMs / 250.0f, 0.0f), width);
    }

    // Reverb tail length in seconds for the current settings (for host "tail" reporting).
    float getTailSeconds() const noexcept { return decayToSeconds (tDecay) * 1.2f + predelayToMs (tPre) * 0.001f + 0.2f; }

    // ---- processing ---------------------------------------------------------
    // 100 % wet. inR may be nullptr (mono in). In/out buffers may alias.
    void processWet (const float* inL, const float* inR, float* wetL, float* wetR, int n) noexcept
    {
        if (inL == nullptr || wetL == nullptr || wetR == nullptr || n <= 0 || line[0].empty()) return;
        if (inR == nullptr) inR = inL;
        for (int i = 0; i < n; ++i)
        {
            float l, r;
            processSample (inL[i], inR[i], l, r);
            wetL[i] = l; wetR[i] = r;
        }
    }

    // In place with dry/wet mix (0..1, equal-power: dry = cos, wet = sin; dry untouched at mix 0).
    void process (float* L, float* R, int n, float mix) noexcept
    {
        if (L == nullptr || n <= 0 || line[0].empty()) return;
        if (R == nullptr) R = L;
        const float m = san (mix, 0.25f);
        const float gdT = std::cos (1.5707963f * m), gwT = std::sin (1.5707963f * m);
        if (mixSnap) { gDry = gdT; gWet = gwT; mixSnap = false; }
        for (int i = 0; i < n; ++i)
        {
            gDry += mixK * (gdT - gDry); gWet += mixK * (gwT - gWet);
            const float dl = L[i], dr = R[i];
            float wl, wr;
            processSample (dl, dr, wl, wr);
            if (R != L) { L[i] = dl * gDry + wl * gWet; R[i] = dr * gDry + wr * gWet; }
            else        { L[i] = dl * gDry + 0.5f * (wl + wr) * gWet; }
        }
        if (m <= 0.0f && std::abs (gDry - 1.0f) < 1e-6f) { gDry = 1.0f; gWet = 0.0f; }
    }

    // One stereo sample, 100 % wet. (Use this from a per-sample effect chain.)
    inline void processSample (float inL, float inR, float& outL, float& outR) noexcept
    {
        if (ctlCount <= 0) { control(); ctlCount = ctl; }
        --ctlCount;

        // ---- input: sanitize, low-cut, DC block --------------------------------
        float xl = clean (inL), xr = clean (inR);
        lcL += lcA * (xl - lcL) + kDn2; xl -= lcL;      // (+kDn2 keeps the states out of the denormal range)
        lcR += lcA * (xr - lcR) + kDn2; xr -= lcR;
        dcL += dcA * (xl - dcL) + kDn2; xl -= dcL;
        dcR += dcA * (xr - dcR) + kDn2; xr -= dcR;
        xl += kDn; xr += kDn;

        float* const bl = preL.data(); float* const br = preR.data();
        const int pm = preMask;
        bl[pw] = xl; br[pw] = xr;

        // ---- pre-delay read ------------------------------------------------------
        float pl, pr;
        {
            const int di = (int) preD; const float fr = (float) (preD - (double) di);
            const int i0 = (pw - di) & pm, i1 = (i0 - 1) & pm;
            pl = bl[i0] + fr * (bl[i1] - bl[i0]);
            pr = br[i0] + fr * (br[i1] - br[i0]);
            preD += preInc;
        }

        // ---- early reflections ----------------------------------------------------
        float el = 0.0f, er = 0.0f;
        for (int k = 0; k < kErTaps; ++k)
        {
            {
                const double d = erDL[k]; erDL[k] = d + erIncL[k];
                const int di = (int) d; const float fr = (float) (d - (double) di);
                const int i0 = (pw - di) & pm, i1 = (i0 - 1) & pm;
                const float* b = (k & 1) ? br : bl;
                el += kErGL[k] * (b[i0] + fr * (b[i1] - b[i0]));
            }
            {
                const double d = erDR[k]; erDR[k] = d + erIncR[k];
                const int di = (int) d; const float fr = (float) (d - (double) di);
                const int i0 = (pw - di) & pm, i1 = (i0 - 1) & pm;
                const float* b = (k & 1) ? bl : br;
                er += kErGR[k] * (b[i0] + fr * (b[i1] - b[i0]));
            }
        }
        pw = (pw + 1) & pm;
        erLpL += erLpA * (el - erLpL); el = erLpL;
        erLpR += erLpA * (er - erLpR); er = erLpR;
        el = erApL[0].tick (el, 0.55f); el = erApL[1].tick (el, 0.45f);
        er = erApR[0].tick (er, 0.55f); er = erApR[1].tick (er, 0.45f);

        // ---- input diffusion ------------------------------------------------------
        float dl = pl + kErToLate * el, dr = pr + kErToLate * er;
        dl = difL[0].tick (dl, difG1); dl = difL[1].tick (dl, difG1);
        dl = difL[2].tick (dl, difG2); dl = difL[3].tick (dl, difG2);
        dr = difR[0].tick (dr, difG1); dr = difR[1].tick (dr, difG1);
        dr = difR[2].tick (dr, difG2); dr = difR[3].tick (dr, difG2);
        inGain += inGainInc;
        dl *= inGain; dr *= inGain;

        // ---- FDN: read, damp, allpass --------------------------------------------
        const int lm = lineMask, w = lw;
        const float aH = hiA, aL = lowA;
        float g0 = 0.0f;
        for (int i = 0; i < kLines; ++i)
        {
            const double d = lineD[i]; lineD[i] = d + lineInc[i];
            const int di = (int) d; const float f = (float) (d - (double) di);
            const float* b = line[(size_t) i].data();
            const int i1 = (w - di) & lm;
            const float s0 = b[(i1 + 1) & lm], s1 = b[i1], s2 = b[(i1 - 1) & lm], s3 = b[(i1 - 2) & lm];
            // 3rd-order Lagrange, fraction f between s1 and s2 (passive: |H| <= 1)
            const float D = f + 1.0f, Dm1 = f, Dm2 = f - 1.0f, Dm3 = f - 2.0f;
            const float s = -Dm1 * Dm2 * Dm3 * 0.16666667f * s0 + D * Dm2 * Dm3 * 0.5f * s1
                            - D * Dm1 * Dm3 * 0.5f * s2 + D * Dm1 * Dm2 * 0.16666667f * s3;
            // 3-band decay: gHi*x + (gMid-gHi)*LP_hi(x) + (gLow-gMid)*LP_low(x), all terms >= 0, sum = gLow < 1
            lpH[i] += aH * (s - lpH[i]);
            lpL[i] += aL * (s - lpL[i]);
            const float y = gHi[i] * s + cMid[i] * lpH[i] + cLow[i] * lpL[i];
            const float v = lap[(size_t) i].tick (y, kLoopApG);
            o[i] = v; g0 += v;
        }
        guard = g0;

        // ---- output taps (two orthogonal sign patterns) -----------------------------
        float tl = 0.0f, tr = 0.0f;
        for (int i = 0; i < kLines; ++i) { tl += kOutL[i] * o[i]; tr += kOutR[i] * o[i]; }

        // ---- Hadamard mix, inject input, saturate, write ----------------------------
        float h[kLines];
        for (int i = 0; i < kLines; i += 2) { h[i] = o[i] + o[i + 1]; h[i + 1] = o[i] - o[i + 1]; }
        for (int i = 0; i < kLines; i += 4)
        {
            const float a = h[i], b2 = h[i + 1], c = h[i + 2], e = h[i + 3];
            h[i] = a + c; h[i + 1] = b2 + e; h[i + 2] = a - c; h[i + 3] = b2 - e;
        }
        for (int i = 0; i < kLines; i += 8)
            for (int j = 0; j < 4; ++j) { const float a = h[i + j], b2 = h[i + j + 4]; h[i + j] = a + b2; h[i + j + 4] = a - b2; }
        for (int j = 0; j < 8; ++j) { const float a = h[j], b2 = h[j + 8]; h[j] = a + b2; h[j + 8] = a - b2; }
        for (int i = 0; i < kLines; ++i)
        {
            const float in = (i & 1) ? dr : dl;
            line[(size_t) i][(size_t) w] = soft (h[i] * 0.25f + kInSign[i] * in + kDn, 2.0f, 4.0f);
        }
        lw = (w + 1) & lm;

        // ---- wet output: balance, high-cut, DC block, width, limiter ----------------
        float wl = tl * kLateGain + el * kErGain;
        float wr = tr * kLateGain + er * kErGain;
        hcL += hcA * (wl - hcL); wl = hcL;
        hcR += hcA * (wr - hcR); wr = hcR;
        odL += odA * (wl - odL); wl -= odL;
        odR += odA * (wr - odR); wr -= odR;
        gM += gMInc; gS += gSInc;
        const float mid = (wl + wr) * gM, side = (wl - wr) * gS;
        wl = soft (mid + side, 1.0f, 2.0f);
        wr = soft (mid - side, 1.0f, 2.0f);
        outL = (std::abs (wl) <= 2.0f) ? wl : 0.0f;      // also catches NaN
        outR = (std::abs (wr) <= 2.0f) ? wr : 0.0f;
    }

#ifdef KORGUS_SUPERVERB_DEBUG
    int debugCountSubnormals() const
    {
        int n = 0;
        auto chk = [&n] (float x) { if (x != 0.0f && std::abs (x) < 1.17549435e-38f) ++n; };
        for (auto& l : line) for (float x : l) chk (x);
        for (auto& a : lap) for (float x : a.b) chk (x);
        for (auto& a : difL) for (float x : a.b) chk (x);
        for (auto& a : difR) for (float x : a.b) chk (x);
        for (auto& a : erApL) for (float x : a.b) chk (x);
        for (auto& a : erApR) for (float x : a.b) chk (x);
        for (float x : preL) chk (x);
        for (float x : preR) chk (x);
        for (int i = 0; i < kLines; ++i) { chk (lpH[i]); chk (lpL[i]); chk (o[i]); }
        chk (lcL); chk (lcR); chk (dcL); chk (dcR); chk (odL); chk (odR); chk (hcL); chk (hcR); chk (erLpL); chk (erLpR);
        return n;
    }
    float debugMaxState() const
    {
        float m = 0;
        for (auto& l : line) for (float x : l) m = std::max (m, std::abs (x));
        return m;
    }
#endif

private:
    // ---- constants -----------------------------------------------------------
    static constexpr float kDn = 1.0e-18f;          // anti-denormal bias (-360 dB)
    static constexpr float kDn2 = 1.0e-21f;
    static constexpr float kLoopApG = 0.5f;
    static constexpr float kLateGain = 0.22f;
    static constexpr float kErGain = 0.18f;
    static constexpr float kErToLate = 0.35f;

    // FDN delay lengths in ms at size = 1 (multiplied by 0.2 .. 1.0)
    static constexpr double kLineMs[kLines] = { 43.13, 47.87, 52.31, 57.73, 62.91, 68.29, 74.17, 79.93,
                                                86.27, 92.89, 99.71, 107.33, 115.09, 124.31, 134.87, 148.73 };
    // in-loop allpass lengths in ms (fixed)
    static constexpr double kApMs[kLines] = { 3.11, 5.87, 4.19, 7.93, 3.61, 6.89, 4.73, 9.71,
                                              3.37, 6.37, 4.43, 8.63, 3.89, 7.39, 5.21, 9.13 };
    static constexpr double kLfoHz[kLines] = { 0.23, 0.71, 0.37, 0.97, 0.29, 0.83, 0.43, 1.09,
                                               0.19, 0.61, 0.53, 0.89, 0.31, 0.67, 0.47, 1.03 };
    static constexpr double kDifMsL[4] = { 4.71, 3.57, 12.37, 9.13 };
    static constexpr double kDifMsR[4] = { 4.93, 3.79, 11.83, 8.71 };
    static constexpr double kErApMsL[2] = { 2.13, 3.31 };
    static constexpr double kErApMsR[2] = { 2.29, 3.07 };
    // early reflection taps (ms at size = 1), odd taps come from the opposite input channel
    static constexpr double kErMsL[kErTaps] = { 7.3, 13.1, 19.7, 27.1, 35.9, 46.3, 58.1, 71.9 };
    static constexpr double kErMsR[kErTaps] = { 8.9, 11.7, 21.3, 25.3, 38.3, 44.1, 61.7, 68.3 };
    static constexpr float kErGL[kErTaps] = { 0.80f, -0.55f, 0.62f, 0.50f, -0.45f, 0.38f, 0.33f, -0.27f };
    static constexpr float kErGR[kErTaps] = { 0.80f, 0.52f, -0.60f, 0.48f, 0.44f, -0.40f, 0.30f, 0.28f };
    static constexpr float kInSign[kLines] = { 1, 1, -1, 1, -1, -1, 1, -1, 1, -1, 1, 1, -1, 1, -1, -1 };
    static constexpr float kOutL[kLines] = { 1, -1, 1, 1, -1, 1, -1, -1, 1, 1, -1, 1, -1, -1, 1, -1 };
    static constexpr float kOutR[kLines] = { 1, -1, -1, -1, -1, -1, -1, 1, -1, 1, -1, -1, 1, -1, -1, -1 };

    struct Ap
    {
        std::vector<float> b; int len = 1, w = 0;
        void init (int n) { len = std::max (1, n); b.assign ((size_t) len, 0.0f); w = 0; }
        void clear() noexcept { std::fill (b.begin(), b.end(), 0.0f); w = 0; }
        inline float tick (float x, float g) noexcept
        {
            const float y = b[(size_t) w]; const float v = x - g * y;
            b[(size_t) w] = v; if (++w >= len) w = 0;
            return y + g * v;
        }
    };

    // ---- helpers ---------------------------------------------------------------
    static float clamp01 (float x) noexcept { return x < 0.0f ? 0.0f : (x > 1.0f ? 1.0f : x); }
    static bool finite (float x) noexcept
    {
        union { float f; std::uint32_t u; } v; v.f = x;        // works with -ffast-math too
        return (v.u & 0x7f800000u) != 0x7f800000u;
    }
    static float san (float x, float def) noexcept { return finite (x) ? clamp01 (x) : def; }
    static inline float clean (float x) noexcept
    {
        if (! finite (x)) return 0.0f;
        return x < -4.0f ? -4.0f : (x > 4.0f ? 4.0f : x);
    }
    // transparent below T, smoothly limited to +-L above
    static inline float soft (float x, float T, float L) noexcept
    {
        const float a = std::abs (x);
        if (a <= T) return x;
        const float u = (a - T) / (L - T);
        return std::copysign (T + (L - T) * u / (1.0f + u), x);
    }
    float onePole (double hz) const noexcept { return (float) (1.0 - std::exp (-6.283185307 * std::min (hz, sr * 0.45) / sr)); }
    static int allocPow2 (std::vector<float>& v, int minLen)
    {
        int n = 64; while (n < minLen) n <<= 1;
        v.assign ((size_t) n, 0.0f);
        return n - 1;
    }
    inline float rnd() noexcept
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (std::int32_t) seed * (1.0f / 2147483648.0f);
    }

    // ---- control rate (every `ctl` samples): smoothing, coefficients, LFOs -------
    void control() noexcept
    {
        // state watchdog: anything non-finite or absurd -> clear everything
        if (! finite (guard) || std::abs (guard) > 1.0e4f || ! finite (hcL + hcR + odL + odR + lcL + lcR)) reset();

        const bool first = snap;
        bool dirty = first;
        if (first)
        {
            sSize = tSize; sDecay = tDecay; sDamp = tDamp; sWidth = tWidth;
            sPre = (double) tPre;
        }
        else
        {
            const float k = (float) (1.0 - std::exp (-dt / 0.06));
            const float kSize = (float) (1.0 - std::exp (-dt / 0.20));
            const float maxStep = (float) (3.0 * dt);                 // size slew limit (keeps delay glide < 1 sample/sample)
            float ds = kSize * (tSize - sSize);
            ds = ds > maxStep ? maxStep : (ds < -maxStep ? -maxStep : ds);
            if (std::abs (tSize - sSize) < 1e-5f) sSize = tSize; else { sSize += ds; dirty = true; }
            if (std::abs (tDecay - sDecay) < 1e-5f) sDecay = tDecay; else { sDecay += k * (tDecay - sDecay); dirty = true; }
            if (std::abs (tDamp - sDamp) < 1e-5f) sDamp = tDamp; else { sDamp += k * (tDamp - sDamp); dirty = true; }
            if (std::abs (tWidth - sWidth) < 1e-5f) sWidth = tWidth; else { sWidth += k * (tWidth - sWidth); }
            // pre-delay: one-pole + slew limit 0.5 sample / sample
            const double maxPre = 0.5 * dt / 0.25;                    // in normalised units (250 ms full scale)
            double dp = (1.0 - std::exp (-dt / 0.08)) * ((double) tPre - sPre);
            dp = dp > maxPre ? maxPre : (dp < -maxPre ? -maxPre : dp);
            sPre += dp;
        }

        if (dirty)
        {
            scale = 0.2 * std::pow (5.0, (double) sSize);
            const double t60 = 0.3 * std::pow (40.0, (double) sDecay);
            const double damp = (double) sDamp;
            const double hiRatio = 0.90 * std::pow (0.15, damp);                    // HF T60 / mid T60 : 0.90 .. 0.135
            const double lowMult = 1.0 + 0.2 * (double) sSize;                      // LF T60 / mid T60 : 1.0 .. 1.2
            hiA = onePole (8000.0 * std::pow (0.2, damp));                          // decay crossover 8 k .. 1.6 k
            hcA = onePole (20000.0 * std::pow (0.2, damp));                         // wet high-cut 20 k .. 4 k
            erLpA = onePole (14000.0 * std::pow (0.3, damp));
            double meanT = 0.0;
            for (int i = 0; i < kLines; ++i)
            {
                const double T = kLineMs[i] * 0.001 * scale + apSec[i];
                meanT += T;
                const double e = -6.907755279 * T / t60;                            // ln(10^-3) * T / T60
                float gm = (float) std::exp (e);
                float gh = (float) std::exp (e / hiRatio);
                float gl = (float) std::exp (e / lowMult);
                gl = std::min (gl, 0.9995f); gm = std::min (gm, gl); gh = std::min (gh, gm);
                gHi[i] = gh; cMid[i] = gm - gh; cLow[i] = gl - gm;
            }
            meanT /= (double) kLines;
            // loudness compensation: half-way between constant impulse level and constant steady-state level
            inGainT = (float) (1.6 * std::pow (meanT / t60, 0.25));
            const float dg = 0.60f + 0.12f * sSize;                                 // input diffusion amount
            difG1 = dg; difG2 = dg - 0.12f;
        }

        const double invCtl = 1.0 / (double) ctl;
        if (first) inGain = inGainT;
        inGainInc = (inGainT - inGain) * (float) invCtl;

        // width (constant power M/S)
        {
            const float w = sWidth, nrm = std::sqrt (2.0f / (1.0f + w * w));
            const float gm = 0.5f * nrm, gs = 0.5f * nrm * w;
            if (first) { gM = gm; gS = gs; }
            gMInc = (gm - gM) * (float) invCtl; gSInc = (gs - gS) * (float) invCtl;
        }

        // pre-delay and early reflection taps
        {
            const double maxD = (double) preMask - 8.0;
            const double pd = std::min (maxD, 1.0 + sPre * 0.25 * sr);
            if (first) preD = pd;
            preInc = (pd - preD) * invCtl;
            for (int k = 0; k < kErTaps; ++k)
            {
                const double a = std::min (maxD, pd + kErMsL[k] * 0.001 * scale * sr);
                const double b = std::min (maxD, pd + kErMsR[k] * 0.001 * scale * sr);
                if (first) { erDL[k] = a; erDR[k] = b; }
                erIncL[k] = (a - erDL[k]) * invCtl; erIncR[k] = (b - erDR[k]) * invCtl;
            }
        }

        // FDN delay modulation: sine LFO + smoothed random, per line
        {
            const double depthS = (0.00022 + 0.00028 * scale) * sr;      // 0.28 .. 0.50 ms sine
            const double depthR = (0.00016 + 0.00020 * scale) * sr;      // random part
            const double maxD = (double) lineMask - 8.0;
            for (int i = 0; i < kLines; ++i)
            {
                lfoPh[i] += kLfoHz[i] * dt; if (lfoPh[i] >= 1.0) lfoPh[i] -= 1.0;
                rnd1[i] += rndK * (rnd() - rnd1[i]);
                rnd2[i] += rndK * (rnd1[i] - rnd2[i]);
                float r = rnd2[i] * rndNorm; r = r > 1.5f ? 1.5f : (r < -1.5f ? -1.5f : r);
                double d = kLineMs[i] * 0.001 * scale * sr + depthS * std::sin (6.283185307 * lfoPh[i]) + depthR * (double) r;
                d = d < 4.0 ? 4.0 : (d > maxD ? maxD : d);
                if (first) lineD[i] = d;
                lineInc[i] = (d - lineD[i]) * invCtl;
            }
        }
        snap = false;
    }

    // ---- state -----------------------------------------------------------------
    double sr = 48000.0, dt = 32.0 / 48000.0;
    int ctl = 32, ctlCount = 0;
    bool snap = true, mixSnap = true;

    // targets / smoothed parameters
    float tSize = 0.5f, tDecay = 0.5f, tDamp = 0.5f, tPre = 0.0f, tWidth = 1.0f;
    float sSize = 0.5f, sDecay = 0.5f, sDamp = 0.5f, sWidth = 1.0f;
    double sPre = 0.0, scale = 0.45;

    // pre-delay + ER
    std::vector<float> preL, preR; int preMask = 0, pw = 0;
    double preD = 1.0, preInc = 0.0;
    double erDL[kErTaps] {}, erDR[kErTaps] {}, erIncL[kErTaps] {}, erIncR[kErTaps] {};
    std::array<Ap, 2> erApL, erApR;
    float erLpL = 0, erLpR = 0, erLpA = 0.5f;

    // diffusion
    std::array<Ap, 4> difL, difR;
    float difG1 = 0.66f, difG2 = 0.54f;

    // FDN
    std::array<std::vector<float>, kLines> line; int lineMask = 0, lw = 0;
    std::array<Ap, kLines> lap;
    double lineD[kLines] {}, lineInc[kLines] {}, lfoPh[kLines] {}, apSec[kLines] {};
    float lpH[kLines] {}, lpL[kLines] {}, o[kLines] {};
    float gHi[kLines] {}, cMid[kLines] {}, cLow[kLines] {};
    float rnd1[kLines] {}, rnd2[kLines] {};
    float hiA = 0.3f, lowA = 0.03f, rndK = 0.005f, rndNorm = 30.0f;
    float inGain = 0.5f, inGainT = 0.5f, inGainInc = 0.0f;
    std::uint32_t seed = 0x1234abcdu;

    // filters / output
    float lcA = 0.02f, dcA = 0.003f, odA = 0.001f, hcA = 0.7f;
    float lcL = 0, lcR = 0, dcL = 0, dcR = 0, odL = 0, odR = 0, hcL = 0, hcR = 0;
    float gM = 0.5f, gS = 0.5f, gMInc = 0, gSInc = 0;
    float gDry = 1.0f, gWet = 0.0f, mixK = 0.001f;
    float guard = 0.0f;
};

} // namespace korgus
