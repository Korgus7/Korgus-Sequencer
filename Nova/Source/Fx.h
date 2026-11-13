#pragma once
#include <juce_audio_basics/juce_audio_basics.h>
#include "Dsp.h"

// Efekty na celom zvuku Korgus Nova
namespace nova
{
    // biquad (RBJ) pre EQ
    struct Biquad
    {
        float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
        float process (float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
        void set (int type, float f, float q, float db, float sr)
        {
            const float A = std::pow (10.0f, db / 40.0f), w = twoPi * clampf (f, 20.0f, sr * 0.45f) / sr, cw = std::cos (w), sw = std::sin (w);
            float a0, B0, B1, B2, A1, A2;
            if (type == 0)        // dolny shelf
            {
                const float al = sw / 2.0f * std::sqrt ((A + 1 / A) * (1 / 0.8f - 1) + 2), sa = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) - (A - 1) * cw + sa); B1 = 2 * A * ((A - 1) - (A + 1) * cw); B2 = A * ((A + 1) - (A - 1) * cw - sa);
                a0 = (A + 1) + (A - 1) * cw + sa; A1 = -2 * ((A - 1) + (A + 1) * cw); A2 = (A + 1) + (A - 1) * cw - sa;
            }
            else if (type == 2)   // horny shelf
            {
                const float al = sw / 2.0f * std::sqrt ((A + 1 / A) * (1 / 0.8f - 1) + 2), sa = 2 * std::sqrt (A) * al;
                B0 = A * ((A + 1) + (A - 1) * cw + sa); B1 = -2 * A * ((A - 1) + (A + 1) * cw); B2 = A * ((A + 1) + (A - 1) * cw - sa);
                a0 = (A + 1) - (A - 1) * cw + sa; A1 = 2 * ((A - 1) - (A + 1) * cw); A2 = (A + 1) - (A - 1) * cw - sa;
            }
            else                  // spicka
            {
                const float al = sw / (2 * q);
                B0 = 1 + al * A; B1 = -2 * cw; B2 = 1 - al * A; a0 = 1 + al / A; A1 = -2 * cw; A2 = 1 - al / A;
            }
            b0 = B0 / a0; b1 = B1 / a0; b2 = B2 / a0; a1 = A1 / a0; a2 = A2 / a0;
        }
    };

    // 3-pasmovy EQ (basy shelf 150 Hz, stredy spicka, vysky shelf 2.5 kHz), stereo
    struct Eq3
    {
        Biquad lo[2], md[2], hi[2]; float last[4] { 1e9f, 0, 0, 0 };
        void reset() { for (int c = 0; c < 2; ++c) { lo[c].z1 = lo[c].z2 = md[c].z1 = md[c].z2 = hi[c].z1 = hi[c].z2 = 0; } last[0] = 1e9f; }
        bool update (float l, float m, float mf, float h, float sr)
        {
            if (l != last[0] || m != last[1] || mf != last[2] || h != last[3])
            {
                last[0] = l; last[1] = m; last[2] = mf; last[3] = h;
                for (int c = 0; c < 2; ++c) { lo[c].set (0, 150, 0.7f, l, sr); md[c].set (1, mf, 0.8f, m, sr); hi[c].set (2, 2500, 0.7f, h, sr); }
            }
            return l != 0 || m != 0 || h != 0;
        }
        void process (float& l, float& r) { l = hi[0].process (md[0].process (lo[0].process (l))); r = hi[1].process (md[1].process (lo[1].process (r))); }
    };

    struct FxSettings
    {
        float tube = 0; bool compOn = false; float comp = 0.35f;
        bool driveOn = false; float drive = 0.3f;
        bool chOn = false; float chRate = 0.6f, chDepth = 0.5f, chMix = 0.4f;
        bool dlOn = false; int dlSync = 0; float dlTime = 375, dlFb = 0.35f, dlTone = 8000, dlMix = 0.25f; bool dlPP = true;
        bool rvOn = true; float rvSize = 0.5f, rvDamp = 0.4f, rvMix = 0.2f;
        float width = 1, punch = 0.5f, eqLow = 0, eqMid = 0, eqMidF = 1000, eqHigh = 0;
        float rvEqLow = 0, rvEqMid = 0, rvEqMidF = 1000, rvEqHigh = 0;
        bool eqEnabled = true, rvEqEnabled = true;
        bool mfOn = false; int mfType = 0; float mfCut = 20000, mfReso = 0, mfDrive = 0;
        float gainDb = 0, volumeDb = 0;
        double bpm = 120;
    };

    class FxChain
    {
    public:
        void prepare (double sampleRate)
        {
            sr = (float) sampleRate;
            chorus.prepare (sr); reverb.prepare (sr);
            dlL.assign ((size_t) (sr * 2.6f), 0); dlR.assign ((size_t) (sr * 2.6f), 0); dw = 0;
            dlTimeSm = -1; compEnv = 0; grDb = 0; pf = ps = 0; mfL.reset(); mfR.reset(); mf2L.reset(); mf2R.reset();
            eq.reset(); rvEq.reset();
        }
        std::atomic<float> gainReduction { 0 };   // pre zobrazenie (dB)
        Analyzer* an = nullptr;                    // spektrum (nastavi Synth)

        void process (float* L, float* R, int n, const FxSettings& s)
        {
            const bool eqOn = eq.update (s.eqLow, s.eqMid, s.eqMidF, s.eqHigh, sr) && s.eqEnabled;
            const bool rvEqOn = rvEq.update (s.rvEqLow, s.rvEqMid, s.rvEqMidF, s.rvEqHigh, sr) && s.rvEqEnabled;
            const bool tapRv = an != nullptr && an->wants (3), tapOut = an != nullptr && an->wants (2);
            if (std::abs (s.rvSize - reverb.size) > 0.01f || std::abs (s.rvDamp - reverb.damp) > 0.01f) reverb.setSize (s.rvSize, s.rvDamp);
            // delay: cas zo synchra s tempom
            float dTime = s.dlTime * 0.001f;
            if (s.dlSync > 0)
            {
                static const float beats[6] = { 0, 1.0f, 0.75f, 0.5f, 1.0f / 3.0f, 0.25f };
                dTime = (float) (60.0 / juce::jmax (20.0, s.bpm)) * beats[s.dlSync];
            }
            const float dTarget = clampf (dTime * sr, 16.0f, (float) dlL.size() - 4.0f);
            if (dlTimeSm < 0) dlTimeSm = dTarget;
            dlTone.setHz (s.dlTone, sr);
            mfL.set (s.mfCut, 0.6f + 9.0f * s.mfReso * s.mfReso, sr); mfR.set (s.mfCut, 0.6f + 9.0f * s.mfReso * s.mfReso, sr);
            const float pre = dbToGain (s.gainDb), post = dbToGain (s.volumeDb);
            const float sm = 1.0f - std::exp (-1.0f / (0.03f * sr));
            const float thr = -6.0f - 24.0f * s.comp, ratio = 3.0f;
            const float aC = std::exp (-1.0f / (0.01f * sr)), rC = std::exp (-1.0f / (0.12f * sr));
            const float pfA = std::exp (-1.0f / (0.001f * sr)), pfR = std::exp (-1.0f / (0.02f * sr)), psA = std::exp (-1.0f / (0.025f * sr)), psR = std::exp (-1.0f / (0.12f * sr));
            const float punchK = (s.punch - 0.5f) * 1.6f;
            float grMax = 0;
            for (int i = 0; i < n; ++i)
            {
                rvSm += sm * ((s.rvOn ? s.rvMix : 0.0f) - rvSm);
                chSm += sm * ((s.chOn ? s.chMix : 0.0f) - chSm);
                dlSm += sm * ((s.dlOn ? s.dlMix : 0.0f) - dlSm);
                wSm += sm * (s.width - wSm);
                float l = L[i] * pre, r = R[i] * pre;
                // tube: analogove teplo (jemna saturacia)
                if (s.tube > 0.001f)
                {
                    const float d = 1.0f + 1.5f * s.tube, nrm = 1.0f / std::tanh (d);
                    l = (1 - s.tube) * l + s.tube * std::tanh (l * d) * nrm * 0.8f;
                    r = (1 - s.tube) * r + s.tube * std::tanh (r * d) * nrm * 0.8f;
                }
                if (s.compOn)
                {
                    const float lev = std::max (std::abs (l), std::abs (r));
                    compEnv = lev > compEnv ? aC * compEnv + (1 - aC) * lev : rC * compEnv + (1 - rC) * lev;
                    const float db = juce::Decibels::gainToDecibels (compEnv, -100.0f);
                    const float gr = db > thr ? (db - thr) * (1.0f - 1.0f / ratio) : 0.0f;
                    grMax = std::max (grMax, gr);
                    const float g = dbToGain (-gr + gr * 0.5f);   // polovicu stisenia vratime (makeup)
                    l *= g; r *= g;
                }
                if (s.driveOn && s.drive > 0.001f)
                {
                    const float d = 1.0f + 9.0f * s.drive, nrm = 1.0f / std::tanh (d);
                    l = std::tanh (l * d) * nrm * (1.0f - 0.4f * s.drive); r = std::tanh (r * d) * nrm * (1.0f - 0.4f * s.drive);
                }
                if (eqOn) eq.process (l, r);
                if (chSm > 0.001f) chorus.process (l, r, chSm, 1.0f + 6.0f * s.chDepth, s.chRate);
                if (dlSm > 0.001f || s.dlOn)
                {
                    dlTimeSm += 0.0005f * (dTarget - dlTimeSm);
                    float rp = (float) dw - dlTimeSm; while (rp < 0) rp += (float) dlL.size();
                    const int i0 = (int) rp; const float fr = rp - (float) i0; const int i1 = (i0 + 1) % (int) dlL.size();
                    const float yl = dlL[(size_t) i0] + fr * (dlL[(size_t) i1] - dlL[(size_t) i0]);
                    const float yr = dlR[(size_t) i0] + fr * (dlR[(size_t) i1] - dlR[(size_t) i0]);
                    const float fbl = dlTone.lp (yl), fbr = yr * 0.8f + 0.2f * fbl;
                    if (s.dlPP) { dlL[(size_t) dw] = 0.5f * (l + r) + fbr * s.dlFb; dlR[(size_t) dw] = fbl * s.dlFb; }
                    else { dlL[(size_t) dw] = l + fbl * s.dlFb; dlR[(size_t) dw] = r + fbr * s.dlFb; }
                    if (++dw >= (int) dlL.size()) dw = 0;
                    l += yl * dlSm; r += yr * dlSm;
                }
                if (rvSm > 0.0005f || s.rvOn)
                {
                    float rl, rr; reverb.process (l, r, rl, rr);
                    if (rvEqOn) rvEq.process (rl, rr);
                    if (tapRv) an->push (3, 0.5f * (rl + rr));
                    l += rl * rvSm; r += rr * rvSm;
                }
                const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * wSm;
                l = mid + side; r = mid - side;
                if (std::abs (punchK) > 0.01f)
                {
                    const float a = std::abs (mid);
                    pf = a > pf ? pfA * pf + (1 - pfA) * a : pfR * pf + (1 - pfR) * a;
                    ps = a > ps ? psA * ps + (1 - psA) * a : psR * ps + (1 - psR) * a;
                    const float g = clampf (std::pow ((pf + 1e-6f) / (ps + 1e-6f), punchK), 0.4f, 2.5f);
                    l *= g; r *= g;
                }
                if (s.mfOn)
                {
                    if (s.mfDrive > 0.001f) { const float d = 1.0f + 6.0f * s.mfDrive; l = std::tanh (l * d) / std::tanh (d); r = std::tanh (r * d) / std::tanh (d); }
                    float bl, br;
                    const float lpL = mfL.process (l, bl), lpR = mfR.process (r, br);
                    if (s.mfType == 0) { l = lpL; r = lpR; } else { l = l - lpL; r = r - lpR; }
                }
                l *= post; r *= post;
                auto clip = [] (float x) { const float a = std::abs (x); return a < 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x); };
                L[i] = clip (l); R[i] = clip (r);
                if (tapOut) an->push (2, 0.5f * (L[i] + R[i]));
            }
            grDb = std::max (grMax, grDb * 0.9f);
            gainReduction.store (grDb);
        }

    private:
        float sr = 48000;
        Chorus chorus; Reverb reverb;
        std::vector<float> dlL, dlR; int dw = 0; float dlTimeSm = -1; OnePole dlTone;
        Eq3 eq, rvEq;
        Svf mfL, mfR, mf2L, mf2R;
        float rvSm = 0, chSm = 0, dlSm = 0, wSm = 1, compEnv = 0, grDb = 0, pf = 0, ps = 0;
    };
}
