#include "DrumKits.h"
#include <cmath>
#include <vector>

namespace
{
    constexpr double twoPi = juce::MathConstants<double>::twoPi;

    struct Gen
    {
        double sr;
        std::vector<float> x;
        juce::Random rnd;
        Gen (double rate, double seconds, juce::int64 seed) : sr (rate), x ((size_t) juce::jmax (1, (int) (rate * seconds))), rnd (seed) {}
        int n() const { return (int) x.size(); }
        double t (int i) const { return i / sr; }

        // sinus s klesajucou vyskou: f(t) = f1 + (f0 - f1) * e^(-t/tp), hlasitost e^(-t/td)
        void sweep (double f0, double f1, double tp, double amp, double td, double attack = 0.0005)
        {
            double ph = 0;
            for (int i = 0; i < n(); ++i)
            {
                const double tt = t (i), f = f1 + (f0 - f1) * std::exp (-tt / tp);
                ph += twoPi * f / sr;
                const double a = amp * std::exp (-tt / td) * juce::jmin (1.0, tt / attack);
                x[(size_t) i] += (float) (a * std::sin (ph));
            }
        }
        // tlmeny sinus (mod nastroja)
        void mode (double f, double amp, double td, double phase = 0) { sweep (f, f, 1.0, amp, td); (void) phase; }
        // sum cez filter (typ: 0 dolny, 1 horny, 2 pasmovy), obalka nabeh/dozvuk, od casu start
        void noise (int type, double fc, double q, double amp, double td, double start = 0, double attack = 0.0005)
        {
            juce::IIRFilter f;
            f.setCoefficients (type == 0 ? juce::IIRCoefficients::makeLowPass (sr, fc, q)
                               : type == 1 ? juce::IIRCoefficients::makeHighPass (sr, fc, q)
                                           : juce::IIRCoefficients::makeBandPass (sr, fc, q));
            const int s0 = (int) (start * sr);
            for (int i = s0; i < n(); ++i)
            {
                const double tt = (i - s0) / sr;
                const float w = rnd.nextFloat() * 2.0f - 1.0f;
                const double a = amp * std::exp (-tt / td) * juce::jmin (1.0, tt / attack);
                x[(size_t) i] += (float) a * f.processSingleSampleRaw (w);
            }
        }
        // kovovy zvuk 808: 6 obdlznikov s neharmonickymi pomermi, cez horny filter
        void metal (double scale, double hp, double amp, double td, double attack = 0.0003)
        {
            static const double r[] = { 205.3, 304.4, 369.6, 522.7, 540.0, 800.0 };
            double ph[6] = {};
            juce::IIRFilter f1, f2;
            f1.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, hp, 0.7));
            f2.setCoefficients (juce::IIRCoefficients::makeHighPass (sr, hp, 0.7));
            for (int i = 0; i < n(); ++i)
            {
                double s = 0;
                for (int k = 0; k < 6; ++k) { ph[k] += r[k] * scale / sr; s += (ph[k] - std::floor (ph[k])) < 0.5 ? 1.0 : -1.0; }
                const double tt = t (i), a = amp * std::exp (-tt / td) * juce::jmin (1.0, tt / attack);
                x[(size_t) i] += (float) a * f2.processSingleSampleRaw (f1.processSingleSampleRaw ((float) (s / 6.0)));
            }
        }
        // tlieskanie: niekolko kratkych zablesknuti a dozvuk
        void clap (double fc, double amp, double tail, int bursts = 4, double gap = 0.011)
        {
            for (int b = 0; b < bursts; ++b) noise (2, fc, 1.2, amp * (b == bursts - 1 ? 1.0 : 0.8), 0.006, b * gap, 0.0008);
            noise (2, fc, 1.0, amp * 0.6, tail, (bursts - 1) * gap, 0.001);
        }
        void saturate (double drive)
        {
            for (auto& v : x) v = (float) (std::tanh (v * drive) / std::tanh (drive));
        }
        void fadeOut (double sec)
        {
            const int m = juce::jmin (n(), (int) (sec * sr));
            for (int i = 0; i < m; ++i) x[(size_t) (n() - 1 - i)] *= (float) i / (float) m;
        }
    };

    void build (int kit, int lane, Gen& g)
    {
        switch (kit * 10 + lane)
        {
            // ---------------- akusticke ----------------
            case 0:  g.sweep (95, 52, 0.03, 1.0, 0.28); g.noise (0, 2500, 0.8, 0.35, 0.012); g.mode (110, 0.25, 0.12); break;
            case 1:  g.mode (185, 0.55, 0.09); g.mode (330, 0.3, 0.06); g.noise (2, 4200, 0.6, 0.9, 0.16); g.noise (1, 7000, 0.7, 0.25, 0.09); break;
            case 2:  g.clap (1300, 0.9, 0.12, 3, 0.009); break;
            case 3:  g.noise (1, 8000, 0.8, 0.7, 0.035); g.metal (1.6, 7500, 0.35, 0.03); break;
            case 4:  g.noise (1, 7000, 0.8, 0.6, 0.35); g.metal (1.6, 7000, 0.4, 0.32); break;
            case 5:  g.sweep (125, 95, 0.08, 0.9, 0.35); g.noise (2, 1200, 0.8, 0.2, 0.05); break;
            case 6:  g.sweep (210, 165, 0.06, 0.9, 0.25); g.noise (2, 1800, 0.8, 0.2, 0.04); break;
            case 7:  g.mode (1750, 0.6, 0.012); g.mode (480, 0.5, 0.03); g.noise (2, 3000, 1.0, 0.4, 0.01); break;
            case 8:  g.noise (1, 5000, 0.7, 0.55, 1.2, 0, 0.002); g.metal (2.1, 5000, 0.45, 1.0, 0.002); g.noise (2, 9000, 0.8, 0.3, 0.6); break;
            case 9:  g.mode (560, 0.6, 0.12); g.mode (845, 0.45, 0.09); g.noise (2, 2500, 2.0, 0.15, 0.02); break;
            // ---------------- 808 / trap ----------------
            case 10: g.sweep (150, 48, 0.045, 1.0, 0.75); g.saturate (1.6); g.fadeOut (0.05); break;
            case 11: g.mode (180, 0.45, 0.08); g.mode (335, 0.35, 0.05); g.noise (1, 2000, 0.7, 0.8, 0.12); break;
            case 12: g.clap (1100, 1.0, 0.22, 4, 0.012); break;
            case 13: g.metal (1.0, 8000, 1.0, 0.025); break;
            case 14: g.metal (1.0, 7500, 1.0, 0.28); break;
            case 15: g.sweep (130, 85, 0.1, 1.0, 0.45); break;
            case 16: g.sweep (230, 165, 0.1, 1.0, 0.35); break;
            case 17: g.mode (1700, 0.7, 0.008); g.mode (500, 0.5, 0.015); break;
            case 18: g.metal (1.0, 4500, 0.8, 1.4, 0.001); g.noise (1, 6000, 0.7, 0.4, 0.9); break;
            case 19: { double p1 = 0, p2 = 0;
                       juce::IIRFilter f; f.setCoefficients (juce::IIRCoefficients::makeBandPass (g.sr, 2600, 1.4));
                       for (int i = 0; i < g.n(); ++i) { p1 += 540 / g.sr; p2 += 800 / g.sr; const double tt = g.t (i);
                           const double s = ((p1 - std::floor (p1)) < 0.5 ? 1 : -1) + ((p2 - std::floor (p2)) < 0.5 ? 1 : -1);
                           const double a = tt < 0.01 ? 1.0 : 0.6 * std::exp (-(tt - 0.01) / 0.09) + 0.4 * std::exp (-tt / 0.02);
                           g.x[(size_t) i] += (float) (0.5 * a) * f.processSingleSampleRaw ((float) s); } } break;
            // ---------------- elektronicke (909) ----------------
            case 20: g.sweep (280, 55, 0.018, 1.0, 0.32); g.noise (0, 3000, 0.7, 0.3, 0.006); g.saturate (2.2); break;
            case 21: g.sweep (240, 180, 0.03, 0.5, 0.07); g.mode (340, 0.3, 0.05); g.noise (2, 5000, 0.5, 1.0, 0.14); break;
            case 22: g.clap (1500, 1.0, 0.18, 4, 0.010); break;
            case 23: g.noise (1, 9000, 0.7, 0.8, 0.03); g.metal (1.3, 9000, 0.5, 0.03); break;
            case 24: g.noise (1, 8500, 0.7, 0.7, 0.3); g.metal (1.3, 8000, 0.5, 0.3); break;
            case 25: g.sweep (200, 105, 0.03, 1.0, 0.3); g.noise (2, 900, 1.0, 0.15, 0.03); break;
            case 26: g.sweep (300, 175, 0.03, 1.0, 0.24); g.noise (2, 1400, 1.0, 0.15, 0.03); break;
            case 27: g.mode (2200, 0.6, 0.01); g.mode (820, 0.5, 0.02); g.noise (2, 4000, 1.0, 0.4, 0.008); break;
            case 28: g.noise (1, 6500, 0.7, 0.6, 0.7); g.metal (1.8, 6000, 0.5, 0.6); break;
            case 29: g.mode (700, 0.5, 0.06); g.mode (1050, 0.5, 0.05); g.noise (2, 3000, 2.0, 0.1, 0.01); break;
            // ---------------- perkusie ----------------
            case 30: g.sweep (120, 80, 0.02, 1.0, 0.22); g.noise (2, 600, 1.0, 0.3, 0.03); break;                     // djembe
            case 31: g.sweep (360, 330, 0.02, 0.9, 0.2); g.mode (660, 0.2, 0.08); g.noise (2, 2000, 1.0, 0.2, 0.015); break; // konga
            case 32: g.sweep (520, 480, 0.01, 0.9, 0.12); g.noise (2, 3000, 1.0, 0.25, 0.01); break;                 // bongo
            case 33: g.noise (2, 6500, 1.5, 0.9, 0.05, 0, 0.012); break;                                              // shaker
            case 34: g.noise (2, 7500, 2.0, 0.6, 0.2, 0, 0.002); g.metal (2.6, 6000, 0.4, 0.15); g.noise (2, 9500, 2.5, 0.4, 0.08, 0.03); break; // tamburina
            case 35: g.sweep (250, 220, 0.03, 1.0, 0.3); g.mode (500, 0.15, 0.1); break;                               // konga nizka
            case 36: g.mode (690, 0.6, 0.2); g.mode (1090, 0.3, 0.12); g.noise (1, 3000, 0.7, 0.5, 0.06); break;      // timbales
            case 37: g.mode (2500, 0.9, 0.04); g.mode (5300, 0.15, 0.02); break;                                        // claves
            case 38: g.mode (1250, 0.4, 1.2); g.mode (3400, 0.3, 0.9); g.mode (5800, 0.2, 0.6); g.mode (8300, 0.1, 0.4); break; // triangel
            case 39: g.mode (880, 0.6, 0.25); g.mode (2320, 0.3, 0.15); g.mode (3600, 0.12, 0.08); break;              // agogo
            // ---------------- ludova ----------------
            case 40: g.sweep (80, 58, 0.05, 1.0, 0.45); g.noise (0, 900, 0.7, 0.35, 0.03); g.mode (140, 0.2, 0.2); break; // velky bubon
            case 41: g.mode (240, 0.5, 0.08); g.noise (2, 3500, 0.7, 0.8, 0.12); g.noise (2, 5500, 1.0, 0.3, 0.06, 0.02); break; // maly bubienok
            case 42: g.clap (1200, 0.9, 0.08, 2, 0.006); g.clap (1500, 0.6, 0.06, 2, 0.007); break;                   // tlieskanie
            case 43: g.mode (1900, 0.6, 0.02); g.mode (3100, 0.4, 0.015); g.noise (2, 2500, 1.5, 0.3, 0.01); break;   // lyzice / drevo
            case 44: for (int k = 0; k < 4; ++k) g.noise (2, 8000 + 600 * k, 3.0, 0.45, 0.06, 0.012 * k, 0.001); g.metal (2.8, 6500, 0.3, 0.2); break; // rolnicky
            case 45: g.sweep (110, 85, 0.05, 1.0, 0.35); g.noise (0, 1200, 0.7, 0.25, 0.03); break;                   // bubon nizky
            case 46: g.sweep (190, 160, 0.04, 0.9, 0.22); g.noise (2, 1600, 0.8, 0.25, 0.03); break;                  // bubon vysoky
            case 47: g.mode (1050, 0.8, 0.05); g.mode (2700, 0.2, 0.02); break;                                        // drevko
            case 48: g.noise (1, 3500, 0.7, 0.6, 0.9, 0, 0.002); g.metal (1.4, 3500, 0.5, 0.8, 0.002); break;         // cinel
            case 49: g.mode (1480, 0.5, 0.5); g.mode (3700, 0.25, 0.35); g.mode (5900, 0.12, 0.2); break;             // zvoncek
            default: break;
        }
    }

    double lengthFor (int kit, int lane)
    {
        if (lane == 8 || (kit == 3 && lane == 8)) return 2.0;
        if (kit == 1 && lane == 0) return 1.6;
        if (lane == 4) return 1.0;
        return 0.8;
    }
}

namespace DrumKits
{
    juce::String kitName (int kit)
    {
        static const char* n[] = { "Akustické", "808 / Trap", "Elektronické (909)", "Perkusie", "Ľudová" };
        return juce::isPositiveAndBelow (kit, numKits) ? juce::String::fromUTF8 (n[kit]) : juce::String();
    }

    juce::String laneName (int kit, int lane)
    {
        static const char* base[] = { "Kick", "Snare", "Clap", "HH zatvorený", "HH otvorený", "Tom nízky", "Tom vysoký", "Rim", "Činel", "Cowbell" };
        static const char* perc[] = { "Djembe", "Konga", "Bongo", "Shaker", "Tamburína", "Konga nízka", "Timbales", "Claves", "Triangel", "Agogo" };
        static const char* folk[] = { "Veľký bubon", "Bubienok", "Tlieskanie", "Lyžice", "Rolničky", "Bubon nízky", "Bubon vysoký", "Drievko", "Činel", "Zvonček" };
        static const char* pre[] = { "", "808 ", "909 ", "", "" };
        if (! juce::isPositiveAndBelow (kit, numKits) || ! juce::isPositiveAndBelow (lane, numLanes)) return {};
        if (kit == 3) return juce::String::fromUTF8 (perc[lane]);
        if (kit == 4) return juce::String::fromUTF8 (folk[lane]);
        return juce::String::fromUTF8 (pre[kit]) + juce::String::fromUTF8 (base[lane]);
    }

    bool make (int kit, int lane, double sr, juce::AudioBuffer<float>& out)
    {
        if (! juce::isPositiveAndBelow (kit, numKits) || ! juce::isPositiveAndBelow (lane, numLanes) || sr < 8000) return false;
        Gen g (sr, lengthFor (kit, lane), 1000 + kit * 37 + lane);
        build (kit, lane, g);
        g.fadeOut (0.12);
        float pk = 0;
        for (auto v : g.x) pk = juce::jmax (pk, std::abs (v));
        if (pk < 1.0e-6f) return false;
        out.setSize (1, g.n());
        for (int i = 0; i < g.n(); ++i) out.setSample (0, i, g.x[(size_t) i] * 0.9f / pk);
        return true;
    }
}
