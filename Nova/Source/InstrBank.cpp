#include "InstrBank.h"
#include <cstring>
#if JUCE_WINDOWS
 #include <windows.h>
#endif

namespace
{
    // poradie = hodnota parametra "inst"
    const char* kNames[InstrBank::numInstr][2] = {
        { "Koncertný klavír", "Concert grand" }, { "Pianíno", "Upright piano" }, { "Rhodes", "Rhodes" }, { "Čembalo", "Harpsichord" },
        { "Akustická gitara", "Acoustic guitar" }, { "Jazzová gitara", "Jazz guitar" }, { "Basgitara", "Bass guitar" }, { "Kontrabas", "Double bass" },
        { "Sláčiková sekcia", "String section" }, { "Husle", "Violin" }, { "Violončelá", "Cellos" }, { "Trúbka s dusítkom", "Muted trumpet" },
        { "Trúbka", "Trumpet" }, { "Tenor saxofón", "Tenor saxophone" }, { "Trombón", "Trombone" }, { "Flauta", "Flute" },
        { "Klarinet", "Clarinet" }, { "Vibrafón", "Vibraphone" }, { "Marimba", "Marimba" }, { "Zvonkohra", "Glockenspiel" },
        { "Pizzicato sláčiky", "Pizzicato strings" }, { "Ústna harmonika", "Harmonica" }, { "Harfa", "Harp" }, { "Píšťalový organ", "Pipe organ" }, { "Lesný roh", "French horn" }, { "Hoboj", "Oboe" }, { "Fagot", "Bassoon" }, { "Tuba", "Tuba" }, { "Sláčiky tremolo", "Tremolo strings" }, { "Sláčiky staccato", "Staccato strings" }, { "Kalimba", "Kalimba" }, { "Rúrkové zvony", "Tubular bells" }, { "Xylofón", "Xylophone" }, { "Elektrická gitara", "Electric guitar" }, { "Okarína", "Ocarina" }, { "Zobcová flauta", "Recorder" } };
}

juce::String InstrBank::name (int id, bool en)
{
    return juce::isPositiveAndBelow (id, numInstr) ? juce::String::fromUTF8 (kNames[id][en ? 1 : 0]) : juce::String();
}

juce::StringArray InstrBank::names (bool en)
{
    juce::StringArray a;
    for (int i = 0; i < numInstr; ++i) a.add (name (i, en));
    return a;
}

std::shared_ptr<InstrBank> InstrBank::get()
{
    static std::weak_ptr<InstrBank> shared;
    static std::mutex sm;
    std::lock_guard<std::mutex> l (sm);
    if (auto s = shared.lock()) return s;
    auto s = std::shared_ptr<InstrBank> (new InstrBank());
    shared = s;
    return s;
}

InstrBank::InstrBank()
{
    for (auto& s : state) s.store (0);
    if (! openPack()) { for (int i = 1; i < numInstr; ++i) state[(size_t) i].store (3); return; }
    th = std::thread ([this] { worker(); });
}

InstrBank::~InstrBank()
{
    quit = true;
    cv.notify_all();
    if (th.joinable()) th.join();
}

bool InstrBank::openPack()
{
#if JUCE_WINDOWS
    HMODULE mod = nullptr;
    if (! GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              (LPCWSTR) (void*) &InstrBank::name, &mod)) return false;
    HRSRC r = FindResourceW (mod, L"INSTR", MAKEINTRESOURCEW (10));   // RT_RCDATA
    if (r == nullptr) return false;
    HGLOBAL h = LoadResource (mod, r);
    pack = h != nullptr ? static_cast<const uint8_t*> (LockResource (h)) : nullptr;
    packSize = SizeofResource (mod, r);
#else
    juce::File f (juce::SystemStats::getEnvironmentVariable ("KORGUS_NOVA_INSTR", {}));
    if (! f.existsAsFile())
        f = juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("instr.knin");
    if (! f.existsAsFile() || ! f.loadFileAsData (packCopy)) return false;
    pack = static_cast<const uint8_t*> (packCopy.getData()); packSize = packCopy.getSize();
#endif
    return pack != nullptr && packSize >= 12 && std::memcmp (pack, "KNIN", 4) == 0;
}

void InstrBank::request (int id)
{
    if (id <= 0 || id >= numInstr) return;
    int expect = 0;
    if (state[(size_t) id].compare_exchange_strong (expect, 1)) cv.notify_one();
}

const InstrBank::Inst* InstrBank::find (int id) const
{
    if (! ready (id)) return nullptr;
    return inst[(size_t) id].get();
}

void InstrBank::worker()
{
    while (! quit)
    {
        int todo = -1;
        for (int i = 1; i < numInstr; ++i) if (state[(size_t) i].load() == 1) { todo = i; break; }
        if (todo < 0)
        {
            std::unique_lock<std::mutex> l (m);
            cv.wait_for (l, std::chrono::milliseconds (100));   // audio vlakno budi bez zamku – kratky limit
            continue;
        }
        loadOne (todo);
    }
}

void InstrBank::loadOne (int id)
{
    auto u32 = [this] (size_t at) { uint32_t v = 0; if (at + 4 <= packSize) std::memcpy (&v, pack + at, 4); return v; };
    const uint32_t count = u32 (8);
    size_t off = 0, size = 0;
    for (uint32_t k = 0; k < count; ++k)
        if ((int) u32 (12 + 12 * k) == id) { off = u32 (16 + 12 * k); size = u32 (20 + 12 * k); }
    if (off == 0 || off + size > packSize || size < 12 || std::memcmp (pack + off, "INST", 4) != 0) { state[(size_t) id].store (3); return; }
    const uint8_t* b = pack + off;
    auto I = std::make_unique<Inst>();
    I->sus = (b[4] & 1) != 0;
    I->nLayers = juce::jlimit (1, maxLayers, (int) b[5]);
    uint16_t n = 0; std::memcpy (&n, b + 6, 2);
    float g = 1.0f; std::memcpy (&g, b + 8, 4);
    I->gain = juce::jlimit (0.05f, 20.0f, g);   // zosilnenie nad ulozene vzorky (vyrovnana hlasitost nastrojov)
    size_t hdr = 12, dataAt = 12 + (size_t) n * 16;
    std::vector<int> layers;
    juce::OggVorbisAudioFormat ogg;
    I->samples.reserve (n);
    for (int k = 0; k < n && ! quit; ++k)
    {
        const int note = b[hdr], layer = b[hdr + 1]; const int8_t cents = (int8_t) b[hdr + 2];
        uint32_t ls, le, sz; std::memcpy (&ls, b + hdr + 4, 4); std::memcpy (&le, b + hdr + 8, 4); std::memcpy (&sz, b + hdr + 12, 4);
        hdr += 16;
        if (dataAt + sz > size) break;
        std::unique_ptr<juce::AudioFormatReader> r (ogg.createReaderFor (new juce::MemoryInputStream (b + dataAt, sz, false), true));
        dataAt += sz;
        if (r == nullptr || r->lengthInSamples < 8) continue;
        Sample s;
        s.root = note; s.tune = (float) cents * 0.01f; s.rate = r->sampleRate; s.length = (int) r->lengthInSamples;
        s.ch = r->numChannels > 1 ? 2 : 1;
        if (le > 0 && le <= (uint32_t) s.length && ls + 16 < le) { s.loopStart = (int) ls; s.loopEnd = (int) le; }
        juce::AudioBuffer<float> buf (s.ch, s.length);
        r->read (&buf, 0, s.length, 0, true, s.ch > 1);
        s.data.resize ((size_t) s.length * (size_t) s.ch);
        for (int c = 0; c < s.ch; ++c)
        {
            const float* src = buf.getReadPointer (c);
            for (int i = 0; i < s.length; ++i)
                s.data[(size_t) i * (size_t) s.ch + (size_t) c] = (int16_t) juce::jlimit (-32767.0f, 32767.0f, src[i] * 32767.0f);
        }
        I->samples.push_back (std::move (s));
        layers.push_back (layer);
    }
    if (I->samples.empty() || quit) { state[(size_t) id].store (3); return; }
    // kazdej note a vrstve najblizsia vzorka; ked vo vrstve chyba, vezme susednu vrstvu
    for (int nt = 0; nt < 128; ++nt)
        for (int l = 0; l < I->nLayers; ++l)
        {
            const Sample* best = nullptr; int bs = 1 << 30;
            for (size_t i = 0; i < I->samples.size(); ++i)
            {
                const int d = std::abs (I->samples[i].root - nt), dl = std::abs (layers[i] - l);
                const int score = d * 4 + dl * 13 + (I->samples[i].root < nt ? 1 : 0);   // radsej vzorka nad notou
                if (score < bs) { bs = score; best = &I->samples[i]; }
            }
            I->map[(size_t) nt][(size_t) l] = best;
        }
    inst[(size_t) id] = std::move (I);
    state[(size_t) id].store (2);
}
