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

CompassMasteringLimiterAudioProcessorEditor::CompassMasteringLimiterAudioProcessorEditor (CompassMasteringLimiterAudioProcessor& p)
: juce::AudioProcessorEditor (&p), processor (p)
{
    setSize (900, 420);

    setRotary (drive);
    addAndMakeVisible (drive);

    setRotary (ceiling);
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

    currentGrLabel.setJustificationType (juce::Justification::centredRight);
    currentGrLabel.setFont (juce::Font (14.0f));
    currentGrLabel.setColour (juce::Label::textColourId, juce::Colours::white.withAlpha (0.70f));
    currentGrLabel.setColour (juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    currentGrLabel.setInterceptsMouseClicks (false, false);
    currentGrLabel.setText ("0.0 dB", juce::dontSendNotification);
    addAndMakeVisible (currentGrLabel);

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

void CompassMasteringLimiterAudioProcessorEditor::timerCallback()
{
    const float current = processor.getCurrentGRDb();

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

    grMeter.repaint();
}

void CompassMasteringLimiterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    // Title
    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (14.0f);
    g.drawText ("Compass Mastering Limiter", 16, 10, getWidth() - 32, 20, juce::Justification::left);

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
    const int knobSize        = 100;
    const int knobGap         = 15;
    const int knobRowExtraH   = 40;
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
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandMiniBars = mainZone.removeFromTop (Layout::clampGlueBars);
    mainZone.removeFromTop (Layout::interBandGap);

    auto bandTruth = mainZone.removeFromTop (Layout::truthStrip);

    // Tall TP meters
    inTpMeter.setBounds (tpLeftZone);
    outTpMeter.setBounds (tpRightZone);

    // Top: three knob columns (identical) inside bandKnobs
    const int colGap     = 18;
    const int labelAreaH = 24;
    const int knobAreaH  = 86;
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
        trim.setBounds (knobArea);
        trimValueLabel.setBounds (valueArea);
    }
    {
        auto [labelArea, knobArea, valueArea] = makeAreas (col2);
        (void) labelArea;
        drive.setBounds (knobArea);
        glueValueLabel.setBounds (valueArea);
    }
    {
        auto [labelArea, knobArea, valueArea] = makeAreas (col3);
        (void) labelArea;
        ceiling.setBounds (knobArea);
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

    grMeter.setBounds (grBarArea.withTrimmedLeft (grTrimLR).withTrimmedRight (grTrimLR));
    currentGrLabel.setBounds (grBarArea.withSizeKeepingCentre (grLabelW, grLabelH).translated (0, grLabelDY));

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
