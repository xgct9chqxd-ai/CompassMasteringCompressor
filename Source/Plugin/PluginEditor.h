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
    static constexpr int topKnobBand = 140;
    static constexpr int contextRow = 40;
    static constexpr int grMainZone = 120;
    static constexpr int clampGlueBars = 40;
    static constexpr int truthStrip = 30;
    static constexpr int interBandGap = 10;

    // Side meter widths
    static constexpr int leftTpWidth = 60;
    static constexpr int rightTpWidth = 60;

    // Truth strip max width
    static constexpr int truthMaxWidth = 680;
}

class GRHistoryMeter final : public juce::Component
{
public:
    void pushValueDb(float grDb) noexcept
    {
        if (!std::isfinite(grDb))
            grDb = 0.0f;

        // UI contract: GR is stereo authority in positive dB (0..range).
        constexpr float kRangeDb = 24.0f;
        lastGrDb = juce::jlimit(0.0f, kRangeDb, grDb);
        repaint();
    }

    void paint(juce::Graphics &g) override
    {
        //// [CC:UI] GR Meter Pill Heat Gradient
        constexpr float kPadXPx = 12.0f;
        constexpr float kPadYPx = 12.0f;
        constexpr float kGapPx = 3.0f;
        constexpr float kMinBarWPx = 6.0f;
        constexpr int kBarsMax = 40;
        constexpr float kMeterRangeDb = 24.0f;
        constexpr float kCornerRadiusPx = 2.0f;
        constexpr float kLitAlpha = 0.52f;
        constexpr float kUnlitAlpha = 0.06f;

        //// [CC:UI] GR Meter Screen Background
        g.fillAll(juce::Colour(0xFF050505));

        //// [CC:UI] GR Meter Header Zone Reserve
        const float headerHeight = 20.0f;

        auto ledArea = getLocalBounds().toFloat().reduced(kPadXPx, kPadYPx);

        const auto headerArea = ledArea.withHeight(headerHeight);
        const auto barsArea = ledArea.withTrimmedTop(headerHeight);

        //// [CC:UI] GR Meter Glass Well + Inner Shadow
        constexpr float kWellRadiusPx = 4.0f;
        constexpr float kWellFillAlpha = 0.28f;
        constexpr float kInnerShadowTopA = 0.45f;
        constexpr float kInnerShadowBotA = 0.35f;
        constexpr float kInnerShadowFracH = 0.18f;

        const auto glassWell = ledArea;
        g.setColour(juce::Colours::black.withAlpha(kWellFillAlpha));
        g.fillRoundedRectangle(glassWell, kWellRadiusPx);

        {
            g.saveState();
            g.reduceClipRegion(glassWell.toNearestInt());

            const float shH = juce::jmax(2.0f, std::round(glassWell.getHeight() * kInnerShadowFracH));

            const auto topSh = glassWell.withHeight(shH);
            juce::ColourGradient topG(juce::Colours::black.withAlpha(kInnerShadowTopA),
                                      topSh.getCentreX(), topSh.getY(),
                                      juce::Colours::transparentBlack,
                                      topSh.getCentreX(), topSh.getBottom(),
                                      false);
            g.setGradientFill(topG);
            g.fillRect(topSh);

            const auto botSh = glassWell.withY(glassWell.getBottom() - shH).withHeight(shH);
            juce::ColourGradient botG(juce::Colours::transparentBlack,
                                      botSh.getCentreX(), botSh.getY(),
                                      juce::Colours::black.withAlpha(kInnerShadowBotA),
                                      botSh.getCentreX(), botSh.getBottom(),
                                      false);
            g.setGradientFill(botG);
            g.fillRect(botSh);

            g.restoreState();
        }

        const float ledW = juce::jmax(0.0f, ledArea.getWidth());
        const float denom = (kMinBarWPx + kGapPx);
        const int barsFit = (denom > 0.0f) ? (int)std::floor((ledW + kGapPx) / denom) : kBarsMax;
        const int bars = juce::jlimit(1, kBarsMax, barsFit);

        const float totalGapW = kGapPx * (float)(bars - 1);
        const float rawBarW = (ledW - totalGapW) / (float)bars;
        const float barWf = juce::jmax(kMinBarWPx, rawBarW);

        const float lit = (kMeterRangeDb > 0.0f) ? ((lastGrDb / kMeterRangeDb) * (float)bars) : 0.0f;

        const juce::Colour cLow = juce::Colour(0xFF602020);
        const juce::Colour cHigh = juce::Colour(0xFFFFD700);

        const float x0 = std::round(barsArea.getX());
        const float y0 = std::round(barsArea.getY());
        const float h = std::round(barsArea.getHeight());

        float x = x0;
        for (int i = 0; i < bars; ++i)
        {
            const float w = std::round(barWf);
            juce::Rectangle<float> b(x, y0, w, h);

            const float t = (bars > 1) ? ((float)i / (float)(bars - 1)) : 0.0f;
            const juce::Colour base = cLow.interpolatedWith(cHigh, t);

            g.setColour(base.withAlpha(i < lit ? kLitAlpha : kUnlitAlpha));

            const float r = juce::jmin(kCornerRadiusPx, 0.5f * w, 0.5f * h);
            g.fillRoundedRectangle(b, r);

            x += w + kGapPx;
        }

        //// [CC:UI] GR Meter Header Readout
        constexpr float kHeaderFontPx = 11.0f;
        constexpr float kHeaderTitleAlpha = 0.40f;
        constexpr float kHeaderValueAlpha = 0.65f;

        constexpr int kHeaderTextXPx = 4;
        constexpr int kHeaderTextYPx = 2;

        const auto hb = headerArea.toNearestInt();
        const int halfW = hb.getWidth() / 2;

        const juce::Rectangle<int> left(hb.getX() + kHeaderTextXPx,
                                        hb.getY() + kHeaderTextYPx,
                                        juce::jmax(0, halfW - kHeaderTextXPx),
                                        juce::jmax(0, hb.getHeight() - kHeaderTextYPx));

        const juce::Rectangle<int> right(hb.getX() + halfW,
                                         hb.getY() + kHeaderTextYPx,
                                         juce::jmax(0, hb.getWidth() - halfW - kHeaderTextXPx),
                                         juce::jmax(0, hb.getHeight() - kHeaderTextYPx));

        g.setFont(juce::Font(juce::FontOptions(kHeaderFontPx)));
        g.setColour(juce::Colours::white.withAlpha(kHeaderTitleAlpha));
        g.drawText("GAIN REDUCTION", left, juce::Justification::left);

        const juce::String grText = juce::String(lastGrDb, 1) + " dB";
        g.setFont(juce::Font(juce::FontOptions(juce::Font::getDefaultMonospacedFontName(), kHeaderFontPx, juce::Font::plain)));
        g.setColour(juce::Colour(0xFFE6A532).withAlpha(kHeaderValueAlpha));
        g.drawText(grText, right, juce::Justification::right);

        //// [CC:UI] GR Meter Glass Gloss Reflection
        constexpr float kGlossAlpha = 0.06f;
        constexpr float kGlossFracH = 0.30f;

        const auto cover = getLocalBounds().toFloat();
        const float glossH = juce::jmax(2.0f, std::round(cover.getHeight() * kGlossFracH));
        const auto gloss = cover.withHeight(glossH);

        juce::ColourGradient glossG(juce::Colours::white.withAlpha(kGlossAlpha),
                                    gloss.getCentreX(), gloss.getY(),
                                    juce::Colours::transparentWhite,
                                    gloss.getCentreX(), gloss.getBottom(),
                                    false);
        g.setGradientFill(glossG);
        g.fillRoundedRectangle(cover, kWellRadiusPx);
    }

private:
    float lastGrDb = 0.0f;
};

class StereoVerticalLedMeter final : public juce::Component
{
public:
    void pushValueDbLR(float lDb, float rDb) noexcept
    {
        if (!std::isfinite(lDb))
            lDb = kDbFloorDb;
        if (!std::isfinite(rDb))
            rDb = kDbFloorDb;

        lDb = juce::jlimit(kDbFloorDb, kDbCeilDb, lDb);
        rDb = juce::jlimit(kDbFloorDb, kDbCeilDb, rDb);

        currentDb[0] = lDb;
        currentDb[1] = rDb;

        if (lDb > heldDb[0])
            heldDb[0] = lDb;
        if (rDb > heldDb[1])
            heldDb[1] = rDb;
    }

    void updatePeakHoldDecay() noexcept
    {
        //// [CML:UI] Stereo TP Meter Hold — UI Thread Decay
        constexpr float kHoldDecayAlpha01 = 0.965f;

        for (int c = 0; c < 2; ++c)
        {
            if (currentDb[c] < heldDb[c])
                heldDb[c] = heldDb[c] * kHoldDecayAlpha01 + currentDb[c] * (1.0f - kHoldDecayAlpha01);
        }
    }

    void resetPeakHoldToCurrent() noexcept
    {
        //// [CML:UI] Stereo TP Meter Hold — Hard Reset
        heldDb[0] = currentDb[0];
        heldDb[1] = currentDb[1];
    }

    //// [CML:UI] Stereo TP Meter Decay Activity
    bool hasActiveDecay() const noexcept
    {
        //// [CML:UI] Stereo TP Meter Hold — Active Decay Test
        constexpr float kHoldActiveEpsDb = 0.05f;

        return (heldDb[0] - currentDb[0]) > kHoldActiveEpsDb
            || (heldDb[1] - currentDb[1]) > kHoldActiveEpsDb;
    }

    void paint(juce::Graphics &g) override
    {
        //// [CML:UI] Stereo TP Meter LEDs — L/R Lanes
        auto r = getLocalBounds().toFloat();

        constexpr float kBgStrokeAlpha01 = 0.09f;
        constexpr float kBgStrokePx = 1.0f;
        constexpr float kCornerPx = 6.0f;

        constexpr float kInsetXPx = 2.0f;
        constexpr float kInsetYPx = 10.0f;

        constexpr int kSegN = 44;
        constexpr float kSegGapPx = 1.0f;

        constexpr float kLaneGapPx = 6.0f;
        constexpr float kMinSegHPx = 1.0f;

        constexpr float kActiveAlpha01 = 0.70f;
        constexpr float kInactiveAlpha01 = 0.12f;

        g.setColour(juce::Colours::white.withAlpha(kBgStrokeAlpha01));
        g.drawRoundedRectangle(r, kCornerPx, kBgStrokePx);

        auto a = r.reduced(kInsetXPx, kInsetYPx);

        //// [CML:UI] Meter Scale Strip — Shared L/R (Adaptive Width)
        constexpr float kScaleStripWPx = 18.0f;
        constexpr float kScaleGapPx = 2.0f;

        constexpr float kMinLaneWPx = 10.0f;
        constexpr float kMinScaleStripWPx = 10.0f;
        constexpr float kMinTextScaleStripWPx = 14.0f;

        constexpr float kMinLanesAreaWPx = 2.0f * kMinLaneWPx + kLaneGapPx;

        auto lanesArea = a;
        juce::Rectangle<float> scaleL, scaleR;

        bool drawScaleTicks = false;
        bool drawScaleText = false;

        const float minNeedW = 2.0f * kMinScaleStripWPx + 2.0f * kScaleGapPx + kMinLanesAreaWPx;
        if (lanesArea.getWidth() >= minNeedW)
        {
            drawScaleTicks = true;

            const float availForScale = lanesArea.getWidth() - kMinLanesAreaWPx - 2.0f * kScaleGapPx;
            const float scaleStripW = juce::jlimit(kMinScaleStripWPx, kScaleStripWPx, 0.5f * availForScale);

            scaleL = lanesArea.removeFromLeft(scaleStripW);
            lanesArea.removeFromLeft(kScaleGapPx);
            scaleR = lanesArea.removeFromRight(scaleStripW);
            lanesArea.removeFromRight(kScaleGapPx);

            drawScaleText = (scaleL.getWidth() >= kMinTextScaleStripWPx) && (scaleR.getWidth() >= kMinTextScaleStripWPx);
        }

        const float laneW = (lanesArea.getWidth() - kLaneGapPx) * 0.5f;
        auto laneL = juce::Rectangle<float>(lanesArea.getX(), a.getY(), laneW, a.getHeight());
        auto laneR = juce::Rectangle<float>(lanesArea.getX() + laneW + kLaneGapPx, a.getY(), laneW, a.getHeight());

        auto drawLane = [&](juce::Rectangle<float> lane, int ch)
        {
            //// [CML:UI] Stereo TP Meter Palette — Threshold Mapped LEDs
            constexpr float kDesatMix01 = 0.45f; // 0=full hue, 1=grey

            constexpr float kGreyR = 0.62f;
            constexpr float kGreyG = 0.62f;
            constexpr float kGreyB = 0.62f;

            constexpr float kGreenR = 0.30f;
            constexpr float kGreenG = 0.68f;
            constexpr float kGreenB = 0.46f;

            constexpr float kYellR = 0.95f;
            constexpr float kYellG = 0.86f;
            constexpr float kYellB = 0.40f;

            constexpr float kAmberR = 0.78f;
            constexpr float kAmberG = 0.44f;
            constexpr float kAmberB = 0.18f;

            constexpr float kRedR = 0.90f;
            constexpr float kRedG = 0.22f;
            constexpr float kRedB = 0.12f;

            constexpr float kGreenTopDb = -6.0f;
            constexpr float kYellowTopDb = 0.0f;

            const juce::Colour cGrey = juce::Colour::fromFloatRGBA(kGreyR, kGreyG, kGreyB, 1.0f);
            const juce::Colour cGreen = juce::Colour::fromFloatRGBA(kGreenR, kGreenG, kGreenB, 1.0f);
            const juce::Colour cYell = juce::Colour::fromFloatRGBA(kYellR, kYellG, kYellB, 1.0f);
            const juce::Colour cAmber = juce::Colour::fromFloatRGBA(kAmberR, kAmberG, kAmberB, 1.0f);
            const juce::Colour cRed = juce::Colour::fromFloatRGBA(kRedR, kRedG, kRedB, 1.0f);

            const float totalGapH = kSegGapPx * (float)(kSegN - 1);
            const float rawSegH = (lane.getHeight() - totalGapH) / (float)kSegN;
            const float segHPx = juce::jmax(kMinSegHPx, rawSegH);

            const float v01 = juce::jlimit(0.0f, 1.0f, (currentDb[ch] - kDbFloorDb) / kDbSpanDb);
            const int litN = (int)(v01 * (float)kSegN + 0.5f);
            for (int i = 0; i < kSegN; ++i)
            {
                const int idxFromBottom = i;
                const float y = lane.getBottom() - (float)(idxFromBottom + 1) * segHPx - (float)idxFromBottom * kSegGapPx;
                juce::Rectangle<float> seg(lane.getX(), y, lane.getWidth(), segHPx);

                const bool isActive = (idxFromBottom < litN);

                if (isActive)
                {
                    const float segDb = kDbFloorDb + ((float)(idxFromBottom + 1) / (float)kSegN) * kDbSpanDb;

                    juce::Colour base;
                    if (segDb <= kGreenTopDb)
                    {
                        base = cGreen;
                    }
                    else if (segDb <= kYellowTopDb)
                    {
                        const float t01 = juce::jlimit(0.0f, 1.0f, (segDb - kGreenTopDb) / (kYellowTopDb - kGreenTopDb));
                        base = cGreen.interpolatedWith(cYell, t01);
                    }
                    else
                    {
                        const float t01 = juce::jlimit(0.0f, 1.0f, (segDb - kYellowTopDb) / (kDbCeilDb - kYellowTopDb));
                        base = cAmber.interpolatedWith(cRed, t01);
                    }

                    base = base.interpolatedWith(cGrey, kDesatMix01);
                    g.setColour(base.withAlpha(kActiveAlpha01));
                }
                else
                {
                    g.setColour(cGrey.withAlpha(kInactiveAlpha01));
                }

                //// [CML:UI] Side Meter Segment Radius
                g.fillRoundedRectangle(seg, 2.0f);
            }

            const float h01 = juce::jlimit(0.0f, 1.0f, (heldDb[ch] - kDbFloorDb) / kDbSpanDb);
            const float yHold = lane.getBottom() - lane.getHeight() * h01;

            g.setColour(juce::Colours::white);
            g.drawLine(lane.getX(), yHold, lane.getRight(), yHold, 1.0f);
        };

        drawLane(laneL, 0);
        drawLane(laneR, 1);

        if (drawScaleTicks)
        {
            //// [CML:UI] Meter Side Scale — Shared Ticks + Optional dB Labels
            constexpr float kTickStrokePx = 1.0f;
            constexpr float kTickMinorLenPx = 5.0f;
            constexpr float kTickMajorLenPx = 9.0f;

            constexpr float kLabelFontPx = 10.0f;
            constexpr float kLabelHPx = 13.0f;
            constexpr float kLabelInsetXPx = 1.0f;

            constexpr int kTickMinorStepDb = 6;
            constexpr int kTickMajorStepDb = 12;

            constexpr float kLoA01 = 0.22f;
            constexpr float kHiA01 = 0.12f;
            constexpr float kTextA01 = 0.70f;

            auto drawScaleFor = [&](juce::Rectangle<float> scale, juce::Rectangle<float> laneRef, bool isRightScale)
            {
                auto drawTickAtDb = [&](float tickDb, bool isMajor)
                {
                    const float h01s = juce::jlimit(0.0f, 1.0f, (tickDb - kDbFloorDb) / kDbSpanDb);
                    const float y = laneRef.getBottom() - laneRef.getHeight() * h01s;

                    const float tickLen = isMajor ? kTickMajorLenPx : kTickMinorLenPx;

                    const float xEdge = isRightScale ? scale.getX() : scale.getRight();
                    const float xTick0 = isRightScale ? xEdge : (xEdge - tickLen);
                    const float xTick1 = isRightScale ? (xEdge + tickLen) : xEdge;

                    g.setColour(juce::Colours::black.withAlpha(kLoA01));
                    g.drawLine(xTick0, y, xTick1, y, kTickStrokePx);
                    g.setColour(juce::Colours::white.withAlpha(kHiA01));
                    g.drawLine(xTick0, y, xTick1, y, kTickStrokePx);

                    if (drawScaleText && isMajor)
                    {
                        auto rr = juce::Rectangle<float>(scale.getX() + kLabelInsetXPx,
                                                         y - 0.5f * kLabelHPx,
                                                         scale.getWidth() - 2.0f * kLabelInsetXPx,
                                                         kLabelHPx)
                                      .toNearestInt();

                        g.setFont(kLabelFontPx);
                        g.setColour(juce::Colours::white.withAlpha(kTextA01));
                        g.drawText(juce::String((int)tickDb),
                                   rr,
                                   isRightScale ? juce::Justification::left : juce::Justification::right,
                                   false);
                    }
                };

                const int dbTop = (int)kDbCeilDb;
                const int dbBot = (int)kDbFloorDb;

                for (int db = dbTop; db >= dbBot; db -= kTickMinorStepDb)
                {
                    const bool isMajor = ((db % kTickMajorStepDb) == 0);
                    drawTickAtDb((float)db, isMajor);
                }
            };

            drawScaleFor(scaleL, laneL, false);
            drawScaleFor(scaleR, laneR, true);
        }
    }

private:
    static constexpr float kDbFloorDb = -120.0f;
    static constexpr float kDbCeilDb = 6.0f;
    static constexpr float kDbSpanDb = (kDbCeilDb - kDbFloorDb);

    float currentDb[2] = {kDbFloorDb, kDbFloorDb};
    float heldDb[2] = {kDbFloorDb, kDbFloorDb};
};

class CompassKnobLookAndFeel;

class CompassMasteringLimiterAudioProcessorEditor final
    : public juce::AudioProcessorEditor,
      private juce::Timer
{
public:
    explicit CompassMasteringLimiterAudioProcessorEditor(CompassMasteringLimiterAudioProcessor &);
    ~CompassMasteringLimiterAudioProcessorEditor() override;

    void paint(juce::Graphics &) override;
    void resized() override;

private:
    void timerCallback() override;

    CompassMasteringLimiterAudioProcessor &processor;

    std::unique_ptr<CompassKnobLookAndFeel> knobLnf;

    juce::Slider drive;
    juce::Slider ceiling;
    juce::Slider trim;
    juce::ComboBox adaptiveBias;
    juce::TextButton stereoLink;
    juce::ComboBox oversamplingMin;

    GRHistoryMeter grMeter;
    juce::Label grTitleLabel;
    juce::Rectangle<int> grFullBounds;
    juce::Rectangle<int> grWellBounds;
    juce::Rectangle<int> grModuleBounds;
    float lastGrDb = 0.0f;
    StereoVerticalLedMeter inTpMeter, outTpMeter;
    juce::Label currentGrLabel;
    juce::Label inTpLabel;
    juce::Label outTpLabel;
    juce::Label lufsSLabel;
    juce::Label lufsILabel;


    juce::Label inPeakLabel;
    juce::Label outPeakLabel;
    juce::Label ceilingLabel;
    juce::Label trimLabel;

    using APVTS = juce::AudioProcessorValueTreeState;
    std::unique_ptr<APVTS::SliderAttachment> driveA;
    std::unique_ptr<APVTS::SliderAttachment> ceilingA;
    std::unique_ptr<APVTS::SliderAttachment> trimA;
    std::unique_ptr<APVTS::ComboBoxAttachment> biasA;
    std::unique_ptr<APVTS::ButtonAttachment> linkA;
    std::unique_ptr<APVTS::ComboBoxAttachment> osA;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(CompassMasteringLimiterAudioProcessorEditor)
};
