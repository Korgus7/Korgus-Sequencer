// Korgus Ozvena pre MPC (Knob FX): rovnaky algoritmus ako Korgus Ozvena v Sequenceri.
// K1 Delenie (Volne / 1/4 / 1/8 bodka / 1/8 / 1/8 triola / 1/16), K2 Opakovanie, K3 Mix; so SHIFT: Cas (pri Volne), Farba, Ping-pong.
#include "mpc_plugin.h"
#include "dsp/math.h"
#include <cmath>
#include <vector>
#include <algorithm>

class KorgusOzvena : public mpc::Plugin
{
public:
    enum Params { kSync, kTime, kFeedback, kTone, kMix, kPing, kNumParams };
    static mpc::PluginInfo makeInfo()
    {
        mpc::PluginInfo i;
        i.name = "Korgus Ozvena"; i.vendor = "Korgus"; i.uniqueId = MPC_FOURCC('K', 'g', 'O', 'z');
        i.version = 1000; i.isInstrument = false; i.numInputs = 2; i.numOutputs = 2; i.tailSamples = 0;
        return i;
    }
    KorgusOzvena() : mpc::Plugin (makeInfo())
    {
        using namespace mpc;
        addParam (choiceParam ("sync", "Delenie", { "Volne", "1/4", "1/8 bodka", "1/8", "1/8 triola", "1/16" }, 3));
        addParam (logParam ("time", "Cas", 10.0f, 2000.0f, 375.0f, "ms", 0));
        addParam (floatParam ("feedback", "Opakovanie", 0.0f, 95.0f, 35.0f, "%", 0));
        addParam (logParam ("tone", "Farba", 1000.0f, 20000.0f, 8000.0f, "Hz", 0));
        addParam (floatParam ("mix", "Mix", 0.0f, 100.0f, 25.0f, "%", 0));
        addParam (boolParam ("pingpong", "Ping-pong", false));
    }
    void prepare (double sampleRate, int) override
    {
        size_ = (int) (sampleRate * 2.1) + 4;
        dl_.assign ((size_t) size_, 0.0f); dr_.assign ((size_t) size_, 0.0f);
        w_ = 0; lp_[0] = lp_[1] = 0; jump_ = true;
    }
    void reset() override { std::fill (dl_.begin(), dl_.end(), 0.0f); std::fill (dr_.begin(), dr_.end(), 0.0f); lp_[0] = lp_[1] = 0; jump_ = true; }
    void process (mpc::ProcessContext& ctx) override
    {
        if (size_ < 8) return;
        const double sr = ctx.sampleRate;
        double ms = get (kTime);
        static const double beats[6] = { 0, 1.0, 0.75, 0.5, 1.0 / 3.0, 0.25 };
        const int sy = getInt (kSync);
        if (sy > 0 && sy < 6) { const double bpm = std::min (400.0, std::max (20.0, (double) ctx.transport.bpm)); ms = std::min (2000.0, 60000.0 / bpm * beats[sy]); }
        const float target = (float) std::min ((double) size_ - 3, std::max (1.0, ms * 0.001 * sr));
        const float f = get (kFeedback) * 0.01f, m = get (kMix) * 0.01f;
        const float a = (float) std::exp (-6.283185307 * get (kTone) / sr);
        const bool ping = getBool (kPing);
        if (jump_) { cur_ = target; jump_ = false; }
        const float* inL = ctx.inputs[0];
        const float* inR = ctx.numInputs > 1 ? ctx.inputs[1] : ctx.inputs[0];
        float* outL = ctx.outputs[0];
        float* outR = ctx.numOutputs > 1 ? ctx.outputs[1] : ctx.outputs[0];
        for (int i = 0; i < ctx.numFrames; ++i)
        {
            cur_ += (target - cur_) * 0.0005f;
            float rp = (float) w_ - cur_;
            if (rp < 0) rp += (float) size_;
            int i0 = (int) rp; const float fr = rp - (float) i0;
            if (i0 >= size_) i0 -= size_; if (i0 < 0) i0 = 0;
            const int i1 = (i0 + 1) % size_;
            float yl = dl_[(size_t) i0] + (dl_[(size_t) i1] - dl_[(size_t) i0]) * fr;
            float yr = dr_[(size_t) i0] + (dr_[(size_t) i1] - dr_[(size_t) i0]) * fr;
            lp_[0] = yl + (lp_[0] - yl) * a + 1.0e-20f; lp_[1] = yr + (lp_[1] - yr) * a + 1.0e-20f;
            lp_[0] -= 1.0e-20f; lp_[1] -= 1.0e-20f;
            if (std::fabs (lp_[0]) < 1.0e-15f) lp_[0] = 0; if (std::fabs (lp_[1]) < 1.0e-15f) lp_[1] = 0;
            yl = mpc::dsp::softClip (lp_[0]); yr = mpc::dsp::softClip (lp_[1]);
            const float xl = inL[i], xr = inR[i];
            if (ping) { dl_[(size_t) w_] = 0.5f * (xl + xr) + yr * f; dr_[(size_t) w_] = yl * f; }
            else { dl_[(size_t) w_] = xl + yl * f; dr_[(size_t) w_] = xr + yr * f; }
            outL[i] = xl * (1.0f - m) + yl * m;
            outR[i] = xr * (1.0f - m) + yr * m;
            if (++w_ >= size_) w_ = 0;
        }
    }
private:
    std::vector<float> dl_, dr_;
    int size_ = 0, w_ = 0; float lp_[2] {}, cur_ = 0; bool jump_ = true;
};
MPC_PLUGIN_ENTRY (KorgusOzvena)
