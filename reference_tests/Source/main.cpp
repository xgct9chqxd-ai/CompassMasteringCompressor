#include <iostream>
#include <cmath>
#include <limits>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>

#include "reference_core/reference_core.h"
#include "PluginProcessor.h"

static bool bufferAllFinite (const juce::AudioBuffer<float>& b) noexcept
{
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* p = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
        {
            const float v = p[i];
            if (! std::isfinite ((double) v))
                return false;
        }
    }
    return true;
}

static float bufferPeakAbs (const juce::AudioBuffer<float>& b) noexcept
{
    float pk = 0.0f;
    for (int ch = 0; ch < b.getNumChannels(); ++ch)
    {
        const float* p = b.getReadPointer (ch);
        for (int i = 0; i < b.getNumSamples(); ++i)
            pk = std::max (pk, std::abs (p[i]));
    }
    return pk;
}

int main()
{
    juce::ScopedJuceInitialiser_GUI juceInit;

    const bool okCore = (reference_core::kReferenceCoreId != nullptr);
    if (! okCore)
    {
        std::cout << "reference_tests FAIL (reference_core)\n";
        return 1;
    }

    //// [CML:TEST] Processor Lifecycle Smoke
    CompassMasteringLimiterAudioProcessor proc;
    proc.setPlayConfigDetails (2, 2, 44100.0, 512);

    auto runOne = [&proc](double sr, int bs) -> bool
    {
        proc.prepareToPlay (sr, bs);

        juce::AudioBuffer<float> buf (2, bs);
        juce::MidiBuffer midi;

        double phase = 0.0;
        const double w = 2.0 * 3.14159265358979323846 * 1000.0 / sr;
        for (int i = 0; i < bs; ++i)
        {
            const float s = (float) std::sin (phase);
            phase += w;
            buf.setSample (0, i, s);
            buf.setSample (1, i, s);
        }

        for (int k = 0; k < 200; ++k)
        {
            proc.processBlock (buf, midi);
            if (! bufferAllFinite (buf))
                return false;
        }

        const float pk = bufferPeakAbs (buf);
        if (! std::isfinite ((double) pk))
            return false;

        proc.releaseResources();
        return true;
    };

    //// [CML:TEST] Block Size Sweep Stability
    const int blockSizes[] = { 16, 32, 64, 128, 256, 512, 1024 };
    for (int bs : blockSizes)
    {
        if (! runOne (44100.0, bs))
        {
            std::cout << "reference_tests FAIL (block " << bs << ")\n";
            return 1;
        }
    }

    //// [CML:TEST] Sample Rate Sweep Stability
    const double sampleRates[] = { 44100.0, 48000.0, 96000.0, 192000.0 };
    for (double sr : sampleRates)
    {
        if (! runOne (sr, 256))
        {
            std::cout << "reference_tests FAIL (sr " << (int) sr << ")\n";
            return 1;
        }
    }

    std::cout << "reference_tests PASS\n";
    return 0;
}
