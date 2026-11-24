#pragma once

#include <cmath>

namespace metamorfo::dsp
{

// Second-order IIR section (transposed direct form II), RBJ cookbook designs.
// Real-time safe; no allocation.
class Biquad
{
public:
    void reset() noexcept { z1_ = z2_ = 0.0; }

    void setLowPass (double sampleRate, double frequency, double q = 0.70710678) noexcept
    {
        const auto [cosw, alpha] = prewarp (sampleRate, frequency, q);
        setNormalised ((1 - cosw) / 2, 1 - cosw, (1 - cosw) / 2, 1 + alpha, -2 * cosw, 1 - alpha);
    }

    void setHighPass (double sampleRate, double frequency, double q = 0.70710678) noexcept
    {
        const auto [cosw, alpha] = prewarp (sampleRate, frequency, q);
        setNormalised ((1 + cosw) / 2, -(1 + cosw), (1 + cosw) / 2, 1 + alpha, -2 * cosw, 1 - alpha);
    }

    // First-order shelf expressed as a biquad (b2 = a2 = 0). gainDb > 0 boosts highs.
    void setHighShelf1 (double sampleRate, double frequency, double gainDb) noexcept
    {
        // Bilinear transform of H(s) = (g*s + w) / (s + w): DC gain 1, HF gain g.
        const double g = std::pow (10.0, gainDb / 20.0);
        const double k = std::tan (kPi * frequency / sampleRate);
        setNormalised (g + k, k - g, 0.0, 1.0 + k, k - 1.0, 0.0);
    }

    float process (float x) noexcept
    {
        const double in = x;
        const double y = b0_ * in + z1_;
        z1_ = b1_ * in - a1_ * y + z2_;
        z2_ = b2_ * in - a2_ * y;
        return static_cast<float> (y);
    }

    // Magnitude response at `frequency` (used by tests).
    double magnitudeAt (double sampleRate, double frequency) const noexcept
    {
        const double w = 2.0 * kPi * frequency / sampleRate;
        const double cr = std::cos (w), ci = -std::sin (w), c2r = std::cos (2 * w), c2i = -std::sin (2 * w);
        const double nr = b0_ + b1_ * cr + b2_ * c2r, ni = b1_ * ci + b2_ * c2i;
        const double dr = 1.0 + a1_ * cr + a2_ * c2r, di = a1_ * ci + a2_ * c2i;
        return std::sqrt ((nr * nr + ni * ni) / (dr * dr + di * di));
    }

private:
    static constexpr double kPi = 3.14159265358979323846;

    struct Prewarp { double cosw, alpha; };
    static Prewarp prewarp (double sampleRate, double frequency, double q) noexcept
    {
        const double w = 2.0 * kPi * frequency / sampleRate;
        return { std::cos (w), std::sin (w) / (2.0 * q) };
    }

    void setNormalised (double b0, double b1, double b2, double a0, double a1, double a2) noexcept
    {
        b0_ = b0 / a0; b1_ = b1 / a0; b2_ = b2 / a0; a1_ = a1 / a0; a2_ = a2 / a0;
    }

    double b0_ = 1, b1_ = 0, b2_ = 0, a1_ = 0, a2_ = 0;
    double z1_ = 0, z2_ = 0;
};

} // namespace metamorfo::dsp
