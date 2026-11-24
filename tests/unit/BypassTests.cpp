// Host bypass (B19/B20): while bypassed the plug-in must still delay the audio by
// its reported latency (otherwise PDC breaks and the voice jumps ~33 ms early) and
// must keep every output channel (mono -> stereo duplicates the input).

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"

using metamorfo::MetamorfoProcessor;
using metamorfo::test::Set;
using metamorfo::test::testSignal;
using metamorfo::test::tryPrepare;

namespace
{
// Runs `blocks` through processBlockBypassed and returns the worst mismatch against the
// input delayed by the latency (channel 1 of a mono input mirrors channel 0).
float bypassError (const Set& in, const Set& out, int blockSize, int numBlocks, bool alternate)
{
    MetamorfoProcessor p;
    auto* pitch = p.getParameters().getParameter (metamorfo::params::id::pitch);
    pitch->setValueNotifyingHost (pitch->convertTo0to1 (7.0f)); // non-neutral: bypass must ignore it
    REQUIRE (tryPrepare (p, in, out, blockSize));
    const int latency = p.getLatencySamples();
    REQUIRE (latency > 0);

    const int numIn = in.size(), numOut = out.size();
    juce::AudioBuffer<float> buffer (std::max (numIn, numOut), blockSize);
    juce::MidiBuffer midi;
    float worst = 0.0f;
    for (int b = 0; b < numBlocks; ++b)
    {
        for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, ch < numIn ? testSignal (ch, b * blockSize + i) : 0.5f);

        // Alternating: runs of 40 blocks; the first 16 bypassed blocks of a run cover the
        // 20 ms cross-fade (checked for clicks separately).
        const bool bypassed = ! alternate || (b / 40) % 2 == 1;
        if (bypassed)
            p.processBlockBypassed (buffer, midi);
        else
            p.processBlock (buffer, midi);

        if (! bypassed || (alternate && b % 40 < 16))
            continue;
        for (int ch = 0; ch < numOut; ++ch)
            for (int i = 0; i < blockSize; ++i)
            {
                const int n = b * blockSize + i - latency;
                const float expected = n >= 0 ? testSignal (numIn == 1 ? 0 : ch, n) : 0.0f;
                worst = std::max (worst, std::abs (buffer.getSample (ch, i) - expected));
            }
    }
    return worst;
}
} // namespace

TEST_CASE ("Bypass delays the audio by the reported latency (B19)", "[bypass]")
{
    CHECK (bypassError (Set::stereo(), Set::stereo(), 256, 40, false) == 0.0f);
    CHECK (bypassError (Set::mono(), Set::mono(), 256, 40, false) == 0.0f);
}

TEST_CASE ("Bypass keeps both channels for mono -> stereo (B20)", "[bypass]")
{
    CHECK (bypassError (Set::mono(), Set::stereo(), 128, 60, false) == 0.0f);
}

TEST_CASE ("Bypass toggled while playing stays aligned", "[bypass]")
{
    // Blocks alternate processed / bypassed: once the fade is over the bypassed blocks
    // must be the exactly delayed input (the dry delay line keeps running in both states).
    CHECK (bypassError (Set::stereo(), Set::stereo(), 64, 200, true) == 0.0f);
}

TEST_CASE ("Toggling bypass cross-fades without clicks", "[bypass]")
{
    // A 220 Hz sine through pitch +7: toggling bypass every 0.25 s must not produce a
    // sample-to-sample step much larger than the signal's own maximum slope.
    MetamorfoProcessor p;
    auto* pitch = p.getParameters().getParameter (metamorfo::params::id::pitch);
    pitch->setValueNotifyingHost (pitch->convertTo0to1 (7.0f));
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 256));
    juce::AudioBuffer<float> buffer (2, 256);
    juce::MidiBuffer midi;
    const double w = 2.0 * juce::MathConstants<double>::pi * 220.0 / 48000.0;
    float previous = 0.0f, worstStep = 0.0f;
    long n = 0;
    for (int b = 0; b < 400; ++b)
    {
        for (int i = 0; i < 256; ++i, ++n)
            for (int ch = 0; ch < 2; ++ch)
                buffer.setSample (ch, i, static_cast<float> (0.5 * std::sin (w * static_cast<double> (n))));
        if ((b / 47) % 2 == 1)
            p.processBlockBypassed (buffer, midi);
        else
            p.processBlock (buffer, midi);
        for (int i = 0; i < 256; ++i)
        {
            const float v = buffer.getSample (0, i);
            if (b > 20)
                worstStep = std::max (worstStep, std::abs (v - previous));
            previous = v;
        }
    }
    // Max slope of 0.5 sin at 220 Hz (dry) / ~330 Hz (+7 st) is ~0.022 per sample.
    INFO ("worst step " << worstStep);
    CHECK (worstStep < 0.06f);
}

TEST_CASE ("Bypass with oversized host blocks", "[bypass]")
{
    MetamorfoProcessor p;
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 64));
    juce::AudioBuffer<float> buffer (2, 3000); // larger than prepared
    buffer.clear();
    juce::MidiBuffer midi;
    p.processBlockBypassed (buffer, midi);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 3000; ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}
