#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dsp/ModeController.h"
#include "dsp/PitchDetector.h"

namespace metamorfo::dsp
{

// Real-time TD-PSOLA pitch + formant engine (SPEC §4.2).
//
//  * Analysis marks are placed one local period apart, using the detector's f0
//    (unvoiced regions use a fixed pseudo-period so they are reconstructed, not shifted).
//  * Synthesis marks are spaced by analysis period / pitch ratio. Each synthesis mark
//    takes the grain around the nearest available analysis mark: a Hann window over
//    +/- one analysis period, RESAMPLED by the formant ratio (formant shift
//    independent of pitch). With Link on, formant ratio = pitch ratio (tape-like shift).
//  * Grains are overlap-added with window-sum normalisation.
//  * Fixed latency: 2 x the longest period (60 Hz) -> 33.3 ms at any sample rate.
//  * Identity settings cross-fade to the exactly delayed input (bit-exact transparency).
//
// All memory is allocated in prepare(); processSample() is real-time safe.
class PsolaEngine
{
public:
    struct Controls
    {
        ModeController::Settings mode;
        float formantSemitones = 0.0f; // -12..+12
        bool link = false;
        float retuneMs = 20.0f;        // smoothing of the pitch ratio between grains
    };

    void prepare (double sampleRate, int maxPeriodSamples);
    void reset() noexcept;

    [[nodiscard]] int latencySamples() const noexcept { return latency_; }

    // Pushes one mono input sample (the detector must already have been fed the same
    // sample) and returns the PSOLA output delayed by latencySamples().
    float processSample (float input, const PitchDetector& detector, const Controls& controls) noexcept;

    // 1 when the settings are neutral: the caller then uses its exactly delayed input
    // instead of the PSOLA output (cross-faded over 20 ms), per channel.
    [[nodiscard]] float identityGain() const noexcept { return identityGain_; }

    // For the display: last applied pitch ratio and the f0 it was applied to.
    [[nodiscard]] float currentRatio() const noexcept { return lastRatio_; }
    [[nodiscard]] float currentInputF0() const noexcept { return lastF0_; }
    [[nodiscard]] bool currentVoiced() const noexcept { return lastVoiced_; }

private:
    struct Mark
    {
        double time = 0.0;
        double period = 0.0;
        float f0 = 0.0f;
        bool voiced = false;
    };

    void generateAnalysisMarks (const PitchDetector& detector) noexcept;
    [[nodiscard]] double refineMark (double previous, double predicted, double period) const noexcept;
    void generateSynthesisMarks (const Controls& controls) noexcept;
    void addGrain (const Mark& analysis, double synthesisTime, float formantRatio) noexcept;
    [[nodiscard]] float readInput (double position) const noexcept;
    [[nodiscard]] const Mark* nearestMark (double time) const noexcept;

    double sampleRate_ = 48000.0;
    int maxPeriod_ = 800;
    int latency_ = 1600;
    double pseudoPeriod_ = 320.0;

    std::vector<float> input_, acc_, wsum_;
    std::uint32_t inputMask_ = 0, outputMask_ = 0;
    std::int64_t now_ = 0;           // samples pushed so far
    std::int64_t lastEmitted_ = -1;  // last output time emitted

    static constexpr int kMarks = 64;
    std::array<Mark, kMarks> marks_ {};
    int markWrite_ = 0, markCount_ = 0;
    double nextAnalysis_ = 0.0;
    double nextSynthesis_ = 0.0;

    ModeController modeController_;
    double logRatio_ = 0.0;      // smoothed log2 pitch ratio
    double logFormant_ = 0.0;    // smoothed log2 formant ratio
    float identityGain_ = 1.0f;  // 1 = exact delayed input
    float identityStep_ = 0.001f;

    float lastRatio_ = 1.0f, lastF0_ = 0.0f;
    bool lastVoiced_ = false;
};

} // namespace metamorfo::dsp
