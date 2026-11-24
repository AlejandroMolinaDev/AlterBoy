#pragma once

#include <algorithm>
#include <array>
#include <cmath>

#include "dsp/Biquad.h"

namespace metamorfo::dsp
{

// 2x polyphase IIR half-band oversampler (two chains of first-order all-pass
// sections). Coefficients: 8-section elliptic half-band design (transition band
// 0.04, ~ -99 dB stop band) computed with the Valenzuela-Constantinides method;
// verified numerically (see docs/ROADMAP.md, phase 5).
class HalfBand2x
{
public:
    static constexpr int kSections = 8;
    static constexpr std::array<double, kSections> kCoefficients {
        0.040633460924, 0.150505129023, 0.300757055992, 0.460774504961,
        0.609524314896, 0.738503841119, 0.849223810392, 0.949742783705
    };
    // Low-frequency group delay of up + down, rounded (base-rate samples).
    static constexpr int kLatency = 3;

    void reset() noexcept
    {
        for (auto* path : { &up_, &down_ })
            for (auto& p : *path)
                p = {};
    }

    // One base-rate sample in, two 2x-rate samples out.
    void upsample (float x, float& even, float& odd) noexcept
    {
        even = static_cast<float> (runPath (up_, 0, x));
        odd = static_cast<float> (runPath (up_, 1, x));
    }

    // Two 2x-rate samples in, one base-rate sample out.
    float downsample (float even, float odd) noexcept
    {
        return static_cast<float> (0.5 * (runPath (down_, 0, odd) + runPath (down_, 1, even)));
    }

private:
    struct AllPass { double x1 = 0.0, y1 = 0.0; };
    using Chains = std::array<AllPass, kSections>; // even indices: path 0, odd: path 1

    static double runPath (Chains& chains, int path, double x) noexcept
    {
        for (int i = path; i < kSections; i += 2)
        {
            auto& s = chains[static_cast<std::size_t> (i)];
            const double a = kCoefficients[static_cast<std::size_t> (i)];
            const double y = a * x + s.x1 - a * s.y1;
            s.x1 = x;
            s.y1 = y;
            x = y;
        }
        return x;
    }

    Chains up_ {}, down_ {};
};

// Triode-style saturation (SPEC §4.3). Original model:
//  * soft pre-emphasis (+3 dB high shelf at 1.5 kHz) before, matching de-emphasis after
//  * input gain 0..+30 dB (2.5 dB per DRIVE step)
//  * asymmetric transfer curve, softer on the positive side, with a bias that grows
//    with the signal envelope -> even harmonics dominate (H2 > H3 by >= 15 dB from
//    -24 to -3 dBFS, DRIVE 3..12, in the design sweep)
//  * runs at 2x through HalfBand2x; DC blocker; adaptive RMS level compensation so
//    turning DRIVE up adds character, not loudness (within +-1.5 dB)
//  * DRIVE 0 returns the input delayed by latencySamples(), bit-exact
class TubeDrive
{
public:
    static constexpr int kLatency = HalfBand2x::kLatency;

    void prepare (double sampleRate) noexcept
    {
        sampleRate_ = sampleRate;
        preEmphasis_.setHighShelf1 (sampleRate, 1500.0, 3.0);
        deEmphasis_.setHighShelf1 (sampleRate, 1500.0, -3.0);
        dcBlocker_.setHighPass (sampleRate, 12.0);
        const double rate2x = 2.0 * sampleRate;
        envAttack_ = static_cast<float> (std::exp (-1.0 / (0.005 * rate2x)));
        envRelease_ = static_cast<float> (std::exp (-1.0 / (0.08 * rate2x)));
        rmsCoeff_ = static_cast<float> (std::exp (-1.0 / (0.2 * sampleRate)));
        gainCoeff_ = static_cast<float> (std::exp (-1.0 / (0.05 * sampleRate)));
        reset();
    }

    void reset() noexcept
    {
        delay_.fill (0.0f);
        delayIndex_ = 0;
        resetShaper();
    }

    // `drive` 0..12 (already smoothed by the caller).
    float process (float x, float drive) noexcept
    {
        // Exactly delayed input (aligned with the oversampled path).
        const float delayed = delay_[static_cast<std::size_t> (delayIndex_)];
        delay_[static_cast<std::size_t> (delayIndex_)] = x;
        delayIndex_ = (delayIndex_ + 1) % kLatency;

        if (drive <= 0.0f)
        {
            // Bypassed: no CPU. The effect fades in over the first half DRIVE step when it
            // is engaged again, so the cold filter state cannot click.
            if (engaged_)
                resetShaper(); // the delay line is untouched: the bypass stays bit-exact
            return delayed;
        }
        engaged_ = true;

        const float wet = runShaper (x, drive);

        // Adaptive level compensation (output RMS follows the input RMS).
        msIn_ = rmsCoeff_ * msIn_ + (1.0f - rmsCoeff_) * delayed * delayed;
        msOut_ = rmsCoeff_ * msOut_ + (1.0f - rmsCoeff_) * wet * wet;
        if (msIn_ > 1.0e-9f && msOut_ > 1.0e-12f)
        {
            // The shaper divides by its input gain (up to +30 dB), so the make-up range must cover it.
            const float target = std::clamp (std::sqrt (msIn_ / msOut_), 0.05f, 48.0f);
            compensation_ = gainCoeff_ * compensation_ + (1.0f - gainCoeff_) * target;
        }

        // Fade the effect in over the first half DRIVE step: DRIVE ~0 is transparent.
        const float amount = std::min (1.0f, drive * 2.0f);
        return delayed + amount * (wet * compensation_ - delayed);
    }

private:
    void resetShaper() noexcept
    {
        oversampler_.reset();
        preEmphasis_.reset();
        deEmphasis_.reset();
        dcBlocker_.reset();
        engaged_ = false;
        envelope_ = 0.0f;
        msIn_ = msOut_ = 0.0f;
        compensation_ = 1.0f;
    }

    float runShaper (float x, float drive) noexcept
    {
        const float gain = std::pow (10.0f, drive * 2.5f / 20.0f);
        float even = 0.0f, odd = 0.0f;
        oversampler_.upsample (preEmphasis_.process (x), even, odd);
        even = shape (even * gain) / gain;
        odd = shape (odd * gain) / gain;
        return dcBlocker_.process (deEmphasis_.process (oversampler_.downsample (even, odd)));
    }

    float shape (float u) noexcept
    {
        const float level = std::abs (u);
        envelope_ = level > envelope_ ? envAttack_ * envelope_ + (1.0f - envAttack_) * level
                                      : envRelease_ * envelope_ + (1.0f - envRelease_) * level;
        const float bias = 0.2f + 0.5f * std::min (envelope_, 8.0f);
        const float z = u + bias;
        constexpr float asym = 0.45f; // harder cut-off on the negative side
        const float fz = z >= 0.0f ? fastTanh (z) : fastTanh (asym * z) / asym;
        return fz - fastTanh (bias);
    }

    // Rational tanh approximation (Lambert continued fraction, 7/6), monotonic and
    // clamped to +-1: accurate to ~1e-4 for |x| < 3, cheap enough for 2x-rate use.
    static float fastTanh (float x) noexcept
    {
        const float c = std::clamp (x, -4.97f, 4.97f);
        const float x2 = c * c;
        const float num = c * (135135.0f + x2 * (17325.0f + x2 * (378.0f + x2)));
        const float den = 135135.0f + x2 * (62370.0f + x2 * (3150.0f + x2 * 28.0f));
        return std::clamp (num / den, -1.0f, 1.0f);
    }

    double sampleRate_ = 48000.0;
    HalfBand2x oversampler_;
    Biquad preEmphasis_, deEmphasis_, dcBlocker_;
    std::array<float, kLatency> delay_ {};
    int delayIndex_ = 0;
    bool engaged_ = false;
    float envelope_ = 0.0f, envAttack_ = 0.0f, envRelease_ = 0.0f;
    float msIn_ = 0.0f, msOut_ = 0.0f, rmsCoeff_ = 0.0f;
    float compensation_ = 1.0f, gainCoeff_ = 0.0f;
};

} // namespace metamorfo::dsp
