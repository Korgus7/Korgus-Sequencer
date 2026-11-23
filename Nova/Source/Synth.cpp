#include "../../Source/GatePatterns.h"
#include "Synth.h"

namespace nova
{
    Synth::Synth() { bank = PianoBank::get(); ibank = InstrBank::get(); }

    void Synth::prepare (double rate, int maxBlock)
    {
        sr = rate;
        for (auto* b : { &bufL, &bufR, &bufBL, &bufBR, &gateBuf, &voxBuf }) b->assign ((size_t) juce::jmax (1, maxBlock), 0);
        vocoder.prepare (rate, juce::jmax (1, maxBlock)); voxPrep.prepare (rate); deEss.prepare (rate); vocWasOn = false;
        layerEq[0].reset(); layerEq[1].reset();
        noise.s = 0x12345678u;
        for (size_t i = 0; i < voices.size(); ++i)
        {
            auto& v = voices[i];
            v.note = -1;
            v.rng.s = 0x12345u + (uint32_t) i * 7919u;   // pevny seed pre kazdy hlas: renderovanie je deterministicke
            for (auto& lv : v.lv) { lv.active = false; lv.dl.assign (4096, 0); }
        }
        WtBank::get();   // wavetable banka: postavi sa raz, mimo audio vlakna
        fx.prepare (sr);
        fx.an = &analyzer; analyzer.rate.store ((float) sr);
    }

    // ---------------------------------------------------------------- noty
    Synth::Voice* Synth::allocVoice()
    {
        int act = 0; Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.note >= 0)
            {
                ++act;
                const bool rel = ! v.held && ! v.sustained;
                const bool oRel = oldest != nullptr && ! oldest->held && ! oldest->sustained;
                if (oldest == nullptr || (rel && ! oRel) || (rel == oRel && v.age < oldest->age)) oldest = &v;
            }
        if (act >= 36 && oldest != nullptr)
        {
            for (auto& lv : oldest->lv) { lv.amp.kill (0.012f); lv.damping = true; lv.dampCoef = std::exp (-1.0f / (0.004f * (float) sr)); }
            oldest->held = oldest->sustained = false;
        }
        for (auto& v : voices) if (v.note < 0) return &v;
        return oldest != nullptr ? oldest : &voices[0];
    }

    void Synth::releaseVoice (Voice& v)
    {
        for (int li = 0; li < 2; ++li)
        {
            auto& lv = v.lv[li];
            if (! lv.active) continue;
            if (lv.wave == 7 && lv.smp != nullptr)
            {
                if (! lv.noDamper) { lv.damping = true; lv.dampCoef = std::exp (-1.0f / (std::max (0.01f, st.L[li].ar * 0.3f * relScale * lv.gain) * (float) sr)); }
            }
            else { lv.amp.off(); lv.fenv.off(); }
        }
    }

    void Synth::startLayer (Voice& v, int li)
    {
        const auto& s = st.L[li];
        auto& lv = v.lv[li];
        lv.active = false;
        if (! s.on) return;
        lv.wave = s.wave; lv.human = 0; lv.t = 0; lv.cnt = 0; lv.lfoPh = 0; lv.lfoHold = v.rng.next(); lv.peak = 0;
        lv.smp = nullptr; lv.user = nullptr; lv.damping = false; lv.level = 1;
        const float noteShift = s.oct * 12.0f + s.semi + s.fine * 0.01f;
        const float f0 = noteHz ((float) v.note + noteShift);
        const float vt = st.veltrack;
        lv.velGain = (1.0f - vt) + vt * std::pow (v.vel, s.wave == 6 ? 1.3f : 1.0f);
        float decay = s.ad;
        if (s.wave == 6) decay *= std::pow (220.0f / f0, 0.3f);
        lv.amp.set (s.aa * atkScale, decay, s.as, s.ar * relScale, (float) sr); lv.amp.v = 0; lv.amp.on();
        lv.fenv.set (s.fa * atkScale, s.fd, s.fs, s.fr * relScale, (float) sr); lv.fenv.v = 0; lv.fenv.on();
        lv.f1L.reset(); lv.f1R.reset(); lv.f2L.reset(); lv.f2R.reset();
        static const float bf[5] = { 280, 460, 1100, 2600, 3500 }, bq[5] = { 5, 5, 4, 4, 4 };
        for (int i = 0; i < 5; ++i) { lv.body[i].reset(); lv.body[i].set (bf[i], bq[i], (float) sr); }
        lv.tiltL.setHz (2200, (float) sr); lv.tiltR.setHz (2200, (float) sr); lv.tiltL.z = lv.tiltR.z = 0;
        lv.noiseL.setHz (200.0f * std::pow (2.0f, s.color * 7.0f), (float) sr);

        if (s.wave <= 4 || s.wave == 9)
        {
            lv.wtPos = -1; lv.wtMip = -1; lv.wtTab = -1;
            std::fill (std::begin (lv.osL), std::end (lv.osL), 0.0f); std::fill (std::begin (lv.osR), std::end (lv.osR), 0.0f); lv.osW = 0;
            const int n = juce::jlimit (1, 16, s.uni);
            for (int k = 0; k < n; ++k)
            {
                const float pos = n > 1 ? (float) k / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                const float cents = s.spread * pos * (0.85f + 0.3f * std::abs (v.rng.next()));
                lv.det[k] = std::pow (2.0f, cents / 1200.0f);
                lv.ph[k] = (n == 1 && (s.wave == 0 || s.wave == 9)) ? 0.0f : 0.5f + 0.5f * v.rng.next();
                const float pan = pos * (n >= 4 ? 0.95f : 0.8f);   // viac hlasov = sirsie stereo
                lv.pl[k] = std::cos ((pan + 1.0f) * pi * 0.25f); lv.pr[k] = std::sin ((pan + 1.0f) * pi * 0.25f);
            }
        }
        else if (s.wave == 5)
        {
            // Karplus-Strong: budenie sumom (jas podla Farby a sily), miesto brnknutia, ladenie allpassom
            lv.amp.set (s.aa * atkScale, 1.0f, 1.0f, s.ar * relScale, (float) sr); lv.amp.v = 0; lv.amp.on();
            const float brightness = clampf (s.color + 0.4f * bright, 0.0f, 1.0f);
            const float P = (float) sr / f0;
            lv.loopS = 0.5f - 0.42f * brightness;
            int N = (int) std::floor (P - lv.loopS - 0.2f);
            N = juce::jlimit (8, (int) lv.dl.size() - 1, N);
            const float d = P - lv.loopS - (float) N;
            lv.apC = (1.0f - d) / (1.0f + d); lv.apIn = lv.apOut = 0; lv.yPrev = 0; lv.dlen = N; lv.w = 0;
            OnePole lp; lp.setHz (600.0f * std::pow (2.0f, brightness * 5.0f + (v.vel - 0.7f) * 1.5f), (float) sr);
            std::vector<float> ex ((size_t) N);
            for (int i = 0; i < N; ++i) ex[(size_t) i] = lp.lp (v.rng.next());
            const int pick = juce::jmax (1, (int) (0.15f * (float) N));
            float mx = 1.0e-6f;
            for (int i = 0; i < N; ++i) { lv.dl[(size_t) i] = ex[(size_t) i] - (i >= pick ? ex[(size_t) (i - pick)] : 0.0f); mx = std::max (mx, std::abs (lv.dl[(size_t) i])); }
            for (int i = 0; i < N; ++i) lv.dl[(size_t) i] /= mx;
            const float t60 = clampf (s.ad * std::pow (220.0f / f0, 0.4f), 0.05f, 30.0f);
            lv.loopG = std::pow (10.0f, -3.0f / (t60 * f0));
            const float pan = clampf (((float) v.note - 64.0f) / 40.0f, -0.6f, 0.6f);
            lv.plPanL = std::cos ((pan + 1.0f) * pi * 0.25f); lv.plPanR = std::sin ((pan + 1.0f) * pi * 0.25f);
        }
        else if (s.wave == 8)
        {
            // Roj: pocet alikvot 4*uni (skutocny unison sa nepouziva); Schroederove fazy a pevne rozhodene startovne fazy
            const int N = juce::jmin (32, 4 * juce::jlimit (1, 16, s.uni));
            for (int k = 0; k < 32; ++k)
            {
                const float kk = (float) (k + 1);
                lv.rojPsi[k] = kk * kk / (2.0f * (float) N);              // psi_k / 2pi, psi_k = pi k^2 / N
                lv.rojPsi[k] -= std::floor (lv.rojPsi[k]);
                float th = lv.rojPsi[k] + rojScatter (k + 1 + 50) * 0.5f;
                lv.rojTh[k] = th - std::floor (th);
                lv.rojG[k] = 0; lv.rojC[k] = 0;
            }
            lv.rojPhi = 0; lv.rojFocus = -1; lv.rojN = 0; lv.rojTog = 0;
        }
        else if (s.wave == 6) { lv.cPh = lv.mPh = 0; }
        else
        {
            if (s.src == 0 && s.inst > 0 && s.inst != InstrBank::oldPiano)
            {
                // realisticky nastroj: vrstvy podla sily uderu, slucka pri drzanych tonoch
                ibank->request (s.inst);
                const auto* I = ibank->find (s.inst);
                if (I == nullptr) return;
                const int nl = I->nLayers;
                const int sn = juce::jlimit (0, 127, juce::roundToInt ((float) v.note + noteShift));
                const float lpos = v.vel * (float) nl - 1.0f + (s.color - 0.5f) * 2.0f;
                const int layer = juce::jlimit (0, nl - 1, (int) std::lround (lpos));
                lv.smp = I->map[(size_t) sn][(size_t) layer];
                if (lv.smp == nullptr) return;
                const float center = (float) (layer + 1) / (float) nl;
                lv.velGain = clampf (std::pow (v.vel / center, 0.9f), 0.08f, 2.0f) * std::pow (0.15f + 0.85f * v.vel, 1.6f) * I->gain;
                // "ludske" hranie: kazda nota trochu ina hlasitost; sláciky, dychy, gitary a basy aj jemne ladenie (klavesy a zvonky nie)
                lv.velGain *= 1.0f + 0.08f * v.rng.next();
                const bool fixedPitch = s.inst <= 3 || (s.inst >= 17 && s.inst <= 19) || s.inst == 23 || (s.inst >= 30 && s.inst <= 32);
                if (! fixedPitch) lv.human = 0.04f * v.rng.next();
                lv.noDamper = false;
                lv.gain = 1.0f / 0.3f;   // doznenie = gombik Doznenie
                lv.atk = 0; lv.atkInc = 1.0f / (std::max (0.0005f, s.aa * atkScale) * (float) sr);
                lv.amp.stage = Env::sustain; lv.amp.s = 1; lv.amp.v = 1;
            }
            else if (s.src == 0)
            {
                static const float centers[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
                const int sn = juce::jlimit (0, 127, juce::roundToInt ((float) v.note + noteShift));
                const float lpos = v.vel * 4.0f - 1.0f + (s.color - 0.5f) * 2.0f;
                const int layer = juce::jlimit (0, 3, (int) std::lround (lpos));
                const PianoBank* pb = s.inst == InstrBank::oldPiano ? bank2Raw.load() : bank.get();
                if (pb == nullptr) return;
                lv.smp = pb->find (sn, layer);
                if (lv.smp == nullptr) return;
                lv.velGain = clampf (std::pow (v.vel / centers[layer], 0.9f), 0.08f, 2.0f) * std::pow (0.15f + 0.85f * v.vel, 1.6f);
                lv.noDamper = sn >= 89;
                lv.gain = juce::jmap ((float) juce::jlimit (21, 88, sn), 21.0f, 88.0f, 0.22f, 0.07f) / 0.12f;   // casova mierka tlmica
                lv.atk = 0; lv.atkInc = 1.0f / (std::max (0.0005f, s.aa * atkScale) * (float) sr);
                lv.amp.stage = Env::sustain; lv.amp.s = 1; lv.amp.v = 1;
            }
            else
            {
                lv.user = getUserSample (li);
                if (lv.user == nullptr || lv.user->buf.getNumSamples() < 4) return;
                lv.ued = getSmpEdit (li);
            }
            lv.pos = 0;
        }
        lv.active = true;
    }

    void Synth::startVoice (Voice& v, int note, float vel, float fromNote)
    {
        v.note = note; v.vel = vel; v.held = true; v.sustained = false; v.age = ++ageCounter;
        v.mmRand = v.rng.next();
        if (! st.playing) { bool other = false; for (auto& o : voices) if (&o != &v && o.note >= 0) { other = true; break; } if (! other) gtFree = 0; }
        v.curNote = fromNote; v.targetNote = (float) note;
        v.drift = v.rng.next() * 4.0f; v.driftTarget = v.rng.next() * 4.0f;
        for (int li = 0; li < 2; ++li) startLayer (v, li);
        if (! v.isActive()) v.note = -1;
    }

    void Synth::noteOn (int note, float vel)
    {
        if (st.mono)
        {
            monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
            monoStack.push_back (note);
            for (auto& v : voices)
                if (v.note >= 0 && v.isActive() && (v.held || v.sustained))
                {
                    // legato: bez noveho nabehu, len zmena vysky (glide)
                    v.note = note; v.held = true; v.targetNote = (float) note;
                    if (st.glide <= 0.0005f) v.curNote = (float) note;
                    lastNote = note;
                    return;
                }
            for (auto& v : voices)
                if (v.note >= 0)
                {
                    v.held = v.sustained = false;
                    for (auto& lv : v.lv) { lv.amp.kill (0.01f); lv.damping = true; lv.dampCoef = std::exp (-1.0f / (0.004f * (float) sr)); }
                }
        }
        else
        {
            for (auto& v : voices)
                if (v.note == note && (v.held || v.sustained))
                {
                    v.held = v.sustained = false;
                    for (int li = 0; li < 2; ++li)
                    {
                        auto& lv = v.lv[li];
                        if (lv.wave == 7 && lv.smp != nullptr) { lv.damping = true; lv.dampCoef = std::exp (-1.0f / (0.06f * (float) sr)); }
                        else { lv.amp.off(); lv.fenv.off(); }
                    }
                }
        }
        auto* v = allocVoice();
        const float from = (st.glide > 0.0005f && lastNote >= 0) ? (float) lastNote : (float) note;
        startVoice (*v, note, vel, from);
        lastNote = note;
    }

    void Synth::noteOff (int note)
    {
        if (st.mono)
        {
            monoStack.erase (std::remove (monoStack.begin(), monoStack.end(), note), monoStack.end());
            for (auto& v : voices)
                if (v.note == note && v.held)
                {
                    if (! monoStack.empty())
                    {
                        const int back = monoStack.back();
                        v.note = back; v.targetNote = (float) back;
                        if (st.glide <= 0.0005f) v.curNote = (float) back;
                        lastNote = back;
                        return;
                    }
                    v.held = false;
                    if (sustain) v.sustained = true; else releaseVoice (v);
                }
            return;
        }
        for (auto& v : voices)
            if (v.note == note && v.held)
            {
                v.held = false;
                if (sustain) v.sustained = true; else releaseVoice (v);
            }
    }

    void Synth::setSustain (bool on)
    {
        sustain = on;
        if (! on)
            for (auto& v : voices)
                if (v.note >= 0 && v.sustained && ! v.held) { v.sustained = false; releaseVoice (v); }
    }

    // ---------------------------------------------------------------- arpeggiator
    void Synth::handleNoteOn (int note, float vel)
    {
        if (! st.arpOn) { noteOn (note, vel); return; }
        if (std::find (physDown.begin(), physDown.end(), note) == physDown.end()) physDown.push_back (note);
        if (st.arpHold && arpLatchedRelease) { arpHeld.clear(); arpLatchedRelease = false; }
        if (std::find (arpHeld.begin(), arpHeld.end(), note) == arpHeld.end()) arpHeld.push_back (note);
        arpLastVel = vel;
    }

    void Synth::handleNoteOff (int note)
    {
        physDown.erase (std::remove (physDown.begin(), physDown.end(), note), physDown.end());
        if (! st.arpOn) { noteOff (note); return; }
        if (st.arpHold) { if (physDown.empty()) arpLatchedRelease = true; return; }
        arpHeld.erase (std::remove (arpHeld.begin(), arpHeld.end(), note), arpHeld.end());
    }

    void Synth::arpStopNotes()
    {
        for (int n : arpPlaying) noteOff (n);
        arpPlaying.clear();
    }

    void Synth::arpStep()
    {
        arpStopNotes();
        if (arpHeld.empty()) return;
        const int oct = juce::jlimit (1, 4, st.arpOct);
        std::vector<int> sorted = arpHeld; std::sort (sorted.begin(), sorted.end());
        std::vector<int> seq;
        auto withOct = [oct] (const std::vector<int>& base) { std::vector<int> r; for (int o = 0; o < oct; ++o) for (int n : base) r.push_back (n + 12 * o); return r; };
        switch (st.arpPat)
        {
            case 0: seq = withOct (sorted); break;
            case 1: seq = withOct (sorted); std::reverse (seq.begin(), seq.end()); break;
            case 2: { auto up = withOct (sorted); seq = up; for (int i = (int) up.size() - 2; i >= 1; --i) seq.push_back (up[(size_t) i]); break; }
            case 3: seq = withOct (arpHeld); break;
            case 4: seq = withOct (sorted); break;
            default: break;
        }
        const float vel = arpLastVel;
        if (st.arpPat == 5)
        {
            const int o = arpIndex % oct;
            for (int n : sorted) { const int nn = juce::jlimit (0, 127, n + 12 * o); noteOn (nn, vel); arpPlaying.push_back (nn); }
        }
        else if (! seq.empty())
        {
            const int idx = st.arpPat == 4 ? (int) (std::abs (noise.next()) * (float) seq.size()) % (int) seq.size() : arpIndex % (int) seq.size();
            const int nn = juce::jlimit (0, 127, seq[(size_t) idx]);
            noteOn (nn, vel); arpPlaying.push_back (nn);
        }
        ++arpIndex;
    }

    void Synth::arpTick (int samples)
    {
        const double bpm = juce::jmax (20.0, st.fx.bpm);
        const double adv = (double) samples / sr * bpm / 60.0;
        if (! st.arpOn) { beat += adv; return; }
        if (arpHeld.empty())
        {
            if (arpRunning) { arpStopNotes(); arpRunning = false; }
            beat += adv; return;
        }
        static const double bps[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
        const double step = bps[juce::jlimit (0, 4, st.arpSpeed)];
        if (! arpRunning) { arpRunning = true; arpIndex = 0; if (! st.playing) beat = 0; lastStepIndex = INT64_MIN; }
        const int64_t idx = (int64_t) std::floor (beat / step + 1e-9);
        if (idx != lastStepIndex) { lastStepIndex = idx; stepStartBeat = (double) idx * step; arpStep(); }
        else if (! arpPlaying.empty() && beat >= stepStartBeat + st.arpLen * step) arpStopNotes();
        beat += adv;
    }

    // ---------------------------------------------------------------- render
    void Synth::renderLayer (Voice& v, int li, float* L, float* R, int n)
    {
        const auto& s = st.L[li];
        auto& lv = v.lv[li];
        // analogove plavanie: najviac +-4 centy (nie na klavirnych samploch)
        const bool sampleSrc = lv.wave == 7 && lv.smp != nullptr;
        const float shift = s.oct * 12.0f + s.semi + s.fine * 0.01f + bend + (sampleSrc ? 0.0f : v.drift * 0.01f * st.fx.tube);
        const float baseHz = noteHz (v.curNote + shift);
        const float mx = st.morphX;
        const float morph = li == 0 ? (mx <= 0 ? 1.0f : 1.0f - mx) : (mx >= 0 ? 1.0f : 1.0f + mx);
        const float lg = morph * s.level * lv.velGain;
        const float q = 0.5f + 9.0f * s.reso * s.reso;
        const float kt = s.ktrk * ((float) v.note - 60.0f) / 12.0f + 1.6f * s.fvel * (v.vel - 0.7f) + 2.0f * bright;
        const bool hasPEnv = std::abs (s.penv) > 0.01f;
        const float invSr = 1.0f / (float) sr;
        const int nu = juce::jlimit (1, 16, s.uni);
        const float uniNorm = 1.0f / std::sqrt ((float) nu);
        const float tilt = 0.8f * bright;
        bool anyMod = false, cutMod = false;
        for (const auto& m : st.mm)
            if (m.src > 0 && std::abs (m.amt) >= 0.001f)
            {
                anyMod = true;
                if ((m.dst < 3 && (m.dst == 2 || m.dst == li)) || m.dst == 13) cutMod = true;
            }
        const bool useFilter = s.cut < 19990.0f || s.fenv > 0.001f || s.ftype != 0 || bright < -0.01f || s.ltgt == 1 || cutMod;
        const bool isPiano = lv.wave == 7 && lv.smp != nullptr;
        static const float bg[5] = { 0.9f, 0.7f, 0.55f, 0.7f, 0.45f };

        // Roj: parametre na blok (frekvencne nasobky, ciele hlasitosti alikvot, panorama)
        const float* sinT = sineLutData();
        float rojKf[32], rojStep[32], rojPL[32], rojPR[32], rojTgt[32];
        int rojAct = 0;
        float rojFocusA = 0;
        if (lv.wave == 8)
        {
            const int Nt = juce::jlimit (4, 32, 4 * nu);
            const int nMax = std::max (Nt, lv.rojN);
            const float B = 0.0012f * s.spread;
            // tmavsi tvar pri nizkom jase / tichsej hre (0.8 - 1.6)
            const float tiltR = clampf (1.25f - 0.3f * bright - 0.5f * (v.vel - 0.7f), 0.8f, 1.6f);
            const float sr32 = (float) sr;
            float sumSq = 0;
            for (int k = 0; k < nMax; ++k)
            {
                const float kk = (float) (k + 1);
                const float sk = k == 0 ? 0.0f : 0.5f * (kk - 1.0f) / 8.0f + 0.5f * rojScatter (k + 1);
                rojKf[k] = kk * (1.0f + B * sk);
                const float fk = rojKf[k] * baseHz * 1.06f;                  // rezerva na vibrato
                float a = k < Nt ? std::exp (-tiltR * std::log (kk)) : 0.0f;
                if (fk > 0.45f * sr32) a = 0;
                else if (fk > 0.35f * sr32) a *= (0.45f * sr32 - fk) / (0.1f * sr32);
                rojTgt[k] = a; sumSq += a * a;
                const float p = (k % 2 == 0 ? -0.35f : 0.35f) * std::min (1.0f, kk * 0.25f);   // neparne mierne vlavo, parne vpravo
                rojPL[k] = std::sqrt (0.5f * (1.0f - p)); rojPR[k] = std::sqrt (0.5f * (1.0f + p));
            }
            const float norm = sumSq > 1.0e-12f ? 0.6f / std::sqrt (sumSq) : 0.0f;
            const float inv = 1.0f / (float) n;
            for (int k = 0; k < nMax; ++k)
            {
                rojTgt[k] *= norm;
                rojStep[k] = (rojTgt[k] - lv.rojG[k]) * inv;
                if (rojTgt[k] > 0 || lv.rojG[k] > 0) rojAct = k + 1;
            }
            lv.rojN = Nt;
            rojFocusA = 1.0f - std::exp (-1.0f / (0.0113f * sr32));
        }
        // Wavetable: tabulka a uroven harmonickych na blok (podla najvyssej frekvencie v bloku, s hysterezou);
        // pri zmene urovne alebo tabulky sa stary a novy zdroj v bloku prelinaju
        const WtBank* wtb = nullptr;
        const float *wtNew = nullptr, *wtOld = nullptr;
        int wtMip = 0, wtTab = 0, wtLenNew = 0, wtLenOld = 0, wtStrNew = 0, wtStrOld = 0;
        bool wtX = false;
        const float wtPosA = 1.0f - std::exp (-1.0f / (0.004f * (float) sr)), wtInvN = 1.0f / (float) n;
        if (lv.wave == 9 && (wtb = WtBank::peek()) != nullptr)
        {
            wtTab = juce::jlimit (0, WtBank::numTables - 1, s.wt);
            const float fmax = baseHz * 1.08f * (nu > 1 ? std::pow (2.0f, s.spread * 1.15f / 1200.0f) : 1.0f);   // rezerva: vibrato + rozladenie
            const float hLim = (float) sr / std::max (fmax, 1.0f);
            int up = 0; while (up < WtBank::numMips - 1 && (float) WtBank::harmonics (up) > 0.49f * hLim) ++up;
            int dn = up; while (dn < WtBank::numMips - 1 && (float) WtBank::harmonics (dn) > 0.44f * hLim) ++dn;
            wtMip = lv.wtMip < 0 ? up : juce::jlimit (up, dn, lv.wtMip);
            if (lv.wtMip < 0) { lv.wtMip = wtMip; lv.wtTab = wtTab; }
            wtX = wtMip != lv.wtMip || wtTab != lv.wtTab;
            wtNew = wtb->frame (wtTab, wtMip, 0); wtLenNew = wtb->len[wtMip]; wtStrNew = wtLenNew + 4;
            wtOld = wtb->frame (lv.wtTab, lv.wtMip, 0); wtLenOld = wtb->len[lv.wtMip]; wtStrOld = wtLenOld + 4;
        }
        // LFO podla tempa piesne (doby na jeden cyklus)
        static const float syncBeats[9] = { 0, 4, 2, 1, 0.5f, 0.25f, 2.0f / 3.0f, 1.0f / 3.0f, 1.0f / 6.0f };
        const float lfoRate = s.lsync > 0 && s.lsync < 9 ? (float) (juce::jmax (20.0, st.fx.bpm) / 60.0) / syncBeats[s.lsync] : s.lrate;
        for (int i = 0; i < n; ++i)
        {
            // LFO
            lv.lfoPh += lfoRate * invSr;
            if (lv.lfoPh >= 1) { lv.lfoPh -= 1; lv.lfoHold = v.rng.next(); }
            float lfo;
            switch (s.lshape)
            {
                case 1: lfo = 1.0f - 4.0f * std::abs (lv.lfoPh - 0.5f); break;
                case 2: lfo = lv.lfoPh < 0.5f ? 1.0f : -1.0f; break;
                case 3: lfo = lv.lfoHold; break;
                default: lfo = std::sin (twoPi * lv.lfoPh); break;
            }
            const float fade = s.ldelay > 0 ? clampf ((lv.t - s.ldelay) / 0.4f, 0.0f, 1.0f) : 1.0f;
            const float lfoV = lfo * s.ldepth * fade;
            lv.lfoOut = lfo * fade;
            const float env = lv.amp.next();
            const float fe = lv.fenv.next();
            // modulacna matica
            float mCut = 0, mVol = 0, mPitch = 0, mCol = 0, mPan = 0, mRes = 0;
            if (anyMod)
                for (const auto& m : st.mm)
                {
                    if (m.src <= 0 || std::abs (m.amt) < 0.001f) continue;
                    float sv = 0;
                    switch (m.src)
                    {
                        case 1: sv = li == 0 ? lv.lfoOut : v.lv[0].lfoOut; break;
                        case 2: sv = li == 1 ? lv.lfoOut : v.lv[1].lfoOut; break;
                        case 3: sv = fe; break;
                        case 4: sv = env; break;
                        case 5: sv = v.vel; break;
                        case 6: sv = modWheel; break;
                        case 7: sv = curRhythm; break;
                        default: sv = v.mmRand; break;
                    }
                    const float x = sv * m.amt;
                    if (m.dst < 12)
                    {
                        const int w = m.dst % 3;
                        if (w != 2 && w != li) continue;
                        switch (m.dst / 3)
                        {
                            case 0: mCut += x * 4.0f; break;     // +-4 oktavy
                            case 1: mVol += x; break;
                            case 2: mPitch += x * 12.0f; break;  // +-12 poltonov
                            default: mCol += x * 0.5f; break;
                        }
                    }
                    else if (m.dst == 12) mPan += x;
                    else mRes += x;
                }
            float cents = (s.ltgt == 0 ? lfoV * 100.0f : 0.0f) + mPitch * 100.0f;
            if (hasPEnv) { cents += s.penv * 100.0f * std::exp (-lv.t / std::max (0.002f, s.ptime)); mPitch += 0.0001f; }   // obalka vysky
            if (modWheel > 0.001f) cents += modWheel * 30.0f * std::sin (twoPi * 5.5f * lv.t);
            const float pitchMul = mPitch != 0.0f ? std::pow (2.0f, cents / 1200.0f) : 1.0f + cents * 0.000577789f;
            const float volMul = (s.ltgt == 2 ? std::max (0.0f, 1.0f + 0.5f * lfoV) : 1.0f) * std::max (0.0f, 1.0f + mVol);
            const float panMod = (s.ltgt == 3 ? lfoV : 0.0f) + mPan;
            const float color = clampf ((s.ltgt == 4 ? s.color + 0.5f * lfoV : s.color) + mCol, 0.0f, 1.0f);   // LFO ciel "Farba"

            if (useFilter && (lv.cnt++ & 15) == 0)
            {
                const float oct = kt + s.fenv * 5.0f * fe + (s.ltgt == 1 ? lfoV * 3.0f : 0.0f) + mCut;
                const float hz = clampf (s.cut * std::pow (2.0f, oct), 20.0f, 20000.0f);
                const float rq = clampf (s.reso + mRes, 0.0f, 1.0f), qq = mRes != 0.0f ? 0.5f + 9.0f * rq * rq : q;
                lv.f1L.set (hz, qq, (float) sr); lv.f1R.set (hz, qq, (float) sr);
                if (s.f24) { lv.f2L.set (hz, qq, (float) sr); lv.f2R.set (hz, qq, (float) sr); }
            }

            float l = 0, r = 0;
            if (lv.wave <= 4)
            {
                const float inc0 = baseHz * pitchMul * invSr;
                if (lv.wave == 4)
                {
                    const float w = lv.noiseL.lp (v.rng.next()) * (1.0f + 2.0f * (1.0f - s.color));
                    l = r = w;
                }
                else
                {
                    const float pw = 0.05f + 0.9f * color;
                    // pila, obdlznik a trojuholnik sa pocitaju na dvojnasobnej frekvencii a filtrom sa vratia spat:
                    // ~20 dB menej zrnitosti (aliasu) pri vysokych tonoch; sinus to nepotrebuje
                    const int nsub = lv.wave == 0 ? 1 : 2;
                    const float incMul = 1.0f / (float) nsub;
                    for (int sub = 0; sub < nsub; ++sub)
                    {
                        float sl = 0, sr2 = 0;
                        for (int k = 0; k < nu; ++k)
                        {
                            const float inc = inc0 * lv.det[k] * incMul;
                            float& ph = lv.ph[k];
                            ph += inc; if (ph >= 1) ph -= 1;
                            float x;
                            switch (lv.wave)
                            {
                                case 0: x = std::sin (twoPi * ph); break;
                                case 1: x = 1.0f - 4.0f * std::abs (ph - 0.5f); break;
                                case 2: x = 2.0f * ph - 1.0f - polyBlep (ph, inc); break;
                                default:
                                {
                                    float p2 = ph + (1.0f - pw); if (p2 >= 1) p2 -= 1;
                                    x = (2.0f * ph - 1.0f - polyBlep (ph, inc)) - (2.0f * p2 - 1.0f - polyBlep (p2, inc)) + (1.0f - 2.0f * pw);
                                    break;
                                }
                            }
                            sl += x * lv.pl[k]; sr2 += x * lv.pr[k];
                        }
                        if (nsub == 1) { l += sl; r += sr2; }
                        else { lv.osL[lv.osW] = sl; lv.osR[lv.osW] = sr2; lv.osW = (lv.osW + 1) & 31; }
                    }
                    if (nsub == 2)
                    {
                        static const float h[23] = { -0.01281327f, 0.00001893f, 0.01778585f, 0.00001019f, -0.03066188f, 0.00002960f, 0.05272951f, 0.00000557f, -0.10003794f, 0.00002111f, 0.31978649f, 0.50625172f,
                                                     0.31978649f, 0.00002111f, -0.10003794f, 0.00000557f, 0.05272951f, 0.00002960f, -0.03066188f, 0.00001019f, 0.01778585f, 0.00001893f, -0.01281327f };
                        float fl = 0, fr = 0;
                        for (int t = 0; t < 23; ++t) { const int ix = (lv.osW - 1 - t) & 31; fl += h[t] * lv.osL[ix]; fr += h[t] * lv.osR[ix]; }
                        l += fl; r += fr;
                    }
                    l *= uniNorm; r *= uniNorm;
                }
            }
            else if (lv.wave == 5)
            {
                const float y = lv.dl[(size_t) lv.w];
                const float lp = (1.0f - lv.loopS) * y + lv.loopS * lv.yPrev; lv.yPrev = y;
                const float o = lv.apC * lp + lv.apIn - lv.apC * lv.apOut; lv.apIn = lp; lv.apOut = o;
                lv.dl[(size_t) lv.w] = o * lv.loopG;
                if (++lv.w >= lv.dlen) lv.w = 0;
                const float g = 0.15f + 0.85f * v.vel;
                l = y * g * lv.plPanL * 1.41f; r = y * g * lv.plPanR * 1.41f;
            }
            else if (lv.wave == 6)
            {
                const float f = baseHz * pitchMul * invSr;
                const float idx = color * 4.0f * (0.4f + 0.9f * v.vel) * std::pow (2.0f, 1.2f * bright) * ((1.0f - std::abs (s.fenv)) + std::abs (s.fenv) * fe);
                lv.mPh += f * s.ratio; lv.mPh -= std::floor (lv.mPh);
                lv.cPh += f; if (lv.cPh >= 1) lv.cPh -= 1;
                l = r = std::sin (twoPi * lv.cPh + idx * std::sin (twoPi * lv.mPh)) * 0.7071f;
            }
            else if (lv.wave == 8)
            {
                const float fc = baseHz * pitchMul * invSr;                 // cykly na vzorku (aj s vibratom, benderom, glide)
                lv.rojPhi += fc; lv.rojPhi -= std::floor (lv.rojPhi);
                // Focus = Farba, filtrova obalka ho meni (nota "kristalizuje"); vyhladene jednopolovym filtrom
                const float envTerm = (1.0f - std::abs (s.fenv)) + std::abs (s.fenv) * fe;
                const float ft = clampf (color * envTerm, 0.0f, 1.0f);
                if (lv.rojFocus < 0) lv.rojFocus = ft; else lv.rojFocus += rojFocusA * (ft - lv.rojFocus);
                const float Kc = std::min (0.5f * lv.rojFocus * lv.rojFocus * fc, 0.1f);   // K/sr = min(kappa f, 0.1 sr)/sr
                const float phi = lv.rojPhi;
                lv.rojTog ^= 1; const bool upd = lv.rojTog != 0;
                float sl = 0, sr_ = 0;
                for (int k = 0; k < rojAct; ++k)
                {
                    float th = lv.rojTh[k];
                    if (upd)   // vazba (Adler) sa pocita kazdy 2. vzorok kvoli procesoru
                    {
                        float d = (float) (k + 1) * phi + lv.rojPsi[k] - th + 1.0f; d -= (float) (int) d;   // d > 0, frac bez volania floor
                        lv.rojC[k] = Kc * sinCyc (sinT, d);
                    }
                    th += fc * rojKf[k] + lv.rojC[k];
                    th += th < 0.0f ? 1.0f : (th >= 1.0f ? -1.0f : 0.0f);                               // prirastok < 1 (alikvoty pod 0.45 sr)
                    lv.rojTh[k] = th;
                    lv.rojG[k] += rojStep[k];
                    const float y = lv.rojG[k] * sinCyc (sinT, th);
                    sl += y * rojPL[k]; sr_ += y * rojPR[k];
                }
                l = sl; r = sr_;
            }
            else if (lv.wave == 9)
            {
                if (wtb != nullptr)
                {
                    // Pozicia = Farba + obalka filtra (Obalka) + LFO; vyhladena, medzi ramcami linearne
                    const float pt = clampf (color + s.fenv * fe, 0.0f, 1.0f);
                    if (lv.wtPos < 0) lv.wtPos = pt; else lv.wtPos += wtPosA * (pt - lv.wtPos);
                    const float fp = lv.wtPos * (float) (WtBank::numFrames - 1);
                    const int fi = std::min ((int) fp, WtBank::numFrames - 2);
                    const float ff = fp - (float) fi;
                    const float* a = wtNew + fi * wtStrNew; const float* b = a + wtStrNew;
                    const float* oa = wtOld + fi * wtStrOld; const float* ob = oa + wtStrOld;
                    const float xw = (float) i * wtInvN;
                    const float inc0 = baseHz * pitchMul * invSr;
                    for (int k = 0; k < nu; ++k)
                    {
                        float& ph = lv.ph[k];
                        ph += inc0 * lv.det[k]; if (ph >= 1) ph -= 1;
                        float x = WtBank::read (a, b, wtLenNew, ph, ff);
                        if (wtX) { const float xo = WtBank::read (oa, ob, wtLenOld, ph, ff); x = xo + xw * (x - xo); }
                        l += x * lv.pl[k]; r += x * lv.pr[k];
                    }
                    l *= uniNorm; r *= uniNorm;
                }
            }
            else if (isPiano)
            {
                const auto* sm = lv.smp;
                // slucka drzaneho tonu: za koncom slucky je v subore pokracovanie, v pasme F sa prelina
                // s tonom od zaciatku slucky (zakryje drobne rozdiely kompresie OGG) a potom skoci spat
                const int le = sm->loopEnd, ll = sm->loopEnd - sm->loopStart;
                const int F = le > 0 ? std::min ({ 1024, sm->length - le - 4, ll / 2 }) : 0;
                if (le > 0 && F > 0 && lv.pos >= (double) (le + F)) lv.pos -= (double) ll;
                else if (le > 0 && F <= 0 && lv.pos >= (double) le) lv.pos -= (double) ll;
                const int ip = (int) lv.pos;
                if (ip + 3 >= sm->length) { lv.active = false; break; }
                const float fr = (float) (lv.pos - ip);
                auto herm = [fr] (float xm1, float x0, float x1, float x2)
                {
                    const float c = (x1 - xm1) * 0.5f, b = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, a3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    return ((a3 * fr + b) * fr + c) * fr + x0;
                };
                const int16_t* d = sm->data.data();
                const int ch = sm->ch;
                auto rd = [&] (int i0, int c) { const int im = i0 > 0 ? i0 - 1 : 0; return herm (d[im * ch + c], d[i0 * ch + c], d[(i0 + 1) * ch + c], d[(i0 + 2) * ch + c]) * (1.0f / 32768.0f); };
                l = rd (ip, 0);
                r = ch > 1 ? rd (ip, 1) : l;
                if (F > 0 && ip >= le)
                {
                    const float w = juce::jlimit (0.0f, 1.0f, (float) (lv.pos - le) / (float) F);
                    const int iw = ip - ll;
                    const float l2 = rd (iw, 0), r2 = ch > 1 ? rd (iw, 1) : l2;
                    l += w * (l2 - l); r += w * (r2 - r);
                }
                const double step = std::pow (2.0, (v.curNote + shift + lv.human - (float) sm->root - sm->tune) / 12.0) * sm->rate / sr * pitchMul;
                lv.pos += step;
                if (lv.atk < 1.0f) lv.atk = std::min (1.0f, lv.atk + lv.atkInc);
                if (lv.damping) lv.level *= lv.dampCoef;
                l *= lv.level * lv.atk; r *= lv.level * lv.atk;
                if (lv.level < 1.0e-4f) lv.active = false;
            }
            else if (lv.user != nullptr)
            {
                // pozicia = posun od zaciatku vybranej casti; odzadu sa cita od konca
                const auto& b = lv.user->buf;
                const auto& e = lv.ued;
                const int len = b.getNumSamples();
                const int s0 = juce::jlimit (0, len - 4, (int) (e.start * (float) len));
                const int s1 = juce::jlimit (s0 + 3, len - 1, (int) (e.end * (float) len));
                const double L = (double) (s1 - s0);
                const float* bl = b.getReadPointer (0);
                const float* br = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
                auto rd = [&] (double off, float& ol, float& orr)
                {
                    double a = e.rev ? (double) s1 - 1.0 - off : (double) s0 + off;
                    a = juce::jlimit (0.0, (double) len - 2.0, a);
                    const int ip = (int) a; const float fr = (float) (a - ip);
                    // kubicka (Hermite) interpolacia – bez zrnitosti pri zmene vysky
                    const int i0 = ip > 0 ? ip - 1 : 0, i3 = juce::jmin (len - 1, ip + 2);
                    auto hm = [fr] (float xm, float x0, float x1, float x2)
                    { const float c = 0.5f * (x1 - xm), v = x0 - x1, w = c + v, a3 = w + v + 0.5f * (x2 - x0), b = w + a3; return ((a3 * fr - b) * fr + c) * fr + x0; };
                    ol = hm (bl[i0], bl[ip], bl[ip + 1], bl[i3]); orr = hm (br[i0], br[ip], br[ip + 1], br[i3]);
                };
                double lsO = 0, leO = L;
                if (s.loop)
                {
                    lsO = juce::jlimit (0.0, L - 2.0, (double) (e.ls * (float) len) - s0);
                    leO = juce::jlimit (lsO + 2.0, L, (double) (e.le * (float) len) - s0);
                    if (lv.pos >= leO) lv.pos -= (leO - lsO);
                }
                else if (lv.pos >= L - 1.0) { lv.active = false; break; }
                rd (lv.pos, l, r);
                if (s.loop)
                {   // prelinanie pred koncom slucky s tonom od jej zaciatku (bez pocutelneho svu)
                    const double X = std::min ((double) e.xf * lv.user->rate, (leO - lsO) * 0.5);
                    if (X > 1.0 && lv.pos > leO - X)
                    {
                        const float w = (float) ((lv.pos - (leO - X)) / X);
                        float l2, r2; rd (lv.pos - (leO - lsO), l2, r2);
                        l += w * (l2 - l); r += w * (r2 - r);
                    }
                }
                float g = 1.0f;
                if (e.fin > 0.0005f) g *= (float) std::min (1.0, lv.pos / ((double) e.fin * lv.user->rate));
                if (! s.loop && e.fout > 0.0005f) g *= (float) std::min (1.0, (L - lv.pos) / ((double) e.fout * lv.user->rate));
                l *= g; r *= g;
                lv.pos += std::pow (2.0, (v.curNote + shift - (float) s.root) / 12.0) * lv.user->rate / sr * pitchMul;
            }
            if (lv.wave == 7 && tilt != 0.0f) { l += tilt * (l - lv.tiltL.lp (l)); r += tilt * (r - lv.tiltR.lp (r)); }
            if (s.noise > 0.001f && lv.wave != 4) { const float nz = v.rng.next() * 0.45f * s.noise * s.noise; l += nz; r += nz; }   // primiesany sum (dych, trsatko)

            // filter vrstvy
            if (useFilter)
            {
                float bl, br;
                float yl = lv.f1L.processSat (l, bl), yr = lv.f1R.processSat (r, br);
                if (s.ftype == 1) { yl = bl; yr = br; }
                else if (s.ftype == 2) { yl = l - yl - bl * (1.0f / q); yr = r - yr - br * (1.0f / q); }
                if (s.f24)
                {
                    float b2l, b2r;
                    const float zl = lv.f2L.processSat (yl, b2l), zr = lv.f2R.processSat (yr, b2r);
                    if (s.ftype == 1) { yl = b2l; yr = b2r; }
                    else if (s.ftype == 2) { yl = yl - zl - b2l * (1.0f / q); yr = yr - zr - b2r * (1.0f / q); }
                    else { yl = zl; yr = zr; }
                }
                if (s.ftype == 3)
                {
                    const float m = 0.5f * (yl + yr);
                    float b = 0.35f * m;
                    for (int j = 0; j < 5; ++j) b += bg[j] * lv.body[j].bpf (m);
                    yl = b * 0.85f + 0.15f * yl; yr = b * 0.85f + 0.15f * yr;
                }
                l = yl; r = yr;
            }
            const float g = (isPiano ? 1.0f : env) * lg * volMul;
            const float pan = clampf (s.pan + panMod, -1.0f, 1.0f);
            const float gl = pan > 0 ? 1.0f - pan : 1.0f, gr = pan < 0 ? 1.0f + pan : 1.0f;
            L[i] += l * g * gl; R[i] += r * g * gr;
            lv.t += invSr;
            if (lv.wave == 5)
            {
                lv.peak = lv.peak * 0.9995f + std::abs (l) * 0.0005f;
                if (lv.t > 0.1f && lv.peak < 2.0e-5f) { lv.active = false; break; }
            }
            if (! isPiano && ! lv.amp.active()) { lv.active = false; break; }
        }
        if (lv.wave == 8) for (int k = 0; k < std::max (rojAct, 0); ++k) lv.rojG[k] = rojTgt[k];
        if (wtb != nullptr) { lv.wtMip = wtMip; lv.wtTab = wtTab; }
    }

    void Synth::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi, const Settings& s)
    {
        st = s;
        bright = clampf (s.morphY + (s.bright - 0.5f) * 2.0f, -1.5f, 1.5f);
        atkScale = std::pow (2.0f, (s.attack - 0.5f) * 5.0f);
        relScale = std::pow (2.0f, (s.release - 0.5f) * 5.0f);
        FxSettings f = s.fx;
        f.rvMix = clampf (f.rvMix * 2.0f * s.space, 0.0f, 0.95f);
        f.rvSize = clampf (f.rvSize + (s.space - 0.5f) * 0.4f, 0.0f, 1.0f);
        f.chMix = clampf (f.chMix * 2.0f * s.width, 0.0f, 1.0f);
        f.width = f.width * (0.4f + 1.2f * s.width);
        f.full = s.full;

        if (fadeRequest)
        {
            fadeRequest = false;
            tailState = 1; fadeAge = ageCounter;   // stlmit aj dozvuk/ozvenu predosleho zvuku, potom ich vymazat (nove tony ostanu)
            arpPlaying.clear(); arpHeld.clear(); monoStack.clear();
            for (auto& v : voices)
                if (v.note >= 0)
                {
                    v.held = v.sustained = false;
                    for (auto& lv : v.lv) { lv.amp.kill (0.02f); lv.damping = true; lv.dampCoef = std::exp (-1.0f / (0.008f * (float) sr)); }
                }
        }
        if (! s.arpOn && arpWasOn) { arpStopNotes(); arpHeld.clear(); arpRunning = false; }
        arpWasOn = s.arpOn;
        if (s.playing) beat = s.ppq;

        const int total = out.getNumSamples();
        for (auto* b : { &bufL, &bufR, &bufBL, &bufBR, &gateBuf, &voxBuf })
        {
            if ((int) b->size() < total) b->resize ((size_t) total);
            std::fill (b->begin(), b->begin() + total, 0.0f);
        }
        int pos = 0, act = 0;
        auto renderTo = [&] (int end)
        {
            while (pos < end)
            {
                const int n = std::min (end - pos, 32);
                // rytmicky gater: vzor 16 krokov synchronne s tempom (pri hrani z pozicie hostitela)
                {
                    static const double stepB[5] = { 1.0, 0.5, 1.0 / 3.0, 0.25, 0.125 };
                    const double adv = juce::jmax (20.0, st.fx.bpm) / 60.0 / sr;
                    const double sl = stepB[juce::jlimit (0, 4, st.gtRate)];
                    const double b0 = st.playing ? beat : gtFree;
                    const float a = 1.0f - std::exp (-1.0f / ((0.0005f + 0.025f * st.gtSmooth) * (float) sr));
                    for (int i = 0; i < n; ++i)
                    {
                        gtR += a * (korgus::gateTarget (b0 + (double) i * adv, sl, st.gtPat, st.gtSwing) - gtR);
                        gateBuf[(size_t) (pos + i)] = st.gtOn ? 1.0f - st.gtDepth * (1.0f - gtR) : 1.0f;
                    }
                    curRhythm = gtR;
                    gtFree += (double) n * adv;
                }
                arpTick (n);
                act = 0;
                for (auto& v : voices)
                {
                    if (v.note < 0) continue;
                    // glide a analogovy drift
                    if (v.curNote != v.targetNote)
                    {
                        const float c = st.glide > 0.0005f ? 1.0f - std::exp (-(float) n / (st.glide * 0.3f * (float) sr)) : 1.0f;
                        v.curNote += (v.targetNote - v.curNote) * c;
                        if (std::abs (v.curNote - v.targetNote) < 0.0005f) v.curNote = v.targetNote;
                    }
                    if (st.fx.tube > 0.001f)
                    {
                        v.drift += (v.driftTarget - v.drift) * 0.002f * (float) n / 32.0f;
                        if (std::abs (v.drift - v.driftTarget) < 0.05f) v.driftTarget = v.rng.next() * 4.0f;
                    }
                    if (v.lv[0].active) renderLayer (v, 0, bufL.data() + pos, bufR.data() + pos, n);
                    if (v.lv[1].active) renderLayer (v, 1, bufBL.data() + pos, bufBR.data() + pos, n);
                    if (! v.isActive()) v.note = -1; else ++act;
                }
                pos += n;
            }
        };
        for (const auto meta : midi)
        {
            renderTo (juce::jlimit (0, total, meta.samplePosition));
            const auto m = meta.getMessage();
            if (m.isNoteOn()) handleNoteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) handleNoteOff (m.getNoteNumber());
            else if (m.isSustainPedalOn()) setSustain (true);
            else if (m.isSustainPedalOff()) setSustain (false);
            else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (m.isController() && m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff())
            {
                setSustain (false); arpHeld.clear(); arpStopNotes(); physDown.clear(); monoStack.clear();
                for (auto& v : voices) if (v.note >= 0) { v.held = v.sustained = false; releaseVoice (v); }
            }
        }
        renderTo (total);
        active.store (act);

        // EQ vrstiev A a B, potom spolu
        const bool eqA = layerEq[0].update (s.L[0].eq8, (float) sr) && s.L[0].eqEnabled;
        const bool eqB = layerEq[1].update (s.L[1].eq8, (float) sr) && s.L[1].eqEnabled;
        const bool tapA = analyzer.wants (0), tapB = analyzer.wants (1);
        for (int i = 0; i < total; ++i)
        {
            float al = bufL[(size_t) i], ar = bufR[(size_t) i], bl = bufBL[(size_t) i], br = bufBR[(size_t) i];
            if (eqA) layerEq[0].process (al, ar);
            if (eqB) layerEq[1].process (bl, br);
            if (tapA) analyzer.push (0, 0.5f * (al + ar));
            if (tapB) analyzer.push (1, 0.5f * (bl + br));
            bufL[(size_t) i] = al + bl; bufR[(size_t) i] = ar + br;
        }

        // vocoder: zvuk Inspirie je nosic, upraveny spev zo sidechainu je modulator (pred efektmi)
        if (s.vcOn)
        {
            if (! vocWasOn) { vocoder.reset(); voxPrep.reset(); deEss.reset(); vcEq.reset(); vocWasOn = true; }
            const bool haveVox = vocInL != nullptr && vocInN >= total;
            if (haveVox)
            {
                for (int i = 0; i < total; ++i) voxBuf[(size_t) i] = 0.5f * (vocInL[i] + (vocInR != nullptr ? vocInR[i] : vocInL[i]));
                voxPrep.process (voxBuf.data(), voxBuf.data(), total);
                vocLevel.store (voxPrep.level());
            }
            else vocLevel.store (0.0f);
            const float r01 = std::log (juce::jlimit (6.0f, 360.0f, s.vcRelease) / 6.0f) / std::log (60.0f);
            vocoder.setParams (korgus::KorgusVocoder::carrierTrack, korgus::KorgusVocoder::bandsTo01 ((int) std::round (s.vcBands)),
                               korgus::KorgusVocoder::semitonesToFormant (s.vcFormant), s.vcClarity, r01, juce::jlimit (0.0f, 1.0f, s.vcMix));
            if (haveVox)
            {
                vocoder.process (bufL.data(), bufR.data(), voxBuf.data(), voxBuf.data(), bufL.data(), bufR.data(), total);
                const float dry = s.vcDry * 0.5f;
                for (int i = 0; i < total; ++i) { bufL[(size_t) i] += dry * voxBuf[(size_t) i]; bufR[(size_t) i] += dry * voxBuf[(size_t) i]; }
                deEss.process (bufL.data(), bufR.data(), total, 0.75f);   // tlmenie ostrych sykaviek (S, C, Z)
                if (vcEq.update (s.vcEqLow, s.vcEqMid, s.vcEqMidF, s.vcEqHigh, (float) sr) && s.vcEqOn)   // EQ vocodera (zalozka EQ)
                    for (int i = 0; i < total; ++i) vcEq.process (bufL[(size_t) i], bufR[(size_t) i]);
            }
        }
        else { vocWasOn = false; vocLevel.store (0.0f); }
        fx.process (bufL.data(), bufR.data(), total, f);
        {   // odstranenie jednosmernej zlozky (pulzne vlny, saturacia) - viac rezervy a ziadne lupnutia
            const float R = 1.0f - 6.2831853f * 8.0f / (float) sr;
            for (int i = 0; i < total; ++i)
            {
                const float l = bufL[(size_t) i], r = bufR[(size_t) i];
                dcY[0] = l - dcX[0] + R * dcY[0]; dcX[0] = l; bufL[(size_t) i] = dcY[0];
                dcY[1] = r - dcX[1] + R * dcY[1]; dcX[1] = r; bufR[(size_t) i] = dcY[1];
            }
        }
        if (s.gtOn)   // gater az za efektmi: seka aj dozvuk a delay (typicky "trance gate")
            for (int i = 0; i < total; ++i) { bufL[(size_t) i] *= gateBuf[(size_t) i]; bufR[(size_t) i] *= gateBuf[(size_t) i]; }
        // prepnutie zvuku: 25 ms stlmit vsetko (aj dozvuk), vymazat chvosty efektov, 5 ms nabeh
        if (tailState != 0)
        {
            const float dn = 1.0f / (0.025f * (float) sr), up = 1.0f / (0.005f * (float) sr);
            for (int i = 0; i < total; ++i)
            {
                if (tailState == 1) { tailG -= dn; if (tailG <= 0.0f) { tailG = 0.0f; tailState = 2; fx.clearTails(); vocoder.reset(); deEss.reset(); vcEq.reset();
                                                                        for (auto& v : voices) if (v.note >= 0 && v.age <= fadeAge) { v.lv[0].active = v.lv[1].active = false; v.note = -1; } } }
                else if (tailState == 2) { tailG += up; if (tailG >= 1.0f) { tailG = 1.0f; tailState = 0; } }
                bufL[(size_t) i] *= tailG; bufR[(size_t) i] *= tailG;
            }
        }
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        if (oR != nullptr) { std::copy (bufL.begin(), bufL.begin() + total, oL); std::copy (bufR.begin(), bufR.begin() + total, oR); }
        else for (int i = 0; i < total; ++i) oL[i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
    }
}
