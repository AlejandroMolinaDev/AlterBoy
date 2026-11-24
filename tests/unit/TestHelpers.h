#pragma once

// Shared helpers for the Catch2 tests.

#include <cmath>

#include <juce_audio_processors/juce_audio_processors.h>

#include "plugin/PluginProcessor.h"

namespace metamorfo::test
{
using Set = juce::AudioChannelSet;

inline juce::AudioProcessor::BusesLayout makeLayout (const Set& in, const Set& out)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    return layout;
}

// Deterministic, channel-dependent, non-trivial values. `seed` decorrelates instances.
inline float testSignal (int channel, int sampleIndex, int seed = 0)
{
    return 0.5f * std::sin (0.013f * static_cast<float> (sampleIndex) * static_cast<float> (channel + 1 + seed))
           + 0.001f * static_cast<float> (channel);
}

// Expected output of the processor at neutral settings: the input delayed by the
// reported latency (silence before the first delayed sample).
inline float expectedNeutral (int channel, int sampleIndex, int latency, int seed = 0)
{
    return sampleIndex >= latency ? testSignal (channel, sampleIndex - latency, seed) : 0.0f;
}

// Thread-safe variant (no Catch2 assertions): returns false if the layout is rejected.
inline bool tryPrepare (MetamorfoProcessor& p, const Set& in, const Set& out, int blockSize, double rate = 48000.0)
{
    if (! p.setBusesLayout (makeLayout (in, out)))
        return false;
    p.setRateAndBufferSizeDetails (rate, blockSize);
    p.prepareToPlay (rate, blockSize);
    return true;
}

// Deterministic block-size sequence 1..maxBlock (simple LCG, no shared state).
class IrregularBlocks
{
public:
    explicit IrregularBlocks (unsigned seed, int maxBlock = 4096) : state_ (seed), max_ (maxBlock) {}

    int next() noexcept
    {
        state_ = state_ * 1664525u + 1013904223u;
        return 1 + static_cast<int> ((state_ >> 8) % static_cast<unsigned> (max_));
    }

private:
    unsigned state_;
    int max_;
};

} // namespace metamorfo::test
