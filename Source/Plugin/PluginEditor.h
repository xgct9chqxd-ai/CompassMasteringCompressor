#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"

class GRHistoryMeter final : public juce::Component
{
public:
    void pushValueDb (float grDb) noexcept
    {
        if (! std::isfinite (grDb)) grDb = 0.0f;

        // UI contract: GR is stereo authority in positive dB (0..range).
        constexpr float kRangeDb = 24.0f;
        lastGrDb = juce::jlimit (0.0f, kRangeDb, grDb);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f);

        // Background
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 8.0f);

        // Filled bar (left -> right as GR increases)
        constexpr float kRangeDb = 24.0f;
        const float fill01 = juce::jlimit (0.0f, 1.0f, lastGrDb / kRangeDb);
        auto fillR = r;
        fillR.setWidth (r.getWidth() * fill01);

        g.setColour (juce::Colours::white.withAlpha (0.60f));
        g.fillRoundedRectangle (fillR, 8.0f);

        // Label "GR"
        auto labelR = r.reduced (10.0f, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.70f));
        g.setFont (12.5f);
        g.drawText ("GR", labelR.removeFromLeft (34.0f), juce::Justification::centredLeft, false);
    }

private:
    float lastGrDb = 0.0f;
};

class CompassMasteringLimiterAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit CompassMasteringLimiterAudioProcessorEditor (CompassMasteringLimiterAudioProcessor&);
    ~CompassMasteringLimiterAudioProcessorEditor() override = default;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    CompassMasteringLimiterAudioProcessor& processor;

    juce::Slider drive;
    juce::Slider ceiling;
    juce::ComboBox adaptiveBias;
    juce::Slider stereoLink;
    juce::ComboBox oversamplingMin;

    GRHistoryMeter grMeter;
    juce::Label currentGrLabel;
    juce::Label inTpLabel;
    juce::Label outTpLabel;
    juce::Label lufsSLabel;
    juce::Label lufsILabel;

    juce::Label inPeakLabel;
    juce::Label outPeakLabel;
    juce::Label ceilingLabel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> driveA;
    std::unique_ptr<APVTS::SliderAttachment> ceilingA;
    std::unique_ptr<APVTS::ComboBoxAttachment> biasA;
    std::unique_ptr<APVTS::SliderAttachment> linkA;
    std::unique_ptr<APVTS::ComboBoxAttachment> osA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassMasteringLimiterAudioProcessorEditor)
};
