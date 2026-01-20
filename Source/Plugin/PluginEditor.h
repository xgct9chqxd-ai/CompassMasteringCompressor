#pragma once
#include <juce_gui_basics/juce_gui_basics.h>
#include <cmath>
#include "PluginProcessor.h"

namespace Layout
{
    // Outer frame insets
    static constexpr int insetL = 18;
    static constexpr int insetR = 18;
    static constexpr int insetT = 14;
    static constexpr int insetB = 14;

    // Vertical band heights
    static constexpr int topKnobBand   = 140;
    static constexpr int contextRow    = 40;
    static constexpr int grMainZone    = 120;
    static constexpr int clampGlueBars = 40;
    static constexpr int truthStrip    = 30;
    static constexpr int interBandGap  = 10;

    // Side meter widths
    static constexpr int leftTpWidth  = 60;
    static constexpr int rightTpWidth = 60;

    // Truth strip max width
    static constexpr int truthMaxWidth = 680;
}

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
        //// [CML:UI] GR meter LEDs — deterministic block strip
        constexpr float kInsetPx       = 14.0f;
        constexpr int   kBlocks        = 60;
        constexpr float kGapPx         = 1.0f;
        constexpr float kBlockHeightFr = 0.55f;
        constexpr float kRangeDb       = 24.0f;
        constexpr float kLitAlpha      = 0.22f;
        constexpr float kUnlitAlpha    = 0.55f;
        constexpr float kTopStrokeA    = 0.08f;
        constexpr float kTopStrokePx   = 1.0f;

        auto r = getLocalBounds().toFloat();
        auto ledArea = r.reduced (kInsetPx, kInsetPx);

        const float bw = (ledArea.getWidth() - kGapPx * (float) (kBlocks - 1)) / (float) kBlocks;
        const float bh = ledArea.getHeight() * kBlockHeightFr;
        const float y  = ledArea.getCentreY() - 0.5f * bh;

        const float grNorm = juce::jlimit (0.0f, 1.0f, lastGrDb / kRangeDb);
        const int lit = (int) std::floor (grNorm * (float) kBlocks + 0.5f);

        for (int i = 0; i < kBlocks; ++i)
        {
            const float x = ledArea.getX() + (float) i * (bw + kGapPx);
            juce::Rectangle<float> b (x, y, bw, bh);

            if (i < lit) g.setColour (juce::Colours::white.withAlpha (kLitAlpha));
            else         g.setColour (juce::Colours::black.withAlpha (kUnlitAlpha));

            g.fillRect (b);

            g.setColour (juce::Colours::white.withAlpha (kTopStrokeA));
            g.drawLine (b.getX(), b.getY(), b.getRight(), b.getY(), kTopStrokePx);
        }
    }

private:
    float lastGrDb = 0.0f;
};

class VerticalPeakMeter final : public juce::Component
{
public:
    void pushValueDb (float newValueDb) noexcept
    {
        if (! std::isfinite (newValueDb)) newValueDb = -120.0f;

        newValueDb = juce::jlimit (-120.0f, 6.0f, newValueDb);

        currentPeakDb = newValueDb;
        if (newValueDb > heldPeakDb) heldPeakDb = newValueDb;

        lastPushedValue = newValueDb;
    }

    void updatePeakHoldDecay() noexcept
    {
        constexpr float decayAlpha = 0.965f;

        if (currentPeakDb < heldPeakDb)
            heldPeakDb = heldPeakDb * decayAlpha + currentPeakDb * (1.0f - decayAlpha);
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 6.0f);

        const float fill01 = juce::jlimit (0.0f, 1.0f, (currentPeakDb + 120.0f) / 126.0f);
        auto fillR = r;
        fillR.setY (r.getBottom() - r.getHeight() * fill01);
        fillR.setHeight (r.getHeight() * fill01);

        g.setColour (juce::Colours::white.withAlpha (0.70f));
        g.fillRoundedRectangle (fillR, 6.0f);

        const float hold01 = juce::jlimit (0.0f, 1.0f, (heldPeakDb + 120.0f) / 126.0f);
        const float yHold = r.getBottom() - r.getHeight() * hold01;

        g.setColour (juce::Colours::white);
        g.drawLine (r.getX(), yHold, r.getRight(), yHold, 1.0f);
    }

private:
    float currentPeakDb = -120.0f;
    float heldPeakDb = -120.0f;
    float lastPushedValue = -120.0f;
};

class HorizontalClampGlueMeter final : public juce::Component
{
public:
    void pushValue (float val01) noexcept
    {
        if (! std::isfinite (val01)) val01 = 0.0f;
        last01 = juce::jlimit (0.0f, 1.0f, val01);
    }

    void setIsClamp (bool isClamp) noexcept
    {
        clampActive = isClamp;
    }

    void paint (juce::Graphics& g) override
    {
        auto r = getLocalBounds().toFloat();

        g.setColour (juce::Colours::white.withAlpha (0.15f));
        g.fillRoundedRectangle (r, 4.0f);

        auto fillR = r;
        fillR.setWidth (r.getWidth() * last01);

        const auto fillCol = (clampActive ? juce::Colours::orangered : juce::Colours::limegreen);
        g.setColour (fillCol.withAlpha (0.70f));
        g.fillRoundedRectangle (fillR, 4.0f);
    }

private:
    float last01 = 0.0f;
    bool clampActive = false;
};

class CompassKnobLookAndFeel;

class CompassMasteringLimiterAudioProcessorEditor final
    : public juce::AudioProcessorEditor
    , private juce::Timer
{
public:
    explicit CompassMasteringLimiterAudioProcessorEditor (CompassMasteringLimiterAudioProcessor&);
    ~CompassMasteringLimiterAudioProcessorEditor() override;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    CompassMasteringLimiterAudioProcessor& processor;

    std::unique_ptr<CompassKnobLookAndFeel> knobLnf;

    juce::Slider drive;
    juce::Slider ceiling;
    juce::Slider trim;
    juce::ComboBox adaptiveBias;
    juce::Slider stereoLink;
    juce::ComboBox oversamplingMin;

    GRHistoryMeter grMeter;
    juce::Label grTitleLabel;
    juce::Rectangle<int> grFullBounds;
    juce::Rectangle<int> grWellBounds;
    juce::Rectangle<int> grModuleBounds;
    float lastGrDb = 0.0f;
    VerticalPeakMeter inTpMeter, outTpMeter;
    HorizontalClampGlueMeter clampMeter, glueMeter;
    juce::Label currentGrLabel;
    juce::Label inTpLabel;
    juce::Label outTpLabel;
    juce::Label lufsSLabel;
    juce::Label lufsILabel;

    juce::Label trimValueLabel;
    juce::Label glueValueLabel;
    juce::Label ceilingValueLabel;

    juce::Label inPeakLabel;
    juce::Label outPeakLabel;
    juce::Label ceilingLabel;
    juce::Label trimLabel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> driveA;
    std::unique_ptr<APVTS::SliderAttachment> ceilingA;
    std::unique_ptr<APVTS::SliderAttachment> trimA;
    std::unique_ptr<APVTS::ComboBoxAttachment> biasA;
    std::unique_ptr<APVTS::SliderAttachment> linkA;
    std::unique_ptr<APVTS::ComboBoxAttachment> osA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassMasteringLimiterAudioProcessorEditor)
};
