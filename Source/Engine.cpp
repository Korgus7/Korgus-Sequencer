#include "Engine.h"

void Engine::Slot::prepare (double sr, int bs)
{
    if (plugin == nullptr) return;
    plugin->setRateAndBufferSizeDetails (sr, bs);
    plugin->prepareToPlay (sr, bs);
    const int ch = juce::jmax (2, plugin->getTotalNumInputChannels(), plugin->getTotalNumOutputChannels());
    buf.setSize (ch, juce::jmax (bs, 16), false, true, false);
    midi.ensureSize (2048);
    active.reserve (128);
}

Engine::Engine()
{
    cmds.reserve (64);
}

Engine::~Engine()
{
    const juce::ScopedLock sl (slotLock);
    for (auto& s : slots)
        if (s->plugin != nullptr) s->plugin->releaseResources();
}

//==============================================================================
int Engine::getOrCreateSlot (const juce::String& trackId)
{
    const juce::ScopedLock sl (slotLock);
    for (size_t i = 0; i < slots.size(); ++i)
        if (slots[i]->trackId == trackId) return (int) i;
    auto s = std::make_unique<Slot>();
    s->trackId = trackId;
    s->midi.ensureSize (2048);
    s->active.reserve (128);
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

void Engine::setTrackPlugin (int slot, std::unique_ptr<juce::AudioPluginInstance> p)
{
    if (p != nullptr)
    {
        // pripravime mimo zamku, aby audio nezaváhalo
        p->setRateAndBufferSizeDetails (sampleRate, blockSize);
        p->prepareToPlay (sampleRate, blockSize);
    }
    std::unique_ptr<juce::AudioPluginInstance> old;
    {
        const juce::ScopedLock sl (slotLock);
        if (! juce::isPositiveAndBelow (slot, (int) slots.size())) return;
        auto& s = *slots[(size_t) slot];
        old = std::move (s.plugin);
        s.plugin = std::move (p);
        s.active.clear();
        if (s.plugin != nullptr)
        {
            const int ch = juce::jmax (2, s.plugin->getTotalNumInputChannels(), s.plugin->getTotalNumOutputChannels());
            s.buf.setSize (ch, juce::jmax (blockSize, 16), false, true, false);
        }
    }
    if (old != nullptr) old->releaseResources();
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

void Engine::handleIncomingMidiMessage (juce::MidiInput*, const juce::MidiMessage& m)
{
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
    for (auto& s : slots)
        if (s->plugin != nullptr) s->plugin->releaseResources();
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
            s->midi.addEvent (juce::MidiMessage::noteOff (1, a.first), off);
        s->active.clear();
        if (hard)
        {
            s->midi.addEvent (juce::MidiMessage::controllerEvent (1, 64, 0), off);
            s->midi.addEvent (juce::MidiMessage::allNotesOff (1), off);
        }
    }
}

void Engine::renderSegment (const SongData& sd, double p0, double p1, int off, double sps)
{
    // metronom na dobach
    if (sd.click)
        for (double b = std::ceil (p0 / 4.0) * 4.0; b < p1; b += 4.0)
            addClick (off + (int) ((b - p0) * sps), std::fmod (b, 16.0) < 0.5);

    for (const auto& td : sd.tracks)
    {
        if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
        auto& s = *slots[(size_t) td.slot];

        // konce nôt
        for (size_t i = 0; i < s.active.size();)
        {
            if (s.active[i].second < p1)
            {
                const int o = off + juce::jmax (0, (int) ((s.active[i].second - p0) * sps));
                s.midi.addEvent (juce::MidiMessage::noteOff (1, s.active[i].first), o);
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
            s.midi.addEvent (juce::MidiMessage::noteOn (1, it->pitch, it->vel), o);
            if (s.active.size() < 512) s.active.push_back ({ it->pitch, it->end });
        }
    }
}

void Engine::audioDeviceIOCallbackWithContext (const float* const*, int, float* const* out, int numOut, int n,
                                               const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c)
        if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);

    const juce::ScopedTryLock sl (slotLock);
    if (! sl.isLocked()) return;

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
                    slots[(size_t) l.slot]->midi.addEvent (l.msg, 0);
            }
        };
        take (scope.startIndex1, scope.blockSize1);
        take (scope.startIndex2, scope.blockSize2);
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

    auto* sd = song.load();
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
            const bool loop = sd->loopOn && sd->locR > sd->locL && pos < sd->locR;
            const double limit = loop ? sd->locR : sd->endStep;
            if (segEnd >= limit)
            {
                const int len = juce::jlimit (0, n - s, (int) std::ceil ((limit - pos) * sps));
                renderSegment (*sd, pos, limit, s, sps);
                allNotesOff (s + len > 0 ? s + len - 1 : 0, false);
                s += len;
                pos = loop ? sd->locL : 0.0;
            }
            else
            {
                renderSegment (*sd, pos, segEnd, s, sps);
                pos = segEnd;
                s = n;
            }
        }
        publicPos = pos;
    }

    // zvuk z pluginov
    if (sd != nullptr)
    {
        for (const auto& td : sd->tracks)
        {
            if (! juce::isPositiveAndBelow (td.slot, (int) slots.size())) continue;
            auto& slot = *slots[(size_t) td.slot];
            if (slot.plugin == nullptr) continue;
            if (slot.buf.getNumSamples() < n)
                slot.buf.setSize (slot.buf.getNumChannels(), n, false, true, true);
            juce::AudioBuffer<float> view (slot.buf.getArrayOfWritePointers(), slot.buf.getNumChannels(), n);
            view.clear();
            slot.plugin->processBlock (view, slot.midi);
            const int chans = juce::jmin (2, view.getNumChannels());
            if (chans < 1) continue;
            if (numOut > 0 && out[0] != nullptr)
                juce::FloatVectorOperations::addWithMultiply (out[0], view.getReadPointer (0), td.gainL, n);
            if (numOut > 1 && out[1] != nullptr)
                juce::FloatVectorOperations::addWithMultiply (out[1], view.getReadPointer (chans > 1 ? 1 : 0), td.gainR, n);
        }
    }

    renderClicks (out, juce::jmin (numOut, 2), n);
}
