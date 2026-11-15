// =============================================================================
//  Korgus Vocoder - channel vocoder (single header, C++17, no dependencies)
//
//  Signal flow:
//    modulator (voice, mono) -> 80 Hz low-cut -> analysis bank (16..40 bands,
//        log spaced 100 Hz..10 kHz, 4th-order Butterworth band-pass = 2 TPT
//        state-variable sections per band) -> |x| -> attack/release follower
//        -> smoother                                   = band envelopes E[b]
//    carrier (mono) -> identical synthesis bank, centre frequencies scaled by
//        the formant ratio (+-1 octave) -> per-band whitening (band level is
//        pulled towards the total carrier level, so dull carriers still speak)
//        -> * E[b] * high-band emphasis -> alternating-band stereo spread
//    consonants: voiced/unvoiced detector (high band vs low band energy);
//        when unvoiced: (a) the modulator's own > 3.5 kHz band is passed
//        through, (b) white noise in 3 broad bands (2.8 / 5 / 8.5 kHz) shaped
//        by the modulator envelopes is added, (c) the vocoded top bands are
//        ducked  -> S, T, K, F stay readable with any carrier, even a dull one
//    level: the envelopes are normalised by the slow modulator level, a slow
//        gated auto-gain makes the wet loudness follow the insert input
//        (mode Track: level of the carrier track, modes Synth / Robot: level
//        of the voice), soft limiter (|wet| <= 2)
//    -> optional mild 3-phase ensemble -> equal-power dry/wet mix
//
//  Carrier sources (parameter "Nosic"):
//    0 Track  : carrier = insert input, modulator = sidechain (key) input.
//               Without a sidechain the dry signal is passed through and
//               sidechainMissing() returns true.
//    1 Synth  : modulator = insert input, carrier = internal 8-voice synth
//               (3 detuned saws + pulse, polyBLEP, noise) played by noteOn/Off
//    2 Robot  : modulator = insert input, carrier = the same oscillator stack
//               following the pitch of the voice (NSDF pitch tracker); snapped
//               to the song key when setKey() provided one, otherwise free.
//
//  Latency: 0 samples (minimum-phase IIR bank, no look-ahead).
//
//  Safety properties:
//    * no allocation / locks / system calls after prepare()
//    * non-finite input samples and parameters are replaced; state is checked
//      every control tick and reset if it ever becomes non-finite
//    * denormal-safe without FTZ/DAZ (-180 dB noise keeps the banks alive)
//    * every parameter change is smoothed; band-count / carrier-mode changes
//      are done inside a short wet fade (5 ms out, 12 ms hold, 5 ms in) (no clicks, no zipper noise)
//    * 44.1 .. 192 kHz, any block size, mono or stereo, in/out may alias
//
//  Six user parameters: carrier mode (choice) + bands, formant, clarity,
//  release, mix (all normalised 0..1).
// =============================================================================
#pragma once

#include <vector>
#include <cmath>
#include <complex>
#include <algorithm>
#include <cstdint>
#include <cstdio>

namespace korgus
{

class KorgusVocoder
{
public:
    static constexpr int kMaxBands = 40;
    static constexpr int kMinBands = 16;
    static constexpr int kVoices = 8;

    enum CarrierMode { carrierTrack = 0, carrierSynth = 1, carrierRobot = 2, carrierHarmony = 3 };   // 3 = robot trojhlas
    // scale types for setKey(): -1 = none (free pitch), same numbering as Autotune for 0..2
    enum ScaleType { scaleNone = -1, scaleMajor = 0, scaleMinor = 1, scaleChromatic = 2,
                     scaleHarmonicMinor = 3, scaleMajorPenta = 4, scaleMinorPenta = 5 };

    // ---- labels (UTF-8) -----------------------------------------------------
    static constexpr int kNumParams = 6;     // 0 = carrier (choice), 1..5 = 0..1
    static const char* paramNameSk (int i) noexcept
    {
        static const char* const n[kNumParams] = { "Nosi\xc4\x8d", "P\xc3\xa1sma", "Formant", "Zrozumite\xc4\xbenos\xc5\xa5", "Dozvuk", "Mix" };
        return (i >= 0 && i < kNumParams) ? n[i] : "";
    }
    static const char* paramNameEn (int i) noexcept
    {
        static const char* const n[kNumParams] = { "Carrier", "Bands", "Formant", "Clarity", "Release", "Mix" };
        return (i >= 0 && i < kNumParams) ? n[i] : "";
    }
    static const char* carrierNameSk (int m) noexcept
    {
        static const char* const n[4] = { "Stopa (k\xc4\xbe\xc3\xba\xc4\x8d)", "Intern\xc3\xbd syntez\xc3\xa1tor", "Robot (sleduje hlas)", "Robot trojhlas" };
        return (m >= 0 && m < 4) ? n[m] : "";
    }
    static const char* carrierNameEn (int m) noexcept
    {
        static const char* const n[4] = { "Track (sidechain)", "Internal synth", "Robot (follows voice)", "Robot harmony (3 voices)" };
        return (m >= 0 && m < 4) ? n[m] : "";
    }
    static float defaultParam (int i) noexcept
    {
        static const float d[kNumParams] = { 0.0f, 2.0f / 3.0f, 0.5f, 0.6f, 0.35f, 1.0f };
        return (i >= 0 && i < kNumParams) ? d[i] : 0.0f;
    }

    // ---- parameter ranges (for labels / displays) ---------------------------
    static int   bandsFrom01 (float b01) noexcept          { return kMinBands + (int) std::floor (clamp01 (b01) * (float) (kMaxBands - kMinBands) + 0.5f); }
    static float bandsTo01 (int bands) noexcept            { return clamp01 ((float) (bands - kMinBands) / (float) (kMaxBands - kMinBands)); }
    static float formantToSemitones (float f01) noexcept   { return (clamp01 (f01) - 0.5f) * 24.0f; }                     // -12 .. +12
    static float semitonesToFormant (float st) noexcept    { return clamp01 (st / 24.0f + 0.5f); }
    static float releaseToMs (float r01) noexcept          { return 6.0f * std::pow (60.0f, clamp01 (r01)); }              // 6 .. 360 ms
    // text for a value display, e.g. "32", "+5.0 st", "60 %", "25 ms"
    static void formatParam (int i, float v01, char* dst, int dstLen) noexcept
    {
        if (dst == nullptr || dstLen <= 0) return;
        switch (i)
        {
            case 1:  std::snprintf (dst, (size_t) dstLen, "%d", bandsFrom01 (v01)); break;
            case 2:  std::snprintf (dst, (size_t) dstLen, "%+.1f st", (double) formantToSemitones (v01)); break;
            case 4:  std::snprintf (dst, (size_t) dstLen, "%.0f ms", (double) releaseToMs (v01)); break;
            case 3: case 5: std::snprintf (dst, (size_t) dstLen, "%.0f %%", (double) (clamp01 (v01) * 100.0f)); break;
            default: dst[0] = 0; break;
        }
    }

    // ---- setup --------------------------------------------------------------
    void prepare (double sampleRate, int /*maxBlock*/ = 512)
    {
        double s = sampleRate;
        if (! (s >= 8000.0)) s = 44100.0;
        if (s > 384000.0) s = 384000.0;
        sr = s;

        ctl = 16;
        while ((double) ctl * 48000.0 < sr * 16.0 * 0.75 && ctl < 128) ctl *= 2;     // 16 @48k, 32 @96k, 64 @192k
        dt = (double) ctl / sr;

        ensMask = allocPow2 (ensL, (int) (sr * 0.03) + 16);
        allocPow2 (ensR, (int) (sr * 0.03) + 16);

        // fixed one-pole / follower coefficients
        hpA      = onePole (80.0);                     // modulator rumble cut
        lpDetA   = onePole (900.0);                    // "voiced" band of the unvoiced detector
        eDetA    = tc (0.004);                         // detector energy smoothing
        mbAtt    = tc (0.004);  mbRel = tc (0.035);    // broadband modulator envelope
        uAtt     = tc (0.0015); uRel  = tc (0.012);    // unvoiced amount
        gsA      = tc (0.003);                         // scalar wet gain smoothing
        mixK     = tc (0.02);
        pwA      = tc (0.25);                          // auto-gain power windows
        vAtt     = tc (0.004);  vRel = tc (0.12);      // synth voice envelope
        fadeStep = (float) (1.0 / (0.005 * sr));
        kwTick   = tcTick (0.12);                      // whitening statistics
        mrAttTick = tcTick (0.03); mrRelTick = tcTick (5.0);
        agcTick  = tcTick (0.4);
        lvlRelTick = tcTick (1.5);
        fmtTick  = tcTick (0.03);
        parTick  = tcTick (0.03);

        setSvf (sibHp, 3500.0, 1.2);                   // sibilance band (detector + direct path)
        setSvf (pitLp[0], 1000.0, 1.3066);             // 4th-order Butterworth low-pass in front of
        setSvf (pitLp[1], 1000.0, 0.5412);             //   the pitch-tracker decimator
        static const double nf[3] = { 2800.0, 5000.0, 8500.0 };
        for (int i = 0; i < 3; ++i)
        {
            const double f = std::min (nf[i], sr * 0.42), q = 1.6;
            setSvf (nzBp[i], f, q);
            // unit-variance white noise through a unit-peak band-pass: power = (pi/2 f/Q) / (sr/2)
            nzNorm[i] = (float) (1.0 / std::sqrt (std::max (1e-6, 1.5707963 * f / q / (sr * 0.5))));
        }

        // pitch tracker (works at about 8 kHz)
        pitDec = std::max (1, (int) std::floor (sr / 8000.0 + 0.5));
        pitFs = sr / (double) pitDec;
        pitW = std::min (kPitBuf - 2, (int) (pitFs * 0.048));
        pitHop = std::max (8, (int) (pitFs * 0.010));
        pitTauMin = std::max (4, (int) (pitFs / 620.0));
        pitTauMax = std::min (pitW / 2, (int) (pitFs / 62.0));

        prepared = true;
        cfgBands = 0;                                  // forces a filter design in reset()
        reset();
    }

    // Clears all audio state (parameters are kept and re-applied without smoothing).
    void reset() noexcept
    {
        if (! prepared) return;
        std::fill (ensL.begin(), ensL.end(), 0.0f);
        std::fill (ensR.begin(), ensR.end(), 0.0f);
        ensW = 0; ensPhS = 0.0; ensPhF = 0.0;
        for (int k = 0; k < 3; ++k) { ensD[k] = (float) (0.007 * sr); ensInc[k] = 0.0f; }

        sFormant = tFormant; sClarity = tClarity; sSpread = tSpread; sEns = tEns;
        cfgMode = tMode;
        applyBands (tBands);
        curRelease = -1.0f; lastFmt = -100.0f; lastSpread = -1.0f;

        hpS = lp1 = lp2 = 0.0f; eH = eL = 0.0f; mb = 0.0f;
        clearSvf (sibHp); clearSvf (pitLp[0]); clearSvf (pitLp[1]);
        for (auto& f : nzBp) clearSvf (f);
        mr = 0.05f; gate = 0.0f; uT = 0.0f; uS = 0.0f;
        cTot = 0.0f; pcW = 0.0f;
        gsT = 0.0f; gs = 0.0f; agc = 1.0f; pWet = 0.0f; pTgt = 0.0f; lcOverMr = 1.0f; sibG = 0.0f; gNoAgc = 0.0f;
        for (int i = 0; i < 3; ++i) nzG[i] = 0.0f;
        fade = 0.0f; fadeT = 1.0f; fadeHold = 0;
        gDry = 1.0f; gWet = 0.0f; mixSnap = true; scMissing = false;
        lastM = lastS = 0.0f;

        seed = 0x1234567u;
        {
            int k = 0;
            for (auto& v : voice)
            {
                v.env = 0.0f; v.gate = false; v.active = false; v.cur = v.target = 60.0f;
                for (int j = 0; j < 3; ++j, ++k) v.ph[j] = (float) ((k * 0.381966 + 0.13) - std::floor (k * 0.381966 + 0.13));
            }
            robot.ph[0] = 0.13f; robot.ph[1] = 0.57f; robot.ph[2] = 0.81f;
        }
        lvlN = 0.0f; lastNote = -1.0f; voiceAge = 0;
        std::fill (std::begin (pitBuf), std::end (pitBuf), 0.0f);
        pitPos = 0; pitCnt = 0; pitHopCnt = 0;
        pitHist[0] = pitHist[1] = pitHist[2] = 0.0f; pitHistN = 0;
        robotNote = -1; robotCand = -1; robotCandN = 0; robotVoiced = false; detectedHz = 0.0f;
        robot.cur = robot.target = robotStartPitch(); robot.env = 0.0f; robot.gate = true; robot.active = true; robot.vel = 1.0f;
        setVoiceInc (robot);
        for (int j = 0; j < 2; ++j)
        {
            auto& h = harm[j];
            h.cur = h.target = robot.cur + (j == 0 ? 4.0f : 7.0f); h.env = 0.0f; h.gate = true; h.active = true; h.vel = 1.0f;
            h.ph[0] = 0.31f + 0.2f * (float) j; h.ph[1] = 0.73f - 0.1f * (float) j; h.ph[2] = 0.05f + 0.3f * (float) j;
            setVoiceInc (h);
        }
        ctlCount = 0;
    }

    // ---- parameters (safe to call from the audio thread at any rate) ---------
    // carrierMode: 0 Track / 1 Synth / 2 Robot;  the rest normalised 0..1
    //   bands   : 16 .. 40 bands                     (default 2/3 = 32)
    //   formant : -12 .. +12 semitones, 0.5 = none   (default 0.5)
    //   clarity : consonant injection + whitening    (default 0.6)
    //   release : envelope release 6 .. 360 ms       (default 0.35 = 25 ms)
    //   mix     : dry / wet, equal power             (default 1)
    void setParams (int carrierMode, float bands, float formant, float clarity, float release, float mix) noexcept
    {
        tMode = carrierMode < 0 ? 0 : (carrierMode > 3 ? 3 : carrierMode);
        tBands = bandsFrom01 (san (bands, 2.0f / 3.0f));
        tFormant = formantToSemitones (san (formant, 0.5f));
        tClarity = san (clarity, 0.6f);
        tRelease = san (release, 0.35f);
        tMix = san (mix, 1.0f);
    }
    // Optional extras (not among the six main controls): stereo spread of alternating
    // bands (default 0.5) and the ensemble amount (default 0.2); both 0..1.
    void setExtras (float spread, float ensemble) noexcept { tSpread = san (spread, 0.5f); tEns = san (ensemble, 0.2f); }
    // Portamento of the internal synth in milliseconds (0 = off, default).
    void setGlideMs (float ms) noexcept { glideMs = finite (ms) ? std::min (2000.0f, std::max (0.0f, ms)) : 0.0f; }

    // Key for the Robot carrier: tonic 0..11 (C..B), scaleType see ScaleType (-1 = free pitch).
    void setKey (int tonic, int scaleType) noexcept
    {
        keyTonic = ((tonic % 12) + 12) % 12;
        keyScale = (scaleType < -1 || scaleType > 5) ? -1 : scaleType;
    }

    // ---- MIDI for the internal synth (call from the audio thread, before process) --
    void noteOn (int note, float vel) noexcept
    {
        if (note < 0 || note > 127) return;
        if (! (vel > 0.0f)) { noteOff (note); return; }
        const float v = vel > 1.0f ? 1.0f : vel;
        Voice* pick = nullptr;
        for (auto& vc : voice) if (vc.active && vc.note == note) { pick = &vc; break; }
        if (pick == nullptr) for (auto& vc : voice) if (! vc.active) { pick = &vc; break; }
        if (pick == nullptr)
        {
            for (auto& vc : voice) if (! vc.gate && (pick == nullptr || vc.age < pick->age)) pick = &vc;     // oldest released
            if (pick == nullptr) { pick = &voice[0]; for (auto& vc : voice) if (vc.age < pick->age) pick = &vc; }
        }
        Voice& vc = *pick;
        const bool wasSilent = ! vc.active;
        vc.note = note; vc.vel = 0.5f + 0.5f * v; vc.gate = true; vc.active = true; vc.age = ++voiceAge;
        vc.target = (float) note;
        if (glideMs > 0.0f && lastNote >= 0.0f) { if (wasSilent) vc.cur = lastNote; }
        else if (wasSilent) vc.cur = vc.target;          // a stolen sounding voice slides quickly (no phase reset, no click)
        lastNote = (float) note;
        setVoiceInc (vc);
    }
    void noteOff (int note) noexcept { for (auto& vc : voice) if (vc.active && vc.note == note) vc.gate = false; }
    void allNotesOff() noexcept { for (auto& vc : voice) vc.gate = false; }

    // ---- status -------------------------------------------------------------
    int   getLatencySamples() const noexcept   { return 0; }
    bool  sidechainMissing() const noexcept    { return scMissing; }          // mode Track without a key input
    int   getNumBands() const noexcept         { return nb; }
    int   getActiveVoices() const noexcept     { int n = 0; for (auto& v : voice) n += v.gate ? 1 : 0; return n; }
    float getDetectedPitchHz() const noexcept  { return robotVoiced ? detectedHz : 0.0f; }     // Robot mode, 0 = unvoiced
    float getUnvoicedAmount() const noexcept   { return uS; }                 // 0 = vowel, 1 = consonant noise (for a display)
    int   getRobotNote() const noexcept        { return robotNote; }           // quantised MIDI note, -1 = none / free
    float getBandLevel (int b) const noexcept  { return (b >= 0 && b < nb) ? mSm[b] / (mr + 1.0e-9f) : 0.0f; }   // for a band display
    float getBandFrequency (int b) const noexcept { return (b >= 0 && b < nb) ? bandHz[b] : 0.0f; }
    float getTailSeconds() const noexcept      { return releaseToMs (tRelease) * 0.004f + 0.06f; }

    // ---- processing ---------------------------------------------------------
    // in  = insert input (mode Track: carrier; modes Synth / Robot: voice), inR may be nullptr
    // sc  = sidechain / key (mode Track: voice), may be nullptr; scR may be nullptr
    // out = result, outR may be nullptr (mono). Buffers may alias.
    void process (const float* inL, const float* inR, const float* scL, const float* scR,
                  float* outL, float* outR, int n) noexcept
    {
        if (inL == nullptr || outL == nullptr || n <= 0) return;
        if (! prepared)
        {
            for (int i = 0; i < n; ++i) { const float l = inL[i], r = inR ? inR[i] : l; outL[i] = l; if (outR) outR[i] = r; }
            return;
        }
        if (inR == nullptr) inR = inL;
        if (scL != nullptr && scR == nullptr) scR = scL;

        const float m = tMix;
        for (int i = 0; i < n; ++i)
        {
            if (ctlCount <= 0)
            {
                control();
                ctlCount = ctl;
                const bool noKey = (cfgMode == carrierTrack && scL == nullptr);
                scMissing = noKey;
                const float mm = noKey ? 0.0f : m;
                gdT = std::cos (1.5707963f * mm); gwT = std::sin (1.5707963f * mm);
                if (mixSnap) { gDry = gdT; gWet = gwT; mixSnap = false; }
            }
            --ctlCount;

            const float dl = clean (inL[i]), dr = clean (inR[i]);
            const float dn = rnd() * kDnNoise;
            float car, mod;
            if (cfgMode == carrierTrack)
            {
                car = 0.5f * (dl + dr);
                mod = scL != nullptr ? 0.5f * (clean (scL[i]) + clean (scR[i])) : 0.0f;
            }
            else
            {
                mod = 0.5f * (dl + dr);
                car = cfgMode == carrierSynth ? synthSample() : (cfgMode == carrierHarmony ? harmonySample() : robotSample());
            }

            float wl, wr;
            vocode (mod + dn, car + dn, wl, wr);

            gDry += mixK * (gdT - gDry); gWet += mixK * (gwT - gWet);
            const float ol = dl * gDry + wl * gWet, orr = dr * gDry + wr * gWet;
            if (outR != nullptr) { outL[i] = ol; outR[i] = orr; }
            else                 { outL[i] = 0.5f * (ol + orr); }
        }
        if (gdT >= 1.0f && std::abs (gDry - 1.0f) < 1.0e-6f) { gDry = 1.0f; gWet = 0.0f; }
    }

private:
    // ---- constants -----------------------------------------------------------
    static constexpr float kDn = 1.0e-18f;           // anti-denormal bias
    static constexpr float kDnNoise = 1.0e-9f;       // -180 dB noise keeps the filter banks out of the denormal range
    static constexpr double kFLo = 100.0, kFHi = 10000.0;
    static constexpr int kPitBuf = 512;
    static constexpr float kSynthAmp = 0.15f;

    struct Svf   { float a1 = 0, a2 = 0, a3 = 0, k = 1, s1 = 0, s2 = 0; };
    struct Voice
    {
        float ph[3] = { 0.13f, 0.57f, 0.81f }, inc[3] = { 0, 0, 0 }, rinc[3] = { 1, 1, 1 };
        float env = 0, vel = 1, cur = 60, target = 60;
        int note = -1; std::uint32_t age = 0; bool gate = false, active = false;
    };

    // ---- one sample of the vocoder core (100 % wet) -----------------------------
    inline void vocode (float mod, float car, float& outL, float& outR) noexcept
    {
        // modulator conditioning + detectors
        hpS += hpA * (mod - hpS); const float xm = mod - hpS;
        lp1 += lpDetA * (xm - lp1); lp2 += lpDetA * (lp1 - lp2);
        float hpm;
        {
            Svf& f = sibHp;
            const float v3 = xm - f.s2, v1 = f.a1 * f.s1 + f.a2 * v3, v2 = f.s2 + f.a2 * f.s1 + f.a3 * v3;
            f.s1 = 2.0f * v1 - f.s1; f.s2 = 2.0f * v2 - f.s2;
            hpm = xm - f.k * v1 - v2;
        }
        eH += eDetA * (hpm * hpm - eH); eL += eDetA * (lp2 * lp2 - eL);
        { const float a = std::abs (xm); mb += (a > mb ? mbAtt : mbRel) * (a - mb); }
        if (cfgMode == carrierRobot || cfgMode == carrierHarmony) pitchFeed (xm);
        cTot += car * car;

        // ---- the two filter banks -------------------------------------------------
        const int N = nb;
        float o[kMaxBands];
        for (int b = 0; b < N; ++b)
        {
            // analysis: 2 SVF band-pass sections, rectifier, attack/release, smoother
            float v3 = mod - mA2[b];
            float v1 = mAa1[b] * mA1[b] + mAa2[b] * v3;
            float v2 = mA2[b] + mAa2[b] * mA1[b] + mAa3[b] * v3;
            mA1[b] = 2.0f * v1 - mA1[b]; mA2[b] = 2.0f * v2 - mA2[b];
            v3 = mAk[b] * v1 - mB2[b];
            v1 = mBa1[b] * mB1[b] + mBa2[b] * v3;
            v2 = mB2[b] + mBa2[b] * mB1[b] + mBa3[b] * v3;
            mB1[b] = 2.0f * v1 - mB1[b]; mB2[b] = 2.0f * v2 - mB2[b];
            const float r = std::abs (mBk[b] * v1);
            float e = mEnv[b];
            e += (r > e ? eAtt[b] : eRel[b]) * (r - e);
            mEnv[b] = e;
            const float s = mSm[b] + eSm[b] * (e - mSm[b]);
            mSm[b] = s;

            // synthesis: 2 SVF band-pass sections on the carrier
            v3 = car - cA2[b];
            v1 = cAa1[b] * cA1[b] + cAa2[b] * v3;
            v2 = cA2[b] + cAa2[b] * cA1[b] + cAa3[b] * v3;
            cA1[b] = 2.0f * v1 - cA1[b]; cA2[b] = 2.0f * v2 - cA2[b];
            v3 = cAk[b] * v1 - cB2[b];
            v1 = cBa1[b] * cB1[b] + cBa2[b] * v3;
            v2 = cB2[b] + cBa2[b] * cB1[b] + cBa3[b] * v3;
            cB1[b] = 2.0f * v1 - cB1[b]; cB2[b] = 2.0f * v2 - cB2[b];
            const float c = cBk[b] * v1;
            cAcc[b] += c * c;
            const float g = bGain[b] + bInc[b];                 // band gain, linearly interpolated between control ticks
            bGain[b] = g;
            o[b] = c * s * g;
        }
        float m0 = 0.0f, m1 = 0.0f, s0 = 0.0f, s1 = 0.0f;
        int b = 0;
        for (; b + 1 < N; b += 2) { m0 += o[b]; m1 += o[b + 1]; s0 += o[b] * bPan[b]; s1 += o[b + 1] * bPan[b + 1]; }
        if (b < N) { m0 += o[b]; s0 += o[b] * bPan[b]; }
        const float sumM = m0 + m1, sumS = s0 + s1;
        lastM = sumM; lastS = sumS;

        // ---- level: scalar gain (gate, normalisation, auto-gain) -------------------
        gs += gsA * (gsT - gs) + kDn;
        { const float v = sumM * gNoAgc; pWet += pwA * (v * v - pWet); }
        { const float t = xm * lcOverMr * gate; pTgt += pwA * (t * t - pTgt); }
        float wl = (sumM + sumS) * gs, wr = (sumM - sumS) * gs;

        // ---- consonants ----------------------------------------------------------------
        uS += (uT > uS ? uAtt : uRel) * (uT - uS) + kDn;
        if (uS > 1.0e-4f)
        {
            const float nz = rnd();
            float sn = 0.0f;
            for (int i = 0; i < 3; ++i)
            {
                Svf& f = nzBp[i];
                const float v3 = nz - f.s2, v1 = f.a1 * f.s1 + f.a2 * v3, v2 = f.s2 + f.a2 * f.s1 + f.a3 * v3;
                f.s1 = 2.0f * v1 - f.s1; f.s2 = 2.0f * v2 - f.s2;
                sn += f.k * v1 * nzG[i];
            }
            const float sib = uS * (hpm * sibG + sn);
            wl += sib; wr += sib;
        }

        // ---- fade (band / mode changes), ensemble, limiter -------------------------------
        if (fade != fadeT) { fade += fade < fadeT ? fadeStep : -fadeStep; fade = fade < 0.0f ? 0.0f : (fade > 1.0f ? 1.0f : fade); }
        { const float fs = fade * fade * (3.0f - 2.0f * fade); wl *= fs; wr *= fs; }
        ensemble (wl, wr);
        wl = soft (wl, 1.0f, 2.0f); wr = soft (wr, 1.0f, 2.0f);
        outL = (std::abs (wl) <= 2.0f) ? wl : 0.0f;       // also catches NaN
        outR = (std::abs (wr) <= 2.0f) ? wr : 0.0f;
    }

    // ---- mild 3-phase ensemble (string-machine style) on the wet signal ------------------
    inline void ensemble (float& l, float& r) noexcept
    {
        float* const bl = ensL.data(); float* const br = ensR.data();
        const int w = ensW, mk = ensMask;
        bl[w] = l; br[w] = r;
        ensW = (w + 1) & mk;
        if (sEns > 1.0e-4f)
        {
            float t[3];
            for (int k = 0; k < 3; ++k)
            {
                const float d = ensD[k]; ensD[k] = d + ensInc[k];
                const int di = (int) d; const float f = d - (float) di;
                const float* buf = (k == 0) ? bl : (k == 2 ? br : nullptr);
                const int i1 = (w - di) & mk, i0 = (i1 + 1) & mk, i2 = (i1 - 1) & mk, i3 = (i1 - 2) & mk;
                if (buf != nullptr) t[k] = herm (buf[i0], buf[i1], buf[i2], buf[i3], f);
                else                t[k] = 0.5f * (herm (bl[i0], bl[i1], bl[i2], bl[i3], f) + herm (br[i0], br[i1], br[i2], br[i3], f));
            }
            const float e = sEns;
            l = l * (1.0f - 0.45f * e) + e * 0.62f * (t[0] + 0.7f * t[1]);
            r = r * (1.0f - 0.45f * e) + e * 0.62f * (t[2] + 0.7f * t[1]);
        }
    }
    static inline float herm (float y0, float y1, float y2, float y3, float f) noexcept
    {
        const float c1 = 0.5f * (y2 - y0), c2 = y0 - 2.5f * y1 + 2.0f * y2 - 0.5f * y3, c3 = 0.5f * (y3 - y0) + 1.5f * (y1 - y2);
        return ((c3 * f + c2) * f + c1) * f + y1;
    }

    // ---- internal carrier synth ------------------------------------------------------------
    static inline float blep (float t, float d, float rd) noexcept
    {
        if (t < d)          { const float x = t * rd; return x + x - x * x - 1.0f; }
        if (t > 1.0f - d)   { const float x = (t - 1.0f) * rd; return x * x + x + x + 1.0f; }
        return 0.0f;
    }
    static inline float saw (float t, float d, float rd) noexcept { return 2.0f * t - 1.0f - blep (t, d, rd); }
    inline float voiceSample (Voice& v) noexcept
    {
        float s = 0.0f;
        for (int k = 0; k < 2; ++k)
        {
            float p = v.ph[k] + v.inc[k]; if (p >= 1.0f) p -= 1.0f; v.ph[k] = p;
            s += saw (p, v.inc[k], v.rinc[k]);
        }
        float p = v.ph[2] + v.inc[2]; if (p >= 1.0f) p -= 1.0f; v.ph[2] = p;
        float p2 = p + 0.31f; if (p2 >= 1.0f) p2 -= 1.0f;
        s += 1.3f * saw (p, v.inc[2], v.rinc[2]) - 0.6f * saw (p2, v.inc[2], v.rinc[2]);     // saw + 31 % pulse
        return s * v.env * v.vel;
    }
    inline float synthSample() noexcept
    {
        float s = 0.0f, em = 0.0f;
        for (auto& v : voice)
        {
            if (! v.active) continue;
            v.env += (v.gate ? vAtt : vRel) * ((v.gate ? 1.0f : 0.0f) - v.env);
            if (! v.gate && v.env < 1.0e-5f) { v.env = 0.0f; v.active = false; continue; }
            s += voiceSample (v);
            em = std::max (em, v.env);
        }
        return kSynthAmp * (s + 0.12f * em * rnd());
    }
    inline float robotSample() noexcept
    {
        robot.env += vAtt * (1.0f - robot.env);
        return kSynthAmp * (voiceSample (robot) + 0.12f * rnd());
    }
    // robot trojhlas: hlas + terca + kvinta (v tonine piesne, inak durovy akord)
    inline float harmonySample() noexcept
    {
        robot.env += vAtt * (1.0f - robot.env);
        for (auto& h : harm) h.env = robot.env;
        return kSynthAmp * (0.62f * (voiceSample (robot) + 0.8f * voiceSample (harm[0]) + 0.8f * voiceSample (harm[1])) + 0.12f * rnd());
    }
    int diatonicAbove (int note, int steps) const noexcept
    {
        static const std::uint16_t masks[6] = { 0xAB5, 0x5AD, 0xFFF, 0x9AD, 0x295, 0x4A9 };
        const std::uint16_t mk = masks[keyScale < 0 ? 2 : keyScale];
        if (keyScale < 0 || keyScale == 2) return note + (steps == 2 ? 4 : 7);   // bez toniny: durova terca a kvinta
        int n = note, left = steps;
        while (left > 0 && n < note + 12) { ++n; const int deg = (((n - keyTonic) % 12) + 12) % 12; if ((mk >> deg) & 1) --left; }
        return n;
    }
    void setHarmonyTargets (float root) noexcept
    {
        if (keyScale < 0) { harm[0].target = root + 4.0f; harm[1].target = root + 7.0f; return; }
        const int r = (int) std::floor (root + 0.5f);
        harm[0].target = (float) diatonicAbove (r, 2); harm[1].target = (float) diatonicAbove (r, 4);
    }
    void setVoiceInc (Voice& v) noexcept
    {
        static const float det[3] = { 0.99481f, 1.00521f, 1.0f };          // -9 / +9 / 0 cents
        const double f = 440.0 * std::exp2 (((double) v.cur - 69.0) / 12.0);
        for (int k = 0; k < 3; ++k)
        {
            const float inc = (float) std::min (0.45, f * det[k] / sr);
            v.inc[k] = inc; v.rinc[k] = 1.0f / inc;
        }
    }
    void glideVoice (Voice& v, float coef) noexcept
    {
        if (v.cur == v.target) return;
        v.cur += coef * (v.target - v.cur);
        if (std::abs (v.cur - v.target) < 0.002f) v.cur = v.target;
        setVoiceInc (v);
    }
    float robotStartPitch() const noexcept { return keyScale >= 0 ? (float) (36 + keyTonic) : 45.0f; }

    // ---- pitch tracker (Robot carrier) -----------------------------------------------------
    inline void pitchFeed (float x) noexcept
    {
        for (auto& f : pitLp)
        {
            const float v3 = x - f.s2, v1 = f.a1 * f.s1 + f.a2 * v3, v2 = f.s2 + f.a2 * f.s1 + f.a3 * v3;
            f.s1 = 2.0f * v1 - f.s1; f.s2 = 2.0f * v2 - f.s2;
            x = v2;
        }
        if (++pitCnt < pitDec) return;
        pitCnt = 0;
        pitBuf[pitPos] = x; pitPos = (pitPos + 1) & (kPitBuf - 1);
        if (++pitHopCnt >= pitHop) { pitHopCnt = 0; pitchAnalyse(); }
    }
    void pitchAnalyse() noexcept
    {
        const int W = pitW, tMax = pitTauMax;
        float x[kPitBuf];
        double e = 0.0;
        for (int i = 0; i < W; ++i) { x[i] = pitBuf[(pitPos - W + i) & (kPitBuf - 1)]; e += (double) x[i] * x[i]; }
        const float rms = (float) std::sqrt (e / (double) W);
        bool voiced = false; float hz = 0.0f;
        if (rms > std::max (2.0e-4f, 0.03f * mr))
        {
            // normalised square difference function (McLeod), first strong peak
            float nsdf[kPitBuf / 2 + 2];
            float m = (float) (2.0 * e);
            nsdf[0] = 1.0f;
            for (int t = 1; t <= tMax + 1; ++t)
            {
                m -= x[t - 1] * x[t - 1] + x[W - t] * x[W - t];
                float a0 = 0.0f, a1 = 0.0f;
                const int n = W - t; int j = 0;
                for (; j + 1 < n; j += 2) { a0 += x[j] * x[j + t]; a1 += x[j + 1] * x[j + 1 + t]; }
                if (j < n) a0 += x[j] * x[j + t];
                nsdf[t] = 2.0f * (a0 + a1) / (m + 1.0e-12f);
            }
            int t = 1;
            while (t <= tMax && nsdf[t] > 0.0f) ++t;                 // leave the lobe around lag 0
            float best = 0.0f;
            int peaks[24]; int np = 0;
            while (t <= tMax)
            {
                while (t <= tMax && nsdf[t] <= 0.0f) ++t;
                int pk = -1; float pv = 0.0f;
                while (t <= tMax && nsdf[t] > 0.0f) { if (nsdf[t] > pv) { pv = nsdf[t]; pk = t; } ++t; }
                if (pk >= pitTauMin && pk < tMax && np < 24) { peaks[np++] = pk; best = std::max (best, pv); }
            }
            if (best > 0.55f)
                for (int i = 0; i < np; ++i)
                {
                    const int pk = peaks[i];
                    if (nsdf[pk] >= 0.88f * best)
                    {
                        const float y0 = nsdf[pk - 1], y1 = nsdf[pk], y2 = nsdf[pk + 1];
                        const float den = y0 - 2.0f * y1 + y2;
                        const float off = std::abs (den) > 1.0e-9f ? 0.5f * (y0 - y2) / den : 0.0f;
                        hz = (float) (pitFs / ((double) pk + (double) std::min (0.5f, std::max (-0.5f, off))));
                        voiced = true;
                        break;
                    }
                }
        }
        robotVoiced = voiced;
        if (! voiced) { pitHistN = 0; robotCandN = 0; return; }      // hold the last note through consonants / pauses

        const float p = 69.0f + 12.0f * std::log2 (hz / 440.0f);
        pitHist[2] = pitHist[1]; pitHist[1] = pitHist[0]; pitHist[0] = p;
        if (pitHistN < 3) ++pitHistN;
        float pm = p;
        if (pitHistN >= 3) pm = std::max (std::min (pitHist[0], pitHist[1]), std::min (std::max (pitHist[0], pitHist[1]), pitHist[2]));
        else if (pitHistN < 2) return;                               // need two voiced frames in a row
        pm = std::min (84.0f, std::max (28.0f, pm));
        detectedHz = 440.0f * std::exp2 ((pm - 69.0f) / 12.0f);

        if (keyScale < 0) { robotNote = -1; robot.target = pm; setHarmonyTargets (pm); return; }
        const int q = nearestScaleNote (pm);
        if (robotNote < 0) robotNote = q;
        else if (q != robotNote && std::abs (pm - (float) q) + 0.5f < std::abs (pm - (float) robotNote))
        {
            if (q == robotCand) { if (++robotCandN >= 4) { robotNote = q; robotCandN = 0; } }
            else { robotCand = q; robotCandN = 1; }
        }
        else robotCandN = 0;
        robot.target = (float) robotNote;
        setHarmonyTargets ((float) robotNote);
    }
    int nearestScaleNote (float p) const noexcept
    {
        static const std::uint16_t masks[6] = { 0xAB5, 0x5AD, 0xFFF, 0x9AD, 0x295, 0x4A9 };   // bit i = semitone i above the tonic
        const std::uint16_t mk = masks[keyScale < 0 ? 2 : keyScale];
        const int c = (int) std::floor (p + 0.5f);
        int best = c; float bd = 1.0e9f;
        for (int n = c - 6; n <= c + 6; ++n)
        {
            const int deg = (((n - keyTonic) % 12) + 12) % 12;
            if (((mk >> deg) & 1) == 0) continue;
            const float d = std::abs (p - (float) n);
            if (d < bd) { bd = d; best = n; }
        }
        return best;
    }

    // ---- control rate ----------------------------------------------------------------------
    void control() noexcept
    {
        // band count / carrier mode change: fade the wet signal out, switch, fade in
        if (tBands != cfgBands || tMode != cfgMode)
        {
            fadeT = 0.0f;
            if (fade <= 0.0f)
            {
                if (tBands != cfgBands) applyBands (tBands);
                if (tMode != cfgMode)
                {
                    // Synth <-> Robot keep the modulator statistics (same voice); Track swaps the roles of the inputs
                    const bool swap = (tMode == carrierTrack) != (cfgMode == carrierTrack);
                    cfgMode = tMode;
                    clearBankState();
                    if (swap) { agc = 1.0f; pWet = pTgt = 0.0f; mr = 0.05f; mb = 0.0f; eH = eL = 0.0f; uS = uT = 0.0f; }
                    lvlN = kSynthAmp * 1.2f;
                    robot.env = 0.0f;
                }
                lastFmt = -100.0f; curRelease = -1.0f; lastSpread = -1.0f;
                fadeHold = (int) (0.012 / dt) + 1;       // let the band statistics settle before fading in
            }
        }
        else if (fadeHold > 0) --fadeHold;
        else fadeT = 1.0f;

        // smoothed parameters
        sFormant += fmtTick * (tFormant - sFormant);
        if (std::abs (tFormant - sFormant) < 1.0e-4f) sFormant = tFormant;
        sClarity += parTick * (tClarity - sClarity);
        sSpread  += parTick * (tSpread - sSpread);
        sEns     += parTick * (tEns - sEns) + kDn;
        if (sFormant != lastFmt) { designCarrier (sFormant); lastFmt = sFormant; }
        if (tRelease != curRelease) { designEnvelopes (tRelease); curRelease = tRelease; }
        if (std::abs (sSpread - lastSpread) > 1.0e-4f)
        {
            lastSpread = sSpread;
            for (int b = 0; b < nb; ++b)
            {
                const float w = std::min (1.0f, std::max (0.0f, (float) std::log2 (bandHz[b] / 250.0f)));    // lows stay centred
                bPan[b] = ((b & 1) ? -1.0f : 1.0f) * 0.4f * sSpread * w;
            }
        }
        const float cl = sClarity;

        // slow modulator level, gate / expander
        mr += (mb > mr ? mrAttTick : mrRelTick) * (mb - mr);
        if (mr < 0.004f) mr = 0.004f;
        const float thr = std::max (3.0e-4f, 0.02f * mr);
        float gg = mb / thr; gg = gg > 1.0f ? 1.0f : gg * gg;
        gate = gg;

        // unvoiced amount: high band against low band
        {
            const float ratio = eH / (eH + 0.6f * eL + 1.0e-10f);
            float u = (ratio - 0.45f) * (1.0f / 0.3f);
            u = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            uT = u * u * (3.0f - 2.0f * u) * gg;
        }

        // carrier whitening + band gains
        const float inv = 1.0f / (float) ctl;
        pcW += kwTick * (cTot * inv - pcW); cTot = 0.0f;
        const float wt = 0.35f + 0.55f * cl;                    // blend: 0 = half whitening (in dB), 1 = full
        const float tiltExp = 0.2f * cl;
        // while a consonant sounds, the clean consonant paths take over from the vocoded top bands
        const float duck = 0.75f * uS * std::min (1.0f, 1.6f * cl);
        for (int b = 0; b < nb; ++b)
        {
            cB2pow[b] += kwTick * (cAcc[b] * inv - cB2pow[b]); cAcc[b] = 0.0f;
            float pr = pcW / (cB2pow[b] + 1.0e-10f);
            pr = pr < 1.0f ? 1.0f : (pr > 4.0e4f ? 4.0e4f : pr);
            const float ra = std::sqrt (pr);
            const float w = (1.0f - wt) * std::sqrt (ra) + wt * ra;
            const float hf = bTilt[b] - 1.3f;                   // 0 below 2.5 kHz .. 1 above 5 kHz
            const float gt = w * bFade[b] * (1.0f + tiltExp * bTilt[b]) * (1.0f - duck * (hf < 0.0f ? 0.0f : (hf > 1.0f ? 1.0f : hf)));
            bInc[b] = (gt - bGain[b]) * inv;
        }

        // level bookkeeping: what the wet signal should be as loud as
        float carNorm = 1.0f;
        if (cfgMode == carrierTrack)
            lcOverMr = 2.0f * std::sqrt (pcW) / mr;             // carrier level, +6 dB (speech crest)
        else
        {
            float l2 = 0.0f;
            if (cfgMode == carrierSynth) { for (auto& v : voice) if (v.active) l2 += v.env * v.env * v.vel * v.vel; }
            else l2 = robot.env * robot.env;
            const float lvl = kSynthAmp * 1.2f * std::sqrt (l2);
            lvlN += (lvl > lvlN ? 0.25f : lvlRelTick) * (lvl - lvlN);
            carNorm = mr / std::max (lvlN, kSynthAmp * 0.6f);
            lcOverMr = 0.7f;                                    // voice level - 3 dB (vocoded crest factor is higher)
        }
        if (pTgt > 1.0e-9f && pWet > 1.0e-14f)
        {
            float gd = std::sqrt (pTgt / pWet);
            gd = gd < 0.25f ? 0.25f : (gd > 16.0f ? 16.0f : gd);
            agc += agcTick * (gd - agc);
        }
        gNoAgc = gg * carNorm / mr;
        gsT = gNoAgc * agc;

        // consonant paths
        sibG = lcOverMr * 1.2f * cl;
        {
            float eg[3] = { 0.0f, 0.0f, 0.0f };
            for (int b = 0; b < nb; ++b)
            {
                const float f = bandHz[b];
                if (f < 2000.0f) continue;
                const int g = f < 3800.0f ? 0 : (f < 6500.0f ? 1 : 2);
                eg[g] += mSm[b] * mSm[b];
            }
            const float k = lcOverMr * 1.3f * cl;
            for (int g = 0; g < 3; ++g) nzG[g] = k * std::sqrt (eg[g]) * nzNorm[g];
        }

        // carrier synth pitch
        if (cfgMode == carrierSynth)
        {
            const float gc = glideMs > 0.0f ? (float) (1.0 - std::exp (-dt / (0.001 * glideMs * 0.35))) : 0.5f;
            for (auto& v : voice) if (v.active) glideVoice (v, gc);
        }
        else if (cfgMode == carrierRobot || cfgMode == carrierHarmony)
        {
            const float gc = (float) (1.0 - std::exp (-dt / (keyScale >= 0 ? 0.012 : 0.03)));
            glideVoice (robot, gc);
            if (cfgMode == carrierHarmony) for (auto& h : harm) glideVoice (h, gc);
        }

        // ensemble LFOs
        if (sEns > 1.0e-4f)
        {
            ensPhS += 0.63 * dt; if (ensPhS >= 1.0) ensPhS -= 1.0;
            ensPhF += 5.9 * dt;  if (ensPhF >= 1.0) ensPhF -= 1.0;
            for (int k = 0; k < 3; ++k)
            {
                const double d = sr * (0.0075 + 0.0021 * std::sin (6.283185307 * (ensPhS + k / 3.0))
                                              + 0.00022 * std::sin (6.283185307 * (ensPhF + k / 3.0)));
                ensInc[k] = ((float) d - ensD[k]) * inv;
            }
        }
        else for (int k = 0; k < 3; ++k) ensInc[k] = 0.0f;

        // state guard
        float g = lastM + lastS + mr + agc + pcW + gs + pWet + pTgt + uS + eH + eL;
        for (int b = 0; b < nb; ++b) g += mSm[b] + cB2pow[b];
        if (! finite (g) || std::abs (g) > 1.0e12f) { reset(); fade = 0.0f; }
    }

    // ---- filter design ---------------------------------------------------------------------
    // 4th-order Butterworth band-pass (centre f0, -3 dB width bw) as two TPT SVF band-pass sections
    void designBand (double f0, double bw, float* a1A, float* a2A, float* a3A, float* kA,
                     float* a1B, float* a2B, float* a3B, float* kB) const noexcept
    {
        const double Q = f0 / bw;
        const std::complex<double> p (-0.7071067811865476, 0.7071067811865476);
        const std::complex<double> a = p / Q, disc = std::sqrt (a * a - 4.0);
        const std::complex<double> r[2] = { (a + disc) * 0.5, (a - disc) * 0.5 };
        double g[2], k[2], h[2];
        const double x0 = std::tan (3.141592653589793 * std::min (f0, sr * 0.49) / sr);
        for (int i = 0; i < 2; ++i)
        {
            const double w = std::abs (r[i]);
            k[i] = -2.0 * r[i].real() / w;
            g[i] = std::tan (3.141592653589793 * std::min (f0 * w, sr * 0.49) / sr);
            const double x = x0 / g[i];
            h[i] = k[i] * x / std::sqrt ((1.0 - x * x) * (1.0 - x * x) + k[i] * k[i] * x * x);
        }
        const auto put = [] (double gg, double kk, double hh, float* a1, float* a2, float* a3, float* ko)
        {
            const double d = 1.0 / (1.0 + gg * (gg + kk));
            *a1 = (float) d; *a2 = (float) (gg * d); *a3 = (float) (gg * gg * d); *ko = (float) (kk / hh);
        };
        put (g[0], k[0], h[0], a1A, a2A, a3A, kA);
        put (g[1], k[1], h[1], a1B, a2B, a3B, kB);
    }
    void applyBands (int n) noexcept
    {
        n = n < kMinBands ? kMinBands : (n > kMaxBands ? kMaxBands : n);
        cfgBands = n; nb = n;
        const double fhi = std::min (kFHi, sr * 0.42);
        const double ratio = std::pow (fhi / kFLo, 1.0 / (double) n);
        for (int b = 0; b < n; ++b)
        {
            const double lo = kFLo * std::pow (ratio, (double) b), hi = lo * ratio;
            const double f0 = std::sqrt (lo * hi);
            bandHz[b] = (float) f0; bandBw[b] = (float) (hi - lo);
            designBand (f0, hi - lo, &mAa1[b], &mAa2[b], &mAa3[b], &mAk[b], &mBa1[b], &mBa2[b], &mBa3[b], &mBk[b]);
            bTilt[b] = f0 > 1000.0 ? (float) std::log2 (f0 / 1000.0) : 0.0f;       // octaves above 1 kHz
            bPan[b] = 0.0f; bGain[b] = 0.0f; bInc[b] = 0.0f;
        }
        designCarrier (sFormant);
        designEnvelopes (tRelease);
        clearBankState();
    }
    void designCarrier (float semitones) noexcept
    {
        const double ratio = std::exp2 ((double) semitones / 12.0);
        for (int b = 0; b < nb; ++b)
        {
            const double f0 = (double) bandHz[b] * ratio;
            const double fc = std::min (f0, sr * 0.46);
            designBand (fc, (double) bandBw[b] * fc / (double) bandHz[b],
                        &cAa1[b], &cAa2[b], &cAa3[b], &cAk[b], &cBa1[b], &cBa2[b], &cBa3[b], &cBk[b]);
            // bands pushed against Nyquist fade out
            const double x = (f0 / sr - 0.36) / (0.45 - 0.36);
            bFade[b] = (float) (x <= 0.0 ? 1.0 : (x >= 1.0 ? 0.0 : 1.0 - x));
        }
    }
    void designEnvelopes (float release01) noexcept
    {
        const double relS = 0.001 * (double) releaseToMs (release01);
        for (int b = 0; b < nb; ++b)
        {
            const double f = (double) bandHz[b];
            const double att = std::min (0.008, std::max (0.0008, 1.6 / f));
            const double rel = std::max (relS, 2.5 / f);
            eAtt[b] = tc (att); eRel[b] = tc (rel); eSm[b] = tc (0.6 * att + 0.04 * rel);
        }
    }
    void clearBankState() noexcept
    {
        for (int b = 0; b < kMaxBands; ++b)
        {
            mA1[b] = mA2[b] = mB1[b] = mB2[b] = 0.0f;
            cA1[b] = cA2[b] = cB1[b] = cB2[b] = 0.0f;
            mEnv[b] = mSm[b] = 0.0f; cAcc[b] = 0.0f; cB2pow[b] = 0.0f;
        }
        cTot = 0.0f; pcW = 0.0f; lastM = lastS = 0.0f;
    }

    // ---- helpers ---------------------------------------------------------------------------
    void setSvf (Svf& f, double hz, double q) const noexcept
    {
        const double g = std::tan (3.141592653589793 * std::min (hz, sr * 0.45) / sr), k = 1.0 / q;
        const double d = 1.0 / (1.0 + g * (g + k));
        f.a1 = (float) d; f.a2 = (float) (g * d); f.a3 = (float) (g * g * d); f.k = (float) k;
    }
    static void clearSvf (Svf& f) noexcept { f.s1 = f.s2 = 0.0f; }
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
    float tc (double seconds) const noexcept { return (float) (1.0 - std::exp (-1.0 / (seconds * sr))); }
    float tcTick (double seconds) const noexcept { return (float) (1.0 - std::exp (-dt / seconds)); }
    static int allocPow2 (std::vector<float>& v, int minLen)
    {
        int n = 64; while (n < minLen) n <<= 1;
        v.assign ((size_t) n, 0.0f);
        return n - 1;
    }
    inline float rnd() noexcept       // white, uniform -1..1
    {
        seed = seed * 1664525u + 1013904223u;
        return (float) (std::int32_t) seed * 4.6566129e-10f;
    }

    // ---- state -----------------------------------------------------------------------------
    double sr = 48000.0, dt = 16.0 / 48000.0;
    int ctl = 16, ctlCount = 0;
    bool prepared = false;
    std::uint32_t seed = 0x1234567u;

    // targets
    int tMode = 0, tBands = 32;
    float tFormant = 0.0f, tClarity = 0.6f, tRelease = 0.35f, tMix = 1.0f, tSpread = 0.5f, tEns = 0.2f;
    float glideMs = 0.0f;
    int keyTonic = 0, keyScale = -1;
    // current
    int cfgMode = 0, cfgBands = 0, nb = 32;
    float sFormant = 0.0f, sClarity = 0.6f, sSpread = 0.5f, sEns = 0.2f;
    float lastFmt = -100.0f, curRelease = -1.0f, lastSpread = -1.0f;
    float fmtTick = 0, parTick = 0;

    // banks (structure of arrays)
    alignas (32) float mAa1[kMaxBands] {}, mAa2[kMaxBands] {}, mAa3[kMaxBands] {}, mAk[kMaxBands] {};
    alignas (32) float mBa1[kMaxBands] {}, mBa2[kMaxBands] {}, mBa3[kMaxBands] {}, mBk[kMaxBands] {};
    alignas (32) float cAa1[kMaxBands] {}, cAa2[kMaxBands] {}, cAa3[kMaxBands] {}, cAk[kMaxBands] {};
    alignas (32) float cBa1[kMaxBands] {}, cBa2[kMaxBands] {}, cBa3[kMaxBands] {}, cBk[kMaxBands] {};
    alignas (32) float mA1[kMaxBands] {}, mA2[kMaxBands] {}, mB1[kMaxBands] {}, mB2[kMaxBands] {};
    alignas (32) float cA1[kMaxBands] {}, cA2[kMaxBands] {}, cB1[kMaxBands] {}, cB2[kMaxBands] {};
    alignas (32) float mEnv[kMaxBands] {}, mSm[kMaxBands] {}, eAtt[kMaxBands] {}, eRel[kMaxBands] {}, eSm[kMaxBands] {};
    alignas (32) float cAcc[kMaxBands] {}, cB2pow[kMaxBands] {}, bGain[kMaxBands] {}, bInc[kMaxBands] {}, bPan[kMaxBands] {};
    float bandHz[kMaxBands] {}, bandBw[kMaxBands] {}, bTilt[kMaxBands] {}, bFade[kMaxBands] {};

    // modulator analysis
    float hpA = 0, lpDetA = 0, eDetA = 0, mbAtt = 0, mbRel = 0, uAtt = 0, uRel = 0;
    float hpS = 0, lp1 = 0, lp2 = 0, eH = 0, eL = 0, mb = 0;
    float mr = 0.05f, gate = 0, uT = 0, uS = 0;
    float mrAttTick = 0, mrRelTick = 0;
    Svf sibHp, nzBp[3];
    float nzNorm[3] { 1, 1, 1 }, nzG[3] { 0, 0, 0 }, sibG = 0;

    // carrier statistics / level
    float cTot = 0, pcW = 0, kwTick = 0;
    float gsA = 0, gsT = 0, gs = 0, gNoAgc = 0, agc = 1, agcTick = 0, pwA = 0, pWet = 0, pTgt = 0, lcOverMr = 1;
    float lvlN = 0, lvlRelTick = 0;
    float lastM = 0, lastS = 0;

    // transitions / mix
    float fade = 0, fadeT = 1, fadeStep = 0.01f;
    int fadeHold = 0;
    float mixK = 0, gDry = 1, gWet = 0, gdT = 1, gwT = 0;
    bool mixSnap = true, scMissing = false;

    // ensemble
    std::vector<float> ensL, ensR;
    int ensMask = 0, ensW = 0;
    float ensD[3] { 0, 0, 0 }, ensInc[3] { 0, 0, 0 };
    double ensPhS = 0, ensPhF = 0;

    // internal synth / robot
    Voice voice[kVoices];
    Voice robot, harm[2];
    float vAtt = 0, vRel = 0, lastNote = -1;
    std::uint32_t voiceAge = 0;
    Svf pitLp[2];
    float pitBuf[kPitBuf] {};
    int pitDec = 6, pitPos = 0, pitCnt = 0, pitHop = 80, pitHopCnt = 0, pitW = 384, pitTauMin = 13, pitTauMax = 129;
    double pitFs = 8000.0;
    float pitHist[3] { 0, 0, 0 }; int pitHistN = 0;
    int robotNote = -1, robotCand = -1, robotCandN = 0;
    bool robotVoiced = false;
    float detectedHz = 0;
};

} // namespace korgus
