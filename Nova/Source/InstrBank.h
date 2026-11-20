#pragma once
#include <juce_audio_formats/juce_audio_formats.h>
#include <atomic>
#include <thread>
#include <mutex>
#include <condition_variable>
#include "PianoBank.h"

// Realisticke nastroje (pianino, Rhodes, gitary, slaciky, dychy, mallets) z balika KNIN.
// Nastroj sa nacita na pozadi az ked ho niekto pouzije; 0 = koncertny klavir (PianoBank).
class InstrBank
{
public:
    static constexpr int numInstr = 36, maxLayers = 6;
    using Sample = PianoBank::Sample;
    struct Inst
    {
        bool sus = false; int nLayers = 1; float gain = 1.0f;
        std::vector<Sample> samples;
        std::array<std::array<const Sample*, maxLayers>, 128> map {};
    };

    static std::shared_ptr<InstrBank> get();
    ~InstrBank();
    void request (int id);              // zacne nacitavat (lubovolne vlakno, bez blokovania)
    const Inst* find (int id) const;    // nullptr kym nie je nacitany
    bool ready (int id) const { return juce::isPositiveAndBelow (id, numInstr) && state[(size_t) id].load() == 2; }
    bool failed (int id) const { return juce::isPositiveAndBelow (id, numInstr) && state[(size_t) id].load() == 3; }

    static juce::String name (int id, bool english);
    static juce::StringArray names (bool english);

private:
    InstrBank();
    void worker();
    bool openPack();
    void loadOne (int id);

    juce::MemoryBlock packCopy;               // len na Linuxe (test); vo Windows priamo z prostriedku DLL
    const uint8_t* pack = nullptr; size_t packSize = 0;
    std::array<std::unique_ptr<Inst>, numInstr> inst;
    std::array<std::atomic<int>, numInstr> state {};   // 0 nic, 1 caka, 2 hotovo, 3 chyba
    std::mutex m; std::condition_variable cv; std::thread th; std::atomic<bool> quit { false };
};
