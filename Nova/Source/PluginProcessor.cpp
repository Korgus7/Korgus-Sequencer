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
    auto g = [this] (const juce::String& id) { return raw[id]->load(); };
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
    }
    auto& f = s.fx;
    f.tube = g ("tube"); f.compOn = g ("comp_on") > 0.5f; f.comp = g ("comp");
    f.driveOn = g ("drive_on") > 0.5f; f.drive = g ("drive");
    f.chOn = g ("ch_on") > 0.5f; f.chRate = g ("ch_rate"); f.chDepth = g ("ch_depth"); f.chMix = g ("ch_mix");
    f.dlOn = g ("dl_on") > 0.5f; f.dlSync = (int) g ("dl_sync"); f.dlTime = g ("dl_time"); f.dlFb = g ("dl_fb"); f.dlTone = g ("dl_tone"); f.dlMix = g ("dl_mix"); f.dlPP = g ("dl_pp") > 0.5f;
    f.rvOn = g ("rv_on") > 0.5f; f.rvSize = g ("rv_size"); f.rvDamp = g ("rv_damp"); f.rvMix = g ("rv_mix");
    f.width = g ("fx_width"); f.punch = g ("fx_punch"); f.eqLow = g ("eq_low"); f.eqMid = g ("eq_mid"); f.eqMidF = g ("eq_midf"); f.eqHigh = g ("eq_high");
    f.mfOn = g ("mf_on") > 0.5f; f.mfType = (int) g ("mf_type"); f.mfCut = g ("mf_cut"); f.mfReso = g ("mf_reso"); f.mfDrive = g ("mf_drive");
    f.gainDb = g ("p_gain"); f.volumeDb = g ("volume");
    s.arpOn = g ("arp_on") > 0.5f; s.arpHold = g ("arp_hold") > 0.5f; s.arpPat = (int) g ("arp_pat"); s.arpSpeed = (int) g ("arp_speed");
    s.arpOct = (int) g ("arp_oct"); s.arpLen = g ("arp_len");
    s.mono = g ("mono") > 0.5f; s.glide = g ("glide"); s.veltrack = g ("veltrack"); s.morphX = g ("morph_x"); s.morphY = g ("morph_y");
    s.bright = g ("bright"); s.attack = g ("attack"); s.release = g ("release"); s.space = g ("space"); s.width = g ("width");
    f.bpm = 120; s.playing = false; s.ppq = 0;
    if (auto* ph = getPlayHead())
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) f.bpm = *b;
            s.playing = pos->getIsPlaying();
            if (auto q = pos->getPpqPosition()) s.ppq = *q;
        }
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
