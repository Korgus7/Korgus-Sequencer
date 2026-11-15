#pragma once
// Rytmicky gater - spolocne pre Korgus Inspiria (zalozka GATER) a efekt Korgus Gater v Sequenceri
#include <cmath>

namespace korgus
{
    struct GatePatterns
    {
        static constexpr int count = 10;
        static const char* nameSk (int i)
        {
            static const char* n[count] = { "Trance", "Trance 2", "Osminy", "Štvrťky", "Šestnástky", "Synkopa", "Galop", "Dub", "Pulz", "Rozbité" };
            return (i >= 0 && i < count) ? n[i] : "";
        }
        static const char* nameEn (int i)
        {
            static const char* n[count] = { "Trance", "Trance 2", "Eighths", "Quarters", "Sixteenths", "Syncopated", "Gallop", "Dub", "Pulse", "Broken" };
            return (i >= 0 && i < count) ? n[i] : "";
        }
        // 16 krokov: '1' = hra, '0' = ticho
        static const char* bits (int i)
        {
            static const char* b[count] = { "1011101010111011", "1101101101101101", "1010101010101010", "1000100010001000", "1111111111111111",
                                            "1001001001001010", "1011011011011011", "1000001010001000", "1110111011101110", "1101001110100110" };
            return (i >= 0 && i < count) ? b[i] : b[0];
        }
    };

    // krok vzoru pre poziciu v dobach (beat), dlzku kroku (sl, v dobach) a swing (0..0.5: parne kroky sa predlzia)
    // vrati 1 = hra / 0 = ticho, vratane kratkej medzery medzi susednymi hrajucimi krokmi
    inline float gateTarget (double beat, double sl, const bool* pat, float swing)
    {
        const double bp = beat / sl;
        const double pair = std::floor (bp * 0.5);
        const double f = bp - 2.0 * pair;                  // 0..2 v ramci dvojice krokov
        const double m = 1.0 + (double) swing;             // hranica medzi krokmi dvojice
        int k; double frac;
        if (f < m) { k = 0; frac = f / m; } else { k = 1; frac = (f - m) / (2.0 - m); }
        const long long step = (long long) pair * 2 + k;
        const int s = (int) ((step % 16 + 16) % 16);
        const bool on = pat[s] && (frac < 0.88 || ! pat[(s + 1) % 16]);
        return on ? 1.0f : 0.0f;
    }
}
