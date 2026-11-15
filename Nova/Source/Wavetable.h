#pragma once
#include <cmath>
#include <vector>
#include <complex>
#include <atomic>
#include <mutex>
#include <algorithm>
#include <cstdint>

// Wavetable oscilator Korgus Inspire: 8 vstavanych tabuliek x 64 ramcov, vsetko vypocitane (ziadne subory).
// Kazdy ramec je ulozeny v 10 urovniach s obmedzenym poctom harmonickych (512, 256, ... 1) - bez aliasu.
// Banka sa postavi raz (mimo audio vlakna, Synth::prepare / rozhranie) a zdielaju ju vsetky instancie.
namespace nova
{
    struct WtBank
    {
        static constexpr int numTables = 8, numFrames = 64, numMips = 10, maxHarm = 512;
        int len[numMips] {}; size_t off[numMips] {};
        std::vector<float> data;

        // ukazovatel na vzorku 0 ramca (pred nou 1 strazna vzorka, za poslednou 3)
        const float* frame (int tab, int mip, int fr) const { return data.data() + off[mip] + (size_t) (tab * numFrames + fr) * (size_t) (len[mip] + 4) + 1; }
        static int harmonics (int mip) { return maxHarm >> mip; }

        // hotova banka alebo nullptr (bezpecne z audio vlakna)
        static const WtBank* peek() { return ptr().load (std::memory_order_acquire); }
        // postavi banku pri prvom volani (NIE z audio vlakna)
        static const WtBank& get()
        {
            static std::once_flag once;
            std::call_once (once, [] { static WtBank b; b.build(); ptr().store (&b, std::memory_order_release); });
            return *ptr().load (std::memory_order_acquire);
        }

        // 4-bodova Lagrangeova interpolacia (x z <0,1>) medzi dvoma ramcami
        static inline float read (const float* a, const float* b, int n, float ph, float fmix)
        {
            const float p = ph * (float) n; const int i = (int) p; const float f = p - (float) i;
            const float fm1 = f - 1.0f, fm2 = f - 2.0f, fp1 = f + 1.0f;
            const float c0 = -f * fm1 * fm2 * (1.0f / 6.0f), c1 = fp1 * fm1 * fm2 * 0.5f, c2 = -fp1 * f * fm2 * 0.5f, c3 = fp1 * f * fm1 * (1.0f / 6.0f);
            const float ya = c0 * a[i - 1] + c1 * a[i] + c2 * a[i + 1] + c3 * a[i + 2];
            const float yb = c0 * b[i - 1] + c1 * b[i] + c2 * b[i + 1] + c3 * b[i + 2];
            return ya + fmix * (yb - ya);
        }

    private:
        static std::atomic<const WtBank*>& ptr() { static std::atomic<const WtBank*> p { nullptr }; return p; }

        static void fft (std::vector<std::complex<double>>& x, bool inverse)
        {
            const size_t n = x.size();
            for (size_t i = 1, j = 0; i < n; ++i)
            {
                size_t bit = n >> 1;
                for (; j & bit; bit >>= 1) j ^= bit;
                j ^= bit;
                if (i < j) std::swap (x[i], x[j]);
            }
            for (size_t l = 2; l <= n; l <<= 1)
            {
                const double ang = (inverse ? 2.0 : -2.0) * 3.14159265358979323846 / (double) l;
                const std::complex<double> wl (std::cos (ang), std::sin (ang));
                for (size_t i = 0; i < n; i += l)
                {
                    std::complex<double> w (1.0, 0.0);
                    for (size_t k = 0; k < l / 2; ++k)
                    {
                        const auto u = x[i + k], v = x[i + k + l / 2] * w;
                        x[i + k] = u + v; x[i + k + l / 2] = u - v; w *= wl;
                    }
                }
            }
        }

        static double hash01 (int k)
        {
            uint32_t x = 0x9E3779B9u * (uint32_t) (k + 101);
            x ^= x >> 15; x *= 0x2C1B3C6Du; x ^= x >> 12; x *= 0x297A2D39u; x ^= x >> 15;
            return (double) (x & 0xFFFFFF) / 16777216.0;
        }

        // spektrum ramca: x(u) = sum a[h] sin(2 pi h u) + b[h] cos(2 pi h u), h = 1..512
        static void spectrum (int tab, double t, std::vector<double>& a, std::vector<double>& b)
        {
            const double pi2 = 6.283185307179586;
            std::fill (a.begin(), a.end(), 0.0); std::fill (b.begin(), b.end(), 0.0);
            auto pulse = [&] (double g, double d)   // pila - g * pila posunuta o d (g = 1: obdlznik / pulz sirky d)
            {
                for (int h = 1; h <= maxHarm; ++h)
                {
                    a[(size_t) h] = (1.0 - g * std::cos (pi2 * h * d)) / h;
                    b[(size_t) h] = g * std::sin (pi2 * h * d) / h;
                }
            };
            auto lerp3 = [] (const double* v, int n, double x, int stride, int idx)   // linearne medzi n bodmi
            {
                const double p = x * (n - 1); int i = (int) p; if (i > n - 2) i = n - 2; const double f = p - i;
                return v[i * stride + idx] + f * (v[(i + 1) * stride + idx] - v[i * stride + idx]);
            };
            switch (tab)
            {
                case 0:   // Analog: pila -> obdlznik -> uzky pulz
                    if (t < 1.0 / 3.0) pulse (t * 3.0, 0.5);
                    else pulse (1.0, 0.5 - 0.4 * (t - 1.0 / 3.0) * 1.5);
                    break;
                case 1:   // PWM: pulz od 50 % po 3 %
                    pulse (1.0, 0.5 - 0.47 * t);
                    break;
                case 2:   // Harmonicky: pila cez rezonancny "filter", ktoreho vrchol prechadza harmonickymi
                {
                    const double c = 1.5 * std::pow (2.0, 5.5 * t), q = 2.5;
                    for (int h = 1; h <= maxHarm; ++h)
                    {
                        const double r = h / c;
                        a[(size_t) h] = 1.0 / (h * std::sqrt ((1.0 - r * r) * (1.0 - r * r) + (r / q) * (r / q)));
                    }
                    break;
                }
                case 3:   // Vokal: A - E - I - O - U (formanty pri zakladnom tone 110 Hz)
                {
                    static const double F[5 * 3] = { 800, 1150, 2900,  400, 1700, 2600,  280, 2250, 2900,  450, 800, 2830,  325, 700, 2530 };
                    static const double G[5 * 3] = { 1, 0.5, 0.25,  1, 0.35, 0.25,  1, 0.25, 0.3,  1, 0.45, 0.1,  1, 0.3, 0.08 };
                    static const double B[3] = { 60, 80, 120 };
                    double mx = 0;
                    for (int h = 1; h <= maxHarm; ++h)
                    {
                        const double f = 110.0 * h; double s = 0.012;
                        for (int k = 0; k < 3; ++k) { const double d = (f - lerp3 (F, 5, t, 3, k)) / B[k]; s += lerp3 (G, 5, t, 3, k) / (1.0 + d * d); }
                        a[(size_t) h] = s / h; mx = std::max (mx, a[(size_t) h]);
                    }
                    a[1] = std::max (a[1], 0.45 * mx);
                    break;
                }
                case 4:   // Digital: fazova modulacia (pomery 2 a 5) - zvonive, jasne spektra; pocita sa v case
                {
                    const size_t N = 4096; std::vector<std::complex<double>> x (N);
                    const double i1 = 3.5 * t, i2 = 1.2 * t * t;
                    for (size_t n = 0; n < N; ++n)
                    {
                        const double u = (double) n / (double) N;
                        x[n] = std::sin (pi2 * u + i1 * std::sin (pi2 * 2.0 * u) + i2 * std::sin (pi2 * 5.0 * u));
                    }
                    fft (x, false);
                    for (int h = 1; h <= maxHarm; ++h) { a[(size_t) h] = -2.0 * x[(size_t) h].imag() / (double) N; b[(size_t) h] = 2.0 * x[(size_t) h].real() / (double) N; }
                    break;
                }
                case 5:   // Organ: 6 registracii tiahel (8', 4', 2 2/3', 2', 1 3/5', 1 1/3', 1', + vyssie)
                {
                    static const int hs[10] = { 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 };
                    static const double R[6 * 10] = { 1, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                                                      1, 0.6, 0, 0.15, 0, 0, 0, 0, 0, 0,
                                                      1, 0.8, 0.7, 0.2, 0, 0, 0, 0, 0, 0,
                                                      1, 0.5, 0.8, 0.5, 0.1, 0.3, 0.1, 0, 0, 0,
                                                      1, 1, 1, 0.8, 0.6, 0.6, 0.5, 0.15, 0.1, 0.05,
                                                      0.9, 1, 0.7, 0.9, 0.6, 0.7, 0.8, 0.5, 0.5, 0.4 };
                    for (int k = 0; k < 10; ++k) a[(size_t) hs[k]] = lerp3 (R, 6, t, 10, k);
                    break;
                }
                case 6:   // Sklo: od neparnych (dute) k parnym harmonickym + ostry "skleny" vrchol, ktory stupa
                {
                    const double c = 7.0 + 18.0 * t, w = 1.5 + 2.0 * t;
                    for (int h = 2; h <= maxHarm; ++h)
                    {
                        const double par = (h & 1) ? 1.0 - 0.9 * t : 0.1 + 0.9 * t;
                        const double d = (h - c) / w;
                        a[(size_t) h] = par * std::pow ((double) h, -1.2) + 0.6 * std::exp (-0.5 * d * d) * ((h & 1) ? 1.0 : 0.3 + 0.7 * t);
                    }
                    a[1] = 1.0;
                    break;
                }
                default:  // Spev: zbor U - A - E, makky zdroj, siroke formanty, rozhodene fazy (difuzny, "viac hlasov")
                {
                    static const double F[3 * 5] = { 350, 700, 2500, 2900, 3300,  700, 1100, 2600, 3000, 3400,  450, 1900, 2600, 3100, 3500 };
                    static const double G[3 * 5] = { 1, 0.3, 0.1, 0.1, 0.05,  1, 0.6, 0.25, 0.2, 0.1,  1, 0.35, 0.3, 0.2, 0.1 };
                    static const double B[5] = { 100, 120, 160, 180, 200 };
                    double mx = 0; std::vector<double> m ((size_t) maxHarm + 1, 0.0);
                    for (int h = 1; h <= maxHarm; ++h)
                    {
                        const double f = 150.0 * h; double s = 0.006;
                        for (int k = 0; k < 5; ++k) { const double d = (f - lerp3 (F, 3, t, 5, k)) / B[k]; s += lerp3 (G, 3, t, 5, k) / (1.0 + d * d); }
                        m[(size_t) h] = s * std::pow ((double) h, -1.3); mx = std::max (mx, m[(size_t) h]);
                    }
                    m[1] = std::max (m[1], 0.5 * mx);
                    for (int h = 1; h <= maxHarm; ++h)
                    {
                        const double ph = h == 1 ? 0.0 : pi2 * hash01 (h);   // faza rovnaka vo vsetkych ramcoch
                        a[(size_t) h] = m[(size_t) h] * std::cos (ph); b[(size_t) h] = m[(size_t) h] * std::sin (ph);
                    }
                    break;
                }
            }
        }

        void build()
        {
            // dlzka = 8x az 64x pocet harmonickych: obrazy kubickej interpolacie su pod -80 dB aj pri plochych spektrach
            static const int lens[numMips] = { 4096, 2048, 1024, 1024, 512, 256, 256, 128, 64, 64 };
            size_t total = 0;
            for (int m = 0; m < numMips; ++m) { len[m] = lens[m]; off[m] = total; total += (size_t) (numTables * numFrames) * (size_t) (len[m] + 4); }
            data.assign (total, 0.0f);
            std::vector<double> a ((size_t) maxHarm + 1), b ((size_t) maxHarm + 1);
            std::vector<std::complex<double>> x;
            for (int tab = 0; tab < numTables; ++tab)
                for (int fr = 0; fr < numFrames; ++fr)
                {
                    spectrum (tab, (double) fr / (double) (numFrames - 1), a, b);
                    double pw = 0;
                    for (int h = 1; h <= maxHarm; ++h) pw += 0.5 * (a[(size_t) h] * a[(size_t) h] + b[(size_t) h] * b[(size_t) h]);
                    double scale = pw > 1.0e-20 ? 0.5 / std::sqrt (pw) : 0.0;    // rovnaka efektivna hodnota vsetkych ramcov
                    for (int m = 0; m < numMips; ++m)
                    {
                        const int n = len[m], H = harmonics (m);
                        x.assign ((size_t) n, std::complex<double> (0.0, 0.0));
                        for (int h = 1; h <= H; ++h)
                        {
                            x[(size_t) h] = std::complex<double> (0.5 * b[(size_t) h], -0.5 * a[(size_t) h]);
                            x[(size_t) (n - h)] = std::conj (x[(size_t) h]);
                        }
                        fft (x, true);
                        if (m == 0)   // spicka najviac 1.5
                        {
                            double pk = 0; for (auto& c : x) pk = std::max (pk, std::abs (c.real()));
                            if (pk * scale > 1.5) scale = 1.5 / pk;
                        }
                        float* d = data.data() + off[m] + (size_t) (tab * numFrames + fr) * (size_t) (n + 4) + 1;
                        for (int i = 0; i < n; ++i) d[i] = (float) (x[(size_t) i].real() * scale);
                        d[-1] = d[n - 1]; d[n] = d[0]; d[n + 1] = d[1]; d[n + 2] = d[2];
                    }
                }
        }
    };
}
