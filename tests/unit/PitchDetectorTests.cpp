#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

#include "dsp/PitchDetector.h"

#include "TestSignals.h"

using metamorfo::dsp::PitchDetector;
using namespace metamorfo::test;

namespace
{
struct Run
{
    double medianF0 = 0.0;
    double voicedRatio = 0.0;
    double maxErrorCents = 0.0; // over voiced estimates after settling
};

// Feeds the signal and collects the estimates produced after `settle` seconds.
Run analyse (const std::vector<float>& x, double sr, double expected = 0.0, double settle = 0.15)
{
    PitchDetector d;
    d.prepare (sr);
    std::vector<double> f0s;
    int estimates = 0, voiced = 0;
    double maxErr = 0.0;
    double lastTime = -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        d.push (x[i]);
        const auto& e = d.latest();
        if (e.time != lastTime && static_cast<double> (i) / sr > settle)
        {
            lastTime = e.time;
            ++estimates;
            if (e.voiced)
            {
                ++voiced;
                f0s.push_back (e.f0);
                if (expected > 0.0)
                    maxErr = std::max (maxErr, std::abs (centsBetween (e.f0, expected)));
            }
        }
    }
    Run r;
    if (! f0s.empty())
    {
        std::nth_element (f0s.begin(), f0s.begin() + static_cast<long> (f0s.size() / 2), f0s.end());
        r.medianF0 = f0s[f0s.size() / 2];
    }
    r.voicedRatio = estimates > 0 ? static_cast<double> (voiced) / estimates : 0.0;
    r.maxErrorCents = maxErr;
    return r;
}
} // namespace

TEST_CASE ("PitchDetector: sines 80-1000 Hz within 5 cents (SPEC 7)", "[detector]")
{
    for (const double sr : { 44100.0, 48000.0, 96000.0 })
        for (const double f : { 80.0, 100.0, 150.0, 220.0, 300.0, 440.0, 600.0, 800.0, 1000.0 })
        {
            INFO ("sr " << sr << " f " << f);
            const auto r = analyse (sine (f, 0.8, sr), sr, f);
            CHECK (r.voicedRatio > 0.95);
            CHECK (r.maxErrorCents < 5.0);
        }
}

TEST_CASE ("PitchDetector: range limits 60 and 1200 Hz", "[detector]")
{
    for (const double f : { 62.0, 1150.0 })
    {
        INFO ("f " << f);
        const auto r = analyse (sine (f, 1.0, 48000.0), 48000.0, f);
        CHECK (r.voicedRatio > 0.9);
        CHECK (r.maxErrorCents < 5.0);
    }
}

TEST_CASE ("PitchDetector: synthetic voice, no octave errors", "[detector]")
{
    for (const double note : { 40.0, 45.0, 52.0, 57.0, 64.0, 69.0, 76.0 }) // E2 .. E5
    {
        const double f = midiToHz (note);
        INFO ("note " << note << " (" << f << " Hz)");
        const auto r = analyse (syntheticVoice (f, 1.0, 48000.0), 48000.0, f);
        CHECK (r.voicedRatio > 0.95);
        CHECK (r.maxErrorCents < 20.0); // formant-filtered pulse: no octave jumps, small bias allowed
        CHECK (std::abs (centsBetween (r.medianF0, f)) < 5.0);
    }
}

TEST_CASE ("PitchDetector: follows vibrato", "[detector]")
{
    const auto r = analyse (syntheticVoice (220.0, 1.5, 48000.0, 50.0), 48000.0);
    CHECK (r.voicedRatio > 0.95);
    CHECK (std::abs (centsBetween (r.medianF0, 220.0)) < 10.0);
}

TEST_CASE ("PitchDetector: noise and silence are unvoiced", "[detector][voicing]")
{
    CHECK (analyse (whiteNoise (1.0, 48000.0), 48000.0).voicedRatio < 0.05);
    CHECK (analyse (std::vector<float> (48000, 0.0f), 48000.0).voicedRatio == 0.0);
    CHECK (analyse (sine (220.0, 1.0, 48000.0, 0.0005f), 48000.0).voicedRatio < 0.05); // -72 dBFS
}

TEST_CASE ("PitchDetector: voiced onset is detected quickly", "[detector][voicing]")
{
    const double sr = 48000.0;
    std::vector<float> x (static_cast<std::size_t> (0.3 * sr), 0.0f);
    const auto v = syntheticVoice (180.0, 0.5, sr);
    x.insert (x.end(), v.begin(), v.end());

    PitchDetector d;
    d.prepare (sr);
    double firstVoicedTime = -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        d.push (x[i]);
        if (firstVoicedTime < 0.0 && d.latest().voiced)
            firstVoicedTime = static_cast<double> (i) / sr;
    }
    REQUIRE (firstVoicedTime > 0.0);
    CHECK (firstVoicedTime - 0.3 < 0.06); // within 60 ms of the onset
}

TEST_CASE ("PitchDetector: estimateAt returns the estimate centred before a time", "[detector]")
{
    const double sr = 48000.0;
    PitchDetector d;
    d.prepare (sr);
    for (const float s : sine (200.0, 0.5, sr))
        d.push (s);
    const auto latest = d.latest();
    CHECK (d.estimateAt (latest.time).time == latest.time);
    CHECK (d.estimateAt (latest.time - 1.0).time < latest.time);
    // The newest analysis ran on the last hop boundary and is centred one max period back.
    const auto lastHop = (d.samplesPushed() / d.hopSamples()) * d.hopSamples();
    CHECK (latest.time == static_cast<double> (lastHop) - d.maxPeriodSamples());
}
