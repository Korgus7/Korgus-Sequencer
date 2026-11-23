#pragma once
// „Pokračuj v mojej melódii“ – lokálna AI (Anticipatory Music Transformer, Stanford CRFM, Apache 2.0).
// Vlastná implementácia GPT-2 v C++ (bez Pythonu), beží na procesore, model sa stiahne pri prvom použití.
#include <juce_core/juce_core.h>
#include <atomic>
#include <functional>
#include <vector>

namespace korgus
{
struct AiNote { double step = 0, len = 1; int pitch = 60, vel = 100; };

class MelodyAI
{
public:
    // nacitanie modelu (.kamt, f16); vrati chybu alebo prazdny retazec
    juce::String load (const juce::File& f);
    bool loaded() const { return nLayer > 0; }
    void unload();

    struct Params
    {
        double stepSec = 0.125;      // dlzka kroku 1/16 v sekundach
        double promptEndStep = 0;    // koniec povodnej melodie (odkial pokracovat)
        double genSteps = 64;        // kolko krokov dopisat
        float topP = 0.93f;          // tvorivost (nucleus)
        int instrument = 0;          // GM nastroj (0 = klavir)
        int pitchLo = 0, pitchHi = 127;
        unsigned seed = 1;
    };
    // pokracovanie melodie; progress 0..1, cancel = prerusenie
    std::vector<AiNote> continueMelody (const std::vector<AiNote>& prompt, const Params& p,
                                        std::function<void (float)> progress, const std::atomic<bool>& cancel);

    // testovaci pristup: logity posledneho tokenu (pre overenie oproti referencii)
    std::vector<float> logitsFor (const std::vector<int>& tokens);

private:
    int nLayer = 0, nHead = 0, nEmbd = 0, nVocab = 0, nCtx = 0;
    struct Layer { std::vector<float> ln1g, ln1b, wqkv, bqkv, wo, bo, ln2g, ln2b, wfc, bfc, wpr, bpr; };
    std::vector<float> wte, wpe, lnfg, lnfb;
    std::vector<Layer> layers;
    // KV cache
    std::vector<float> kc, vc; int cacheLen = 0;
    std::vector<float> lastHidden;   // posledny skryty stav (po ln_f)

    void resetCache();
    // spracuje tokeny od pozicie cacheLen, ulozi posledny skryty stav
    void forward (const int* toks, int n);
    float logit (int token) const;
};
} // namespace korgus
