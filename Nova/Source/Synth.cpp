#include "Synth.h"

namespace nova
{
    Synth::Synth() { bank = PianoBank::get(); }

    void Synth::prepare (double rate, int maxBlock)
    {
        sr = rate;
        bufL.assign ((size_t) juce::jmax (1, maxBlock), 0); bufR.assign ((size_t) juce::jmax (1, maxBlock), 0);
        for (auto& v : voices)
        {
            v.note = -1;
            for (auto& lv : v.lv) { lv.active = false; lv.dl.assign (4096, 0); }
        }
        fx.prepare (sr);
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
        lv.wave = s.wave; lv.t = 0; lv.cnt = 0; lv.lfoPh = 0; lv.lfoHold = noise.next(); lv.peak = 0;
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

        if (s.wave <= 4)
        {
            const int n = juce::jlimit (1, 8, s.uni);
            for (int k = 0; k < n; ++k)
            {
                const float pos = n > 1 ? (float) k / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                const float cents = s.spread * pos * (0.85f + 0.3f * std::abs (noise.next()));
                lv.det[k] = std::pow (2.0f, cents / 1200.0f);
                lv.ph[k] = (n == 1 && s.wave == 0) ? 0.0f : 0.5f + 0.5f * noise.next();
                const float pan = pos * 0.8f;
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
            for (int i = 0; i < N; ++i) ex[(size_t) i] = lp.lp (noise.next());
            const int pick = juce::jmax (1, (int) (0.15f * (float) N));
            float mx = 1.0e-6f;
            for (int i = 0; i < N; ++i) { lv.dl[(size_t) i] = ex[(size_t) i] - (i >= pick ? ex[(size_t) (i - pick)] : 0.0f); mx = std::max (mx, std::abs (lv.dl[(size_t) i])); }
            for (int i = 0; i < N; ++i) lv.dl[(size_t) i] /= mx;
            const float t60 = clampf (s.ad * std::pow (220.0f / f0, 0.4f), 0.05f, 30.0f);
            lv.loopG = std::pow (10.0f, -3.0f / (t60 * f0));
            const float pan = clampf (((float) v.note - 64.0f) / 40.0f, -0.6f, 0.6f);
            lv.plPanL = std::cos ((pan + 1.0f) * pi * 0.25f); lv.plPanR = std::sin ((pan + 1.0f) * pi * 0.25f);
        }
        else if (s.wave == 6) { lv.cPh = lv.mPh = 0; }
        else
        {
            if (s.src == 0)
            {
                static const float centers[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
                const int sn = juce::jlimit (0, 127, juce::roundToInt ((float) v.note + noteShift));
                const float lpos = v.vel * 4.0f - 1.0f + (s.color - 0.5f) * 2.0f;
                const int layer = juce::jlimit (0, 3, (int) std::lround (lpos));
                lv.smp = bank->find (sn, layer);
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
            }
            lv.pos = 0;
        }
        lv.active = true;
    }

    void Synth::startVoice (Voice& v, int note, float vel, float fromNote)
    {
        v.note = note; v.vel = vel; v.held = true; v.sustained = false; v.age = ++ageCounter;
        v.curNote = fromNote; v.targetNote = (float) note;
        v.drift = noise.next() * 4.0f; v.driftTarget = noise.next() * 4.0f;
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
        const float shift = s.oct * 12.0f + s.semi + s.fine * 0.01f + bend + v.drift * st.fx.tube;
        const float baseHz = noteHz (v.curNote + shift);
        const float mx = st.morphX;
        const float morph = li == 0 ? (mx <= 0 ? 1.0f : 1.0f - mx) : (mx >= 0 ? 1.0f : 1.0f + mx);
        const float lg = morph * s.level * lv.velGain;
        const float q = 0.5f + 9.0f * s.reso * s.reso;
        const float kt = 0.5f * ((float) v.note - 60.0f) / 12.0f + 0.8f * (v.vel - 0.7f) + 2.0f * bright;
        const float invSr = 1.0f / (float) sr;
        const int nu = juce::jlimit (1, 8, s.uni);
        const float uniNorm = 1.0f / std::sqrt ((float) nu);
        const float tilt = 0.8f * bright;
        const bool useFilter = s.cut < 19990.0f || s.fenv > 0.001f || s.ftype != 0 || bright < -0.01f || s.ltgt == 1;
        const bool isPiano = lv.wave == 7 && lv.smp != nullptr;
        static const float bg[5] = { 0.9f, 0.7f, 0.55f, 0.7f, 0.45f };
        for (int i = 0; i < n; ++i)
        {
            // LFO
            lv.lfoPh += s.lrate * invSr;
            if (lv.lfoPh >= 1) { lv.lfoPh -= 1; lv.lfoHold = noise.next(); }
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
            float cents = s.ltgt == 0 ? lfoV * 100.0f : 0.0f;
            if (modWheel > 0.001f) cents += modWheel * 30.0f * std::sin (twoPi * 5.5f * lv.t);
            const float pitchMul = 1.0f + cents * 0.000577789f;
            const float volMul = s.ltgt == 2 ? std::max (0.0f, 1.0f + 0.5f * lfoV) : 1.0f;
            const float panMod = s.ltgt == 3 ? lfoV : 0.0f;
            const float env = lv.amp.next();
            const float fe = lv.fenv.next();

            if (useFilter && (lv.cnt++ & 15) == 0)
            {
                const float oct = kt + s.fenv * 5.0f * fe + (s.ltgt == 1 ? lfoV * 3.0f : 0.0f);
                const float hz = clampf (s.cut * std::pow (2.0f, oct), 20.0f, 20000.0f);
                lv.f1L.set (hz, q, (float) sr); lv.f1R.set (hz, q, (float) sr);
                if (s.f24) { lv.f2L.set (hz, q, (float) sr); lv.f2R.set (hz, q, (float) sr); }
            }

            float l = 0, r = 0;
            if (lv.wave <= 4)
            {
                const float inc0 = baseHz * pitchMul * invSr;
                if (lv.wave == 4)
                {
                    const float w = lv.noiseL.lp (noise.next()) * (1.0f + 2.0f * (1.0f - s.color));
                    l = r = w;
                }
                else
                {
                    const float pw = 0.05f + 0.9f * s.color;
                    for (int k = 0; k < nu; ++k)
                    {
                        const float inc = inc0 * lv.det[k];
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
                        l += x * lv.pl[k]; r += x * lv.pr[k];
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
                const float idx = s.color * 4.0f * (0.4f + 0.9f * v.vel) * std::pow (2.0f, 1.2f * bright) * ((1.0f - std::abs (s.fenv)) + std::abs (s.fenv) * fe);
                lv.mPh += f * s.ratio; lv.mPh -= std::floor (lv.mPh);
                lv.cPh += f; if (lv.cPh >= 1) lv.cPh -= 1;
                l = r = std::sin (twoPi * lv.cPh + idx * std::sin (twoPi * lv.mPh)) * 0.7071f;
            }
            else if (isPiano)
            {
                const auto* sm = lv.smp;
                const int ip = (int) lv.pos;
                if (ip + 3 >= sm->length) { lv.active = false; break; }
                const float fr = (float) (lv.pos - ip);
                auto herm = [fr] (float xm1, float x0, float x1, float x2)
                {
                    const float c = (x1 - xm1) * 0.5f, b = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, a3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                    return ((a3 * fr + b) * fr + c) * fr + x0;
                };
                const int16_t* d = sm->data.data();
                const int im = ip > 0 ? ip - 1 : 0;
                l = herm (d[im * 2], d[ip * 2], d[(ip + 1) * 2], d[(ip + 2) * 2]) * (1.0f / 32768.0f);
                r = herm (d[im * 2 + 1], d[ip * 2 + 1], d[(ip + 1) * 2 + 1], d[(ip + 2) * 2 + 1]) * (1.0f / 32768.0f);
                const double step = std::pow (2.0, (v.curNote + shift - (float) sm->root) / 12.0) * sm->rate / sr * pitchMul;
                lv.pos += step;
                if (lv.atk < 1.0f) lv.atk = std::min (1.0f, lv.atk + lv.atkInc);
                if (lv.damping) lv.level *= lv.dampCoef;
                l *= lv.level * lv.atk; r *= lv.level * lv.atk;
                if (lv.level < 1.0e-4f) lv.active = false;
            }
            else if (lv.user != nullptr)
            {
                const auto& b = lv.user->buf;
                const int len = b.getNumSamples();
                int ip = (int) lv.pos;
                if (ip + 2 >= len)
                {
                    if (s.loop) { lv.pos -= (double) (len - 2); ip = juce::jlimit (0, len - 3, (int) lv.pos); }
                    else { lv.active = false; break; }
                }
                const float fr = (float) (lv.pos - ip);
                const float* bl = b.getReadPointer (0);
                const float* br = b.getReadPointer (b.getNumChannels() > 1 ? 1 : 0);
                l = bl[ip] + fr * (bl[ip + 1] - bl[ip]);
                r = br[ip] + fr * (br[ip + 1] - br[ip]);
                lv.pos += std::pow (2.0, (v.curNote + shift - (float) s.root) / 12.0) * lv.user->rate / sr * pitchMul;
            }
            if (lv.wave == 7 && tilt != 0.0f) { l += tilt * (l - lv.tiltL.lp (l)); r += tilt * (r - lv.tiltR.lp (r)); }

            // filter vrstvy
            if (useFilter)
            {
                float bl, br;
                float yl = lv.f1L.process (l, bl), yr = lv.f1R.process (r, br);
                if (s.ftype == 1) { yl = bl; yr = br; }
                else if (s.ftype == 2) { yl = l - yl - bl * (1.0f / q); yr = r - yr - br * (1.0f / q); }
                if (s.f24)
                {
                    float b2l, b2r;
                    const float zl = lv.f2L.process (yl, b2l), zr = lv.f2R.process (yr, b2r);
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

        if (fadeRequest)
        {
            fadeRequest = false;
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
        if ((int) bufL.size() < total) { bufL.resize ((size_t) total); bufR.resize ((size_t) total); }
        std::fill (bufL.begin(), bufL.begin() + total, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + total, 0.0f);
        int pos = 0, act = 0;
        auto renderTo = [&] (int end)
        {
            while (pos < end)
            {
                const int n = std::min (end - pos, 32);
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
                        if (std::abs (v.drift - v.driftTarget) < 0.05f) v.driftTarget = noise.next() * 4.0f;
                    }
                    for (int li = 0; li < 2; ++li) if (v.lv[li].active) renderLayer (v, li, bufL.data() + pos, bufR.data() + pos, n);
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

        fx.process (bufL.data(), bufR.data(), total, f);
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        if (oR != nullptr) { std::copy (bufL.begin(), bufL.begin() + total, oL); std::copy (bufR.begin(), bufR.begin() + total, oR); }
        else for (int i = 0; i < total; ++i) oL[i] = 0.5f * (bufL[(size_t) i] + bufR[(size_t) i]);
    }
}
