#include "PluginProcessor.h"
#include "PluginEditor.h"

using namespace nova;

NovaProcessor::NovaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, &undo, "NOVA", createLayout())
{
    for (auto& s : paramSpecs()) raw[s.id] = params.getRawParameterValue (s.id);
    params.addParameterListener ("preset", this);
    // na zaciatku tovarensky zvuk 0
    PatchData p;
    if (presets.load ("f:" + factoryPatches()[0].name, p)) { applyPatch (p); currentKey = "f:" + p.name; }
    undo.clearUndoHistory();
    startTimer (400);
}

NovaProcessor::~NovaProcessor() { stopTimer(); params.removeParameterListener ("preset", this); cancelPendingUpdate(); }

bool NovaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void NovaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
}

void NovaProcessor::fillSettings (Settings& s)
{
    settingsFrom (s, [this] (const juce::String& id) { return raw[id]->load(); });
    s.fx.bpm = 120; s.playing = false; s.ppq = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) s.fx.bpm = *b;
            s.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) s.ppq = *q;
        }
}

void NovaProcessor::settingsFrom (Settings& s, const std::function<float (const juce::String&)>& g)
{
    for (int li = 0; li < 2; ++li)
    {
        const juce::String p = li == 0 ? "a_" : "b_";
        auto& L = s.L[li];
        L.on = g (p + "on") > 0.5f; L.wave = (int) g (p + "wave"); L.oct = g (p + "oct"); L.semi = g (p + "semi"); L.fine = g (p + "fine");
        L.uni = (int) g (p + "uni"); L.spread = g (p + "spread"); L.level = g (p + "level"); L.pan = g (p + "pan"); L.color = g (p + "color");
        L.ratio = ratioValue ((int) g (p + "ratio")); L.ftype = (int) g (p + "ftype"); L.f24 = g (p + "f24") > 0.5f;
        L.cut = g (p + "cut"); L.reso = g (p + "reso"); L.fenv = g (p + "fenv");
        L.fa = g (p + "fa"); L.fd = g (p + "fd"); L.fs = g (p + "fs"); L.fr = g (p + "fr");
        L.aa = g (p + "aa"); L.ad = g (p + "ad"); L.as = g (p + "as"); L.ar = g (p + "ar");
        L.lshape = (int) g (p + "lshape"); L.ltgt = (int) g (p + "ltgt"); L.lrate = g (p + "lrate"); L.ldepth = g (p + "ldepth"); L.ldelay = g (p + "ldelay");
        L.src = (int) g (p + "src"); L.root = (int) g (p + "root"); L.loop = g (p + "loop") > 0.5f;
        L.eqLow = g (p + "eql"); L.eqMid = g (p + "eqm"); L.eqMidF = g (p + "eqmf"); L.eqHigh = g (p + "eqh");
    }
    auto& f = s.fx;
    f.tube = g ("tube"); f.compOn = g ("comp_on") > 0.5f; f.comp = g ("comp");
    f.driveOn = g ("drive_on") > 0.5f; f.drive = g ("drive");
    f.chOn = g ("ch_on") > 0.5f; f.chRate = g ("ch_rate"); f.chDepth = g ("ch_depth"); f.chMix = g ("ch_mix");
    f.dlOn = g ("dl_on") > 0.5f; f.dlSync = (int) g ("dl_sync"); f.dlTime = g ("dl_time"); f.dlFb = g ("dl_fb"); f.dlTone = g ("dl_tone"); f.dlMix = g ("dl_mix"); f.dlPP = g ("dl_pp") > 0.5f;
    f.rvOn = g ("rv_on") > 0.5f; f.rvSize = g ("rv_size"); f.rvDamp = g ("rv_damp"); f.rvMix = g ("rv_mix");
    f.width = g ("fx_width"); f.punch = g ("fx_punch"); f.eqLow = g ("eq_low"); f.eqMid = g ("eq_mid"); f.eqMidF = g ("eq_midf"); f.eqHigh = g ("eq_high");
    f.rvEqLow = g ("rv_eql"); f.rvEqMid = g ("rv_eqm"); f.rvEqMidF = g ("rv_eqmf"); f.rvEqHigh = g ("rv_eqh");
    f.mfOn = g ("mf_on") > 0.5f; f.mfType = (int) g ("mf_type"); f.mfCut = g ("mf_cut"); f.mfReso = g ("mf_reso"); f.mfDrive = g ("mf_drive");
    f.gainDb = g ("p_gain"); f.volumeDb = g ("volume");
    s.arpOn = g ("arp_on") > 0.5f; s.arpHold = g ("arp_hold") > 0.5f; s.arpPat = (int) g ("arp_pat"); s.arpSpeed = (int) g ("arp_speed");
    s.arpOct = (int) g ("arp_oct"); s.arpLen = g ("arp_len");
    s.mono = g ("mono") > 0.5f; s.glide = g ("glide"); s.veltrack = g ("veltrack"); s.morphX = g ("morph_x"); s.morphY = g ("morph_y");
    s.bright = g ("bright"); s.attack = g ("attack"); s.release = g ("release"); s.space = g ("space"); s.width = g ("width");
}

void NovaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    keyboard.processNextMidiBuffer (midi, 0, buffer.getNumSamples(), true);
    Settings s;
    fillSettings (s);
    synth.process (buffer, midi, s);
}

// ------------------------------------------------------------- zvuky
PatchData NovaProcessor::currentPatch()
{
    PatchData p;
    p.name = currentName; p.category = currentCategory;
    for (auto& s : paramSpecs()) if (isPatchParam (s.id)) p.values[s.id] = raw[s.id]->load();
    for (int i = 0; i < 2; ++i) { p.sampleFlac[i] = smpFlac[i]; p.sampleName[i] = smpName[i]; }
    return p;
}

void NovaProcessor::applyPatch (const PatchData& p)
{
    undo.beginNewTransaction (juce::String::fromUTF8 ("Zvuk"));
    for (auto& s : paramSpecs())
    {
        if (! isPatchParam (s.id)) continue;
        auto it = p.values.find (s.id);
        const float v = it != p.values.end() ? it->second : s.def;
        if (auto* prm = params.getParameter (s.id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v));
    }
    for (int i = 0; i < 2; ++i) setSample (i, p.sampleFlac[i], p.sampleName[i]);
    currentName = p.name; currentCategory = p.category;
    synth.fadeAll();
    undo.beginNewTransaction();
    if (onPatchChanged) onPatchChanged();
}

bool NovaProcessor::loadPreset (const juce::String& key)
{
    PatchData p;
    if (! presets.load (key, p)) return false;
    applyPatch (p);
    currentKey = key;
    // pri tovarenskom zvuku nastavime aj vyber v jednoduchej verzii (bez opatovneho nacitania)
    if (const auto* e = presets.find (key); e != nullptr && e->factory)
        if (auto* prm = params.getParameter ("preset"))
        {
            lastPresetParam = e->factoryIndex;
            if (juce::roundToInt (raw["preset"]->load()) != e->factoryIndex)
            {
                pendingPreset = -2;
                prm->setValueNotifyingHost (prm->convertTo0to1 ((float) e->factoryIndex));
            }
        }
    if (onPatchChanged) onPatchChanged();
    return true;
}

juce::String NovaProcessor::savePreset (const juce::String& name, const juce::String& category)
{
    auto p = currentPatch();
    p.name = name; p.category = category;
    currentKey = presets.save (p);
    currentName = name; currentCategory = category;
    if (onPatchChanged) onPatchChanged();
    return currentKey;
}

void NovaProcessor::parameterChanged (const juce::String& id, float v)
{
    if (id != "preset" || restoring) return;
    if (pendingPreset.load() == -2) { pendingPreset = -1; return; }   // zmenu sme spravili sami
    pendingPreset = juce::roundToInt (v);
    triggerAsyncUpdate();
}

void NovaProcessor::handleAsyncUpdate()
{
    const int idx = pendingPreset.exchange (-1);
    if (idx < 0 || idx == lastPresetParam && currentKey == "f:" + factoryPatches()[(size_t) juce::jlimit (0, (int) factoryPatches().size() - 1, idx)].name) return;
    if (! juce::isPositiveAndBelow (idx, (int) factoryPatches().size())) return;
    lastPresetParam = idx;
    PatchData p;
    const auto key = "f:" + factoryPatches()[(size_t) idx].name;
    if (presets.load (key, p)) { applyPatch (p); currentKey = key; if (onPatchChanged) onPatchChanged(); }
}

// ------------------------------------------------------------- Prekvap ma
namespace
{
    // nahodny, ale hudobny zvuk: najprv typ (pad, pluck, ...), potom hodnoty v rozumnych medziach
    PatchData randomPatch (juce::Random& r, bool pianoOk)
    {
        PatchData p;
        auto& v = p.values;
        auto R = [&r] (float a, float b) { return a + (b - a) * r.nextFloat(); };
        auto LR = [&r] (float a, float b) { return a * std::pow (b / a, r.nextFloat()); };
        auto I = [&r] (int a, int b) { return (float) (a + r.nextInt (b - a + 1)); };
        auto pick = [&r] (std::initializer_list<float> l) { return *(l.begin() + r.nextInt ((int) l.size())); };
        auto chance = [&r] (float c) { return r.nextFloat() < c; };
        auto copyEnv = [&v] { for (auto k : { "aa", "ad", "as", "ar" }) v[juce::String ("b_") + k] = v[juce::String ("a_") + k]; };

        int type = r.nextInt (pianoOk ? 8 : 7);
        v["veltrack"] = R (0.5f, 0.85f);
        v["rv_mix"] = R (0.15f, 0.35f); v["rv_size"] = R (0.4f, 0.8f); v["rv_damp"] = R (0.3f, 0.6f);
        static const char* adj[] = { "Hmlistý", "Zamatový", "Ligotavý", "Snový", "Polnočný", "Jantárový", "Mrazivý", "Teplý", "Kozmický", "Dúhový",
                                     "Tichý", "Zlatý", "Strieborný", "Večerný", "Lesný", "Morský", "Hviezdny", "Jemný", "Hlboký", "Nočný", "Ranný", "Sklenený" };
        const char* noun = "zvuk"; const char* cat = "Moje zvuky";
        switch (type)
        {
            case 0:   // pad
            {
                noun = pick ({ 0, 1, 2 }) == 0 ? "pad" : (chance (0.5f) ? "oblak" : "opar"); cat = "Pady";
                v["a_wave"] = pick ({ 2, 2, 3, 1 }); v["a_uni"] = I (3, 7); v["a_spread"] = R (7, 20); v["a_color"] = R (0.2f, 0.6f);
                v["a_cut"] = LR (600, 4000); v["a_reso"] = R (0.05f, 0.35f); v["a_fenv"] = R (0, 0.35f);
                v["a_fa"] = R (0.3f, 2); v["a_fd"] = R (1, 3); v["a_fs"] = R (0.3f, 0.8f); v["a_fr"] = R (1, 2.5f);
                v["a_aa"] = R (0.3f, 1.8f); v["a_ad"] = 1.5f; v["a_as"] = R (0.8f, 1); v["a_ar"] = R (1.2f, 3); v["a_level"] = 0.8f;
                if (chance (0.5f)) { v["a_ltgt"] = 1; v["a_lshape"] = pick ({ 0, 1 }); v["a_lrate"] = LR (0.08f, 0.6f); v["a_ldepth"] = R (0.1f, 0.35f); }
                if (chance (0.7f))
                {
                    v["b_on"] = 1; v["b_wave"] = pick ({ 0, 1, 2, 3 }); v["b_oct"] = pick ({ -1, 1, 2 }); v["b_level"] = R (0.2f, 0.45f);
                    v["b_cut"] = LR (1000, 8000); copyEnv();
                }
                v["tube"] = R (0, 0.4f);
                if (chance (0.7f)) { v["ch_on"] = 1; v["ch_mix"] = R (0.3f, 0.55f); }
                v["rv_mix"] = R (0.3f, 0.5f); v["rv_size"] = R (0.65f, 0.9f);
                break;
            }
            case 1:   // pluck
            {
                noun = chance (0.5f) ? "pluck" : "drnk"; cat = "Plucky";
                if (chance (0.5f))
                {
                    v["a_wave"] = 5; v["a_color"] = R (0.3f, 0.85f); v["a_ad"] = R (0.8f, 4); v["a_aa"] = 0.001f; v["a_as"] = 1; v["a_ar"] = R (0.2f, 1); v["a_level"] = 1;
                }
                else
                {
                    v["a_wave"] = pick ({ 2, 3 }); v["a_uni"] = I (1, 3); v["a_spread"] = R (4, 12);
                    v["a_cut"] = LR (250, 1200); v["a_reso"] = R (0.1f, 0.45f); v["a_fenv"] = R (0.45f, 0.85f);
                    v["a_fa"] = 0.001f; v["a_fd"] = R (0.12f, 0.45f); v["a_fs"] = 0; v["a_fr"] = 0.2f;
                    v["a_aa"] = 0.001f; v["a_ad"] = R (0.3f, 0.9f); v["a_as"] = 0; v["a_ar"] = R (0.2f, 0.5f); v["a_level"] = 0.9f;
                }
                if (chance (0.5f))
                {
                    v["b_on"] = 1; v["b_wave"] = pick ({ 0, 5 }); v["b_oct"] = pick ({ 1, -1 }); v["b_level"] = R (0.2f, 0.4f); v["b_color"] = R (0.3f, 0.7f);
                    v["b_aa"] = 0.001f; v["b_ad"] = R (0.4f, 2); v["b_as"] = v["b_wave"] == 5 ? 1.0f : 0.0f; v["b_ar"] = 0.3f;
                }
                if (chance (0.5f)) { v["dl_on"] = 1; v["dl_sync"] = pick ({ 2, 3, 4 }); v["dl_mix"] = R (0.15f, 0.3f); v["dl_fb"] = R (0.2f, 0.45f); }
                if (chance (0.4f)) { v["ch_on"] = 1; v["ch_mix"] = R (0.1f, 0.3f); }
                break;
            }
            case 2:   // klavesy (FM)
            {
                noun = chance (0.5f) ? "klavírik" : "tón"; cat = "Klávesy";
                v["a_wave"] = 6; v["a_ratio"] = pick ({ 1, 1, 3, 5 }); v["a_color"] = R (0.25f, 0.55f); v["a_fenv"] = R (0.5f, 0.9f);
                v["a_fa"] = 0.001f; v["a_fd"] = R (0.6f, 1.8f); v["a_fs"] = 0; v["a_aa"] = 0.002f; v["a_ad"] = R (2, 6); v["a_as"] = R (0, 0.25f); v["a_ar"] = R (0.3f, 0.6f); v["a_level"] = 1;
                if (chance (0.3f)) { v["a_ltgt"] = 3; v["a_lrate"] = R (3, 6); v["a_ldepth"] = R (0.1f, 0.3f); }
                if (chance (0.4f)) { v["b_on"] = 1; v["b_wave"] = 0; v["b_oct"] = 2; v["b_semi"] = pick ({ 0, 7 }); v["b_level"] = R (0.08f, 0.15f); v["b_aa"] = 0.001f; v["b_ad"] = 0.12f; v["b_as"] = 0; v["b_ar"] = 0.05f; }
                if (chance (0.6f)) { v["ch_on"] = 1; v["ch_mix"] = R (0.2f, 0.45f); }
                break;
            }
            case 3:   // bas
            {
                noun = chance (0.5f) ? "bas" : "basák"; cat = "Basy";
                if (chance (0.25f)) { v["a_wave"] = 6; v["a_ratio"] = pick ({ 0, 1 }); v["a_color"] = R (0.2f, 0.5f); v["a_fenv"] = R (0.4f, 0.8f); v["a_fd"] = 0.3f; v["a_fs"] = 0.1f; }
                else
                {
                    v["a_wave"] = pick ({ 2, 3, 1 }); v["a_cut"] = LR (200, 900); v["a_reso"] = R (0.1f, 0.4f); v["a_fenv"] = R (0.25f, 0.7f);
                    v["a_fa"] = 0.002f; v["a_fd"] = R (0.15f, 0.5f); v["a_fs"] = R (0, 0.4f); v["a_fr"] = 0.12f;
                }
                v["a_oct"] = pick ({ 0, -1 }); v["a_aa"] = 0.003f; v["a_ad"] = 0.5f; v["a_as"] = R (0.7f, 1); v["a_ar"] = R (0.08f, 0.2f); v["a_level"] = 0.8f;
                if (chance (0.7f)) { v["b_on"] = 1; v["b_wave"] = 0; v["b_oct"] = v["a_oct"] - 1; v["b_level"] = R (0.4f, 0.7f); copyEnv(); }
                if (chance (0.6f)) { v["mono"] = 1; v["glide"] = R (0, 0.06f); }
                if (chance (0.7f)) v["rv_on"] = 0; else v["rv_mix"] = 0.1f;
                v["tube"] = R (0, 0.5f);
                if (chance (0.3f)) { v["drive_on"] = 1; v["drive"] = R (0.1f, 0.35f); }
                break;
            }
            case 4:   // lead
            {
                noun = chance (0.5f) ? "lead" : "hlas"; cat = "Leady";
                v["a_wave"] = pick ({ 2, 3 }); v["a_uni"] = I (1, 4); v["a_spread"] = R (5, 15); v["a_color"] = R (0.3f, 0.6f);
                v["a_cut"] = LR (1200, 6000); v["a_reso"] = R (0.1f, 0.4f); v["a_fenv"] = R (0.1f, 0.4f);
                v["a_fa"] = 0.005f; v["a_fd"] = R (0.3f, 0.8f); v["a_fs"] = R (0.4f, 0.8f);
                v["a_aa"] = R (0.005f, 0.05f); v["a_ad"] = 0.4f; v["a_as"] = 0.9f; v["a_ar"] = R (0.15f, 0.4f); v["a_level"] = 0.8f;
                v["a_lrate"] = R (4.8f, 6); v["a_ldepth"] = R (0.05f, 0.14f); v["a_ldelay"] = R (0.2f, 0.45f);
                if (chance (0.8f)) { v["mono"] = 1; v["glide"] = R (0.03f, 0.12f); }
                if (chance (0.5f))
                {
                    v["b_on"] = 1; v["b_wave"] = pick ({ 2, 3 }); v["b_oct"] = pick ({ -1, 1 }); v["b_fine"] = R (4, 10); v["b_level"] = 0.4f;
                    v["b_cut"] = v["a_cut"]; v["b_reso"] = v["a_reso"]; copyEnv();
                }
                if (chance (0.7f)) { v["dl_on"] = 1; v["dl_sync"] = pick ({ 2, 3 }); v["dl_mix"] = R (0.15f, 0.25f); v["dl_fb"] = R (0.2f, 0.4f); }
                v["tube"] = R (0, 0.4f);
                break;
            }
            case 5:   // zvonky (FM)
            {
                noun = chance (0.5f) ? "zvonček" : "kryštál"; cat = "Zvonky";
                v["a_wave"] = 6; v["a_ratio"] = pick ({ 5, 6, 8, 9 }); v["a_color"] = R (0.2f, 0.5f); v["a_fenv"] = R (0.7f, 1);
                v["a_fa"] = 0.001f; v["a_fd"] = R (0.3f, 1.2f); v["a_fs"] = 0; v["a_aa"] = 0.001f; v["a_ad"] = R (2.5f, 6); v["a_as"] = 0; v["a_ar"] = R (0.6f, 1.5f); v["a_level"] = 1;
                if (chance (0.6f)) { v["b_on"] = 1; v["b_wave"] = 0; v["b_oct"] = pick ({ 1, 2 }); v["b_level"] = R (0.1f, 0.25f); v["b_aa"] = 0.001f; v["b_ad"] = R (1, 3); v["b_as"] = 0; v["b_ar"] = 0.6f; }
                v["rv_mix"] = R (0.35f, 0.5f); v["rv_size"] = R (0.65f, 0.9f);
                if (chance (0.4f)) { v["ch_on"] = 1; v["ch_mix"] = R (0.15f, 0.35f); }
                break;
            }
            case 6:   // slaciky
            {
                noun = chance (0.5f) ? "sláčik" : "orchester"; cat = "Sláčiky";
                v["a_wave"] = 2; v["a_uni"] = I (4, 7); v["a_spread"] = R (8, 16);
                v["a_cut"] = LR (1500, 4500); v["a_reso"] = R (0.05f, 0.25f); v["a_fenv"] = R (0.05f, 0.2f);
                v["a_fa"] = R (0.2f, 0.6f); v["a_fd"] = 1; v["a_fs"] = 0.7f; v["a_fr"] = 0.6f;
                v["a_aa"] = R (0.15f, 0.6f); v["a_ad"] = 0.6f; v["a_as"] = 0.9f; v["a_ar"] = R (0.5f, 1.2f); v["a_level"] = 0.85f;
                if (chance (0.4f)) { v["a_ftype"] = 3; v["a_uni"] = 2; v["a_spread"] = 4; }
                v["a_lrate"] = R (5, 5.8f); v["a_ldepth"] = R (0.05f, 0.15f); v["a_ldelay"] = R (0.25f, 0.4f);
                if (chance (0.4f)) { v["b_on"] = 1; v["b_wave"] = 2; v["b_oct"] = -1; v["b_level"] = 0.35f; v["b_cut"] = v["a_cut"]; copyEnv(); }
                v["ch_on"] = 1; v["ch_mix"] = R (0.35f, 0.6f); v["rv_mix"] = R (0.3f, 0.45f); v["rv_size"] = 0.7f;
                break;
            }
            default:  // klavir + pad
            {
                noun = chance (0.5f) ? "klavír" : "koncert"; cat = "Klavír";
                v["a_wave"] = 7; v["a_src"] = 0; v["a_color"] = R (0.25f, 0.7f); v["a_level"] = 1; v["a_aa"] = 0.0015f; v["a_ar"] = 0.4f;
                v["b_on"] = 1; v["b_wave"] = pick ({ 2, 1, 0, 3 }); v["b_uni"] = I (3, 5); v["b_spread"] = 10; v["b_cut"] = LR (800, 3000);
                v["b_aa"] = R (0.4f, 1.5f); v["b_ad"] = 1.5f; v["b_as"] = 0.9f; v["b_ar"] = R (1, 2.5f); v["b_level"] = R (0.12f, 0.3f); v["b_oct"] = pick ({ 0, -1, 1 });
                v["rv_mix"] = R (0.25f, 0.4f);
                if (chance (0.5f)) { v["ch_on"] = 1; v["ch_mix"] = R (0.2f, 0.4f); }
                break;
            }
        }
        if (chance (0.4f)) v["eq_high"] = R (-4, 3);
        p.name = juce::String::fromUTF8 (adj[r.nextInt ((int) (sizeof (adj) / sizeof (adj[0])))]) + " " + juce::String::fromUTF8 (noun);
        p.category = juce::String::fromUTF8 (cat);
        return p;
    }

    // cielova hlasitost podla tovarenskych zvukov: drzane zvuky okolo -19.5 dB, uderove okolo -16 dB
    float targetLevelDb (const juce::String& cat)
    {
        for (auto c : { "Pady", "Sláčiky", "Leady", "Basy" }) if (cat == juce::String::fromUTF8 (c)) return -19.5f;
        return -16.0f;
    }
}

float NovaProcessor::measureLevel (const PatchData& p)
{
    // akord C3 G3 C4 E4 1.2 s; hlasitost = najvacsie RMS v okne 0.4 s
    Settings s;
    settingsFrom (s, [&p] (const juce::String& id)
    {
        if (id == "p_gain" || id == "volume") return 0.0f;
        auto it = p.values.find (id);
        if (it != p.values.end()) return it->second;
        const auto* sp = findSpec (id);
        return sp != nullptr ? sp->def : 0.0f;
    });
    s.arpOn = false;
    const double sr = 48000; const int bs = 512, total = (int) (2.0 * sr);
    auto syn = std::make_unique<Synth>();
    syn->prepare (sr, bs);
    std::vector<float> sq; sq.reserve ((size_t) total);
    juce::AudioBuffer<float> b (2, bs);
    for (int pos = 0; pos < total; pos += bs)
    {
        juce::MidiBuffer mb;
        if (pos == 0) for (int n : { 48, 55, 60, 64 }) mb.addEvent (juce::MidiMessage::noteOn (1, n, 0.8f), 0);
        if (pos <= (int) (1.2 * sr) && pos + bs > (int) (1.2 * sr)) for (int n : { 48, 55, 60, 64 }) mb.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        b.clear(); syn->process (b, mb, s);
        for (int i = 0; i < bs; ++i) sq.push_back (0.5f * (b.getSample (0, i) * b.getSample (0, i) + b.getSample (1, i) * b.getSample (1, i)));
    }
    const int w = (int) (0.4 * sr);
    double acc = 0, best = 0;
    for (int i = 0; i < (int) sq.size(); ++i)
    {
        acc += sq[(size_t) i];
        if (i >= w) acc -= sq[(size_t) (i - w)];
        best = std::max (best, acc / w);
    }
    return juce::Decibels::gainToDecibels ((float) std::sqrt (best), -100.0f);
}

void NovaProcessor::surprise()
{
    juce::Random r;
    auto p = randomPatch (r, synth.pianoReady());
    const float lv = measureLevel (p);
    p.values["p_gain"] = lv > -90.0f ? juce::jlimit (-30.0f, 6.0f, targetLevelDb (p.category) - lv) : 0.0f;
    applyPatch (p);
    currentKey = "r:";
    if (onPatchChanged) onPatchChanged();
}

void NovaProcessor::newSound (const juce::String& category)
{
    PatchData p;
    p.name = juce::String::fromUTF8 ("Nový zvuk"); p.category = category;
    for (auto& sp : paramSpecs()) if (isPatchParam (sp.id)) p.values[sp.id] = sp.def;
    p.values["a_cut"] = 5000; p.values["a_ar"] = 0.4f;
    const float lv = measureLevel (p);
    p.values["p_gain"] = lv > -90.0f ? juce::jlimit (-30.0f, 6.0f, -19.5f - lv) : 0.0f;
    applyPatch (p);
    currentKey = "r:";
    if (onPatchChanged) onPatchChanged();
}

// ------------------------------------------------------------- vlastne WAV
void NovaProcessor::setSample (int layer, const juce::MemoryBlock& flac, const juce::String& name)
{
    smpFlac[layer] = flac; smpName[layer] = name;
    if (flac.getSize() == 0) { synth.setUserSample (layer, nullptr); return; }
    juce::FlacAudioFormat fmt;
    std::unique_ptr<juce::AudioFormatReader> r (fmt.createReaderFor (new juce::MemoryInputStream (flac, false), true));
    if (r == nullptr) { synth.setUserSample (layer, nullptr); return; }
    auto s = std::make_shared<UserSample>();
    s->rate = r->sampleRate; s->name = name; s->flac = flac;
    s->buf.setSize ((int) juce::jlimit (1u, 2u, r->numChannels), (int) r->lengthInSamples);
    r->read (&s->buf, 0, (int) r->lengthInSamples, 0, true, true);
    synth.setUserSample (layer, s);
}

bool NovaProcessor::loadSampleFile (int layer, const juce::File& f, juce::String& error)
{
    juce::AudioFormatManager fm; fm.registerBasicFormats();
    std::unique_ptr<juce::AudioFormatReader> r (fm.createReaderFor (f));
    if (r == nullptr) { error = juce::String::fromUTF8 ("Súbor sa nedá prečítať (WAV, AIFF, FLAC, MP3, OGG)."); return false; }
    const int len = (int) juce::jmin (r->lengthInSamples, (juce::int64) (r->sampleRate * 60.0));
    const int ch = (int) juce::jlimit (1u, 2u, r->numChannels);
    juce::AudioBuffer<float> b (ch, len);
    r->read (&b, 0, len, 0, true, ch > 1);
    juce::MemoryBlock mb;
    {
        auto* ms = new juce::MemoryOutputStream (mb, false);
        std::unique_ptr<juce::OutputStream> os (ms);
        juce::FlacAudioFormat flac;
        auto w = flac.createWriterFor (os, juce::AudioFormatWriterOptions{}.withSampleRate (r->sampleRate).withNumChannels (ch).withBitsPerSample (24));
        if (w == nullptr) { error = juce::String::fromUTF8 ("Zvuk sa nepodarilo spracovať."); return false; }
        w->writeFromAudioSampleBuffer (b, 0, len);
    }
    setSample (layer, mb, f.getFileNameWithoutExtension());
    const juce::String p = layer == 0 ? "a_" : "b_";
    auto set = [this] (const juce::String& id, float v) { if (auto* prm = params.getParameter (id)) prm->setValueNotifyingHost (prm->convertTo0to1 (v)); };
    set (p + "on", 1); set (p + "wave", 7); set (p + "src", 1);
    if (onPatchChanged) onPatchChanged();
    return true;
}

juce::String NovaProcessor::sampleName (int layer) const { return smpName[layer & 1]; }

// ------------------------------------------------------------- programy (tovarenske zvuky)
int NovaProcessor::getNumPrograms() { return (int) factoryPatches().size(); }
int NovaProcessor::getCurrentProgram() { return (int) raw["preset"]->load(); }
void NovaProcessor::setCurrentProgram (int index)
{
    if (juce::isPositiveAndBelow (index, getNumPrograms())) loadPreset ("f:" + factoryPatches()[(size_t) index].name);
}
const juce::String NovaProcessor::getProgramName (int index)
{
    return juce::isPositiveAndBelow (index, getNumPrograms()) ? factoryPatches()[(size_t) index].name : juce::String();
}

// ------------------------------------------------------------- stav (projekt)
void NovaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    auto state = params.copyState();
    state.setProperty ("ver", 2, nullptr);
    state.setProperty ("patchName", currentName, nullptr);
    state.setProperty ("patchCat", currentCategory, nullptr);
    state.setProperty ("patchKey", currentKey, nullptr);
    state.setProperty ("uiMode", uiMode, nullptr);
    state.setProperty ("uiScale", uiScale, nullptr);
    for (int i = 0; i < 2; ++i)
    {
        state.setProperty (i == 0 ? "smpA" : "smpB", smpFlac[i].getSize() > 0 ? smpFlac[i].toBase64Encoding() : juce::String(), nullptr);
        state.setProperty (i == 0 ? "smpAName" : "smpBName", smpName[i], nullptr);
    }
    if (auto xml = state.createXml()) copyXmlToBinary (*xml, destData);
}

void NovaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr || ! xml->hasTagName (params.state.getType())) return;
    auto tree = juce::ValueTree::fromXml (*xml);
    restoring = true;
    const bool v1 = ! tree.hasProperty ("ver");
    params.replaceState (tree);
    lastPresetParam = (int) raw["preset"]->load();
    if (v1)
    {
        // projekt z prvej verzie: nacitame tovarensky zvuk podla vyberu, makra zostanu
        PatchData p;
        const int idx = juce::jlimit (0, (int) factoryPatches().size() - 1, lastPresetParam);
        if (presets.load ("f:" + factoryPatches()[(size_t) idx].name, p)) { applyPatch (p); currentKey = "f:" + p.name; }
    }
    else
    {
        currentName = tree.getProperty ("patchName").toString();
        currentCategory = tree.getProperty ("patchCat").toString();
        currentKey = tree.getProperty ("patchKey").toString();
        uiMode = (int) tree.getProperty ("uiMode", 0);
        uiScale = (float) (double) tree.getProperty ("uiScale", 1.0);
        for (int i = 0; i < 2; ++i)
        {
            juce::MemoryBlock mb;
            const auto b64 = tree.getProperty (i == 0 ? "smpA" : "smpB").toString();
            if (b64.isNotEmpty()) mb.fromBase64Encoding (b64);
            setSample (i, mb, tree.getProperty (i == 0 ? "smpAName" : "smpBName").toString());
        }
    }
    restoring = false;
    undo.clearUndoHistory();
    startTimer (400);
    if (onPatchChanged) onPatchChanged();
}

juce::AudioProcessorEditor* NovaProcessor::createEditor() { return new NovaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NovaProcessor(); }
