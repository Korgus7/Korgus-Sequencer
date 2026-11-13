#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Widgets.h"

class NovaEditor : public juce::AudioProcessorEditor,
                   public nova::ui::KnobHost,
                   public juce::DragAndDropContainer,
                   public juce::FileDragAndDropTarget,
                   private juce::Timer
{
public:
    explicit NovaEditor (NovaProcessor&);
    ~NovaEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    void setMode (int mode);
    bool keyPressed (const juce::KeyPress& k) override;
    void mouseDown (const juce::MouseEvent& e) override;
    void parentHierarchyChanged() override;
    void setScale (float s);
    void knobMenu (juce::Component& knob, const juce::String& paramId) override;
    int knobCc (const juce::String& paramId) override { return proc.ccFor (paramId); }
    bool knobLearning (const juce::String& paramId) override { return proc.isLearning (paramId); }
    // prehliadac zvukov (layer -1 = jedno tlacidlo Nacitat)
    void openWavBrowser (int layer, std::function<void (const juce::File&, int)> onLoad);
    NovaProcessor& proc;
    nova::ui::Look look;

private:
    void timerCallback() override;
    void layoutSize();
    std::unique_ptr<juce::Component> view;
    std::unique_ptr<juce::Component> overlay;
    int lastCcVersion = -1;
    juce::Component content;
    juce::TooltipWindow tips { this, 600 };
};
