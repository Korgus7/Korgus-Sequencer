#include "SecondInput.h"
juce::String sk (const char* utf8);   // preklad textu (UiCommon)

static std::unique_ptr<juce::AudioIODeviceType> makeType()
{
   #if JUCE_WINDOWS
    return std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_WASAPI (juce::WASAPIDeviceMode::shared));
   #elif JUCE_ALSA
    return std::unique_ptr<juce::AudioIODeviceType> (juce::AudioIODeviceType::createAudioIODeviceType_ALSA());
   #else
    return {};
   #endif
}

juce::StringArray SecondInput::deviceNames()
{
    if (type == nullptr) type = makeType();
    if (type == nullptr) return {};
    type->scanForDevices();
    return type->getDeviceNames (true);
}

bool SecondInput::open (const juce::String& name, double preferredRate, juce::String& error)
{
    close();
    if (name.isEmpty()) return false;
    if (type == nullptr) type = makeType();
    if (type == nullptr) { error = sk ("Windows Audio nie je dostupné."); return false; }
    type->scanForDevices();
    std::unique_ptr<juce::AudioIODevice> d (type->createDevice ({}, name));
    if (d == nullptr) { error = sk ("Zariadenie sa nenašlo: ") + name; return false; }
    // vzorkovanie ako hlavna karta, ak ho zariadenie vie (menej prevzorkovania)
    double rate = 0;
    for (auto r : d->getAvailableSampleRates()) if (std::abs (r - preferredRate) < 1.0) rate = r;
    if (rate <= 0) rate = d->getAvailableSampleRates().isEmpty() ? 48000.0 : d->getAvailableSampleRates().getLast();
    juce::BigInteger ins; ins.setRange (0, juce::jmin (2, d->getInputChannelNames().size()), true);
    error = d->open (ins, {}, rate, d->getDefaultBufferSize());
    if (error.isNotEmpty()) return false;
    numCh = d->getActiveInputChannels().countNumberOfSetBits();
    devName = name;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        device = std::move (d);
        started = false;
    }
    device->start (this);
    return true;
}

void SecondInput::close()
{
    std::unique_ptr<juce::AudioIODevice> d;
    {
        const juce::SpinLock::ScopedLockType l (lock);
        active = false;
        d = std::move (device);
    }
    if (d != nullptr) { d->stop(); d->close(); }
    devName.clear();
}

juce::StringArray SecondInput::channelNames() const
{
    if (! isOpen()) return {};
    juce::String n = devName;
    if (n.length() > 22) n = n.substring (0, 22).trim();
    return { n + ": 1", n + ": 2" };
}

void SecondInput::audioDeviceAboutToStart (juce::AudioIODevice* d)
{
    srcRate = d->getCurrentSampleRate();
    srcBlock = juce::jmax (16, d->getCurrentBufferSizeSamples());
    srcLat = juce::jmax (0, d->getInputLatencyInSamples());
    wpos = 0;
    active = true;
}

void SecondInput::audioDeviceIOCallbackWithContext (const float* const* in, int numIn, float* const* out, int numOut, int n,
                                                    const juce::AudioIODeviceCallbackContext&)
{
    for (int c = 0; c < numOut; ++c) if (out[c] != nullptr) juce::FloatVectorOperations::clear (out[c], n);
    const float* a = numIn > 0 ? in[0] : nullptr;
    const float* b = numIn > 1 ? in[1] : a;
    auto w = wpos.load (std::memory_order_relaxed);
    for (int i = 0; i < n; ++i, ++w)
    {
        const auto k = (size_t) (w & kMask) * 2;
        ring[k] = a != nullptr ? a[i] : 0.0f;
        ring[k + 1] = b != nullptr ? b[i] : 0.0f;
    }
    wpos.store (w, std::memory_order_release);
}

bool SecondInput::pull (float* l, float* r, int n, double mainRate)
{
    if (mainRate <= 0) mainRate = 48000.0;
    if (mainRate != lastMainRate || n > lastBlock) { lastMainRate = mainRate; lastBlock = juce::jmax (lastBlock, n); started = false; }
    const int mainBlock = lastBlock;
    const juce::SpinLock::ScopedTryLockType tl (lock);
    if (! tl.isLocked() || ! active.load())
    {
        juce::FloatVectorOperations::clear (l, n); juce::FloatVectorOperations::clear (r, n);
        return false;
    }
    const double sr = srcRate.load(), base = sr / mainRate;
    // cielova naplnenost zasobnika (vo vzorkach zdroja): dva bloky zdroja + jeden blok hlavnej karty + rezerva
    const double target = 2.0 * srcBlock.load() + mainBlock * base + 64.0;
    const auto w = wpos.load (std::memory_order_acquire);
    if (! started)
    {
        if ((double) w < target + n * base + 4) { juce::FloatVectorOperations::clear (l, n); juce::FloatVectorOperations::clear (r, n); return true; }
        rpos = (double) w - target;
        fillAvg = target;
        started = true;
    }
    double avail = (double) w - rpos;
    // vypadok alebo pretecenie - zacat odznova (kratke ticho namiesto chrcania)
    if (avail < n * base * 1.01 + 4 || avail > kFrames * 0.75)
    {
        started = false;
        juce::FloatVectorOperations::clear (l, n); juce::FloatVectorOperations::clear (r, n);
        return true;
    }
    // jemne dorovnanie hodin: viac v zasobniku = citat o chlp rychlejsie
    fillAvg += (avail - fillAvg) * 0.02;
    const double err = (fillAvg - target) / juce::jmax (1.0, target);
    const double step = base * (1.0 + juce::jlimit (-0.004, 0.004, err * 0.004));
    latMain = (int) ((target + srcLat.load()) / sr * mainRate);

    for (int i = 0; i < n; ++i)
    {
        const auto k = (juce::int64) std::floor (rpos);
        const float f = (float) (rpos - (double) k);
        auto smp = [this] (juce::int64 idx, int c) { return ring[(size_t) (idx & kMask) * 2 + (size_t) c]; };
        for (int c = 0; c < 2; ++c)
        {
            const float x0 = smp (k - 1, c), x1 = smp (k, c), x2 = smp (k + 1, c), x3 = smp (k + 2, c);
            const float c1 = 0.5f * (x2 - x0), c2 = x0 - 2.5f * x1 + 2.0f * x2 - 0.5f * x3, c3 = 0.5f * (x3 - x0) + 1.5f * (x1 - x2);
            (c == 0 ? l : r)[i] = ((c3 * f + c2) * f + c1) * f + x1;
        }
        rpos += step;
    }
    return true;
}
