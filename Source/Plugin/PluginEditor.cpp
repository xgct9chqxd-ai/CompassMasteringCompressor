#include "PluginEditor.h"

static void setRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 20);

    // Force visibility on dark UI (neutral, high-contrast; no "good/bad" color semantics)
    s.setColour (juce::Slider::rotarySliderOutlineColourId, juce::Colours::white.withAlpha (0.35f));
    s.setColour (juce::Slider::rotarySliderFillColourId,    juce::Colours::white.withAlpha (0.85f));
    s.setColour (juce::Slider::thumbColourId,               juce::Colours::white.withAlpha (0.90f));

    s.setColour (juce::Slider::textBoxTextColourId,         juce::Colours::white.withAlpha (0.90f));
    s.setColour (juce::Slider::textBoxOutlineColourId,      juce::Colours::white.withAlpha (0.25f));
    s.setColour (juce::Slider::textBoxBackgroundColourId,   juce::Colours::transparentBlack);
}

class CompassKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider) override
    {
        const auto knobBounds = juce::Rectangle<float> ((float) x, (float) y, (float) width, (float) height);
        drawMachinedKnob (g, knobBounds, sliderPosProportional, rotaryStartAngle, rotaryEndAngle, slider);
    }

private:
    void drawMachinedKnob (juce::Graphics& g,
                           juce::Rectangle<float> knobBounds,
                           float sliderPosProportional,
                           float rotaryStartAngle,
                           float rotaryEndAngle,
                           juce::Slider& slider)
    {
        const float cx = knobBounds.getCentreX();
        const float cy = knobBounds.getCentreY();
        const float r  = juce::jmin (knobBounds.getWidth(), knobBounds.getHeight()) * 0.5f;

        const auto boundsF = knobBounds;
        const float radiusBase = boundsF.getWidth() * 0.5f;

        const float clipInset    = juce::jlimit (0.0f, radiusBase * 0.10f, radiusBase * 0.06f);
        const float shadowDy     = juce::jlimit (0.0f, radiusBase * 0.10f, radiusBase * 0.03f);
        const float faceInset    = juce::jlimit (0.0f, radiusBase * 0.10f, radiusBase * 0.06f);
        const float aoInset      = juce::jlimit (0.0f, radiusBase * 0.10f, radiusBase * 0.03f);
        const float rimThickness = juce::jlimit (radiusBase * 0.10f, radiusBase * 0.15f, radiusBase * 0.12f);

        const auto outer      = boundsF.reduced (clipInset);
        const auto shadowRect = outer.translated (0.0f, shadowDy);
        const auto rimOuter   = outer;
        const auto rimInner   = rimOuter.reduced (rimThickness);
        const auto face       = rimInner.reduced (faceInset);
        const auto aoRing     = rimInner.reduced (aoInset);

        const auto center = face.getCentre();
        const float radius = face.getWidth() * 0.5f;

        // Step 3A — Contact shadow (grounds knob to panel). Draw beneath knob layers.
        {
            const juce::Colour baseColour = juce::Colours::black;

            const float shadowExpandStep = juce::jlimit (0.0f, radius * 0.08f, radius * 0.03f);

            constexpr float a0 = 0.20f;
            constexpr float a1 = 0.10f;
            constexpr float a2 = 0.05f;

            for (int i = 0; i < 3; ++i)
            {
                const float ai = (i == 0 ? a0 : (i == 1 ? a1 : a2));
                const float e  = shadowExpandStep * (float) i;

                auto rShadow = shadowRect.expanded (e, e);

                juce::ColourGradient grad (baseColour.withAlpha (ai),
                                          rShadow.getCentreX(), rShadow.getBottom(),
                                          baseColour.withAlpha (0.0f),
                                          rShadow.getCentreX(), rShadow.getY(),
                                          false);

                g.setGradientFill (grad);
                g.fillEllipse (rShadow);
            }
        }

        // Step 3B — Ambient Occlusion ring (knob meets panel). Draw beneath rim/face layers.
        {
            const juce::Colour base = juce::Colours::black;

            // Donut ring: rimOuter minus rimInner (even-odd fill).
            juce::Path ringPath;
            ringPath.addEllipse (rimOuter);
            ringPath.addEllipse (rimInner);
            ringPath.setUsingNonZeroWinding (false);

            constexpr float aoAlphaBottom = 0.10f;
            constexpr float aoAlphaTop    = 0.00f;

            juce::ColourGradient aoGrad (base.withAlpha (aoAlphaBottom),
                                         rimOuter.getCentreX(), rimOuter.getBottom(),
                                         base.withAlpha (aoAlphaTop),
                                         rimOuter.getCentreX(), rimOuter.getY(),
                                         false);

            g.setGradientFill (aoGrad);
            g.fillPath (ringPath);

            // Mild inner-edge emphasis band near rimInner.
            const float innerEmphasis = juce::jlimit (0.0f, radius * 0.06f, radius * 0.02f);

            const float minDim = juce::jmin (rimInner.getWidth(), rimInner.getHeight());
            const float safeE  = juce::jlimit (0.0f, minDim * 0.25f, innerEmphasis);

            auto innerBandOuter = rimInner.expanded (safeE, safeE);
            auto innerBandInner = rimInner.reduced  (safeE);

            juce::Path innerBand;
            innerBand.addEllipse (innerBandOuter);
            innerBand.addEllipse (innerBandInner);
            innerBand.setUsingNonZeroWinding (false);

            constexpr float innerAlphaBottom = 0.08f;

            juce::ColourGradient innerGrad (base.withAlpha (innerAlphaBottom),
                                            rimOuter.getCentreX(), rimOuter.getBottom(),
                                            base.withAlpha (0.0f),
                                            rimOuter.getCentreX(), rimOuter.getY(),
                                            false);

            g.setGradientFill (innerGrad);
            g.fillPath (innerBand);
        }

        const juce::Colour cDarkMid (0xFF2A2A2A);
        const juce::Colour cDark    (0xFF111111);
        const juce::Colour cBlack   (0xFF000000);
        const juce::Colour cRim     (0xFF333333);

        const float ro = r - 0.5f;


        // Step 4A — Machined rim: Rim base fill (dark neutral base). Draw beneath rim highlights and face.
        {
            juce::Path rimPath;
            rimPath.addEllipse (rimOuter);
            rimPath.addEllipse (rimInner);
            rimPath.setUsingNonZeroWinding (false);  // even-odd fill to subtract inner

            const juce::Colour rimBase = juce::Colour::fromFloatRGBA (0.12f, 0.12f, 0.12f, 1.0f);
            g.setColour (rimBase);
            g.fillPath (rimPath);
        }

        // Step 4B — Machined rim: Rim bevel gradient (top-left brighter, bottom-right darker).
        {
            juce::Path rimPath;
            rimPath.addEllipse (rimOuter);
            rimPath.addEllipse (rimInner);
            rimPath.setUsingNonZeroWinding (false);  // even-odd fill to subtract inner

            const float off = radius * 0.25f;
            const juce::Point<float> pLight = center.translated (-off, -off);
            const juce::Point<float> pDark  = center.translated ( off,  off);

            const juce::Colour light = juce::Colour::fromFloatRGBA (0.22f, 0.22f, 0.22f, 1.0f);
            const juce::Colour dark  = juce::Colour::fromFloatRGBA (0.08f, 0.08f, 0.08f, 1.0f);

            juce::ColourGradient grad (light, pLight.x, pLight.y,
                                      dark,  pDark.x,  pDark.y,
                                      true);

            g.setGradientFill (grad);
            g.fillPath (rimPath);
        }

        // Step 4C — Machined rim: Rim catch light (thin highlight arc on rimOuter).
        {
            const float strokeW = juce::jlimit (radius * 0.01f, radius * 0.03f, radius * 0.02f);

            const float startRad = juce::degreesToRadians (300.0f);
            const float endRad   = juce::degreesToRadians ( 60.0f);

            juce::Path catchPath;
            catchPath.addArc (rimOuter.getX(), rimOuter.getY(),
                              rimOuter.getWidth(), rimOuter.getHeight(),
                              startRad, endRad, true);

            const juce::Colour catchCol = juce::Colour::fromFloatRGBA (0.85f, 0.82f, 0.78f, 0.18f);
            g.setColour (catchCol);
            g.strokePath (catchPath, juce::PathStrokeType (strokeW,
                                                          juce::PathStrokeType::curved,
                                                          juce::PathStrokeType::rounded));
        }

        // Step 5A — Face dome: base fill (solid).
        {
            const juce::Colour faceBase = juce::Colour::fromFloatRGBA (0.16f, 0.16f, 0.16f, 1.0f);
            g.setColour (faceBase);
            g.fillEllipse (face);
        }

        // Step 5B — Face dome: subtle dome gradient (no hotspot).
        {
            const juce::Colour domeCenter = juce::Colour::fromFloatRGBA (0.19f, 0.19f, 0.19f, 1.0f);
            const juce::Colour domeEdge   = juce::Colour::fromFloatRGBA (0.16f, 0.16f, 0.16f, 1.0f);

            const float off = radius * 0.10f;
            const juce::Point<float> domeP = center.translated (-off, -off);

            juce::ColourGradient domeGrad (domeCenter, domeP.x, domeP.y,
                                           domeEdge,   center.x, center.y,
                                           true);

            g.setGradientFill (domeGrad);
            g.fillEllipse (face);
        }

        // Knob fill (radial gradient)
        if (ro > 0.0f)
        {
            const float gx = cx - 0.1f * r;
            const float gy = cy - 0.1f * r;

            juce::ColourGradient grad (cDarkMid, gx, gy, cBlack, cx, cy, true);
            grad.addColour (0.70, cDark);

            g.setGradientFill (grad);
            g.fillEllipse (cx - ro, cy - ro, 2.0f * ro, 2.0f * ro);
        }

        // Edge ticks (static)
        if (r > 0.0f)
        {
            constexpr int   tickCount = 48;
            constexpr float tickLen   = 8.0f;
            constexpr float tickW     = 1.0f;
            constexpr float tickAlpha = 0.55f;

            const float tickOuter = r - 2.0f;
            const float tickInner = tickOuter - tickLen;

            if (tickInner > 0.0f && tickOuter > tickInner)
            {
                g.setColour (juce::Colours::white.withAlpha (tickAlpha));

                const float startA = rotaryStartAngle;
                const float endA   = rotaryEndAngle;
                const float rotA   = juce::MathConstants<float>::halfPi + juce::MathConstants<float>::pi; // 90 + 180 degrees

                for (int i = 0; i < tickCount; ++i)
                {
                    const float t = (float) i / (float) (tickCount - 1);
                    const float aBase = startA + t * (endA - startA);
                    const float a = aBase + rotA;

                    const float dx = std::cos (a);
                    const float dy = std::sin (a);

                    const float x1 = cx + tickInner * dx;
                    const float y1 = cy + tickInner * dy;
                    const float x2 = cx + tickOuter * dx;
                    const float y2 = cy + tickOuter * dy;

                    g.drawLine (x1, y1, x2, y2, tickW);
                }
            }
        }

        // Subtle rim stroke
        if (ro > 0.0f)
        {
            g.setColour (cRim);
            g.drawEllipse (cx - ro, cy - ro, 2.0f * ro, 2.0f * ro, 1.0f);
        }

        // Inner shadow (top bias)
        {
            const float rr = r - 2.0f;
            if (rr > 0.0f)
            {
                juce::ColourGradient sh (juce::Colours::black.withAlpha (0.80f),
                                        cx, cy - rr,
                                        juce::Colours::transparentBlack,
                                        cx, cy,
                                        false);
                g.setGradientFill (sh);
                g.fillEllipse (cx - rr, cy - rr, 2.0f * rr, 2.0f * rr);
            }
        }

        // Faint highlight (bottom bias)
        {
            const float rr = r - 2.0f;
            if (rr > 0.0f)
            {
                juce::ColourGradient hi (juce::Colours::transparentWhite,
                                        cx, cy,
                                        juce::Colours::white.withAlpha (0.05f),
                                        cx, cy + rr,
                                        false);
                g.setGradientFill (hi);
                g.fillEllipse (cx - rr, cy - rr, 2.0f * rr, 2.0f * rr);
            }
        }

        // Indicator (line) — replaces JUCE dot/pointer; mapping unchanged.
        const float angle = rotaryStartAngle
                          + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

        const float lineW     = 3.0f;
        const float lineLen   = 0.9f * r;       // 45% of diameter
        const float lineInset = 0.10f * r;      // gap from center
        const float lineR     = 2.0f;

        // Subtle glow (no blur): one outer pass, then main pass.
        {
            juce::Path glow;
            glow.addRoundedRectangle (-0.5f * (lineW + 2.0f),
                                      -(lineInset + lineLen),
                                      (lineW + 2.0f),
                                      lineLen,
                                      lineR + 1.0f);

            g.setColour (juce::Colours::white.withAlpha (0.10f));
            g.fillPath (glow, juce::AffineTransform::rotation (angle).translated (cx, cy));
        }

        {
            juce::Path line;
            line.addRoundedRectangle (-0.5f * lineW,
                                      -(lineInset + lineLen),
                                      lineW,
                                      lineLen,
                                      lineR);

            auto col = slider.findColour (juce::Slider::thumbColourId);
            g.setColour (col.isTransparent() ? juce::Colours::white.withAlpha (0.90f) : col);
            g.fillPath (line, juce::AffineTransform::rotation (angle).translated (cx, cy));
        }
    }
};

CompassMasteringLimiterAudioProcessorEditor::CompassMasteringLimiterAudioProcessorEditor (CompassMasteringLimiterAudioProcessor& p)
: juce::AudioProcessorEditor (&p), processor (p)
{
    setSize (900, 420);

    knobLnf = std::make_unique<CompassKnobLookAndFeel>();

    setRotary (drive);
    drive.setLookAndFeel (knobLnf.get());
    addAndMakeVisible (drive);

    setRotary (ceiling);
    ceiling.setLookAndFeel (knobLnf.get());
    addAndMakeVisible (ceiling);
    adaptiveBias.addItem ("Transparent", 1);
    adaptiveBias.addItem ("Balanced", 2);
    adaptiveBias.addItem ("Aggressive", 3);
    addAndMakeVisible (adaptiveBias);

    stereoLink.setSliderStyle (juce::Slider::LinearHorizontal);
    stereoLink.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
    addAndMakeVisible (stereoLink);

    oversamplingMin.addItem ("2x", 1);
    oversamplingMin.addItem ("4x", 2);
    oversamplingMin.addItem ("8x", 3);
    addAndMakeVisible (oversamplingMin);

    addAndMakeVisible (grMeter);
    grMeter.setInterceptsMouseClicks (false, false);
    grMeter.toBack();
    grMeter.setAlpha (0.0f);
    grMeter.setEnabled (false);

    //// [CML:UI] GR title label — display-only header
    grTitleLabel.setText ("GR", juce::dontSendNotification);
    grTitleLabel.setJustificationType (juce::Justification::centred);
    grTitleLabel.setFont (juce::Font (34.0f, juce::Font::bold));
    grTitleLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.88f));
    grTitleLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    grTitleLabel.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (grTitleLabel);

    currentGrLabel.setJustificationType (juce::Justification::centred);
    currentGrLabel.setFont (juce::Font (22.0f, juce::Font::bold));
    currentGrLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.88f));
    currentGrLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    currentGrLabel.setInterceptsMouseClicks (false, false);
    currentGrLabel.setText ("0.0 dB", juce::dontSendNotification);
    addAndMakeVisible (currentGrLabel);

    //// [CML:UI] GR hidden components — paint-owned render
    currentGrLabel.setVisible (false);
    grTitleLabel.setVisible (false);

    inTpLabel.setJustificationType (juce::Justification::centredLeft);
    inTpLabel.setFont (juce::Font (13.0f));
    inTpLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    inTpLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    inTpLabel.setInterceptsMouseClicks (false, false);
    inTpLabel.setText ("IN TP: -120.0 dBTP", juce::dontSendNotification);
    addAndMakeVisible (inTpLabel);

    outTpLabel.setJustificationType (juce::Justification::centredLeft);
    outTpLabel.setFont (juce::Font (13.0f));
    outTpLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    outTpLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    outTpLabel.setInterceptsMouseClicks (false, false);
    outTpLabel.setText ("OUT TP: -120.0 dBTP", juce::dontSendNotification);
    addAndMakeVisible (outTpLabel);

    lufsSLabel.setJustificationType (juce::Justification::centredLeft);
    lufsSLabel.setFont (juce::Font (13.0f));
    lufsSLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    lufsSLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lufsSLabel.setInterceptsMouseClicks (false, false);
    lufsSLabel.setText ("LUFS-S: -120.0", juce::dontSendNotification);
    addAndMakeVisible (lufsSLabel);

    lufsILabel.setJustificationType (juce::Justification::centredLeft);
    lufsILabel.setFont (juce::Font (13.0f));
    lufsILabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    lufsILabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    lufsILabel.setInterceptsMouseClicks (false, false);
    lufsILabel.setText ("LUFS-I: -120.0", juce::dontSendNotification);
    addAndMakeVisible (lufsILabel);

    inPeakLabel.setJustificationType (juce::Justification::centredLeft);
    inPeakLabel.setFont (juce::Font (13.0f));
    inPeakLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    inPeakLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    inPeakLabel.setInterceptsMouseClicks (false, false);
    inPeakLabel.setText ("IN PEAK: -120.0 dBFS", juce::dontSendNotification);
    addAndMakeVisible (inPeakLabel);

    outPeakLabel.setJustificationType (juce::Justification::centredLeft);
    outPeakLabel.setFont (juce::Font (13.0f));
    outPeakLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    outPeakLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    outPeakLabel.setInterceptsMouseClicks (false, false);
    outPeakLabel.setText ("OUT PEAK: -120.0 dBFS", juce::dontSendNotification);
    addAndMakeVisible (outPeakLabel);

    ceilingLabel.setJustificationType (juce::Justification::centredLeft);
    ceilingLabel.setFont (juce::Font (13.0f));
    ceilingLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    ceilingLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    ceilingLabel.setInterceptsMouseClicks (false, false);
    ceilingLabel.setText ("CEILING: -1.0 dBTP", juce::dontSendNotification);
    addAndMakeVisible (ceilingLabel);

    // Trim knob setup
    setRotary (trim);
    trim.setLookAndFeel (knobLnf.get());
    addAndMakeVisible (trim);

    trimLabel.setText ("Trim", juce::dontSendNotification);
    trimLabel.setJustificationType (juce::Justification::centred);
    trimLabel.setFont (juce::Font (12.5f));
    trimLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    addAndMakeVisible (trimLabel);
    trimLabel.setInterceptsMouseClicks (false, false);

    // Value readouts under top rotaries (display-only)
    auto initValueLabel = [&] (juce::Label& l)
    {
        l.setJustificationType (juce::Justification::centred);
        l.setFont (juce::Font (12.5f));
        l.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
        l.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
        l.setInterceptsMouseClicks (false, false);
        addAndMakeVisible (l);
    };

    initValueLabel (trimValueLabel);
    initValueLabel (glueValueLabel);
    initValueLabel (ceilingValueLabel);

    // Meters visibility + clamp/glue flags
    addAndMakeVisible (inTpMeter);
    addAndMakeVisible (outTpMeter);
    addAndMakeVisible (clampMeter);
    addAndMakeVisible (glueMeter);
    clampMeter.setIsClamp (true);
    glueMeter.setIsClamp (false);

    auto& vts = processor.getAPVTS();

    driveA   = std::make_unique<APVTS::SliderAttachment> (vts, "drive", drive);
    ceilingA = std::make_unique<APVTS::SliderAttachment> (vts, "ceiling", ceiling);
    trimA    = std::make_unique<APVTS::SliderAttachment> (vts, "trim", trim);
    biasA    = std::make_unique<APVTS::ComboBoxAttachment> (vts, "adaptive_bias", adaptiveBias);
    linkA    = std::make_unique<APVTS::SliderAttachment> (vts, "stereo_link", stereoLink);
    osA      = std::make_unique<APVTS::ComboBoxAttachment> (vts, "oversampling_min", oversamplingMin);

    startTimerHz (30);
}

CompassMasteringLimiterAudioProcessorEditor::~CompassMasteringLimiterAudioProcessorEditor()
{
    drive.setLookAndFeel (nullptr);
    ceiling.setLookAndFeel (nullptr);
    trim.setLookAndFeel (nullptr);
    knobLnf.reset();
}


void CompassMasteringLimiterAudioProcessorEditor::timerCallback()
{
    const float current = processor.getCurrentGRDb();
    lastGrDb = current;

    grMeter.pushValueDb (current);
    currentGrLabel.setText (juce::String::formatted ("%.1f dB", current), juce::dontSendNotification);

    // Top rotary value readouts (APVTS raw values)
    auto& vts = processor.getAPVTS();
    const float trimDb   = vts.getRawParameterValue ("trim")->load();
    const float glueDb   = vts.getRawParameterValue ("drive")->load();
    const float ceilDbTP = vts.getRawParameterValue ("ceiling")->load();

    trimValueLabel.setText    (juce::String::formatted ("%.1f dB",   trimDb),   juce::dontSendNotification);
    glueValueLabel.setText    (juce::String::formatted ("%.1f dB",   glueDb),   juce::dontSendNotification);
    ceilingValueLabel.setText (juce::String::formatted ("%.1f dBTP", ceilDbTP), juce::dontSendNotification);

    float inTp = -120.0f;
    float outTp = -120.0f;
    if (processor.getCurrentTruePeakDbTP (inTp, outTp))
    {
        inTpMeter.pushValueDb (inTp);
        outTpMeter.pushValueDb (outTp);
        inTpMeter.repaint();
        outTpMeter.repaint();

        inTpLabel.setText  (juce::String::formatted ("IN TP: %.1f",  inTp),  juce::dontSendNotification);
        outTpLabel.setText (juce::String::formatted ("OUT TP: %.1f", outTp), juce::dontSendNotification);
    }

    float lufsS = -120.0f;
    float lufsI = -120.0f;
    if (processor.getCurrentLufsDb (lufsS, lufsI))
    {
        lufsS = juce::jmax (-120.0f, lufsS);
        lufsI = juce::jmax (-120.0f, lufsI);

        lufsSLabel.setText (juce::String::formatted ("LUFS-S: %.1f", lufsS), juce::dontSendNotification);
        lufsILabel.setText (juce::String::formatted ("LUFS-I: %.1f", lufsI), juce::dontSendNotification);
    }

    float inPk = -120.0f;
    float outPk = -120.0f;
    if (processor.getCurrentPeakDbFS (inPk, outPk))
    {
        inPk = juce::jmax (-120.0f, inPk);
        outPk = juce::jmax (-120.0f, outPk);
    }

    repaint (grFullBounds);
}

void CompassMasteringLimiterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // Title
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (14.0f);
    g.drawText ("Compass Mastering Limiter", 16, 10, getWidth() - 32, 20, juce::Justification::left);

    //// [CML:UI] GR module framing — recessed plate + well
    {
        // Compute geometry (all derived from stored rectangles; no magic outside this block)
        constexpr float kOuterInsetPx   = 8.0f;
        constexpr float kCavityExpandPx = 14.0f;
        constexpr float kTabWidthFrac   = 0.22f;
        constexpr float kTabHeightPx    = 34.0f;
        constexpr float kTabRiseFrac    = 0.65f;

        auto outer  = grFullBounds.toFloat().reduced (kOuterInsetPx);
        auto cavity = grWellBounds.toFloat().expanded (kCavityExpandPx, kCavityExpandPx);
        auto well   = grWellBounds.toFloat();

        // Define a header tab centered above the well (reference-style)
        const float tabW = well.getWidth() * kTabWidthFrac;
        const float tabH = kTabHeightPx;
        auto tab = juce::Rectangle<float> (
            well.getCentreX() - 0.5f * tabW,
            well.getY() - (tabH * kTabRiseFrac),
            tabW,
            tabH
        );

        // 1) Outer module plate (vertical gradient + stroke)
        {
            juce::ColourGradient plate (juce::Colours::white.withAlpha (0.08f),
                                        outer.getCentreX(), outer.getY(),
                                        juce::Colours::black.withAlpha (0.45f),
                                        outer.getCentreX(), outer.getBottom(),
                                        false);
            g.setGradientFill (plate);
            g.fillRoundedRectangle (outer, 10.0f);

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (outer, 10.0f, 1.0f);
        }

        // 2) Inner cavity bezel (darker gradient + bevel strokes)
        {
            juce::ColourGradient cav (juce::Colours::black.withAlpha (0.55f),
                                      cavity.getCentreX(), cavity.getY(),
                                      juce::Colours::black.withAlpha (0.80f),
                                      cavity.getCentreX(), cavity.getBottom(),
                                      false);
            g.setGradientFill (cav);
            g.fillRoundedRectangle (cavity, 9.0f);

            g.setColour (juce::Colours::white.withAlpha (0.06f));
            g.drawRoundedRectangle (cavity, 9.0f, 1.0f);

            // Bevel illusion: top/left highlight + bottom/right shadow
            auto c = cavity.reduced (0.5f, 0.5f);
            const float x1 = c.getX();
            const float y1 = c.getY();
            const float x2 = c.getRight();
            const float y2 = c.getBottom();

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawLine (x1, y1, x2, y1, 1.0f);
            g.drawLine (x1, y1, x1, y2, 1.0f);

            g.setColour (juce::Colours::black.withAlpha (0.40f));
            g.drawLine (x1, y2, x2, y2, 1.0f);
            g.drawLine (x2, y1, x2, y2, 1.0f);
        }

        // 3) Inner well floor (near-black gradient + lip strokes)
        {
            juce::ColourGradient wf (juce::Colours::black.withAlpha (0.55f),
                                     well.getCentreX(), well.getY(),
                                     juce::Colours::black.withAlpha (0.85f),
                                     well.getCentreX(), well.getBottom(),
                                     false);
            g.setGradientFill (wf);
            g.fillRoundedRectangle (well, 8.0f);

            g.setColour (juce::Colours::white.withAlpha (0.07f));
            g.drawRoundedRectangle (well, 8.0f, 1.0f);
            g.setColour (juce::Colours::black.withAlpha (0.45f));
            g.drawRoundedRectangle (well.reduced (1.0f, 1.0f), 7.0f, 1.0f);
        }

        // 4) Glass overlay (clip to well, top highlight)
        {
            juce::Graphics::ScopedSaveState ss (g);
            g.reduceClipRegion (well.toNearestInt());

            auto glass = well.reduced (2.0f, 2.0f);
            glass.setHeight (glass.getHeight() * 0.55f);

            juce::ColourGradient grad (juce::Colours::white.withAlpha (0.10f),
                                       glass.getCentreX(), glass.getY(),
                                       juce::Colours::white.withAlpha (0.0f),
                                       glass.getCentreX(), glass.getBottom(),
                                       false);
            g.setGradientFill (grad);
            g.fillRoundedRectangle (glass, 7.0f);
        }

        //// [CML:UI] GR header tab — reference-style badge
        {
            constexpr float kTabCornerPx   = 8.0f;
            constexpr float kTabStrokePx   = 1.0f;
            constexpr float kTitleFontPx   = 22.0f;
            constexpr float kValueFontPx   = 16.0f;
            constexpr float kTitleAlpha    = 0.90f;
            constexpr float kValueAlpha    = 0.85f;

            const float grMagDb = juce::jmax (0.0f, lastGrDb);

            juce::ColourGradient tabGrad (juce::Colours::white.withAlpha (0.09f),
                                          tab.getCentreX(), tab.getY(),
                                          juce::Colours::black.withAlpha (0.55f),
                                          tab.getCentreX(), tab.getBottom(),
                                          false);
            g.setGradientFill (tabGrad);
            g.fillRoundedRectangle (tab, kTabCornerPx);

            g.setColour (juce::Colours::white.withAlpha (0.08f));
            g.drawRoundedRectangle (tab, kTabCornerPx, kTabStrokePx);

            auto tabTop  = tab.withHeight (tab.getHeight() * 0.5f);
            auto tabBot  = tab.withTrimmedTop (tab.getHeight() * 0.5f);

            g.setColour (juce::Colours::white.withAlpha (kTitleAlpha));
            g.setFont (juce::Font (kTitleFontPx, juce::Font::bold));
            g.drawText ("GR", tabTop.toNearestInt(), juce::Justification::centred, false);

            g.setColour (juce::Colours::white.withAlpha (kValueAlpha));
            g.setFont (juce::Font (kValueFontPx, juce::Font::bold));
            g.drawText (juce::String::formatted ("%.1f dB", grMagDb),
                        tabBot.toNearestInt(),
                        juce::Justification::centred,
                        false);
        }
    }

    // Static labels (constitution: labeling for all visible controls)
    auto drawLabelAbove = [&] (const juce::Component& c, const juce::String& label)
    {
        auto r = c.getBounds().translated (0, -18);
        r.setHeight (16);
        g.setColour (juce::Colours::white.withAlpha (0.70f));
        g.setFont (12.5f);
        g.drawText (label, r, juce::Justification::centred, false);
    };

    drawLabelAbove (drive,           "Glue");
    drawLabelAbove (ceiling,         "Ceiling");
    drawLabelAbove (adaptiveBias,    "Adaptive Bias");
    drawLabelAbove (stereoLink,      "Stereo Link");
    drawLabelAbove (oversamplingMin, "Oversampling Min");
}

void CompassMasteringLimiterAudioProcessorEditor::resized()
{
    // Local geometry constants (single source of truth for this layout function)
    const int knobSize        = 135;
    const int knobGap         = 8;
    const int knobRowExtraH   = 60;
    const int valueLabelDY    = 4;
    const int valueLabelH     = 20;

    const int behaviorW       = 220;
    const int behaviorGap     = 40;
    const int behaviorTrimTop = 8;
    const int behaviorH       = 28;

    const int grTrimLR        = 80;
    const int grLabelW        = 120;
    const int grLabelH        = 30;
    const int grLabelDY       = 10;

    const int clampGlueGap    = 10;

    const int truthNumW       = 140;
    const int truthGap        = 20;

    // 1) Start with full bounds
    auto r = getLocalBounds();

    // 2) Outer frame (insets)
    auto framed = r.withTrimmedLeft (Layout::insetL)
                   .withTrimmedRight (Layout::insetR)
                   .withTrimmedTop (Layout::insetT)
                   .withTrimmedBottom (Layout::insetB);

    // 3) Carve out left/right TP meter columns
    auto tpLeftZone = framed.removeFromLeft (Layout::leftTpWidth);
    auto tpRightZone = framed.removeFromRight (Layout::rightTpWidth);

    // 4) Remainder is the center main zone
    auto mainZone = framed;

    // 5) Carve vertical bands from main zone (in order)
    auto bandKnobs = mainZone.removeFromTop (Layout::topKnobBand);
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandContext = mainZone.removeFromTop (Layout::contextRow);
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandGR = mainZone.removeFromTop (Layout::grMainZone);
    grFullBounds = bandGR;  // store BEFORE modifying bandGR
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandMiniBars = mainZone.removeFromTop (Layout::clampGlueBars);
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandTruth = mainZone.removeFromTop (Layout::truthStrip);

    // Tall TP meters
    inTpMeter.setBounds (tpLeftZone);
    outTpMeter.setBounds (tpRightZone);

    // Top: three knob columns (identical) inside bandKnobs
    const int colGap     = 0;
    const int labelAreaH = 24;
    const int knobAreaH  = knobSize;
    const int valueAreaH = 30;

    auto cols = bandKnobs;

    const int colsTotalGap = colGap * 2;
    const int colW = (cols.getWidth() - colsTotalGap) / 3;

    auto col1 = cols.removeFromLeft (colW);
    cols.removeFromLeft (colGap);
    auto col2 = cols.removeFromLeft (colW);
    cols.removeFromLeft (colGap);
    auto col3 = cols.removeFromLeft (colW);

    auto makeAreas = [&] (juce::Rectangle<int> col)
    {
        auto labelArea = col.removeFromTop (labelAreaH);
        auto knobArea  = col.removeFromTop (knobAreaH);
        auto valueArea = col.removeFromTop (valueAreaH);
        return (std::tuple<juce::Rectangle<int>, juce::Rectangle<int>, juce::Rectangle<int>> { labelArea, knobArea, valueArea });
    };

    {
        auto [labelArea, knobArea, valueArea] = makeAreas (col1);
        (void) labelArea;
        trim.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize).translated (56, 18));
        trimValueLabel.setBounds (valueArea);
    }
    {
        auto [labelArea, knobArea, valueArea] = makeAreas (col2);
        (void) labelArea;
        drive.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize).translated (0, 18));
        glueValueLabel.setBounds (valueArea);
    }
    {
        auto [labelArea, knobArea, valueArea] = makeAreas (col3);
        (void) labelArea;
        ceiling.setBounds (knobArea.withSizeKeepingCentre (knobSize, knobSize).translated (-56, 18));
        ceilingValueLabel.setBounds (valueArea);
    }

    // Context band (reserved)
    (void) bandContext;

    // GR band: header + bar + breathing
    auto grHeader  = bandGR.removeFromTop (48);
    auto grBarArea = bandGR.removeFromTop (52);

    adaptiveBias.setBounds (grHeader.removeFromLeft (behaviorW).withTrimmedTop (behaviorTrimTop).withHeight (behaviorH));
    grHeader.removeFromLeft (behaviorGap);
    stereoLink.setBounds (grHeader.withTrimmedTop (behaviorTrimTop).withHeight (behaviorH));

    const int wellPadX = 18;
    const int wellH    = 74;
    auto well = grFullBounds.withTrimmedTop (48)
                           .withTrimmedBottom (6)
                           .reduced (wellPadX, 0)
                           .withHeight (wellH)
                           .withY (grFullBounds.getY() + 58);
    grWellBounds = well;

    auto meterInner = well.reduced (18, 16);
    grMeter.setBounds (meterInner);

    auto titleArea = well.withTrimmedTop (6).removeFromTop (34);
    grTitleLabel.setBounds (titleArea);

    auto valueArea = well.withTrimmedBottom (8).removeFromBottom (28);
    currentGrLabel.setBounds (valueArea);

    // Clamp & Glue mini bars
    clampMeter.setBounds (bandMiniBars.removeFromLeft (bandMiniBars.getWidth() / 2 - clampGlueGap));
    glueMeter.setBounds  (bandMiniBars.removeFromRight (bandMiniBars.getWidth() / 2 - clampGlueGap));

    // Truth strip: constrained to max width and centered
    auto truthRow = bandTruth.withSizeKeepingCentre (juce::jmin (Layout::truthMaxWidth, bandTruth.getWidth()),
                                                    bandTruth.getHeight());

    inTpLabel.setBounds  (truthRow.removeFromLeft (truthNumW));
    truthRow.removeFromLeft (truthGap);
    outTpLabel.setBounds (truthRow.removeFromLeft (truthNumW));
    truthRow.removeFromLeft (truthGap);
    lufsSLabel.setBounds (truthRow.removeFromLeft (truthNumW));
    lufsILabel.setBounds (truthRow.removeFromLeft (truthNumW));
}
