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
    setResizable (false, false);
    setSize (820, 260);

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

    auto& vts = processor.getAPVTS();

    driveA   = std::make_unique<APVTS::SliderAttachment> (vts, "drive", drive);
    ceilingA = std::make_unique<APVTS::SliderAttachment> (vts, "ceiling", ceiling);
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

    drawLabelAbove (drive,           "Drive");
    drawLabelAbove (ceiling,         "Ceiling");
    drawLabelAbove (adaptiveBias,    "Adaptive Bias");
    drawLabelAbove (stereoLink,      "Stereo Link");
    drawLabelAbove (oversamplingMin, "Oversampling Min");
}

void CompassMasteringLimiterAudioProcessorEditor::resized()
{
    auto b = getLocalBounds().reduced (16);
    b.removeFromTop (30);

    const int knob = 104;
    const int gap  = 16;

    auto row = b.removeFromTop (knob + 30);

    drive.setBounds (row.removeFromLeft (knob));
    row.removeFromLeft (gap);
    ceiling.setBounds (row.removeFromLeft (knob));
    row.removeFromLeft (gap);

    adaptiveBias.setBounds (row.removeFromLeft (260).withTrimmedTop (34).withHeight (26));
    row.removeFromLeft (gap);
    stereoLink.setBounds (row.removeFromLeft (260).withTrimmedTop (34).withHeight (26));
    row.removeFromLeft (gap);
    oversamplingMin.setBounds (row.removeFromLeft (100).withTrimmedTop (34).withHeight (26));

    b.removeFromTop (10);
    grMeter.setBounds (b.removeFromTop (110));

    auto m = grMeter.getBounds();
    currentGrLabel.setBounds (m.getRight() - 90, m.getY() + 6, 84, 18);
}
