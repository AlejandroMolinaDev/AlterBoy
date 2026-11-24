// G4 (test audit): the reported latency must equal the MEASURED latency, for the
// dry path (MIX 0 %) and the wet path (MIX 100 %, PITCH 0), at several sample rates.

#include <catch2/catch_test_macros.hpp>

#include <cmath>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"

using metamorfo::MetamorfoProcessor;
using metamorfo::test::Set;

namespace
{
// Sends an impulse through the processor in fixed blocks and returns the delay of
// the output peak relative to the input impulse, in samples.
int measureImpulseDelay (double rate, float mixPercent)
{
    namespace id = metamorfo::params::id;
    MetamorfoProcessor p;
    auto* mix = p.getParameters().getParameter (id::mix);
    mix->setValueNotifyingHost (mix->convertTo0to1 (mixPercent));
    REQUIRE (metamorfo::test::tryPrepare (p, Set::stereo(), Set::stereo(), 512, rate));

    const int length = static_cast<int> (rate); // 1 s, far longer than the 60 ms latency cap
    const int impulseAt = 1000;
    juce::AudioBuffer<float> block (2, 512);
    juce::MidiBuffer midi;
    int peakIndex = -1;
    float peak = 0.0f;

    for (int position = 0; position < length; position += 512)
    {
        const int size = std::min (512, length - position);
        block.setSize (2, size, false, false, true);
        block.clear();
        if (impulseAt >= position && impulseAt < position + size)
            for (int ch = 0; ch < 2; ++ch)
                block.setSample (ch, impulseAt - position, 0.9f);

        p.processBlock (block, midi);

        for (int i = 0; i < size; ++i)
            if (std::abs (block.getSample (0, i)) > peak)
            {
                peak = std::abs (block.getSample (0, i));
                peakIndex = position + i;
            }
    }

    REQUIRE (peak > 0.1f);
    const int reported = p.getLatencySamples();
    CHECK (reported >= 0);
    CHECK (reported <= static_cast<int> (std::ceil (0.060 * rate))); // SPEC §4.5 hard cap: 60 ms
    return peakIndex - impulseAt - reported; // 0 when measured == reported
}
} // namespace

TEST_CASE ("Latency reported equals measured", "[latency]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0 })
    {
        INFO ("sample rate " << rate);
        CHECK (measureImpulseDelay (rate, 0.0f) == 0);   // dry path (aligned dry signal)
        CHECK (measureImpulseDelay (rate, 100.0f) == 0); // wet path, PITCH 0
    }
}
