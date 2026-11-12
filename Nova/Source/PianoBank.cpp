#include "PianoBank.h"
#include <thread>
#include <mutex>
#include <cstring>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

std::shared_ptr<PianoBank> PianoBank::get()
{
    static std::weak_ptr<PianoBank> shared;
    static std::mutex m;
    std::lock_guard<std::mutex> l (m);
    if (auto s = shared.lock()) return s;
    auto s = std::shared_ptr<PianoBank> (new PianoBank());
    shared = s;
    std::weak_ptr<PianoBank> w = s;
    std::thread ([w] { if (auto b = w.lock()) b->load(); }).detach();
    return s;
}

const PianoBank::Sample* PianoBank::find (int note, int layer) const
{
    if (! isReady.load()) return nullptr;
    return map[(size_t) juce::jlimit (0, 127, note)][(size_t) juce::jlimit (0, numLayers - 1, layer)];
}

static bool readPack (juce::MemoryBlock& out)
{
#if JUCE_WINDOWS
    // balik je ako prostriedok (RCDATA "PIANO") priamo v subore pluginu
    HMODULE mod = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR) (void*) &readPack, &mod)) return false;
    HRSRC r = FindResourceW (mod, L"PIANO", MAKEINTRESOURCEW (10));   // 10 = RT_RCDATA
    if (r == nullptr) return false;
    HGLOBAL h = LoadResource (mod, r);
    const void* p = h != nullptr ? LockResource (h) : nullptr;
    const DWORD sz = SizeofResource (mod, r);
    if (p == nullptr || sz == 0) return false;
    out.replaceAll (p, (size_t) sz);
    return true;
#else
    // test na Linuxe: subor piano.knpk (premenna KORGUS_NOVA_PIANO alebo vedla programu)
    juce::File f (juce::SystemStats::getEnvironmentVariable ("KORGUS_NOVA_PIANO", {}));
    if (! f.existsAsFile())
        f = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("piano.knpk");
    return f.existsAsFile() && f.loadFileAsData (out);
#endif
}

void PianoBank::load()
{
    juce::MemoryBlock pack;
    if (! readPack (pack) || pack.getSize() < 12 || std::memcmp (pack.getData(), "KNPK", 4) != 0) { isFailed = true; return; }
    const auto* p = static_cast<const uint8_t*> (pack.getData());
    auto u32 = [p] (size_t at) { uint32_t v; std::memcpy (&v, p + at, 4); return v; };
    const uint32_t count = u32 (8);
    size_t hdr = 12, dataAt = 12 + (size_t) count * 6;
    juce::OggVorbisAudioFormat ogg;
    std::vector<Sample> loaded;
    std::vector<int> layers;
    loaded.reserve (count);
    for (uint32_t k = 0; k < count; ++k)
    {
        const int note = p[hdr], layer = p[hdr + 1];
        uint32_t sz; std::memcpy (&sz, p + hdr + 2, 4);
        hdr += 6;
        if (dataAt + sz > pack.getSize()) break;
        auto* in = new juce::MemoryInputStream (p + dataAt, sz, false);
        dataAt += sz;
        std::unique_ptr<juce::AudioFormatReader> r (ogg.createReaderFor (in, true));
        if (r == nullptr) continue;
        Sample s;
        s.root = note; s.rate = r->sampleRate; s.length = (int) r->lengthInSamples;
        juce::AudioBuffer<float> buf (2, s.length);
        r->read (&buf, 0, s.length, 0, true, true);
        s.data.resize ((size_t) s.length * 2);
        const float* L = buf.getReadPointer (0);
        const float* R = buf.getReadPointer (r->numChannels > 1 ? 1 : 0);
        for (int i = 0; i < s.length; ++i)
        {
            s.data[(size_t) i * 2]     = (int16_t) juce::jlimit (-32767.0f, 32767.0f, L[i] * 32767.0f);
            s.data[(size_t) i * 2 + 1] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, R[i] * 32767.0f);
        }
        loaded.push_back (std::move (s));
        layers.push_back (layer);
    }
    if (loaded.empty()) { isFailed = true; return; }
    samples = std::move (loaded);
    // kazdej note priradime najblizsiu vzorku v kazdej vrstve
    for (int n = 0; n < 128; ++n)
        for (int l = 0; l < numLayers; ++l)
        {
            const Sample* best = nullptr; int bd = 1000;
            for (size_t i = 0; i < samples.size(); ++i)
                if (layers[i] == l)
                {
                    const int d = std::abs (samples[i].root - n);
                    // pri rovnakej vzdialenosti radsej vzorka nad notou (posun dole znie prirodzenejsie)
                    if (d < bd || (d == bd && samples[i].root > n)) { bd = d; best = &samples[i]; }
                }
            map[(size_t) n][(size_t) l] = best;
        }
    isReady = true;
}
