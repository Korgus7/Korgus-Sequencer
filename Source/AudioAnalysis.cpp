#include "AudioAnalysis.h"
#include "../third_party/pocketfft_hdronly.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <numeric>

namespace
{
constexpr double asr = 22050.0;

std::vector<float> toMono22k (const juce::AudioBuffer<float>& in, double sr)
{
    const int n = in.getNumSamples(), ch = juce::jmax (1, in.getNumChannels());
    std::vector<float> mono ((size_t) n + 256, 0.0f);
    for (int c = 0; c < ch; ++c)
    {
        const float* s = in.getReadPointer (c);
        for (int i = 0; i < n; ++i) mono[(size_t) i] += s[i] / (float) ch;
    }
    const double ratio = sr / asr;
    if (std::abs (ratio - 1.0) < 1.0e-6) { mono.resize ((size_t) n); return mono; }
    const int m = (int) std::floor (n / ratio);
    std::vector<float> out ((size_t) juce::jmax (0, m));
    juce::WindowedSincInterpolator ip;
    ip.process (ratio, mono.data(), out.data(), m);
    return out;
}

// magnitudove spektrum po ramcoch (hann)
struct Spec
{
    int nfft = 0, hop = 0, frames = 0, bins = 0;
    std::vector<float> mag;   // frames * bins
    float at (int f, int b) const { return mag[(size_t) f * (size_t) bins + (size_t) b]; }
};

Spec stft (const std::vector<float>& x, int nfft, int hop, const std::function<bool (double)>& prog, double p0, double p1)
{
    Spec s;
    s.nfft = nfft; s.hop = hop; s.bins = nfft / 2 + 1;
    s.frames = juce::jmax (0, ((int) x.size() - nfft) / hop + 1);
    s.mag.assign ((size_t) s.frames * (size_t) s.bins, 0.0f);
    std::vector<float> win ((size_t) nfft), fr ((size_t) nfft);
    for (int i = 0; i < nfft; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) nfft);
    std::vector<std::complex<float>> sp ((size_t) s.bins);
    const pocketfft::shape_t shape { (size_t) nfft };
    const pocketfft::stride_t sr { (ptrdiff_t) sizeof (float) }, sc { (ptrdiff_t) sizeof (std::complex<float>) };
    for (int f = 0; f < s.frames; ++f)
    {
        for (int i = 0; i < nfft; ++i) fr[(size_t) i] = x[(size_t) (f * hop + i)] * win[(size_t) i];
        pocketfft::r2c (shape, sr, sc, 0, pocketfft::FORWARD, fr.data(), sp.data(), 1.0f);
        float* m = s.mag.data() + (size_t) f * (size_t) s.bins;
        for (int b = 0; b < s.bins; ++b) m[b] = std::abs (sp[(size_t) b]);
        if ((f & 255) == 0 && prog) prog (p0 + (p1 - p0) * f / juce::jmax (1, s.frames));
    }
    return s;
}

// spektralny tok v pasme (log magnituda, len narast), odcitany lokalny priemer
std::vector<float> flux (const Spec& s, double fLo, double fHi)
{
    const int b0 = juce::jlimit (1, s.bins - 1, (int) (fLo / asr * s.nfft)), b1 = juce::jlimit (b0 + 1, s.bins, (int) (fHi / asr * s.nfft) + 1);
    std::vector<float> o ((size_t) s.frames, 0.0f);
    for (int f = 1; f < s.frames; ++f)
    {
        double sum = 0;
        for (int b = b0; b < b1; ++b)
        {
            const float d = std::log1p (100.0f * s.at (f, b)) - std::log1p (100.0f * s.at (f - 1, b));
            if (d > 0) sum += d;
        }
        o[(size_t) f] = (float) (sum / (b1 - b0));
    }
    // odcitat klzavy priemer (~0.25 s)
    const int w = juce::jmax (1, (int) (0.25 * asr / s.hop));
    std::vector<float> r (o.size(), 0.0f);
    double acc = 0;
    for (int i = 0; i < (int) o.size(); ++i)
    {
        acc += o[(size_t) i];
        if (i - 2 * w - 1 >= 0) acc -= o[(size_t) (i - 2 * w - 1)];
        const int lo = juce::jmax (0, i - 2 * w), cnt = i - lo + 1;
        const int c = i - w;
        if (c >= 0) r[(size_t) c] = juce::jmax (0.0f, o[(size_t) c] - (float) (acc / cnt));
    }
    return r;
}

// tempo (perioda v ramcoch) autokorelaciou s preferenciou okolo 120 BPM
double tempoPeriod (const std::vector<float>& env, double fps)
{
    const int n = (int) env.size();
    const int lagMin = (int) (fps * 60.0 / 200.0), lagMax = juce::jmin (n - 1, (int) (fps * 60.0 / 55.0));
    std::vector<double> ac ((size_t) lagMax + 2, 0.0);
    for (int L = lagMin; L <= lagMax; ++L)
    {
        double s = 0;
        for (int i = L; i < n; ++i) s += (double) env[(size_t) i] * env[(size_t) (i - L)];
        ac[(size_t) L] = s / (n - L);
    }
    double best = -1, bestL = fps * 0.5;
    for (int L = lagMin + 1; L < lagMax; ++L)
    {
        const double bpm = 60.0 * fps / L;
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 120.0) / 0.9, 2.0));
        // pomoc od dvojnasobnej periody (takt)
        const double score = (ac[(size_t) L] + 0.5 * (2 * L <= lagMax ? ac[(size_t) (2 * L)] : 0.0)) * w;
        if (score > best && ac[(size_t) L] >= ac[(size_t) L - 1] && ac[(size_t) L] >= ac[(size_t) L + 1]) { best = score; bestL = L; }
    }
    // parabolicke spresnenie
    const int L = (int) bestL;
    if (L > lagMin && L < lagMax)
    {
        const double a = ac[(size_t) L - 1], b = ac[(size_t) L], c = ac[(size_t) L + 1], den = a - 2 * b + c;
        if (std::abs (den) > 1.0e-12) return L + 0.5 * (a - c) / den;
    }
    return bestL;
}

// sledovanie dob dynamickym programovanim (Ellis 2007)
std::vector<int> trackBeats (const std::vector<float>& env, double period)
{
    const int n = (int) env.size();
    if (n < 4 || period < 2) return {};
    const double tight = 300.0;
    std::vector<double> score ((size_t) n, 0.0);
    std::vector<int> back ((size_t) n, -1);
    double mx = 1.0e-9; for (auto v : env) mx = juce::jmax (mx, (double) v);
    for (int t = 0; t < n; ++t)
    {
        const double loc = env[(size_t) t] / mx;
        const int a = t - (int) std::round (2.0 * period), b = t - (int) std::round (0.5 * period);
        double best = 0; int bi = -1;
        for (int tau = juce::jmax (0, a); tau <= b; ++tau)
        {
            const double d = std::log ((t - tau) / period);
            const double v = score[(size_t) tau] - tight * d * d * 0.01;
            if (bi < 0 || v > best) { best = v; bi = tau; }
        }
        score[(size_t) t] = loc + (bi >= 0 ? best : 0.0);
        back[(size_t) t] = bi;
    }
    // koniec: najlepsie skore v poslednej perioda
    int t = n - 1; double bs = -1.0e18;
    for (int i = juce::jmax (0, n - (int) period); i < n; ++i) if (score[(size_t) i] > bs) { bs = score[(size_t) i]; t = i; }
    std::vector<int> beats;
    while (t >= 0) { beats.push_back (t); t = back[(size_t) t]; }
    std::reverse (beats.begin(), beats.end());
    return beats;
}

const char* pcName (int pc) { static const char* n[12] = { "C", "C#", "D", "Eb", "E", "F", "F#", "G", "Ab", "A", "B", "H" }; return n[(pc % 12 + 12) % 12]; }

// "maximum" bez extremov (99. percentil nenulovych hodnot)
float robustMax (const std::vector<float>& e)
{
    std::vector<float> v; v.reserve (e.size());
    for (auto x : e) if (x > 0) v.push_back (x);
    if (v.empty()) return 0.0f;
    const size_t k = (size_t) ((v.size() - 1) * 0.99);
    std::nth_element (v.begin(), v.begin() + (long) k, v.end());
    return v[k];
}

std::vector<std::pair<double, float>> pickPeaks (const std::vector<float>& env, double fps, double minGapSec, float k, float floorRel = 0.08f)
{
    std::vector<std::pair<double, float>> out;
    const int n = (int) env.size(), w = juce::jmax (2, (int) (0.4 * fps)), gap = juce::jmax (1, (int) (minGapSec * fps));
    float mx = robustMax (env);
    if (mx <= 0) return out;
    int last = -gap;
    std::vector<float> win;
    for (int i = 1; i + 1 < n; ++i)
    {
        const float v = env[(size_t) i];
        if (v < env[(size_t) i - 1] || v < env[(size_t) i + 1] || v < mx * floorRel) continue;
        win.clear();
        for (int j = juce::jmax (0, i - w); j < juce::jmin (n, i + w); ++j) win.push_back (env[(size_t) j]);
        std::nth_element (win.begin(), win.begin() + (long) win.size() / 2, win.end());
        const float med = win[win.size() / 2];
        if (v < med * k + mx * 0.05f) continue;
        if (i - last < gap) { if (! out.empty() && v > out.back().second) { out.back() = { i / fps, v }; last = i; } continue; }
        out.push_back ({ i / fps, v });
        last = i;
    }
    return out;
}
} // namespace

AnalysisResult analyzeAudio (const juce::AudioBuffer<float>& in, double sr, int what, const std::function<bool (double)>& progress)
{
    AnalysisResult r;
    auto prog = [&] (double p) { return ! progress || progress (p); };
    const auto x = toMono22k (in, sr);
    if (x.size() < (size_t) (asr * 2)) return r;

    // 1) doby a tempo
    const auto sp = stft (x, 1024, 256, progress, 0.0, 0.3);
    if (! prog (0.3)) return r;
    const double fps = asr / sp.hop;
    const auto env = flux (sp, 30.0, 8000.0);
    const double per = tempoPeriod (env, fps);
    auto bf = trackBeats (env, per);
    if (bf.size() < 8) return r;
    for (int f : bf) r.beats.push_back ((f * sp.hop + sp.nfft * 0.5) / asr);
    std::vector<double> iv;
    for (size_t i = 1; i < r.beats.size(); ++i) iv.push_back (r.beats[i] - r.beats[i - 1]);
    auto ivs = iv; std::sort (ivs.begin(), ivs.end());
    const double med = ivs[ivs.size() / 2];
    r.bpm = 60.0 / med;
    // chroma po dobach (akordy aj urcenie prvej doby taktu)
    const auto cs = stft (x, 8192, 2048, progress, 0.4, 0.7);
    const int nb = (int) r.beats.size();
    std::vector<std::array<double, 12>> chroma ((size_t) nb);
    std::vector<double> energy ((size_t) nb, 0.0);
    std::vector<int> pcOf ((size_t) cs.bins, -1);
    for (int b = 1; b < cs.bins; ++b)
    {
        const double f = b * asr / cs.nfft;
        if (f < 60.0 || f > 2000.0) continue;
        pcOf[(size_t) b] = (int) std::lround (12.0 * std::log2 (f / 440.0) + 69.0) % 12;
    }
    for (int i = 0; i < nb; ++i)
    {
        auto& c = chroma[(size_t) i]; c.fill (0.0);
        const double t0 = r.beats[(size_t) i], t1 = i + 1 < nb ? r.beats[(size_t) i + 1] : t0 + med;
        const int f0 = juce::jmax (0, (int) ((t0 * asr - cs.nfft * 0.5) / cs.hop)), f1 = juce::jmin (cs.frames, juce::jmax (f0 + 1, (int) ((t1 * asr - cs.nfft * 0.5) / cs.hop)));
        for (int f = f0; f < f1; ++f)
            for (int b = 1; b < cs.bins; ++b)
                if (pcOf[(size_t) b] >= 0) { const double m = cs.at (f, b); c[(size_t) pcOf[(size_t) b]] += m * m; }
        double sum = 0; for (double v : c) sum += v;
        energy[(size_t) i] = sum;
        if (sum > 0) for (double& v : c) v = std::sqrt (v / sum);
    }
    std::vector<double> nov ((size_t) nb, 0.0);   // zmena harmonie na dobe
    for (int i = 1; i < nb; ++i) { double d = 0; for (int q = 0; q < 12; ++q) d += std::abs (chroma[(size_t) i][(size_t) q] - chroma[(size_t) i - 1][(size_t) q]); nov[(size_t) i] = d; }
    // prva doba taktu: kopak (nizke pasmo) a zmena harmonie
    const auto low = flux (sp, 30.0, 150.0);
    auto envAt = [&] (const std::vector<float>& e, double t) { const int f = juce::jlimit (0, (int) e.size() - 1, (int) std::round (t * fps - sp.nfft * 0.5 / sp.hop)); float m = 0; for (int k = -2; k <= 2; ++k) m = juce::jmax (m, e[(size_t) juce::jlimit (0, (int) e.size() - 1, f + k)]); return m; };
    double lowMax = 0, novMax = 0;
    for (size_t i = 0; i < r.beats.size(); ++i) { lowMax = juce::jmax (lowMax, (double) envAt (low, r.beats[i])); novMax = juce::jmax (novMax, nov[i]); }
    {
        double best = -1; int bp = 0;
        for (int ph = 0; ph < 4; ++ph)
        {
            double s = 0;
            double kk = 0, nn = 0;
            for (size_t i = (size_t) ph; i < r.beats.size(); i += 4) { kk += envAt (low, r.beats[i]); nn += nov[i]; }
            s = kk / (lowMax + 1.0e-9) + 1.5 * nn / (novMax + 1.0e-9);
            if (s > best) { best = s; bp = ph; }
        }
        r.downbeat = bp;
    }
    {   // stale tempo? dlzky taktov od prvej doby (bez prveho a posledneho taktu)
        std::vector<double> bars;
        for (size_t i = (size_t) r.downbeat + 8; i < r.beats.size(); i += 4) bars.push_back ((r.beats[i] - r.beats[i - 4]) / 4.0);
        if (bars.size() > 2) bars.pop_back();
        if (bars.size() >= 3)
        {
            auto srt = bars; std::sort (srt.begin(), srt.end());
            const double mdn = srt[srt.size() / 2];
            std::vector<double> dev; for (double b : bars) dev.push_back (std::abs (b - mdn));
            std::sort (dev.begin(), dev.end());
            const double mad = dev[dev.size() / 2];
            const double first = r.beats[(size_t) r.downbeat + 4], last = r.beats[(size_t) r.downbeat + 4 + 4 * bars.size()];
            const double drift = std::abs ((last - first) / (4.0 * bars.size()) - mdn) / mdn;
            r.steady = mad / mdn < 0.015 && drift < 0.01;
            if (r.steady) r.bpm = 60.0 * 4.0 * bars.size() / (last - first);
        }
    }
    if (! prog (0.7)) return r;

    // 2) akordy: chroma po dobach, sablony dur/mol, Viterbi
    if (what & 2)
    {
        double emax = 0; for (double e : energy) emax = juce::jmax (emax, e);
        // 24 akordov + ticho
        const int S = 25;
        auto emit = [&] (int i, int s) -> double
        {
            if (s == 24) return energy[(size_t) i] < emax * 0.002 ? 1.0 : 0.0;
            const int root = s % 12; const bool minor = s >= 12;
            const int iv3 = minor ? 3 : 4;
            const auto& c = chroma[(size_t) i];
            double dot = 0, nt = 0;
            for (int k = 0; k < 12; ++k)
            {
                const int rel = (k - root + 12) % 12;
                const double w = rel == 0 ? 1.0 : rel == iv3 ? 0.9 : rel == 7 ? 0.8 : 0.0;
                dot += w * c[(size_t) k]; nt += w * w;
            }
            return dot / std::sqrt (nt);
        };
        std::vector<std::vector<double>> sc ((size_t) nb, std::vector<double> (S));
        std::vector<std::vector<int>> bk ((size_t) nb, std::vector<int> (S, 0));
        const double change = 0.18;
        for (int s = 0; s < S; ++s) sc[0][(size_t) s] = emit (0, s);
        for (int i = 1; i < nb; ++i)
        {
            int bestPrev = 0;
            for (int s = 1; s < S; ++s) if (sc[(size_t) i - 1][(size_t) s] > sc[(size_t) i - 1][(size_t) bestPrev]) bestPrev = s;
            for (int s = 0; s < S; ++s)
            {
                const double stay = sc[(size_t) i - 1][(size_t) s], sw = sc[(size_t) i - 1][(size_t) bestPrev] - change;
                if (stay >= sw) { sc[(size_t) i][(size_t) s] = stay + emit (i, s); bk[(size_t) i][(size_t) s] = s; }
                else { sc[(size_t) i][(size_t) s] = sw + emit (i, s); bk[(size_t) i][(size_t) s] = bestPrev; }
            }
        }
        std::vector<int> path ((size_t) nb);
        int s = 0; for (int k = 1; k < S; ++k) if (sc[(size_t) nb - 1][(size_t) k] > sc[(size_t) nb - 1][(size_t) s]) s = k;
        for (int i = nb - 1; i >= 0; --i) { path[(size_t) i] = s; s = bk[(size_t) i][(size_t) s]; }
        // zmeny (akord aspon 2 doby)
        int cur = -1;
        for (int i = 0; i < nb; ++i)
        {
            const int st = path[(size_t) i];
            if (st == cur) continue;
            int runLen = 1; while (i + runLen < nb && path[(size_t) (i + runLen)] == st) ++runLen;
            if (runLen < 2 && cur >= 0) continue;
            cur = st;
            if (st == 24) continue;
            r.chords.push_back ({ r.beats[(size_t) i], juce::String (pcName (st % 12)) + (st >= 12 ? "m" : "") });
        }
        if (! prog (0.75)) return r;
    }

    // 3) bicie: kopak, virbel, hi-hat
    if (what & 4)
    {
        const auto k = flux (sp, 30.0, 120.0), s = flux (sp, 1000.0, 4500.0), h = flux (sp, 7000.0, 10500.0);
        auto conv = [&] (const std::vector<std::pair<double, float>>& pk, std::vector<AnalysisResult::Hit>& out)
        {
            std::vector<float> vs; for (auto& p : pk) vs.push_back (p.second);
            if (vs.empty()) return;
            auto srt = vs; std::sort (srt.begin(), srt.end());
            const float hiQ = srt[(size_t) (srt.size() * 8 / 10)];
            const double lat = sp.nfft * 0.5 / asr;   // stred ramca
            for (auto& p : pk) out.push_back ({ p.first + lat - (double) sp.hop / asr, p.second >= hiQ ? 1.0f : 0.7f });
        };
        const float sMax = juce::jmax (1.0e-9f, robustMax (s)), hMax = juce::jmax (1.0e-9f, robustMax (h));
        auto bandAt = [&] (const std::vector<float>& e, double t) { const int f = juce::jlimit (0, (int) e.size() - 1, (int) std::round (t * fps)); float m = 0; for (int q = -1; q <= 1; ++q) m = juce::jmax (m, e[(size_t) juce::jlimit (0, (int) e.size() - 1, f + q)]); return m; };
        const auto m = flux (sp, 150.0, 600.0);
        const float mMax = juce::jmax (1.0e-9f, robustMax (m)), kMax = juce::jmax (1.0e-9f, robustMax (k));
        auto sn = pickPeaks (s, fps, 0.09, 1.8f, 0.15f);
        // virbel: ma aj "telo" (150-600 Hz), hi-hat nie
        sn.erase (std::remove_if (sn.begin(), sn.end(), [&] (const auto& p) { return bandAt (m, p.first) / mMax < 0.3f || bandAt (s, p.first) / sMax < bandAt (k, p.first) / kMax; }), sn.end());
        auto kk = pickPeaks (k, fps, 0.09, 1.6f, 0.25f);
        // kopak: nizke pasmo prevlada nad pasmom virbla
        kk.erase (std::remove_if (kk.begin(), kk.end(), [&] (const auto& p) { return bandAt (k, p.first) / kMax < 1.2f * bandAt (s, p.first) / sMax; }), kk.end());
        conv (kk, r.kick);
        conv (sn, r.snare);
        conv (pickPeaks (h, fps, 0.05, 1.6f), r.hat);
    }
    prog (1.0);
    r.ok = true;
    return r;
}
