#include "PluginEditor.h"

static void setRotary (juce::Slider& s)
{
    s.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 88, 20);
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

    adaptiveBias.setSliderStyle (juce::Slider::LinearHorizontal);
    adaptiveBias.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
    addAndMakeVisible (adaptiveBias);

    stereoLink.setSliderStyle (juce::Slider::LinearHorizontal);
    stereoLink.setTextBoxStyle (juce::Slider::TextBoxRight, false, 90, 20);
    addAndMakeVisible (stereoLink);

    oversamplingMin.addItem ("2x", 1);
    oversamplingMin.addItem ("4x", 2);
    oversamplingMin.addItem ("8x", 3);
    addAndMakeVisible (oversamplingMin);

    addAndMakeVisible (grMeter);

    auto& vts = processor.getAPVTS();

    driveA   = std::make_unique<APVTS::SliderAttachment> (vts, "drive", drive);
    ceilingA = std::make_unique<APVTS::SliderAttachment> (vts, "ceiling", ceiling);
    biasA    = std::make_unique<APVTS::SliderAttachment> (vts, "adaptive_bias", adaptiveBias);
    linkA    = std::make_unique<APVTS::SliderAttachment> (vts, "stereo_link", stereoLink);
    osA      = std::make_unique<APVTS::ComboBoxAttachment> (vts, "oversampling_min", oversamplingMin);

    startTimerHz (30);
}

void CompassMasteringLimiterAudioProcessorEditor::timerCallback()
{
    grMeter.pushValueDb (processor.getCurrentGRDb());
    grMeter.repaint();
}

void CompassMasteringLimiterAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (juce::Colours::black);

    g.setColour (juce::Colours::white.withAlpha (0.85f));
    g.setFont (14.0f);
    g.drawText ("Compass Mastering Limiter", 16, 10, getWidth() - 32, 20, juce::Justification::left);
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
}
