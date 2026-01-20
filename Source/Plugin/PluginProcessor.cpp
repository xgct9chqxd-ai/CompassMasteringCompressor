#include "PluginProcessor.h"
#include "PluginEditor.h"

CompassMasteringLimiterAudioProcessor::CompassMasteringLimiterAudioProcessor()
: juce::AudioProcessor (BusesProperties()
#if ! JucePlugin_IsMidiEffect
 #if ! JucePlugin_IsSynth
    .withInput  ("Input",  juce::AudioChannelSet::stereo(), true)
 #endif
    .withOutput ("Output", juce::AudioChannelSet::stereo(), true)
#endif
)
{
    apvts = std::make_unique<APVTS>(*this, nullptr, "PARAMS", createParameterLayout());
}

CompassMasteringLimiterAudioProcessor::APVTS::ParameterLayout
CompassMasteringLimiterAudioProcessor::createParameterLayout()
{
    APVTS::ParameterLayout layout;

    // IDs are constitution-locked (no aliases, no hidden controls)
    // Value ranges are defined exactly per Gate-2 spec.
    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "drive", 1 },
        "Glue",
        juce::NormalisableRange<float> { 0.0f, 20.0f, 0.01f },
        0.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float v, int) { return juce::String (v, 1) + " dB"; },
        [] (const juce::String& s) { return s.getFloatValue(); }
    ));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "ceiling", 1 },
        "Ceiling",
        juce::NormalisableRange<float> { -6.0f, 0.0f, 0.01f },
        -0.3f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float v, int) { return juce::String (v, 1) + " dBTP"; },
        [] (const juce::String& s) { return s.getFloatValue(); }
    ));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "adaptive_bias", 1 },
        "Adaptive Bias",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.5f },
        0.5f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float v, int)
        {
            // Constitution: continuous scalar with discrete UI labels mapped to fixed scalar points.
            // Enforced scalar points: 0.0=Transparent, 0.5=Balanced, 1.0=Aggressive
            if (v <= 0.25f) return juce::String ("Transparent");
            if (v >= 0.75f) return juce::String ("Aggressive");
            return juce::String ("Balanced");
        },
        [] (const juce::String& s)
        {
            if (s.equalsIgnoreCase ("Transparent")) return 0.0f;
            if (s.equalsIgnoreCase ("Balanced"))    return 0.5f;
            if (s.equalsIgnoreCase ("Aggressive"))  return 1.0f;

            const float v = s.getFloatValue();
            const float vc = juce::jlimit (0.0f, 1.0f, v);
            const float snapped = std::round (vc * 2.0f) * 0.5f; // -> {0.0, 0.5, 1.0}
            return juce::jlimit (0.0f, 1.0f, snapped);
        }
    ));

layout.add(std::make_unique<juce::AudioParameterFloat>(
    juce::ParameterID{"trim", 1},
    "Trim",
    juce::NormalisableRange<float>{-20.0f, 20.0f, 0.01f},
    0.0f,
    juce::String(),
    juce::AudioProcessorParameter::genericParameter,
    [](float v, int) { return juce::String(v, 1) + " dB"; },
    [](const juce::String& s) { return s.getFloatValue(); }
));

    layout.add (std::make_unique<juce::AudioParameterFloat>(
        juce::ParameterID { "stereo_link", 1 },
        "Stereo Link",
        juce::NormalisableRange<float> { 0.0f, 1.0f, 0.0001f },
        1.0f,
        juce::String(),
        juce::AudioProcessorParameter::genericParameter,
        [] (float v, int) { return juce::String (v * 100.0f, 1) + " %"; },
        [] (const juce::String& s) { return s.getFloatValue() / 100.0f; }
    ));

    layout.add (std::make_unique<juce::AudioParameterChoice>(
        juce::ParameterID { "oversampling_min", 1 },
        "Oversampling Min",
        juce::StringArray { "2x", "4x", "8x" },
        0
    ));

    return layout;
}

void CompassMasteringLimiterAudioProcessor::reset (double sampleRate, int maxBlock, int channels) noexcept
{
    lastSampleRate = (sampleRate > 0.0 ? sampleRate : 44100.0);
    lastInvSampleRate = 1.0 / juce::jmax (1.0, lastSampleRate);
    effectiveControlInvDt = lastInvSampleRate;
    effectiveAudioInvDt   = lastInvSampleRate; // OSFactor=1.0 until oversampled audio-path processing is introduced
    lastMaxBlock   = (maxBlock > 0 ? maxBlock : 0);
    lastChannels   = (channels > 0 ? channels : 0);

    // Phase 11 — Metering Plumbing: deterministic meter timing + accumulator reset (single source of truth).
    meterDt = 1.0 / lastSampleRate;
    meterPublishSamples = (int) juce::jmax (1.0, lastSampleRate / 50.0); // 50 Hz, clamped
    lastLink01Smoothed = 1.0;
    meterCountdown = meterPublishSamples;

    for (int c = 0; c < 2; ++c)
    {
        inPeakHold[c]  = 0.0;
        outPeakHold[c] = 0.0;
        inRmsSq[c]     = 0.0;
        outRmsSq[c]    = 0.0;
        inTpHold[c]    = 0.0;
        outTpHold[c]   = 0.0;
        grHoldDb[c]    = 0.0;
    }

    // Loudness reset (deterministic; bounded; allocation-free).
    lufsChunkE.fill (0.0);
    lufsChunkWrite  = 0u;
    lufsChunkFilled = 0u;
    lufsShortSumE   = 0.0;
    lufsCurChunkE   = 0.0;
    lufsCurChunkN   = 0;
    lufsIntSumE     = 0.0;
    lufsIntN        = 0u;

    // Publication bookkeeping reset (deterministic; lock-free). Do NOT clear meterRing here (reset() may run on audio thread).
    meterWriteIndex.store (0u, std::memory_order_relaxed);
    meterReadIndex.store  (0u, std::memory_order_relaxed);
    meterSeq.store        (0u, std::memory_order_relaxed);
    meterLastReadSeq.store(0u, std::memory_order_relaxed);

    // Deterministic reset of adaptive/envelope/guard state:
    const float driveDb   = apvts->getRawParameterValue ("drive")->load();
    const float ceilingDb = apvts->getRawParameterValue ("ceiling")->load();
    const float bias01    = apvts->getRawParameterValue ("adaptive_bias")->load();
    const float link01    = apvts->getRawParameterValue ("stereo_link")->load();

    driveDbSmoothed.reset (lastSampleRate, 0.020);        // 20 ms
    ceilingDbSmoothed.reset (lastSampleRate, 0.020);      // 20 ms
    adaptiveBias01Smoothed.reset (lastSampleRate, 0.050); // 50 ms
    stereoLink01Smoothed.reset (lastSampleRate, 0.050);   // 50 ms

    driveDbSmoothed.setCurrentAndTargetValue (driveDb);
    ceilingDbSmoothed.setCurrentAndTargetValue (ceilingDb);
    adaptiveBias01Smoothed.setCurrentAndTargetValue (bias01);
    stereoLink01Smoothed.setCurrentAndTargetValue (link01);

    // Envelope state reset (deterministic; bounded)
    microStage1DbState = { 0.0, 0.0 };
    microStage2DbState = { 0.0, 0.0 };
    macroEnergyState   = { 0.0, 0.0 };

    lastAttnTargetDb   = { 0.0, 0.0 };
    lastAppliedAttnDb  = { 0.0, 0.0 };

    // Spectral Guardrails state (measurement-only)
    guardLpState = { 0.0, 0.0 };
    guardHpState2 = { 0.0, 0.0 };
    guardTotE    = { 0.0, 0.0 };
    guardHiE     = { 0.0, 0.0 };
    lowShelfZ1   = { 0.0, 0.0 };

    // Phase 1.4 — Crest Factor RMS Window (50 ms rectangular MA) + SR_max enforcement
    invalidConfig = (sampleRate > 192000.0);

    rmsWinN = (int) std::ceil (0.050 * sampleRate);
    if (rmsWinN < 1) rmsWinN = 1;
    if (rmsWinN > kCrestRmsWinMaxN) rmsWinN = kCrestRmsWinMaxN;

    rmsSilenceResetN = (int) std::ceil (0.100 * sampleRate);
    if (rmsSilenceResetN < 1) rmsSilenceResetN = 1;

   #if JUCE_DEBUG
    jassert (rmsWinN <= kCrestRmsWinMaxN);
   #endif

    for (int c = 0; c < 2; ++c)
    {
        rmsSqSum[c] = 0.0;
        rmsWriteIdx[c] = 0;
        rmsSilenceCount[c] = 0;
        for (int i = 0; i < kCrestRmsWinMaxN; ++i)
            rmsSqRing[c][i] = 0.0;
    }

    eventDensityState  = { 0.0, 0.0 };

    lastOutScalar = { 1.0f, 1.0f };

    // Ceiling envelope (state + coeffs) — computed here (SR-dependent), never in processBlock.
    for (int c = 0; c < 2; ++c)
        ceilingGainState[c] = 1.0f;
    ceilingGainStateLinked = 1.0f;

    {
        // Step 3.1 — Envelope SR domain: must match the loop where ceiling is enforced.
        // Ceiling enforcement runs inside processOneSample(), which is called at:
        // - Oversampled rate when canOsAudio == true
        // - Native rate otherwise
        float srEnv = (float) juce::jmax (1.0, lastSampleRate);
        if (activeOversampler != nullptr)
        {
            const int osFactor = juce::jmax (1, (int) activeOversampler->getOversamplingFactor());
            srEnv = (float) juce::jmax (1.0, lastSampleRate * (double) osFactor);
        }

        // Step 3.2 — ms -> tau (seconds), with safety clamps
        const float tauDown = juce::jmax (1.0e-5f, ceilingAttackMs  * 0.001f);
        const float tauUp   = juce::jmax (1.0e-5f, ceilingReleaseMs * 0.001f);

        float aDown = std::expf (-1.0f / (tauDown * srEnv));
        float aUp   = std::expf (-1.0f / (tauUp   * srEnv));

        if (! std::isfinite ((double) aDown)) aDown = 0.0f;
        if (! std::isfinite ((double) aUp))   aUp   = 0.0f;

        // Clamp to (0, 1)
        aDown = juce::jlimit (1.0e-6f, 0.999999f, aDown);
        aUp   = juce::jlimit (1.0e-6f, 0.999999f, aUp);

        ceilA_down = aDown;
        ceilA_up   = aUp;
    }
}

void CompassMasteringLimiterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    reset (sampleRate, samplesPerBlock, ch);

    // Optional deterministic ring init (fixed-size, outside audio thread).
    // This is allowed here (prepareToPlay is not the audio callback boundary).
    meterRing.fill (MeterSnapshot {});

    // Phase 1.x — Hot-path exp/log tables (deterministic; fixed-size; no audio-thread init)
    {
        // exp(-x) table, x in [0, kExpMaxX]
        for (int i = 0; i < kExpTableSize; ++i)
        {
            const double t = (kExpTableSize > 1 ? (double) i / (double) (kExpTableSize - 1) : 0.0);
            const double x = t * kExpMaxX;
            expNegTable[(size_t) i] = std::exp (-x);
        }

        // log10 table holds the log-domain values directly: [-12, +6]
        for (int i = 0; i < kLogTableSize; ++i)
        {
            const double t = (kLogTableSize > 1 ? (double) i / (double) (kLogTableSize - 1) : 0.0);
            log10Table[(size_t) i] = kLogMin + t * kLogRange;
        }
    }

    // Prebuild oversampling instances (no allocations in audio thread).
    prepareOversampling (ch, samplesPerBlock);

    // Latch initial oversampling selection (treated as transport-safe init).
    const int osMinIndex = (int) apvts->getRawParameterValue ("oversampling_min")->load();
    selectOversamplingAtBoundary (osMinIndex);

    // Transport becomes unknown on prepare (hosts differ); edge detection begins on first block.
    transportKnown = false;
    lastTransportPlaying = false;
}

void CompassMasteringLimiterAudioProcessor::releaseResources()
{

    // Phase 11 — Metering Plumbing: deterministic teardown reset (no SR math here).
    meterPublishSamples = 0;
    meterCountdown      = 0;
    meterDt             = 0.0;

    for (int c = 0; c < 2; ++c)
    {
        inPeakHold[c]  = 0.0;
        outPeakHold[c] = 0.0;
        inRmsSq[c]     = 0.0;
        outRmsSq[c]    = 0.0;
        inTpHold[c]    = 0.0;
        outTpHold[c]   = 0.0;
        grHoldDb[c]    = 0.0;
    }

    meterWriteIndex.store (0u, std::memory_order_relaxed);
    meterReadIndex.store  (0u, std::memory_order_relaxed);
    meterSeq.store        (0u, std::memory_order_relaxed);
    meterLastReadSeq.store(0u, std::memory_order_relaxed);

}

bool CompassMasteringLimiterAudioProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto& in  = layouts.getMainInputChannelSet();
    const auto& out = layouts.getMainOutputChannelSet();
    return (in == out) && (! out.isDisabled());
}


double CompassMasteringLimiterAudioProcessor::onePoleAlpha (double tauSec, double dtSec) noexcept
{
    return std::exp (-dtSec / juce::jmax (1.0e-9, tauSec));
}

double CompassMasteringLimiterAudioProcessor::expLookup (double x) const noexcept
{
    if (! std::isfinite (x) || x <= 0.0)
        return 1.0;

    if (x >= kExpMaxX)
        return expNegTable.back();

    const double scaled = x / kExpMaxX;
    const double pos = scaled * (double) (kExpTableSize - 1);
    const int idx = (int) std::floor (pos);
    const double frac = pos - (double) idx;

    const int i0 = juce::jlimit (0, kExpTableSize - 2, idx);
    const int i1 = i0 + 1;

    const double a = expNegTable[(size_t) i0];
    const double b = expNegTable[(size_t) i1];
    return a * (1.0 - frac) + b * frac;
}

double CompassMasteringLimiterAudioProcessor::log10Lookup (double y) const noexcept
{
    if (! std::isfinite (y) || y < 1.0e-12)
        y = 1.0e-12;

    // Index computation only (per spec). Returned value comes from table interpolation.
    const double logy = std::log10 (y);

    double scaled = (logy - kLogMin) / kLogRange;
    scaled = juce::jlimit (0.0, 1.0, scaled);

    const double pos = scaled * (double) (kLogTableSize - 1);
    const int idx = (int) std::floor (pos);
    const double frac = pos - (double) idx;

    const int i0 = juce::jlimit (0, kLogTableSize - 2, idx);
    const int i1 = i0 + 1;

    const double a = log10Table[(size_t) i0];
    const double b = log10Table[(size_t) i1];
    return a * (1.0 - frac) + b * frac;
}

double CompassMasteringLimiterAudioProcessor::probeSettleTimeSec (double sampleRate) const noexcept
{
    constexpr double kSoftK     = 32.0;
    constexpr double kMaxAttnDb = 120.0;

    // Phase 1.4 spec-binding continuity invariant (per-sample hard cap)
    constexpr double kMaxStepDb = 0.01;

    const double sr = juce::jmax (1.0, sampleRate);
    const double dt = 1.0 / sr;

    // Full-scale instantaneous condition in the existing GR target path:
    // tpDb is formed as 20*log10(|s| + eps). For |s|=1, tpDb ~= 0 dB.
    const double tpDb = 0.0;
    const double ceilingDb = 0.0;

    auto softplusTargetDb = [&](double driveDb) noexcept -> double
    {
        const double x = (tpDb + driveDb) - ceilingDb;
        const double z = kSoftK * x;
        const double softplus = z + std::log1p (std::exp (-std::abs (z)));
        double attnTargetDb = softplus / kSoftK;
        return juce::jlimit (0.0, kMaxAttnDb, attnTargetDb);
    };

    auto stepState = [&](double yPrev, double targetDb) noexcept -> double
    {
        return yPrev + juce::jlimit (-kMaxStepDb, +kMaxStepDb, (targetDb - yPrev));
    };

    // Spec: simulate Drive step 0 -> +20 dB for at least 100 ms; use long enough tail for asymptote.
    const int nTotal = (int) (0.200 * sr + 0.5);
    const int wN     = (int) (0.100 * sr + 0.5);
    if (nTotal <= 2 || wN <= 2 || wN >= nTotal)
        return 0.0;

    // Simulate the step and store GR[n] deterministically (caller thread only).
    std::vector<double> gr;
    gr.resize ((size_t) nTotal, 0.0);

    double y = 0.0;
    for (int n = 0; n < nTotal; ++n)
    {
        const double driveDb = (n == 0 ? 0.0 : 20.0);
        const double targetDb = softplusTargetDb (driveDb);
        y = stepState (y, targetDb);
        y = juce::jlimit (0.0, kMaxAttnDb, y);
        gr[(size_t) n] = y;
    }

    const double grFinal = gr.back();

    // Sliding window: earliest t where over next 100 ms, max(|GR - GR_final|) <= 0.1 dB.
    for (int n = 0; n + wN <= nTotal; ++n)
    {
        double maxDev = 0.0;
        for (int k = n; k < n + wN; ++k)
        {
            const double dev = std::abs (gr[(size_t) k] - grFinal);
            if (dev > maxDev) maxDev = dev;
            if (maxDev > 0.1) break;
        }

        if (maxDev <= 0.1)
            return (double) n * dt;
    }

    return (double) nTotal * dt;
}

bool CompassMasteringLimiterAudioProcessor::probeContinuityFastAutomation (double sampleRate, double& outMaxAbsDeltaDb) const noexcept
{
    constexpr double kSoftK     = 32.0;
    constexpr double kMaxAttnDb = 120.0;

    // Phase 1.4 spec-binding continuity invariant (per-sample hard cap)
    constexpr double kMaxStepDb = 0.01;

    const double sr = juce::jmax (1.0, sampleRate);
    const double dt = 1.0 / sr;

    const double tpDb = 0.0;
    const double ceilingDb = 0.0;

    auto softplusTargetDb = [&](double driveDb) noexcept -> double
    {
        const double x = (tpDb + driveDb) - ceilingDb;
        const double z = kSoftK * x;
        const double softplus = z + std::log1p (std::exp (-std::abs (z)));
        double attnTargetDb = softplus / kSoftK;
        return juce::jlimit (0.0, kMaxAttnDb, attnTargetDb);
    };

    auto stepState = [&](double yPrev, double targetDb) noexcept -> double
    {
        return yPrev + juce::jlimit (-kMaxStepDb, +kMaxStepDb, (targetDb - yPrev));
    };

    // Spec: ramp 0 -> +20 dB over 1 ms, then hold; analyze 10 ms total window.
    const int nRamp  = juce::jmax (2, (int) (0.001 * sr + 0.5));
    const int nTotal = juce::jmax (2, (int) (0.010 * sr + 0.5));

    double y = 0.0;
    double maxAbsDelta = 0.0;

    for (int n = 0; n < nTotal; ++n)
    {
        double driveDb = 20.0;
        if (n < nRamp)
        {
            const double t = (double) n / (double) (nRamp - 1);
            driveDb = 20.0 * t;
        }

        const double targetDb = softplusTargetDb (driveDb);

        const double yPrev = y;
        y = stepState (y, targetDb);
        y = juce::jlimit (0.0, kMaxAttnDb, y);

        const double d = std::abs (y - yPrev);
        if (d > maxAbsDelta) maxAbsDelta = d;
    }

    outMaxAbsDeltaDb = maxAbsDelta;
    return (maxAbsDelta <= 0.01);
}

void CompassMasteringLimiterAudioProcessor::processOneSample (float* const* chPtr,
                                                             int numCh,
                                                             int i,
                                                             double dt,
                                                             double driveDb,
                                                             double ceilingDb,
                                                             double bias01,
                                                             double link01,
                                                             double& grDbNegMin) noexcept
{
    // Softplus controls (smooth, monotonic, branch-free)
    constexpr double kEpsLin    = 1.0e-12; // avoids log(0)
    constexpr double kSoftK     = 32.0;
    constexpr double kMaxAttnDb = 120.0;

    // Time constants (seconds). Names avoid forbidden terminology.
    constexpr double kMacroSecBase = 0.1200; // 120 ms nominal

    std::array<double, 2> attnDbCh { 0.0, 0.0 };

    // Spectral Guardrails (Phase 1.7): parallel HF measurement only (no influence on envelope)
    // Pipeline (per channel):
    // - 2nd-order Butterworth HPF @ 8 kHz on abs(s)
    // - One-pole smoothing tau = 5 ms
    // - Leaky integrator on squared smoothed output tau = 20 ms
    constexpr double kGuardFcHz = 8000.0;
    constexpr double kHfTauSec  = 0.005;
    constexpr double kAccTauSec = 0.020;

    const int chProc = juce::jmin (2, numCh);
    double hfEnergyStereo = 0.0;

    const bool overloadAssistOn = (overloadAssistBlocks > 0);

    for (int c = 0; c < chProc; ++c)
    {
        const double s = (double) chPtr[c][i];

        // Step 4 — Compute Input peak + RMS (broadband), linear domain (no dB).
        // Guard: accumulators are 2ch; never index beyond [0..1].
        if (c < 2)
        {
            const double inAbs = std::abs (s);
            inPeakHold[(size_t) c] = juce::jmax (inPeakHold[(size_t) c], inAbs);
            inRmsSq[(size_t) c] += (inAbs * inAbs);
        }

        if (! overloadAssistOn)
        {
            double z1 = guardLpState[(size_t) c];
            double z2 = guardHpState2[(size_t) c];
            const double absS = std::abs (s);
            double y = guardNb0 * absS + z1;
            z1 = guardNb1 * absS - guardNa1 * y + z2;
            z2 = guardNb2 * absS - guardNa2 * y;
            guardLpState[(size_t) c] = z1;
            guardHpState2[(size_t) c] = z2;

            // Measurement-path low-shelf compensation (Phase 1.7 Priority 4)
            double yShelf = lowShelfB0 * y + lowShelfZ1[(size_t) c];
            lowShelfZ1[(size_t) c] = lowShelfB1 * y - lowShelfA1 * yShelf;
            y = yShelf;

            const double aHf  = expLookup (dt / kHfTauSec);
            const double aAcc = expLookup (dt / kAccTauSec);

            // hfSmoothed (tau = 5 ms) stored in guardTotE
            const double hfSm = aHf * guardTotE[(size_t) c] + (1.0 - aHf) * y;
            guardTotE[(size_t) c] = hfSm;

            // hfEnergy (tau = 20 ms) stored in guardHiE
            const double e = aAcc * guardHiE[(size_t) c] + (1.0 - aAcc) * (hfSm * hfSm);
            guardHiE[(size_t) c] = e;

            if (e > hfEnergyStereo) hfEnergyStereo = e;
        }

        const double tpDb = 20.0 * std::log10 (std::abs (s) + kEpsLin);

        // Phase 1.9 — Silence-horizon reset (0.35 s): bounded adaptive memory under sustained silence.
        {
            constexpr double kSilenceHorizonSec = 0.35;
            constexpr double kSilenceDecayAlpha = 0.995;

            const double fs = (dt > 0.0 ? (1.0 / dt) : 0.0);
            const int silenceSamplesRequired = (int) std::ceil (kSilenceHorizonSec * juce::jmax (1.0, fs));

            if (tpDb < -90.0)
                ++silenceCountSamples[(size_t) c];
            else
                silenceCountSamples[(size_t) c] = (int) (kSilenceDecayAlpha * (double) silenceCountSamples[(size_t) c]);

            if (silenceCountSamples[(size_t) c] >= silenceSamplesRequired)
            {
                microStage1DbState[(size_t) c] = 0.0;
                microStage2DbState[(size_t) c] = 0.0;
                macroEnergyState[(size_t) c]   = 0.0;
                eventDensityState[(size_t) c]  = 0.0;

                guardLpState[(size_t) c]       = 0.0;
                guardHpState2[(size_t) c] = 0.0;
                guardTotE[(size_t) c]          = 0.0;
                guardHiE[(size_t) c]           = 0.0;
                lowShelfZ1[(size_t) c]         = 0.0;

                silenceCountSamples[(size_t) c] = 0;
            }
        }

        const double x = (tpDb + driveDb) - ceilingDb;

        const double z = kSoftK * x;
        const double softplus = z + std::log1p (std::exp (-std::abs (z)));
        double attnTargetDb = softplus / kSoftK;
        attnTargetDb = juce::jlimit (0.0, kMaxAttnDb, attnTargetDb);

        // Priority 7 — Adaptive GR floor & hysteresis (pre-gate proxy macro01; bounded ±0.03 dB)
        constexpr double kGrFloorDbBase    = 0.05;
        constexpr double kHysteresisDbBase = 0.08;
        constexpr double kMaxAdaptDb       = 0.03;

        // Pre-gate proxy energy input (same math as later, but using current attnTargetDb pre-gate)
        const double energyInputEarly = juce::jmax (0.0, std::pow (10.0, attnTargetDb / 20.0) - 1.0);

        // Early macro01 proxy (same math form as later; no state writes)
        const double macroSecEarly = kMacroSecBase * (1.20 - 0.40 * bias01);
        const double aMEarly = std::exp (-dt / macroSecEarly);
        const double EprevEarly = macroEnergyState[(size_t) c];
        const double uEarly = energyInputEarly;

        double EnextEarly = aMEarly * EprevEarly + (1.0 - aMEarly) * uEarly;
        EnextEarly = juce::jlimit (juce::jmin (EprevEarly, uEarly), juce::jmax (EprevEarly, uEarly), EnextEarly);
        EnextEarly = juce::jmax (0.0, EnextEarly);

        const double macro01Early = EnextEarly / (1.0 + EnextEarly);

        // Smooth, bounded offset: smoothstep(macro01Early) ∈ [0,1] → m ∈ [-1,+1] → deltaDb ∈ [-0.03,+0.03]
        const double macroSmoothEarly = (3.0 * macro01Early * macro01Early) - (2.0 * macro01Early * macro01Early * macro01Early);
        const double mEarly = (2.0 * macroSmoothEarly) - 1.0;
        const double deltaDb = kMaxAdaptDb * mEarly;

        double kGrFloorDb = kGrFloorDbBase - deltaDb;
        double kHysteresisDb = kHysteresisDbBase + deltaDb;

        kGrFloorDb    = juce::jlimit (kGrFloorDbBase - kMaxAdaptDb,    kGrFloorDbBase + kMaxAdaptDb,    kGrFloorDb);
        kHysteresisDb = juce::jlimit (kHysteresisDbBase - kMaxAdaptDb, kHysteresisDbBase + kMaxAdaptDb, kHysteresisDb);

        kGrFloorDb    = juce::jmax (0.0, kGrFloorDb);
        kHysteresisDb = juce::jmax (0.0, kHysteresisDb);

        double currentTarget = attnTargetDb;
        if (currentTarget < kGrFloorDb)
            currentTarget = 0.0;
        else if (currentTarget < lastAttnTargetDb[(size_t) c] + kHysteresisDb)
            currentTarget = lastAttnTargetDb[(size_t) c];

        lastAttnTargetDb[(size_t) c] = currentTarget;
        attnTargetDb = currentTarget;

        const double energyInput = juce::jmax (0.0, std::pow (10.0, attnTargetDb / 20.0) - 1.0);

        const double macroSec = kMacroSecBase * (1.20 - 0.40 * bias01);
        const double aM = expLookup (dt / macroSec);

        const double Eprev = macroEnergyState[(size_t) c];
        const double u = energyInput;

        double Enext = aM * Eprev + (1.0 - aM) * u;

        Enext = juce::jlimit (juce::jmin (Eprev, u), juce::jmax (Eprev, u), Enext);
        Enext = juce::jmax (0.0, Enext);

        macroEnergyState[(size_t) c] = Enext;

        const double macro01 = macroEnergyState[(size_t) c] / (1.0 + macroEnergyState[(size_t) c]);
        const double sustained01 = juce::jlimit (0.0, 1.0, macro01);

        // Phase 1.4 — Crest Factor RMS Window (50 ms rectangular MA) + Crest statistic binding
        const double absS = std::abs (s);

        // Deterministic silence reset (100 ms): counter increments when tpDb < -90 dB.
        if (tpDb < -90.0)
            ++rmsSilenceCount[(size_t) c];
        else
            rmsSilenceCount[(size_t) c] = 0;

        if (rmsSilenceCount[(size_t) c] >= rmsSilenceResetN)
        {
            for (int cc = 0; cc < 2; ++cc)
            {
                rmsSqSum[cc] = 0.0;
                rmsWriteIdx[cc] = 0;
                rmsSilenceCount[cc] = 0;
                for (int i = 0; i < kCrestRmsWinMaxN; ++i)
                    rmsSqRing[cc][i] = 0.0;
            }
        }

        const double newSq = absS * absS;
        const int idx = rmsWriteIdx[(size_t) c];
        const double oldSq = rmsSqRing[(size_t) c][idx];

        rmsSqRing[(size_t) c][idx] = newSq;
        rmsSqSum[(size_t) c] += (newSq - oldSq);

        int nextIdx = idx + 1;
        if (nextIdx >= rmsWinN) nextIdx = 0;
        rmsWriteIdx[(size_t) c] = nextIdx;

        rmsSqSum[(size_t) c] = juce::jmax (0.0, rmsSqSum[(size_t) c]);

        const double rmsLin = std::sqrt (rmsSqSum[(size_t) c] / (double) rmsWinN);

        constexpr double eps = 1.0e-12;
        const double rmsDb = 20.0 * std::log10 (rmsLin + eps);
        const double crestDb = tpDb - rmsDb;

        const double w_cf = juce::jlimit (0.0, 1.0, (crestDb - 6.0) / 18.0);

        constexpr double kDensitySecBase = 0.090;
        const double densitySec = kDensitySecBase * (1.20 - 0.40 * bias01);
        const double aD = onePoleAlpha (densitySec, dt);
        const double densityInput = 1.0 - std::exp (-0.18 * attnTargetDb);
        eventDensityState[(size_t) c] = aD * eventDensityState[(size_t) c] + (1.0 - aD) * densityInput;
        const double density01 = juce::jlimit (0.0, 1.0, eventDensityState[(size_t) c]);

        constexpr double kMicroSecMin = 0.0030;
        constexpr double kMicroSecMax = 0.0600;

        const double hold01 = juce::jlimit (0.0, 1.0,
            (0.60 + 0.20 * (1.0 - bias01)) * sustained01 +
            (0.40 - 0.20 * (1.0 - bias01)) * density01);

        const double snap01 = w_cf;

        const double resp01 = juce::jlimit (0.0, 1.0,
            (0.45 + 0.35 * bias01) * snap01 +
            (0.55 - 0.35 * bias01) * (1.0 - hold01));

        const double microSec = kMicroSecMin + (kMicroSecMax - kMicroSecMin) * (1.0 - resp01);

        // Micro envelope (Phase 1.3): critically damped 2nd-order system (state-space), Forward Euler.
        // States: x1 = position-like (envelope dB), x2 = velocity-like (dB/s).
        double x1 = microStage1DbState[(size_t) c];
        double x2 = microStage2DbState[(size_t) c];

        // Overshoot prohibited (Phase 1.3 enforcement): fixed absolute epsilon in dB.
        constexpr double epsDb = 1.0e-9;

        const double yPrev = x1;
        const double xT = attnTargetDb;

        const double microSecBase = microSec;

        constexpr double kCoupleMin = 1.0;
        constexpr double kCoupleMax = 3.0;

        const bool isRelease = (xT < yPrev - epsDb);

        const double macroCurve = macro01 * macro01 * (10.0 + macro01 * (macro01 * (macro01 * 6.0 - 15.0)));
        const double couple = kCoupleMin + (kCoupleMax - kCoupleMin) * macroCurve;
        const double microSecEff = isRelease ? (microSecBase * couple) : microSecBase;

        const double omega0 = 1.0 / microSecEff;

        const double dx1_dt = x2;
        const double dx2_dt = (omega0 * omega0) * (xT - x1) - 2.0 * omega0 * x2;

        x1 += dt * dx1_dt;
        x2 += dt * dx2_dt;
        if (isRelease) x2 *= 0.98;

        // Enforce monotonic convergence without crossing the target (saturate to target only).
        double yNext = x1;
        if (std::abs (xT - yPrev) <= epsDb)
        {
            yNext = xT;
        }
        else if (xT > yPrev)
        {
            yNext = juce::jmin (yNext, xT);
        }
        else if (xT < yPrev)
        {
            yNext = juce::jmax (yNext, xT);
        }

        // Phase 1.4 — GR output state slew limiting (dB domain, time-consistent)
        // These are the real sonic targets — tune these numbers by ear later
        constexpr double maxAttackDbPerSec  = 18000.0;   // ~18,000 dB/s = very fast, catches most transients
        constexpr double maxReleaseDbPerSec = 1800.0;    // 10× slower = musical recovery, no pumping

        // Convert to per-sample limits using the actual time step (dt)
        const double derivNorm = juce::jlimit (0.0, 1.0, tpDerivLin[(size_t) c] / 0.5);
        const double attackBoost = 1.0 + 0.2 * derivNorm;

        const double attackScale = 0.85 + 0.30 * w_cf;
        const double maxAttackDb  = (maxAttackDbPerSec * attackScale * attackBoost) * dt;
        const double maxReleaseDb = maxReleaseDbPerSec * dt;

        // Apply asymmetric slew
        const double delta = yNext - yPrev;
        x1 = yPrev + juce::jlimit (-maxReleaseDb, maxAttackDb, delta);

        // Downstream clamp(s) (existing) apply after enforcement.
        x1 = juce::jlimit (0.0, kMaxAttnDb, x1);

        // Velocity clamp (safety): prevents runaway under extreme dt/tau conditions.
        constexpr double kMaxVelDbPerSec = 600.0;
        x2 = juce::jlimit (-kMaxVelDbPerSec, kMaxVelDbPerSec, x2);

        microStage1DbState[(size_t) c] = x1;
        microStage2DbState[(size_t) c] = x2;

        attnDbCh[(size_t) c] = x1;
    }

    // Note: detector/envelope is 2ch; additional channels are not part of the attenuation-link computation.

    constexpr double kLinkSmoothingTauSec = 0.007;
    const double aLink = onePoleAlpha (kLinkSmoothingTauSec, dt);
    lastLink01Smoothed = aLink * lastLink01Smoothed + (1.0 - aLink) * link01;
    const double link01Smooth = juce::jlimit (0.0, 1.0, lastLink01Smoothed);

    const double linkedDb = juce::jmax (attnDbCh[0], attnDbCh[1]);
    const double outDbL0 = (1.0 - link01Smooth) * attnDbCh[0] + link01Smooth * linkedDb;
    const double outDbR0 = (1.0 - link01Smooth) * attnDbCh[1] + link01Smooth * linkedDb;

    double outDbL = outDbL0;
    double outDbR = outDbR0;
    {
        auto smoothstep = [] (double x) noexcept -> double
        {
            x = juce::jlimit (0.0, 1.0, x);
            return (3.0 * x * x) - (2.0 * x * x * x);
        };

        // HF stress -> grScalar mapping (Phase 1.7)
        // Hard containment: prevent log-domain escape (NaN/Inf/negative).
        if (! std::isfinite (hfEnergyStereo) || hfEnergyStereo <= 0.0)
            hfEnergyStereo = 0.0;

        const double hfStress = 10.0 * log10Lookup (hfEnergyStereo + 1.0e-12);
        const double stressNorm = juce::jlimit (0.0, 1.0, (hfStress - (-30.0)) / 30.0);

        const double s = smoothstep (stressNorm);
        double grScalar = 1.0 + 0.25 * s;
        grScalar = juce::jlimit (1.0, 1.25, grScalar);

        // Level-dependent activation via GR average (Phase 1.7)
        const double grAbsDb = juce::jmax (outDbL, outDbR);
        const double aGr = expLookup (dt / 0.050);
        guardGrAvgDb = aGr * guardGrAvgDb + (1.0 - aGr) * grAbsDb;

        const double t = juce::jlimit (0.0, 1.0, (guardGrAvgDb - 6.0) / 12.0);
        const double activation = t * t * (t * (t * 6.0 - 15.0) + 10.0);

        double finalScalar = 1.0 + activation * (grScalar - 1.0);
        finalScalar = juce::jlimit (1.0, 1.25, finalScalar);

        // Final application (hard-fenced): broadband scaling of post-link GR only
        outDbL *= finalScalar;
        outDbR *= finalScalar;
    }

    outDbL = juce::jlimit (0.0, kMaxAttnDb, outDbL);
    outDbR = juce::jlimit (0.0, kMaxAttnDb, outDbR);

    constexpr double kTinyGrDb = 0.03;
    if (outDbL < kTinyGrDb) outDbL = 0.0;
    if (outDbR < kTinyGrDb) outDbR = 0.0;

    constexpr double kMaxSlewDbPerSec = 600.0;
    const double maxDeltaDb = kMaxSlewDbPerSec * dt;

    const double prevL = lastAppliedAttnDb[0];
    const double prevR = lastAppliedAttnDb[1];

    outDbL = juce::jlimit (prevL - maxDeltaDb, prevL + maxDeltaDb, outDbL);
    outDbR = juce::jlimit (prevR - maxDeltaDb, prevR + maxDeltaDb, outDbR);

    lastAppliedAttnDb[0] = outDbL;
    lastAppliedAttnDb[1] = outDbR;

    if (numCh >= 1 && std::isfinite (outDbL))
    {
        const double grThisDb = juce::jlimit (0.0, 120.0, outDbL);
        grHoldDb[0] = juce::jmax (grHoldDb[0], grThisDb);
    }

    if (numCh >= 2 && std::isfinite (outDbR))
    {
        const double grThisDb = juce::jlimit (0.0, 120.0, outDbR);
        grHoldDb[1] = juce::jmax (grHoldDb[1], grThisDb);
    }

    if (! std::isfinite (outDbL) || ! std::isfinite (outDbR))
        badMathThisBlock = true;

    const double grDbNeg = -juce::jmax (outDbL, outDbR);
    if (grDbNeg < grDbNegMin)
        grDbNegMin = grDbNeg;

    const float gL0 = (float) std::pow (10.0, -outDbL / 20.0);
    const float gR0 = (float) std::pow (10.0, -outDbR / 20.0);

    constexpr double kOutTauSec = 0.005;
    const float aOut = (float) onePoleAlpha (kOutTauSec, dt);
    const float gL = lastOutScalar[0] * aOut + gL0 * (1.0f - aOut);
    const float gR = lastOutScalar[1] * aOut + gR0 * (1.0f - aOut);
    lastOutScalar[0] = gL;
    lastOutScalar[1] = gR;

    constexpr float kEpsAbs = 1.0e-12f;
    constexpr float kSoftClipK = 1.25f;
    constexpr float kCeilingKnee = 0.035f;
    float ceilingLin = (float) std::pow (10.0, ceilingDb / 20.0);
    if (! std::isfinite ((double) ceilingLin) || ceilingLin <= 0.0f)
        ceilingLin = 0.0f;

    if (numCh >= 2)
    {
        float xL = chPtr[0][i];
        float xR = chPtr[1][i];
        if (! std::isfinite ((double) xL)) xL = 0.0f;
        if (! std::isfinite ((double) xR)) xR = 0.0f;

        float yL = xL * gL;
        float yR = xR * gR;

        if (ceilingLin > 0.0f)
        {
            const bool stereoLinked = (link01Smooth >= 0.5);

            if (stereoLinked)
            {
                const float aL = std::abs (yL);
                const float aR = std::abs (yR);
                const float aMax = juce::jmax (aL, aR);

                float gReqLinked = (aMax > kEpsAbs ? (ceilingLin / aMax) : 1.0f);
                if (! std::isfinite ((double) gReqLinked)) gReqLinked = 1.0f;
                gReqLinked = juce::jlimit (0.0f, 1.0f, gReqLinked);

                const float gCeilLinked = stepCeilingEnv (gReqLinked, ceilingGainStateLinked, ceilA_down, ceilA_up);
                yL *= gCeilLinked;
                yR *= gCeilLinked;
            }
            else
            {
                // L (unlinked) — keep existing per-channel ceiling behavior
                {
                    const float a = std::abs (yL);
                    float gReq = (a > kEpsAbs ? (ceilingLin / a) : 1.0f);
                    if (! std::isfinite ((double) gReq)) gReq = 1.0f;
                    gReq = juce::jlimit (0.0f, 1.0f, gReq);

                    const float gCeil = stepCeilingEnv (gReq, ceilingGainState[0], ceilA_down, ceilA_up);
                    yL *= gCeil;
                }

                // R (unlinked) — keep existing per-channel ceiling behavior
                {
                    const float a = std::abs (yR);
                    float gReq = (a > kEpsAbs ? (ceilingLin / a) : 1.0f);
                    if (! std::isfinite ((double) gReq)) gReq = 1.0f;
                    gReq = juce::jlimit (0.0f, 1.0f, gReq);

                    const float gCeil = stepCeilingEnv (gReq, ceilingGainState[1], ceilA_down, ceilA_up);
                    yR *= gCeil;
                }
            }

            // Post-ceiling softclip + hard margin (per-channel), unchanged behavior
            {
                const float u = yL / ceilingLin;
                const float a = std::abs (u);

                float w = (a - 1.0f) / kCeilingKnee;
                w = juce::jlimit (0.0f, 1.0f, w);
                w = w * w * (3.0f - 2.0f * w); // smoothstep

                const float ySat = ceilingLin * (u >= 0.0f ? 1.0f : -1.0f);
                yL = yL + w * (ySat - yL);
            }

            {
                const float u = yR / ceilingLin;
                const float a = std::abs (u);

                float w = (a - 1.0f) / kCeilingKnee;
                w = juce::jlimit (0.0f, 1.0f, w);
                w = w * w * (3.0f - 2.0f * w); // smoothstep

                const float ySat = ceilingLin * (u >= 0.0f ? 1.0f : -1.0f);
                yR = yR + w * (ySat - yR);
            }
        }

        if (! std::isfinite ((double) yL)) yL = 0.0f;
        if (! std::isfinite ((double) yR)) yR = 0.0f;
        chPtr[0][i] = yL;
        chPtr[1][i] = yR;
    }
    else if (numCh >= 1)
    {
        float x = chPtr[0][i];
        if (! std::isfinite ((double) x)) x = 0.0f;

        float y = x * gL;

        if (ceilingLin > 0.0f)
        {
            const float a = std::abs (y);
            float gReq = (a > kEpsAbs ? (ceilingLin / a) : 1.0f);
            if (! std::isfinite ((double) gReq)) gReq = 1.0f;
            gReq = juce::jlimit (0.0f, 1.0f, gReq);

            const float gCeil = stepCeilingEnv (gReq, ceilingGainState[0], ceilA_down, ceilA_up);
            y *= gCeil;

            const float u = y / ceilingLin;
            const float aU = std::abs (u);

            float w = (aU - 1.0f) / kCeilingKnee;
            w = juce::jlimit (0.0f, 1.0f, w);
            w = w * w * (3.0f - 2.0f * w); // smoothstep

            const float ySat = ceilingLin * (u >= 0.0f ? 1.0f : -1.0f);
            y = y + w * (ySat - y);

        }

        if (! std::isfinite ((double) y)) y = 0.0f;
        chPtr[0][i] = y;
    }

    for (int c = 2; c < numCh; ++c)
    {
        float x = chPtr[c][i];
        if (! std::isfinite ((double) x)) x = 0.0f;

        float y = x * gR;

        if (ceilingLin > 0.0f)
        {
            const float a = std::abs (y);
            float gReq = (a > kEpsAbs ? (ceilingLin / a) : 1.0f);
            if (! std::isfinite ((double) gReq)) gReq = 1.0f;
            gReq = juce::jlimit (0.0f, 1.0f, gReq);

            const float gCeil = stepCeilingEnv (gReq, ceilingGainState[1], ceilA_down, ceilA_up);
            y *= gCeil;

            const float u = y / ceilingLin;
            const float aU = std::abs (u);

            float w = (aU - 1.0f) / kCeilingKnee;
            w = juce::jlimit (0.0f, 1.0f, w);
            w = w * w * (3.0f - 2.0f * w); // smoothstep

            const float ySat = ceilingLin * (u >= 0.0f ? 1.0f : -1.0f);
            y = y + w * (ySat - y);

        }

        if (! std::isfinite ((double) y)) y = 0.0f;
        chPtr[c][i] = y;
    }

    // Step 4 — Compute Output peak + RMS (broadband), linear domain (no dB).
    // Guard: accumulators are 2ch; never index beyond [0..1].
    const int chProcOut = juce::jmin (2, numCh);
    for (int c = 0; c < chProcOut; ++c)
    {
        const double y = (double) chPtr[c][i];
        const double outAbs = std::abs (y);
        outPeakHold[(size_t) c] = juce::jmax (outPeakHold[(size_t) c], outAbs);
        outRmsSq[(size_t) c] += (outAbs * outAbs);
    }
}

// Phase 11 — Metering Plumbing: publishMeters (SPSC ring producer)
void CompassMasteringLimiterAudioProcessor::publishMeters (const MeterSnapshot& s) noexcept
{
    const uint32_t w = meterWriteIndex.load (std::memory_order_relaxed);

    meterRing[(size_t) w] = s;

    const uint32_t wNext = (w + 1u) % kMeterRingCapacity;
    const uint32_t r     = meterReadIndex.load (std::memory_order_relaxed);

    if (wNext == r)
    {
        const uint32_t rNext = (r + 1u) % kMeterRingCapacity;
        meterReadIndex.store (rNext, std::memory_order_relaxed);
    }

    meterWriteIndex.store (wNext, std::memory_order_relaxed);

    meterSeq.fetch_add (1u, std::memory_order_release);
}

bool CompassMasteringLimiterAudioProcessor::readMeters (MeterSnapshot& out) const noexcept
{
    const uint32_t seqNow  = meterSeq.load (std::memory_order_acquire);
    const uint32_t seqPrev = meterLastReadSeq.load (std::memory_order_relaxed);

    // If nothing has ever been published, report "no data".
    if (seqNow == 0u)
        return false;

    const uint32_t w = meterWriteIndex.load (std::memory_order_relaxed);
    const uint32_t idx = (w + kMeterRingCapacity - 1u) % kMeterRingCapacity;

    out = meterRing[(size_t) idx];

    // Only advance the "last read" marker when a new publish occurred.
    if (seqNow != seqPrev)
        meterLastReadSeq.store (seqNow, std::memory_order_relaxed);

    // Latest-snapshot policy: advance consumer index to keep producer's "ring full" test sane.
    meterReadIndex.store (w, std::memory_order_relaxed);

    return true;
}

void CompassMasteringLimiterAudioProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals denormGuard;

    // FTZ/DAZ always on (JUCE-supported): prevents denormal CPU spikes on silence/tails.
    juce::FloatVectorOperations::disableDenormalisedNumberSupport (true);

    // NaN/Inf containment helpers
    auto isBadF = [] (float x) noexcept { return ! std::isfinite (x); };
    auto isBadD = [] (double x) noexcept { return ! std::isfinite (x); };

    badMathThisBlock = false;

   #if JUCE_DEBUG
    // Debug asserts for internal state invariants
    jassert (std::isfinite (truePeakLin));
    jassert (std::isfinite (microStage1DbState[0]) && std::isfinite (microStage1DbState[1]));
    jassert (std::isfinite (microStage2DbState[0]) && std::isfinite (microStage2DbState[1]));
    jassert (std::isfinite (macroEnergyState[0])   && std::isfinite (macroEnergyState[1]));
    jassert (rmsWinN <= kCrestRmsWinMaxN);
    jassert (rmsSqSum[0] >= -1.0e-9 && rmsSqSum[1] >= -1.0e-9);
   #endif

    // Release containment: sanitize any bad carried state immediately (prevents propagation)
    if (isBadD (truePeakLin) ||
        isBadD (microStage1DbState[0]) || isBadD (microStage1DbState[1]) ||
        isBadD (microStage2DbState[0]) || isBadD (microStage2DbState[1]) ||
        isBadD (macroEnergyState[0])   || isBadD (macroEnergyState[1]))
    {
        truePeakLin = 0.0;
        microStage1DbState = { 0.0, 0.0 };
        microStage2DbState = { 0.0, 0.0 };
        macroEnergyState   = { 0.0, 0.0 };
        for (int c = 0; c < 2; ++c)
        {
            rmsSqSum[c] = 0.0;
            rmsWriteIdx[c] = 0;
            rmsSilenceCount[c] = 0;
            for (int i = 0; i < kCrestRmsWinMaxN; ++i)
                rmsSqRing[c][i] = 0.0;
        }
        eventDensityState  = { 0.0, 0.0 };
        guardLpState       = { 0.0, 0.0 };
        guardHpState2      = { 0.0, 0.0 };
        guardTotE          = { 0.0, 0.0 };
        guardHiE           = { 0.0, 0.0 };
        lowShelfZ1         = { 0.0, 0.0 };
        lowShelfZ1         = { 0.0, 0.0 };
        lastAppliedAttnDb  = { 0.0, 0.0 };
    }

    // Gate-3 deterministic transport semantics:
    // transport stop/start resets adaptive/envelope/guard states and is the only safe boundary for oversampling selection.
    if (auto* ph = getPlayHead())
    {
        juce::AudioPlayHead::CurrentPositionInfo pos;
        if (ph->getCurrentPosition (pos))
        {
            const bool playingNow = pos.isPlaying;
            if (! transportKnown)
            {
                transportKnown = true;
                lastTransportPlaying = playingNow;
            }
            else if (playingNow != lastTransportPlaying)
            {
                lastTransportPlaying = playingNow;

                const int ch = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
                reset (lastSampleRate, lastMaxBlock, ch);

                ceilingGainState[0] = 1.0f;
                ceilingGainState[1] = 1.0f;
                ceilingGainStateLinked = 1.0f;

                // Transport-safe boundary: latch oversampling selection (no allocations here).
                const int osMinIndexBoundary = (int) apvts->getRawParameterValue ("oversampling_min")->load();
                selectOversamplingAtBoundary (osMinIndexBoundary);
            }
        }
    }

    // Offline/nonrealtime transition is a transport-safe boundary for oversampling selection + latency.
    const bool nonRealtimeNow = isNonRealtime();
    if (nonRealtimeNow != lastNonRealtime)
    {
        lastNonRealtime = nonRealtimeNow;

        const int ch = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
        reset (lastSampleRate, lastMaxBlock, ch);

        ceilingGainState[0] = 1.0f;
        ceilingGainState[1] = 1.0f;
        ceilingGainStateLinked = 1.0f;

        const int osMinIndexBoundary = (int) apvts->getRawParameterValue ("oversampling_min")->load();
        selectOversamplingAtBoundary (osMinIndexBoundary);
    }

    // Phase 1.9 — Bypass crossfade target (wet keeps computing even when bypassed).
    const auto* bp = getBypassParameter();
    const bool bypassedNow = (bp != nullptr) && (bp->getValue() >= 0.5f);
    const float bypassTarget = (bypassedNow ? 0.0f : 1.0f);
    auto stepBypassMix = [&] () noexcept
    {
        constexpr float maxDelta = 0.01f;
        const float d = bypassTarget - bypassMix;
        const float step = juce::jlimit (-maxDelta, +maxDelta, d);
        bypassMix += step;
        bypassMix = juce::jlimit (0.0f, 1.0f, bypassMix);
    };

    // Gate-2 rule: read params once per block into locals (atomics -> locals).
    const float driveDbTarget   = apvts->getRawParameterValue ("drive")->load();
    const float ceilingDbTarget = apvts->getRawParameterValue ("ceiling")->load();
    const float bias01Target    = apvts->getRawParameterValue ("adaptive_bias")->load();
    const float link01Target    = apvts->getRawParameterValue ("stereo_link")->load();
    const int   osMinIndex      = (int) apvts->getRawParameterValue ("oversampling_min")->load();
    juce::ignoreUnused (osMinIndex);

    float trimDb = apvts->getRawParameterValue("trim")->load();
    float trimLin = std::pow(10.0f, trimDb / 20.0f);
    buffer.applyGain(trimLin);

    driveDbSmoothed.setTargetValue (driveDbTarget);
    ceilingDbSmoothed.setTargetValue (ceilingDbTarget);
    adaptiveBias01Smoothed.setTargetValue (bias01Target);
    stereoLink01Smoothed.setTargetValue (link01Target);

    const auto tBlockStart = juce::Time::getHighResolutionTicks();

    // Control-domain true peak measurement disabled in Phase0 (buffer-size CPU stress).
    // measureTruePeak (buffer);

    // Envelope System (Micro + Macro, coupled)
    // - All control-domain attenuation is computed in dB domain.
    // - Micro: critically damped 2nd-order follower of the instantaneous attenuation target (no overshoot).
    // - Macro: bounded energy accumulator influences micro recovery behavior (no competing control paths).
    // - Stereo linking occurs after per-channel envelope generation.
    {
        const int numCh = juce::jmin (getTotalNumInputChannels(), buffer.getNumChannels());
        const int n     = buffer.getNumSamples();
        if (numCh <= 0 || n <= 0)
            return;

        // Softplus controls (smooth, monotonic, branch-free)
        constexpr double kEpsLin    = 1.0e-12; // avoids log(0)
        constexpr double kSoftK     = 32.0;
        constexpr double kMaxAttnDb = 120.0;

        


        // Time constants (seconds). Names avoid forbidden terminology.
        // "Micro" is ultra-fast; "Macro" is slower density behavior.
        constexpr double kMicroSecBase = 0.0080; // 8 ms nominal
        constexpr double kMacroSecBase = 0.1200; // 120 ms nominal

        // Coupling strength: macro energy influences micro recovery speed.
        constexpr double kCouple = 1.25;

        // Oversampled audio-path processing (no allocations in audio thread).
        // Fallback to native-rate if scratch buffer is not prepared for this block size / channel count.
        const bool canOsAudio =
            (activeOversampler != nullptr) &&
            (workBufferFloat.getNumChannels() >= numCh) &&
            (workBufferFloat.getNumSamples()  >= n);

        if (canOsAudio)
        {
            // CPU fast-path: if this block cannot exceed Ceiling after Drive AND envelope state is at rest,
            // skip oversampling + heavy inner loop, but advance smoothers deterministically.
            bool skipOsFastPath = false;
            skipOsFastPath = false; // Phase 1.9: fast-path disabled (wet path always runs when canOsAudio)
            {
                // Only skip if envelope state is already effectively at rest (avoids dropping residual GR).
                constexpr double kRestDb = 1.0e-3;  // ~0.001 dB
                constexpr double kRestEn = 1.0e-6;  // tiny macro energy

                const bool envAtRest =
                    (std::abs (microStage2DbState[0]) <= kRestDb) &&
                    (std::abs (microStage2DbState[1]) <= kRestDb) &&
                    (std::abs (macroEnergyState[0])   <= kRestEn) &&
                    (std::abs (macroEnergyState[1])   <= kRestEn);

                if (envAtRest)
                {
                    float peak = 0.0f;
                    for (int c = 0; c < numCh; ++c)
                    {
                        const float* p = buffer.getReadPointer (c);
                        for (int i = 0; i < n; ++i)
                        {
                            const float a = std::abs (p[i]);
                            if (a > peak) peak = a;
                        }
                    }

                    // Conservative overshoot test using target values (no per-sample smoothing work).
                    // If even the block peak cannot exceed ceiling after Drive, attenuation is guaranteed 0.
                    constexpr double kEpsLinFast = 1.0e-12;
                    const double tpDbPeak   = 20.0 * std::log10 ((double) peak + kEpsLinFast);
                    const double driveDbT   = (double) driveDbSmoothed.getTargetValue();
                    const double ceilingDbT = (double) ceilingDbSmoothed.getTargetValue();

                    // Small negative margin keeps us safely on the "no GR" side.
                    if (((tpDbPeak + driveDbT) - ceilingDbT) <= -0.05)
                        skipOsFastPath = false; // Phase 1.9: fast-path disabled (prevent wet/dry discontinuity)
                }
            }

            if (! skipOsFastPath)
            {

            // Copy float -> double scratch (native-rate, length n)
            for (int c = 0; c < numCh; ++c)
            {
                const float* src = buffer.getReadPointer (c);
                float* dst = workBufferFloat.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                    dst[i] = src[i];
            }

            juce::dsp::AudioBlock<float> fullBlock (workBufferFloat);
            auto blockN = fullBlock.getSubBlock (0, (size_t) n);

            juce::ScopedNoDenormals innerNoDenormals;
            auto osBlock = activeOversampler->processSamplesUp (blockN);

            const int osCh = (int) osBlock.getNumChannels();
            const int osN  = (int) osBlock.getNumSamples();

            const int osFactor = juce::jmax (1, (int) activeOversampler->getOversamplingFactor());
            const double dtOS  = lastInvSampleRate / (double) osFactor;

            // Cache oversampled channel pointers (hot path, no allocations)
            constexpr int kOsChCacheMax = 8;
            std::array<float*, (size_t) kOsChCacheMax> osPtr {};
            const int osCached = juce::jmin (juce::jmin (numCh, osCh), kOsChCacheMax);
            for (int c = 0; c < osCached; ++c)
                osPtr[(size_t) c] = osBlock.getChannelPointer ((size_t) c);

            // Phase D: materialize a fixed pointer array for the sample helper (no allocation).
            std::array<float*, (size_t) kOsChCacheMax> osPtrArr {};
            const int numChEff = juce::jmin (juce::jmin (numCh, osCh), kOsChCacheMax);
            for (int c = 0; c < numChEff; ++c)
                osPtrArr[(size_t) c] = (c < osCached ? osPtr[(size_t) c] : osBlock.getChannelPointer ((size_t) c));

            double grDbNegMin = 0.0; // 0 dB (no reduction) down to -kMaxAttnDb
            const int tpCh = juce::jmin (2, numChEff);

            


            


            // Advance smoothers at native rate (one step per native sample), reuse values across osFactor sub-samples.
            for (int iN = 0; iN < n; ++iN)
            {
                const double driveDb   = (double) driveDbSmoothed.getNextValue();
                const double ceilingDb = (double) ceilingDbSmoothed.getNextValue();
                const double bias01    = juce::jlimit (0.0, 1.0, (double) adaptiveBias01Smoothed.getNextValue());
                const double link01    = juce::jlimit (0.0, 1.0, (double) stereoLink01Smoothed.getNextValue());

                for (int k = 0; k < osFactor; ++k)
                {
                    const int i = iN * osFactor + k;
                    if (i >= osN)
                        break;

                    for (int c = 0; c < tpCh; ++c)
                    {
                        const double a = std::abs ((double) osPtrArr[(size_t) c][i]);
                        if (std::isfinite (a))
                            inTpHold[(size_t) c] = juce::jmax (inTpHold[(size_t) c], a);
                    }

                    processOneSample (osPtrArr.data(), numChEff, i, dtOS, driveDb, ceilingDb, bias01, link01, grDbNegMin);

                    for (int c = 0; c < tpCh; ++c)
                    {
                        const double a = std::abs ((double) osPtrArr[(size_t) c][i]);
                        if (std::isfinite (a))
                            outTpHold[(size_t) c] = juce::jmax (outTpHold[(size_t) c], a);
                    }
                }
            }

            grDbForUI.store ((float) juce::jlimit (0.0, 120.0, -grDbNegMin), std::memory_order_relaxed);


            activeOversampler->processSamplesDown (blockN);

            // Copy wet -> output with Phase 1.9 bypass crossfade (dry = input buffer).
            for (int c = 0; c < numCh; ++c)
            {
                const float* src = workBufferFloat.getReadPointer (c); // wet
                float* dst = buffer.getWritePointer (c);              // contains dry until we overwrite
                for (int i = 0; i < n; ++i)
                {
                    stepBypassMix();
                    const float dry = dst[i];
                    const float wet = src[i];
                    dst[i] = bypassMix * wet + (1.0f - bypassMix) * dry;
                }
            }
            }
            else
            {
                // Deterministic advancement of smoothers (native-rate length n), with no audio-path work.
                driveDbSmoothed.skip (n);
                ceilingDbSmoothed.skip (n);
                adaptiveBias01Smoothed.skip (n);
                stereoLink01Smoothed.skip (n);

                // Deterministic bypass ramp advancement (native-rate length n), even when skipping audio-path work.
                for (int i = 0; i < n; ++i)
                    stepBypassMix();

                grDbForUI.store (0.0f, std::memory_order_relaxed);
            }
        }
        else
        {
            // Native-rate fallback (prior behavior). No allocations. Deterministic.
            // Phase 11 note: input TP is sample-peak here unless/until true-peak is enabled for this path; do not add a new OS path just for input TP.
            constexpr int kChCacheMax = 8;
            std::array<float*, (size_t) kChCacheMax> chPtr {};
            const int chCached = juce::jmin (numCh, kChCacheMax);
            for (int c = 0; c < chCached; ++c)
                chPtr[(size_t) c] = buffer.getWritePointer (c);

            // Phase D: materialize a fixed pointer array for the sample helper (no allocation).
            std::array<float*, (size_t) kChCacheMax> chPtrArr {};
            const int numChEff = juce::jmin (numCh, kChCacheMax);
            for (int c = 0; c < numChEff; ++c)
                chPtrArr[(size_t) c] = (c < chCached ? chPtr[(size_t) c] : buffer.getWritePointer (c));

            double grDbNegMin = 0.0; // 0 dB (no reduction) down to -kMaxAttnDb

            


            for (int i = 0; i < n; ++i)
            {
                stepBypassMix();

                const double driveDb   = (double) driveDbSmoothed.getNextValue();
                const double ceilingDb = (double) ceilingDbSmoothed.getNextValue();
                const double bias01    = juce::jlimit (0.0, 1.0, (double) adaptiveBias01Smoothed.getNextValue());
                const double link01    = juce::jlimit (0.0, 1.0, (double) stereoLink01Smoothed.getNextValue());

                constexpr int kChCacheMax = 8;
                std::array<float, (size_t) kChCacheMax> drySnap {};
                const int numChSnap = juce::jmin (numChEff, kChCacheMax);
                for (int c = 0; c < numChSnap; ++c)
                    drySnap[(size_t) c] = chPtrArr[(size_t) c][i];

                const int tpCh = juce::jmin (2, numChEff);
                for (int c = 0; c < tpCh; ++c)
                {
                    const double a = std::abs ((double) chPtrArr[(size_t) c][i]);
                    if (std::isfinite (a))
                        inTpHold[(size_t) c] = juce::jmax (inTpHold[(size_t) c], a);
                }

                processOneSample (chPtrArr.data(), numChEff, i, lastInvSampleRate, driveDb, ceilingDb, bias01, link01, grDbNegMin);

                // Phase 1.9 bypass blend: wet already computed into chPtrArr; drySnap preserves raw input for this sample.
                if (bypassMix < 1.0f)
                {
                    for (int c = 0; c < numChSnap; ++c)
                    {
                        const float wet = chPtrArr[(size_t) c][i];
                        const float dry = drySnap[(size_t) c];
                        chPtrArr[(size_t) c][i] = bypassMix * wet + (1.0f - bypassMix) * dry;
                    }
                }
            }

            grDbForUI.store ((float) juce::jlimit (0.0, 120.0, -grDbNegMin), std::memory_order_relaxed);
        }
    }

    // Loudness update (Phase 11): from final native-rate output buffer (post-DSP).
    // Deterministic, bounded, no allocations.
    if (meterPublishSamples > 0)
    {
        const int numCh = juce::jmin (2, buffer.getNumChannels());
        const int n = buffer.getNumSamples();

        for (int i = 0; i < n; ++i)
        {
            double e = 0.0;
            if (numCh >= 2)
            {
                const double y0 = (double) buffer.getSample (0, i);
                const double y1 = (double) buffer.getSample (1, i);
                e = 0.5 * (y0 * y0 + y1 * y1);
            }
            else if (numCh == 1)
            {
                const double y0 = (double) buffer.getSample (0, i);
                e = (y0 * y0);
            }

            if (! std::isfinite (e) || e < 0.0) e = 0.0;
            e = juce::jlimit (0.0, 1.0e12, e);

            lufsCurChunkE += e;
            lufsCurChunkN += 1;

            lufsIntSumE += e;
            lufsIntN    += 1u;

            if (lufsCurChunkN >= meterPublishSamples)
            {
                const double oldE = lufsChunkE[(size_t) lufsChunkWrite];
                if (lufsChunkFilled >= (uint32_t) kLufsShortChunks)
                    lufsShortSumE -= oldE;
                else
                    lufsChunkFilled += 1u;

                lufsChunkE[(size_t) lufsChunkWrite] = lufsCurChunkE;
                lufsShortSumE += lufsCurChunkE;

                lufsChunkWrite = (lufsChunkWrite + 1u) % (uint32_t) kLufsShortChunks;

                lufsCurChunkE = 0.0;
                lufsCurChunkN = 0;
            }
        }
    }

    // Meter publish cadence (~50 Hz). Publish latest holds (bounded), then reset holds.
    // Audio thread: must remain allocation-free and lock-free.
    if (meterPublishSamples > 0)
    {
        meterCountdown -= buffer.getNumSamples();
        while (meterCountdown <= 0)
        {
            MeterSnapshot s{};

            constexpr double kEps = 1.0e-12;

            for (int c = 0; c < 2; ++c)
            {
                double x = inPeakHold[c];
                if (! std::isfinite (x) || x < 0.0) x = 0.0;
                x = juce::jlimit (0.0, 1.0e6, x);
                double db = 20.0 * std::log10 (x + kEps);
                s.inPeakDb[c] = juce::jlimit (-120.0, 60.0, db);

                x = outPeakHold[c];
                if (! std::isfinite (x) || x < 0.0) x = 0.0;
                x = juce::jlimit (0.0, 1.0e6, x);
                db = 20.0 * std::log10 (x + kEps);
                s.outPeakDb[c] = juce::jlimit (-120.0, 60.0, db);

                x = inTpHold[c];
                if (! std::isfinite (x) || x < 0.0) x = 0.0;
                x = juce::jlimit (0.0, 1.0e6, x);
                db = 20.0 * std::log10 (x + kEps);
                s.inTpDb[c] = juce::jlimit (-120.0, 60.0, db);

                x = outTpHold[c];
                if (! std::isfinite (x) || x < 0.0) x = 0.0;
                x = juce::jlimit (0.0, 1.0e6, x);
                db = 20.0 * std::log10 (x + kEps);
                s.outTpDb[c] = juce::jlimit (-120.0, 60.0, db);

                x = grHoldDb[c];
                if (! std::isfinite (x) || x < 0.0) x = 0.0;
                s.grDb[c] = juce::jlimit (0.0, 120.0, x);
            }

            // Crest factor (broadband): stereo peak minus stereo RMS.
            // Stereo peak: max(L,R). Stereo RMS: max-energy channel (max sumSq) -> RMS dB.
            {
                const double win = (double) juce::jmax (1, meterPublishSamples);

                const double inPeakStereoDb  = juce::jmax (s.inPeakDb[0],  s.inPeakDb[1]);
                const double outPeakStereoDb = juce::jmax (s.outPeakDb[0], s.outPeakDb[1]);

                double inSumSqSel = juce::jmax (inRmsSq[0], inRmsSq[1]);
                if (! std::isfinite (inSumSqSel) || inSumSqSel < 0.0) inSumSqSel = 0.0;
                inSumSqSel = juce::jlimit (0.0, 1.0e12, inSumSqSel);
                double inRmsStereoDb = 10.0 * std::log10 ((inSumSqSel / win) + kEps);
                if (! std::isfinite (inRmsStereoDb)) inRmsStereoDb = -120.0;
                inRmsStereoDb = juce::jlimit (-120.0, 60.0, inRmsStereoDb);

                double outSumSqSel = juce::jmax (outRmsSq[0], outRmsSq[1]);
                if (! std::isfinite (outSumSqSel) || outSumSqSel < 0.0) outSumSqSel = 0.0;
                outSumSqSel = juce::jlimit (0.0, 1.0e12, outSumSqSel);
                double outRmsStereoDb = 10.0 * std::log10 ((outSumSqSel / win) + kEps);
                if (! std::isfinite (outRmsStereoDb)) outRmsStereoDb = -120.0;
                outRmsStereoDb = juce::jlimit (-120.0, 60.0, outRmsStereoDb);

                double crest = inPeakStereoDb - inRmsStereoDb;
                if (! std::isfinite (crest)) crest = 0.0;
                s.crestPreDb = juce::jlimit (-60.0, 120.0, crest);

                crest = outPeakStereoDb - outRmsStereoDb;
                if (! std::isfinite (crest)) crest = 0.0;
                s.crestPostDb = juce::jlimit (-60.0, 120.0, crest);
            }

            // Loudness (Phase 11): unweighted energy, deterministic. Bounded for UI sanity.
            {
                constexpr double kEps = 1.0e-18;
                constexpr double kOffset = -0.691; // LUFS-style offset (unweighted here by constitution)

                const double shortE = lufsShortSumE + lufsCurChunkE;
                const double shortN = (double) (lufsChunkFilled) * (double) juce::jmax (1, meterPublishSamples) + (double) lufsCurChunkN;
                double msShort = (shortN > 0.0 ? (shortE / shortN) : 0.0);
                if (! std::isfinite (msShort) || msShort < 0.0) msShort = 0.0;
                msShort = juce::jlimit (0.0, 1.0e12, msShort);

                double lufsShort = kOffset + 10.0 * std::log10 (msShort + kEps);
                if (! std::isfinite (lufsShort)) lufsShort = -120.0;
                s.lufsShortDb = juce::jlimit (-120.0, 60.0, lufsShort);

                const double intN = (double) lufsIntN;
                double msInt = (intN > 0.0 ? (lufsIntSumE / intN) : 0.0);
                if (! std::isfinite (msInt) || msInt < 0.0) msInt = 0.0;
                msInt = juce::jlimit (0.0, 1.0e12, msInt);

                double lufsInt = kOffset + 10.0 * std::log10 (msInt + kEps);
                if (! std::isfinite (lufsInt)) lufsInt = -120.0;
                s.lufsIntDb = juce::jlimit (-120.0, 60.0, lufsInt);
            }

            publishMeters (s);

            for (int c = 0; c < 2; ++c)
            {
                inPeakHold[c]  = 0.0;
                outPeakHold[c] = 0.0;
                inRmsSq[c]     = 0.0;
                outRmsSq[c]    = 0.0;
                inTpHold[c]    = 0.0;
                outTpHold[c]   = 0.0;
                grHoldDb[c]    = 0.0;
            }

            meterCountdown += meterPublishSamples;
        }
    }

    // CPU overload behavior: if we exceed a conservative share of block duration, enable assist briefly.
    {
        const auto tEnd = juce::Time::getHighResolutionTicks();
        const double elapsedSec = juce::Time::highResolutionTicksToSeconds (tEnd - tBlockStart);
        const int n = buffer.getNumSamples();
        const double blockSec = (lastSampleRate > 0.0 ? (double) n / lastSampleRate : 0.0);

        // If we're close to the deadline, disable guardrails extras for a short period.
        if (blockSec > 0.0 && elapsedSec > 0.85 * blockSec)
            overloadAssistBlocks = juce::jmax (overloadAssistBlocks, 64);

        if (overloadAssistBlocks > 0)
            --overloadAssistBlocks;
    }

    // NaN/Inf containment (release): if we tripped bad math, force safe output and reset state.
    if (badMathThisBlock)
    {
        // Force safe output (prevents explosions/ceiling risk); reset internal state so next block is stable.
        buffer.clear();

        truePeakLin = 0.0;
        microStage1DbState = { 0.0, 0.0 };
        microStage2DbState = { 0.0, 0.0 };
        macroEnergyState   = { 0.0, 0.0 };
        for (int c = 0; c < 2; ++c)
        {
            rmsSqSum[c] = 0.0;
            rmsWriteIdx[c] = 0;
            rmsSilenceCount[c] = 0;
            for (int i = 0; i < kCrestRmsWinMaxN; ++i)
                rmsSqRing[c][i] = 0.0;
        }
        eventDensityState  = { 0.0, 0.0 };
        guardLpState       = { 0.0, 0.0 };
        guardHpState2      = { 0.0, 0.0 };
        guardTotE          = { 0.0, 0.0 };
        guardHiE           = { 0.0, 0.0 };
        lastAppliedAttnDb  = { 0.0, 0.0 };

        grDbForUI.store (0.0f, std::memory_order_relaxed);
    }

    // Clear any output channels with no input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void CompassMasteringLimiterAudioProcessor::processBlockBypassed (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ignoreUnused (midi);
    juce::ScopedNoDenormals noDenormals;

    // True bypass: do not touch transport/smoothers/adaptive state.
    // Skeleton behavior: pass-through. Clear any output channels with no input.
    for (int ch = getTotalNumInputChannels(); ch < getTotalNumOutputChannels(); ++ch)
        buffer.clear (ch, 0, buffer.getNumSamples());
}

void CompassMasteringLimiterAudioProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    // Session reload must restore identical internal state.
    auto root = apvts->copyState().createXml();
    if (root == nullptr)
        root = std::make_unique<juce::XmlElement> ("PARAMS");

    auto* internal = root->createNewChildElement ("internal_state");
    internal->setAttribute ("transportKnown", (int) transportKnown);
    internal->setAttribute ("lastTransportPlaying", (int) lastTransportPlaying);

    copyXmlToBinary (*root, destData);
}

void CompassMasteringLimiterAudioProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    std::unique_ptr<juce::XmlElement> xml (getXmlFromBinary (data, sizeInBytes));
    if (xml == nullptr)
        return;

    if (xml->hasTagName (apvts->state.getType()))
        apvts->replaceState (juce::ValueTree::fromXml (*xml));
    else
        apvts->replaceState (juce::ValueTree::fromXml (*xml));

    if (auto* internal = xml->getChildByName ("internal_state"))
    {
        transportKnown = (internal->getIntAttribute ("transportKnown", 0) != 0);
        lastTransportPlaying = (internal->getIntAttribute ("lastTransportPlaying", 0) != 0);
    }
    else
    {
        transportKnown = false;
        lastTransportPlaying = false;
    }

    // Constitution: recall/reload must restore identical internal state.
    // Do not hard-reset adaptive/envelope/guard state here.
    // Sync smoothed parameters to loaded values (no ramp) to prevent discontinuities.
    const float driveDb   = apvts->getRawParameterValue ("drive")->load();
    const float ceilingDb = apvts->getRawParameterValue ("ceiling")->load();
    const float bias01    = apvts->getRawParameterValue ("adaptive_bias")->load();
    const float link01    = apvts->getRawParameterValue ("stereo_link")->load();

    driveDbSmoothed.setCurrentAndTargetValue (driveDb);
    ceilingDbSmoothed.setCurrentAndTargetValue (ceilingDb);
    adaptiveBias01Smoothed.setCurrentAndTargetValue (bias01);
    stereoLink01Smoothed.setCurrentAndTargetValue (link01);
}


void CompassMasteringLimiterAudioProcessor::prepareOversampling (int channels, int maxBlock)
{
    const int ch = juce::jmax (1, channels);
    const int mb = juce::jmax (1, maxBlock);

    // Prebuild 2x/4x/8x with FIR equiripple halfband filters (linear-phase).
    for (int i = 0; i < kOsCount; ++i)
    {
        const int stages = i + 1; // 1->2x, 2->4x, 3->8x
        oversamplers[(size_t) i] = std::make_unique<juce::dsp::Oversampling<float>>(
            ch,
            stages,
            juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple,
            true
        );

        oversamplers[(size_t) i]->initProcessing ((size_t) mb);
        oversamplers[(size_t) i]->reset();

        oversamplerLatencySamples[(size_t) i] = oversamplers[(size_t) i]->getLatencyInSamples();
    }

    // Scratch buffer for input conversion (double precision).
    workBufferFloat.setSize (ch, mb, false, false, true);

    // Default active oversampler is 2x until boundary latch selects otherwise.
    activeOversampler = oversamplers[0].get();
    setLatencySamples (oversamplerLatencySamples[0]);
}

void CompassMasteringLimiterAudioProcessor::selectOversamplingAtBoundary (int osMinIndex) noexcept
{
    const int idx = juce::jlimit (0, kOsCount - 1, osMinIndex);
    latchedOsMinIndex = idx;

    auto* os = oversamplers[(size_t) idx].get();
    if (os != nullptr)
    {
        activeOversampler = os;
        const int osFactor = juce::jmax (1, (int) activeOversampler->getOversamplingFactor());
        const double fsDet = lastSampleRate * (double) osFactor;

        // Phase 1.7 Priority 4: dynamic guardrail HPF cutoff at detector rate
        const double fsSafe = juce::jmax (1.0, fsDet);
        const double kGuardFcHz = fsSafe / 5.5;

        // Existing 2nd-order Butterworth HPF topology (same normalization), cutoff replaced with kGuardFcHz
        const double w0 = 2.0 * juce::MathConstants<double>::pi * kGuardFcHz / fsSafe;
        const double cw = std::cos (w0);
        const double sw = std::sin (w0);
        constexpr double Q = 0.7071067811865476;
        const double alpha = sw / (2.0 * Q);
        const double b0 = (1.0 + cw) * 0.5;
        const double b1 = -(1.0 + cw);
        const double b2 = (1.0 + cw) * 0.5;
        const double a0 = (1.0 + alpha);
        const double a1 = -(2.0 * cw);
        const double a2 = (1.0 - alpha);
        guardNb0 = b0 / a0;
        guardNb1 = b1 / a0;
        guardNb2 = b2 / a0;
        guardNa1 = a1 / a0;
        guardNa2 = a2 / a0;

        // Phase 1.7 Priority 4: 1st-order low-shelf post-compensation (+3 dB @ 200 Hz), detector-rate bilinear prewarp
        const double gainDb = 3.0;
        const double fc = 200.0;
        const double omega = 2.0 * fsSafe * std::tan (juce::MathConstants<double>::pi * fc / fsSafe);
        const double A = std::pow (10.0, gainDb / 40.0);
        const double b0s = (1.0 + A * omega) / (1.0 + omega);
        const double b1s = (A * omega - 1.0) / (1.0 + omega);
        const double a1s = (omega - 1.0) / (1.0 + omega);

        lowShelfB0 = b0s;
        lowShelfB1 = b1s;
        lowShelfA1 = a1s;

        setLatencySamples (oversamplerLatencySamples[(size_t) idx]);

        // Deterministic, transport-safe boundary behavior:
        // reset oversampler state only when latching selection (not per block).
        activeOversampler->reset();
    }
}

void CompassMasteringLimiterAudioProcessor::measureTruePeak (const juce::AudioBuffer<float>& buffer) noexcept
{
    truePeakLin = 0.0;

    const int ch = juce::jmin (2, buffer.getNumChannels());
    const int n  = buffer.getNumSamples();
    if (ch <= 0 || n <= 0)
        return;

    // If input is effectively silent, reset FIR history to prevent denormal accumulation
    // in FIR filter history (common crackle source under heavy attenuation / silence).
    constexpr float kSilenceLin = 1.0e-8f;

    float inPeak = 0.0f;
    for (int c = 0; c < ch; ++c)
    {
        const float* src = buffer.getReadPointer (c);
        for (int i = 0; i < n; ++i)
        {
            const float a = std::abs (src[i]);
            if (a > inPeak) inPeak = a;
        }
    }

    if (inPeak < kSilenceLin)
    {
        truePeakLin = 0.0;

        // Phase 1.2 Step 5 — silence soak (decay-only, no FIR work):
        // Preserve detector-owned sustained energy as a deterministic decay across this silent block,
        // while still clearing FIR/transient history to prevent denormal accumulation.
        const double dtDet = lastInvSampleRate / (double) kTpOSFactor;
        const double aS    = onePoleAlpha (kTpSustainedTauSec, dtDet);
        const int    nDet  = n * kTpOSFactor;
        const double aBlk  = std::pow (aS, (double) nDet);

        std::array<double, 2> eKeep { 0.0, 0.0 };
        for (int c = 0; c < 2; ++c)
        {
            double e = tpSustainedPowEma[(size_t) c];
            if (! std::isfinite (e) || e < 0.0) e = 0.0;
            eKeep[(size_t) c] = e * aBlk;
        }

        resetTruePeakDetector();

        for (int c = 0; c < 2; ++c)
            tpSustainedPowEma[(size_t) c] = eKeep[(size_t) c];

        return;
    }

    double tpCh[2] = { 0.0, 0.0 };

    // Phase 1.2 — Sustained energy accumulator (detector domain):
    // Update cadence: per reconstructed sample (4x).
    const double dtDet = lastInvSampleRate / (double) kTpOSFactor;
    const double aS    = onePoleAlpha (kTpSustainedTauSec, dtDet);

    // Polyphase FIR reconstruction (4x):
    // y[n*L + p] = sum_k h[p + L*k] * x[n - k]
    for (int c = 0; c < ch; ++c)
    {
        int pos = tpHistPos[(size_t) c];
        const float* src = buffer.getReadPointer (c);

        double localPeak = 0.0;

        for (int i = 0; i < n; ++i)
        {
            const double x = (double) src[i];

            tpHist[(size_t) c][(size_t) pos] = x;
            pos = (pos + 1) % kTpTaps;

            const int newest = pos - 1;

            for (int p = 0; p < kTpOSFactor; ++p)
            {
                double acc = 0.0;
                int k = 0;

                for (int t = p; t < kTpTaps; t += kTpOSFactor)
                {
                    int idx = newest - k;
                    if (idx < 0) idx += kTpTaps;

                    acc += kTpFIR[(size_t) t] * tpHist[(size_t) c][(size_t) idx];
                    ++k;
                }

                // Sustained energy (broadband power EMA), deterministic + stable:
                // finite containment only; no hot-loop clamps.
                const double accSafe = (std::isfinite (acc) ? acc : 0.0);
                const double pwr = accSafe * accSafe;
                tpSustainedPowEma[(size_t) c] = aS * tpSustainedPowEma[(size_t) c] + (1.0 - aS) * pwr;

                const double a = std::abs (accSafe);
                if (a > localPeak)
                    localPeak = a;
            }
        }

        tpHistPos[(size_t) c] = pos;

        tpCh[c] = localPeak;

        // Phase 1.2 Step 3 — transient event derivative (detector domain):
        // First-order temporal derivative of peak magnitude (rising edge only).
        tpDerivLin[(size_t) c] = tpCh[c] - prevTpChLin[(size_t) c];
        if (tpDerivLin[(size_t) c] < 0.0)
            tpDerivLin[(size_t) c] = 0.0;
        prevTpChLin[(size_t) c] = tpCh[c];

        // Meter accumulator storage (input true peak hold, linear).
        if (tpCh[c] > inTpHold[c])
            inTpHold[c] = tpCh[c];
    }

    // Preserve existing scalar: max across channels.
    truePeakLin = tpCh[0];
    if (tpCh[1] > truePeakLin)
        truePeakLin = tpCh[1];

    // Diagnostic-only safety: if near-silence, reset FIR history to rule out denormal accumulation.
    if (truePeakLin < 1.0e-5)
        resetTruePeakDetector();
}

juce::AudioProcessorEditor* CompassMasteringLimiterAudioProcessor::createEditor()
{
    return new CompassMasteringLimiterAudioProcessorEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new CompassMasteringLimiterAudioProcessor();
}
