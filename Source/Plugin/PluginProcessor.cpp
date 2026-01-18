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
        "Drive",
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
    guardTotE    = { 0.0, 0.0 };
    guardHiE     = { 0.0, 0.0 };
    crestRmsSqState    = { 0.0, 0.0 };
    eventDensityState  = { 0.0, 0.0 };

    lastOutScalar = { 1.0f, 1.0f };
}

void CompassMasteringLimiterAudioProcessor::prepareToPlay (double sampleRate, int samplesPerBlock)
{
    const int ch = juce::jmax (getTotalNumInputChannels(), getTotalNumOutputChannels());
    reset (sampleRate, samplesPerBlock, ch);

    // Optional deterministic ring init (fixed-size, outside audio thread).
    // This is allowed here (prepareToPlay is not the audio callback boundary).
    meterRing.fill (MeterSnapshot {});

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

    // Spectral Guardrails: parallel measurement (no influence on envelope)
    constexpr double kGuardFcHz   = 4500.0;
    constexpr double kGuardTauSec = 0.040;
    const double gGuardLp = std::exp (-2.0 * juce::MathConstants<double>::pi * kGuardFcHz * dt);
    const double aGuardE  = onePoleAlpha (kGuardTauSec, dt);

    const int chProc = juce::jmin (2, numCh);
    double guardrailHf01 = 0.0;

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
            const double lp = gGuardLp * guardLpState[(size_t) c] + (1.0 - gGuardLp) * s;
            guardLpState[(size_t) c] = lp;
            const double hp = s - lp;

            const double s2  = s  * s;
            const double hp2 = hp * hp;

            guardTotE[(size_t) c] = aGuardE * guardTotE[(size_t) c] + (1.0 - aGuardE) * s2;
            guardHiE[(size_t) c]  = aGuardE * guardHiE[(size_t) c]  + (1.0 - aGuardE) * hp2;

            const double denom = guardTotE[(size_t) c] + 1.0e-18;
            const double hf = juce::jlimit (0.0, 1.0, guardHiE[(size_t) c] / denom);
            if (hf > guardrailHf01) guardrailHf01 = hf;
        }

        const double tpDb = 20.0 * std::log10 (std::abs (s) + kEpsLin);

        const double x = (tpDb + driveDb) - ceilingDb;

        const double z = kSoftK * x;
        const double softplus = z + std::log1p (std::exp (-std::abs (z)));
        double attnTargetDb = softplus / kSoftK;
        attnTargetDb = juce::jlimit (0.0, kMaxAttnDb, attnTargetDb);

        constexpr double kGrFloorDb    = 0.05;
        constexpr double kHysteresisDb = 0.08;

        double currentTarget = attnTargetDb;
        if (currentTarget < kGrFloorDb)
            currentTarget = 0.0;
        else if (currentTarget < lastAttnTargetDb[(size_t) c] + kHysteresisDb)
            currentTarget = lastAttnTargetDb[(size_t) c];

        lastAttnTargetDb[(size_t) c] = currentTarget;
        attnTargetDb = currentTarget;

        const double energyInput = juce::jmax (0.0, std::pow (10.0, attnTargetDb / 20.0) - 1.0);

        const double macroSec = kMacroSecBase * (1.20 - 0.40 * bias01);
        const double aM = std::exp (-dt / macroSec);

        const double Eprev = macroEnergyState[(size_t) c];
        const double u = energyInput;

        double Enext = aM * Eprev + (1.0 - aM) * u;

        Enext = juce::jlimit (juce::jmin (Eprev, u), juce::jmax (Eprev, u), Enext);
        Enext = juce::jmax (0.0, Enext);

        macroEnergyState[(size_t) c] = Enext;

        const double macro01 = macroEnergyState[(size_t) c] / (1.0 + macroEnergyState[(size_t) c]);
        const double sustained01 = juce::jlimit (0.0, 1.0, macro01);

        constexpr double kCrestRmsSecBase = 0.050;
        const double crestRmsSec = kCrestRmsSecBase * (1.15 - 0.30 * bias01);
        const double aCR = onePoleAlpha (crestRmsSec, dt);
        const double absS = std::abs (s);
        crestRmsSqState[(size_t) c] = aCR * crestRmsSqState[(size_t) c] + (1.0 - aCR) * (absS * absS);
        const double rmsLin = std::sqrt (juce::jmax (0.0, crestRmsSqState[(size_t) c]));
        const double crest = absS / (rmsLin + 1.0e-12);
        const double crest01 = juce::jlimit (0.0, 1.0, (crest - 1.0) / (crest + 1.0));

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

        const double snap01 = crest01;

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

        const double couple = kCoupleMin + (kCoupleMax - kCoupleMin) * macro01;
        const double microSecEff = isRelease ? (microSecBase * couple) : microSecBase;

        const double omega0 = 1.0 / microSecEff;

        const double dx1_dt = x2;
        const double dx2_dt = (omega0 * omega0) * (xT - x1) - 2.0 * omega0 * x2;

        x1 += dt * dx1_dt;
        x2 += dt * dx2_dt;

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

        // Phase 1.4 — GR output state slew limiting (dB domain, per-sample hard cap).
        // Canonical enforcement (spec-binding):
        // maxStepDb = 0.01
        // ySlewed = yPrev + jlimit(-maxStepDb, +maxStepDb, (yNext - yPrev))
        constexpr double maxStepDb = 0.01;
        x1 = yPrev + juce::jlimit (-maxStepDb, +maxStepDb, (yNext - yPrev));

        // Downstream clamp(s) (existing) apply after enforcement.
        x1 = juce::jlimit (0.0, kMaxAttnDb, x1);

        // Velocity clamp (safety): prevents runaway under extreme dt/tau conditions.
        constexpr double kMaxVelDbPerSec = 600.0;
        x2 = juce::jlimit (-kMaxVelDbPerSec, kMaxVelDbPerSec, x2);

        microStage1DbState[(size_t) c] = x1;
        microStage2DbState[(size_t) c] = x2;

        attnDbCh[(size_t) c] = x1;
    }

    if (numCh > 2)
    {
        for (int c = 2; c < numCh; ++c)
            attnDbCh[1] = juce::jmax (attnDbCh[1], attnDbCh[0]);
    }

    const double linkedDb = juce::jmax (attnDbCh[0], attnDbCh[1]);
    const double outDbL0 = (1.0 - link01) * attnDbCh[0] + link01 * linkedDb;
    const double outDbR0 = (1.0 - link01) * attnDbCh[1] + link01 * linkedDb;

    double outDbL = outDbL0;
    double outDbR = outDbR0;
    {
        const double grAbsDb = juce::jmax (outDbL, outDbR);

        constexpr double kGuardOnDb       = 3.0;
        constexpr double kGuardFullDb     = 12.0;
        constexpr double kGuardMaxExtraDb = 1.2;

        double t = 0.0;
        if (grAbsDb > kGuardOnDb)
            t = juce::jlimit (0.0, 1.0, (grAbsDb - kGuardOnDb) / (kGuardFullDb - kGuardOnDb));

        const double tSmooth = t * t * (3.0 - 2.0 * t);

        const double hf01 = juce::jlimit (0.0, 1.0, guardrailHf01);
        const double guardDb = juce::jlimit (0.0, kGuardMaxExtraDb, tSmooth * hf01 * kGuardMaxExtraDb);

        outDbL += guardDb;
        outDbR += guardDb;
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
    float ceilingLin = (float) std::pow (10.0, ceilingDb / 20.0);
    if (! std::isfinite ((double) ceilingLin) || ceilingLin <= 0.0f)
        ceilingLin = 0.0f;

    if (numCh >= 1)
    {
        float x = chPtr[0][i];
        if (! std::isfinite ((double) x)) x = 0.0f;

        const float a = std::abs (x);
        const float gCeil = (a > kEpsAbs ? (ceilingLin / a) : 1.0e12f);
        const float gApplied = (gL < gCeil ? gL : gCeil);

        float y = x * gApplied;
        if (! std::isfinite ((double) y)) y = 0.0f;
        chPtr[0][i] = y;
    }

    if (numCh >= 2)
    {
        float x = chPtr[1][i];
        if (! std::isfinite ((double) x)) x = 0.0f;

        const float a = std::abs (x);
        const float gCeil = (a > kEpsAbs ? (ceilingLin / a) : 1.0e12f);
        const float gApplied = (gR < gCeil ? gR : gCeil);

        float y = x * gApplied;
        if (! std::isfinite ((double) y)) y = 0.0f;
        chPtr[1][i] = y;
    }

    for (int c = 2; c < numCh; ++c)
    {
        float x = chPtr[c][i];
        if (! std::isfinite ((double) x)) x = 0.0f;

        const float a = std::abs (x);
        const float gCeil = (a > kEpsAbs ? (ceilingLin / a) : 1.0e12f);
        const float gApplied = (gR < gCeil ? gR : gCeil);

        float y = x * gApplied;
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
        crestRmsSqState    = { 0.0, 0.0 };
        eventDensityState  = { 0.0, 0.0 };
        guardLpState       = { 0.0, 0.0 };
        guardTotE          = { 0.0, 0.0 };
        guardHiE           = { 0.0, 0.0 };
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

                // Transport-safe boundary: latch oversampling selection (no allocations here).
                const int osMinIndexBoundary = (int) apvts->getRawParameterValue ("oversampling_min")->load();
                selectOversamplingAtBoundary (osMinIndexBoundary);
            }
        }
    }

    // Gate-2 rule: read params once per block into locals (atomics -> locals).
    const float driveDbTarget   = apvts->getRawParameterValue ("drive")->load();
    const float ceilingDbTarget = apvts->getRawParameterValue ("ceiling")->load();
    const float bias01Target    = apvts->getRawParameterValue ("adaptive_bias")->load();
    const float link01Target    = apvts->getRawParameterValue ("stereo_link")->load();
    const int   osMinIndex      = (int) apvts->getRawParameterValue ("oversampling_min")->load();
    juce::ignoreUnused (osMinIndex);

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
                        skipOsFastPath = true;
                }
            }

            skipOsFastPath = false;

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

            // Copy double -> float output (native-rate)
            for (int c = 0; c < numCh; ++c)
            {
                const float* src = workBufferFloat.getReadPointer (c);
                float* dst = buffer.getWritePointer (c);
                for (int i = 0; i < n; ++i)
                    dst[i] = (float) src[i];
            }
            }
            else
            {
                // Deterministic advancement of smoothers (native-rate length n), with no audio-path work.
                driveDbSmoothed.skip (n);
                ceilingDbSmoothed.skip (n);
                adaptiveBias01Smoothed.skip (n);
                stereoLink01Smoothed.skip (n);

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
                const double driveDb   = (double) driveDbSmoothed.getNextValue();
                const double ceilingDb = (double) ceilingDbSmoothed.getNextValue();
                const double bias01    = juce::jlimit (0.0, 1.0, (double) adaptiveBias01Smoothed.getNextValue());
                const double link01    = juce::jlimit (0.0, 1.0, (double) stereoLink01Smoothed.getNextValue());

                const int tpCh = juce::jmin (2, numChEff);
                for (int c = 0; c < tpCh; ++c)
                {
                    const double a = std::abs ((double) chPtrArr[(size_t) c][i]);
                    if (std::isfinite (a))
                        inTpHold[(size_t) c] = juce::jmax (inTpHold[(size_t) c], a);
                }

                processOneSample (chPtrArr.data(), numChEff, i, lastInvSampleRate, driveDb, ceilingDb, bias01, link01, grDbNegMin);
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
        crestRmsSqState    = { 0.0, 0.0 };
        eventDensityState  = { 0.0, 0.0 };
        guardLpState       = { 0.0, 0.0 };
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
