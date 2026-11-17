#include "CrashGuard.h"
#include "Engine.h"
#include "BuiltinFx.h"

namespace
{
    int pluginChans (const juce::AudioPluginInstance* p)
    {
        return p == nullptr ? 0 : juce::jmax (p->getTotalNumInputChannels(), p->getTotalNumOutputChannels());
    }
    void preparePlugin (juce::AudioPluginInstance* p, double sr, int bs)
    {
        if (p == nullptr) return;
        p->setRateAndBufferSizeDetails (sr, bs);
        p->prepareToPlay (sr, bs);
    }
}

void Engine::Slot::addVoice (AudioBufPtr b, juce::int64 pos, float gain, int lane)
{
    if (b == nullptr) return;
    if (lane == 3)   // zatvoreny hi-hat zastavi otvoreny
        for (auto& v : voices) if (v.lane == 4 && v.fade < 0) { v.fade = 64; v.fadeLen = 64; }
    if (lane == 99)  // nahlad v prehliadaci zvukov: predosly nahlad stichne
        for (auto& v : voices) if (v.lane == 99 && v.fade < 0) { v.fade = 256; v.fadeLen = 256; }
    if (voices.size() >= 64) voices.erase (voices.begin());
    Voice v; v.buf = std::move (b); v.pos = pos; v.gain = gain; v.lane = lane;
    if (juce::isPositiveAndBelow (lane, 10)) v.ds = dset[(size_t) lane];
    voices.push_back (std::move (v));
}

namespace
{
    inline float ahd (float t, float a, float h, float d, bool infH, bool infD)
    {
        if (t < a) return a > 0 ? t / a : 1.0f;
        t -= a;
        if (infH || t < h) return 1.0f;
        if (infD) return 1.0f;
        t -= h;
        return t >= d ? 0.0f : 1.0f - t / d;
    }
}

// hlas s upravou zvuku (vyska, filter, obalky, panorama); vrati true, ked doznel
bool Engine::Slot::renderEdited (Voice& v, float* oL, float* oR, int n, double sr)
{
    const auto& d = v.ds;
    const int len = v.buf->getNumSamples();
    const float* l = v.buf->getReadPointer (0);
    const float* r = v.buf->getNumChannels() > 1 ? v.buf->getReadPointer (1) : l;
    const float vel = juce::jlimit (0.0f, 1.0f, v.gain);
    const float baseSt = d.tr + d.tune * 0.01f + d.pVel * (vel - 0.75f) * 12.0f;
    const float ampG = juce::Decibels::decibelsToGain (d.gain) * ((1.0f - d.aVel) + d.aVel * vel);
    const float pl = std::cos ((d.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
    const float pr = std::sin ((d.pan + 1.0f) * 0.25f * juce::MathConstants<float>::pi) * 1.41421356f;
    const bool infH = d.aH >= 1.999f, infD = d.aD >= 3.999f;
    const float dt = (float) (1.0 / sr);
    const float drv = 1.0f + d.drive * 6.0f, drvN = 1.0f / std::tanh (drv);
    for (int i = juce::jmax (0, (int) -v.pos); i < n; ++i)
    {
        if (v.rpos >= len - 1) return true;
        const float pe = ahd (v.t, d.pA, d.pH, d.pD, false, false);
        const float st = baseSt + d.pEnv * pe;
        const int i0 = (int) v.rpos; const float fr = (float) (v.rpos - i0);
        float xl = l[i0] + (l[i0 + 1] - l[i0]) * fr, xr = r[i0] + (r[i0 + 1] - r[i0]) * fr;
        v.rpos += std::exp2 (st / 12.0f);
        if (d.fOn)
        {
            if ((v.cnt++ & 15) == 0)
            {
                const float fe = ahd (v.t, d.fA, d.fH, d.fD, false, false);
                const float fc = juce::jlimit (20.0f, (float) sr * 0.45f, d.cut * std::exp2 (d.fEnv * 5.0f * fe + d.fVel * (vel - 0.75f) * 4.0f));
                v.g = std::tan (juce::MathConstants<float>::pi * fc / (float) sr);
                v.k = 2.0f - 1.94f * juce::jlimit (0.0f, 1.0f, d.res);
            }
            // typy: 0 LP24 Ladder, 1 LP24 Zero, 2 LP12 Ladder, 3 BP12 Ladder, 4 HP12 Ladder, 5 LP12 State, 6 BP12 State, 7 HP12 State
            const int ft = juce::jlimit (0, 7, d.fType);
            const int stages = ft <= 1 ? 2 : 1;
            const int mode = (ft == 3 || ft == 6) ? 2 : ((ft == 4 || ft == 7) ? 1 : 0);   // 0 LP, 1 HP, 2 BP
            const bool ladder = ft == 0 || ft == 2 || ft == 3 || ft == 4;                  // ladder = mierne skreslenie v spatnej vazbe
            float* xs[2] = { &xl, &xr };
            for (int c = 0; c < 2; ++c)
                for (int sg = 0; sg < stages; ++sg)
                {
                    const int z = c * 2 + sg;
                    float x = *xs[c];
                    const float s1 = ladder ? std::tanh (v.s1[z]) : v.s1[z];
                    const float hp = (x - (v.k + v.g) * s1 - v.s2[z]) / (1.0f + v.g * (v.k + v.g));
                    const float bp = v.g * hp + s1; v.s1[z] = v.g * hp + bp;
                    const float lp = v.g * bp + v.s2[z]; v.s2[z] = v.g * bp + lp;
                    *xs[c] = mode == 1 ? hp : (mode == 2 ? bp : lp);
                }
        }
        if (d.drive > 0.001f)
        {
            if (d.soft) { xl = std::tanh (xl * drv) * drvN; xr = std::tanh (xr * drv) * drvN; }   // Soft = makke skreslenie
            else { xl = juce::jlimit (-1.0f, 1.0f, xl * drv); xr = juce::jlimit (-1.0f, 1.0f, xr * drv); }
        }
        const float ae = ahd (v.t, d.aA, d.aH, d.aD, infH, infD);
        if (v.t > d.aA && ae <= 0.0f) return true;
        float g = ampG * ae * (1.0f + d.punch * 1.5f * std::exp (-v.t / 0.012f));
        if (v.fade >= 0) { if (v.fade == 0) return true; g *= (float) v.fade / (float) v.fadeLen; --v.fade; }
        if (! std::isfinite (xl) || ! std::isfinite (xr)) return true;
        oL[i] += xl * g * pl; oR[i] += xr * g * pr;
        v.t += dt;
    }
    return false;
}

void Engine::Slot::renderVoices (juce::AudioBuffer<float>& dst, int n)
{
    if (killVoices.exchange (false)) for (auto& v : voices) if (v.fade < 0) { v.fade = 256; v.fadeLen = 256; }
    for (size_t k = 0; k < voices.size();)
    {
        auto& v = voices[k];
        if (v.ds.on)
        {
            const bool done = renderEdited (v, dst.getWritePointer (0), dst.getWritePointer (1), n, sr);
            v.pos += n;
            if (done) { voices[k] = std::move (voices.back()); voices.pop_back(); }
            else ++k;
            continue;
        }
        const int len = v.buf->getNumSamples();
        const float* l = v.buf->getReadPointer (0);
        const float* r = v.buf->getNumChannels() > 1 ? v.buf->getReadPointer (1) : l;
        float* oL = dst.getWritePointer (0);
        float* oR = dst.getWritePointer (1);
        bool done = false;
        for (int i = juce::jmax (0, (int) -v.pos); i < n; ++i)
        {
            const juce::int64 sp = v.pos + i;
            if (sp >= len) { done = true; break; }
            float g = v.gain;
            if (v.fade >= 0) { if (v.fade == 0) { done = true; break; } g *= (float) v.fade / (float) v.fadeLen; --v.fade; }
            oL[i] += l[sp] * g;
            oR[i] += r[sp] * g;
        }
        v.pos += n;
        if (done || v.pos >= len) { voices[k] = std::move (voices.back()); voices.pop_back(); }
        else ++k;
    }
}

int Engine::Slot::numChans() const
{
    int ch = juce::jmax (2, pluginChans (plugin.get()));
    for (const auto& f : fx) ch = juce::jmax (ch, pluginChans (f.get()));
    return ch;
}

void Engine::Slot::prepare (double rate, int bs)
{
    sr = rate;
    const double sr = rate;
    preparePlugin (plugin.get(), sr, bs);
    for (auto& f : fx) preparePlugin (f.get(), sr, bs);
    preparePlugin (midiFx.get(), sr, bs);
    buf.setSize (numChans(), juce::jmax (bs, 16), false, true, false);
    midi.ensureSize (2048);
    active.reserve (128);
}

Engine::Engine()
{
    cmds.reserve (64);
    order.reserve (512);
    trigs.reserve (64);
    fxMidi.ensureSize (2048);
    trackMidi.ensureSize (2048);
    masterSlot = getOrCreateSlot ("__master");
    auxSlot[0] = getOrCreateSlot ("__aux1");
    auxSlot[1] = getOrCreateSlot ("__aux2");
}

Engine::~Engine()
{
    const juce::ScopedLock sl (slotLock);
    forEachPlugin ([] (juce::AudioPluginInstance& p) { p.releaseResources(); });
}

void Engine::forEachPlugin (const std::function<void (juce::AudioPluginInstance&)>& fn)
{
    for (auto& s : slots)
    {
        if (s->plugin != nullptr) fn (*s->plugin);
        for (auto& f : s->fx) if (f != nullptr) fn (*f);
    }
}

//==============================================================================
int Engine::getOrCreateSlot (const juce::String& trackId)
{
    const juce::ScopedLock sl (slotLock);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->trackId == trackId) return (int) i;
    auto s = std::make_unique<Slot>();
    s->trackId = trackId;
    s->buf.setSize (2, juce::jmax (blockSize, 16));
    s->midi.ensureSize (2048);
    s->active.reserve (128);
    s->voices.reserve (64);
    s->lastParams.reserve (64);
    slots.push_back (std::move (s));
    return (int) slots.size() - 1;
}

int Engine::getNumSlots()
{
    const juce::ScopedLock sl (slotLock);
    return (int) slots.size();
}

juce::String Engine::getSlotTrackId (int slot)
{
    const juce::ScopedLock sl (slotLock);
    return juce::isPositiveAndBelow (slot, (int) slots.size()) ? slots[(size_t) slot]->trackId : juce::String();
}

juce::AudioPluginInstance* Engine::getPlugin (int slot)
{
    const juce::ScopedLock sl (slotLock);
    return juce::isPositiveAndBelow (slot, (int) slots.size()) ? slots[(size_t) slot]->plugin.get() : nullptr;
}

juce::AudioPluginInstance* Engine::getFx (int slot, int index)
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slot, (int) slots.size()) || ! juce::isPositiveAndBelow (index, numFx)) return nullptr;
    return slots[(size_t) slot]->fx[(size_t) index].get();
}

void Engine::notifyNewSong()
{
    const juce::ScopedLock sl (slotLock);
    forEachPlugin ([] (juce::AudioPluginInstance& p) { BuiltinFx::newSong (&p); });
}

void Engine::setTrackPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p != nullptr) p->setPlayHead (&head);
    preparePlugin (p.get(), sampleRate, blockSize);   // mimo zamku, aby audio nezavahalo
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        if (! juce::isPositiveAndBelow (slot, (int) slots.size())) return;
        auto& s = *slots[(size_t) slot];
        old = std::move (s.plugin);
        s.plugin = std::move (p);
        s.active.clear();
        s.buf.setSize (s.numChans(), juce::jmax (blockSize, 16), false, true, false);
    }
    if (old != nullptr) old->releaseResources();
}

void Engine::setTrackFx (int slot, int index, std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (! juce::isPositiveAndBelow (index, numFx)) return;
    if (p != nullptr) p->setPlayHead (&head);
    preparePlugin (p.get(), sampleRate, blockSize);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        if (! juce::isPositiveAndBelow (slot, (int) slots.size())) return;
        auto& s = *slots[(size_t) slot];
        old = std::move (s.fx[(size_t) index]);
        s.fx[(size_t) index] = std::move (p);
        s.buf.setSize (s.numChans(), juce::jmax (blockSize, 16), false, true, false);
    }
    if (old != nullptr) old->releaseResources();
}

void Engine::setTrackMidiFx (int slot, std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p != nullptr) p->setPlayHead (&head);
    preparePlugin (p.get(), sampleRate, blockSize);
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        if (! juce::isPositiveAndBelow (slot, (int) slots.size())) return;
        auto& s = *slots[(size_t) slot];
        old = std::move (s.midiFx);
        s.midiFx = std::move (p);
    }
    if (old != nullptr) old->releaseResources();
}

juce::AudioPluginInstance* Engine::getMidiFx (int slot)
{
    const juce::ScopedLock sl (slotLock);
    if (! juce::isPositiveAndBelow (slot, (int) slots.size())) return nullptr;
    return slots[(size_t) slot]->midiFx.get();
}

void Engine::triggerSample (int slot, AudioBufPtr buf, float gain, int lane)
{
    if (buf == nullptr) return;
    const juce::SpinLock::ScopedLockType l (trigLock);
    if (trigs.size() < 64) trigs.push_back ({ slot, std::move (buf), gain, lane });
}

void Engine::setMidiFilter (const juce::String& deviceId)
{
    const juce::SpinLock::ScopedLockType l (filterLock);
    midiFilter = deviceId;
}

void Engine::setDrumSetLive (int slot, int lane, const DrumSet& d)
{
    const juce::SpinLock::ScopedLockType l (liveLock);
    for (auto& x : liveSets) if (x.slot == slot && x.lane == lane) { x.ds = d; return; }
    if (liveSets.size() < 64) liveSets.push_back ({ slot, lane, d });
}

void Engine::setSong (std::unique_ptr<SongData> s)
{
    { const juce::SpinLock::ScopedLockType l (liveLock); liveSets.clear(); }   // nova pieseň uz obsahuje upravy
    song.store (s.get());
    songHistory.push_back (std::move (s));
    // posledne verzie nechame zit, aby ich audio vlakno nikdy nedrzalo zmazane
    while (songHistory.size() > 8) songHistory.erase (songHistory.begin());
}

void Engine::play (double fromStep, int countInBars)
{
    const juce::SpinLock::ScopedLockType l (cmdLock);
    cmds.push_back ({ 1, fromStep, countInBars });
}

void Engine::stop()
{
    const juce::SpinLock::ScopedLockType l (cmdLock);
    cmds.push_back ({ 2, 0, 0 });
}

void Engine::setPosition (double step)
{
    publicPos = step;
    const juce::SpinLock::ScopedLockType l (cmdLock);
    cmds.push_back ({ 3, step, 0 });
}

void Engine::liveNote (int slot, int pitch, float vel, bool on)
{
    if (slot < 0) return;
    auto m = on ? juce::MidiMessage::noteOn (1, juce::jlimit (0, 127, pitch), juce::jlimit (0.01f, 1.0f, vel))
                : juce::MidiMessage::noteOff (1, juce::jlimit (0, 127, pitch));
    const auto scope = liveFifo.write (1);
    if (scope.blockSize1 > 0) liveBuf[(size_t) scope.startIndex1] = { slot, m };
    else if (scope.blockSize2 > 0) liveBuf[(size_t) scope.startIndex2] = { slot, m };
}

void Engine::handleIncomingMidiMessage (juce::MidiInput* src, const juce::MidiMessage& m)
{
    if (m.isController() && m.getChannel() >= 1)
    {
        // MIDI learn / priradene ovladace (z akehokolvek zariadenia)
        const CcIn c { (uint8_t) m.getChannel(), (uint8_t) m.getControllerNumber(), (uint8_t) m.getControllerValue() };
        const auto sc = ccFifo.write (1);
        if (sc.blockSize1 > 0) ccBuf[(size_t) sc.startIndex1] = c;
        else if (sc.blockSize2 > 0) ccBuf[(size_t) sc.startIndex2] = c;
        if (ccMapped[(size_t) ((c.ch - 1) * 128 + c.cc)].load()) return;
    }
    {
        // stopa moze mat vybrane, z ktorych klaves hra
        const juce::SpinLock::ScopedLockType l (filterLock);
        if (midiFilter.isNotEmpty() && (src == nullptr || src->getIdentifier() != midiFilter)) return;
    }
    const int slot = selectedSlot.load();
    if (m.isNoteOnOrOff() || m.isController() || m.isPitchWheel() || m.isChannelPressure() || m.isAftertouch())
    {
        auto copy = m;
        copy.setChannel (1);
        const auto scope = liveFifo.write (1);
        if (scope.blockSize1 > 0) liveBuf[(size_t) scope.startIndex1] = { slot, copy };
        else if (scope.blockSize2 > 0) liveBuf[(size_t) scope.startIndex2] = { slot, copy };
        std::array<int, 8> ex {}; int ne = 0;
        { const juce::SpinLock::ScopedLockType l (filterLock); ex = extraSlots; ne = numExtra; }
        for (int k = 0; k < ne; ++k)
        {
            if (ex[(size_t) k] == slot || ex[(size_t) k] < 0) continue;
            const auto sc2 = liveFifo.write (1);
            if (sc2.blockSize1 > 0) liveBuf[(size_t) sc2.startIndex1] = { ex[(size_t) k], copy };
            else if (sc2.blockSize2 > 0) liveBuf[(size_t) sc2.startIndex2] = { ex[(size_t) k], copy };
        }
    }
    if (m.isNoteOnOrOff() || (m.isController() && m.getControllerNumber() == 64))
    {
        const int pitch = m.isController() ? -64 : m.getNoteNumber();
        const float vel = m.isController() ? (float) m.getControllerValue() / 127.0f : m.getFloatVelocity();
        const bool on = m.isController() ? m.getControllerValue() >= 64 : m.isNoteOn();
        // pozicia, ktoru hrac prave POCUJE: koniec posledneho bloku + uplynuty cas - oneskorenie vystupu zvukovky
        double p = publicPos.load();
        if (state == playing)
            p = juce::jmax (0.0, p + ((juce::Time::getMillisecondCounterHiRes() - publicMs.load()) * 0.001 - outLatSec.load()) * publicSps.load());
        juce::MessageManager::callAsync ([this, pitch, vel, on, p] { if (onMidiIn) onMidiIn (pitch, vel, on, p); });
    }
}

//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    const juce::ScopedLock sl (slotLock);
    sampleRate = dev->getCurrentSampleRate();
    blockSize = dev->getCurrentBufferSizeSamples();
    outLatSec = (dev->getOutputLatencyInSamples() + blockSize) / juce::jmax (1.0, sampleRate);
    for (auto& s : slots) s->prepare (sampleRate, blockSize);
}

void Engine::audioDeviceStopped()
{
    const juce::ScopedLock sl (slotLock);
    forEachPlugin ([] (juce::AudioPluginInstance& p) { p.releaseResources(); });
}

void Engine::addClick (int offset, int level)
{
    if (level == 0 && ! metroAcc.load()) level = 1;
    const int snd = juce::jlimit (0, 3, metroSnd.load());
    clickSndCur = snd;
    clickDelay = juce::jmax (0, offset);
    static const double lens[4] { 0.05, 0.035, 0.012, 0.18 };
    clickLen = clickLeft = juce::jmax (1, (int) (sampleRate * lens[snd]));
    clickPhase = clickPhase2 = 0;
    clickNoise = 0;
    static const double freq[4][3] { { 1600, 1050, 820 }, { 2100, 1500, 1250 }, { 1, 1, 1 }, { 2637, 1975, 1568 } };
    clickInc  = juce::MathConstants<double>::twoPi * freq[snd][level] / sampleRate;
    clickInc2 = clickInc * 2.76;   // druhy alikvot pre drievko a zvoncek
    clickAmp = metroVol.load() * (level == 0 ? 1.0f : level == 1 ? 0.8f : 0.5f);
}

void Engine::renderClicks (float* const* out, int numOut, int n)
{
    if (metroTest.exchange (false)) addClick (0, 0);
    for (int i = 0; i < n && (clickLeft > 0 || clickDelay > 0); ++i)
    {
        if (clickDelay > 0) { --clickDelay; continue; }
        const float env = (float) clickLeft / (float) juce::jmax (1, clickLen);
        float s = 0;
        switch (clickSndCur)
        {
            case 1:   // drievko: kratky tlmeny tón s alikvotom
                s = ((float) std::sin (clickPhase) + 0.35f * (float) std::sin (clickPhase2)) * 0.32f * env * env * env;
                break;
            case 2:   // klik: kratky sum s vysokou priepustou
            {
                const float w = clickRnd.nextFloat() * 2.0f - 1.0f;
                s = (w - clickNoise) * 0.45f * env * env;
                clickNoise = w;
                break;
            }
            case 3:   // zvoncek: zvoniaci tón s dlhsim dozvukom
                s = ((float) std::sin (clickPhase) + 0.25f * (float) std::sin (clickPhase2)) * 0.22f * env * env;
                break;
            default:  // pipnutie
                s = (float) std::sin (clickPhase) * 0.3f * env * env;
                break;
        }
        s *= clickAmp;
        clickPhase += clickInc; clickPhase2 += clickInc2;
        --clickLeft;
        for (int c = 0; c < numOut; ++c)
            if (out[c] != nullptr) out[c][i] += s;
    }
}

void Engine::allNotesOff (int off, bool hard)
{
    for (auto& s : slots)
    {
        for (auto& a : s->active)
            s->midi.addEvent (juce::MidiMessage::noteOff (s->channel, a.first), off);
        s->active.clear();
        if (hard)
        {
            s->midi.addEvent (juce::MidiMessage::controllerEvent (s->channel, 64, 0), off);
            s->midi.addEvent (juce::MidiMessage::allNotesOff (s->channel), off);
        }
    }
}

//==============================================================================
void Engine::mixClips (const TrackData& td, juce::AudioBuffer<float>& dst, double p0, int off, int len, double sps)
{
    for (const auto& c : td.clips)
    {
        if (c.buf == nullptr) continue;
        // vzoriek od zaciatku klipu (cas, nie kroky - pri zmene tempa klip hra plynulo dalej)
        const double into = p0 >= c.start && curSongData != nullptr ? curSongData->secondsBetween (c.start, p0) * sampleRate : (p0 - c.start) * sps;
        const double durS = c.dur * sampleRate;
        const int i0 = juce::jmax (0, (int) std::ceil (-into));
        const int i1 = juce::jmin (len, (int) std::floor (durS - into));
        if (i1 <= i0) continue;
        const juce::int64 src0 = (juce::int64) std::llround (c.off * sampleRate + into + i0);
        const int bufLen = c.buf->getNumSamples();
        if (src0 >= bufLen) continue;
        int a = i0;
        juce::int64 sp = src0;
        if (sp < 0) { a += (int) -sp; sp = 0; }
        const int cnt = juce::jmin (i1 - a, (int) (bufLen - sp));
        if (cnt <= 0) continue;
        const float* l = c.buf->getReadPointer (0) + sp;
        const float* r = c.buf->getNumChannels() > 1 ? c.buf->getReadPointer (1) + sp : l;
        if (c.fi <= 0 && c.fo <= 0)
        {
            dst.addFrom (0, off + a, l, cnt, c.gain);
            dst.addFrom (1, off + a, r, cnt, c.gain);
        }
        else
        {
            // nabeh a dobeh klipu
            const double fiS = c.fi * sampleRate, foS = c.fo * sampleRate;
            float* oL = dst.getWritePointer (0) + off + a;
            float* oR = dst.getWritePointer (1) + off + a;
            for (int i = 0; i < cnt; ++i)
            {
                const double t = into + a + i;
                double g = c.gain;
                if (fiS > 0 && t < fiS) { const double u = juce::jmax (0.0, t / fiS); g *= c.xfi ? std::sin (u * juce::MathConstants<double>::halfPi) : u; }
                if (foS > 0 && t > durS - foS) { const double u = juce::jmax (0.0, (durS - t) / foS); g *= c.xfo ? std::sin (u * juce::MathConstants<double>::halfPi) : u; }
                oL[i] += l[i] * (float) g;
                oR[i] += r[i] * (float) g;
            }
        }
    }
}

void Engine::captureInput (int off, int len, double p0)
{
    const int nr = numRecs.load();
    if (! recArmed.load() || recStopped.load() || curIn == nullptr || curNumIn <= 0 || len <= 0 || nr <= 0) return;
    if (! recActive.load()) { recStartStep = p0; recActive = true; }
    for (int k = 0; k < nr; ++k)
    {
        auto& r = *recs[(size_t) k];
        const float* a = juce::isPositiveAndBelow (r.a, curNumIn) ? curIn[r.a] : nullptr;
        const float* b = juce::isPositiveAndBelow (r.b, curNumIn) ? curIn[r.b] : nullptr;
        const auto scope = r.fifo.write (len);
        auto put = [&] (int start, int size, int from)
        {
            for (int i = 0; i < size; ++i)
            {
                r.l[(size_t) (start + i)] = a != nullptr ? a[off + from + i] : 0.0f;
                if (r.b >= 0) r.r[(size_t) (start + i)] = b != nullptr ? b[off + from + i] : 0.0f;
            }
        };
        put (scope.startIndex1, scope.blockSize1, 0);
        put (scope.startIndex2, scope.blockSize2, scope.blockSize1);
    }
}

void Engine::startRecording (const std::vector<RecSpec>& specs)
{
    recArmed = false;
    numRecs = 0;
    // volane az po dokonceni predoslej nahravky, audio vlakno teraz zasobniky nepouziva
    const int cap = (int) (juce::jmax (44100.0, sampleRate) * 10);
    recs.clear();
    for (const auto& sp : specs)
    {
        auto r = std::make_unique<Rec> (cap);
        r->a = sp.a; r->b = sp.b;
        recs.push_back (std::move (r));
    }
    recStopped = false;
    recActive = false;
    numRecs = (int) recs.size();
    recArmed = ! recs.empty();
}

void Engine::drainRecording (int index, std::vector<float>& left, std::vector<float>& right)
{
    if (! juce::isPositiveAndBelow (index, (int) recs.size())) return;
    auto& r = *recs[(size_t) index];
    const auto scope = r.fifo.read (r.fifo.getNumReady());
    auto take = [&] (std::vector<float>& dst, const std::vector<float>& src)
    {
        dst.insert (dst.end(), src.begin() + scope.startIndex1, src.begin() + scope.startIndex1 + scope.blockSize1);
        dst.insert (dst.end(), src.begin() + scope.startIndex2, src.begin() + scope.startIndex2 + scope.blockSize2);
    };
    take (left, r.l);
    if (r.b >= 0) take (right, r.r);
}

void Engine::renderSegment (const SongData& sd, double p0, double p1, int off, int len, double sps)
{
    // metronom na dobach
    if (metroOn.load() && ! offline)
    {
        const double sub = (double) juce::jlimit (1, 4, metroSub.load());
        for (double b = std::ceil (p0 / sub - 1e-9) * sub; b < p1; b += sub)
            addClick (off + (int) ((b - p0) * sps), std::fmod (b, 16.0) < 0.5 ? 0 : std::fmod (b, 4.0) < 0.5 ? 1 : 2);
    }

    for (const auto& td : sd.tracks)
    {
        if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
        auto& s = *slots[(size_t) td.slot];
        s.dset = td.dset;   // upravy zvuku bicich pre nove udery
        {
            const juce::SpinLock::ScopedTryLockType tl (liveLock);
            if (tl.isLocked())
                for (const auto& x : liveSets) if (x.slot == td.slot && juce::isPositiveAndBelow (x.lane, 10)) s.dset[(size_t) x.lane] = x.ds;
        }

        if (td.isAudio || td.frozen)
        {
            if (td.audible) mixClips (td, s.buf, p0, off, len, sps);
            continue;
        }

        // konce nôt
        for (size_t i = 0; i < s.active.size();)
        {
            if (s.active[i].second < p1)
            {
                const int o = off + juce::jmax (0, (int) ((s.active[i].second - p0) * sps));
                s.midi.addEvent (juce::MidiMessage::noteOff (td.channel, s.active[i].first), o);
                s.active[i] = s.active.back();
                s.active.pop_back();
            }
            else ++i;
        }
        if (! td.audible) continue;

        // MIDI CC a pitch bend (pred notami v tom istom okamihu)
        auto cit = std::lower_bound (td.ccs.begin(), td.ccs.end(), p0, [] (const CcEv& c, double v) { return c.start < v; });
        for (; cit != td.ccs.end() && cit->start < p1; ++cit)
        {
            const int o = off + juce::jmax (0, (int) ((cit->start - p0) * sps));
            s.midi.addEvent (cit->ctrl == 128 ? juce::MidiMessage::pitchWheel (td.channel, cit->value)
                                              : juce::MidiMessage::controllerEvent (td.channel, cit->ctrl, cit->value), o);
        }

        // zaciatky nôt
        auto it = std::lower_bound (td.notes.begin(), td.notes.end(), p0,
                                    [] (const NoteEv& n, double v) { return n.start < v; });
        for (; it != td.notes.end() && it->start < p1; ++it)
        {
            const int o = off + juce::jmax (0, (int) ((it->start - p0) * sps));
            s.midi.addEvent (juce::MidiMessage::noteOn (td.channel, it->pitch, it->vel), o);
            if (s.active.size() < 512) s.active.push_back ({ it->pitch, it->end });
        }

        // udery zo samplov
        auto h = std::lower_bound (td.hits.begin(), td.hits.end(), p0,
                                   [] (const HitEv& x, double v) { return x.start < v; });
        for (; h != td.hits.end() && h->start < p1; ++h)
        {
            const int o = off + juce::jmax (0, (int) ((h->start - p0) * sps));
            s.addVoice (h->buf, -(juce::int64) o, h->gain, h->lane);
        }
    }
}

//==============================================================================
void Engine::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext&)
{
    process (in, numIn, out, numOut, n, nullptr);
}

void Engine::process (const float* const* in, int numIn, float* const* out, int numOut, int n, const SongData* forced)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    if (forced == nullptr)
        if (const int tc = tunerCh.load (std::memory_order_relaxed); tc >= 0 && tc < numIn && in != nullptr && in[tc] != nullptr)
        {
            unsigned w = tunerW.load (std::memory_order_relaxed);
            for (int i = 0; i < n; ++i) tunerBuf[(w++) & (tunerSize - 1)] = in[tc][i];
            tunerW.store (w, std::memory_order_release);
        }

    const juce::ScopedTryLock sl (slotLock);
    if (! sl.isLocked() || (offline.load() && forced == nullptr)) return;   // pocas exportu ticho
    curIn = in; curNumIn = numIn; curOut = out; curNumOut = juce::jmin (2, numOut);

    for (auto& s : slots) s->midi.clear();

    // ziva hra (klavesy, MIDI)
    {
        const int ready = liveFifo.getNumReady();
        const auto scope = liveFifo.read (ready);
        auto take = [this] (int start, int size)
        {
            for (int i = 0; i < size; ++i)
            {
                const auto& l = liveBuf[(size_t) (start + i)];
                if (juce::isPositiveAndBelow (l.slot, (int) slots.size()))
                {
                    auto& sl = *slots[(size_t) l.slot];
                    auto msg = l.msg;
                    msg.setChannel (sl.channel);
                    sl.midi.addEvent (msg, 0);
                }
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
    }

    // nahlad / ziva hra samplov
    {
        const juce::SpinLock::ScopedTryLockType l (trigLock);
        if (l.isLocked())
        {
            for (auto& t : trigs)
                if (juce::isPositiveAndBelow (t.slot, (int) slots.size()))
                {
                    auto& sl = *slots[(size_t) t.slot];
                    // aktualne upravy zvuku (aj ked sa neprehrava)
                    if (auto* sd = song.load())
                        for (const auto& td : sd->tracks) if (td.slot == t.slot) { sl.dset = td.dset; break; }
                    {
                        const juce::SpinLock::ScopedTryLockType tl (liveLock);
                        if (tl.isLocked())
                            for (const auto& x : liveSets) if (x.slot == t.slot && juce::isPositiveAndBelow (x.lane, 10)) sl.dset[(size_t) x.lane] = x.ds;
                    }
                    sl.addVoice (t.buf, 0, t.gain, t.lane);
                }
            trigs.clear();
        }
    }

    // prikazy z hlavneho vlakna
    {
        const juce::SpinLock::ScopedTryLockType l (cmdLock);
        if (l.isLocked())
        {
            for (const auto& c : cmds)
            {
                if (c.type == 1)
                {
                    pos = c.a;
                    if (c.b > 0 && song.load() != nullptr)
                    {
                        beatSamples = 60.0 / song.load()->tempoAt (c.a) * sampleRate;
                        countBeatsLeft = c.b * 4;
                        countTotal = countBeatsLeft;
                        countSamples = 0;
                        countBeat = 0;
                        startAfterCount = c.a;
                        state = counting;
                    }
                    else state = playing;
                }
                else if (c.type == 2)
                {
                    allNotesOff (0, true);
                    for (auto& sl : slots) for (auto& v : sl->voices) if (v.fade < 0) { v.fade = 256; v.fadeLen = 256; }
                    state = stopped;
                }
                else if (c.type == 3)
                {
                    allNotesOff (0, false);
                    pos = c.a;
                }
            }
            cmds.clear();
        }
    }

    auto* sd = forced != nullptr ? forced : song.load();

    curSongData = sd;
    head.bpm = sd != nullptr ? juce::jlimit (20.0, 400.0, sd->tempoAt (pos)) : 120.0;
    head.ppq = pos / 4.0;
    head.sr = sampleRate;
    head.samples = (juce::int64) ((sd != nullptr ? sd->secondsBetween (0, pos) : pos * 15.0 / head.bpm) * sampleRate);
    head.playing = state == playing;

    // pracovne buffre stop
    if (sd != nullptr)
        for (const auto& td : sd->tracks)
        {
            if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
            slots[(size_t) td.slot]->channel = td.channel;
            auto& b = slots[(size_t) td.slot]->buf;
            if (b.getNumSamples() < n) b.setSize (juce::jmax (2, b.getNumChannels()), n, false, true, true);
            for (int c = 0; c < b.getNumChannels(); ++c) juce::FloatVectorOperations::clear (b.getWritePointer (c), n);
        }

    if (sd != nullptr)
        for (const auto& gd : sd->groups)
        {
            if (! juce::isPositiveAndBelow (gd.slot, (int) slots.size())) continue;
            auto& b = slots[(size_t) gd.slot]->buf;
            if (b.getNumSamples() < n || b.getNumChannels() < 2) b.setSize (juce::jmax (2, b.getNumChannels()), juce::jmax (n, b.getNumSamples()), false, true, true);
            for (int c = 0; c < b.getNumChannels(); ++c) juce::FloatVectorOperations::clear (b.getWritePointer (c), n);
        }

    int startOffset = 0;
    if (state == counting && sd != nullptr)
    {
        const double total = countTotal.load() * beatSamples;
        for (int b = (int) std::ceil (countSamples / beatSamples); b * beatSamples < countSamples + n && b < countTotal.load(); ++b)
        {
            addClick ((int) (b * beatSamples - countSamples), b % 4 == 0 ? 0 : 1);
            countBeat = b;
        }
        if (countSamples + n >= total)
        {
            startOffset = juce::jlimit (0, n, (int) (total - countSamples));
            pos = startAfterCount;
            state = playing;
        }
        countSamples += n;
    }

    if (state == playing && sd != nullptr)
    {
        int s = startOffset;
        int guard = 0;
        while (s < n && ++guard < 8)
        {
            // pri zmenach tempa sa krok pocita z casu (integral tempa), v ramci bloku stale vzoriek na krok
            const double segEnd = sd->advance (pos, (n - s) / sampleRate);
            const double sps = segEnd > pos ? (n - s) / (segEnd - pos) : sampleRate * 15.0 / sd->tempoAt (pos);
            const bool loop = ! offline && sd->loopOn && sd->locR > sd->locL && pos < sd->locR;
            const double limit = offline ? 1.0e12 : (loop ? sd->locR : sd->endStep);
            if (segEnd >= limit)
            {
                const int len = juce::jlimit (0, n - s, (int) std::ceil (sd->secondsBetween (pos, limit) * sampleRate));
                renderSegment (*sd, pos, limit, s, len, sps);
                captureInput (s, len, pos);
                if (recActive.load() && ! loop) recStopped = true;   // koniec piesne ukonci nahravanie, v cykle sa nahrava dalej (takes)
                allNotesOff (s + len > 0 ? s + len - 1 : 0, false);
                s += len;
                pos = loop ? sd->locL : 0.0;
            }
            else
            {
                renderSegment (*sd, pos, segEnd, s, n - s, sps);
                captureInput (s, n - s, pos);
                pos = segEnd;
                s = n;
            }
        }
        if (! offline)
        {
            publicPos = pos;
            publicMs = juce::Time::getMillisecondCounterHiRes();
            publicSps = 4.0 * sd->tempoAt (pos) / 60.0;   // krokov (16-tin) za sekundu
        }
    }

    // master: stopy sa scitaju sem, potom efekty mastra
    auto& ms = *slots[(size_t) masterSlot];
    if (ms.buf.getNumSamples() < n || ms.buf.getNumChannels() < 2) ms.buf.setSize (juce::jmax (2, ms.buf.getNumChannels()), n, false, true, true);
    for (int c = 0; c < ms.buf.getNumChannels(); ++c) juce::FloatVectorOperations::clear (ms.buf.getWritePointer (c), n);
    float* mixL = ms.buf.getWritePointer (0);
    float* mixR = ms.buf.getWritePointer (1);

    float* auxL[2] {}; float* auxR[2] {};
    for (int k = 0; k < 2; ++k)
    {
        auto& as = *slots[(size_t) auxSlot[k]];
        if (as.buf.getNumSamples() < n || as.buf.getNumChannels() < 2) as.buf.setSize (juce::jmax (2, as.buf.getNumChannels()), n, false, true, true);
        for (int c = 0; c < as.buf.getNumChannels(); ++c) juce::FloatVectorOperations::clear (as.buf.getWritePointer (c), n);
        auxL[k] = as.buf.getWritePointer (0); auxR[k] = as.buf.getWritePointer (1);
    }

    // kluc pre sidechain: stopy oznacene ako kluc sa spracuju prve, ich signal (pred hlasitostou) ide do keyBuf
    if (keyBuf.getNumSamples() < n) keyBuf.setSize (2, n, false, false, true);
    keyBuf.clear (0, n);
    bool keyReady = false;
    auto setKey = [this, n, &keyReady, sd] (juce::AudioPluginInstance* f)
    {
        BuiltinFx::setSong (f, sd != nullptr ? sd->keyTonic : -1, sd != nullptr ? sd->keyScale : -1, sd != nullptr ? &sd->chords : nullptr);
        if (keyReady) BuiltinFx::setSidechain (f, keyBuf.getReadPointer (0), keyBuf.getReadPointer (1), n);
        else BuiltinFx::setSidechain (f, nullptr, nullptr, 0);
    };

    // stopy: odposluch vstupu, nastroj, efekty, hlasitost a panorama
    if (sd != nullptr)
    {
        order.clear();
        for (int i = 0; i < (int) sd->tracks.size(); ++i) if (sd->tracks[(size_t) i].isKey) order.push_back (i);
        const int numKeys = (int) order.size();
        for (int i = 0; i < (int) sd->tracks.size(); ++i) if (! sd->tracks[(size_t) i].isKey) order.push_back (i);
        for (int oi = 0; oi < (int) order.size(); ++oi)
        {
            if (oi == numKeys && numKeys > 0) keyReady = true;
            const auto& td = sd->tracks[(size_t) order[(size_t) oi]];
            if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
            auto& slot = *slots[(size_t) td.slot];
            juce::AudioBuffer<float> view (slot.buf.getArrayOfWritePointers(), slot.buf.getNumChannels(), n);

            if (td.isAudio && td.monitor && td.audible && curIn != nullptr)
            {
                const float* a = juce::isPositiveAndBelow (td.inA, curNumIn) ? curIn[td.inA] : nullptr;
                const float* b = td.inB < 0 ? a : (juce::isPositiveAndBelow (td.inB, curNumIn) ? curIn[td.inB] : nullptr);
                if (a != nullptr) view.addFrom (0, 0, a, n);
                if (b != nullptr) view.addFrom (1, 0, b, n);
            }

            // automatizacia: parametre pluginov a hlasitost / panorama
            float gL = td.gainL, gR = td.gainR;
            if (! td.autos.empty())
            {
                float vol = td.vol, pan = td.pan;
                bool mix = false;
                for (const auto& al : td.autos)
                {
                    const float v = juce::jlimit (0.0f, 1.0f, al.valueAt (pos));
                    if (al.type == 0) { vol = v; mix = true; }
                    else if (al.type == 1) { pan = v * 2.0f - 1.0f; mix = true; }
                    else
                    {
                        juce::AudioPluginInstance* pl = al.which == 0 ? slot.plugin.get()
                                                      : (juce::isPositiveAndBelow (al.which - 1, numFx) ? slot.fx[(size_t) al.which - 1].get() : nullptr);
                        if (pl == nullptr) continue;
                        const auto& params = pl->getParameters();
                        if (! juce::isPositiveAndBelow (al.param, params.size())) continue;
                        const int key = al.which * 100000 + al.param;
                        auto it = std::find_if (slot.lastParams.begin(), slot.lastParams.end(), [key] (const auto& x) { return x.first == key; });
                        if (it != slot.lastParams.end() && std::abs (it->second - v) < 1.0e-4f) continue;
                        params[al.param]->setValue (v);
                        if (it != slot.lastParams.end()) it->second = v;
                        else if (slot.lastParams.size() < 64) slot.lastParams.push_back ({ key, v });
                    }
                }
                if (mix)
                {
                    const float g = vol * vol;
                    gL = g * juce::jmin (1.0f, 1.0f - pan);
                    gR = g * juce::jmin (1.0f, 1.0f + pan);
                }
            }

            bool any = false;
            if (td.frozen) { slot.midi.clear(); any = true; }   // zmrazena: len nahrane audio, setri procesor
            else {
            if (slot.midiFx != nullptr) { CrashGuard::Busy cg (slot.midiFx.get()); slot.midiFx->processBlock (view, slot.midi); }
            // MIDI stopy (po arpeggiatore, pred nastrojom) pre efekty, ktore ho chcu (Korgus Vocoder: Interny syntezator)
            trackMidi.clear();
            for (int k = 0; k < numFx; ++k)
                if (auto& f = slot.fx[(size_t) k]; f != nullptr && f->acceptsMidi()) { trackMidi.addEvents (slot.midi, 0, n, 0); break; }
            if (slot.plugin != nullptr)
            {
                // nastroj so vstupom pre hlas (Inspiria - vocoder): kluc (spev) do sidechain kanalov
                if (slot.plugin->getBusCount (true) > 1)
                    if (auto* b1 = slot.plugin->getBus (true, 1); b1 != nullptr && b1->isEnabled())
                        for (int c = 0; c < b1->getNumberOfChannels(); ++c)
                        {
                            const int idx = slot.plugin->getChannelIndexInProcessBlockBuffer (true, 1, c);
                            if (juce::isPositiveAndBelow (idx, view.getNumChannels()))
                            {
                                if (keyReady) juce::FloatVectorOperations::copy (view.getWritePointer (idx), keyBuf.getReadPointer (juce::jmin (c, 1)), n);
                                else juce::FloatVectorOperations::clear (view.getWritePointer (idx), n);
                            }
                        }
                CrashGuard::Busy cg (slot.plugin.get()); slot.plugin->processBlock (view, slot.midi); any = true;
            }
            if (! slot.voices.empty())
            {
                slot.renderVoices (view, n); any = true;
                // bicie: mekke obmedzenie spicky, aby sa viac sucasnych uderov (alebo hlasne vlastne samply) neskreslilo
                if (slot.plugin == nullptr)
                    for (int c = 0; c < juce::jmin (2, view.getNumChannels()); ++c)
                    {
                        float* d = view.getWritePointer (c);
                        for (int i = 0; i < n; ++i)
                        {
                            const float a = std::abs (d[i]);
                            if (a > 0.7f) d[i] = std::copysign (0.7f + 0.3f * std::tanh ((a - 0.7f) / 0.3f), d[i]);
                        }
                    }
            }
            for (int k = 0; k < numFx; ++k)
                if (auto& f = slot.fx[(size_t) k]; f != nullptr && ! (td.fxOff & (1u << k)))
                {
                    CrashGuard::Busy cg (f.get());
                    setKey (f.get());
                    fxMidi.clear();
                    if (f->acceptsMidi()) fxMidi.addEvents (trackMidi, 0, n, 0);
                    f->processBlock (view, fxMidi);
                    any = true;
                }
            }
            if (! any && ! td.isAudio) continue;
            if (td.isKey)
                for (int c = 0; c < 2; ++c)
                    keyBuf.addFrom (c, 0, view, juce::jmin (c, view.getNumChannels() - 1), 0, n);

            const int chans = view.getNumChannels();
            const float* srcL = view.getReadPointer (0);
            const float* srcR = view.getReadPointer (chans > 1 ? 1 : 0);
            float* dstL = mixL; float* dstR = mixR;
            if (juce::isPositiveAndBelow (td.group, (int) sd->groups.size()))
                if (const int gs = sd->groups[(size_t) td.group].slot; juce::isPositiveAndBelow (gs, (int) slots.size()) && slots[(size_t) gs]->buf.getNumChannels() >= 2)
                { dstL = slots[(size_t) gs]->buf.getWritePointer (0); dstR = slots[(size_t) gs]->buf.getWritePointer (1); }
            if (slot.curL < 0 || (std::abs (slot.curL - gL) < 1.0e-5f && std::abs (slot.curR - gR) < 1.0e-5f))
            {
                juce::FloatVectorOperations::addWithMultiply (dstL, srcL, gL, n);
                juce::FloatVectorOperations::addWithMultiply (dstR, srcR, gR, n);
            }
            else
            {
                // plynuly prechod hlasitosti cez blok (bez prasknutia)
                const float dL = (gL - slot.curL) / (float) n, dR = (gR - slot.curR) / (float) n;
                float a = slot.curL, b = slot.curR;
                for (int i = 0; i < n; ++i) { a += dL; b += dR; dstL[i] += srcL[i] * a; dstR[i] += srcR[i] * b; }
            }
            slot.curL = gL; slot.curR = gR;
            if (forced == nullptr)
            {
                const auto rl = juce::FloatVectorOperations::findMinAndMax (srcL, n), rr = juce::FloatVectorOperations::findMinAndMax (srcR, n);
                meterPut (td.slot, juce::jmax (-rl.getStart(), rl.getEnd()) * gL, juce::jmax (-rr.getStart(), rr.getEnd()) * gR);
            }
            for (int k = 0; k < 2; ++k)
                if (td.send[k] > 0.0f)
                {
                    juce::FloatVectorOperations::addWithMultiply (auxL[k], srcL, gL * td.send[k], n);
                    juce::FloatVectorOperations::addWithMultiply (auxR[k], srcR, gR * td.send[k], n);
                }
        }
    }

    // skupinove stopy: efekty, hlasitost a panorama -> master (a posielanie do FX 1 / FX 2)
    if (sd != nullptr)
        for (const auto& gd : sd->groups)
        {
            if (! juce::isPositiveAndBelow (gd.slot, (int) slots.size())) continue;
            auto& gs = *slots[(size_t) gd.slot];
            if (gs.buf.getNumChannels() < 2) continue;
            juce::AudioBuffer<float> view (gs.buf.getArrayOfWritePointers(), gs.buf.getNumChannels(), n);
            for (int q = 0; q < numFx; ++q)
                if (auto& f = gs.fx[(size_t) q]; f != nullptr && ! (gd.fxOff & (1u << q))) { CrashGuard::Busy cg (f.get()); setKey (f.get()); fxMidi.clear(); f->processBlock (view, fxMidi); }
            const float gL = gd.audible ? gd.gainL : 0.0f, gR = gd.audible ? gd.gainR : 0.0f;
            const float* srcL = view.getReadPointer (0); const float* srcR = view.getReadPointer (1);
            juce::FloatVectorOperations::addWithMultiply (mixL, srcL, gL, n);
            juce::FloatVectorOperations::addWithMultiply (mixR, srcR, gR, n);
            for (int k = 0; k < 2; ++k)
                if (gd.send[k] > 0.0f)
                {
                    juce::FloatVectorOperations::addWithMultiply (auxL[k], srcL, gL * gd.send[k], n);
                    juce::FloatVectorOperations::addWithMultiply (auxR[k], srcR, gR * gd.send[k], n);
                }
            if (forced == nullptr)
            {
                const auto rl = juce::FloatVectorOperations::findMinAndMax (srcL, n), rr = juce::FloatVectorOperations::findMinAndMax (srcR, n);
                meterPut (gd.slot, juce::jmax (-rl.getStart(), rl.getEnd()) * gL, juce::jmax (-rr.getStart(), rr.getEnd()) * gR);
            }
        }

    // spolocne efekty FX 1 / FX 2 -> master
    for (int k = 0; k < 2; ++k)
    {
        auto& as = *slots[(size_t) auxSlot[k]];
        const unsigned off = sd != nullptr ? sd->auxFxOff[k] : 0u;
        bool anyFx = false;
        for (int q = 0; q < numFx; ++q) anyFx = anyFx || (as.fx[(size_t) q] != nullptr && ! (off & (1u << q)));
        if (! anyFx) continue;
        juce::AudioBuffer<float> view (as.buf.getArrayOfWritePointers(), as.buf.getNumChannels(), n);
        for (int q = 0; q < numFx; ++q)
            if (auto& f = as.fx[(size_t) q]; f != nullptr && ! (off & (1u << q))) { CrashGuard::Busy cg (f.get()); setKey (f.get()); fxMidi.clear(); f->processBlock (view, fxMidi); }
        const float g = sd != nullptr ? sd->auxGain[k] : 1.0f;
        juce::FloatVectorOperations::addWithMultiply (mixL, auxL[k], g, n);
        juce::FloatVectorOperations::addWithMultiply (mixR, auxR[k], g, n);
        if (forced == nullptr)
        {
            const auto rl = juce::FloatVectorOperations::findMinAndMax (auxL[k], n), rr = juce::FloatVectorOperations::findMinAndMax (auxR[k], n);
            meterPut (auxSlot[k], juce::jmax (-rl.getStart(), rl.getEnd()) * g, juce::jmax (-rr.getStart(), rr.getEnd()) * g);
        }
    }

    {
        juce::AudioBuffer<float> view (ms.buf.getArrayOfWritePointers(), ms.buf.getNumChannels(), n);
        const bool bypass = forced != nullptr && bypassMaster.load();
        if (! bypass)
            for (int q = 0; q < numFx; ++q)
                if (auto& f = ms.fx[(size_t) q]; f != nullptr && ! ((sd != nullptr ? sd->masterFxOff : 0u) & (1u << q))) { CrashGuard::Busy cg (f.get()); setKey (f.get()); fxMidi.clear(); f->processBlock (view, fxMidi); }
        const float mg = bypass ? 1.0f : (sd != nullptr ? sd->masterGain : 1.0f);
        if (forced == nullptr)
        {
            const auto rl = juce::FloatVectorOperations::findMinAndMax (mixL, n), rr = juce::FloatVectorOperations::findMinAndMax (mixR, n);
            meterPut (masterSlot, juce::jmax (-rl.getStart(), rl.getEnd()) * mg, juce::jmax (-rr.getStart(), rr.getEnd()) * mg);
        }
        if (curNumOut == 1 && out[0] != nullptr)
        {
            juce::FloatVectorOperations::addWithMultiply (out[0], mixL, mg * 0.5f, n);
            juce::FloatVectorOperations::addWithMultiply (out[0], mixR, mg * 0.5f, n);
        }
        else
        {
            if (curNumOut > 0 && out[0] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[0], mixL, mg, n);
            if (curNumOut > 1 && out[1] != nullptr) juce::FloatVectorOperations::addWithMultiply (out[1], mixR, mg, n);
        }
    }

    // referencna piesen namiesto mixu (len pri realnom prehravani)
    if (forced == nullptr && refOn.load() && head.playing)
    {
        const juce::SpinLock::ScopedTryLockType l (refLock);
        if (l.isLocked() && refBuf != nullptr && sd != nullptr)
        {
            const auto start = (juce::int64) (sd->secondsBetween (0.0, head.ppq * 4.0) * sampleRate);
            const int rch = refBuf->getNumChannels();
            for (int c = 0; c < curNumOut; ++c)
            {
                if (out[c] == nullptr) continue;
                juce::FloatVectorOperations::clear (out[c], n);
                const float* src = refBuf->getReadPointer (juce::jmin (c, rch - 1));
                for (int i = 0; i < n; ++i)
                {
                    const auto k = start + i;
                    if (k >= 0 && k < refBuf->getNumSamples()) out[c][i] = src[k] * refGain;
                }
            }
            if (curNumOut == 1 && rch > 1 && out[0] != nullptr)
                for (int i = 0; i < n; ++i) { const auto k = start + i; if (k >= 0 && k < refBuf->getNumSamples()) out[0][i] = 0.5f * (refBuf->getSample (0, (int) k) + refBuf->getSample (1, (int) k)) * refGain; }
        }
    }

    renderClicks (out, curNumOut, n);

    // po exporte: plynuly nabeh vystupu (ziadna rana zo stavov pluginov)
    if (forced == nullptr)
        if (int left = postFade.load (std::memory_order_relaxed); left > 0)
        {
            const int tot = juce::jmax (1, (int) (sampleRate * 0.4));
            for (int i = 0; i < n; ++i)
            {
                const float g = left > 0 ? 1.0f - (float) left / (float) tot : 1.0f;
                for (int c = 0; c < curNumOut; ++c) if (out[c] != nullptr) out[c][i] *= g * g;
                if (left > 0) --left;
            }
            postFade.store (left, std::memory_order_relaxed);
        }
}

//==============================================================================
bool Engine::renderOffline (const SongData& sd, double endStep, double tailSec, juce::AudioFormatWriter& w,
                            const std::function<bool (double)>& progress)
{
    double savedPos = 0;
    bool prepass = false;     // Korgus Master na mastri: predbezny prechod, v ktorom sa nauci hlasitost
    int latency = 0;          // oneskorenie efektov mastra (vzorky) - zo suboru sa odstrihne
    // efekty mastra, ktore sa pri tomto exporte realne pouziju (volat pod slotLock)
    auto forMasterFx = [this, &sd] (bool onlyActive, const std::function<void (juce::AudioPluginInstance&)>& fn)
    {
        if (onlyActive && bypassMaster.load()) return;
        auto& ms = *slots[(size_t) masterSlot];
        for (int q = 0; q < numFx; ++q)
            if (ms.fx[(size_t) q] != nullptr && (! onlyActive || ! (sd.masterFxOff & (1u << q)))) fn (*ms.fx[(size_t) q]);
    };
    {
        const juce::ScopedLock sl (slotLock);
        {
            const juce::SpinLock::ScopedLockType l (cmdLock);
            cmds.clear();
        }
        savedPos = pos;
        offline = true;     // realne audio odteraz len ticho
        recArmed = false;
        for (auto& s : slots) { s->active.clear(); s->voices.clear(); }
        forEachPlugin ([] (juce::AudioPluginInstance& p) { p.setNonRealtime (true); p.reset(); });
        clickLeft = clickDelay = 0;
        state = playing;
        pos = juce::jlimit (0.0, juce::jmax (0.0, endStep - 1.0), exportStart.load());
        forMasterFx (true, [&] (juce::AudioPluginInstance& p)
        {
            prepass = prepass || BuiltinFx::wantsExportPrepass (&p);
            latency += juce::jmax (0, p.getLatencySamples());
        });
    }

    const int bs = juce::jmax (64, blockSize);
    juce::AudioBuffer<float> buf (2, bs);
    const double startStep = pos;
    const juce::int64 total = (juce::int64) ((sd.secondsBetween (startStep, endStep) + tailSec) * sampleRate) + latency;
    bool ok = true;

    // jeden prechod celou piesnou; write = false: zvuk sa zahodi (ucenie hlasitosti)
    auto runPass = [&] (bool write, double p0, double p1)
    {
        juce::int64 skip = write ? latency : 0;
        for (juce::int64 done = 0; done < total; done += bs)
        {
            const int n = (int) juce::jmin ((juce::int64) bs, total - done);
            buf.clear();
            {
                const juce::ScopedLock sl (slotLock);
                process (nullptr, 0, buf.getArrayOfWritePointers(), 2, n, &sd);
            }
            if (write)
            {
                const int first = (int) juce::jmin ((juce::int64) n, skip);
                skip -= first;
                if (n > first) w.writeFromAudioSampleBuffer (buf, first, n - first);
            }
            if (progress && ! progress (p0 + (p1 - p0) * (double) done / (double) total)) return false;
        }
        return true;
    };

    if (prepass)
    {
        {
            const juce::ScopedLock sl (slotLock);
            forMasterFx (true, [] (juce::AudioPluginInstance& p) { BuiltinFx::setExportPhase (&p, 1); });
        }
        ok = runPass (false, 0.0, 0.5);
        if (ok)
        {
            // zaciatok odznova: stav efektov sa vynuluje, nauceny zisk Korgus Master zostane a zmrzne
            const juce::ScopedLock sl (slotLock);
            {
                const juce::SpinLock::ScopedLockType l (cmdLock);
                cmds.clear();
            }
            for (auto& s : slots) { s->active.clear(); s->voices.clear(); s->lastParams.clear(); }
            forEachPlugin ([] (juce::AudioPluginInstance& p) { p.reset(); });
            clickLeft = clickDelay = 0;
            state = playing;
            pos = startStep;
            forMasterFx (true, [] (juce::AudioPluginInstance& p) { BuiltinFx::setExportPhase (&p, 2); });
        }
        if (ok) ok = runPass (true, 0.5, 1.0);
    }
    else ok = runPass (true, 0.0, 1.0);

    {
        const juce::ScopedLock sl (slotLock);
        forMasterFx (false, [] (juce::AudioPluginInstance& p) { BuiltinFx::setExportPhase (&p, 0); });
        state = stopped;
        pos = savedPos;
        for (auto& s : slots) { s->active.clear(); s->voices.clear(); }
        // vsetky noty vypnut (CC 123/120), aby po exporte nic nedoznievalo
        forEachPlugin ([] (juce::AudioPluginInstance& p)
        {
            juce::MidiBuffer mb;
            for (int ch = 1; ch <= 16; ++ch) { mb.addEvent (juce::MidiMessage::allNotesOff (ch), 0); mb.addEvent (juce::MidiMessage::allSoundOff (ch), 0); }
            juce::AudioBuffer<float> tmp (juce::jmax (2, juce::jmax (p.getTotalNumInputChannels(), p.getTotalNumOutputChannels())), 64);
            tmp.clear(); p.processBlock (tmp, mb);
            p.setNonRealtime (false); p.reset();
        });
        offline = false;
        exportStart = 0.0;
        postFade = (int) (sampleRate * 0.4);
    }
    return ok;
}
