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
        grDb = juce::jlimit (-60.0f, 0.0f, grDb);

        history[(size_t) head] = grDb;
        head = (head + 1) % kMax;
        if (head == 0) filled = true;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat().reduced (6.0f);

        // Restrained background (observational, no drama)
        g.setColour (juce::Colours::white.withAlpha (0.10f));
        g.fillRoundedRectangle (r, 8.0f);

        auto yForDb = [&] (float db) -> float
        {
            const float n = (juce::jlimit (-60.0f, 0.0f, db) + 60.0f) / 60.0f; // 0..1
            return r.getBottom() - r.getHeight() * n; // 0 dB at top, -60 dB at bottom
        };

        const int count = filled ? kMax : head;
        if (count > 2)
        {
            juce::Path p;
            const float dx = r.getWidth() / (float) (count - 1);

            auto sampleAt = [&] (int i) -> float
            {
                const int idx = filled ? (head + i) % kMax : i;
                return history[(size_t) idx];
            };

            p.startNewSubPath (r.getX(), yForDb (sampleAt (0)));
            for (int i = 1; i < count; ++i)
                p.lineTo (r.getX() + dx * (float) i, yForDb (sampleAt (i)));

            // History path only (primary + only visual)
            g.setColour (juce::Colours::white.withAlpha (0.60f));
            g.strokePath (p, juce::PathStrokeType (1.5f));
        }
    }

private:
    static constexpr int kMax = 120;
    std::array<float, (size_t) kMax> history {};
    int head = 0;
    bool filled = false;
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

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> driveA;
    std::unique_ptr<APVTS::SliderAttachment> ceilingA;
    std::unique_ptr<APVTS::ComboBoxAttachment> biasA;
    std::unique_ptr<APVTS::SliderAttachment> linkA;
    std::unique_ptr<APVTS::ComboBoxAttachment> osA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassMasteringLimiterAudioProcessorEditor)
};
