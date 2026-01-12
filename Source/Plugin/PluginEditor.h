#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include "PluginProcessor.h"

class CompassMasteringLimiterAudioProcessorEditor final : public juce::AudioProcessorEditor
{
public:
    explicit CompassMasteringLimiterAudioProcessorEditor (CompassMasteringLimiterAudioProcessor&);
    ~CompassMasteringLimiterAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    CompassMasteringLimiterAudioProcessor& processor;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassMasteringLimiterAudioProcessorEditor)
};
