// Korgus Kompresor pre MPC (Knob FX): rovnaky algoritmus ako Korgus Kompresor v Sequenceri.
// K1 Prah, K2 Pomer, K3 Zisk; so SHIFT: Nabeh, Navrat, Mix.
#include "mpc_plugin.h"
#include "dsp/math.h"
#include <cmath>
#include <algorithm>

class KorgusKompresor : public mpc::Plugin
{
public:
    enum Params { kThr, kRatio, kAttack, kRelease, kMakeup, kMix, kNumParams };
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Korgus Kompresor"; i.vendor = "Korgus"; i.uniqueId = MPC_FOURCC('K', 'g', 'K', 'o');
        i.version = 1000; i.isInstrument = false; i.numInputs = 2; i.numOutputs = 2; i.tailSamples = 0;
        return i;
    }
    KorgusKompresor() : mpc::Plugin (makeInfo())
    {
        using namespace mpc;
        addParam (floatParam ("threshold", "Prah", -60.0f, 0.0f, -18.0f, "dB", 1));
        addParam (logParam ("ratio", "Pomer", 1.0f, 20.0f, 4.0f, ":1", 1));
        addParam (logParam ("attack", "Nabeh", 0.1f, 100.0f, 10.0f, "ms", 1));
        addParam (logParam ("release", "Navrat", 10.0f, 1000.0f, 120.0f, "ms", 0));
        addParam (floatParam ("makeup", "Zisk", 0.0f, 24.0f, 0.0f, "dB", 1));
        addParam (floatParam ("mix", "Mix", 0.0f, 100.0f, 100.0f, "%", 0));
    }
    void prepare (double, int) override { env_ = 0; gain_ = 1; }
    void reset() override { env_ = 0; gain_ = 1; }
    void process (mpc::ProcessContext& ctx) override
    {
        const double sr = ctx.sampleRate;
        const float aA = (float) std::exp (-1.0 / (get (kAttack) * 0.001 * sr));
        const float aR = (float) std::exp (-1.0 / (get (kRelease) * 0.001 * sr));
        const float t = get (kThr), slope = 1.0f - 1.0f / std::max (1.0f, get (kRatio)), knee = 6.0f;
        const float mk = mpc::dsp::dbToGain (get (kMakeup)), mix = get (kMix) * 0.01f;
        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            const float xl = inL[i], xr = inR[i];
            const float x = std::max (std::fabs (xl), std::fabs (xr));
            env_ = x > env_ ? x + (env_ - x) * aA : x + (env_ - x) * aR;
            const float lev = env_ > 1.0e-6f ? 20.0f * std::log10 (env_) : -120.0f, over = lev - t;
            float gr = 0;
            if (over > knee * 0.5f) gr = over * slope;
            else if (over > -knee * 0.5f) { const float k = over + knee * 0.5f; gr = slope * k * k / (2.0f * knee); }
            const float g = mpc::dsp::dbToGain (-gr) * mk;
            gain_ += (g - gain_) * 0.02f;   // bez cvakania pri zmene gombikov
            outL[i] = xl + (xl * gain_ - xl) * mix;
            outR[i] = xr + (xr * gain_ - xr) * mix;
        }
    }
private:
    float env_ = 0, gain_ = 1;
};
MPC_PLUGIN_ENTRY (KorgusKompresor)
