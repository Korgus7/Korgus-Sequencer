#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class NovaLook : public juce::LookAndFeel_V4
{
public:
    NovaLook();
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float a0, float a1, juce::Slider&) override;
};

class NovaEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit NovaEditor (NovaProcessor&);
    ~NovaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

private:
    struct PresetButton : juce::Component
    {
        juce::String name; bool selected = false, hover = false; std::function<void()> onClick;
        void paint (juce::Graphics&) override;
        void mouseUp (const juce::MouseEvent& e) override { if (e.mouseWasClicked() && onClick) onClick(); }
        void mouseEnter (const juce::MouseEvent&) override { hover = true; repaint(); }
        void mouseExit (const juce::MouseEvent&) override { hover = false; repaint(); }
    };
    void timerCallback() override;

    NovaProcessor& proc;
    NovaLook look;
    std::vector<std::unique_ptr<PresetButton>> buttons;
    struct Knob { juce::Slider s; juce::Label l; std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> att; };
    std::array<Knob, 6> knobs;
    int shownPreset = -1;
    juce::String status;
};
