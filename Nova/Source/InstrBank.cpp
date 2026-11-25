#include "InstrBank.h"
#include <cstring>
#include <juce_cryptography/juce_cryptography.h>
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
        { "Pizzicato sláčiky", "Pizzicato strings" }, { "Ústna harmonika", "Harmonica" }, { "Harfa", "Harp" }, { "Píšťalový organ", "Pipe organ" }, { "Lesný roh", "French horn" }, { "Hoboj", "Oboe" }, { "Fagot", "Bassoon" }, { "Tuba", "Tuba" }, { "Sláčiky tremolo", "Tremolo strings" }, { "Sláčiky staccato", "Staccato strings" }, { "Kalimba", "Kalimba" }, { "Rúrkové zvony", "Tubular bells" }, { "Xylofón", "Xylophone" }, { "Elektrická gitara", "Electric guitar" }, { "Okarína", "Ocarina" }, { "Zobcová flauta", "Recorder" },
        { "Pôvodný klavír", "Original piano" }, { "Viola", "Viola" }, { "Pikola", "Piccolo" }, { "Banjo", "Banjo" }, { "Tympany", "Timpani" },
        { "Bicia súprava", "Drum kit" }, { "Ručné bubny", "Hand drums" }, { "Perkusie", "Percussion" } };
    const char* kManifest = "https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/instr/instruments.json";
    const char* kBase = "https://raw.githubusercontent.com/Korgus7/Korgus-Download/main/instr/";
    juce::String sha256 (const juce::File& f) { juce::FileInputStream in (f); return in.openedOk() ? juce::SHA256 (in).toHexString() : juce::String(); }
}

juce::String InstrBank::name (int id, bool en)
{
    if (id == oldPiano) return en ? "Original piano (Salamander)" : juce::String::fromUTF8 ("Pôvodný klavír (Salamander)");
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
    for (auto& x : state) x.store (0);
    for (auto& x : prog) x.store (0.0f);
    for (auto& x : failAt) x.store (0);
    th = std::thread ([this] { worker(); });
}

InstrBank::~InstrBank()
{
    quit = true;
    cv.notify_all();
    if (th.joinable()) th.join();
}

juce::File InstrBank::userDir()
{
#if JUCE_WINDOWS
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Korgus").getChildFile ("Inspiria").getChildFile ("Instruments");
#else
    const auto e = juce::SystemStats::getEnvironmentVariable ("KORGUS_NOVA_INSTR_USER", {});
    if (e.isNotEmpty()) return juce::File (e);
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("Korgus").getChildFile ("Inspiria").getChildFile ("Instruments");
#endif
}

juce::File InstrBank::bundledDir()
{
#if JUCE_WINDOWS
    HMODULE mod = nullptr;
    wchar_t path[MAX_PATH * 2] = {};
    if (GetModuleHandleExW (GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT, (LPCWSTR) (void*) &InstrBank::name, &mod)
        && GetModuleFileNameW (mod, path, MAX_PATH * 2) > 0)
    {
        // plugin je jeden subor "Korgus Inspiria.vst3" (alebo DLL vnutri balika .vst3/Contents/x86_64-win)
        auto f = juce::File (juce::String (path)).getParentDirectory();
        for (int k = 0; k < 4; ++k, f = f.getParentDirectory())
            if (f.getChildFile ("Inspiria Instruments").isDirectory()) return f.getChildFile ("Inspiria Instruments");
    }
    return {};
#else
    juce::File d (juce::SystemStats::getEnvironmentVariable ("KORGUS_NOVA_INSTR", {}));
    if (d.isDirectory()) return d;
    return juce::File::getSpecialLocation (juce::File::currentExecutableFile).getSiblingFile ("instr");
#endif
}

void InstrBank::request (int id)
{
    if (id <= 0 || id >= numInstr || id == oldPiano) return;
    int expect = 0;
    if (state[(size_t) id].compare_exchange_strong (expect, 1)) { cv.notify_one(); return; }
    // po chybe (napr. bez internetu) skusit znova najskor o 20 s
    expect = 3;
    if (juce::Time::getMillisecondCounter() - failAt[(size_t) id].load() > 20000 && state[(size_t) id].compare_exchange_strong (expect, 1)) cv.notify_one();
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
            // ked nic necaka: raz za relaciu skontrolovat, ci nie su nove verzie pouzitych nastrojov (stiahnu sa na dalsie spustenie)
            if (! updatesChecked) { bool any = false; for (bool u : used) any = any || u; if (any) { updatesChecked = true; checkUpdates(); continue; } }
            std::unique_lock<std::mutex> l (m);
            cv.wait_for (l, std::chrono::milliseconds (100));   // audio vlakno budi bez zamku – kratky limit
            continue;
        }
        loadOne (todo);
    }
}

bool InstrBank::fetchManifest()
{
    if (manifestState == 1) return true;
    juce::URL u (juce::String (kManifest) + "?t=" + juce::String (juce::Time::currentTimeMillis()));
    auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                       .withConnectionTimeoutMs (8000).withExtraHeaders ("User-Agent: KorgusInspiria\r\nCache-Control: no-cache"));
    if (in == nullptr) { manifestState = 2; return false; }
    const auto j = juce::JSON::parse (in->readEntireStreamAsString());
    if (const auto* a = j["files"].getArray())
        for (const auto& e : *a)
        {
            MEntry me; me.name = e["name"].toString(); me.sha = e["sha256"].toString(); me.size = (juce::int64) e["size"];
            if (me.name.isNotEmpty()) manifest[(int) e["id"]] = me;
        }
    manifestState = manifest.empty() ? 2 : 1;
    return manifestState == 1;
}

bool InstrBank::download (int id, juce::File& out)
{
    if (! fetchManifest()) return false;
    auto it = manifest.find (id);
    if (it == manifest.end()) return false;
    const auto& me = it->second;
    const auto dir = userDir();
    dir.createDirectory();
    const auto tmp = dir.getChildFile (me.name + ".part");
    juce::URL u (juce::String (kBase) + juce::URL::addEscapeChars (me.name, false) + "?t=" + juce::String (juce::Time::currentTimeMillis()));
    auto in = u.createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                       .withConnectionTimeoutMs (10000).withExtraHeaders ("User-Agent: KorgusInspiria"));
    if (in == nullptr) return false;
    {
        tmp.deleteFile();
        juce::FileOutputStream os (tmp);
        if (! os.openedOk()) return false;
        juce::HeapBlock<char> buf (65536);
        juce::int64 got = 0;
        while (! quit)
        {
            const int n = in->read (buf.get(), 65536);
            if (n <= 0) break;
            os.write (buf.get(), (size_t) n); got += n;
            if (me.size > 0) prog[(size_t) id].store ((float) juce::jlimit (0.0, 1.0, (double) got / (double) me.size));
        }
        os.flush();
    }
    if (quit || (me.size > 0 && tmp.getSize() != me.size) || (me.sha.isNotEmpty() && sha256 (tmp) != me.sha)) { tmp.deleteFile(); return false; }
    out = dir.getChildFile (me.name);
    out.deleteFile();
    return tmp.moveFileTo (out);
}

void InstrBank::checkUpdates()
{
    if (! fetchManifest()) return;
    for (int id = 1; id < numInstr && ! quit; ++id)
    {
        if (! used[(size_t) id]) continue;
        auto it = manifest.find (id);
        if (it == manifest.end()) continue;
        const auto uf = userDir().getChildFile (it->second.name), bf = bundledDir().getChildFile (it->second.name);
        const bool okU = uf.existsAsFile() && sha256 (uf) == it->second.sha;
        const bool okB = bf.existsAsFile() && sha256 (bf) == it->second.sha;
        if (okU || okB) continue;
        juce::File f;
        download (id, f);   // nova verzia sa pouzije pri dalsom spusteni
    }
}

void InstrBank::loadOne (int id)
{
    used[(size_t) id] = true;
    const auto nm = fileName (id);
    // novsi z dvoch suborov (stiahnuty po aktualizacii je novsi ako od instalatora)
    juce::File f;
    for (auto d : { userDir(), bundledDir() })
        if (d != juce::File() && d.getChildFile (nm).existsAsFile())
            if (f == juce::File() || d.getChildFile (nm).getLastModificationTime() > f.getLastModificationTime()) f = d.getChildFile (nm);
    if (f == juce::File())
    {
        prog[(size_t) id].store (0.0f);
        state[(size_t) id].store (4);
        if (! download (id, f)) { failAt[(size_t) id].store (juce::Time::getMillisecondCounter()); state[(size_t) id].store (3); return; }
    }
    juce::MemoryBlock mb;
    if (! f.loadFileAsData (mb) || ! parse (id, mb))
    {   // poskodeny subor: zmazat stiahnuty a skusit stiahnut znova
        if (f.isAChildOf (userDir())) f.deleteFile();
        failAt[(size_t) id].store (juce::Time::getMillisecondCounter());
        state[(size_t) id].store (3);
    }
}

bool InstrBank::parse (int id, const juce::MemoryBlock& mb)
{
    const auto* pack = static_cast<const uint8_t*> (mb.getData());
    const size_t packSize = mb.getSize();
    if (packSize < 12 || std::memcmp (pack, "KNIN", 4) != 0) return false;
    auto u32 = [&] (size_t at) { uint32_t v = 0; if (at + 4 <= packSize) std::memcpy (&v, pack + at, 4); return v; };
    const uint32_t count = u32 (8);
    size_t off = 0, size = 0;
    for (uint32_t k = 0; k < count; ++k)
        if ((int) u32 (12 + 12 * k) == id) { off = u32 (16 + 12 * k); size = u32 (20 + 12 * k); }
    if (off == 0 || off + size > packSize || size < 12 || std::memcmp (pack + off, "INST", 4) != 0) return false;
    const uint8_t* b = pack + off;
    auto I = std::make_unique<Inst>();
    I->sus = (b[4] & 1) != 0;
    I->kit = (b[4] & 2) != 0;
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
    if (I->samples.empty() || quit) return false;
    // kazdej note a vrstve najblizsia vzorka; ked vo vrstve chyba, vezme susednu vrstvu
    for (int nt = 0; nt < 128; ++nt)
        for (int l = 0; l < I->nLayers; ++l)
        {
            const Sample* best = nullptr; int bs = 1 << 30;
            for (size_t i = 0; i < I->samples.size(); ++i)
            {
                if (I->kit && I->samples[i].root != nt) continue;   // bicie: len klavesy so zvukom
                const int d = std::abs (I->samples[i].root - nt), dl = std::abs (layers[i] - l);
                const int score = d * 4 + dl * 13 + (I->samples[i].root < nt ? 1 : 0);   // radsej vzorka nad notou
                if (score < bs) { bs = score; best = &I->samples[i]; }
            }
            I->map[(size_t) nt][(size_t) l] = best;
        }
    inst[(size_t) id] = std::move (I);
    state[(size_t) id].store (2);
    return true;
}
