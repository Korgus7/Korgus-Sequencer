#include "AudioStore.h"

AudioStore::AudioStore (const juce::File& d) : dir (d)
{
    dir.createDirectory();
    formats.registerBasicFormats();
}

void AudioStore::setSampleRate (double sr)
{
    if (sr <= 0 || std::abs (sr - sampleRate) < 0.5) return;
    sampleRate = sr;
    cache.clear();
    stretched.clear();   // pri dalsom pouziti sa prevzorkuje znova
}

juce::String AudioStore::newId() const
{
    juce::Random r;
    for (;;)
    {
        juce::String id;
        for (int i = 0; i < 10; ++i) id << juce::String::charToString ((juce::juce_wchar) ("abcdefghijklmnopqrstuvwxyz0123456789"[r.nextInt (36)]));
        if (dir.findChildFiles (juce::File::findFiles, false, id + ".*").isEmpty()) return id;
    }
}

juce::File AudioStore::fileFor (const juce::String& id)
{
    if (id.isEmpty() || ! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")) return {};
    auto files = dir.findChildFiles (juce::File::findFiles, false, id + ".*");
    return files.isEmpty() ? juce::File() : files.getFirst();
}

juce::String AudioStore::importFile (const juce::File& src)
{
    if (! src.existsAsFile()) return {};
    {
        std::unique_ptr<juce::AudioFormatReader> test (formats.createReaderFor (src));
        if (test == nullptr || test->lengthInSamples <= 0) return {};
    }
    const auto id = newId();
    const auto dst = dir.getChildFile (id + src.getFileExtension().toLowerCase());
    if (! src.copyFileTo (dst)) return {};
    if (get (id) == nullptr) { dst.deleteFile(); return {}; }
    return id;
}

juce::String AudioStore::addRecording (const float* left, const float* right, int n, double sr)
{
    if (n <= 0 || left == nullptr) return {};
    const int ch = right != nullptr ? 2 : 1;
    const auto id = newId();
    const auto f = dir.getChildFile (id + ".wav");
    {
        std::unique_ptr<juce::OutputStream> os (f.createOutputStream().release());
        if (os == nullptr) return {};
        juce::WavAudioFormat wav;
        auto w = wav.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (sr).withNumChannels (ch).withBitsPerSample (24));
        if (w == nullptr) return {};
        const float* chans[] = { left, right };
        w->writeFromFloatArrays (chans, ch, n);
    }
    if (std::abs (sr - sampleRate) < 0.5)
    {
        auto b = std::make_shared<juce::AudioBuffer<float>> (ch, n);
        b->copyFrom (0, 0, left, n);
        if (right != nullptr) b->copyFrom (1, 0, right, n);
        cache[id] = b;
    }
    peaks.erase (id);
    return id;
}

void AudioStore::addFromData (const juce::String& id, const juce::String& name, const juce::MemoryBlock& data)
{
    if (data.getSize() == 0 || fileFor (id).existsAsFile()) return;
    if (! id.containsOnly ("abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789_-")) return;
    auto ext = juce::File::createLegalFileName (name).fromLastOccurrenceOf (".", true, false).toLowerCase();
    if (ext.length() < 2 || ext.length() > 6) ext = ".wav";
    dir.getChildFile (id + ext).replaceWithData (data.getData(), data.getSize());
}

AudioBufPtr AudioStore::decode (const juce::File& f)
{
    std::unique_ptr<juce::AudioFormatReader> r (formats.createReaderFor (f));
    if (r == nullptr || r->lengthInSamples <= 0 || r->lengthInSamples > (juce::int64) 48000 * 60 * 30) return nullptr;
    const int ch = juce::jlimit (1, 2, (int) r->numChannels);
    const int len = (int) r->lengthInSamples;
    juce::AudioBuffer<float> src (ch, len + 64);
    src.clear();
    r->read (&src, 0, len, 0, true, ch > 1);

    if (std::abs (r->sampleRate - sampleRate) < 0.5)
    {
        src.setSize (ch, len, true, false, true);
        return std::make_shared<juce::AudioBuffer<float>> (std::move (src));
    }

    const double ratio = r->sampleRate / sampleRate;
    const int outLen = (int) std::floor (len / ratio);
    auto out = std::make_shared<juce::AudioBuffer<float>> (ch, juce::jmax (1, outLen));
    for (int c = 0; c < ch; ++c)
    {
        juce::WindowedSincInterpolator ip;
        ip.process (ratio, src.getReadPointer (c), out->getWritePointer (c), outLen);
    }
    return out;
}

AudioBufPtr AudioStore::get (const juce::String& id)
{
    auto it = cache.find (id);
    if (it != cache.end()) return it->second;
    const auto f = fileFor (id);
    if (! f.existsAsFile()) return nullptr;
    auto b = decode (f);
    if (b != nullptr) cache[id] = b;
    return b;
}

juce::var AudioStore::peaksFor (const juce::String& id)
{
    auto it = peaks.find (id);
    if (it != peaks.end()) return it->second;
    auto b = get (id);
    if (b == nullptr) return {};

    const int B = 256, len = b->getNumSamples(), n = (len + B - 1) / B;
    juce::MemoryBlock mn ((size_t) n), mx ((size_t) n);
    auto* pn = static_cast<juce::int8*> (mn.getData());
    auto* px = static_cast<juce::int8*> (mx.getData());
    const float* c0 = b->getReadPointer (0);
    const float* c1 = b->getNumChannels() > 1 ? b->getReadPointer (1) : nullptr;
    for (int i = 0; i < n; ++i)
    {
        float a = 1, z = -1;
        const int e = juce::jmin (len, (i + 1) * B);
        for (int j = i * B; j < e; ++j)
        {
            const float v = c1 != nullptr ? (c0[j] + c1[j]) * 0.5f : c0[j];
            a = juce::jmin (a, v); z = juce::jmax (z, v);
        }
        pn[i] = (juce::int8) juce::jlimit (-127, 127, (int) std::floor (a * 127.0f));
        px[i] = (juce::int8) juce::jlimit (-127, 127, (int) std::ceil (z * 127.0f));
    }
    auto* o = new juce::DynamicObject();
    o->setProperty ("id", id);
    o->setProperty ("sr", sampleRate);
    o->setProperty ("B", B);
    o->setProperty ("full", len / sampleRate);
    o->setProperty ("mn", juce::Base64::toBase64 (mn.getData(), mn.getSize()));
    o->setProperty ("mx", juce::Base64::toBase64 (mx.getData(), mx.getSize()));
    juce::var v (o);
    peaks[id] = v;
    return v;
}

// rovnaky postup ako v povodnom Sekvenceri 92: obalka nastupov, autokorelacia, faza na dobu
bool AudioStore::detectTempo (const juce::String& id, double& bpmOut, double& phaseOut)
{
    auto b = get (id);
    if (b == nullptr) return false;
    const double sr = sampleRate;
    const int hop = juce::jmax (1, (int) std::round (sr / 344.0));
    const double fr = sr / hop;
    const float* c0 = b->getReadPointer (0);
    const float* c1 = b->getNumChannels() > 1 ? b->getReadPointer (1) : nullptr;
    const int n = juce::jmin (b->getNumSamples() / hop, (int) std::round (fr * 120));
    if (n < fr * 4) return false;

    std::vector<float> env ((size_t) n), amp ((size_t) n), onA ((size_t) n, 0.0f), raw ((size_t) n, 0.0f), on ((size_t) n, 0.0f);
    double lp = 0;
    const double k = std::exp (-2.0 * juce::MathConstants<double>::pi * 200.0 / sr);
    for (int f = 0; f < n; ++f)
    {
        double e = 0, el = 0;
        const int a = f * hop;
        for (int i = 0; i < hop; ++i)
        {
            const double v = c1 != nullptr ? (c0[a + i] + c1[a + i]) * 0.5 : c0[a + i];
            e += v * v;
            lp = lp * k + v * (1 - k);
            el += lp * lp;
        }
        env[(size_t) f] = (float) std::log (1e-7 + e);
        amp[(size_t) f] = (float) std::sqrt (el);
    }
    for (int f = 1; f < n; ++f)
    {
        onA[(size_t) f] = juce::jmax (0.0f, amp[(size_t) f] - amp[(size_t) f - 1]);
        raw[(size_t) f] = juce::jmax (0.0f, env[(size_t) f] - env[(size_t) f - 1]);
    }
    const int W = (int) std::round (fr * 0.25);
    std::vector<double> pre ((size_t) n + 1, 0.0);
    for (int f = 0; f < n; ++f) pre[(size_t) f + 1] = pre[(size_t) f] + raw[(size_t) f];
    for (int f = 0; f < n; ++f)
    {
        const int a = juce::jmax (0, f - W), z = juce::jmin (n, f + W + 1);
        const double m = (pre[(size_t) z] - pre[(size_t) a]) / (z - a);
        on[(size_t) f] = (float) juce::jmax (0.0, raw[(size_t) f] - m);
    }

    const int minL = (int) std::floor (fr * 60 / 200), maxL = (int) std::ceil (fr * 60 / 60);
    double best = 0; int bestL = 0;
    for (int L = minL; L <= maxL; ++L)
    {
        double sum = 0;
        for (int f = L; f < n; ++f) sum += on[(size_t) f] * on[(size_t) (f - L)];
        const double bpm = 60 * fr / L;
        const double w = std::exp (-0.5 * std::pow (std::log2 (bpm / 120) / 0.9, 2));
        if (sum * w > best) { best = sum * w; bestL = L; }
    }
    if (bestL == 0) return false;

    auto at = [n] (const std::vector<float>& x, double t) { const int i = (int) std::round (t); return i < n ? (double) x[(size_t) i] : 0.0; };
    const double b0 = 60 * fr / bestL;
    double bestS = -1, bestB = b0, meanS = 0; int bestPh = 0;
    for (double bb = b0 * 0.97; bb <= b0 * 1.03; bb += 0.05)
    {
        const double per = 60 * fr / bb;
        const int P = (int) std::ceil (per);
        double tot = 0, mx = -1; int mph = 0;
        for (int ph = 0; ph < P; ++ph)
        {
            double sum = 0;
            for (double t = ph; t < n; t += per) sum += at (on, t);
            tot += sum;
            if (sum > mx) { mx = sum; mph = ph; }
        }
        if (mx > bestS) { bestS = mx; bestB = bb; bestPh = mph; meanS = tot / P; }
    }
    if (! (meanS > 0) || bestS / meanS < 1.3) return false;
    {
        const double per = 60 * fr / bestB;
        const int P = (int) std::ceil (per);
        double mx = -1;
        for (int ph = 0; ph < P; ++ph)
        {
            double sum = 0;
            for (double t = ph; t < n; t += per) sum += at (onA, t);
            if (sum > mx) { mx = sum; bestPh = ph; }
        }
    }
    double bpm = bestB;
    if (bpm < 70) bpm *= 2;
    if (bpm > 180) bpm /= 2;
    bpm = std::abs (bpm - std::round (bpm)) < 0.2 ? std::round (bpm) : std::round (bpm * 10) / 10;
    const double T = 60 / bpm;
    bpmOut = bpm;
    phaseOut = std::fmod (bestPh / fr, T);
    return true;
}

//==============================================================================
AudioBufPtr AudioStore::peekStretched (const juce::String& id, double alpha) const
{
    auto it = stretched.find (stretchKey (id, alpha));
    return it != stretched.end() ? it->second : nullptr;
}

void AudioStore::putStretched (const juce::String& id, double alpha, AudioBufPtr b)
{
    // pre kazdu nahravku drzime len poslednu verziu (setri pamat)
    for (auto it = stretched.begin(); it != stretched.end();)
        if (it->first.upToFirstOccurrenceOf ("@", false, false) == id) it = stretched.erase (it); else ++it;
    stretched[stretchKey (id, alpha)] = std::move (b);
}

AudioBufPtr timeStretch (const juce::AudioBuffer<float>& in, double alpha)
{
    const int ch = in.getNumChannels(), n = in.getNumSamples();
    const int N = 2048, Hs = N / 2, delta = 512;
    const double Ha = Hs / alpha;
    const int outLen = juce::jmax (1, (int) (n * alpha));
    auto out = std::make_shared<juce::AudioBuffer<float>> (ch, outLen + N);
    out->clear();
    if (n < N * 2) { for (int c = 0; c < ch; ++c) out->copyFrom (c, 0, in, c, 0, juce::jmin (n, outLen)); out->setSize (ch, outLen, true); return out; }

    std::vector<float> win ((size_t) N);
    for (int i = 0; i < N; ++i) win[(size_t) i] = 0.5f - 0.5f * std::cos (juce::MathConstants<float>::twoPi * (float) i / (float) N);
    std::vector<float> mono ((size_t) n);
    for (int i = 0; i < n; ++i)
    {
        float v = 0;
        for (int c = 0; c < ch; ++c) v += in.getSample (c, i);
        mono[(size_t) i] = v;
    }

    int prev = 0;
    for (int k = 0;; ++k)
    {
        const int op = k * Hs;
        if (op >= outLen) break;
        int best = juce::jlimit (0, n - N - 1, (int) (k * Ha));
        if (k > 0)
        {
            // najlepsie pokracovanie predosleho ramca (podobnost priebehu)
            const int tgt = prev + Hs;
            if (tgt + Hs < n)
            {
                double bestC = -1.0e30;
                const int nominal = (int) (k * Ha);
                for (int d = -delta; d <= delta; d += 2)
                {
                    const int cand = nominal + d;
                    if (cand < 0 || cand + N >= n) continue;
                    double cc = 0, e = 1.0e-9;
                    for (int j = 0; j < Hs; j += 4)
                    {
                        const float a = mono[(size_t) (tgt + j)], b = mono[(size_t) (cand + j)];
                        cc += a * b; e += b * b;
                    }
                    const double score = cc / std::sqrt (e);
                    if (score > bestC) { bestC = score; best = cand; }
                }
            }
        }
        for (int c = 0; c < ch; ++c)
        {
            const float* src = in.getReadPointer (c) + best;
            float* dst = out->getWritePointer (c) + op;
            const int cnt = juce::jmin (N, n - best);
            for (int j = 0; j < cnt; ++j) dst[j] += src[j] * win[(size_t) j];
        }
        prev = best;
    }
    out->setSize (ch, outLen, true);
    return out;
}
