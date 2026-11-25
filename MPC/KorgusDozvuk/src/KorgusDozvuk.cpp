// Korgus Dozvuk pre MPC (Knob FX): dozvuk typu Freeverb (8 hrebenovych a 4 prechodove filtre na kanal), ako v Sequenceri.
// K1 Velkost, K2 Tlmenie, K3 Mix; so SHIFT: Predozvuk, Sirka, Basy prec (orezanie basov v dozvuku).
#include "mpc_plugin.h"
#include "dsp/math.h"
#include "dsp/onepole.h"
#include <cmath>
#include <vector>
#include <algorithm>

namespace
{
    struct Comb
    {
        std::vector<float> b; int i = 0; float store = 0;
        void init (int n) { b.assign ((size_t) std::max (1, n), 0.0f); i = 0; store = 0; }
        float process (float x, float fb, float d1, float d2)
        {
            const float y = b[(size_t) i];
            store = y * d2 + store * d1;
            b[(size_t) i] = x + store * fb;
            if (++i >= (int) b.size()) i = 0;
            return y;
        }
    };
    struct Allpass
    {
        std::vector<float> b; int i = 0;
        void init (int n) { b.assign ((size_t) std::max (1, n), 0.0f); i = 0; }
        float process (float x)
        {
            const float bo = b[(size_t) i];
            const float y = -x + bo;
            b[(size_t) i] = x + bo * 0.5f;
            if (++i >= (int) b.size()) i = 0;
            return y;
        }
    };
}

class KorgusDozvuk : public mpc::Plugin
{
public:
    enum Params { kSize, kDamp, kMix, kPre, kWidth, kLowCut, kNumParams };
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Korgus Dozvuk"; i.vendor = "Korgus"; i.uniqueId = MPC_FOURCC('K', 'g', 'D', 'z');
        i.version = 1000; i.isInstrument = false; i.numInputs = 2; i.numOutputs = 2; i.tailSamples = 0;
        return i;
    }
    KorgusDozvuk() : mpc::Plugin (makeInfo())
    {
        using namespace mpc;
        addParam (floatParam ("size", "Velkost", 0.0f, 100.0f, 60.0f, "%", 0));
        addParam (floatParam ("damp", "Tlmenie", 0.0f, 100.0f, 45.0f, "%", 0));
        addParam (floatParam ("mix", "Mix", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam (floatParam ("predelay", "Predozvuk", 0.0f, 100.0f, 10.0f, "ms", 0));
        addParam (floatParam ("width", "Sirka", 0.0f, 100.0f, 100.0f, "%", 0));
        addParam (logParam ("lowcut", "Basy prec", 20.0f, 1000.0f, 150.0f, "Hz", 0));
    }
    void prepare (double sr, int) override
    {
        static const int combT[8] = { 1116, 1188, 1277, 1356, 1422, 1491, 1557, 1617 };
        static const int apT[4] = { 556, 441, 341, 225 };
        const double k = sr / 44100.0; const int spread = 23;
        for (int c = 0; c < 8; ++c) { cl_[c].init ((int) (combT[c] * k)); cr_[c].init ((int) ((combT[c] + spread) * k)); }
        for (int a = 0; a < 4; ++a) { al_[a].init ((int) (apT[a] * k)); ar_[a].init ((int) ((apT[a] + spread) * k)); }
        pre_.assign ((size_t) (sr * 0.11) + 2, 0.0f); pw_ = 0;
        hpL_.reset(); hpR_.reset();
    }
    void reset() override
    {
        for (auto& c : cl_) std::fill (c.b.begin(), c.b.end(), 0.0f);
        for (auto& c : cr_) std::fill (c.b.begin(), c.b.end(), 0.0f);
        for (auto& a : al_) std::fill (a.b.begin(), a.b.end(), 0.0f);
        for (auto& a : ar_) std::fill (a.b.begin(), a.b.end(), 0.0f);
        std::fill (pre_.begin(), pre_.end(), 0.0f);
    }
    void process (mpc::ProcessContext& ctx) override
    {
        if (pre_.size() < 4) return;
        const float room = 0.7f + get (kSize) * 0.01f * 0.28f;          // spatna vazba hrebenov
        const float d1 = get (kDamp) * 0.01f * 0.4f, d2 = 1.0f - d1;
        const float mix = get (kMix) * 0.01f, w = get (kWidth) * 0.01f;
        const float wet1 = 0.5f + 0.5f * w, wet2 = 0.5f - 0.5f * w;
        hpL_.setCutoff (get (kLowCut), ctx.sampleRate); hpR_.setCutoff (get (kLowCut), ctx.sampleRate);
        const int pd = std::min ((int) pre_.size() - 2, (int) (get (kPre) * 0.001 * ctx.sampleRate));
        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        const int ps = (int) pre_.size();
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            pre_[(size_t) pw_] = (xl + xr) * 0.015f;
            int rp = pw_ - pd; if (rp < 0) rp += ps;
            const float x = pre_[(size_t) rp];
            if (++pw_ >= ps) pw_ = 0;
            float l = 0, r = 0;
            for (int c = 0; c < 8; ++c) { l += cl_[c].process (x, room, d1, d2); r += cr_[c].process (x, room, d1, d2); }
            for (int a = 0; a < 4; ++a) { l = al_[a].process (l); r = ar_[a].process (r); }
            l = hpL_.process (l); r = hpR_.process (r);
            const float yl = l * wet1 + r * wet2, yr = r * wet1 + l * wet2;
            outL[i] = xl * (1.0f - mix * 0.5f) + yl * mix * 3.0f;
            outR[i] = xr * (1.0f - mix * 0.5f) + yr * mix * 3.0f;
        }
    }
private:
    Comb cl_[8], cr_[8]; Allpass al_[4], ar_[4];
    std::vector<float> pre_; int pw_ = 0;
    mpc::dsp::OnePoleHP hpL_, hpR_;
};
MPC_PLUGIN_ENTRY (KorgusDozvuk)
