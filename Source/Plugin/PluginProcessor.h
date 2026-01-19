#pragma once
#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_dsp/juce_dsp.h>
#include <array>
#include <memory>
#include <atomic>
#include <cstdint>

class CompassMasteringLimiterAudioProcessor final : public juce::AudioProcessor
{
public:
    using APVTS = juce::AudioProcessorValueTreeState;

    CompassMasteringLimiterAudioProcessor();
    ~CompassMasteringLimiterAudioProcessor() override = default;

    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    void processBlockBypassed (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Compass Mastering Limiter"; }
    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock&) override;
    void setStateInformation (const void*, int) override;

    APVTS& getAPVTS() noexcept { return *apvts; }
    const APVTS& getAPVTS() const noexcept { return *apvts; }

    float getCurrentGRDb() const noexcept { return grDbForUI.load(std::memory_order_relaxed); }

    bool getCurrentTruePeakDbTP (float& inDbTP, float& outDbTP) const noexcept
    {
        MeterSnapshot s{};
        if (! readMeters (s))
            return false;

        const double inStereo  = juce::jmax (s.inTpDb[0],  s.inTpDb[1]);
        const double outStereo = juce::jmax (s.outTpDb[0], s.outTpDb[1]);

        const double inClamped  = juce::jlimit (-120.0, 60.0, inStereo);
        const double outClamped = juce::jlimit (-120.0, 60.0, outStereo);

        inDbTP  = (float) inClamped;
        outDbTP = (float) outClamped;
        return true;
    }

    bool getCurrentLufsDb (float& lufsS, float& lufsI) const noexcept
    {
        MeterSnapshot s{};
        if (! readMeters (s))
            return false;

        const double sClamped = juce::jlimit (-120.0, 60.0, s.lufsShortDb);
        const double iClamped = juce::jlimit (-120.0, 60.0, s.lufsIntDb);

        lufsS = (float) sClamped;
        lufsI = (float) iClamped;
        return true;
    }

    bool getCurrentPeakDbFS (float& inDbFS, float& outDbFS) const noexcept
    {
        MeterSnapshot s{};
        if (! readMeters (s))
            return false;

        const double inStereo  = juce::jmax (s.inPeakDb[0],  s.inPeakDb[1]);
        const double outStereo = juce::jmax (s.outPeakDb[0], s.outPeakDb[1]);

        const double inClamped  = juce::jlimit (-120.0, 60.0, inStereo);
        const double outClamped = juce::jlimit (-120.0, 60.0, outStereo);

        inDbFS  = (float) inClamped;
        outDbFS = (float) outClamped;
        return true;
    }

    float getCurrentCeilingDbTP() const noexcept
    {
        const float v = apvts->getRawParameterValue ("ceiling")->load();
        return (float) juce::jlimit (-120.0f, 60.0f, v);
    }

    bool getCurrentClampGlue01 (float& outClamp01, float& outGlue01) const noexcept
    {
        MeterSnapshot s{};
        if (! readMeters (s))
            return false;

        const float c = juce::jmax (s.clamp01[0], s.clamp01[1]);
        const float g = juce::jmax (s.glue01[0],  s.glue01[1]);

        outClamp01 = juce::jlimit (0.0f, 1.0f, c);
        outGlue01  = juce::jlimit (0.0f, 1.0f, g);
        return true;
    }

    // Phase 1.4 — deterministic probes (non-realtime; callable from tests/debug harness)
    double probeSettleTimeSec (double sampleRate) const noexcept;
    bool probeContinuityFastAutomation (double sampleRate, double& outMaxAbsDeltaDb) const noexcept;

private:
    static APVTS::ParameterLayout createParameterLayout();

    // Meter snapshot (POD, numeric-only). UI may consume via future plumbing.
    // Step 1.1: declare the data structure only (no instances, no publication mechanism yet).
    struct MeterSnapshot final
    {
        double   inPeakDb[2]   { 0.0, 0.0 };
        double   outPeakDb[2]  { 0.0, 0.0 };

        double   inTpDb[2]     { 0.0, 0.0 };
        double   outTpDb[2]    { 0.0, 0.0 };

        double   grDb[2]       { 0.0, 0.0 }; // attenuation magnitude in dB (0..+)
        double   lufsShortDb   = 0.0;
        double   lufsIntDb     = 0.0;

        double   crestPreDb    = 0.0;
        double   crestPostDb   = 0.0;

        float   clamp01[2]     { 0.0f, 0.0f };
        float   glue01[2]      { 1.0f, 1.0f };

        uint64_t frameCounter  = 0; // monotonic debug-only counter
    };

    // Meter publication primitive (SPSC ring, lock-free; declarations only in this step).
    // Producer: audio thread. Consumer: UI thread.
    //
    // Publication rules:
    // - Audio thread writes at a fixed cadence (e.g., every 256 or 512 samples).
    // - If ring is full, new writes overwrite the oldest unread snapshot (consumer may miss frames).
    // - UI reads opportunistically; missed frames are acceptable.
    // - Snapshot is immutable once published (writer writes slot fully, then advances write index).
    //
    // Indices are modulo kMeterRingCapacity.
    static constexpr uint32_t kMeterRingCapacity = 128; // fixed, no allocations
    std::array<MeterSnapshot, (size_t) kMeterRingCapacity> meterRing {};

    // writeIndex: next slot the producer will publish.
    // readIndex:  next slot the consumer will read (advanced by consumer; may be forced forward on overwrite).
    // meterSeq:   publication signal (incremented by producer AFTER a snapshot is fully written and published).
    std::atomic<uint32_t> meterWriteIndex { 0u };
    mutable std::atomic<uint32_t> meterReadIndex  { 0u };
    std::atomic<uint32_t> meterSeq        { 0u }; // wraparound OK

    // Consumer-side tracking for readMeters(): updated only by UI thread.
    // Atomic is used defensively; keep usage contained to metering plumbing only.
    mutable std::atomic<uint32_t> meterLastReadSeq { 0u };

    void publishMeters (const MeterSnapshot& s) noexcept;
    bool readMeters (MeterSnapshot& out) const noexcept;

    // Meter accumulators (double precision) — Step 3.1 (storage only; no meter math/publishing yet)
    double inPeakHold[2]  = { 0.0, 0.0 };
    double outPeakHold[2] = { 0.0, 0.0 };
    double inRmsSq[2]     = { 0.0, 0.0 };
    double outRmsSq[2]    = { 0.0, 0.0 };
    double inTpHold[2]    = { 0.0, 0.0 };
    double outTpHold[2]   = { 0.0, 0.0 };
    double grHoldDb[2]    = { 0.0, 0.0 }; // attenuation magnitude in dB (0..+)

    // Meter publish timing (members) — configured/used in later steps
    int    meterPublishSamples = 0;
    int    meterCountdown      = 0;
    double meterDt             = 0.0;

    // Loudness state (Phase 11): deterministic, bounded, allocation-free.
    // Short-term is a fixed 3.0 s window implemented as 150 chunks at the meter cadence (~50 Hz).
    // Integrated is a running mean-square accumulator.
    static constexpr int kLufsHz          = 50;
    static constexpr int kLufsShortSec    = 3;
    static constexpr int kLufsShortChunks = kLufsHz * kLufsShortSec; // 150 (fixed, no allocations)

    std::array<double, (size_t) kLufsShortChunks> lufsChunkE {};
    uint32_t lufsChunkWrite  = 0u;
    uint32_t lufsChunkFilled = 0u;
    double   lufsShortSumE   = 0.0;

    double   lufsCurChunkE   = 0.0;
    int      lufsCurChunkN   = 0;

    double   lufsIntSumE     = 0.0;
    uint64_t lufsIntN        = 0u;

    // Gate-3 deterministic state model:
    void reset (double sampleRate, int maxBlock, int channels) noexcept;

    // Oversampling + True Peak (Gate-4):
    // - FIR polyphase only (linear-phase reconstruction)
    // - Oversampling is prebuilt in prepareToPlay (no allocations in audio thread)
    // - Active oversampling selection changes only on transport stop/start edge
    void prepareOversampling (int channels, int maxBlock);
    void selectOversamplingAtBoundary (int osMinIndex) noexcept;
    void measureTruePeak (const juce::AudioBuffer<float>& buffer) noexcept;

    // Phase D: promoted helpers (no allocations, no virtual dispatch)
    static double onePoleAlpha (double tauSec, double dtSec) noexcept;

    // Phase 1.x — Hot-path exp/log acceleration (tables precomputed in prepareToPlay; no lazy init)
    static constexpr int    kExpTableSize = 8192;
    static constexpr double kExpMaxX      = 0.1;   // x = dt/tau domain clamp

    static constexpr int    kLogTableSize = 4096;
    static constexpr double kLogMin       = -12.0; // log10(y) min
    static constexpr double kLogRange     = 18.0;  // [-12, +6]

    std::array<double, (size_t) kExpTableSize> expNegTable {};
    std::array<double, (size_t) kLogTableSize> log10Table  {};

    double expLookup (double x) const noexcept;     // returns exp(-x) using expNegTable
    double log10Lookup (double y) const noexcept;   // returns log10(y) using log10Table (indexing may use std::log10)

    void processOneSample (float* const* chPtr,
                          int numCh,
                          int i,
                          double dt,
                          double driveDb,
                          double ceilingDb,
                          double bias01,
                          double link01,
                          double& grDbNegMin) noexcept;

    // Gate-2 smoothing policy (declared now; configured in prepareToPlay/reset):
    // - Drive/Ceiling: sample-accurate linear ramps (SmoothedValue)
    // - Stereo Link / Adaptive Bias: smoothed (SmoothedValue)
    // - Oversampling Min: discrete choice (no smoothing)
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> driveDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> ceilingDbSmoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> adaptiveBias01Smoothed;
    juce::SmoothedValue<float, juce::ValueSmoothingTypes::Linear> stereoLink01Smoothed;

    // Deterministic lifecycle context (captured in prepareToPlay/reset):
    double lastSampleRate = 0.0;
    double lastInvSampleRate = 0.0; // cached 1 / lastSampleRate (SR-derived coeff scalar)
    double effectiveControlInvDt = 0.0; // 1.0 / nativeSR
    double effectiveAudioInvDt   = 0.0; // 1.0 / (nativeSR * OSFactor); OSFactor=1.0 for now
    int lastMaxBlock = 0;
    int lastChannels = 0;

    // Transport edge detection (stop/start triggers reset semantics):
    bool transportKnown = false;
    bool lastTransportPlaying = false;

    // Offline/nonrealtime edge detection (transition triggers boundary semantics):
    bool lastNonRealtime = false;

    // Oversampling (prebuilt instances; selected at transport-safe boundary only)
    static constexpr int kOsCount = 3; // 2x / 4x / 8x
    std::array<std::unique_ptr<juce::dsp::Oversampling<float>>, kOsCount> oversamplers;
    juce::dsp::Oversampling<float>* activeOversampler = nullptr;
    std::array<int, kOsCount> oversamplerLatencySamples { 0, 0, 0 };
    int latchedOsMinIndex = 0; // 0=2x, 1=4x, 2=8x (latched only at boundary)

    // Control-domain true peak (linear)
    double truePeakLin = 0.0;

    // Phase 1.2 — True-Peak Detector Path (Reference)
    // Polyphase FIR oversampled reconstruction (linear-phase, deterministic coefficients).
    // Implemented as 4x interpolation; coefficients are symmetric (linear-phase) and normalized so sum(h) == 4.0.
    static constexpr int kTpOSFactor = 4;
    static constexpr int kTpTaps     = 32; // must be even and divisible by kTpOSFactor

    static constexpr std::array<double, kTpTaps> kTpFIR {
        -0.000000000000000000e+00,
        -8.291365655703771638e-04,
        -3.525756966746731828e-03,
        -3.488316484777266926e-03,
         6.579729475753049242e-03,
         2.634752184268256830e-02,
         4.035166818965605501e-02,
         2.428673766138198312e-02,
        -3.407969929110992585e-02,
        -1.130051644440947245e-01,
        -1.538132119698306655e-01,
        -8.715260485310091787e-02,
         1.222467629501392816e-01,
         4.403366958529203457e-01,
         7.651370491704410082e-01,
         9.706077254322565961e-01,
         9.706077254322565961e-01,
         7.651370491704410082e-01,
         4.403366958529203457e-01,
         1.222467629501392955e-01,
        -8.715260485310093175e-02,
        -1.538132119698306377e-01,
        -1.130051644440948078e-01,
        -3.407969929110995361e-02,
         2.428673766138198312e-02,
         4.035166818965605501e-02,
         2.634752184268256830e-02,
         6.579729475753050977e-03,
        -3.488316484777266926e-03,
        -3.525756966746731828e-03,
        -8.291365655703861626e-04,
        -0.000000000000000000e+00
    };

    // Per-channel FIR history (preallocated; 2-channel accumulator contract).
    std::array<std::array<double, kTpTaps>, 2> tpHist{};
    std::array<int, 2> tpHistPos{};

    // Detector-domain transient feature:
    // First-order temporal derivative of peak magnitude (linear), per channel, per block.
    std::array<double, 2> prevTpChLin { 0.0, 0.0 };
    std::array<double, 2> tpDerivLin  { 0.0, 0.0 };

    // Phase 1.2 — Sustained energy accumulator (detector domain):
    // Broadband power EMA of reconstructed samples (double), per channel.
    // Decay law is locked: a = exp(-dt/tau) via onePoleAlpha(tau, dt).
    static constexpr double kTpSustainedTauSec = 0.040;
    std::array<double, 2> tpSustainedPowEma { 0.0, 0.0 };

    void resetTruePeakDetector() noexcept
    {
        for (int c = 0; c < 2; ++c)
        {
            tpHist[(size_t) c].fill (0.0);
            tpHistPos[(size_t) c] = 0;

            prevTpChLin[(size_t) c] = 0.0;
            tpDerivLin[(size_t) c]  = 0.0;
            tpSustainedPowEma[(size_t) c] = 0.0;
        }
    }

    // UI meter readout (GR in dB, negative: 0..-60). Published once per block (atomic, lock-free).
    std::atomic<float> grDbForUI { 0.0f };


    // Output scalar continuity (tiny, deterministic) — prevents micro-steps reaching the output
    std::array<float, 2> lastOutScalar { 1.0f, 1.0f };

    // Phase 1.9 — Bypass crossfade (sample-accurate, no resets on bypass edges)
    float bypassMix = 1.0f; // 1=wet, 0=dry

    // Phase 1.9 — Silence-horizon bounded memory (per-channel sample counter)
    std::array<int, 2> silenceCountSamples { 0, 0 };

    // Phase 1.6 — Stereo link transition smoothing (7 ms one-pole on control only)
    double lastLink01Smoothed = 1.0;

    // Guards + Safety Rails (Gate-10):
    // - Hard bounds: attenuation depth already clamped via kMaxAttnDb in processBlock
    // - Slew limiting: prevents extreme step changes from bad automation or math glitches
    std::array<double, 2> lastAppliedAttnDb { 0.0, 0.0 };

    // Tiny GR floor + hysteresis memory (per-channel) to prevent threshold chatter (observational behavior unchanged)
    std::array<double, 2> lastAttnTargetDb { 0.0, 0.0 };

    // CPU overload behavior: temporarily disable non-essential measurement extras (never changes user settings)
    int overloadAssistBlocks = 0;

    // NaN/Inf containment latch (release): if tripped, block output is forced safe and internal state resets
    bool badMathThisBlock = false;

    // Envelope system (Micro + Macro, coupled)
    // Micro: critically damped 2nd-order model (no overshoot; ultra-fast capture; no ringing)
    // Discrete-time implementation: two cascaded one-pole followers (stable for any dt; no stiffness).
    // Macro: energy accumulation + exponential decay (bounded; monotonic)
    // Coupling: macro state influences micro recovery behavior (no competing control paths)
    std::array<double, 2> microStage1DbState { 0.0, 0.0 };
    std::array<double, 2> microStage2DbState { 0.0, 0.0 };
    std::array<double, 2> macroEnergyState   { 0.0, 0.0 };

    // Gate: Adaptive Release inputs (deterministic, bounded, continuous)
    // - Crest factor proxy: 50 ms rectangular moving-average RMS^2 ring (SR_max=192 kHz => N_max=9600)
    // - Event density: continuous "event presence" accumulator (per-channel)
    static constexpr int kCrestRmsWinMaxN = 9600; // ceil(0.050 * 192000) = 9600
    double rmsSqRing[2][kCrestRmsWinMaxN] {};
    double rmsSqSum[2] { 0.0, 0.0 };
    int    rmsWriteIdx[2] { 0, 0 };
    int    rmsWinN = 1; // runtime: ceil(0.050*sampleRate), clamped to [1, kCrestRmsWinMaxN]
    int    rmsSilenceCount[2] { 0, 0 };
    int    rmsSilenceResetN = 1; // runtime: ceil(0.100*sampleRate)
    bool   invalidConfig = false; // set true if sampleRate > 192000.0
    std::array<double, 2> eventDensityState  { 0.0, 0.0 };

    // Spectral Guardrails (measurement-only; broadband application)
    // Parallel measurement path:
    // - frequency-selective measurement allowed
    // - must not influence detector/envelope timing/shape
    // - may only add subtle broadband GR under heavy limiting
    std::array<double, 2> guardLpState  { 0.0, 0.0 }; // one-pole LP state for split
    std::array<double, 2> guardHpState2 { 0.0, 0.0 }; // reserved state slot (2ch contract)
    std::array<double, 2> guardTotE    { 0.0, 0.0 }; // total energy EMA
    std::array<double, 2> guardHiE     { 0.0, 0.0 }; // high-band energy EMA
    double guardNb0 = 0.0, guardNb1 = 0.0, guardNb2 = 0.0, guardNa1 = 0.0, guardNa2 = 0.0;

    // Guardrail measurement compensation (Phase 1.7 Priority 4): 1st-order low-shelf (+3 dB @ 200 Hz), measurement path only
    double lowShelfB0 = 1.0, lowShelfB1 = 0.0, lowShelfA1 = 0.0; // identity defaults until boundary compute
    std::array<double, 2> lowShelfZ1 { 0.0, 0.0 };

    // GR average (Phase 1.7 activation): 50 ms one-pole EMA of grAbsDb (global, not per-channel)
    double guardGrAvgDb = 0.0;

    // Scratch buffer for oversampled measurement (double precision)
    juce::AudioBuffer<float> workBufferFloat;

    std::unique_ptr<APVTS> apvts;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (CompassMasteringLimiterAudioProcessor)
};
