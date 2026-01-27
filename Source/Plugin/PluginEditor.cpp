#include "PluginEditor.h"
#include <unordered_map>
#include <random>
#include <functional>

//==============================================================================
// HELPER: Sets up a rotary slider with "Stealth" text box
static void setRotary(juce::Slider &s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);

    //// [CML:UI] Rotary TextBox Visible Styling
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 24);

    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::white.withAlpha(0.7f));
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xFFE6A532).withAlpha(0.4f));

    constexpr double kVelSensFine01 = 0.35;
    constexpr int kVelThreshPxMin = 1;
    constexpr double kVelOffsetMin01 = 0.0;
    s.setVelocityBasedMode(false);
    s.setVelocityModeParameters(kVelSensFine01, kVelThreshPxMin, kVelOffsetMin01, true, juce::ModifierKeys::shiftModifier);
}

//==============================================================================
// LOOK & FEEL: "Compass" Industrial Design
class CompassKnobLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    CompassKnobLookAndFeel()
    {
        setDefaultSansSerifTypefaceName("Inter");
    }

    // --- Custom Controls ---
    //// [CML:UI] ComboBox Dark Theme + Gold Arrow
    void drawComboBox(juce::Graphics &g,
                      int width,
                      int height,
                      bool,
                      int, int, int, int,
                      juce::ComboBox &) override
    {
        constexpr float kCornerRadiusPx = 4.0f;
        constexpr float kBorderAlpha01 = 0.20f;
        constexpr float kArrowInsetRightPx = 16.0f;
        constexpr float kArrowHalfWPx = 4.0f;
        constexpr float kArrowHalfHPx = 3.0f;

        const auto r = juce::Rectangle<float>((float)width, (float)height);

        g.setColour(juce::Colour(0xFF0D0D0D));
        g.fillRoundedRectangle(r, kCornerRadiusPx);

        g.setColour(juce::Colours::white.withAlpha(kBorderAlpha01));
        g.drawRoundedRectangle(r, kCornerRadiusPx, 1.0f);

        const float cx = r.getRight() - kArrowInsetRightPx;
        const float cy = r.getCentreY();

        juce::Path p;
        p.addTriangle(cx - kArrowHalfWPx, cy - kArrowHalfHPx,
                      cx + kArrowHalfWPx, cy - kArrowHalfHPx,
                      cx, cy + kArrowHalfHPx);

        g.setColour(juce::Colour(0xFFE6A532));
        g.fillPath(p);
    }

    //// [CML:UI] Slider TextBox Etched Label
    void drawLabel(juce::Graphics &g, juce::Label &label) override
    {
        auto *parent = label.getParentComponent();

        // CHECK: Is this label part of a Slider?
        if (dynamic_cast<juce::Slider *>(parent) != nullptr)
        {
            juce::LookAndFeel_V4::drawLabel(g, label);
            return;
        }

        // Default behavior for other labels (like the combo boxes)
        juce::LookAndFeel_V4::drawLabel(g, label);
    }

    //// [CML:UI] ComboBox Text Center Bold White
    void positionComboBoxText(juce::ComboBox &box, juce::Label &label) override
    {
        // [Fix]: Explicitly calculate label bounds based on the ComboBox size
        // instead of relying on the label's previous bounds.

        auto r = box.getLocalBounds().toFloat().reduced(2.0f);
        r.removeFromRight(20.0f); // Reserve space for the arrow area

        label.setBounds(r.toNearestInt());

        box.setColour(juce::ComboBox::textColourId, juce::Colours::white.withAlpha(0.7f));

        label.setJustificationType(juce::Justification::centred);
        label.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        label.setFont(juce::Font(juce::FontOptions(12.0f, juce::Font::bold)));
    }

    //// [CML:UI] Popup Menu Dark Theme Background
    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
    {
        constexpr float kBorderAlpha01 = 0.20f;

        g.fillAll(juce::Colour(0xFF050505));
        g.setColour(juce::Colours::white.withAlpha(kBorderAlpha01));
        g.drawRect(0, 0, width, height);
    }

    //// [CML:UI] Popup Menu Item Text White
    void drawPopupMenuItem(juce::Graphics &g,
                           const juce::Rectangle<int> &area,
                           bool isSeparator,
                           bool isActive,
                           bool isHighlighted,
                           bool isTicked,
                           bool hasSubMenu,
                           const juce::String &text,
                           const juce::String &shortcutKeyText,
                           const juce::Drawable *,
                           const juce::Colour *textColourToUse) override
    {
        constexpr int kTextPadLeftPx = 10;
        constexpr int kTextPadRightPx = 26;
        constexpr int kShortcutReserveWPx = 90;
        constexpr float kHighlightAlpha01 = 0.12f;
        constexpr float kSeparatorAlpha01 = 0.18f;
        constexpr float kInactiveAlpha01 = 0.35f;
        constexpr float kSeparatorStrokePx = 1.0f;

        constexpr float kSubmenuInsetRightPx = 12.0f;
        constexpr float kSubmenuArrowHalfHPx = 4.0f;
        constexpr float kSubmenuArrowHalfWPx = 3.0f;

        constexpr float kTickInsetXPx = 8.0f;
        constexpr float kTickHalfHPx = 3.0f;
        constexpr float kTickHalfWPx = 4.0f;
        constexpr float kTickMidInsetPx = 1.0f;
        constexpr float kTickStrokePx = 1.5f;

        if (isSeparator)
        {
            const int y = area.getCentreY();
            g.setColour(juce::Colours::white.withAlpha(kSeparatorAlpha01));
            g.drawLine((float)area.getX(), (float)y, (float)area.getRight(), (float)y, kSeparatorStrokePx);
            return;
        }

        auto r = area;

        if (isHighlighted)
        {
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(kHighlightAlpha01));
            g.fillRect(r);
        }

        g.setFont(getPopupMenuFont());

        juce::Colour textCol = juce::Colours::white.withAlpha(0.7f);
        if (textColourToUse != nullptr)
            textCol = *textColourToUse;

        if (!isActive)
            textCol = textCol.withAlpha(kInactiveAlpha01);

        g.setColour(textCol);

        auto textArea = r.reduced(kTextPadLeftPx, 0);
        textArea.removeFromRight(kTextPadRightPx);

        g.drawFittedText(text, textArea, juce::Justification::centredLeft, 1);

        if (shortcutKeyText.isNotEmpty())
        {
            auto keyArea = r.reduced(kTextPadLeftPx, 0);
            keyArea.removeFromLeft(juce::jmax(0, keyArea.getWidth() - kShortcutReserveWPx));
            g.drawFittedText(shortcutKeyText, keyArea, juce::Justification::centredRight, 1);
        }

        if (hasSubMenu)
        {
            const float cx = (float)r.getRight() - kSubmenuInsetRightPx;
            const float cy = (float)r.getCentreY();

            juce::Path p;
            p.addTriangle(cx - kSubmenuArrowHalfWPx, cy - kSubmenuArrowHalfHPx,
                          cx - kSubmenuArrowHalfWPx, cy + kSubmenuArrowHalfHPx,
                          cx + kSubmenuArrowHalfWPx, cy);
            g.fillPath(p);
        }

        juce::ignoreUnused(isTicked);
    }

    void drawButtonBackground(juce::Graphics &g, juce::Button &button, const juce::Colour &, bool, bool) override
    {
        auto r = button.getLocalBounds().toFloat();
        bool on = button.getToggleState();
        bool down = button.isMouseButtonDown();

        // [Pro Fix]: Unified "Recessed" styling (matches ComboBox)

        // 1. Recessed Base (Same as ComboBox)
        g.setColour(juce::Colour(0xFF0A0A0A));
        g.fillRoundedRectangle(r, 4.0f);

        // 2. Inner Shadow
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(r.translated(0.5f, 0.5f), 4.0f, 1.0f);

        // 3. Bottom Highlight
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(r.translated(-0.5f, -0.5f), 4.0f, 1.0f);

        //// [CML:UI] Link Button Active Amber
        constexpr float kLinkOnFillAlpha01 = 0.20f;
        constexpr float kLinkOnBorderAlpha01 = 1.00f;

        // 4. Active State (Amber theme)
        if (on || down)
        {
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(kLinkOnFillAlpha01));
            g.fillRoundedRectangle(r.reduced(2.0f), 3.0f);

            // Brighter solid Gold border
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(kLinkOnBorderAlpha01));
            g.drawRoundedRectangle(r, 4.0f, 1.0f);
        }
    }

    void drawButtonText(juce::Graphics &g, juce::TextButton &button, bool, bool) override
    {
        g.setFont(12.0f);
        bool on = button.getToggleState();
        g.setColour(on ? juce::Colour(0xFFE6A532) : juce::Colours::white.withAlpha(0.4f));
        g.drawText(button.getButtonText(), button.getLocalBounds(), juce::Justification::centred, false);
    }

    // --- CPU Optimization: Cache the noise texture ---
    juce::Image noiseCache;

    void drawBackgroundNoise(juce::Graphics &g, int w, int h)
    {
        if (noiseCache.isNull() || noiseCache.getWidth() != w || noiseCache.getHeight() != h)
        {
            noiseCache = juce::Image(juce::Image::ARGB, w, h, true);
            juce::Graphics g2(noiseCache);

            juce::Random rng(1234);
            for (int i = 0; i < 3000; ++i)
            {
                float x = rng.nextFloat() * w;
                float y = rng.nextFloat() * h;

                if (rng.nextBool())
                    g2.setColour(juce::Colours::white.withAlpha(0.015f));
                else
                    g2.setColour(juce::Colours::black.withAlpha(0.04f));

                g2.fillRect(x, y, 1.0f, 1.0f);
            }
        }

        g.drawImageAt(noiseCache, 0, 0);
    }

    // --- CPU Optimization: Cache the entire editor background ---
    juce::Image backgroundCache;

    void invalidateBackgroundCache() { backgroundCache = juce::Image(); }

    void drawBufferedBackground(juce::Graphics &g, int w, int h, std::function<void(juce::Graphics &)> drawFunction)
    {
        if (backgroundCache.isNull() || backgroundCache.getWidth() != w || backgroundCache.getHeight() != h)
        {
            backgroundCache = juce::Image(juce::Image::RGB, w, h, true);
            juce::Graphics g2(backgroundCache);
            drawFunction(g2);
        }

        g.drawImageAt(backgroundCache, 0, 0);
    }

    std::unordered_map<int, juce::Image> knobCache;

    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider &slider) override
    {
        const int sizeKey = (width << 16) | height;
        auto &bgImage = knobCache[sizeKey];

        if (bgImage.isNull() || bgImage.getWidth() != width || bgImage.getHeight() != height)
        {
            bgImage = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics g2(bgImage);

            auto bounds = juce::Rectangle<float>((float)width, (float)height);
            float side = juce::jmin((float)width, (float)height);
            auto center = bounds.getCentre();
            float r = (side * 0.5f) / 1.3f;

            {
                float wellR = r * 1.15f;
                juce::ColourGradient well(juce::Colours::black.withAlpha(0.95f), center.x, center.y,
                                          juce::Colours::transparentBlack, center.x, center.y + wellR, true);
                well.addColour(r / wellR, juce::Colours::black.withAlpha(0.95f));
                g2.setGradientFill(well);
                g2.fillEllipse(center.x - wellR, center.y - wellR, wellR * 2, wellR * 2);
            }

            {
                int numTicks = 24;
                float tickR_Inner = r * 1.18f;
                float tickR_Outer_Major = r * 1.28f;
                float tickR_Outer_Minor = r * 1.23f;

                for (int i = 0; i <= numTicks; ++i)
                {
                    bool isMajor = (i % 4 == 0);
                    float angle = startAngle + (float)i / (float)numTicks * (endAngle - startAngle);
                    float outerR = isMajor ? tickR_Outer_Major : tickR_Outer_Minor;
                    g2.setColour(juce::Colours::white.withAlpha(isMajor ? 1.0f : 0.6f));
                    juce::Line<float> tick(center.getPointOnCircumference(tickR_Inner, angle),
                                           center.getPointOnCircumference(outerR, angle));
                    g2.drawLine(tick, isMajor ? 1.5f : 1.0f);
                }
            }

            float bodyR = r * 0.85f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF2B2B2B), center.x - bodyR, center.y - bodyR,
                                                    juce::Colour(0xFF050505), center.x + bodyR, center.y + bodyR, true));
            g2.fillEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2);

            {
                juce::ColourGradient rimGrad(juce::Colours::white.withAlpha(0.3f), center.x - bodyR, center.y - bodyR,
                                             juce::Colours::black, center.x + bodyR, center.y + bodyR, true);
                g2.setGradientFill(rimGrad);
                g2.drawEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2, 2.0f);
            }

            float faceR = bodyR * 0.9f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF222222), center.x, center.y - faceR,
                                                    juce::Colour(0xFF0A0A0A), center.x, center.y + faceR, false));
            g2.fillEllipse(center.x - faceR, center.y - faceR, faceR * 2, faceR * 2);
        }

        g.drawImageAt(bgImage, x, y);

        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
        float side = juce::jmin((float)width, (float)height);
        auto center = bounds.getCentre();
        float r = (side * 0.5f) / 1.3f;
        float bodyR = r * 0.85f;
        float faceR = bodyR * 0.9f;
        float angle = startAngle + pos * (endAngle - startAngle);

        juce::Path p;
        float ptrW = 3.5f;
        float ptrLen = faceR * 0.6f;
        p.addRoundedRectangle(-ptrW * 0.5f, -faceR + 6.0f, ptrW, ptrLen, 1.0f);
        auto xf = juce::AffineTransform::rotation(angle).translated(center);
        g.setColour(juce::Colours::white.withAlpha(0.9f));
        g.fillPath(p, xf);
    }
};

//==============================================================================
// EDITOR
//==============================================================================

CompassMasteringLimiterAudioProcessorEditor::CompassMasteringLimiterAudioProcessorEditor(CompassMasteringLimiterAudioProcessor &p)
    : juce::AudioProcessorEditor(&p), processor(p)
{
    setSize(900, 500);
    knobLnf = std::make_unique<CompassKnobLookAndFeel>();

    auto setupKnob = [&](juce::Slider &s, const juce::String &name, const juce::String &suffix)
    {
        s.setName(name);
        setRotary(s);
        s.setNumDecimalPlacesToDisplay(1);
        s.setLookAndFeel(knobLnf.get());
        addAndMakeVisible(s);
    };
    setupKnob(drive, "Drive", " %");
    setupKnob(ceiling, "Ceiling", " dB");
    setupKnob(trim, "Trim", " dB");

    stereoLink.setButtonText("LINK");
    stereoLink.setClickingTogglesState(true);
    stereoLink.setLookAndFeel(knobLnf.get());
    addAndMakeVisible(stereoLink);

    auto setupCombo = [&](juce::ComboBox &c)
    {
        c.setJustificationType(juce::Justification::centred);
        c.setLookAndFeel(knobLnf.get());
        addAndMakeVisible(c);
        c.onChange = [this]
        { grabKeyboardFocus(); };
    };
    setupCombo(adaptiveBias);
    adaptiveBias.addItem("Transparent", 1);
    adaptiveBias.addItem("Balanced", 2);
    adaptiveBias.addItem("Aggressive", 3);
    adaptiveBias.setSelectedId(3, juce::dontSendNotification);

    setupCombo(oversamplingMin);
    oversamplingMin.addItem("Standard", 1);
    oversamplingMin.addItem("High", 2);
    oversamplingMin.addItem("Ultra", 3);

    addAndMakeVisible(grMeter);
    grMeter.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(inTpMeter);
    addAndMakeVisible(outTpMeter);

    auto makeLabel = [&](juce::Label &l, float size, bool bold)
    {
        l.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        l.setJustificationType(juce::Justification::centred);
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
    };

    //// [CML:UI] Central GR External Labels Disabled


    auto makeSurgical = [&](juce::Label &l)
    {
        l.setFont(10.0f);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
        l.setJustificationType(juce::Justification::centred);
        addAndMakeVisible(l);
    };

    makeSurgical(inTpLabel);
    inTpLabel.setText("IN", juce::dontSendNotification);

    makeSurgical(outTpLabel);
    outTpLabel.setText("OUT", juce::dontSendNotification);

    //// [CML:STATE] APVTS Attachment IDs
    auto &vts = processor.getAPVTS();
    driveA = std::make_unique<APVTS::SliderAttachment>(vts, "drive", drive);
    ceilingA = std::make_unique<APVTS::SliderAttachment>(vts, "ceiling", ceiling);
    trimA = std::make_unique<APVTS::SliderAttachment>(vts, "trim", trim);
    biasA = std::make_unique<APVTS::ComboBoxAttachment>(vts, "adaptive_bias", adaptiveBias);
    linkA = std::make_unique<APVTS::ButtonAttachment>(vts, "stereo_link", stereoLink);
    osA = std::make_unique<APVTS::ComboBoxAttachment>(vts, "oversampling_min", oversamplingMin);

    startTimerHz(30);
}

CompassMasteringLimiterAudioProcessorEditor::~CompassMasteringLimiterAudioProcessorEditor()
{
    drive.setLookAndFeel(nullptr);
    ceiling.setLookAndFeel(nullptr);
    trim.setLookAndFeel(nullptr);
    stereoLink.setLookAndFeel(nullptr);
    adaptiveBias.setLookAndFeel(nullptr);
    oversamplingMin.setLookAndFeel(nullptr);
    knobLnf.reset();
}

void CompassMasteringLimiterAudioProcessorEditor::timerCallback()
{
    static float lastGr = -1000.0f;
    static float lastInL = -1000.0f;
    static float lastInR = -1000.0f;
    static float lastOutL = -1000.0f;
    static float lastOutR = -1000.0f;

    constexpr float kUiDisabledAlpha01 = 0.35f;

    // --- Fix: Robust Playback Detection ---
    bool isPlaying = false;
    if (auto *ph = processor.getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo pos;
        if (ph->getCurrentPosition(pos))
            isPlaying = pos.isPlaying; // Simplified to just playback state
    }

    // Only update enabled state if it changed to prevent flickering
    if (oversamplingMin.isEnabled() == isPlaying)
    {
        oversamplingMin.setEnabled(!isPlaying);
        oversamplingMin.setAlpha(isPlaying ? kUiDisabledAlpha01 : 1.0f);
    }

    // --- Fix: Robust Bypass Detection ---
    bool isBypassed = false;

    // 1. Check standard bypass parameter
    if (auto *bypassParam = processor.getBypassParameter())
        isBypassed = (bypassParam->getValue() > 0.5f);

    // 2. Fallback: Check APVTS for "bypass" or "master_bypass"
    if (!isBypassed)
    {
        auto &vts = processor.getAPVTS();
        if (auto *p = vts.getParameter("bypass"))
            isBypassed = (p->getValue() > 0.5f);
    }

    //// [CML:UI] Bypass Edge Resync For GR Meter
    static bool wasBypassed = false;
    const bool bypassEdgeOff = (wasBypassed && !isBypassed);
    wasBypassed = isBypassed;

    if (isBypassed)
    {
        // Force reset meters
        grMeter.pushValueDb(0.0f);
        grMeter.repaint();

        inTpMeter.pushValueDbLR(-120.0f, -120.0f);
        outTpMeter.pushValueDbLR(-120.0f, -120.0f);
        inTpMeter.resetPeakHoldToCurrent();
        outTpMeter.resetPeakHoldToCurrent();
        inTpMeter.repaint();
        outTpMeter.repaint();

        lastGr = -1000.0f;
        lastInL = -1000.0f;
        lastInR = -1000.0f;
        lastOutL = -1000.0f;
        lastOutR = -1000.0f;
    }
    else
    {
        //// [CML:UI] GR Meter Finite Guard And Update Gate
        constexpr float kGrResetSentinelDb = -1000.0f;
        constexpr float kGrDeltaEpsDb = 0.05f;

        if (bypassEdgeOff)
            lastGr = kGrResetSentinelDb;

        float gr = processor.getCurrentGRDb();
        if (!std::isfinite(gr))
        {
            gr = 0.0f;
            lastGr = kGrResetSentinelDb;
        }

        if (!std::isfinite(lastGr) || std::abs(gr - lastGr) > kGrDeltaEpsDb)
        {
            grMeter.pushValueDb(gr);
            grMeter.repaint();
            lastGr = gr;
        }

        //// [CML:UI] Meter Read Guard NoData
        constexpr float kNoDataDbFS = -120.0f;

        float inL = kNoDataDbFS, inR = kNoDataDbFS, outL = kNoDataDbFS, outR = kNoDataDbFS;
        const bool haveTp = processor.getCurrentTruePeakDbTP_LR(inL, inR, outL, outR);

        if (!haveTp)
        {
            inTpMeter.pushValueDbLR(kNoDataDbFS, kNoDataDbFS);
            outTpMeter.pushValueDbLR(kNoDataDbFS, kNoDataDbFS);
            inTpMeter.resetPeakHoldToCurrent();
            outTpMeter.resetPeakHoldToCurrent();
            inTpMeter.repaint();
            outTpMeter.repaint();

            lastInL = kNoDataDbFS;
            lastInR = kNoDataDbFS;
            lastOutL = kNoDataDbFS;
            lastOutR = kNoDataDbFS;
        }
        else
        {
            //// [CML:UI] TP Meter Always Push Current Db
            constexpr float kTpDeltaEpsDb = 0.05f;

            const bool inputChanged = (std::abs(inL - lastInL) > kTpDeltaEpsDb) || (std::abs(inR - lastInR) > kTpDeltaEpsDb);
            const bool outputChanged = (std::abs(outL - lastOutL) > kTpDeltaEpsDb) || (std::abs(outR - lastOutR) > kTpDeltaEpsDb);

            // Always push so currentDb tracks gradual movement (peak-hold can then decay smoothly).
            inTpMeter.pushValueDbLR(inL, inR);
            outTpMeter.pushValueDbLR(outL, outR);

            // Peak-hold decay after currentDb is refreshed.
            inTpMeter.updatePeakHoldDecay();
            outTpMeter.updatePeakHoldDecay();

            // Advance last values every tick (delta gate remains per-tick).
            lastInL = inL;
            lastInR = inR;
            lastOutL = outL;
            lastOutR = outR;

            const bool inNeedsRepaint = inputChanged || inTpMeter.hasActiveDecay();
            const bool outNeedsRepaint = outputChanged || outTpMeter.hasActiveDecay();

            if (inNeedsRepaint)
                inTpMeter.repaint();

            if (outNeedsRepaint)
                outTpMeter.repaint();
        }
    }

    // Always update text labels (parameters might still change during bypass)
    auto &vts = processor.getAPVTS();
}

void CompassMasteringLimiterAudioProcessorEditor::paint(juce::Graphics &g)
{
    if (auto *lnf = dynamic_cast<CompassKnobLookAndFeel *>(knobLnf.get()))
    {
        lnf->drawBufferedBackground(g, getWidth(), getHeight(), [this, lnf](juce::Graphics &gBuffer)
                                    {
            gBuffer.fillAll(juce::Colour(0xFF0D0D0D)); 

            lnf->drawBackgroundNoise(gBuffer, getWidth(), getHeight());

            {
                juce::ColourGradient vig(juce::Colours::transparentBlack, getWidth() / 2.0f, getHeight() / 2.0f,
                                         juce::Colours::black.withAlpha(0.6f), 0.0f, 0.0f, true);
                gBuffer.setGradientFill(vig);
                gBuffer.fillAll();
            }

            auto drawScrew = [&](int cx, int cy)
            {
                float r = 6.0f;
                gBuffer.setGradientFill(juce::ColourGradient(juce::Colour(0xFF151515), cx - r, cy - r,
                                                             juce::Colour(0xFF2A2A2A), cx + r, cy + r, true));
                gBuffer.fillEllipse(cx - r, cy - r, r * 2, r * 2);
                gBuffer.setColour(juce::Colours::black.withAlpha(0.8f));
                gBuffer.drawEllipse(cx - r, cy - r, r * 2, r * 2, 1.0f);
                gBuffer.setColour(juce::Colour(0xFF050505));
                juce::Path p;
                p.addStar(juce::Point<float>((float)cx, (float)cy), 6, r * 0.3f, r * 0.6f);
                gBuffer.fillPath(p);
            };
            int m = 14;
            drawScrew(m, m); drawScrew(getWidth() - m, m);
            drawScrew(m, getHeight() - m); drawScrew(getWidth() - m, getHeight() - m);

            gBuffer.setFont(15.0f);
            gBuffer.setColour(juce::Colours::black.withAlpha(0.4f));
            gBuffer.drawText("COMPASS", 35, 19, 100, 20, juce::Justification::left);
            gBuffer.drawText("// LIMITER", 106, 19, 100, 20, juce::Justification::left);

            gBuffer.setColour(juce::Colours::white.withAlpha(0.9f));
            gBuffer.drawText("COMPASS", 34, 18, 100, 20, juce::Justification::left);
            gBuffer.setColour(juce::Colour(0xFFE6A532));
            gBuffer.drawText("// LIMITER", 105, 18, 100, 20, juce::Justification::left);

            //// [CML:UI] Side Meter Wells Recessed
            auto drawMeterWell = [&](const juce::Rectangle<int> &bounds, bool isLeft)
            {
                (void) isLeft;

                constexpr float kWellExpandPx        = 6.0f;
                constexpr float kWellCornerRadiusPx  = 4.0f;
                constexpr float kGlassAlpha          = 0.05f;

                const auto well = bounds.toFloat().expanded (kWellExpandPx);

                gBuffer.setColour (juce::Colour (0xFF0A0A0A));
                gBuffer.fillRoundedRectangle (well, kWellCornerRadiusPx);

                gBuffer.setColour (juce::Colours::white.withAlpha (kGlassAlpha));
                gBuffer.fillRoundedRectangle (well.reduced (1.0f), kWellCornerRadiusPx);
            };
            drawMeterWell(inTpMeter.getBounds(), true);
            drawMeterWell(outTpMeter.getBounds(), false);

            if (!grWellBounds.isEmpty())
            {
                auto well = grWellBounds.toFloat();
                gBuffer.setColour(juce::Colour(0xFF131313));
                gBuffer.fillRoundedRectangle(well.expanded(4.0f), 6.0f);
                gBuffer.setGradientFill(juce::ColourGradient(juce::Colours::black, well.getCentreX(), well.getY(),
                                                       juce::Colour(0xFF0A0A0A), well.getCentreX(), well.getBottom(), false));
                gBuffer.fillRoundedRectangle(well, 4.0f);

                float h = well.getHeight();
                float y0 = well.getY();

                gBuffer.setColour(juce::Colours::white.withAlpha(0.03f));
                gBuffer.drawHorizontalLine(y0 + h * 0.50f, well.getX(), well.getRight());

                gBuffer.setColour(juce::Colours::white.withAlpha(0.015f));
                gBuffer.drawHorizontalLine(y0 + h * 0.25f, well.getX(), well.getRight());
                gBuffer.drawHorizontalLine(y0 + h * 0.75f, well.getX(), well.getRight());

                juce::ColourGradient innerShadow(juce::Colours::transparentBlack, well.getCentreX(), well.getBottom() - 20.0f,
                                                 juce::Colours::black.withAlpha(0.8f), well.getCentreX(), well.getBottom(), false);
                gBuffer.setGradientFill(innerShadow);
                gBuffer.fillRoundedRectangle(well, 4.0f);

                gBuffer.setGradientFill(juce::ColourGradient(juce::Colour(0xFF333333).withAlpha(0.03f), well.getTopLeft(),
                                                       juce::Colours::transparentBlack, well.getBottomRight(), false));
                gBuffer.fillRoundedRectangle(well, 4.0f);

                gBuffer.setColour(juce::Colours::white.withAlpha(0.15f));
                gBuffer.drawHorizontalLine(well.getY() + 1.0f, well.getX() + 2.0f, well.getRight() - 2.0f);

                gBuffer.setColour(juce::Colours::white.withAlpha(0.1f));
                gBuffer.drawRoundedRectangle(well, 4.0f, 1.0f);
            }

            auto drawEtch = [&](juce::Component &c)
            {
                auto b = c.getBounds().toFloat().reduced(6.0f, 0.0f);
                gBuffer.setColour(juce::Colours::black.withAlpha(0.7f));
                gBuffer.fillRoundedRectangle(b, 3.0f);
                gBuffer.setColour(juce::Colours::white.withAlpha(0.08f));
                gBuffer.drawRoundedRectangle(b, 3.0f, 1.0f);
            };
            });
    }

    auto drawHead = [&](juce::String text, juce::Component &c)
    {
        auto b = c.getBounds().translated(0, -20);
        bool active = c.isMouseOverOrDragging();
        g.setColour(active ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::white.withAlpha(0.4f));
        g.setFont(11.0f);
        g.drawText(text.toUpperCase(), b.withHeight(16), juce::Justification::centred);
    };
    drawHead("Glue", drive);
    drawHead("Ceiling", ceiling);
    drawHead("Trim", trim);

    drawHead("Character", adaptiveBias);

    drawHead("Quality", oversamplingMin);
}

void CompassMasteringLimiterAudioProcessorEditor::resized()
{
    const int sidePanelWidth = 80;
    const int mainPadding = 10;
    const int topDeckHeight = 170;
    const int midDeckHeight = 120;
    const int botDeckHeight = 100;
    const int brandingHeight = 20;

    const int meterWidth = 28;
    const int meterHeight = 340;
    const int bigKnobSize = 130;
    const int smallKnobSize = 80;
    const int uiControlHeight = 24;
    const int labelHeight = 20;

    const int labelOverlapStandard = 5;
    const int labelOverlapTrim = 8;
    const int bottomDeckYOffset = -10;

    auto r = getLocalBounds();

    auto rLeft = r.removeFromLeft(sidePanelWidth);
    auto rRight = r.removeFromRight(sidePanelWidth);

    int meterY = (getHeight() - meterHeight) / 2;

    inTpMeter.setBounds(rLeft.getX() + (sidePanelWidth - meterWidth) / 2, meterY, meterWidth, meterHeight);
    outTpMeter.setBounds(rRight.getX() + (sidePanelWidth - meterWidth) / 2, meterY, meterWidth, meterHeight);

    inTpLabel.setBounds(inTpMeter.getX(), meterY + meterHeight + 8, meterWidth, labelHeight);
    outTpLabel.setBounds(outTpMeter.getX(), meterY + meterHeight + 8, meterWidth, labelHeight);

    auto main = r.reduced(mainPadding);

    auto topDeck = main.removeFromTop(topDeckHeight);
    topDeck.removeFromTop(brandingHeight);

    auto driveArea = topDeck.removeFromLeft(topDeck.getWidth() / 2);
    auto ceilArea = topDeck;

    //// [CML:UI] Rotary Slider TextBox Height Reserve
    constexpr int kKnobTextBoxExtraHPx = 25;

    drive.setBounds(driveArea.withSizeKeepingCentre(bigKnobSize, bigKnobSize + kKnobTextBoxExtraHPx));

    ceiling.setBounds(ceilArea.withSizeKeepingCentre(bigKnobSize, bigKnobSize + kKnobTextBoxExtraHPx));

    //// [CML:UI] Central GR External Labels Disabled
    auto midDeck = main.removeFromTop(midDeckHeight);
    midDeck.removeFromTop(30);

    grWellBounds = midDeck.reduced(mainPadding, 0).toNearestInt();
    grMeter.setBounds(grWellBounds.reduced(4));
    grMeter.setVisible(true);

    auto botDeck = main.removeFromBottom(botDeckHeight);
    int colW = botDeck.getWidth() / 4;

    auto c1 = botDeck.removeFromLeft(colW);
    trim.setBounds(c1.withSizeKeepingCentre(smallKnobSize, smallKnobSize + kKnobTextBoxExtraHPx).translated(0, bottomDeckYOffset));

    adaptiveBias.setBounds(botDeck.removeFromLeft(colW).withSizeKeepingCentre(110, uiControlHeight).translated(0, bottomDeckYOffset));

    stereoLink.setBounds(botDeck.removeFromLeft(colW).withSizeKeepingCentre(90, uiControlHeight).translated(0, bottomDeckYOffset));

    oversamplingMin.setBounds(botDeck.withSizeKeepingCentre(90, uiControlHeight).translated(0, bottomDeckYOffset));

    if (auto *lnf = dynamic_cast<CompassKnobLookAndFeel *>(knobLnf.get()))
        lnf->invalidateBackgroundCache();
}
