#include "MelodyAI.h"
#include <cmath>
#include <cstring>
#include <algorithm>
#include <random>
#include <thread>
#include <mutex>
#include <condition_variable>

namespace korgus
{
namespace
{
    // slovnik Anticipatory Music Transformer (arrival-time)
    constexpr int TIME_OFFSET = 0, MAX_TIME = 10000, DUR_OFFSET = 10000, MAX_DUR = 1000, NOTE_OFFSET = 11000;
    constexpr int MAX_NOTE = 128 * 129, REST = NOTE_OFFSET + MAX_NOTE, AUTOREGRESS = 55026;

    float halfToFloat (uint16_t h)
    {
        const uint32_t s = (uint32_t) (h & 0x8000) << 16, e = (h >> 10) & 0x1f, m = h & 0x3ff;
        uint32_t f;
        if (e == 0) { if (m == 0) f = s; else { int ee = -1; uint32_t mm = m; do { ++ee; mm <<= 1; } while ((mm & 0x400) == 0); f = s | ((uint32_t) (127 - 15 - ee) << 23) | ((mm & 0x3ff) << 13); } }
        else if (e == 31) f = s | 0x7f800000 | (m << 13);
        else f = s | ((e + 127 - 15) << 23) | (m << 13);
        float r; std::memcpy (&r, &f, 4); return r;
    }

    // jednoduchy zdielany fond vlakien pre nasobenie matic
    struct Pool
    {
        int n = 1; std::vector<std::thread> th; std::mutex mx; std::condition_variable cv, done;
        std::function<void (int, int)> job; int gen = 0, pending = 0; bool quit = false;
        Pool()
        {
            n = juce::jlimit (1, 8, (int) std::thread::hardware_concurrency() - 1);
            for (int i = 1; i < n; ++i) th.emplace_back ([this, i] { run (i); });
        }
        ~Pool() { { std::lock_guard<std::mutex> l (mx); quit = true; } cv.notify_all(); for (auto& t : th) t.join(); }
        void run (int idx)
        {
            int seen = 0;
            for (;;)
            {
                std::function<void (int, int)> j;
                { std::unique_lock<std::mutex> l (mx); cv.wait (l, [&] { return quit || gen != seen; }); if (quit) return; seen = gen; j = job; }
                j (idx, n);
                { std::lock_guard<std::mutex> l (mx); if (--pending == 0) done.notify_one(); }
            }
        }
        void parallel (std::function<void (int, int)> f)
        {
            if (n == 1) { f (0, 1); return; }
            { std::lock_guard<std::mutex> l (mx); job = f; pending = n - 1; ++gen; }
            cv.notify_all();
            f (0, n);
            std::unique_lock<std::mutex> l (mx); done.wait (l, [&] { return pending == 0; });
        }
    };
    Pool& pool() { static Pool p; return p; }

    // out[T,N] = in[T,K] * W[K,N] + b[N]
    void matmul (const float* in, int T, int K, const float* W, const float* b, int N, float* out)
    {
        pool().parallel ([=] (int idx, int cnt)
        {
            const int chunk = ((N + cnt - 1) / cnt + 15) & ~15;
            const int n0 = idx * chunk, n1 = std::min (N, n0 + chunk);
            if (n0 >= n1) return;
            for (int t = 0; t < T; ++t)
            {
                float* o = out + (size_t) t * N;
                for (int n = n0; n < n1; ++n) o[n] = b != nullptr ? b[n] : 0.0f;
                const float* x = in + (size_t) t * K;
                for (int k = 0; k < K; ++k)
                {
                    const float a = x[k]; if (a == 0.0f) continue;
                    const float* w = W + (size_t) k * N;
                    for (int n = n0; n < n1; ++n) o[n] += a * w[n];
                }
            }
        });
    }
    void layerNorm (const float* x, int T, int E, const float* g, const float* b, float* y)
    {
        for (int t = 0; t < T; ++t)
        {
            const float* xi = x + (size_t) t * E; float* yi = y + (size_t) t * E;
            double m = 0; for (int i = 0; i < E; ++i) m += xi[i]; m /= E;
            double v = 0; for (int i = 0; i < E; ++i) { const double d = xi[i] - m; v += d * d; } v /= E;
            const float r = (float) (1.0 / std::sqrt (v + 1e-5));
            for (int i = 0; i < E; ++i) yi[i] = ((float) (xi[i] - m)) * r * g[i] + b[i];
        }
    }
    inline float gelu (float x) { return 0.5f * x * (1.0f + std::tanh (0.7978845608f * (x + 0.044715f * x * x * x))); }
}

juce::String MelodyAI::load (const juce::File& f)
{
    unload();
    juce::FileInputStream in (f);
    if (! in.openedOk()) return "Model sa nedá otvoriť.";
    char magic[4]; in.read (magic, 4);
    if (std::memcmp (magic, "KAMT", 4) != 0) return "Neplatný súbor modelu.";
    int h[6]; in.read (h, sizeof (h));
    if (h[0] != 1) return "Nepodporovaná verzia modelu.";
    nLayer = h[1]; nHead = h[2]; nEmbd = h[3]; nVocab = h[4]; nCtx = h[5];
    const int E = nEmbd;
    std::vector<uint16_t> buf;
    auto rd = [&] (std::vector<float>& dst, size_t count) -> bool
    {
        buf.resize (count); dst.resize (count);
        if (in.read (buf.data(), (int) (count * 2)) != (int) (count * 2)) return false;
        for (size_t i = 0; i < count; ++i) dst[i] = halfToFloat (buf[i]);
        return true;
    };
    bool ok = rd (wte, (size_t) nVocab * E) && rd (wpe, (size_t) nCtx * E);
    layers.resize ((size_t) nLayer);
    for (auto& L : layers)
        ok = ok && rd (L.ln1g, E) && rd (L.ln1b, E) && rd (L.wqkv, (size_t) E * 3 * E) && rd (L.bqkv, 3 * E) && rd (L.wo, (size_t) E * E) && rd (L.bo, E)
                && rd (L.ln2g, E) && rd (L.ln2b, E) && rd (L.wfc, (size_t) E * 4 * E) && rd (L.bfc, 4 * E) && rd (L.wpr, (size_t) 4 * E * E) && rd (L.bpr, E);
    ok = ok && rd (lnfg, E) && rd (lnfb, E);
    if (! ok) { unload(); return "Súbor modelu je poškodený (neúplný)."; }
    resetCache();
    return {};
}

void MelodyAI::unload()
{
    nLayer = 0; wte.clear(); wte.shrink_to_fit(); wpe.clear(); layers.clear(); layers.shrink_to_fit();
    kc.clear(); kc.shrink_to_fit(); vc.clear(); vc.shrink_to_fit(); cacheLen = 0;
}

void MelodyAI::resetCache()
{
    kc.assign ((size_t) nLayer * nCtx * nEmbd, 0.0f); vc.assign ((size_t) nLayer * nCtx * nEmbd, 0.0f); cacheLen = 0;
}

void MelodyAI::forward (const int* toks, int T)
{
    const int E = nEmbd, H = nHead, D = E / H;
    if (cacheLen + T > nCtx) T = nCtx - cacheLen;
    if (T <= 0) return;
    std::vector<float> x ((size_t) T * E), h ((size_t) T * E), qkv ((size_t) T * 3 * E), att ((size_t) T * E), tmp ((size_t) T * E), fc ((size_t) T * 4 * E);
    for (int t = 0; t < T; ++t)
    {
        const int tok = juce::jlimit (0, nVocab - 1, toks[t]);
        for (int i = 0; i < E; ++i) x[(size_t) t * E + i] = wte[(size_t) tok * E + i] + wpe[(size_t) (cacheLen + t) * E + i];
    }
    const float scale = 1.0f / std::sqrt ((float) D);
    for (int l = 0; l < nLayer; ++l)
    {
        auto& L = layers[(size_t) l];
        layerNorm (x.data(), T, E, L.ln1g.data(), L.ln1b.data(), h.data());
        matmul (h.data(), T, E, L.wqkv.data(), L.bqkv.data(), 3 * E, qkv.data());
        float* K = kc.data() + (size_t) l * nCtx * E; float* V = vc.data() + (size_t) l * nCtx * E;
        for (int t = 0; t < T; ++t)
        {
            std::memcpy (K + (size_t) (cacheLen + t) * E, qkv.data() + (size_t) t * 3 * E + E, sizeof (float) * (size_t) E);
            std::memcpy (V + (size_t) (cacheLen + t) * E, qkv.data() + (size_t) t * 3 * E + 2 * E, sizeof (float) * (size_t) E);
        }
        // pozornost (kauzalna), paralelne podla hlav
        pool().parallel ([&, T] (int idx, int cnt)
        {
            std::vector<float> sc ((size_t) (cacheLen + T));
            for (int hd = idx; hd < H; hd += cnt)
                for (int t = 0; t < T; ++t)
                {
                    const float* q = qkv.data() + (size_t) t * 3 * E + hd * D;
                    const int len = cacheLen + t + 1;
                    float mx = -1e30f;
                    for (int j = 0; j < len; ++j)
                    {
                        const float* k = K + (size_t) j * E + hd * D; float s = 0;
                        for (int d = 0; d < D; ++d) s += q[d] * k[d];
                        s *= scale; sc[(size_t) j] = s; mx = std::max (mx, s);
                    }
                    float sum = 0; for (int j = 0; j < len; ++j) { sc[(size_t) j] = std::exp (sc[(size_t) j] - mx); sum += sc[(size_t) j]; }
                    float* o = att.data() + (size_t) t * E + hd * D;
                    for (int d = 0; d < D; ++d) o[d] = 0;
                    for (int j = 0; j < len; ++j)
                    {
                        const float w = sc[(size_t) j] / sum; const float* v = V + (size_t) j * E + hd * D;
                        for (int d = 0; d < D; ++d) o[d] += w * v[d];
                    }
                }
        });
        matmul (att.data(), T, E, L.wo.data(), L.bo.data(), E, tmp.data());
        for (size_t i = 0; i < x.size(); ++i) x[i] += tmp[i];
        layerNorm (x.data(), T, E, L.ln2g.data(), L.ln2b.data(), h.data());
        matmul (h.data(), T, E, L.wfc.data(), L.bfc.data(), 4 * E, fc.data());
        for (auto& v : fc) v = gelu (v);
        matmul (fc.data(), T, 4 * E, L.wpr.data(), L.bpr.data(), E, tmp.data());
        for (size_t i = 0; i < x.size(); ++i) x[i] += tmp[i];
    }
    cacheLen += T;
    lastHidden.resize ((size_t) E);
    layerNorm (x.data() + (size_t) (T - 1) * E, 1, E, lnfg.data(), lnfb.data(), lastHidden.data());
}

float MelodyAI::logit (int token) const
{
    const float* w = wte.data() + (size_t) token * nEmbd; float s = 0;
    for (int i = 0; i < nEmbd; ++i) s += w[i] * lastHidden[(size_t) i];
    return s;
}

std::vector<float> MelodyAI::logitsFor (const std::vector<int>& tokens)
{
    resetCache(); forward (tokens.data(), (int) tokens.size());
    std::vector<float> r ((size_t) nVocab);
    for (int i = 0; i < nVocab; ++i) r[(size_t) i] = logit (i);
    return r;
}

std::vector<AiNote> MelodyAI::continueMelody (const std::vector<AiNote>& promptIn, const Params& p,
                                              std::function<void (float)> progress, const std::atomic<bool>& cancel)
{
    std::vector<AiNote> res;
    if (! loaded() || promptIn.empty()) return res;
    // udalosti (cas v 10 ms, dlzka, nota); najviac poslednych 200 udalosti
    struct Ev { int t, d, n; };
    std::vector<Ev> ev;
    for (auto& a : promptIn)
        ev.push_back ({ (int) std::lround (a.step * p.stepSec * 100.0), juce::jlimit (1, MAX_DUR - 1, (int) std::lround (a.len * p.stepSec * 100.0)), p.instrument * 128 + juce::jlimit (0, 127, a.pitch) });
    std::sort (ev.begin(), ev.end(), [] (const Ev& a, const Ev& b) { return a.t < b.t || (a.t == b.t && a.n < b.n); });
    if (ev.size() > 200) ev.erase (ev.begin(), ev.end() - 200);
    const int base = ev.front().t;
    for (auto& e : ev) e.t -= base;
    int startT = (int) std::lround (p.promptEndStep * p.stepSec * 100.0) - base;
    startT = std::max (startT, ev.back().t);
    const int endT = startT + (int) std::lround (p.genSteps * p.stepSec * 100.0);
    if (endT >= MAX_TIME) return res;
    // tokeny s vyplnou REST (najviac 1 s medzery), ako v originale (ops.pad)
    std::vector<int> toks { AUTOREGRESS };
    int prev = 0;
    for (auto& e : ev)
    {
        while (e.t > prev + 100) { prev += 100; toks.insert (toks.end(), { TIME_OFFSET + prev, DUR_OFFSET, REST }); }
        toks.insert (toks.end(), { TIME_OFFSET + e.t, DUR_OFFSET + e.d, NOTE_OFFSET + e.n }); prev = e.t;
    }
    while (startT > prev + 100) { prev += 100; toks.insert (toks.end(), { TIME_OFFSET + prev, DUR_OFFSET, REST }); }
    if ((int) toks.size() > nCtx - 60) return res;
    int curT = prev;
    resetCache();
    forward (toks.data(), (int) toks.size());
    std::mt19937 rng (p.seed);
    // vzorkovanie z povolenych tokenov (nucleus)
    auto sample = [&] (int lo, int hi, std::function<bool (int)> allow) -> int
    {
        std::vector<std::pair<float, int>> c;
        for (int t = lo; t < hi; ++t) if (! allow || allow (t)) c.push_back ({ logit (t), t });
        if (c.empty()) return -1;
        float mx = -1e30f; for (auto& x : c) mx = std::max (mx, x.first);
        double sum = 0; for (auto& x : c) { x.first = std::exp (x.first - mx); sum += x.first; }
        std::sort (c.begin(), c.end(), [] (auto& a, auto& b) { return a.first > b.first; });
        double cum = 0; size_t keep = c.size();
        for (size_t i = 0; i < c.size(); ++i) { cum += c[i].first / sum; if (cum >= p.topP) { keep = i + 1; break; } }
        double tot = 0; for (size_t i = 0; i < keep; ++i) tot += c[i].first;
        std::uniform_real_distribution<double> u (0.0, tot); double r = u (rng);
        for (size_t i = 0; i < keep; ++i) { r -= c[i].first; if (r <= 0) return c[i].second; }
        return c[keep - 1].second;
    };
    const int nLo = NOTE_OFFSET + p.instrument * 128 + juce::jlimit (0, 127, p.pitchLo), nHi = NOTE_OFFSET + p.instrument * 128 + juce::jlimit (0, 127, p.pitchHi);
    while (! cancel.load() && cacheLen + 3 < nCtx)
    {
        // cas: nie do minulosti, najviac 4 s dopredu
        const int tt = sample (TIME_OFFSET + curT, TIME_OFFSET + std::min (MAX_TIME, curT + 400), nullptr);
        if (tt < 0) break;
        const int nt = tt - TIME_OFFSET;
        if (nt >= endT) break;
        forward (&tt, 1);
        const int dd = sample (DUR_OFFSET, DUR_OFFSET + MAX_DUR, nullptr);
        forward (&dd, 1);
        const int nn = sample (nLo, REST + 1, [&] (int t) { return t <= nHi || t == REST; });
        forward (&nn, 1);
        curT = nt;
        if (nn != REST && dd > DUR_OFFSET)
        {
            AiNote a; a.step = (nt + base) / 100.0 / p.stepSec; a.len = std::max (0.5, (dd - DUR_OFFSET) / 100.0 / p.stepSec);
            a.pitch = (nn - NOTE_OFFSET) % 128; res.push_back (a);
        }
        if (progress) progress ((float) (nt - startT) / (float) std::max (1, endT - startT));
    }
    return res;
}
} // namespace korgus
