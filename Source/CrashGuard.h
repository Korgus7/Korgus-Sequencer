#pragma once
#include <juce_core/juce_core.h>
#include <atomic>

// zaznam o pade programu: ktory plugin sa prave nacitaval alebo spracoval zvuk
namespace CrashGuard
{
    void install (const juce::File& dataDir);              // obsluha padu + znacka "program bezi"
    void cleanExit();                                      // normalne ukoncenie - zmaze znacku
    void registerPlugin (const void* p, const juce::String& uid, const juce::String& name);
    void beginLoad (const juce::String& uid, const juce::String& name);
    void endLoad();

    struct Info { bool crashed = false; juce::String uid, name, where; };
    Info takeLastCrash();                                  // co sa stalo minule (raz po spusteni)

    extern std::atomic<const void*> busy;                  // plugin, ktory prave spracuva zvuk
    struct Busy { explicit Busy (const void* p) { busy.store (p, std::memory_order_relaxed); } ~Busy() { busy.store (nullptr, std::memory_order_relaxed); } };
}
