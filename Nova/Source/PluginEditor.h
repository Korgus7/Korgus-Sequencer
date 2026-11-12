#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_utils/juce_audio_utils.h>
#include "PluginProcessor.h"
#include "Widgets.h"

class NovaEditor : public juce::AudioProcessorEditor,
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
    void setScale (float s);
    NovaProcessor& proc;
    nova::ui::Look look;

private:
    void timerCallback() override;
    void layoutSize();
    std::unique_ptr<juce::Component> view;
    juce::Component content;
    juce::TooltipWindow tips { this, 600 };
};
