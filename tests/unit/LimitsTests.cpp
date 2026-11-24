// Engine / detector at the edges of their range, long silences and a 1-hour soak.

#include "EngineTestUtils.h"

#include <random>

using namespace metamorfo::test;
namespace id = metamorfo::params::id;

namespace
{
std::vector<float> concat (std::initializer_list<std::vector<float>> parts)
{
    std::vector<float> out;
    for (const auto& p : parts)
        out.insert (out.end(), p.begin(), p.end());
    return out;
}

// Voice whose f0 glides exponentially from f0a to f0b.
std::vector<float> glide (double f0a, double f0b, double seconds)
{
    const auto n = static_cast<std::size_t> (seconds * kRate);
    std::vector<float> out (n);
    double phase = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double t = static_cast<double> (i) / static_cast<double> (n);
        const double f = f0a * std::pow (f0b / f0a, t);
        phase += f / kRate;
        phase -= std::floor (phase);
        // Band-limited-ish pulse: a few harmonics with decaying amplitude.
        double v = 0.0;
        for (int k = 1; k <= 8; ++k)
            v += std::sin (2.0 * kPi * k * phase) / k;
        out[i] = static_cast<float> (0.3 * v);
    }
    return out;
}
} // namespace

TEST_CASE ("Lowest supported f0 (61 Hz) transposes correctly", "[limits]")
{
    const auto voice = syntheticVoice (61.0, 2.0, kRate);
    const auto r = render (voice, { { id::pitch, 5.0f } });
    CHECK (std::abs (centsBetween (medianF0 (r.out, 0.5), 61.0 * std::pow (2.0, 5.0 / 12.0))) < 5.0);
}

TEST_CASE ("Highest supported f0 (1150 Hz) transposes down an octave", "[limits]")
{
    const auto voice = sine (1150.0, 1.5, kRate);
    const auto r = render (voice, { { id::pitch, -12.0f } });
    CHECK (std::abs (centsBetween (medianF0 (r.out, 0.4), 575.0)) < 5.0);
}

TEST_CASE ("Robot beyond the ratio limit is clamped, finite and stable", "[limits][robot]")
{
    // 1150 Hz input asked to sing MIDI 24 (32.7 Hz): ratio 1/35 is clamped to 1/8.
    const auto voice = sine (1150.0, 1.5, kRate);
    const auto r = render (voice, { { id::mode, 2.0f } }, 256, { { 0.0, 1.5, 24 } });
    for (const float v : r.out)
        REQUIRE (std::isfinite (v));
    const double f = medianF0 (r.out, 0.4);
    INFO ("clamped output " << f << " Hz");
    CHECK (std::abs (centsBetween (f, 1150.0 / 8.0)) < 15.0);
}

TEST_CASE ("Octave jumps in the input are followed (no stuck octave)", "[limits][detector]")
{
    const auto x = concat ({ syntheticVoice (110.0, 0.6, kRate), syntheticVoice (220.0, 0.6, kRate),
                             syntheticVoice (110.0, 0.6, kRate), syntheticVoice (440.0, 0.6, kRate) });
    const auto r = render (x, { { id::pitch, 0.1f } }); // non-identity so the engine runs
    const double ratio = std::pow (2.0, 0.1 / 12.0);
    CHECK (std::abs (centsBetween (medianF0 (r.out, 0.30, 0.55), 110.0 * ratio)) < 10.0);
    CHECK (std::abs (centsBetween (medianF0 (r.out, 0.90, 1.15), 220.0 * ratio)) < 10.0);
    CHECK (std::abs (centsBetween (medianF0 (r.out, 1.50, 1.75), 110.0 * ratio)) < 10.0);
    CHECK (std::abs (centsBetween (medianF0 (r.out, 2.10, 2.35), 440.0 * ratio)) < 10.0);
}

TEST_CASE ("Two-octave glissando is tracked while transposing", "[limits][detector]")
{
    const auto x = glide (110.0, 440.0, 3.0);
    const auto r = render (x, { { id::pitch, 7.0f } });
    // Instantaneous expected f0 at time t (input glide x +7 st).
    for (const double t : { 0.6, 1.2, 1.8, 2.4 })
    {
        const double expected = 110.0 * std::pow (4.0, t / 3.0) * std::pow (2.0, 7.0 / 12.0);
        const double measured = medianF0 (r.out, t - 0.05, t + 0.05);
        INFO ("t " << t << " expected " << expected << " measured " << measured);
        CHECK (std::abs (centsBetween (measured, expected)) < 25.0); // glide moves ~40 cents in 100 ms
    }
}

TEST_CASE ("Voice gated every 30 ms: finite, no clicks beyond the gating itself", "[limits][voicing]")
{
    auto x = syntheticVoice (200.0, 2.0, kRate);
    const auto gate = static_cast<std::size_t> (0.03 * kRate);
    const auto ramp = static_cast<std::size_t> (0.002 * kRate);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const std::size_t posInGate = i % gate;
        const bool on = (i / gate) % 2 == 0;
        float g = on ? 1.0f : 0.0f;
        if (on && posInGate < ramp) g = static_cast<float> (posInGate) / static_cast<float> (ramp);
        if (on && posInGate >= gate - ramp) g = static_cast<float> (gate - posInGate) / static_cast<float> (ramp);
        x[i] *= g;
    }
    const double inputClicks = clickiness (x, 0);
    const auto r = render (x, { { id::pitch, -5.0f }, { id::formant, 2.0f } });
    for (const float v : r.out)
        REQUIRE (std::isfinite (v));
    CHECK (clickiness (r.out, static_cast<std::size_t> (0.05 * kRate)) < std::max (0.5, 3.0 * inputClicks));
}

TEST_CASE ("Ten minutes of silence, then voice: no stuck state", "[limits][silence]")
{
    // 10 min of digital silence (plus a dither-level tail) then a voice.
    std::vector<float> x (static_cast<std::size_t> (600.0 * kRate), 0.0f);
    const auto v = syntheticVoice (220.0, 1.5, kRate);
    x.insert (x.end(), v.begin(), v.end());
    const auto r = render (x, { { id::pitch, 7.0f } }, 4096);
    std::vector<float> tail (r.out.end() - static_cast<long> (v.size()), r.out.end());
    CHECK (std::abs (centsBetween (medianF0 (tail, 0.4), 220.0 * std::pow (2.0, 7.0 / 12.0))) < 5.0);
}

TEST_CASE ("One-hour soak with random automation keeps alignment and stability", "[.soak]")
{
    // Hidden by default (run with "[.soak]"): ~1 h of audio processed offline.
    MetamorfoProcessor p;
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 512, kRate));
    const int latency = p.getLatencySamples();
    juce::AudioBuffer<float> buffer (2, 512);
    juce::MidiBuffer midi;
    std::mt19937 rng (2026);
    auto& params = p.getParameters().processor.getParameters();
    const auto voice = syntheticVoice (180.0, 5.0, kRate, 30.0);
    const auto totalBlocks = static_cast<long long> (3600.0 * kRate / 512.0);
    for (long long b = 0; b < totalBlocks; ++b)
    {
        if (b % 200 == 0)
            for (int k = 0; k < 3; ++k)
                params[static_cast<int> (rng() % 6)]->setValueNotifyingHost (std::uniform_real_distribution<float> (0.0f, 1.0f) (rng));
        for (int i = 0; i < 512; ++i)
        {
            const float s = voice[static_cast<std::size_t> ((b * 512 + i) % static_cast<long long> (voice.size()))];
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < 512; ++i)
            REQUIRE (std::isfinite (buffer.getSample (0, i)));
    }

    // After an hour: neutral settings must still be the bit-exact delayed input.
    for (auto* param : params)
        if (auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (param))
            ranged->setValueNotifyingHost (ranged->getDefaultValue());
    const auto base = totalBlocks * 512;
    int mismatches = 0;
    std::vector<float> history;
    for (int b = 0; b < 40; ++b)
    {
        for (int i = 0; i < 512; ++i)
        {
            const float s = voice[static_cast<std::size_t> ((base + b * 512 + i) % static_cast<long long> (voice.size()))];
            buffer.setSample (0, i, s);
            buffer.setSample (1, i, s);
            history.push_back (s);
        }
        p.processBlock (buffer, midi);
        if (b < 10) // let the 20 ms identity cross-fade and the delay line settle
            continue;
        for (int i = 0; i < 512; ++i)
        {
            const auto idx = static_cast<std::size_t> (b * 512 + i - latency);
            if (buffer.getSample (0, i) != history[idx])
                ++mismatches;
        }
    }
    CHECK (mismatches == 0);
}
