// =============================================================================
//  Korgus Master - one-knob-per-idea stereo mastering processor
//  (single header, C++17, no dependencies)
//
//  Inspired by the TC Electronic System 6000 MD3 chain (multiband dynamics, EQ,
//  soft clip, brickwall limiter, loudness) but with six beginner controls.
//
//  Signal flow:
//    in -> sanitize -> [input LUFS meter, BS.1770-4]
//       -> rumble high-pass (24 Hz, 2nd order)
//       -> tone EQ : low shelf 100 Hz, "mud" bell 300 Hz, presence bell 3 kHz,
//                    air shelf 10 kHz, tilt (pivot 1 kHz) incl. "smart tilt"
//                    (slow correction of the measured high/mid balance)
//       -> 4-band Linkwitz-Riley (LR4) crossover 120 Hz / 800 Hz / 5 kHz with
//          all-pass phase compensation (bands sum to an all-pass = flat magnitude)
//       -> per band: stereo-linked RMS compressor, soft knee, threshold relative
//          to the learned long-term band level (works at any input level),
//          program-dependent attack + two-stage release, adaptive make-up
//       -> per band M/S: bass mono (band 1 side = 0), side widening above
//       -> automatic loudness gain (one static gain learned from the gated
//          integrated input loudness + the measured loudness loss of the chain)
//       -> soft clipper, 4x oversampled, only the clip residual is filtered
//          (bit-transparent below its threshold)
//       -> look-ahead true-peak limiter (4x oversampled inter-sample detection, 2 ms,
//          sliding-max + double box-car smoothing, two-stage release)
//       -> hard safety clamp at the ceiling -> [output LUFS meter]
//       -> optional loudness-matched bypass (delay compensated)
//
//  Safety properties:
//    * non-finite input is replaced, input is clamped to +-16
//    * all filters are fixed-topology stable biquads; state is checked every
//      control tick and cleared if it ever becomes non-finite
//    * the limiter gain is <= the required gain for every sample by construction
//      and the output is hard-clamped at the ceiling afterwards
//    * the automatic gain is rate limited and clamped (+-24 dB), its feedback
//      term (measured loss) is clamped to -6..+9 dB -> it cannot run away
//    * denormal-safe without FTZ/DAZ (tiny bias in every recursive state)
//    * no allocation / locks / system calls after prepare()
//    * every parameter is smoothed (no zipper noise), latency is constant
//
//  Six user controls: style (choice) + five knobs, all normalised 0..1.
//
//  Latency: 24 (clipper) + 24 (true-peak detector) + 2 ms look-ahead samples,
//  constant for a given sample rate (144 @ 48 kHz) -> getLatencySamples().
//
//  Typical use:
//    korgus::KorgusMaster m;
//    m.prepare (sampleRate, maxBlock, 2);            // allocates; report getLatencySamples() to the host
//    m.setParams (style, loudness, punch, bright, bass, width);   // any thread, any time
//    m.process (L, R, n);                            // audio thread, in place (R may be nullptr)
//    auto meters = m.getMeters();                    // UI thread (timer)
//  The loudness gain is learned while the song plays. For a file export run the
//  song once through process() (discard the output), then reset(),
//  setLearningFrozen (true) and render: the gain is then static from the first sample.
//  Call resetLearning() when another song is loaded.
// =============================================================================
#pragma once

#include <vector>
#include <array>
#include <atomic>
#include <cmath>
#include <algorithm>
#include <cstdint>
#include <cstring>

namespace korgus
{

class KorgusMaster
{
public:
    static constexpr int kBands = 4;
    static constexpr int kNumStyles = 6;       // styles shown to the user
    static constexpr int kStyleFlat = 6;       // hidden: neutral chain for tests / null test
    static constexpr int kNumKnobs = 5;

    enum Style { Balanced = 0, Pop, Rock, HipHopEdm, Acoustic, Gentle };
    enum Knob  { Loudness = 0, Punch, Bright, Bass, Width };

    // ---- labels (UTF-8) -----------------------------------------------------
    static const char* styleNameSk (int i) noexcept
    {
        static const char* const n[kNumStyles] = { "Vyvážený", "Pop", "Rock", "Hip-hop / EDM", "Akustický", "Jemný / Spev" };
        return (i >= 0 && i < kNumStyles) ? n[i] : "";
    }
    static const char* styleNameEn (int i) noexcept
    {
        static const char* const n[kNumStyles] = { "Balanced", "Pop", "Rock", "Hip-hop / EDM", "Acoustic", "Gentle / Vocal" };
        return (i >= 0 && i < kNumStyles) ? n[i] : "";
    }
    static const char* knobNameSk (int i) noexcept
    {
        static const char* const n[kNumKnobs] = { "Hlasitosť", "Dynamika", "Jas", "Basy", "Šírka" };
        return (i >= 0 && i < kNumKnobs) ? n[i] : "";
    }
    static const char* knobNameEn (int i) noexcept
    {
        static const char* const n[kNumKnobs] = { "Loudness", "Punch", "Brightness", "Bass", "Width" };
        return (i >= 0 && i < kNumKnobs) ? n[i] : "";
    }
    static const char* styleLabelSk() noexcept { return "Štýl"; }
    static const char* styleLabelEn() noexcept { return "Style"; }
    static float knobDefault (int i) noexcept
    {
        static const float d[kNumKnobs] = { 0.4f, 0.5f, 0.5f, 0.5f, 2.0f / 3.0f };
        return (i >= 0 && i < kNumKnobs) ? d[i] : 0.5f;
    }

    // ---- value mappings (for displays) --------------------------------------
    static float loudnessToLufs (float v01) noexcept  { return -18.0f + 10.0f * clamp01 (v01); }       // -18 .. -8 LUFS
    static float lufsToLoudness (float lufs) noexcept { return clamp01 ((lufs + 18.0f) / 10.0f); }
    static float widthToPercent (float v01) noexcept  { return 150.0f * clamp01 (v01); }               // 0 .. 150 %
    static float toneToDb (float v01, float range) noexcept { return (clamp01 (v01) - 0.5f) * 2.0f * range; }

    // ---- meters ---------------------------------------------------------------
    struct Meters
    {
        float inMomentary = -100.0f, inShortTerm = -100.0f, inIntegrated = -100.0f;      // LUFS
        float outMomentary = -100.0f, outShortTerm = -100.0f, outIntegrated = -100.0f;   // LUFS (mastered signal)
        float bandGrDb[kBands] = { 0, 0, 0, 0 };   // gain reduction per band, dB (>= 0)
        float limiterGrDb = 0.0f;                  // limiter gain reduction, dB (>= 0, peak of the last ~16 ms)
        float clipGrDb = 0.0f;                     // soft clipper peak reduction, dB (>= 0)
        float truePeakDb = -100.0f;                // output true peak of the last ~16 ms, dBTP
        float truePeakMaxDb = -100.0f;             // output true peak maximum since resetMeters(), dBTP
        float autoGainDb = 0.0f;                   // gain currently applied by the loudness automation
        float matchGainDb = 0.0f;                  // how much louder the master is than the input (LU)
        float targetLufs = -14.0f;
    };

    KorgusMaster()
    {
        for (int i = 0; i < kNumKnobs; ++i) uKnob[i].store (knobDefault (i));
        for (auto& b : mBand) b.store (0.0f);
    }

    // ---- setup --------------------------------------------------------------
    void prepare (double sampleRate, int /*maxBlock*/ = 512, int /*channels*/ = 2)
    {
        double s = sampleRate;
        if (! (s >= 16000.0)) s = 44100.0;
        if (s > 384000.0) s = 384000.0;
        sr = s;

        ctl = 16;
        while ((double) ctl * 48000.0 < sr * 16.0 * 0.75 && ctl < 128) ctl *= 2;   // 16 @48k, 32 @96k, 64 @192k
        dtc = (double) ctl / sr;
        hopLen = (int) std::lround (sr * 0.1);
        meterLen = std::max (16, (int) std::lround (sr / 60.0));

        // oversampling filters (4x, Kaiser-windowed sinc, Nyquist(4) design)
        {
            double h[kProtoLen];
            designProto (h, kProtoHalf, 8.0);
            double sumAll = 0.0; for (double v : h) sumAll += v;
            for (int i = 0; i < kProtoLen; ++i) decCoef[i] = (float) (h[i] / sumAll);
            interpBound = polyphase (h, kIK, &upCoef[0][0]);
            double hd[4 * kDK + 1];
            designProto (hd, 2 * kDK, 10.0);
            detBound = polyphase (hd, kDK, &detCoef[0][0]);
        }

        // limiter geometry
        la = std::max (8, (int) std::lround (sr * 0.002));          // 2 ms look-ahead
        box1N = (la + 2) / 2; box2N = (la + 2) - box1N;
        int cap = 16; while (cap < la + 4) cap *= 2;
        dqMask = (uint32_t) (cap - 1);
        dqV.assign ((size_t) cap, 0.0f); dqI.assign ((size_t) cap, 0u);
        box1.assign ((size_t) box1N, 0.0); box2.assign ((size_t) box2N, 0.0);
        limDL.assign ((size_t) cap, 0.0f); limDR.assign ((size_t) cap, 0.0f); limP.assign ((size_t) cap, 0.0f);
        latency = 2 * (kIK / 2) + kDK / 2 + la;                      // clipper (24) + detector (24) + look-ahead
        int dcap = 64; while (dcap < latency + 4) dcap *= 2;
        dryMask = dcap - 1;
        dryL.assign ((size_t) dcap, 0.0f); dryR.assign ((size_t) dcap, 0.0f);

        // fixed filters
        hpC = butter (24.0, true);
        const double f1 = 120.0, f2 = 800.0, f3 = 5000.0;
        xLp[0] = butter (f1, false); xHp[0] = butter (f1, true); xAp[0] = allpassOf (xLp[0]);
        xLp[1] = butter (f2, false); xHp[1] = butter (f2, true);
        xLp[2] = butter (f3, false); xHp[2] = butter (f3, true); xAp[2] = allpassOf (xLp[2]);
        kwPre = kWeightShelf(); kwRlb = kWeightHp();

        // smart-tilt detector (one-pole filters on the mono sum)
        tlK1 = onePole (1000.0); tlK2 = onePole (150.0); tlK3 = onePole (4000.0);

        // time constants
        kPar  = 1.0 - std::exp (-dtc / 0.030);
        kTgt  = 1.0 - std::exp (-dtc / 0.080);
        static const double rmsMs[kBands] = { 12.0, 8.0, 4.0, 2.0 };
        for (int b = 0; b < kBands; ++b) kRms[b] = 1.0 - std::exp (-dtc / (rmsMs[b] * 0.001));
        kSlowA = 1.0 - std::exp (-dtc / 0.150);
        kSlowR = 1.0 - std::exp (-dtc / 1.2);
        kLvl   = 1.0 - std::exp (-dtc / 6.0);
        kMk    = 1.0 - std::exp (-dtc / 3.0);
        kTilt  = 1.0 - std::exp (-dtc / 10.0);
        limSA  = 1.0 - std::exp (-1.0 / (0.150 * sr));
        limSR  = 1.0 - std::exp (-1.0 / (0.700 * sr));
        kXf    = 1.0 - std::exp (-1.0 / (0.010 * sr));
        lossLeak = std::exp (-0.1 / 60.0);

        prepared = true;
        resetLearning();
        reset();
    }

    // Clears all audio state. Parameters and everything the processor has learned about
    // the programme (loudness, band levels, tone balance) are kept, so a second pass
    // over the same song starts settled. Call resetLearning() when a new song is loaded.
    void reset() noexcept
    {
        if (! prepared) return;
        for (auto& c : chan) c.clear();
        inMeter.clearAudio(); outMeter.clearAudio();
        std::fill (dqV.begin(), dqV.end(), 0.0f); std::fill (dqI.begin(), dqI.end(), 0u);
        std::fill (box1.begin(), box1.end(), 0.0); std::fill (box2.begin(), box2.end(), 0.0);
        std::fill (limDL.begin(), limDL.end(), 0.0f); std::fill (limDR.begin(), limDR.end(), 0.0f);
        std::fill (limP.begin(), limP.end(), 0.0f);
        std::fill (dryL.begin(), dryL.end(), 0.0f); std::fill (dryR.begin(), dryR.end(), 0.0f);
        dqHead = dqTail = 0; limIdx = 0; box1Pos = box2Pos = 0; box1Sum = box2Sum = 0.0;
        limW = 0; dryW = 0; limF = limS = 0.0; qPrev = 0.0f;
        tl1 = tl2 = tl3 = 0.0; accHi = accMid = 0.0;
        for (int b = 0; b < kBands; ++b) { accP[b] = 0.0; rms[b] = 0.0; cF[b] = cS[b] = 0.0; grNow[b] = 0.0; }
        ctlCount = 0; hopCount = 0; meterCount = 0;
        accG2 = 0.0; g2Fill = 0; g2Pos = 0;
        mLimGr = 0.0f; mClip = 1.0f; mTp = 0.0f;
        xf = bypassOn.load() ? 1.0f : 0.0f;
        snap = true;            // the first process() call snaps every smoothed value to its target
    }

    // Forgets everything learned about the programme (call when another song is loaded).
    void resetLearning() noexcept
    {
        if (! prepared) return;
        inMeter.clearAll(); outMeter.clearAll();
        lossA = lossB = 0.0; lossDb = 0.0; haveEst = false; adaptCur = adaptTgt = 0.0;
        learnAge = 0.0; gDb = 0.0; gDbHop = 0.0; inPeak = 0.0f;
        for (int b = 0; b < kBands; ++b) { lvl[b] = 0.0; lvlN[b] = 0; mkAvg[b] = 0.0; mkN[b] = 0; }
        eHi = eMid = 0.0; tiltN = 0; smartCur = 0.0;
        tpMax = 0.0f;
        publishLoudness();
    }

    // Restarts the displayed output integrated loudness and the true-peak maximum.
    // (Thread-safe request, executed by the audio thread.)
    void resetMeters() noexcept { meterResetReq.store (true); }

    int getLatencySamples() const noexcept { return latency; }

    // ---- parameters (thread-safe, all knobs 0..1) ---------------------------
    //  style     0..5 (see Style)                      "Štýl"
    //  loudness  target -18 .. -8 LUFS (0.4 = -14)     "Hlasitosť"
    //  punch     multiband compression amount          "Dynamika"   (0 = off, 0.5 = style default)
    //  bright    tilt + air, 0.5 = neutral             "Jas"
    //  bass      low shelf +-5 dB, 0.5 = neutral       "Basy"
    //  width     0 .. 150 % (2/3 = 100 %)              "Šírka"
    void setParams (int style, float loudness, float punch, float bright, float bass, float width) noexcept
    {
        uStyle.store (std::min (std::max (style, 0), kStyleFlat));
        uKnob[Loudness].store (san01 (loudness, 0.4f));
        uKnob[Punch].store (san01 (punch, 0.5f));
        uKnob[Bright].store (san01 (bright, 0.5f));
        uKnob[Bass].store (san01 (bass, 0.5f));
        uKnob[Width].store (san01 (width, 2.0f / 3.0f));
    }

    // Output ceiling in dBTP (-6 .. -0.1, default -1). A setup parameter: set it before playback.
    void setCeilingDb (float db) noexcept { uCeil.store (std::isfinite (db) ? std::min (-0.1f, std::max (-6.0f, db)) : -1.0f); }
    float getCeilingDb() const noexcept { return uCeil.load(); }

    // Slow automatic tone correction ("smart tilt"), default on.
    void setSmartTone (bool on) noexcept { uSmart.store (on); }

    // Freezes the loudness automation at its current gain (use for the final render
    // after a learning pass: the gain is then perfectly static).
    void setLearningFrozen (bool frozen) noexcept { uFrozen.store (frozen); }

    // A/B compare. While bypassed the output is the latency-compensated dry signal;
    // with matchLoudness it is gain-matched to the master (equal-loudness compare).
    // The processor keeps running, meters keep showing the mastered signal.
    void setBypass (bool bypassed, bool matchLoudness = true) noexcept { bypassOn.store (bypassed); bypassMatch.store (matchLoudness); }

    // Linear gain that makes the dry signal as loud as the master.
    float getMatchedBypassGain() const noexcept { return std::pow (10.0f, mMatch.load() * 0.05f); }

    // ---- meters (thread-safe, call from the UI timer) -------------------------
    Meters getMeters() const noexcept
    {
        Meters m;
        m.inMomentary = mInM.load();   m.inShortTerm = mInS.load();   m.inIntegrated = mInI.load();
        m.outMomentary = mOutM.load(); m.outShortTerm = mOutS.load(); m.outIntegrated = mOutI.load();
        for (int b = 0; b < kBands; ++b) m.bandGrDb[b] = mBand[b].load();
        m.limiterGrDb = mLim.load(); m.clipGrDb = mClipDb.load();
        m.truePeakDb = mTpDb.load(); m.truePeakMaxDb = mTpMaxDb.load();
        m.autoGainDb = mAuto.load(); m.matchGainDb = mMatch.load(); m.targetLufs = mTarget.load();
        return m;
    }

    int getGuardTrips() const noexcept { return guardTrips.load(); }

    // ---- processing ---------------------------------------------------------
    // In place. R may be nullptr (mono). Any block size.
    void process (float* L, float* R, int n) noexcept
    {
        if (! prepared || L == nullptr || n <= 0) return;
        const bool stereo = (R != nullptr && R != L);
        monoIn = ! stereo;
        if (snap) { controlTick(); snap = false; ctlCount = 0; }
        Chan& A = chan[0]; Chan& B = chan[1];

        for (int i = 0; i < n; ++i)
        {
            const float fl = clean (L[i]);
            const float fr = stereo ? clean (R[i]) : fl;
            double l = fl, r = fr;

            dryL[(size_t) dryW] = fl; dryR[(size_t) dryW] = fr;
            const float al = std::abs (fl), ar = std::abs (fr);
            if (al > inPeak) inPeak = al;
            if (ar > inPeak) inPeak = ar;

            // ---- input loudness ------------------------------------------------
            inMeter.push (kwPre, kwRlb, l, r, monoIn);

            // ---- rumble high-pass ------------------------------------------------
            {
                const double hl = A.hp.tick (hpC, l), hr = B.hp.tick (hpC, r);
                l += hpMix * (hl - l); r += hpMix * (hr - r);
            }

            // ---- smart-tilt detector ---------------------------------------------
            {
                const double m = 0.5 * (l + r);
                tl1 += tlK1 * (m - tl1) + kDn; tl2 += tlK2 * (m - tl2) + kDn; tl3 += tlK3 * (m - tl3) + kDn;
                const double hi = m - tl3, mid = tl1 - tl2;
                accHi += hi * hi; accMid += mid * mid;
            }

            // ---- tone EQ -----------------------------------------------------------
            for (int k = 0; k < kNumEq; ++k) { l = A.eq[k].tick (eqC[k], l); r = B.eq[k].tick (eqC[k], r); }
            if (eqRamping)          // coefficients glide per sample while a tone control moves
                for (int k = 0; k < kNumEq; ++k)
                {
                    eqC[k].b0 += eqInc[k].b0; eqC[k].b1 += eqInc[k].b1; eqC[k].b2 += eqInc[k].b2;
                    eqC[k].a1 += eqInc[k].a1; eqC[k].a2 += eqInc[k].a2;
                }

            // ---- crossover, dynamics, stereo --------------------------------------
            double bl[kBands], br[kBands];
            A.split (*this, l, bl); B.split (*this, r, br);
            double om = 0.0, os = 0.0;
            for (int b = 0; b < kBands; ++b)
            {
                accP[b] += bl[b] * bl[b] + br[b] * br[b];
                om += gm[b] * (bl[b] + br[b]); os += gs[b] * (bl[b] - br[b]);
                gm[b] += gmInc[b]; gs[b] += gsInc[b];
            }
            om *= 0.5; os *= 0.5;

            // ---- loudness gain -----------------------------------------------------
            const float xl = (float) ((om + os) * drv), xr = (float) ((om - os) * drv);
            accG2 += drv * drv; drv += drvInc;

            // ---- soft clipper (4x) ------------------------------------------------
            const float cl = A.clipper (*this, xl), cr = B.clipper (*this, xr);

            // ---- true-peak limiter -------------------------------------------------
            float q = A.detect (*this, cl);
            { const float q2 = B.detect (*this, cr); if (q2 > q) q = q2; }
            const float p = q > qPrev ? q : qPrev; qPrev = q;
            const float zl = A.det.center(), zr = B.det.center();

            double req = 0.0;
            if (p > limC) req = std::min (60.0, 20.0 * std::log10 ((double) p / (double) limC));
            limF = req > limF ? req : limF * limRelC;
            if (limF < 1.0e-7) limF = 0.0;
            limS += (limF > limS ? limSA : limSR) * (limF - limS) + kDn;
            const float e = (float) (limF > limS ? limF : limS);

            while (dqTail != dqHead && dqV[(size_t) ((dqTail - 1u) & dqMask)] <= e) --dqTail;
            dqV[(size_t) (dqTail & dqMask)] = e; dqI[(size_t) (dqTail & dqMask)] = limIdx; ++dqTail;
            while ((uint32_t) (limIdx - dqI[(size_t) (dqHead & dqMask)]) > (uint32_t) la) ++dqHead;
            const double mx = dqV[(size_t) (dqHead & dqMask)];
            ++limIdx;

            box1Sum += mx - box1[(size_t) box1Pos]; box1[(size_t) box1Pos] = mx; if (++box1Pos >= box1N) box1Pos = 0;
            const double a1 = box1Sum / (double) box1N;
            box2Sum += a1 - box2[(size_t) box2Pos]; box2[(size_t) box2Pos] = a1; if (++box2Pos >= box2N) box2Pos = 0;
            const double a2 = box2Sum / (double) box2N;

            limDL[(size_t) limW] = zl; limDR[(size_t) limW] = zr; limP[(size_t) limW] = p;
            const int rd = (int) (((uint32_t) limW - (uint32_t) la) & dqMask);
            limW = (int) (((uint32_t) limW + 1u) & dqMask);
            float gl = 1.0f;
            if (a2 > 1.0e-6) { gl = (float) std::exp (-0.11512925464970229 * a2); if ((float) a2 > mLimGr) mLimGr = (float) a2; }
            float wl = limDL[(size_t) rd] * gl, wr = limDR[(size_t) rd] * gl;
            { const float tp = limP[(size_t) rd] * gl; if (tp > mTp) mTp = tp; }

            // safety clamp (never active in normal operation)
            if (! (std::abs (wl) <= ceilLin)) wl = wl > ceilLin ? ceilLin : (wl < -ceilLin ? -ceilLin : 0.0f);
            if (! (std::abs (wr) <= ceilLin)) wr = wr > ceilLin ? ceilLin : (wr < -ceilLin ? -ceilLin : 0.0f);

            // ---- output loudness ---------------------------------------------------
            outMeter.push (kwPre, kwRlb, (double) wl, (double) wr, monoIn);

            // ---- loudness-matched bypass ------------------------------------------
            xf += kXf * (xfT - xf);
            if (std::abs (xfT - xf) < 1.0e-4f) xf = xfT;
            if (xf > 1.0e-5f)
            {
                const int di = (dryW - latency) & dryMask;
                wl += xf * (dryL[(size_t) di] * bypGain - wl);
                wr += xf * (dryR[(size_t) di] * bypGain - wr);
                wl = wl > 1.0f ? 1.0f : (wl < -1.0f ? -1.0f : wl);
                wr = wr > 1.0f ? 1.0f : (wr < -1.0f ? -1.0f : wr);
                bypGain += bypInc;
            }
            dryW = (dryW + 1) & dryMask;

            L[i] = wl;
            if (stereo) R[i] = wr;
            lastOut = wl + wr;

            if (++ctlCount >= ctl)   { ctlCount = 0; controlTick(); }
            if (++hopCount >= hopLen) { hopCount = 0; hopTick(); }
            if (++meterCount >= meterLen) { meterCount = 0; meterTick(); }
        }
    }

private:
    // ---- constants -----------------------------------------------------------
    static constexpr double kPi = 3.14159265358979323846;
    static constexpr double kDn = 1.0e-30;          // anti-denormal bias for double states
    static constexpr int kIK = 24;                  // clipper interpolator taps per phase
    static constexpr int kDK = 48;                  // true-peak detector taps per phase
    static constexpr int kProtoHalf = 4 * (kIK / 2);
    static constexpr int kProtoLen = 2 * kProtoHalf + 1;      // 97
    static constexpr int kDecRing = 128;
    static constexpr int kNumEq = 5;                // low shelf, mud, presence, air, tilt
    static constexpr int kHistBins = 800;           // -70 .. +10 LUFS in 0.1 LU steps

    struct StyleDef
    {
        float thr[kBands];     // threshold, dB relative to the long-term band level (at punch = 0.5)
        float ratio[kBands];
        float attMs[kBands], relMs[kBands];
        float mk[kBands];      // static band make-up / balance, dB
        float lowShelf, mud, pres, air, tilt;   // dB
        float clipHead;        // soft-clip limit above the ceiling, dB (smaller = more clipping, less limiting)
        float clipKnee;        // dB below the limit where the clipper starts to bend
        float limRelMs;        // limiter fast release
        float smartRef;        // target high/mid balance for the smart tilt, dB
        float flat;            // 1 = neutral test chain (no HP, no bass mono, no smart tilt)
    };

    static const StyleDef& styleDef (int i) noexcept
    {
        static const StyleDef s[kNumStyles + 1] = {
            // Balanced
            { { 3.0f, 2.0f, 2.0f, 2.0f }, { 2.0f, 1.8f, 1.8f, 2.0f }, { 30, 15, 8, 4 }, { 160, 130, 100, 70 }, { 0, 0, 0, 0 },
              0.5f, -0.5f, 0.5f, 1.0f, 0.0f, 3.0f, 6.0f, 70.0f, -12.0f, 0.0f },
            // Pop
            { { 2.0f, 1.5f, 1.0f, 1.0f }, { 2.5f, 2.0f, 2.0f, 2.5f }, { 30, 15, 8, 4 }, { 150, 120, 90, 60 }, { 0.3f, -0.2f, 0.2f, 0.3f },
              1.0f, -1.0f, 1.0f, 2.0f, 0.0f, 2.5f, 6.0f, 60.0f, -11.0f, 0.0f },
            // Rock
            { { 2.0f, 1.0f, 1.0f, 2.0f }, { 2.5f, 2.5f, 2.2f, 2.0f }, { 40, 20, 10, 5 }, { 180, 140, 100, 70 }, { 0.3f, 0.0f, 0.4f, 0.0f },
              1.0f, -0.5f, 1.5f, 1.0f, 0.0f, 2.0f, 5.0f, 60.0f, -11.5f, 0.0f },
            // Hip-hop / EDM
            { { 1.0f, 1.5f, 1.0f, 1.0f }, { 3.0f, 2.0f, 2.2f, 2.5f }, { 45, 20, 8, 3 }, { 140, 120, 90, 60 }, { 0.8f, -0.4f, 0.0f, 0.4f },
              2.0f, -1.5f, 0.5f, 2.0f, 0.0f, 1.5f, 5.0f, 50.0f, -11.0f, 0.0f },
            // Acoustic
            { { 4.0f, 4.0f, 4.0f, 4.0f }, { 1.5f, 1.5f, 1.5f, 1.6f }, { 40, 25, 12, 6 }, { 220, 180, 140, 100 }, { 0, 0, 0, 0 },
              0.0f, -0.5f, 0.5f, 1.0f, 0.0f, 5.0f, 6.0f, 90.0f, -12.5f, 0.0f },
            // Gentle / Vocal
            { { 4.0f, 3.0f, 2.0f, 3.0f }, { 1.6f, 1.6f, 2.0f, 1.8f }, { 35, 20, 10, 5 }, { 200, 160, 120, 90 }, { -0.3f, 0.0f, 0.3f, 0.0f },
              -0.5f, -1.0f, 1.0f, 1.5f, 0.0f, 5.0f, 6.0f, 90.0f, -12.0f, 0.0f },
            // Flat (hidden, for tests)
            { { 0, 0, 0, 0 }, { 1, 1, 1, 1 }, { 30, 15, 8, 4 }, { 160, 130, 100, 70 }, { 0, 0, 0, 0 },
              0.0f, 0.0f, 0.0f, 0.0f, 0.0f, 24.0f, 6.0f, 70.0f, -12.0f, 1.0f },
        };
        return s[std::min (std::max (i, 0), kNumStyles)];
    }

    // smoothed internal parameters
    enum P { pThr = 0, pRatio = pThr + kBands, pMk = pRatio + kBands,
             pLs = pMk + kBands, pMud, pPres, pAir, pTilt,
             pClipHead, pClipKnee, pWidth, pBassMono, pHp, pSmart, pCeil, pCount };

    // ---- small helpers ---------------------------------------------------------
    static float clamp01 (float v) noexcept { return v < 0.0f ? 0.0f : (v > 1.0f ? 1.0f : v); }
    static float san01 (float v, float def) noexcept { return std::isfinite (v) ? clamp01 (v) : def; }
    static float clean (float v) noexcept
    {
        if (! std::isfinite (v)) return 0.0f;
        return v > 16.0f ? 16.0f : (v < -16.0f ? -16.0f : v);
    }
    static double clampD (double v, double lo, double hi) noexcept { return v < lo ? lo : (v > hi ? hi : v); }
    static double dbToLin (double db) noexcept { return std::exp (0.11512925464970229 * db); }
    static double lufsOf (double p) noexcept { return p > 1.0e-12 ? -0.691 + 10.0 * std::log10 (p) : -120.0; }
    static double bessel0 (double x) noexcept
    {
        double sum = 1.0, term = 1.0;
        for (int k = 1; k < 40; ++k) { term *= (x * 0.5 / k) * (x * 0.5 / k); sum += term; }
        return sum;
    }
    // prototype low-pass h[n] = sinc(n/4) * kaiser, n = -half..half
    static void designProto (double* h, int half, double beta) noexcept
    {
        const double i0b = bessel0 (beta);
        for (int i = 0; i <= 2 * half; ++i)
        {
            const int n = i - half;
            const double x = (double) n / 4.0;
            const double sinc = (n == 0) ? 1.0 : std::sin (kPi * x) / (kPi * x);
            const double r = (double) n / (double) half;
            h[i] = sinc * bessel0 (beta * std::sqrt (std::max (0.0, 1.0 - r * r))) / i0b;
        }
    }
    // phases 1..3 (K taps each, unity DC gain); returns the worst-case peak gain of the interpolation
    static float polyphase (const double* h, int K, float* out) noexcept
    {
        double bound = 1.0;
        for (int p = 1; p < 4; ++p)
        {
            double sum = 0.0, absSum = 0.0;
            for (int i = 0; i < K; ++i) sum += h[4 * (i - K / 2) + p + 2 * K];
            for (int i = 0; i < K; ++i)
            {
                const double c = h[4 * (i - K / 2) + p + 2 * K] / sum;
                out[(p - 1) * K + i] = (float) c; absSum += std::abs (c);
            }
            bound = std::max (bound, absSum);
        }
        return (float) (bound * 1.001);
    }
    double onePole (double hz) const noexcept { return 1.0 - std::exp (-2.0 * kPi * std::min (hz, 0.45 * sr) / sr); }

    static inline float dot (const float* a, const float* b, int n) noexcept
    {
        float s0 = 0.0f, s1 = 0.0f, s2 = 0.0f, s3 = 0.0f;
        int i = 0;
        for (; i + 3 < n; i += 4)
        {
            s0 += a[i] * b[i]; s1 += a[i + 1] * b[i + 1]; s2 += a[i + 2] * b[i + 2]; s3 += a[i + 3] * b[i + 3];
        }
        for (; i < n; ++i) s0 += a[i] * b[i];
        return (s0 + s1) + (s2 + s3);
    }

    // ---- biquad (transposed direct form II, double) ----------------------------
    struct Coef { double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0; };
    struct Bq
    {
        double s1 = 0.0, s2 = 0.0;
        inline double tick (const Coef& c, double x) noexcept
        {
            const double y = c.b0 * x + s1;
            s1 = c.b1 * x - c.a1 * y + s2 + kDn;
            s2 = c.b2 * x - c.a2 * y;
            return y;
        }
        void clear() noexcept { s1 = s2 = 0.0; }
    };

    Coef butter (double hz, bool highpass) const noexcept
    {
        const double w = 2.0 * kPi * std::min (hz, 0.45 * sr) / sr, cs = std::cos (w), al = std::sin (w) / (2.0 * 0.7071067811865476);
        const double a0 = 1.0 + al;
        Coef c;
        if (highpass) { c.b0 = (1.0 + cs) * 0.5 / a0; c.b1 = -(1.0 + cs) / a0; c.b2 = c.b0; }
        else          { c.b0 = (1.0 - cs) * 0.5 / a0; c.b1 = (1.0 - cs) / a0;  c.b2 = c.b0; }
        c.a1 = -2.0 * cs / a0; c.a2 = (1.0 - al) / a0;
        return c;
    }
    static Coef allpassOf (const Coef& d) noexcept { Coef c; c.b0 = d.a2; c.b1 = d.a1; c.b2 = 1.0; c.a1 = d.a1; c.a2 = d.a2; return c; }

    Coef peak (double hz, double q, double db) const noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * std::min (hz, 0.45 * sr) / sr;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q), a0 = 1.0 + al / A;
        Coef c; c.b0 = (1.0 + al * A) / a0; c.b1 = -2.0 * cs / a0; c.b2 = (1.0 - al * A) / a0; c.a1 = c.b1; c.a2 = (1.0 - al / A) / a0;
        return c;
    }
    Coef shelf (double hz, double q, double db, bool high, double extraDb = 0.0) const noexcept
    {
        const double A = std::pow (10.0, db / 40.0), w = 2.0 * kPi * std::min (hz, 0.45 * sr) / sr;
        const double cs = std::cos (w), al = std::sin (w) / (2.0 * q), sq = 2.0 * std::sqrt (A) * al;
        const double g = dbToLin (extraDb);
        Coef c;
        if (high)
        {
            const double a0 = (A + 1.0) - (A - 1.0) * cs + sq;
            c.b0 = g * A * ((A + 1.0) + (A - 1.0) * cs + sq) / a0;
            c.b1 = g * -2.0 * A * ((A - 1.0) + (A + 1.0) * cs) / a0;
            c.b2 = g * A * ((A + 1.0) + (A - 1.0) * cs - sq) / a0;
            c.a1 = 2.0 * ((A - 1.0) - (A + 1.0) * cs) / a0;
            c.a2 = ((A + 1.0) - (A - 1.0) * cs - sq) / a0;
        }
        else
        {
            const double a0 = (A + 1.0) + (A - 1.0) * cs + sq;
            c.b0 = g * A * ((A + 1.0) - (A - 1.0) * cs + sq) / a0;
            c.b1 = g * 2.0 * A * ((A - 1.0) - (A + 1.0) * cs) / a0;
            c.b2 = g * A * ((A + 1.0) - (A - 1.0) * cs - sq) / a0;
            c.a1 = -2.0 * ((A - 1.0) + (A + 1.0) * cs) / a0;
            c.a2 = ((A + 1.0) + (A - 1.0) * cs - sq) / a0;
        }
        return c;
    }

    // ITU-R BS.1770-4 K-weighting, re-derived for the current sample rate
    Coef kWeightShelf() const noexcept
    {
        const double f0 = 1681.974450955533, G = 3.999843853973347, Q = 0.7071752369554196;
        const double K = std::tan (kPi * f0 / sr), Vh = std::pow (10.0, G / 20.0), Vb = std::pow (Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        Coef c;
        c.b0 = (Vh + Vb * K / Q + K * K) / a0; c.b1 = 2.0 * (K * K - Vh) / a0; c.b2 = (Vh - Vb * K / Q + K * K) / a0;
        c.a1 = 2.0 * (K * K - 1.0) / a0; c.a2 = (1.0 - K / Q + K * K) / a0;
        return c;
    }
    Coef kWeightHp() const noexcept
    {
        const double f0 = 38.13547087602444, Q = 0.5003270373238773;
        const double K = std::tan (kPi * f0 / sr), a0 = 1.0 + K / Q + K * K;
        Coef c; c.b0 = 1.0; c.b1 = -2.0; c.b2 = 1.0; c.a1 = 2.0 * (K * K - 1.0) / a0; c.a2 = (1.0 - K / Q + K * K) / a0;
        return c;
    }

    // ---- BS.1770 loudness meter (400 ms blocks, 100 ms hop, gated integration) --
    struct LoudMeter
    {
        Bq f[4];
        double acc = 0.0;
        double hop[30] = {};
        int hopPos = 0, hopFill = 0;
        uint32_t cnt[kHistBins] = {};
        double sum[kHistBins] = {};
        double momentary = -120.0, shortTerm = -120.0, integrated = -120.0, blockP = 0.0;
        bool intValid = false;

        void clearAudio() noexcept
        {
            for (auto& b : f) b.clear();
            acc = 0.0; hopPos = 0; hopFill = 0; for (double& h : hop) h = 0.0;
            momentary = shortTerm = -120.0; blockP = 0.0;
        }
        void clearIntegrated() noexcept
        {
            std::memset (cnt, 0, sizeof (cnt)); for (double& s : sum) s = 0.0;
            integrated = -120.0; intValid = false;
        }
        void clearAll() noexcept { clearAudio(); clearIntegrated(); }

        inline void push (const Coef& pre, const Coef& rlb, double l, double r, bool mono) noexcept
        {
            const double kl = f[1].tick (rlb, f[0].tick (pre, l));
            acc += kl * kl;
            if (! mono) { const double kr = f[3].tick (rlb, f[2].tick (pre, r)); acc += kr * kr; }
        }

        // returns the mean-square power of the last 400 ms (or of what is available)
        double endHop (int len) noexcept
        {
            double v = acc / (double) len; acc = 0.0;
            if (! std::isfinite (v)) v = 0.0;
            hop[hopPos] = v; hopPos = (hopPos + 1) % 30; if (hopFill < 30) ++hopFill;
            const int nb = std::min (4, hopFill);
            double b = 0.0; for (int i = 1; i <= nb; ++i) b += hop[(hopPos - i + 30) % 30];
            b /= (double) nb;
            double st = 0.0; for (int i = 0; i < hopFill; ++i) st += hop[i];
            st /= (double) hopFill;
            blockP = b; momentary = lufsOf (b); shortTerm = lufsOf (st);
            if (hopFill >= 4 && momentary >= -70.0)
            {
                const int bin = std::min (kHistBins - 1, std::max (0, (int) ((momentary + 70.0) * 10.0)));
                ++cnt[bin]; sum[bin] += b;
                double tot = 0.0, n = 0.0;
                for (int i = 0; i < kHistBins; ++i) { tot += sum[i]; n += (double) cnt[i]; }
                const double gate = 0.1 * tot / n;
                double tot2 = 0.0, n2 = 0.0;
                for (int i = 0; i < kHistBins; ++i)
                    if (cnt[i] != 0 && sum[i] >= gate * (double) cnt[i]) { tot2 += sum[i]; n2 += (double) cnt[i]; }
                if (n2 > 0.0) { integrated = lufsOf (tot2 / n2); intValid = true; }
            }
            return b;
        }
    };

    // ---- 4x polyphase interpolator / decimator ---------------------------------
    template <int K> struct Interp4
    {
        float buf[2 * K] = {};
        int w = 0, hot = 0;
        void clear() noexcept { std::memset (buf, 0, sizeof (buf)); w = 0; hot = 0; }
        inline void push (float x) noexcept { w = (w == 0) ? K - 1 : w - 1; buf[w] = x; buf[w + K] = x; }
        inline float center() const noexcept { return buf[w + K / 2]; }
        inline float phase (const float* c) const noexcept { return dot (buf + w, c, K); }
    };
    struct Dec4
    {
        float buf[2 * kDecRing] = {};
        int w = 0, zeros = kDecRing;
        void clear() noexcept { std::memset (buf, 0, sizeof (buf)); w = 0; zeros = kDecRing; }
        inline void push (float x) noexcept
        {
            w = (w - 1) & (kDecRing - 1); buf[w] = x; buf[w + kDecRing] = x;
            if (x != 0.0f) zeros = 0; else if (zeros < kDecRing) ++zeros;
        }
        inline float out (const float* c) const noexcept { return zeros >= kProtoLen ? 0.0f : dot (buf + w, c, kProtoLen); }
    };

    // ---- per-channel state -----------------------------------------------------
    struct Chan
    {
        Bq hp, eq[kNumEq];
        Bq x2l[2], x2h[2], apLo, apHi, x1l[2], x1h[2], x3l[2], x3h[2];
        Interp4<kIK> up;
        Interp4<kDK> det;
        Dec4 dec;
        float cd[kIK / 2] = {};      // extra delay for the clipper's dry path
        int cdW = 0;

        void clear() noexcept
        {
            hp.clear(); for (auto& b : eq) b.clear();
            for (int i = 0; i < 2; ++i) { x2l[i].clear(); x2h[i].clear(); x1l[i].clear(); x1h[i].clear(); x3l[i].clear(); x3h[i].clear(); }
            apLo.clear(); apHi.clear(); up.clear(); det.clear(); dec.clear();
            std::memset (cd, 0, sizeof (cd)); cdW = 0;
        }

        // LR4 tree: split at 800 Hz, all-pass compensate, split the halves at 120 Hz / 5 kHz
        inline void split (const KorgusMaster& o, double x, double* band) noexcept
        {
            double lo = x2l[1].tick (o.xLp[1], x2l[0].tick (o.xLp[1], x));
            double hi = x2h[1].tick (o.xHp[1], x2h[0].tick (o.xHp[1], x));
            lo = apLo.tick (o.xAp[2], lo);
            hi = apHi.tick (o.xAp[0], hi);
            band[0] = x1l[1].tick (o.xLp[0], x1l[0].tick (o.xLp[0], lo));
            band[1] = x1h[1].tick (o.xHp[0], x1h[0].tick (o.xHp[0], lo));
            band[2] = x3l[1].tick (o.xLp[2], x3l[0].tick (o.xLp[2], hi));
            band[3] = x3h[1].tick (o.xHp[2], x3h[0].tick (o.xHp[2], hi));
        }

        // soft clip at 4x; returns x delayed by 24 samples plus the band-limited clip residual
        inline float clipper (KorgusMaster& o, float x) noexcept
        {
            up.push (x);
            if (std::abs (x) * o.interpBound >= o.clipT) up.hot = kIK; else if (up.hot > 0) --up.hot;
            float res = 0.0f;
            if (up.hot > 0)
            {
                dec.push (o.clipResidual (up.center()));
                res = dec.out (o.decCoef);
                dec.push (o.clipResidual (up.phase (o.upCoef[0])));
                dec.push (o.clipResidual (up.phase (o.upCoef[1])));
                dec.push (o.clipResidual (up.phase (o.upCoef[2])));
            }
            else if (dec.zeros < kDecRing)
            {
                dec.push (0.0f);
                res = dec.out (o.decCoef);
                dec.push (0.0f); dec.push (0.0f); dec.push (0.0f);
            }
            const float d = cd[cdW];
            cd[cdW] = up.center(); if (++cdW >= kIK / 2) cdW = 0;
            return d + res;
        }

        // inter-sample peak of the 4 sub-samples following det.center()
        inline float detect (const KorgusMaster& o, float x) noexcept
        {
            det.push (x);
            if (std::abs (x) * o.detBound >= o.limC) det.hot = kDK; else if (det.hot > 0) --det.hot;
            float q = std::abs (det.center());
            if (det.hot > 0)
            {
                const float a = std::abs (det.phase (o.detCoef[0])), b = std::abs (det.phase (o.detCoef[1])), c = std::abs (det.phase (o.detCoef[2]));
                if (a > q) q = a;
                if (b > q) q = b;
                if (c > q) q = c;
            }
            return q;
        }
    };

    inline float clipResidual (float u) noexcept
    {
        const float a = std::abs (u);
        if (a <= clipT) return 0.0f;
        const float y = clipT + clipR * std::tanh ((a - clipT) * clipRInv);
        const float ratio = a / y;
        if (ratio > mClip) mClip = ratio;
        return u > 0.0f ? y - a : a - y;
    }

    // ---- control rate ------------------------------------------------------------
    void pullParams() noexcept
    {
        const int st = uStyle.load();
        float k[kNumKnobs]; for (int i = 0; i < kNumKnobs; ++i) k[i] = uKnob[i].load();
        const float ceil = uCeil.load(); const bool smart = uSmart.load();
        bool same = (st == cStyle && ceil == cCeil && smart == cSmart);
        for (int i = 0; i < kNumKnobs; ++i) same = same && (k[i] == cKnob[i]);
        if (same && ! snap) return;
        cStyle = st; cCeil = ceil; cSmart = smart; for (int i = 0; i < kNumKnobs; ++i) cKnob[i] = k[i];
        if (learnAge > 1.5) learnAge = 1.5;         // re-learn the chain loss quickly after a change

        const StyleDef& s = styleDef (st);
        const double d = 2.0 * k[Punch], dm = std::min (d, 1.0), dx = std::max (d - 1.0, 0.0);
        for (int b = 0; b < kBands; ++b)
        {
            tgt[pThr + b] = s.thr[b] + (1.0 - d) * 3.0;
            tgt[pRatio + b] = 1.0 + (s.ratio[b] - 1.0) * dm * (1.0 + 0.6 * dx);
            tgt[pMk + b] = s.mk[b] * dm;
            attC[b] = 1.0 - std::exp (-dtc / (s.attMs[b] * 0.001));
            relC[b] = 1.0 - std::exp (-dtc / (s.relMs[b] * 0.001));
        }
        const double br = (k[Bright] - 0.5) * 2.0, bs = (k[Bass] - 0.5) * 2.0;
        tgt[pLs] = s.lowShelf + 5.0 * bs;
        tgt[pMud] = s.mud;
        tgt[pPres] = s.pres + 0.8 * br;
        tgt[pAir] = s.air + 3.0 * br;
        tgt[pTilt] = s.tilt + 2.0 * br;
        tgt[pClipHead] = s.clipHead; tgt[pClipKnee] = s.clipKnee;
        tgt[pWidth] = 1.5 * k[Width];
        tgt[pBassMono] = 1.0 - s.flat; tgt[pHp] = 1.0 - s.flat;
        tgt[pSmart] = (smart && s.flat < 0.5f) ? 1.0 : 0.0;
        tgt[pCeil] = ceil;
        punchD = d;
        smartRef = s.smartRef;
        limRelC = std::exp (-1.0 / (s.limRelMs * 0.001 * sr));
        targetLufs = loudnessToLufs (k[Loudness]);
        mTarget.store ((float) targetLufs);
    }

    void controlTick() noexcept
    {
        pullParams();
        if (snap) { for (int i = 0; i < pCount; ++i) cur[i] = tgt[i]; tgtCur = targetLufs; }
        else      { for (int i = 0; i < pCount; ++i) cur[i] += kPar * (tgt[i] - cur[i]); tgtCur += kTgt * (targetLufs - tgtCur); }

        // ---- smart tilt: slow correction of the high/mid balance ------------------
        if (accHi + accMid > 1.0e-9 * ctl)
        {
            if (tiltN < 100000000) ++tiltN;
            const double k = std::max (kTilt, 1.0 / (double) tiltN);
            eHi += k * (accHi - eHi); eMid += k * (accMid - eMid);
        }
        accHi = accMid = 0.0;
        double smartT = 0.0;
        if (tiltN > 0 && eHi > 1.0e-14 && eMid > 1.0e-14)
            smartT = clampD (0.35 * (smartRef - 10.0 * std::log10 (eHi / eMid)), -2.0, 2.0);
        {
            const double step = 1.0 * dtc;          // 1 dB/s
            smartCur += clampD (smartT - smartCur, -step, step);
        }

        // ---- EQ coefficients -------------------------------------------------------
        const double tilt = cur[pTilt] + cur[pSmart] * smartCur;
        const double eqNow[kNumEq] = { cur[pLs], cur[pMud], cur[pPres], cur[pAir], tilt };
        eqRamping = false;
        for (int k = 0; k < kNumEq; ++k)
        {
            if (! snap) eqC[k] = eqTgt[k];           // the ramp of the previous tick ends exactly on its target
            eqInc[k] = Coef { 0.0, 0.0, 0.0, 0.0, 0.0 };
            if (! snap && std::abs (eqNow[k] - eqLast[k]) < 1.0e-5) continue;
            eqLast[k] = eqNow[k];
            Coef t;
            switch (k)
            {
                case 0: t = shelf (100.0, 0.7071, eqNow[0], false); break;
                case 1: t = peak (300.0, 0.9, eqNow[1]); break;
                case 2: t = peak (3000.0, 0.8, eqNow[2]); break;
                case 3: t = shelf (10000.0, 0.6, eqNow[3], true); break;
                default: t = shelf (1000.0, 0.5, eqNow[4], true, -0.5 * eqNow[4]); break;
            }
            eqTgt[k] = t;
            if (snap) { eqC[k] = t; continue; }
            const double inv = 1.0 / (double) ctl;   // (the stability triangle is convex: every interpolated set is stable)
            eqInc[k].b0 = (t.b0 - eqC[k].b0) * inv; eqInc[k].b1 = (t.b1 - eqC[k].b1) * inv; eqInc[k].b2 = (t.b2 - eqC[k].b2) * inv;
            eqInc[k].a1 = (t.a1 - eqC[k].a1) * inv; eqInc[k].a2 = (t.a2 - eqC[k].a2) * inv;
            eqRamping = true;
        }
        hpMix = cur[pHp];

        // ---- multiband compressor --------------------------------------------------
        const double w = cur[pWidth];
        static const double widen[kBands] = { 0.0, 0.5, 1.0, 1.0 };
        for (int b = 0; b < kBands; ++b)
        {
            const double pw = 0.5 * accP[b] / (double) ctl; accP[b] = 0.0;
            if (snap) rms[b] = pw; else rms[b] += kRms[b] * (pw - rms[b]) + kDn;
            double c = 0.0;
            if (rms[b] > 1.0e-8)
            {
                if (lvlN[b] < 100000000) ++lvlN[b];
                lvl[b] += std::max (kLvl, 1.0 / (double) lvlN[b]) * (rms[b] - lvl[b]);
                const double ratio = cur[pRatio + b];
                if (ratio > 1.0001 && lvl[b] > 1.0e-10)
                {
                    const double over = 10.0 * std::log10 (rms[b] / lvl[b]) - cur[pThr + b];
                    const double slope = 1.0 - 1.0 / ratio, knee = 6.0;
                    if (over >= 0.5 * knee) c = over * slope;
                    else if (over > -0.5 * knee) { const double t = over + 0.5 * knee; c = slope * t * t / (2.0 * knee); }
                    const double cap = 2.5 + 2.5 * punchD;            // gentle range limit (5 dB at the default)
                    c = cap * (1.0 - std::exp (-c / cap));
                }
            }
            // program-dependent attack (faster for big overshoots), two-stage release
            if (c > cF[b]) cF[b] += std::min (1.0, attC[b] * (c - cF[b] > 6.0 ? 2.5 : 1.0)) * (c - cF[b]);
            else           cF[b] += relC[b] * (c - cF[b]);
            cS[b] += (cF[b] > cS[b] ? kSlowA : kSlowR) * (cF[b] - cS[b]);
            if (cF[b] < 1.0e-9) cF[b] = 0.0;
            if (cS[b] < 1.0e-9) cS[b] = 0.0;
            const double gr = std::max (cF[b], cS[b]);
            grNow[b] = gr;
            if (rms[b] > 1.0e-8)
            {
                if (mkN[b] < 100000000) ++mkN[b];
                mkAvg[b] += std::max (kMk, 1.0 / (double) mkN[b]) * (gr - mkAvg[b]);
                if (mkAvg[b] < 1.0e-9) mkAvg[b] = 0.0;
            }
            const double gdb = -gr + 0.6 * mkAvg[b] + cur[pMk + b];
            const double g = (gdb == 0.0) ? 1.0 : dbToLin (gdb);
            double sg = (w <= 1.0) ? w : 1.0 + (w - 1.0) * widen[b];
            if (b == 0) sg = (1.0 - cur[pBassMono]) * std::min (w, 1.0);
            setRamp (gm[b], gmT[b], gmInc[b], g);
            setRamp (gs[b], gsT[b], gsInc[b], g * sg);
        }

        // ---- loudness automation ----------------------------------------------------
        learnAge += dtc;
        if (haveEst)
        {
            const double step = (0.25 + 15.0 * std::exp (-learnAge / 1.5)) * dtc;
            adaptCur += clampD (adaptTgt - adaptCur, -step, step);
            gDb = clampD (tgtCur + adaptCur, -24.0, 24.0);
        }
        else gDb = 0.0;
        setRamp (drv, drvT, drvInc, dbToLin (gDb));

        // ---- clipper / limiter constants ---------------------------------------------
        ceilLin = (float) dbToLin (cur[pCeil]);
        limC = (float) dbToLin (cur[pCeil] - 0.1);
        const double cLim = dbToLin (cur[pCeil] + cur[pClipHead]);
        const double cT = cLim * dbToLin (-cur[pClipKnee]);
        clipT = (float) cT; clipR = (float) (cLim - cT); clipRInv = 1.0f / clipR;

        // ---- bypass -----------------------------------------------------------------
        xfT = bypassOn.load() ? 1.0f : 0.0f;
        {
            double mg = 1.0;
            if (bypassMatch.load()) { mg = dbToLin (gDb - lossDb); if (inPeak > 1.0e-6f) mg = std::min (mg, (double) ceilLin / (double) inPeak); }
            if (snap) bypGain = (float) mg;
            bypInc = ((float) mg - bypGain) / (float) ctl;
            if (xf <= 1.0e-5f) { bypGain = (float) mg; bypInc = 0.0f; }
        }

        // ---- state guard --------------------------------------------------------------
        if (! std::isfinite (lastOut + box1Sum + box2Sum + limS + drv + gm[0] + gm[1] + gm[2] + gm[3] + rms[0] + rms[1] + rms[2] + rms[3]
                             + lvl[0] + lvl[1] + lvl[2] + lvl[3] + eHi + eMid + adaptCur))
        {
            lastOut = 0.0f;
            guardTrips.fetch_add (1);
            if (! guarding) { guarding = true; resetLearning(); reset(); guarding = false; }
        }
    }

    inline void setRamp (double& v, double& target, double& inc, double newTarget) noexcept
    {
        if (snap) { v = newTarget; target = newTarget; inc = 0.0; return; }
        v = target;                                  // remove accumulated rounding
        inc = (newTarget - v) / (double) ctl;
        target = newTarget;
    }

    // every 100 ms: loudness meters + the loudness automation's measurements
    void hopTick() noexcept
    {
        if (meterResetReq.exchange (false)) { outMeter.clearIntegrated(); tpMax = 0.0f; }

        const double pin = inMeter.endHop (hopLen), pout = outMeter.endHop (hopLen);
        g2[g2Pos] = accG2 / (double) hopLen; accG2 = 0.0; g2Pos = (g2Pos + 1) & 3; if (g2Fill < 4) ++g2Fill;
        double G2 = 0.0; for (int i = 0; i < g2Fill; ++i) G2 += g2[i];
        G2 /= (double) g2Fill;

        if (! uFrozen.load())
        {
            const double lb = inMeter.momentary;
            if (lb > -70.0 && (! inMeter.intValid || lb > inMeter.integrated - 10.0) && G2 > 1.0e-12)
            {
                const double leak = lossLeak * std::exp (-0.5 * std::abs (gDb - gDbHop));
                lossA = lossA * leak + pin; lossB = lossB * leak + pout / G2;
            }
            gDbHop = gDb;
            double est = -200.0;
            if (inMeter.intValid) est = inMeter.integrated; else if (lb > -70.0) est = lb;
            if (est > -100.0)
            {
                lossDb = (lossA > 1.0e-14 && lossB > 1.0e-14) ? clampD (10.0 * std::log10 (lossA / lossB), -6.0, 9.0) : 0.0;
                adaptTgt = lossDb - est;
                if (! haveEst) { haveEst = true; adaptCur = -tgtCur; }
            }
        }
        publishLoudness();
    }

    void publishLoudness() noexcept
    {
        mInM.store ((float) std::max (-100.0, inMeter.momentary)); mInS.store ((float) std::max (-100.0, inMeter.shortTerm));
        mInI.store ((float) std::max (-100.0, inMeter.integrated));
        mOutM.store ((float) std::max (-100.0, outMeter.momentary)); mOutS.store ((float) std::max (-100.0, outMeter.shortTerm));
        mOutI.store ((float) std::max (-100.0, outMeter.integrated));
        mAuto.store ((float) gDb); mMatch.store ((float) (haveEst ? gDb - lossDb : 0.0));
    }

    // every ~16 ms: fast meters
    void meterTick() noexcept
    {
        for (int b = 0; b < kBands; ++b) mBand[b].store ((float) grNow[b]);
        mLim.store (mLimGr); mLimGr = 0.0f;
        mClipDb.store (mClip > 1.0f ? 20.0f * std::log10 (mClip) : 0.0f); mClip = 1.0f;
        if (mTp > tpMax) tpMax = mTp;
        mTpDb.store (mTp > 1.0e-5f ? 20.0f * std::log10 (mTp) : -100.0f); mTp = 0.0f;
        mTpMaxDb.store (tpMax > 1.0e-5f ? 20.0f * std::log10 (tpMax) : -100.0f);
        mAuto.store ((float) gDb);
    }

    // ---- state ---------------------------------------------------------------------
    bool prepared = false, snap = true, monoIn = false, guarding = false;
    double sr = 48000.0, dtc = 16.0 / 48000.0;
    int ctl = 16, hopLen = 4800, meterLen = 800, latency = 132;
    int ctlCount = 0, hopCount = 0, meterCount = 0;

    // user parameters (written by any thread)
    std::atomic<int> uStyle { 0 };
    std::atomic<float> uKnob[kNumKnobs];
    std::atomic<float> uCeil { -1.0f };
    std::atomic<bool> uSmart { true }, uFrozen { false }, bypassOn { false }, bypassMatch { true }, meterResetReq { false };
    int cStyle = -1; float cKnob[kNumKnobs] = { -1, -1, -1, -1, -1 }; float cCeil = 0.0f; bool cSmart = true;

    double tgt[pCount] = {}, cur[pCount] = {};
    double targetLufs = -14.0, tgtCur = -14.0, smartRef = -12.0, punchD = 1.0;
    double kPar = 0.01, kTgt = 0.01;

    // filters
    Coef hpC, eqC[kNumEq], eqTgt[kNumEq], eqInc[kNumEq], xLp[3], xHp[3], xAp[3], kwPre, kwRlb;
    bool eqRamping = false;
    double eqLast[kNumEq] = {};
    double hpMix = 1.0;
    Chan chan[2];

    // smart tilt
    double tlK1 = 0, tlK2 = 0, tlK3 = 0, tl1 = 0, tl2 = 0, tl3 = 0, accHi = 0, accMid = 0, eHi = 0, eMid = 0, smartCur = 0, kTilt = 0;
    int tiltN = 0;

    // compressor
    double accP[kBands] = {}, rms[kBands] = {}, lvl[kBands] = {}, cF[kBands] = {}, cS[kBands] = {}, mkAvg[kBands] = {}, grNow[kBands] = {};
    int lvlN[kBands] = {}, mkN[kBands] = {};
    double kRms[kBands] = {}, attC[kBands] = {}, relC[kBands] = {}, kSlowA = 0, kSlowR = 0, kLvl = 0, kMk = 0;
    double gm[kBands] = { 1, 1, 1, 1 }, gmT[kBands] = { 1, 1, 1, 1 }, gmInc[kBands] = {};
    double gs[kBands] = { 1, 1, 1, 1 }, gsT[kBands] = { 1, 1, 1, 1 }, gsInc[kBands] = {};

    // loudness automation
    LoudMeter inMeter, outMeter;
    double drv = 1.0, drvT = 1.0, drvInc = 0.0, gDb = 0.0, gDbHop = 0.0;
    double accG2 = 0.0, g2[4] = {}; int g2Fill = 0, g2Pos = 0;
    double lossA = 0.0, lossB = 0.0, lossDb = 0.0, lossLeak = 0.995, adaptCur = 0.0, adaptTgt = 0.0, learnAge = 0.0;
    bool haveEst = false;
    float inPeak = 0.0f;

    // oversampling
    float upCoef[3][kIK] = {}, detCoef[3][kDK] = {}, decCoef[kProtoLen] = {}, interpBound = 4.0f, detBound = 4.0f;
    float clipT = 1.0f, clipR = 1.0f, clipRInv = 1.0f;

    // limiter
    int la = 96, box1N = 49, box2N = 49, box1Pos = 0, box2Pos = 0, limW = 0;
    uint32_t dqMask = 0, dqHead = 0, dqTail = 0, limIdx = 0;
    std::vector<float> dqV, limDL, limDR, limP;
    std::vector<uint32_t> dqI;
    std::vector<double> box1, box2;
    double box1Sum = 0.0, box2Sum = 0.0, limF = 0.0, limS = 0.0, limSA = 0.0, limSR = 0.0, limRelC = 0.999;
    float limC = 0.89f, ceilLin = 0.891f, qPrev = 0.0f, lastOut = 0.0f;

    // bypass
    std::vector<float> dryL, dryR;
    int dryMask = 0, dryW = 0;
    float xf = 0.0f, xfT = 0.0f, kXf = 0.01f, bypGain = 1.0f, bypInc = 0.0f;

    // meters
    float mLimGr = 0.0f, mClip = 1.0f, mTp = 0.0f, tpMax = 0.0f;
    std::atomic<float> mInM { -100.0f }, mInS { -100.0f }, mInI { -100.0f }, mOutM { -100.0f }, mOutS { -100.0f }, mOutI { -100.0f };
    std::atomic<float> mBand[kBands];
    std::atomic<float> mLim { 0.0f }, mClipDb { 0.0f }, mTpDb { -100.0f }, mTpMaxDb { -100.0f }, mAuto { 0.0f }, mMatch { 0.0f }, mTarget { -14.0f };
    std::atomic<int> guardTrips { 0 };
};

} // namespace korgus
