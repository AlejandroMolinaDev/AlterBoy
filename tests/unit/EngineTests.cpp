// Pitch / formant engine through the full processor (SPEC §7): transpose accuracy,
// formant shift with stable f0, Link, Mix 0 % null, voiced/unvoiced transitions.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "dsp/PitchDetector.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"
#include "TestSignals.h"

using metamorfo::MetamorfoProcessor;
using namespace metamorfo::test;
namespace id = metamorfo::params::id;

namespace
{
constexpr double kRate = 48000.0;

struct Rendered
{
    std::vector<float> out; // channel 0, latency removed
    int latency = 0;
};

Rendered render (const std::vector<float>& input, const std::map<std::string, float>& params, int block = 256)
{
    MetamorfoProcessor p;
    for (const auto& [name, value] : params)
    {
        auto* param = p.getParameters().getParameter (name);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), block, kRate));

    Rendered r;
    r.latency = p.getLatencySamples();
    const int total = static_cast<int> (input.size()) + r.latency;
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (total));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<std::size_t> (pos + i);
            const float x = idx < input.size() ? input[idx] : 0.0f;
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
            out.push_back (buffer.getSample (0, i));
    }
    r.out.assign (out.begin() + r.latency, out.end());
    return r;
}

// Median f0 of the voiced estimates between `from` and `to` seconds.
double medianF0 (const std::vector<float>& x, double from = 0.3, double to = 1.0e9)
{
    metamorfo::dsp::PitchDetector d;
    d.prepare (kRate);
    std::vector<double> f0s;
    double lastTime = -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        d.push (x[i]);
        const auto& e = d.latest();
        const double t = e.time / kRate;
        if (e.time != lastTime && t > from && t < to && e.voiced)
            f0s.push_back (e.f0);
        lastTime = e.time;
    }
    if (f0s.empty())
        return 0.0;
    std::nth_element (f0s.begin(), f0s.begin() + static_cast<long> (f0s.size() / 2), f0s.end());
    return f0s[f0s.size() / 2];
}

// Spectral centroid (Hann window, DFT on a log-spaced grid of bins, 80 Hz - 8 kHz).
double centroid (const std::vector<float>& x, double fromSeconds = 0.4, int length = 8192)
{
    const auto start = static_cast<std::size_t> (fromSeconds * kRate);
    double num = 0.0, den = 0.0;
    for (double f = 80.0; f < 8000.0; f *= 1.01)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < length; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * n / (length - 1));
            const double v = w * x[start + static_cast<std::size_t> (n)];
            const double ph = 2.0 * kPi * f * n / kRate;
            re += v * std::cos (ph);
            im -= v * std::sin (ph);
        }
        const double mag = std::sqrt (re * re + im * im);
        num += f * mag * f; // weight by bin width (log grid: width ~ f)
        den += mag * f;
    }
    return num / den;
}

// Largest second difference relative to the signal's peak (click detector).
double clickiness (const std::vector<float>& x, std::size_t from)
{
    double peak = 1.0e-9, worst = 0.0;
    for (std::size_t i = from + 2; i < x.size(); ++i)
    {
        peak = std::max (peak, static_cast<double> (std::abs (x[i])));
        worst = std::max (worst, static_cast<double> (std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2])));
    }
    return worst / peak;
}
} // namespace

TEST_CASE ("Transpose: +7 st on a sine (SPEC §7)", "[engine][transpose]")
{
    const auto r = render (sine (220.0, 1.5, kRate), { { id::pitch, 7.0f } });
    const double expected = 220.0 * std::pow (2.0, 7.0 / 12.0);
    const double f = medianF0 (r.out);
    INFO ("measured " << f << " expected " << expected);
    CHECK (std::abs (centsBetween (f, expected)) < 5.0);
}

TEST_CASE ("Transpose: synthetic voice +-1, +-7, +-12 st", "[engine][transpose]")
{
    const auto voice = syntheticVoice (220.0, 1.5, kRate);
    for (const float st : { 1.0f, -1.0f, 7.0f, -7.0f, 12.0f, -12.0f })
    {
        const auto r = render (voice, { { id::pitch, st } });
        const double expected = 220.0 * std::pow (2.0, st / 12.0);
        const double f = medianF0 (r.out);
        INFO ("pitch " << st << " measured " << f << " expected " << expected);
        CHECK (std::abs (centsBetween (f, expected)) < 5.0);
    }
}

TEST_CASE ("Formant +-6 with pitch 0: f0 unchanged, centroid moves (SPEC §7)", "[engine][formant]")
{
    const auto voice = syntheticVoice (220.0, 1.5, kRate);
    const double base = centroid (render (voice, {}).out);
    for (const float fm : { 6.0f, -6.0f })
    {
        const auto r = render (voice, { { id::formant, fm } });
        const double f = medianF0 (r.out);
        const double c = centroid (r.out);
        INFO ("formant " << fm << " f0 " << f << " centroid " << c << " (base " << base << ")");
        CHECK (std::abs (centsBetween (f, 220.0)) < 5.0);
        if (fm > 0)
            CHECK (c > base * 1.03);
        else
            CHECK (c < base * 0.97);
    }
}

TEST_CASE ("Subtle formant -2 keeps pitch and lowers the centroid slightly", "[engine][formant]")
{
    const auto voice = syntheticVoice (196.0, 1.5, kRate);
    const double base = centroid (render (voice, {}).out);
    const auto r = render (voice, { { id::formant, -2.0f } });
    CHECK (std::abs (centsBetween (medianF0 (r.out), 196.0)) < 5.0);
    const double c = centroid (r.out);
    CHECK (c < base);
    CHECK (c > base * 0.80); // subtle, not a different voice
}

TEST_CASE ("Link: formant follows pitch (SPEC §7)", "[engine][link]")
{
    const auto voice = syntheticVoice (220.0, 1.5, kRate);
    const double base = centroid (render (voice, {}).out);
    const double expected = 220.0 * std::pow (2.0, 5.0 / 12.0);

    const auto off = render (voice, { { id::pitch, 5.0f }, { id::link, 0.0f } });
    const auto on = render (voice, { { id::pitch, 5.0f }, { id::link, 1.0f } });
    CHECK (std::abs (centsBetween (medianF0 (off.out), expected)) < 5.0);
    CHECK (std::abs (centsBetween (medianF0 (on.out), expected)) < 5.0);

    const double cOff = centroid (off.out) / base;
    const double cOn = centroid (on.out) / base;
    INFO ("centroid ratio off " << cOff << " on " << cOn);
    CHECK (std::abs (cOff - 1.0) < 0.08); // formants preserved
    CHECK (cOn > 1.15);                   // formants shifted with the pitch (~ x1.33)
}

TEST_CASE ("Mix 0 % is a perfect null against the delayed input (SPEC §7)", "[engine][mix]")
{
    const auto voice = syntheticVoice (180.0, 1.0, kRate, 30.0);
    const auto r = render (voice, { { id::pitch, 7.0f }, { id::formant, -3.0f }, { id::mix, 0.0f } });
    REQUIRE (r.out.size() >= voice.size());
    for (std::size_t i = 0; i < voice.size(); ++i)
        REQUIRE (r.out[i] == voice[i]);
}

TEST_CASE ("Voiced / unvoiced transitions without clicks (SPEC §7)", "[engine][voicing]")
{
    // voice - silence - noise ("breath") - voice, with fades like real phrases.
    std::vector<float> x;
    const auto addFaded = [&] (std::vector<float> part) {
        const std::size_t ramp = static_cast<std::size_t> (0.02 * kRate);
        for (std::size_t i = 0; i < ramp && i < part.size(); ++i)
        {
            const float g = static_cast<float> (i) / static_cast<float> (ramp);
            part[i] *= g;
            part[part.size() - 1 - i] *= g;
        }
        x.insert (x.end(), part.begin(), part.end());
    };
    addFaded (syntheticVoice (200.0, 0.5, kRate, 20.0));
    addFaded (std::vector<float> (static_cast<std::size_t> (0.2 * kRate), 0.0f));
    addFaded (whiteNoise (0.3, kRate, 0.05f));
    addFaded (syntheticVoice (260.0, 0.5, kRate, 20.0));

    const double inputClicks = clickiness (x, 0);
    for (const float st : { 5.0f, -7.0f })
    {
        const auto r = render (x, { { id::pitch, st } });
        const double c = clickiness (r.out, static_cast<std::size_t> (0.05 * kRate));
        INFO ("pitch " << st << " clickiness " << c << " (input " << inputClicks << ")");
        CHECK (c < std::max (0.5, 3.0 * inputClicks));
    }
}

TEST_CASE ("Extreme settings stay finite and bounded", "[engine][robustness]")
{
    for (const auto& input : { whiteNoise (0.5, kRate, 0.9f), sine (110.0, 0.5, kRate, 1.0f),
                               syntheticVoice (90.0, 0.5, kRate, 0.0, 5.5, 1.0f) })
        for (const float st : { 12.0f, -12.0f })
            for (const float fm : { 12.0f, -12.0f })
            {
                const auto r = render (input, { { id::pitch, st }, { id::formant, fm }, { id::link, 0.0f } }, 64);
                float peak = 0.0f;
                for (const float v : r.out)
                {
                    REQUIRE (std::isfinite (v));
                    peak = std::max (peak, std::abs (v));
                }
                CHECK (peak < 4.0f);
            }
}
