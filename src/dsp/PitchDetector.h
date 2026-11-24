#pragma once

#include <array>
#include <cstdint>
#include <vector>

#include "dsp/Biquad.h"
#include "dsp/VoicingDetector.h"

namespace metamorfo::dsp
{

// One pitch analysis result. `time` is the absolute input sample index the
// analysis window is centred on.
struct PitchEstimate
{
    double time = 0.0;
    float f0 = 0.0f;           // Hz; 0 when unvoiced
    float aperiodicity = 1.0f; // YIN CMND at the chosen lag (0 = perfectly periodic)
    float rmsDb = -120.0f;
    float zcr = 0.0f;
    bool voiced = false;
};

// Monophonic f0 detector (SPEC §4.1).
//
//  * analysis signal: high-pass ~55 Hz, then low-passed and decimated to ~11-12 kHz
//  * YIN (difference function + cumulative mean normalisation + absolute threshold)
//    on the decimated signal finds the period coarsely and robustly
//  * the lag is refined on the full-rate signal around the coarse estimate with
//    parabolic interpolation (sub-sample precision: < 5 cents up to 1.2 kHz)
//  * a 5-point median in the log domain plus an octave-jump hysteresis remove
//    isolated octave errors; VoicingDetector decides voiced/unvoiced
//
// Window: two periods of the lowest f0 (60 Hz), so an estimate describes the
// signal centred maxPeriodSamples() in the past. Hop: 128 samples at 48 kHz
// (scaled with the sample rate). All buffers are allocated in prepare().
class PitchDetector
{
public:
    static constexpr float kMinF0 = 60.0f;
    static constexpr float kMaxF0 = 1200.0f;

    void prepare (double sampleRate);
    void reset() noexcept;

    // Feeds one mono input sample. Runs an analysis every hop.
    void push (float sample) noexcept;

    [[nodiscard]] const PitchEstimate& latest() const noexcept { return latest_; }

    // Most recent estimate whose centre is <= time (falls back to the oldest kept).
    [[nodiscard]] PitchEstimate estimateAt (double time) const noexcept;

    [[nodiscard]] int maxPeriodSamples() const noexcept { return maxPeriod_; }
    [[nodiscard]] int hopSamples() const noexcept { return hop_; }
    [[nodiscard]] std::int64_t samplesPushed() const noexcept { return pushed_; }
    [[nodiscard]] double sampleRate() const noexcept { return sampleRate_; }

private:
    void analyse() noexcept;
    float smoothF0 (float rawF0) noexcept;

    double sampleRate_ = 48000.0;
    int decimation_ = 4;
    double decimatedRate_ = 12000.0;
    int maxPeriod_ = 800;     // full-rate samples (sr / 60)
    int minPeriod_ = 40;      // full-rate samples (sr / 1200)
    int maxPeriodD_ = 200;    // decimated samples
    int minPeriodD_ = 10;
    int hop_ = 128;

    Biquad highPass_, lowPassA_, lowPassB_;

    // Rings (power-of-two sizes).
    std::vector<float> full_, decimated_;
    std::uint32_t fullMask_ = 0, decimatedMask_ = 0;
    std::uint32_t fullWrite_ = 0, decimatedWrite_ = 0;
    std::int64_t pushed_ = 0, decimatedCount_ = 0;

    // Scratch (analysis only).
    std::vector<float> scratchD_, scratchF_, diff_, cmnd_;

    VoicingDetector voicing_;

    std::array<float, 5> medianCents_ {};
    int medianCount_ = 0, medianWrite_ = 0;
    float stableCents_ = 0.0f;
    bool hasStable_ = false;
    int octaveJumpCount_ = 0;

    static constexpr int kHistory = 64;
    std::array<PitchEstimate, kHistory> history_ {};
    int historyWrite_ = 0, historyCount_ = 0;
    PitchEstimate latest_;
};

} // namespace metamorfo::dsp
