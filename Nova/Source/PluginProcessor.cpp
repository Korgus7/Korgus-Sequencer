#include "PluginProcessor.h"
#include "PluginEditor.h"

juce::AudioProcessorValueTreeState::ParameterLayout NovaProcessor::layout()
{
    juce::StringArray names;
    for (const auto& p : nova::presets()) names.add (juce::String::fromUTF8 (p.name));
    std::vector<std::unique_ptr<juce::RangedAudioParameter>> v;
    v.push_back (std::make_unique<juce::AudioParameterChoice> (juce::ParameterID { "preset", 1 }, juce::String::fromUTF8 ("Zvuk"), names, 0));
    auto pct = [] (const char* id, const char* name)
    {
        return std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { id, 1 }, juce::String::fromUTF8 (name), juce::NormalisableRange<float> (0.0f, 1.0f), 0.5f,
                                                            juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (juce::roundToInt (x * 100.0f)) + " %"; }));
    };
    v.push_back (pct ("bright", "Jas"));
    v.push_back (pct ("attack", "Nábeh"));
    v.push_back (pct ("release", "Dozvuk"));
    v.push_back (pct ("space", "Priestor"));
    v.push_back (pct ("width", "Šírka"));
    v.push_back (std::make_unique<juce::AudioParameterFloat> (juce::ParameterID { "volume", 1 }, juce::String::fromUTF8 ("Hlasitosť"), juce::NormalisableRange<float> (-24.0f, 6.0f), 0.0f,
                                                             juce::AudioParameterFloatAttributes().withStringFromValueFunction ([] (float x, int) { return juce::String (x, 1) + " dB"; })));
    return { v.begin(), v.end() };
}

NovaProcessor::NovaProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "NOVA", layout())
{
    pPreset = params.getRawParameterValue ("preset");
    pBright = params.getRawParameterValue ("bright");
    pAttack = params.getRawParameterValue ("attack");
    pRelease = params.getRawParameterValue ("release");
    pSpace = params.getRawParameterValue ("space");
    pWidth = params.getRawParameterValue ("width");
    pVolume = params.getRawParameterValue ("volume");
}

bool NovaProcessor::isBusesLayoutSupported (const BusesLayout& l) const
{
    const auto o = l.getMainOutputChannelSet();
    return o == juce::AudioChannelSet::stereo() || o == juce::AudioChannelSet::mono();
}

void NovaProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    synth.prepare (sampleRate, samplesPerBlock);
}

void NovaProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals nd;
    for (int c = getTotalNumInputChannels(); c < buffer.getNumChannels(); ++c) buffer.clear (c, 0, buffer.getNumSamples());
    synth.setPreset ((int) pPreset->load());
    nova::Knobs k;
    k.bright = pBright->load(); k.attack = pAttack->load(); k.release = pRelease->load();
    k.space = pSpace->load(); k.width = pWidth->load(); k.volumeDb = pVolume->load();
    synth.setKnobs (k);
    synth.process (buffer, midi);
}

int NovaProcessor::getNumPrograms() { return (int) nova::presets().size(); }
int NovaProcessor::getCurrentProgram() { return (int) pPreset->load(); }
void NovaProcessor::setCurrentProgram (int index)
{
    if (auto* p = params.getParameter ("preset"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) juce::jlimit (0, getNumPrograms() - 1, index)));
}
const juce::String NovaProcessor::getProgramName (int index)
{
    const auto& l = nova::presets();
    return juce::isPositiveAndBelow (index, (int) l.size()) ? juce::String::fromUTF8 (l[(size_t) index].name) : juce::String();
}

void NovaProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    if (auto xml = params.copyState().createXml()) copyXmlToBinary (*xml, destData);
}

void NovaProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    if (auto xml = getXmlFromBinary (data, sizeInBytes))
        if (xml->hasTagName (params.state.getType())) params.replaceState (juce::ValueTree::fromXml (*xml));
}

juce::AudioProcessorEditor* NovaProcessor::createEditor() { return new NovaEditor (*this); }

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter() { return new NovaProcessor(); }
