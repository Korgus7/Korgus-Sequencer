#include "Synth.h"

namespace nova
{
    const std::vector<Preset>& presets()
    {
        static const std::vector<Preset> list = []
        {
            std::vector<Preset> v;
            Preset p;
            // 0 koncertne piano
            p = {}; p.name = "Koncertné piano"; p.engine = 0; p.pnTone = 0.1f; p.gain = 0.95f; p.reverb = 0.16f; p.revSize = 0.45f; p.revDamp = 0.45f;
            v.push_back (p);
            // 1 makke piano
            p = {}; p.name = "Mäkké piano"; p.engine = 0; p.pnLayerBias = -0.8f; p.pnTone = -0.55f; p.gain = 1.0f; p.reverb = 0.3f; p.revSize = 0.62f; p.revDamp = 0.55f;
            v.push_back (p);
            // 2 elektricke piano (FM)
            p = {}; p.name = "Elektrické piano"; p.engine = 3; p.a = 0.002f; p.d = 6.0f; p.s = 0; p.r = 0.35f;
            p.fmRatio = 1; p.fmIndex = 1.6f; p.fmIdxT = 0.35f; p.fmIdxFloor = 0.25f; p.fmTine = 0.9f; p.fmTineRatio = 14; p.trem = 0.22f;
            p.chorus = 0.35f; p.reverb = 0.18f; p.revSize = 0.5f; p.gain = 0.34f;
            v.push_back (p);
            // 3 teply pad
            p = {}; p.name = "Teplý pad"; p.engine = 1; p.a = 0.9f; p.d = 1.5f; p.s = 0.85f; p.r = 1.6f;
            p.uni = 5; p.detune = 14; p.saw = 1; p.sub = 0.35f; p.noise = 0.015f; p.cut = 900; p.reso = 0.8f; p.kt = 0.35f;
            p.fAmt = 1.2f; p.fa = 1.2f; p.fd = 2.5f; p.fs = 0.4f; p.fr = 1.5f; p.velCut = 0.6f; p.drift = 3;
            p.chorus = 0.45f; p.reverb = 0.4f; p.revSize = 0.75f; p.revDamp = 0.45f; p.gain = 0.19f;
            v.push_back (p);
            // 4 skleneny pad
            p = {}; p.name = "Sklenený pad"; p.engine = 1; p.a = 1.2f; p.d = 2.0f; p.s = 0.9f; p.r = 2.2f;
            p.uni = 4; p.detune = 9; p.saw = 0.6f; p.pulse = 0.5f; p.pw = 0.3f; p.octUp = 0.35f; p.cut = 2600; p.reso = 1.4f; p.kt = 0.5f;
            p.fAmt = 0.8f; p.fa = 1.5f; p.fd = 3.0f; p.fs = 0.6f; p.fr = 2.0f; p.velCut = 0.5f; p.drift = 4;
            p.chorus = 0.5f; p.reverb = 0.5f; p.revSize = 0.85f; p.revDamp = 0.25f; p.gain = 0.145f;
            v.push_back (p);
            // 5 slaciky (subor)
            p = {}; p.name = "Sláčiky"; p.engine = 1; p.a = 0.32f; p.d = 0.6f; p.s = 0.9f; p.r = 0.7f;
            p.uni = 6; p.detune = 11; p.saw = 1; p.sub = 0.1f; p.cut = 2400; p.reso = 0.75f; p.kt = 0.6f;
            p.fAmt = 0.7f; p.fa = 0.35f; p.fd = 1.0f; p.fs = 0.7f; p.fr = 0.6f; p.velCut = 0.8f;
            p.vibRate = 5.2f; p.vibDepth = 9; p.vibDelay = 0.35f; p.drift = 3;
            p.chorus = 0.55f; p.chorusDepth = 4; p.reverb = 0.35f; p.revSize = 0.7f; p.gain = 0.19f;
            v.push_back (p);
            // 6 husle
            p = {}; p.name = "Husle"; p.engine = 1; p.a = 0.12f; p.d = 0.4f; p.s = 0.9f; p.r = 0.35f;
            p.uni = 2; p.detune = 4; p.saw = 1; p.cut = 4200; p.reso = 0.6f; p.kt = 0.4f;
            p.fAmt = 0.5f; p.fa = 0.12f; p.fd = 0.6f; p.fs = 0.75f; p.fr = 0.4f; p.velCut = 1.0f;
            p.vibRate = 5.6f; p.vibDepth = 22; p.vibDelay = 0.28f; p.body = true; p.bowNoise = 0.06f;
            p.chorus = 0.15f; p.reverb = 0.32f; p.revSize = 0.6f; p.gain = 0.12f;
            v.push_back (p);
            // 7 pluck
            p = {}; p.name = "Pluck"; p.engine = 2; p.a = 0.001f; p.d = 1; p.s = 1; p.r = 0.2f;
            p.plBright = 0.75f; p.plT60 = 1.6f; p.plPick = 0.13f; p.plRel = 0.2f;
            p.chorus = 0.2f; p.reverb = 0.25f; p.revSize = 0.55f; p.gain = 0.6f;
            v.push_back (p);
            // 8 harfa
            p = {}; p.name = "Harfa"; p.engine = 2; p.a = 0.001f; p.d = 1; p.s = 1; p.r = 1.2f;
            p.plBright = 0.45f; p.plT60 = 5.0f; p.plPick = 0.3f; p.plRel = 1.2f; p.plDouble = 0.4f;
            p.chorus = 0.1f; p.reverb = 0.38f; p.revSize = 0.75f; p.gain = 0.55f;
            v.push_back (p);
            // 9 celesta (FM)
            p = {}; p.name = "Celesta"; p.engine = 3; p.a = 0.001f; p.d = 5.0f; p.s = 0; p.r = 0.6f;
            p.fmRatio = 3.5f; p.fmIndex = 0.9f; p.fmIdxT = 0.15f; p.fmIdxFloor = 0.05f; p.fmTine = 0.3f; p.fmTineRatio = 7;
            p.chorus = 0.15f; p.reverb = 0.35f; p.revSize = 0.6f; p.gain = 0.27f;
            v.push_back (p);
            return v;
        }();
        return list;
    }

    Synth::Synth() { bank = PianoBank::get(); }

    void Synth::prepare (double rate, int maxBlock)
    {
        sr = rate;
        bufL.assign ((size_t) juce::jmax (1, maxBlock), 0); bufR.assign ((size_t) juce::jmax (1, maxBlock), 0);
        for (auto& v : voices)
        {
            v.sr = (float) sr; v.note = -1; v.amp.stage = Env::idle; v.smp = nullptr;
            v.dl.assign (4096, 0); v.dl2.assign (4096, 0);
        }
        chorus.prepare ((float) sr); reverb.prepare ((float) sr);
        const auto& p = presets()[(size_t) presetIndex];
        reverb.setSize (p.revSize, p.revDamp);
        tiltL.setHz (2200, (float) sr); tiltR.setHz (2200, (float) sr);
    }

    void Synth::setPreset (int index)
    {
        index = juce::jlimit (0, (int) presets().size() - 1, index);
        if (index == presetIndex) return;
        presetIndex = index;
        // doznievajuce tony rychlo stlmime, nech sa zvuky nemiesaju
        for (auto& v : voices)
            if (v.note >= 0) { v.amp.kill (0.02f); v.damping = true; v.dampCoef = std::exp (-1.0f / (0.01f * (float) sr)); v.held = v.sustained = false; }
    }

    void Synth::allNotesOff()
    {
        for (auto& v : voices) if (v.note >= 0) { v.held = v.sustained = false; releaseVoice (v); }
    }

    Synth::Voice* Synth::allocVoice (int)
    {
        int active = 0;
        Voice* oldest = nullptr;
        for (auto& v : voices)
            if (v.note >= 0)
            {
                ++active;
                const bool rel = ! v.held && ! v.sustained;
                if (oldest == nullptr || (rel && (oldest->held || oldest->sustained)) || ((rel == (! oldest->held && ! oldest->sustained)) && v.age < oldest->age)) oldest = &v;
            }
        // pri vela hlasoch najstarsi rychlo stisime (bez lupnutia), novy ide do volneho
        if (active >= 44 && oldest != nullptr) { oldest->amp.kill (0.012f); oldest->damping = true; oldest->dampCoef = std::exp (-1.0f / (0.004f * (float) sr)); oldest->held = oldest->sustained = false; }
        for (auto& v : voices) if (v.note < 0) return &v;
        return oldest != nullptr ? oldest : &voices[0];
    }

    void Synth::noteOn (int note, float vel)
    {
        const auto& p = presets()[(size_t) presetIndex];
        for (auto& v : voices)
            if (v.note == note && (v.held || v.sustained || v.amp.stage != Env::release))
            {
                v.held = v.sustained = false;
                if (v.engine == 0) { v.damping = true; v.dampCoef = std::exp (-1.0f / (0.06f * (float) sr)); }
                else releaseVoice (v);
            }
        if (p.engine == 0 && ! pianoReady()) return;
        auto* v = allocVoice (note);
        startVoice (*v, note, vel);
    }

    void Synth::noteOff (int note)
    {
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

    void Synth::releaseVoice (Voice& v)
    {
        const auto& p = presets()[(size_t) presetIndex];
        if (v.engine == 0)
        {
            if (! v.noDamper) { v.damping = true; v.dampCoef = std::exp (-1.0f / (v.dampTau * releaseScale() * (float) sr)); }
            return;
        }
        if (v.engine == 2) v.amp.setRelease (p.plRel * releaseScale());
        v.amp.off(); v.fenv.off();
    }

    void Synth::startVoice (Voice& v, int note, float vel)
    {
        const auto& p = presets()[(size_t) presetIndex];
        v.note = note; v.vel = vel; v.engine = p.engine; v.age = ++ageCounter; v.held = true; v.sustained = false; v.t = 0;
        v.freq = noteHz ((float) note); v.sr = (float) sr; v.damping = false; v.level = 1; v.cnt = 0;
        const float as = attackScale(), rs = releaseScale();
        if (p.engine == 1)
        {
            v.amp.set (p.a * as, p.d, p.s, p.r * rs, (float) sr); v.amp.v = 0; v.amp.on();
            v.fenv.set (p.fa * as, p.fd, p.fs, p.fr * rs, (float) sr); v.fenv.v = 0; v.fenv.on();
            const int n = juce::jlimit (1, 8, p.uni);
            for (int k = 0; k < n; ++k)
            {
                const float pos = n > 1 ? (float) k / (float) (n - 1) * 2.0f - 1.0f : 0.0f;
                const float cents = p.detune * pos * (0.85f + 0.3f * std::abs (noise.next()));
                v.inc[k] = v.freq * std::pow (2.0f, cents / 1200.0f) / (float) sr;
                v.ph[k] = 0.5f + 0.5f * noise.next();
                const float pan = pos * 0.8f;
                v.pl[k] = std::cos ((pan + 1.0f) * pi * 0.25f); v.pr[k] = std::sin ((pan + 1.0f) * pi * 0.25f);
                v.drPh[k] = 0.5f + 0.5f * noise.next(); v.drRate[k] = (0.08f + 0.25f * std::abs (noise.next())) / (float) sr;
            }
            v.subPh = 0; v.vibPh = 0; v.fL.reset(); v.fR.reset(); v.bowEnv = 1;
            static const float bf[5] = { 280, 460, 1100, 2600, 3500 }, bq[5] = { 5, 5, 4, 4, 4 };
            for (int i = 0; i < 5; ++i) { v.body[i].reset(); v.body[i].set (bf[i], bq[i], (float) sr); }
        }
        else if (p.engine == 2)
        {
            v.amp.set (0.001f, 1.0f, 1.0f, p.plRel * rs, (float) sr); v.amp.v = 0; v.amp.on();
            const float bright = clampf (p.plBright + (knobs.bright - 0.5f) * 0.8f, 0.0f, 1.0f);
            auto setup = [&] (std::vector<float>& dl, int& dlen, int& w, float& apC, float& apIn, float& apOut, float& yPrev, float detuneCents, float level)
            {
                const float f = v.freq * std::pow (2.0f, detuneCents / 1200.0f);
                const float P = (float) sr / f;
                v.loopS = 0.5f - 0.42f * bright;
                int N = (int) std::floor (P - v.loopS - 0.2f);
                N = juce::jlimit (8, (int) dl.size() - 1, N);
                const float d = P - v.loopS - (float) N;
                apC = (1.0f - d) / (1.0f + d); apIn = apOut = 0; yPrev = 0; dlen = N; w = 0;
                // budenie: sum cez dolnu priepust podla jasu a sily, potom miesto brnknutia (hrebenovy filter)
                OnePole lp; lp.setHz (600.0f * std::pow (2.0f, bright * 5.0f + (v.vel - 0.7f) * 1.5f), (float) sr);
                std::vector<float> ex ((size_t) N);
                float mx = 1.0e-6f;
                for (int i = 0; i < N; ++i) { ex[(size_t) i] = lp.lp (noise.next()); }
                const int pick = juce::jmax (1, (int) (p.plPick * (float) N));
                for (int i = 0; i < N; ++i) { dl[(size_t) i] = ex[(size_t) i] - (i >= pick ? ex[(size_t) (i - pick)] : 0.0f); mx = std::max (mx, std::abs (dl[(size_t) i])); }
                for (int i = 0; i < N; ++i) dl[(size_t) i] *= level / mx;
            };
            setup (v.dl, v.dlen, v.w, v.apC, v.apIn, v.apOut, v.yPrev, 0.0f, 1.0f);
            v.dl2len = 0;
            if (p.plDouble > 0) { int l2; setup (v.dl2, l2, v.w2, v.apC2, v.apIn2, v.apOut2, v.yPrev2, 3.5f, p.plDouble); v.dl2len = l2; }
            const float t60 = clampf (p.plT60 * std::pow (220.0f / v.freq, 0.4f), 0.3f, 14.0f);
            v.loopG = std::pow (10.0f, -3.0f / (t60 * v.freq));
            v.plLev = 0.15f + 0.85f * v.vel;
            const float pan = clampf (((float) note - 64.0f) / 40.0f, -0.6f, 0.6f);
            v.panL = std::cos ((pan + 1.0f) * pi * 0.25f); v.panR = std::sin ((pan + 1.0f) * pi * 0.25f);
        }
        else if (p.engine == 3)
        {
            const float dec = p.d * std::pow (220.0f / v.freq, 0.3f);
            v.amp.set (p.a * as, dec, p.s, p.r * rs, (float) sr); v.amp.v = 0; v.amp.on();
            const float bm = std::pow (2.0f, (knobs.bright - 0.5f) * 2.4f);
            v.idx0 = p.fmIndex * (0.4f + 0.9f * v.vel) * bm;
            v.tineIdx = p.fmTine * v.vel * bm;
            v.fmDec = 1.0f; v.cPh = v.mPh = v.tPh = 0;
        }
        else
        {
            // klavir: vrstva podla sily uderu (bez prelinania - rozne nahravky by sa fazovo rusili)
            static const float centers[4] = { 0.25f, 0.5f, 0.75f, 1.0f };
            const float lpos = v.vel * 4.0f - 1.0f + p.pnLayerBias;
            const int layer = juce::jlimit (0, 3, (int) std::lround (lpos));
            v.smp = bank->find (note, layer);
            if (v.smp == nullptr) { v.note = -1; return; }
            v.step = std::pow (2.0, (note - v.smp->root) / 12.0) * v.smp->rate / sr;
            v.pos = 0;
            v.gain = clampf (std::pow (v.vel / centers[layer], 0.9f), 0.08f, 2.0f) * std::pow (0.15f + 0.85f * v.vel, 1.6f);
            v.noDamper = note >= 89;
            v.dampTau = (float) juce::jmap ((float) juce::jlimit (21, 88, note), 21.0f, 88.0f, 0.22f, 0.07f);
            const float atkTime = knobs.attack > 0.5f ? 0.0015f * std::pow (2.0f, (knobs.attack - 0.5f) * 14.0f) : 0.0015f;
            v.atk = 0; v.atkInc = 1.0f / (atkTime * (float) sr);
            v.amp.stage = Env::sustain; v.amp.s = 1; v.amp.v = 1;
        }
    }

    void Synth::renderAnalog (Voice& v, float* L, float* R, int n)
    {
        const auto& p = presets()[(size_t) presetIndex];
        const int nu = juce::jlimit (1, 8, p.uni);
        const float brightOct = (knobs.bright - 0.5f) * 4.0f;
        const float bendR = std::pow (2.0f, bend / 12.0f);
        const float uniNorm = 1.0f / std::sqrt ((float) nu);
        for (int i = 0; i < n; ++i)
        {
            if ((v.cnt++ & 31) == 0)
            {
                const float fe = v.fenv.v;
                const float oct = p.kt * ((float) v.note - 60.0f) / 12.0f + p.fAmt * fe + p.velCut * (v.vel - 0.7f) + brightOct;
                const float hz = p.cut * std::pow (2.0f, oct);
                v.fL.set (hz, p.reso, (float) sr); v.fR.set (hz, p.reso, (float) sr);
            }
            v.fenv.next();
            const float a = v.amp.next();
            // vibrato s oneskorenim + modulacne koliesko
            float vib = 0;
            if (p.vibDepth > 0 || modWheel > 0)
            {
                v.vibPh += p.vibRate / (float) sr; if (v.vibPh >= 1) v.vibPh -= 1;
                const float fade = clampf ((v.t - p.vibDelay) / 0.4f, 0.0f, 1.0f);
                vib = (p.vibDepth * fade + modWheel * 30.0f) * std::sin (twoPi * v.vibPh);
            }
            float sl = 0, sr2 = 0;
            for (int k = 0; k < nu; ++k)
            {
                float cents = vib;
                if (p.drift > 0) { v.drPh[k] += v.drRate[k]; if (v.drPh[k] >= 1) v.drPh[k] -= 1; cents += p.drift * std::sin (twoPi * v.drPh[k]); }
                const float inc = v.inc[k] * bendR * (1.0f + cents * 0.000577789f);
                float& ph = v.ph[k];
                ph += inc; if (ph >= 1) ph -= 1;
                float s = 0;
                if (p.saw > 0) s += p.saw * (2.0f * ph - 1.0f - polyBlep (ph, inc));
                if (p.pulse > 0)
                {
                    float p2 = ph + p.pw; if (p2 >= 1) p2 -= 1;
                    s += p.pulse * ((2.0f * ph - 1.0f - polyBlep (ph, inc)) - (2.0f * p2 - 1.0f - polyBlep (p2, inc)));
                }
                sl += s * v.pl[k]; sr2 += s * v.pr[k];
            }
            sl *= uniNorm; sr2 *= uniNorm;
            float extra = 0;
            if (p.sub > 0 || p.octUp > 0)
            {
                v.subPh += v.inc[0] * bendR * 0.5f; if (v.subPh >= 1) v.subPh -= 1;
                extra += p.sub * std::sin (twoPi * v.subPh) + p.octUp * std::sin (twoPi * v.subPh * 4.0f);
            }
            if (p.noise > 0) extra += p.noise * noise.next();
            if (p.bowNoise > 0) { extra += p.bowNoise * v.bowEnv * noise.next(); v.bowEnv *= 0.99985f; }
            sl += extra; sr2 += extra;
            float yl = v.fL.lp (sl), yr = v.fR.lp (sr2);
            if (p.body)
            {
                // telo huslí: rezonancie na mono signale
                static const float bg[5] = { 0.9f, 0.7f, 0.55f, 0.7f, 0.45f };
                const float m = 0.5f * (yl + yr);
                float b = 0.35f * m;
                for (int j = 0; j < 5; ++j) b += bg[j] * v.body[j].bpf (m);
                yl = b * 0.85f + 0.15f * yl; yr = b * 0.85f + 0.15f * yr;
            }
            const float g = a * (0.3f + 0.7f * v.vel);
            L[i] += yl * g; R[i] += yr * g;
            v.t += 1.0f / (float) sr;
            if (! v.amp.active()) { v.note = -1; return; }
        }
    }

    void Synth::renderPluck (Voice& v, float* L, float* R, int n)
    {
        const float bendR = std::pow (2.0f, bend / 12.0f);
        (void) bendR;
        for (int i = 0; i < n; ++i)
        {
            const float a = v.amp.next();
            auto string = [&v] (std::vector<float>& dl, int dlen, int& w, float apC, float& apIn, float& apOut, float& yPrev)
            {
                const float y = dl[(size_t) w];
                const float lp = (1.0f - v.loopS) * y + v.loopS * yPrev; yPrev = y;
                const float out = apC * lp + apIn - apC * apOut; apIn = lp; apOut = out;
                dl[(size_t) w] = out * v.loopG;
                if (++w >= dlen) w = 0;
                return y;
            };
            float s = string (v.dl, v.dlen, v.w, v.apC, v.apIn, v.apOut, v.yPrev);
            if (v.dl2len > 0) s += string (v.dl2, v.dl2len, v.w2, v.apC2, v.apIn2, v.apOut2, v.yPrev2);
            s *= v.plLev * a;
            L[i] += s * v.panL; R[i] += s * v.panR;
            v.level = v.level * 0.9995f + std::abs (s) * 0.0005f;
            v.t += 1.0f / (float) sr;
            if (! v.amp.active() || (v.t > 0.1f && v.level < 2.0e-5f)) { v.note = -1; return; }
        }
    }

    void Synth::renderFm (Voice& v, float* L, float* R, int n)
    {
        const auto& p = presets()[(size_t) presetIndex];
        const float bendR = std::pow (2.0f, bend / 12.0f);
        const float f = v.freq * bendR / (float) sr;
        const float idxCoef = std::exp (-1.0f / (p.fmIdxT * (float) sr)), tineCoef = std::exp (-1.0f / (0.035f * (float) sr));
        for (int i = 0; i < n; ++i)
        {
            const float a = v.amp.next();
            v.fmDec *= idxCoef; v.tineIdx *= tineCoef;
            const float idx = v.idx0 * (p.fmIdxFloor + (1.0f - p.fmIdxFloor) * v.fmDec);
            v.mPh += f * p.fmRatio; if (v.mPh >= 1) v.mPh -= 1;
            v.tPh += f * p.fmTineRatio; if (v.tPh >= 1) v.tPh -= 1;
            v.cPh += f; if (v.cPh >= 1) v.cPh -= 1;
            const float mod = idx * std::sin (twoPi * v.mPh) + v.tineIdx * std::sin (twoPi * v.tPh);
            const float s = std::sin (twoPi * v.cPh + mod) * a * (0.1f + 0.9f * std::pow (v.vel, 1.3f));
            L[i] += s * 0.7071f; R[i] += s * 0.7071f;
            if (! v.amp.active()) { v.note = -1; return; }
        }
    }

    void Synth::renderPiano (Voice& v, float* L, float* R, int n)
    {
        const auto* s = v.smp;
        if (s == nullptr) { v.note = -1; return; }
        const int16_t* d = s->data.data();
        const int len = s->length;
        const double step = v.step * std::pow (2.0, bend / 12.0);
        const float k = v.gain * (1.0f / 32768.0f);
        for (int i = 0; i < n; ++i)
        {
            const int ip = (int) v.pos;
            if (ip + 3 >= len) { v.note = -1; return; }
            const float fr = (float) (v.pos - ip);
            // kubicka interpolacia (Hermite)
            auto herm = [fr] (float xm1, float x0, float x1, float x2)
            {
                const float c = (x1 - xm1) * 0.5f, b = xm1 - 2.5f * x0 + 2.0f * x1 - 0.5f * x2, a3 = 0.5f * (x2 - xm1) + 1.5f * (x0 - x1);
                return ((a3 * fr + b) * fr + c) * fr + x0;
            };
            const int im = ip > 0 ? ip - 1 : 0;
            const float l = herm (d[im * 2], d[ip * 2], d[(ip + 1) * 2], d[(ip + 2) * 2]);
            const float r = herm (d[im * 2 + 1], d[ip * 2 + 1], d[(ip + 1) * 2 + 1], d[(ip + 2) * 2 + 1]);
            if (v.atk < 1.0f) v.atk = std::min (1.0f, v.atk + v.atkInc);
            if (v.damping) v.level *= v.dampCoef;
            const float g = k * v.level * v.atk;
            L[i] += l * g; R[i] += r * g;
            v.pos += step;
            if (v.level < 1.0e-4f) { v.note = -1; return; }
        }
    }

    void Synth::renderVoice (Voice& v, float* L, float* R, int n)
    {
        switch (v.engine)
        {
            case 0: renderPiano (v, L, R, n); break;
            case 1: renderAnalog (v, L, R, n); break;
            case 2: renderPluck (v, L, R, n); break;
            default: renderFm (v, L, R, n); break;
        }
    }

    void Synth::process (juce::AudioBuffer<float>& out, const juce::MidiBuffer& midi)
    {
        const int total = out.getNumSamples();
        if ((int) bufL.size() < total) { bufL.resize ((size_t) total); bufR.resize ((size_t) total); }
        std::fill (bufL.begin(), bufL.begin() + total, 0.0f);
        std::fill (bufR.begin(), bufR.begin() + total, 0.0f);
        int pos = 0;
        auto renderTo = [&] (int end)
        {
            while (pos < end)
            {
                const int n = std::min (end - pos, 64);
                for (auto& v : voices) if (v.note >= 0) renderVoice (v, bufL.data() + pos, bufR.data() + pos, n);
                pos += n;
            }
        };
        for (const auto meta : midi)
        {
            renderTo (juce::jlimit (0, total, meta.samplePosition));
            const auto m = meta.getMessage();
            if (m.isNoteOn()) noteOn (m.getNoteNumber(), m.getFloatVelocity());
            else if (m.isNoteOff()) noteOff (m.getNoteNumber());
            else if (m.isSustainPedalOn()) setSustain (true);
            else if (m.isSustainPedalOff()) setSustain (false);
            else if (m.isPitchWheel()) bend = (float) (m.getPitchWheelValue() - 8192) / 8192.0f * 2.0f;
            else if (m.isController() && m.getControllerNumber() == 1) modWheel = (float) m.getControllerValue() / 127.0f;
            else if (m.isAllNotesOff() || m.isAllSoundOff()) { setSustain (false); allNotesOff(); }
        }
        renderTo (total);

        // spolocne efekty
        const auto& p = presets()[(size_t) presetIndex];
        const float revTarget = clampf (p.reverb * 2.0f * knobs.space, 0.0f, 0.9f);
        const float chTarget = clampf (p.chorus * 2.0f * knobs.width, 0.0f, 1.0f);
        const float widthTarget = 0.4f + 1.2f * knobs.width;
        const float volTarget = dbToGain (knobs.volumeDb) * p.gain;
        const float wantSize = clampf (p.revSize + (knobs.space - 0.5f) * 0.4f, 0.0f, 1.0f);
        if (std::abs (wantSize - reverb.size) > 0.01f || std::abs (p.revDamp - reverb.damp) > 0.01f) reverb.setSize (wantSize, p.revDamp);
        const float tilt = p.engine == 0 ? p.pnTone + (knobs.bright - 0.5f) * 1.6f : 0.0f;
        const float sm = 1.0f - std::exp (-1.0f / (0.03f * (float) sr));
        float* oL = out.getWritePointer (0);
        float* oR = out.getNumChannels() > 1 ? out.getWritePointer (1) : nullptr;
        for (int i = 0; i < total; ++i)
        {
            revSm += sm * (revTarget - revSm); chSm += sm * (chTarget - chSm); widthSm += sm * (widthTarget - widthSm); volSm += sm * (volTarget - volSm);
            float l = bufL[(size_t) i], r = bufR[(size_t) i];
            if (tilt != 0.0f) { l += tilt * (l - tiltL.lp (l)); r += tilt * (r - tiltR.lp (r)); }
            if (p.trem > 0)
            {
                tremPh += 4.8f / (float) sr; if (tremPh >= 1) tremPh -= 1;
                const float tr = p.trem * std::sin (twoPi * tremPh);
                l *= 1.0f + tr; r *= 1.0f - tr;
            }
            if (chSm > 0.001f) chorus.process (l, r, chSm, p.chorusDepth);
            float rl, rr;
            reverb.process (l, r, rl, rr);
            l += rl * revSm; r += rr * revSm;
            const float mid = 0.5f * (l + r), side = 0.5f * (l - r) * widthSm;
            l = (mid + side) * volSm; r = (mid - side) * volSm;
            auto clip = [] (float x) { const float a = std::abs (x); return a < 0.9f ? x : std::copysign (0.9f + 0.1f * std::tanh ((a - 0.9f) * 10.0f), x); };
            oL[i] = clip (l);
            if (oR != nullptr) oR[i] = clip (r);
        }
    }
}
