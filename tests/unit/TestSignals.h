#pragma once

// Synthetic test signals for the C++ tests (mirror of tools/analysis/signals.py):
// sines, noise and a "synthetic voice" (Rosenberg glottal pulse + 4 formant resonators).

#include <cmath>
#include <cstdint>
#include <vector>

namespace metamorfo::test
{
inline constexpr double kPi = 3.14159265358979323846;

inline double midiToHz (double note) { return 440.0 * std::pow (2.0, (note - 69.0) / 12.0); }

inline double centsBetween (double measured, double reference) { return 1200.0 * std::log2 (measured / reference); }

inline std::vector<float> sine (double freq, double seconds, double sr, float amp = 0.5f)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * sr));
    for (std::size_t i = 0; i < x.size(); ++i)
        x[i] = amp * static_cast<float> (std::sin (2.0 * kPi * freq * static_cast<double> (i) / sr));
    return x;
}

inline std::vector<float> whiteNoise (double seconds, double sr, float amp = 0.3f, std::uint32_t seed = 1)
{
    std::vector<float> x (static_cast<std::size_t> (seconds * sr));
    std::uint32_t s = seed;
    for (auto& v : x)
    {
        s = s * 1664525u + 1013904223u;
        v = amp * (static_cast<float> (s >> 8) / 8388608.0f - 1.0f);
    }
    return x;
}

// Steady vowel /a/ at `f0` (optionally with vibrato in cents).
inline std::vector<float> syntheticVoice (double f0, double seconds, double sr, double vibratoCents = 0.0,
                                          double vibratoHz = 5.5, float amp = 0.5f)
{
    const std::size_t n = static_cast<std::size_t> (seconds * sr);
    std::vector<double> flowDerivative (n);
    double phase = 0.0, previousFlow = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / sr;
        const double f = f0 * std::pow (2.0, vibratoCents / 1200.0 * std::sin (2.0 * kPi * vibratoHz * t));
        phase += f / sr;
        phase -= std::floor (phase);
        constexpr double open = 0.6, close = 0.25;
        double flow = 0.0;
        if (phase < open)
            flow = 0.5 * (1.0 - std::cos (kPi * phase / open));
        else if (phase < open + close)
            flow = std::cos (kPi * (phase - open) / (2.0 * close));
        flowDerivative[i] = flow - previousFlow;
        previousFlow = flow;
    }

    struct Formant { double freq, bandwidth; };
    const Formant formants[] = { { 730, 80 }, { 1090, 90 }, { 2440, 120 }, { 3400, 130 } };
    std::vector<double> y (n, 0.0);
    double gain = 1.0;
    for (const auto& fm : formants)
    {
        const double r = std::exp (-kPi * fm.bandwidth / sr);
        const double a1 = -2.0 * r * std::cos (2.0 * kPi * fm.freq / sr), a2 = r * r, b0 = 1.0 - r;
        double y1 = 0.0, y2 = 0.0;
        for (std::size_t i = 0; i < n; ++i)
        {
            const double v = b0 * flowDerivative[i] - a1 * y1 - a2 * y2;
            y2 = y1;
            y1 = v;
            y[i] += gain * v;
        }
        gain *= 0.8;
    }

    double peak = 1.0e-12;
    for (const double v : y)
        peak = std::max (peak, std::abs (v));
    std::vector<float> out (n);
    for (std::size_t i = 0; i < n; ++i)
        out[i] = static_cast<float> (amp * y[i] / peak);
    return out;
}

} // namespace metamorfo::test
