#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "PianoBank.h"
#include <map>

// Realisticke nastroje (pianino, Rhodes, gitary, slaciky, dychy, mallets, bicie) - kazdy je samostatny subor instr_NN.knin.
// Nastroj sa nacita na pozadi az ked ho niekto pouzije; ak subor chyba, stiahne sa z Korgus-Download (zoznam instr/instruments.json).
// Hladaju sa: priecinok "Inspiria Instruments" vedla pluginu (instalator) a %APPDATA%\Korgus\Inspiria\Instruments (stiahnute).
// 0 = koncertny klavir (PianoBank).
class InstrBank
{
public:
    static constexpr int numInstr = 49, maxLayers = 6;
    static constexpr int oldPiano = 36, numMenu = 48;   // 36 = povodny klavir (PianoBank 1), mimo suborov nastrojov
    using Sample = PianoBank::Sample;
    struct Inst
    {
        bool sus = false, kit = false; int nLayers = 1; float gain = 1.0f;   // kit = bicie: kazda klavesa svoj zvuk, bez transpozicie
        std::vector<Sample> samples;
        std::array<std::array<const Sample*, maxLayers>, 128> map {};
        std::array<float, maxLayers> layerGain { 1, 1, 1, 1, 1, 1 };   // jemne vrstvy zosilnene (ostane stvrtina rozdielu oproti najsilnejsej)
    };

    static std::shared_ptr<InstrBank> get();
    ~InstrBank();
    void request (int id);              // zacne nacitavat (lubovolne vlakno, bez blokovania)
    const Inst* find (int id) const;    // nullptr kym nie je nacitany
    bool ready (int id) const { return juce::isPositiveAndBelow (id, numInstr) && state[(size_t) id].load() == 2; }
    bool failed (int id) const { return juce::isPositiveAndBelow (id, numInstr) && state[(size_t) id].load() == 3; }
    bool downloading (int id) const { return juce::isPositiveAndBelow (id, numInstr) && state[(size_t) id].load() == 4; }
    float progress (int id) const { return juce::isPositiveAndBelow (id, numInstr) ? prog[(size_t) id].load() : 0.0f; }
    static juce::File userDir();        // stiahnute nastroje
    static juce::File bundledDir();     // nastroje od instalatora (vedla pluginu)
    static juce::String fileName (int id) { return "instr_" + juce::String (id).paddedLeft ('0', 2) + ".knin"; }

    static juce::String name (int id, bool english);
    static juce::StringArray names (bool english);

private:
    InstrBank();
    void worker();
    void loadOne (int id);
    bool parse (int id, const juce::MemoryBlock& pack);
    bool fetchManifest();
    bool download (int id, juce::File& out);
    void checkUpdates();
    struct MEntry { juce::String name, sha; juce::int64 size = 0; };
    std::map<int, MEntry> manifest; int manifestState = 0;   // 0 neskusane, 1 mame, 2 nepodarilo sa
    std::array<bool, numInstr> used {};       // nacitane v tejto relacii (kontrola aktualizacie)
    bool updatesChecked = false;
    std::array<std::unique_ptr<Inst>, numInstr> inst;
    std::array<std::atomic<int>, numInstr> state {};   // 0 nic, 1 caka, 2 hotovo, 3 chyba, 4 stahuje sa
    std::array<std::atomic<float>, numInstr> prog {};
    std::array<std::atomic<juce::uint32>, numInstr> failAt {};
    std::mutex m; std::condition_variable cv; std::thread th; std::atomic<bool> quit { false };
};
