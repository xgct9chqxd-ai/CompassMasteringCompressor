#include "PluginEditor.h"
#include <unordered_map>
#include <random>

//==============================================================================
// HELPER: Sets up a rotary slider with "Stealth" text box
static void setRotary(juce::Slider &s)
{
    s.setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    s.setTextBoxStyle(juce::Slider::TextBoxBelow, false, 80, 20);
    s.setColour(juce::Slider::textBoxTextColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    s.setColour(juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
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
    void drawComboBox(juce::Graphics &g, int width, int height, bool, int, int, int, int, juce::ComboBox &box) override
    {
        auto r = juce::Rectangle<float>((float)width, (float)height);

        // 1. Recessed Background
        g.setColour(juce::Colour(0xFF0A0A0A));
        g.fillRoundedRectangle(r, 4.0f);

        // 2. Inner Shadow
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawRoundedRectangle(r.translated(0.5f, 0.5f), 4.0f, 1.0f);

        // 3. Bottom Highlight
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(r.translated(-0.5f, -0.5f), 4.0f, 1.0f);

        // 4. Focus/Hover ring
        if (box.hasKeyboardFocus(true))
        {
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.5f));
            g.drawRoundedRectangle(r, 4.0f, 1.0f);
        }

        // Chevron
        juce::Path p;
        auto center = r.removeFromRight(20.0f).getCentre();
        p.addTriangle(center.x - 3, center.y - 2, center.x + 3, center.y - 2, center.x, center.y + 3);
        g.setColour(juce::Colours::white.withAlpha(0.4f));
        g.fillPath(p);
    }

    void drawPopupMenuBackground(juce::Graphics &g, int width, int height) override
    {
        g.fillAll(juce::Colour(0xFF0F0F0F));
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRect(0, 0, width, height);
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

        // 4. Active State (Inner glow instead of flat fill)
        if (on || down)
        {
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.15f));
            g.fillRoundedRectangle(r.reduced(2.0f), 3.0f);

            // Subtle active border
            g.setColour(juce::Colour(0xFFE6A532).withAlpha(0.3f));
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
        // Re-generate only if size changes (or first run)
        if (noiseCache.isNull() || noiseCache.getWidth() != w || noiseCache.getHeight() != h)
        {
            noiseCache = juce::Image(juce::Image::ARGB, w, h, true);
            juce::Graphics g2(noiseCache);

            juce::Random rng(1234); // Fixed seed for consistent pattern
            for (int i = 0; i < 3000; ++i)
            {
                float x = rng.nextFloat() * w;
                float y = rng.nextFloat() * h;

                // Mix light and dark specs for depth
                if (rng.nextBool())
                    g2.setColour(juce::Colours::white.withAlpha(0.015f));
                else
                    g2.setColour(juce::Colours::black.withAlpha(0.04f));

                g2.fillRect(x, y, 1.0f, 1.0f);
            }
        }

        // Zero-cost blit
        g.drawImageAt(noiseCache, 0, 0);
    }

    // --- CPU Optimization: Cache the static knob body ---
    // [Pro Fix]: Use unordered_map for O(1) lookup speed instead of logN
    std::unordered_map<int, juce::Image> knobCache;

    void drawRotarySlider(juce::Graphics &g, int x, int y, int width, int height, float pos, float startAngle, float endAngle, juce::Slider &slider) override
    {
        // 1. Image Caching
        const int sizeKey = (width << 16) | height;
        auto &bgImage = knobCache[sizeKey];

        // Draw Cached Background first
        if (bgImage.isNull() || bgImage.getWidth() != width || bgImage.getHeight() != height)
        {
            bgImage = juce::Image(juce::Image::ARGB, width, height, true);
            juce::Graphics g2(bgImage);

            auto bounds = juce::Rectangle<float>((float)width, (float)height);
            float side = juce::jmin((float)width, (float)height);
            auto center = bounds.getCentre();
            float r = side * 0.5f;

            // --- A. Recessed Well ---
            {
                float wellR = r * 1.15f;
                juce::ColourGradient well(juce::Colours::black.withAlpha(0.95f), center.x, center.y,
                                          juce::Colours::transparentBlack, center.x, center.y + wellR, true);
                well.addColour(r / wellR, juce::Colours::black.withAlpha(0.95f));
                g2.setGradientFill(well);
                g2.fillEllipse(center.x - wellR, center.y - wellR, wellR * 2, wellR * 2);
            }

            // --- B. Scale Markings (Major/Minor Ticks) ---
            {
                int numTicks = 24;
                float tickR_Inner = r * 1.18f;
                float tickR_Outer_Major = r * 1.28f;
                float tickR_Outer_Minor = r * 1.23f;

                for (int i = 0; i <= numTicks; ++i)
                {
                    bool isMajor = (i % 4 == 0); // Every 4th tick is Major
                    float angle = startAngle + (float)i / (float)numTicks * (endAngle - startAngle);

                    float outerR = isMajor ? tickR_Outer_Major : tickR_Outer_Minor;
                    g2.setColour(juce::Colours::white.withAlpha(isMajor ? 0.25f : 0.1f)); // Brighter major ticks

                    juce::Line<float> tick(center.getPointOnCircumference(tickR_Inner, angle),
                                           center.getPointOnCircumference(outerR, angle));
                    g2.drawLine(tick, isMajor ? 1.5f : 1.0f);
                }
            }

            // --- C. Main Body ---
            float bodyR = r * 0.85f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF2B2B2B), center.x - bodyR, center.y - bodyR,
                                                    juce::Colour(0xFF050505), center.x + bodyR, center.y + bodyR, true));
            g2.fillEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2);

            // --- D. Chamfer Ring ---
            {
                juce::ColourGradient rimGrad(juce::Colours::white.withAlpha(0.3f), center.x - bodyR, center.y - bodyR,
                                             juce::Colours::black, center.x + bodyR, center.y + bodyR, true);
                g2.setGradientFill(rimGrad);
                g2.drawEllipse(center.x - bodyR, center.y - bodyR, bodyR * 2, bodyR * 2, 2.0f);
            }

            // --- E. Face ---
            float faceR = bodyR * 0.9f;
            g2.setGradientFill(juce::ColourGradient(juce::Colour(0xFF222222), center.x, center.y - faceR,
                                                    juce::Colour(0xFF0A0A0A), center.x, center.y + faceR, false));
            g2.fillEllipse(center.x - faceR, center.y - faceR, faceR * 2, faceR * 2);
        }

        g.drawImageAt(bgImage, x, y);

        // 2. Dynamic Elements (Ticks + Pointer)
        auto bounds = juce::Rectangle<float>((float)x, (float)y, (float)width, (float)height);
        float side = juce::jmin((float)width, (float)height);
        auto center = bounds.getCentre();
        float r = side * 0.5f;
        float bodyR = r * 0.85f;
        float faceR = bodyR * 0.9f;

        float angle = startAngle + pos * (endAngle - startAngle);

        // --- B. Dynamic Scale Markings (Ghost Ticks) ---
        // Moved out of cache so we can light up the active tick
        {
            int numTicks = 24;
            float tickR_Inner = r * 1.18f;
            float tickR_Outer_Major = r * 1.28f;
            float tickR_Outer_Minor = r * 1.23f;
            const float twoPi = 2.0f * juce::MathConstants<float>::pi;

            for (int i = 0; i <= numTicks; ++i)
            {
                float tickAngle = startAngle + (float)i / (float)numTicks * (endAngle - startAngle);
                bool isMajor = (i % 4 == 0);

                // [Pro Fix]: Compute wrapped angular distance to handle wrap-around correctly
                float diff = tickAngle - angle;

                // Normalize diff to -PI...PI range
                while (diff <= -juce::MathConstants<float>::pi)
                    diff += twoPi;
                while (diff > juce::MathConstants<float>::pi)
                    diff -= twoPi;

                float dist = std::abs(diff);
                bool isClose = dist < 0.15f;

                float outerR = isMajor ? tickR_Outer_Major : tickR_Outer_Minor;

                // Base alpha + proximity boost
                float alpha = isMajor ? 0.25f : 0.1f;
                if (isClose)
                    alpha += 0.3f; // Significant boost for "active" tick

                g.setColour(juce::Colours::white.withAlpha(alpha));

                juce::Line<float> tick(center.getPointOnCircumference(tickR_Inner, tickAngle),
                                       center.getPointOnCircumference(outerR, tickAngle));
                g.drawLine(tick, isMajor ? 1.5f : 1.0f);
            }
        }

        // --- Pointer ---
        juce::Path p;
        float ptrW = 3.5f;
        float ptrLen = faceR * 0.6f;
        p.addRoundedRectangle(-ptrW * 0.5f, -faceR + 6.0f, ptrW, ptrLen, 1.0f);

        auto xf = juce::AffineTransform::rotation(angle).translated(center);
        bool active = slider.isMouseOverOrDragging();

        // [Refinement]: Unify Pointer Color - All white now
        juce::Colour activeColor = juce::Colours::white;

        // Glow if active
        if (active)
        {
            g.setColour(activeColor.withAlpha(0.25f));
            g.strokePath(p, juce::PathStrokeType(5.0f), xf);
        }
        g.setColour(active ? activeColor : juce::Colours::black.withAlpha(0.6f));
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

    // --- Controls ---
    auto setupKnob = [&](juce::Slider &s, const juce::String &name)
    {
        s.setName(name); // Crucial for Color Law
        setRotary(s);
        s.setLookAndFeel(knobLnf.get());
        addAndMakeVisible(s);
    };
    setupKnob(drive, "Drive");
    setupKnob(ceiling, "Ceiling");
    setupKnob(trim, "Trim");

    stereoLink.setButtonText("LINK");
    stereoLink.setClickingTogglesState(true);
    stereoLink.setLookAndFeel(knobLnf.get());
    addAndMakeVisible(stereoLink);

    auto setupCombo = [&](juce::ComboBox &c)
    {
        c.setJustificationType(juce::Justification::centred);
        c.setLookAndFeel(knobLnf.get());
        addAndMakeVisible(c);

        // [Fix]: Clear focus on selection to remove persistent orange halo
        c.onChange = [this]
        { grabKeyboardFocus(); };
    };
    setupCombo(adaptiveBias);
    adaptiveBias.addItem("Transparent", 1);
    adaptiveBias.addItem("Balanced", 2);
    adaptiveBias.addItem("Aggressive", 3);
    adaptiveBias.setSelectedId(3, juce::dontSendNotification);

    setupCombo(oversamplingMin);
    oversamplingMin.addItem("2x", 1);
    oversamplingMin.addItem("4x", 2);
    oversamplingMin.addItem("8x", 3);

    // --- Meters ---
    addAndMakeVisible(grMeter);
    grMeter.setInterceptsMouseClicks(false, false);
    addAndMakeVisible(inTpMeter);
    addAndMakeVisible(outTpMeter);

    // --- Labels ---
    auto makeLabel = [&](juce::Label &l, float size, bool bold)
    {
        l.setFont(juce::Font(size, bold ? juce::Font::bold : juce::Font::plain));
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.7f));
        l.setJustificationType(juce::Justification::centred);
        l.setInterceptsMouseClicks(false, false);
        addAndMakeVisible(l);
    };

    makeLabel(grTitleLabel, 22.0f, true); // Larger title for the module
    grTitleLabel.setText("GAIN REDUCTION", juce::dontSendNotification);
    grTitleLabel.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.2f));

    makeLabel(currentGrLabel, 18.0f, true);
    makeLabel(trimValueLabel, 14.0f, true);
    makeLabel(glueValueLabel, 14.0f, true);
    makeLabel(ceilingValueLabel, 14.0f, true);

    auto makeSurgical = [&](juce::Label &l)
    {
        l.setFont(10.0f);
        l.setColour(juce::Label::textColourId, juce::Colours::white.withAlpha(0.4f));
        l.setJustificationType(juce::Justification::centredLeft);
        addAndMakeVisible(l);
    };
    makeSurgical(inTpLabel);
    inTpLabel.setText("IN", juce::dontSendNotification); // Static Label

    makeSurgical(outTpLabel);
    outTpLabel.setText("OUT", juce::dontSendNotification); // Static Label

    makeSurgical(lufsSLabel);
    makeSurgical(lufsILabel);

    // --- Attachments ---
    auto &vts = processor.getAPVTS();
    driveA = std::make_unique<APVTS::SliderAttachment>(vts, "drive", drive);
    ceilingA = std::make_unique<APVTS::SliderAttachment>(vts, "ceiling", ceiling);
    trimA = std::make_unique<APVTS::SliderAttachment>(vts, "trim", trim);
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
    // [Optimization]: Static variables used here for single-file demonstration.
    // In production, move these to the class header (PluginEditor.h) to support multiple plugin instances correctly.
    static float lastGr = -1000.0f;

    float gr = processor.getCurrentGRDb();

    // Pro Pattern: Threshold check to prevent redundant repaints for GR
    // Only update if value changes by more than 0.05 dB
    if (std::abs(gr - lastGr) > 0.05f)
    {
        // [Fix]: Pass raw GR value. Forced negative polarity (-abs) caused issues with the specific meter type.
        grMeter.pushValueDb(gr);
        grMeter.repaint();
        currentGrLabel.setText(juce::String::formatted("%.1f dB", std::abs(gr)), juce::dontSendNotification);
        lastGr = gr;
    }

    auto &vts = processor.getAPVTS();
    trimValueLabel.setText(juce::String::formatted("%.1f dB", vts.getRawParameterValue("trim")->load()), juce::dontSendNotification);
    glueValueLabel.setText(juce::String::formatted("%.1f %%", vts.getRawParameterValue("drive")->load()), juce::dontSendNotification);
    ceilingValueLabel.setText(juce::String::formatted("%.1f dB", vts.getRawParameterValue("ceiling")->load()), juce::dontSendNotification);

    float inL, inR, outL, outR;
    // [Fix]: Unconditional update to ensure meters don't freeze if return check fails.
    processor.getCurrentTruePeakDbTP_LR(inL, inR, outL, outR);

    // [Fix]: Unconditional update for Input/Output Meters
    // Removed threshold check because it was preventing the internal peak-hold decay
    // animation from running when the audio level was stable.
    inTpMeter.pushValueDbLR(inL, inR);
    inTpMeter.repaint();

    outTpMeter.pushValueDbLR(outL, outR);
    outTpMeter.repaint();

    // Removed dynamic text updates for inTpLabel and outTpLabel
}

void CompassMasteringLimiterAudioProcessorEditor::paint(juce::Graphics &g)
{
    g.fillAll(juce::Colour(0xFF0D0D0D)); // Base Matte Black

    // --- Background Noise Texture (Refined: Cached Film Grain) ---
    // Optimization: Draw cached noise instead of 3000 random rects per frame
    if (auto *lnf = dynamic_cast<CompassKnobLookAndFeel *>(knobLnf.get()))
    {
        lnf->drawBackgroundNoise(g, getWidth(), getHeight());
    }

    // --- Vignette (Refined: Tighter falloff) ---
    {
        juce::ColourGradient vig(juce::Colours::transparentBlack, getWidth() / 2.0f, getHeight() / 2.0f,
                                 juce::Colours::black.withAlpha(0.6f), 0.0f, 0.0f, true);
        g.setGradientFill(vig);
        g.fillAll();
    }

    // --- Panel Screws (Industrial Fasteners) ---
    auto drawScrew = [&](int cx, int cy)
    {
        float r = 6.0f;
        g.setGradientFill(juce::ColourGradient(juce::Colour(0xFF151515), cx - r, cy - r,
                                               juce::Colour(0xFF2A2A2A), cx + r, cy + r, true));
        g.fillEllipse(cx - r, cy - r, r * 2, r * 2);
        g.setColour(juce::Colours::black.withAlpha(0.8f));
        g.drawEllipse(cx - r, cy - r, r * 2, r * 2, 1.0f);
        g.setColour(juce::Colour(0xFF050505));
        juce::Path p;
        p.addStar(juce::Point<float>((float)cx, (float)cy), 6, r * 0.3f, r * 0.6f);
        g.fillPath(p);
    };
    int m = 14;
    drawScrew(m, m);
    drawScrew(getWidth() - m, m);
    drawScrew(m, getHeight() - m);
    drawScrew(getWidth() - m, getHeight() - m);

    // --- Stamped Branding ---
    g.setFont(15.0f);

    // Shadow (Refined: Softened print thickness)
    g.setColour(juce::Colours::black.withAlpha(0.4f)); // Reduced alpha for realism
    g.drawText("COMPASS", 35, 19, 100, 20, juce::Justification::left);
    g.drawText("// LIMITER", 106, 19, 100, 20, juce::Justification::left);

    // Main text
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.drawText("COMPASS", 34, 18, 100, 20, juce::Justification::left);
    g.setColour(juce::Colour(0xFFE6A532));
    g.drawText("// LIMITER", 105, 18, 100, 20, juce::Justification::left);

    // --- Side Meter Housings ---
    auto drawMeterWell = [&](const juce::Rectangle<int> &bounds, bool isLeft)
    {
        auto well = bounds.toFloat().expanded(6.0f);
        g.setColour(juce::Colour(0xFF131313));
        g.fillRoundedRectangle(well, 6.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black, well.getCentreX(), well.getY(),
                                               juce::Colour(0xFF0A0A0A), well.getCentreX(), well.getBottom(), false));
        g.fillRoundedRectangle(well.reduced(2.0f), 4.0f);
        g.setColour(juce::Colours::white.withAlpha(0.05f));
        g.drawRoundedRectangle(well, 6.0f, 1.0f);

        // Scale Markings
        // [Pro Fix]: Etched look (Shorter, dimmer, drop shadow)
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        float yStart = well.getY() + 4.0f;
        float height = well.getHeight() - 8.0f;
        float ticks[] = {0.0f, 0.25f, 0.5f, 0.75f};
        for (float t : ticks)
        {
            float y = yStart + (t * height);

            // Drop shadow for "etched" effect
            g.setColour(juce::Colours::black.withAlpha(0.5f));
            if (isLeft)
                g.drawHorizontalLine(y + 1.0f, well.getX() - 3.0f, well.getX());
            else
                g.drawHorizontalLine(y + 1.0f, well.getRight(), well.getRight() + 3.0f);

            // Highlight line
            g.setColour(juce::Colours::white.withAlpha(0.1f));
            if (isLeft)
                g.drawHorizontalLine(y, well.getX() - 3.0f, well.getX());
            else
                g.drawHorizontalLine(y, well.getRight(), well.getRight() + 3.0f);
        }
    };
    drawMeterWell(inTpMeter.getBounds(), true);
    drawMeterWell(outTpMeter.getBounds(), false);

    // --- The GR Module Well ---
    if (!grWellBounds.isEmpty())
    {
        auto well = grWellBounds.toFloat();
        g.setColour(juce::Colour(0xFF131313));
        g.fillRoundedRectangle(well.expanded(4.0f), 6.0f);
        g.setGradientFill(juce::ColourGradient(juce::Colours::black, well.getCentreX(), well.getY(),
                                               juce::Colour(0xFF0A0A0A), well.getCentreX(), well.getBottom(), false));
        g.fillRoundedRectangle(well, 4.0f);

        // Grid Lines
        float h = well.getHeight();
        float y0 = well.getY();

        // [Pro Fix]: Alternating rhythm for less visual noise
        // 50% Line (Midpoint)
        g.setColour(juce::Colours::white.withAlpha(0.03f)); // Slightly reduced from 0.04f
        g.drawHorizontalLine(y0 + h * 0.50f, well.getX(), well.getRight());

        // 25% and 75% Lines (Subtler)
        g.setColour(juce::Colours::white.withAlpha(0.015f)); // Much dimmer
        g.drawHorizontalLine(y0 + h * 0.25f, well.getX(), well.getRight());
        g.drawHorizontalLine(y0 + h * 0.75f, well.getX(), well.getRight());

        // [Pro Fix]: Inner Shadow at bottom for depth
        juce::ColourGradient innerShadow(juce::Colours::transparentBlack, well.getCentreX(), well.getBottom() - 20.0f,
                                         juce::Colours::black.withAlpha(0.8f), well.getCentreX(), well.getBottom(), false);
        g.setGradientFill(innerShadow);
        g.fillRoundedRectangle(well, 4.0f);

        // Glass Sheen
        g.setGradientFill(juce::ColourGradient(juce::Colours::white.withAlpha(0.03f), well.getTopLeft(),
                                               juce::Colours::transparentBlack, well.getBottomRight(), false));
        g.fillRoundedRectangle(well, 4.0f);

        // [Pro Fix]: Top Edge Inner Highlight (Glass catch-light)
        g.setColour(juce::Colours::white.withAlpha(0.15f));
        g.drawHorizontalLine(well.getY() + 1.0f, well.getX() + 2.0f, well.getRight() - 2.0f);

        // Frame
        g.setColour(juce::Colours::white.withAlpha(0.1f));
        g.drawRoundedRectangle(well, 4.0f, 1.0f);
    }

    // --- Headers ---
    auto drawHead = [&](juce::String text, juce::Slider &s)
    {
        auto b = s.getBounds().translated(0, -20);
        bool active = s.isMouseOverOrDragging();
        g.setColour(active ? juce::Colours::white.withAlpha(0.9f) : juce::Colours::white.withAlpha(0.4f));
        g.setFont(11.0f);
        g.drawText(text.toUpperCase(), b.withHeight(16), juce::Justification::centred);
    };
    drawHead("Glue", drive);
    drawHead("Ceiling", ceiling);
    drawHead("Trim", trim);

    // --- Value Etchings ---
    auto drawEtch = [&](juce::Component &c)
    {
        auto b = c.getBounds().toFloat().reduced(6.0f, 0.0f);
        g.setColour(juce::Colours::black.withAlpha(0.7f));
        g.fillRoundedRectangle(b, 3.0f);
        g.setColour(juce::Colours::white.withAlpha(0.08f));
        g.drawRoundedRectangle(b, 3.0f, 1.0f);
    };
    drawEtch(trimValueLabel);
    drawEtch(glueValueLabel);
    drawEtch(ceilingValueLabel);
    drawEtch(currentGrLabel);
}

void CompassMasteringLimiterAudioProcessorEditor::resized()
{
    // --- Layout Constants (Fix direction: define grid and constants) ---
    // Grid & Structure
    const int sidePanelWidth = 80;
    const int mainPadding = 10;
    const int topDeckHeight = 170;
    const int midDeckHeight = 120;
    const int botDeckHeight = 100;
    const int brandingHeight = 20;

    // Components dimensions
    const int meterWidth = 28;
    const int meterHeight = 340;
    const int bigKnobSize = 130;
    const int smallKnobSize = 80;
    const int uiControlHeight = 24; // Buttons, Combos
    const int labelHeight = 20;

    // Spacing & Offsets
    const int labelOverlapStandard = 5; // Tucks label up towards knob
    const int labelOverlapTrim = 8;     // Tucks label tighter for Trim
    const int bottomDeckYOffset = -10;  // Vertical shift for bottom row alignment

    auto r = getLocalBounds();

    // --- 1. Side Meter Columns ---
    auto rLeft = r.removeFromLeft(sidePanelWidth);
    auto rRight = r.removeFromRight(sidePanelWidth);

    int meterY = (getHeight() - meterHeight) / 2;

    inTpMeter.setBounds(rLeft.getX() + (sidePanelWidth - meterWidth) / 2, meterY, meterWidth, meterHeight);
    outTpMeter.setBounds(rRight.getX() + (sidePanelWidth - meterWidth) / 2, meterY, meterWidth, meterHeight);

    // Labels relative to meters
    const int meterLabelGap = 8;
    inTpLabel.setBounds(rLeft.getX(), meterY + meterHeight + meterLabelGap, sidePanelWidth, labelHeight);
    outTpLabel.setBounds(rRight.getX(), meterY + meterHeight + meterLabelGap, sidePanelWidth, labelHeight);

    // --- Main Center ---
    auto main = r.reduced(mainPadding);

    // A. Top Deck (Knobs)
    auto topDeck = main.removeFromTop(topDeckHeight);
    topDeck.removeFromTop(brandingHeight); // Branding spacer

    auto driveArea = topDeck.removeFromLeft(topDeck.getWidth() / 2);
    auto ceilArea = topDeck;

    // Drive Knob
    drive.setBounds(driveArea.withSizeKeepingCentre(bigKnobSize, bigKnobSize));
    glueValueLabel.setBounds(drive.getX(), drive.getBottom() - labelOverlapStandard, drive.getWidth(), labelHeight);

    // Ceiling Knob
    ceiling.setBounds(ceilArea.withSizeKeepingCentre(bigKnobSize, bigKnobSize));
    ceilingValueLabel.setBounds(ceiling.getX(), ceiling.getBottom() - labelOverlapStandard, ceiling.getWidth(), labelHeight);

    // B. Middle Deck (GR Well)
    auto midDeck = main.removeFromTop(midDeckHeight);
    const int grTitleHeight = 30;
    grTitleLabel.setBounds(midDeck.removeFromTop(grTitleHeight)); // "GAIN REDUCTION" title

    // GR Meter Well & Label
    grWellBounds = midDeck.reduced(mainPadding, 0).toNearestInt();
    const int wellPadding = 4;
    grMeter.setBounds(grWellBounds.reduced(wellPadding));
    grMeter.setVisible(true); // [Fix]: Explicitly ensure visibility

    const int grLabelGap = 6;
    currentGrLabel.setBounds(grWellBounds.getX(), grWellBounds.getBottom() + grLabelGap, grWellBounds.getWidth(), labelHeight);

    // C. Bottom Deck (Utilities)
    auto botDeck = main.removeFromBottom(botDeckHeight);
    int colW = botDeck.getWidth() / 4;

    // 1. Trim (Large, Bottom Left)
    auto c1 = botDeck.removeFromLeft(colW);
    trim.setBounds(c1.withSizeKeepingCentre(smallKnobSize, smallKnobSize).translated(0, bottomDeckYOffset));
    trimValueLabel.setBounds(trim.getX(), trim.getBottom() - labelOverlapTrim, trim.getWidth(), labelHeight);

    // 2. Bias
    adaptiveBias.setBounds(botDeck.removeFromLeft(colW).withSizeKeepingCentre(110, uiControlHeight).translated(0, bottomDeckYOffset));

    // 3. Link
    stereoLink.setBounds(botDeck.removeFromLeft(colW).withSizeKeepingCentre(90, uiControlHeight).translated(0, bottomDeckYOffset));

    // 4. OS
    oversamplingMin.setBounds(botDeck.withSizeKeepingCentre(90, uiControlHeight).translated(0, bottomDeckYOffset));
}