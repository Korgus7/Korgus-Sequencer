#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "Synth.h"

class NovaProcessor : public juce::AudioProcessor
{
public:
    NovaProcessor();
    ~NovaProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Korgus Nova"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 4.0; }

    int getNumPrograms() override;
    int getCurrentProgram() override;
    void setCurrentProgram (int index) override;
    const juce::String getProgramName (int index) override;
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState params;
    nova::Synth synth;

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout layout();
    std::atomic<float>* pPreset = nullptr, *pBright = nullptr, *pAttack = nullptr, *pRelease = nullptr, *pSpace = nullptr, *pWidth = nullptr, *pVolume = nullptr;
};
