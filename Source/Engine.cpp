#include "Engine.h"

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
    if (voices.size() >= 64) voices.erase (voices.begin());
    voices.push_back ({ std::move (b), pos, gain, lane, -1, 64 });
}

void Engine::Slot::renderVoices (juce::AudioBuffer<float>& dst, int n)
{
    for (size_t k = 0; k < voices.size();)
    {
        auto& v = voices[k];
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

void Engine::Slot::prepare (double sr, int bs)
{
    preparePlugin (plugin.get(), sr, bs);
    for (auto& f : fx) preparePlugin (f.get(), sr, bs);
    buf.setSize (numChans(), juce::jmax (bs, 16), false, true, false);
    midi.ensureSize (2048);
    active.reserve (128);
}

Engine::Engine()
{
    cmds.reserve (64);
    trigs.reserve (64);
    fxMidi.ensureSize (64);
    masterSlot = getOrCreateSlot ("__master");
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

void Engine::setSong (std::unique_ptr<SongData> s)
{
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
    }
    if (m.isNoteOnOrOff() || (m.isController() && m.getControllerNumber() == 64))
    {
        const int pitch = m.isController() ? -64 : m.getNoteNumber();
        const float vel = m.isController() ? (float) m.getControllerValue() / 127.0f : m.getFloatVelocity();
        const bool on = m.isController() ? m.getControllerValue() >= 64 : m.isNoteOn();
        const double p = publicPos.load();
        juce::MessageManager::callAsync ([this, pitch, vel, on, p] { if (onMidiIn) onMidiIn (pitch, vel, on, p); });
    }
}

//==============================================================================
void Engine::audioDeviceAboutToStart (juce::AudioIODevice* dev)
{
    const juce::ScopedLock sl (slotLock);
    sampleRate = dev->getCurrentSampleRate();
    blockSize = dev->getCurrentBufferSizeSamples();
    for (auto& s : slots) s->prepare (sampleRate, blockSize);
}

void Engine::audioDeviceStopped()
{
    const juce::ScopedLock sl (slotLock);
    forEachPlugin ([] (juce::AudioPluginInstance& p) { p.releaseResources(); });
}

void Engine::addClick (int offset, bool accent)
{
    clickDelay = juce::jmax (0, offset);
    clickLen = clickLeft = (int) (sampleRate * 0.05);
    clickPhase = 0;
    clickInc = juce::MathConstants<double>::twoPi * (accent ? 1600.0 : 1050.0) / sampleRate;
}

void Engine::renderClicks (float* const* out, int numOut, int n)
{
    for (int i = 0; i < n && (clickLeft > 0 || clickDelay > 0); ++i)
    {
        if (clickDelay > 0) { --clickDelay; continue; }
        const float env = (float) clickLeft / (float) juce::jmax (1, clickLen);
        const float s = (float) std::sin (clickPhase) * 0.3f * env * env;
        clickPhase += clickInc;
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
        const double into = (p0 - c.start) * sps;            // vzoriek od zaciatku klipu
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
                if (fiS > 0 && t < fiS) g *= juce::jmax (0.0, t / fiS);
                if (foS > 0 && t > durS - foS) g *= juce::jmax (0.0, (durS - t) / foS);
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
    if (sd.click && ! offline)
        for (double b = std::ceil (p0 / 4.0) * 4.0; b < p1; b += 4.0)
            addClick (off + (int) ((b - p0) * sps), std::fmod (b, 16.0) < 0.5);

    for (const auto& td : sd.tracks)
    {
        if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
        auto& s = *slots[(size_t) td.slot];

        if (td.isAudio)
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
                    slots[(size_t) t.slot]->addVoice (t.buf, 0, t.gain, t.lane);
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
                        beatSamples = 60.0 / song.load()->tempo * sampleRate;
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

    head.bpm = sd != nullptr && sd->tempo > 0 ? sd->tempo : 120.0;
    head.ppq = pos / 4.0;
    head.sr = sampleRate;
    head.samples = (juce::int64) (pos * sampleRate * 60.0 / (head.bpm * 4.0));
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

    int startOffset = 0;
    if (state == counting && sd != nullptr)
    {
        const double total = countTotal.load() * beatSamples;
        for (int b = (int) std::ceil (countSamples / beatSamples); b * beatSamples < countSamples + n && b < countTotal.load(); ++b)
        {
            addClick ((int) (b * beatSamples - countSamples), b % 4 == 0);
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
        const double sps = sampleRate * 60.0 / (sd->tempo * 4.0);   // vzoriek na krok
        int s = startOffset;
        int guard = 0;
        while (s < n && ++guard < 8)
        {
            const double segEnd = pos + (n - s) / sps;
            const bool loop = ! offline && sd->loopOn && sd->locR > sd->locL && pos < sd->locR;
            const double limit = offline ? 1.0e12 : (loop ? sd->locR : sd->endStep);
            if (segEnd >= limit)
            {
                const int len = juce::jlimit (0, n - s, (int) std::ceil ((limit - pos) * sps));
                renderSegment (*sd, pos, limit, s, len, sps);
                captureInput (s, len, pos);
                if (recActive.load()) recStopped = true;   // nahravanie konci pri skoku spat
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
        if (! offline) publicPos = pos;
    }

    // master: stopy sa scitaju sem, potom efekty mastra
    auto& ms = *slots[(size_t) masterSlot];
    if (ms.buf.getNumSamples() < n || ms.buf.getNumChannels() < 2) ms.buf.setSize (juce::jmax (2, ms.buf.getNumChannels()), n, false, true, true);
    for (int c = 0; c < ms.buf.getNumChannels(); ++c) juce::FloatVectorOperations::clear (ms.buf.getWritePointer (c), n);
    float* mixL = ms.buf.getWritePointer (0);
    float* mixR = ms.buf.getWritePointer (1);

    // stopy: odposluch vstupu, nastroj, efekty, hlasitost a panorama
    if (sd != nullptr)
    {
        for (const auto& td : sd->tracks)
        {
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
            if (slot.plugin != nullptr) { slot.plugin->processBlock (view, slot.midi); any = true; }
            if (! slot.voices.empty()) { slot.renderVoices (view, n); any = true; }
            for (auto& f : slot.fx)
                if (f != nullptr) { fxMidi.clear(); f->processBlock (view, fxMidi); any = true; }
            if (! any && ! td.isAudio) continue;

            const int chans = view.getNumChannels();
            const float* srcL = view.getReadPointer (0);
            const float* srcR = view.getReadPointer (chans > 1 ? 1 : 0);
            if (slot.curL < 0 || (std::abs (slot.curL - gL) < 1.0e-5f && std::abs (slot.curR - gR) < 1.0e-5f))
            {
                juce::FloatVectorOperations::addWithMultiply (mixL, srcL, gL, n);
                juce::FloatVectorOperations::addWithMultiply (mixR, srcR, gR, n);
            }
            else
            {
                // plynuly prechod hlasitosti cez blok (bez prasknutia)
                const float dL = (gL - slot.curL) / (float) n, dR = (gR - slot.curR) / (float) n;
                float a = slot.curL, b = slot.curR;
                for (int i = 0; i < n; ++i) { a += dL; b += dR; mixL[i] += srcL[i] * a; mixR[i] += srcR[i] * b; }
            }
            slot.curL = gL; slot.curR = gR;
        }
    }

    {
        juce::AudioBuffer<float> view (ms.buf.getArrayOfWritePointers(), ms.buf.getNumChannels(), n);
        const bool bypass = forced != nullptr && bypassMaster.load();
        if (! bypass)
            for (auto& f : ms.fx)
                if (f != nullptr) { fxMidi.clear(); f->processBlock (view, fxMidi); }
        const float mg = bypass ? 1.0f : (sd != nullptr ? sd->masterGain : 1.0f);
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

    renderClicks (out, curNumOut, n);
}

//==============================================================================
bool Engine::renderOffline (const SongData& sd, double endStep, double tailSec, juce::AudioFormatWriter& w,
                            const std::function<bool (double)>& progress)
{
    double savedPos = 0;
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
        pos = 0;
    }

    const int bs = juce::jmax (64, blockSize);
    juce::AudioBuffer<float> buf (2, bs);
    const double sps = sampleRate * 60.0 / (sd.tempo * 4.0);
    const juce::int64 total = (juce::int64) (endStep * sps + tailSec * sampleRate);
    bool ok = true;
    for (juce::int64 done = 0; done < total; done += bs)
    {
        const int n = (int) juce::jmin ((juce::int64) bs, total - done);
        buf.clear();
        {
            const juce::ScopedLock sl (slotLock);
            process (nullptr, 0, buf.getArrayOfWritePointers(), 2, n, &sd);
        }
        w.writeFromAudioSampleBuffer (buf, 0, n);
        if (progress && ! progress ((double) done / (double) total)) { ok = false; break; }
    }

    {
        const juce::ScopedLock sl (slotLock);
        state = stopped;
        pos = savedPos;
        for (auto& s : slots) s->active.clear();
        forEachPlugin ([] (juce::AudioPluginInstance& p) { p.setNonRealtime (false); p.reset(); });
        offline = false;
    }
    return ok;
}
