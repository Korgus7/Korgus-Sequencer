#include "Separator.h"
#include <juce_core/juce_core.h>
#include "../third_party/onnxruntime/onnxruntime_c_api.h"
#include "../third_party/pocketfft_hdronly.hpp"
#include <complex>
#include <vector>

namespace
{
    // parametre modelu Kim_Vocal_2 (MDX-Net)
    constexpr int nFft = 7680, hop = 1024, dimF = 3072, dimT = 256;
    constexpr int nBins = nFft / 2 + 1;
    constexpr int chunk = hop * (dimT - 1);
    constexpr int trim = nFft / 2;
    constexpr int gen = chunk - 2 * trim;
    constexpr float compensate = 1.009f;

    struct Ort
    {
        const OrtApi* api = nullptr;
        OrtEnv* env = nullptr;
        OrtSessionOptions* opts = nullptr;
        OrtSession* session = nullptr;
        OrtMemoryInfo* mem = nullptr;
        juce::String err;

        bool ok (OrtStatus* st)
        {
            if (st == nullptr) return true;
            err = juce::String (api->GetErrorMessage (st));
            api->ReleaseStatus (st);
            return false;
        }
        ~Ort()
        {
            if (api == nullptr) return;
            if (mem != nullptr) api->ReleaseMemoryInfo (mem);
            if (session != nullptr) api->ReleaseSession (session);
            if (opts != nullptr) api->ReleaseSessionOptions (opts);
            if (env != nullptr) api->ReleaseEnv (env);
        }
    };

    std::vector<float> hann()
    {
        std::vector<float> w ((size_t) nFft);
        for (int i = 0; i < nFft; ++i) w[(size_t) i] = 0.5f - 0.5f * std::cos (2.0f * juce::MathConstants<float>::pi * (float) i / (float) nFft);
        return w;
    }
}

bool separateVocals (const juce::File& ortLibrary, const juce::File& model,
                     const juce::AudioBuffer<float>& in44, juce::AudioBuffer<float>& vocals,
                     juce::String& error, const std::function<bool (double)>& progress)
{
    juce::DynamicLibrary lib;
    if (! lib.open (ortLibrary.getFullPathName())) { error = juce::String::fromUTF8 ("ONNX Runtime sa nedá načítať: ") + ortLibrary.getFullPathName(); return false; }
    using GetBase = const OrtApiBase* (ORT_API_CALL*) ();
    auto getBase = (GetBase) lib.getFunction ("OrtGetApiBase");
    if (getBase == nullptr) { error = juce::String::fromUTF8 ("ONNX Runtime: chýba OrtGetApiBase."); return false; }

    {
        Ort o;
        o.api = getBase()->GetApi (ORT_API_VERSION);
        if (o.api == nullptr) { error = juce::String::fromUTF8 ("ONNX Runtime má nesprávnu verziu."); return false; }
        auto& api = *o.api;
        if (! o.ok (api.CreateEnv (ORT_LOGGING_LEVEL_WARNING, "korgus", &o.env))
            || ! o.ok (api.CreateSessionOptions (&o.opts))
            || ! o.ok (api.SetIntraOpNumThreads (o.opts, juce::jmax (1, juce::SystemStats::getNumCpus() - 1)))
            || ! o.ok (api.SetSessionGraphOptimizationLevel (o.opts, ORT_ENABLE_ALL)))
        { error = o.err; return false; }
       #if JUCE_WINDOWS
        const auto path = model.getFullPathName();
        if (! o.ok (api.CreateSession (o.env, path.toWideCharPointer(), o.opts, &o.session))) { error = o.err; return false; }
       #else
        if (! o.ok (api.CreateSession (o.env, model.getFullPathName().toRawUTF8(), o.opts, &o.session))) { error = o.err; return false; }
       #endif
        if (! o.ok (api.CreateCpuMemoryInfo (OrtArenaAllocator, OrtMemTypeDefault, &o.mem))) { error = o.err; return false; }

        const int n = in44.getNumSamples();
        const float* src[2] = { in44.getReadPointer (0), in44.getReadPointer (in44.getNumChannels() > 1 ? 1 : 0) };
        const int pad = gen + trim - (n % gen);
        const int total = trim + n + pad;
        // zmes s okrajmi
        std::vector<float> mix[2];
        for (int c = 0; c < 2; ++c)
        {
            mix[c].assign ((size_t) total + (size_t) chunk, 0.0f);
            std::copy (src[c], src[c] + n, mix[c].begin() + trim);
        }
        vocals.setSize (2, n);
        vocals.clear();

        const auto win = hann();
        std::vector<float> input ((size_t) 4 * dimF * dimT);
        std::vector<float> padded ((size_t) chunk + nFft), frame ((size_t) nFft), acc ((size_t) chunk + nFft), wsum ((size_t) chunk + nFft);
        std::vector<std::complex<float>> spec ((size_t) nBins);
        const pocketfft::shape_t shape { (size_t) nFft };
        const pocketfft::stride_t sReal { (ptrdiff_t) sizeof (float) }, sCplx { (ptrdiff_t) sizeof (std::complex<float>) };
        const int64_t dims[4] = { 1, 4, dimF, dimT };
        const char* inNames[] = { "input" };
        const char* outNames[] = { "output" };

        const int chunks = (n + pad) / gen;
        int written = 0;
        for (int k = 0; k < chunks; ++k)
        {
            const int i0 = k * gen;
            // STFT oboch kanalov (center, reflect)
            for (int c = 0; c < 2; ++c)
            {
                const float* x = mix[c].data() + i0;
                const int h = nFft / 2;
                for (int i = 0; i < chunk; ++i) padded[(size_t) (i + h)] = x[i];
                for (int i = 1; i <= h; ++i) { padded[(size_t) (h - i)] = x[i]; padded[(size_t) (h + chunk - 1 + i)] = x[chunk - 1 - i]; }
                for (int t = 0; t < dimT; ++t)
                {
                    for (int j = 0; j < nFft; ++j) frame[(size_t) j] = padded[(size_t) (t * hop + j)] * win[(size_t) j];
                    pocketfft::r2c (shape, sReal, sCplx, 0, pocketfft::FORWARD, frame.data(), spec.data(), 1.0f);
                    for (int f = 0; f < dimF; ++f)
                    {
                        input[((size_t) (2 * c) * dimF + (size_t) f) * dimT + (size_t) t] = spec[(size_t) f].real();
                        input[((size_t) (2 * c + 1) * dimF + (size_t) f) * dimT + (size_t) t] = spec[(size_t) f].imag();
                    }
                }
            }
            OrtValue* inVal = nullptr;
            OrtValue* outVal = nullptr;
            if (! o.ok (api.CreateTensorWithDataAsOrtValue (o.mem, input.data(), input.size() * sizeof (float), dims, 4,
                                                             ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT, &inVal)))
            { error = o.err; return false; }
            const bool ran = o.ok (api.Run (o.session, nullptr, inNames, (const OrtValue* const*) &inVal, 1, outNames, 1, &outVal));
            api.ReleaseValue (inVal);
            if (! ran) { error = o.err; return false; }
            float* outp = nullptr;
            if (! o.ok (api.GetTensorMutableData (outVal, (void**) &outp))) { api.ReleaseValue (outVal); error = o.err; return false; }

            // ISTFT predpovedaneho spevu
            for (int c = 0; c < 2; ++c)
            {
                std::fill (acc.begin(), acc.end(), 0.0f);
                std::fill (wsum.begin(), wsum.end(), 0.0f);
                for (int t = 0; t < dimT; ++t)
                {
                    for (int f = 0; f < nBins; ++f)
                        spec[(size_t) f] = f < dimF ? std::complex<float> (outp[((size_t) (2 * c) * dimF + (size_t) f) * dimT + (size_t) t],
                                                                           outp[((size_t) (2 * c + 1) * dimF + (size_t) f) * dimT + (size_t) t])
                                                    : std::complex<float> (0.0f, 0.0f);
                    pocketfft::c2r (shape, sCplx, sReal, 0, pocketfft::BACKWARD, spec.data(), frame.data(), 1.0f / (float) nFft);
                    for (int j = 0; j < nFft; ++j)
                    {
                        acc[(size_t) (t * hop + j)] += frame[(size_t) j] * win[(size_t) j];
                        wsum[(size_t) (t * hop + j)] += win[(size_t) j] * win[(size_t) j];
                    }
                }
                const int h = nFft / 2;
                float* dst = vocals.getWritePointer (c);
                for (int i = trim; i < chunk - trim; ++i)
                {
                    const int outIdx = i0 + i - trim;   // poloha v povodnom zvuku
                    if (outIdx < 0 || outIdx >= n) continue;
                    dst[outIdx] = acc[(size_t) (i + h)] / juce::jmax (1.0e-8f, wsum[(size_t) (i + h)]) * compensate;
                }
            }
            api.ReleaseValue (outVal);
            written = juce::jmin (n, i0 + gen);
            if (progress && ! progress ((double) (k + 1) / (double) chunks)) { error = juce::String::fromUTF8 ("Zrušené."); return false; }
        }
        juce::ignoreUnused (written);
    }
    return true;
}

bool downloadFile (const juce::String& url, const juce::File& dest, juce::String& error,
                   const std::function<bool (double)>& progress)
{
    int status = 0;
    auto in = juce::URL (url).createInputStream (juce::URL::InputStreamOptions (juce::URL::ParameterHandling::inAddress)
                                                     .withConnectionTimeoutMs (30000)
                                                     .withNumRedirectsToFollow (10)
                                                     .withStatusCode (&status));
    if (in == nullptr || (status != 0 && status >= 400)) { error = juce::String::fromUTF8 ("Sťahovanie zlyhalo (") + juce::String (status) + "): " + url; return false; }
    dest.getParentDirectory().createDirectory();
    const auto tmp = dest.getSiblingFile (dest.getFileName() + ".part");
    tmp.deleteFile();
    {
        juce::FileOutputStream out (tmp);
        if (! out.openedOk()) { error = juce::String::fromUTF8 ("Nedá sa zapísať: ") + tmp.getFullPathName(); return false; }
        const auto totalLen = in->getTotalLength();
        juce::HeapBlock<char> buf (1 << 16);
        juce::int64 done = 0;
        for (;;)
        {
            const int r = in->read (buf, 1 << 16);
            if (r <= 0) break;
            out.write (buf, (size_t) r);
            done += r;
            if (progress && ! progress (totalLen > 0 ? (double) done / (double) totalLen : 0.0)) { out.flush(); tmp.deleteFile(); error = juce::String::fromUTF8 ("Zrušené."); return false; }
        }
        out.flush();
        if (totalLen > 0 && done < totalLen) { error = juce::String::fromUTF8 ("Sťahovanie sa prerušilo."); tmp.deleteFile(); return false; }
    }
    dest.deleteFile();
    return tmp.moveFileTo (dest);
}
