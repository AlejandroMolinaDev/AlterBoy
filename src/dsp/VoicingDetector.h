#pragma once

namespace metamorfo::dsp
{

// Voiced / unvoiced decision (SPEC §4.1) from three cues measured on the same
// analysis window: YIN aperiodicity, RMS level and zero-crossing rate. Uses
// hysteresis so the decision does not flicker. Real-time safe.
class VoicingDetector
{
public:
    struct Thresholds
    {
        float enterAperiodicity = 0.20f, exitAperiodicity = 0.35f;
        float enterRmsDb = -50.0f, exitRmsDb = -56.0f;
        float enterZcr = 0.20f, exitZcr = 0.30f; // zero crossings per sample (full rate)
    };

    void reset() noexcept { voiced_ = false; }
    void setThresholds (const Thresholds& t) noexcept { t_ = t; }

    bool update (float aperiodicity, float rmsDb, float zeroCrossingRate) noexcept
    {
        if (voiced_)
            voiced_ = ! (aperiodicity > t_.exitAperiodicity || rmsDb < t_.exitRmsDb || zeroCrossingRate > t_.exitZcr);
        else
            voiced_ = aperiodicity < t_.enterAperiodicity && rmsDb > t_.enterRmsDb && zeroCrossingRate < t_.enterZcr;
        return voiced_;
    }

    [[nodiscard]] bool isVoiced() const noexcept { return voiced_; }

private:
    Thresholds t_;
    bool voiced_ = false;
};

} // namespace metamorfo::dsp
