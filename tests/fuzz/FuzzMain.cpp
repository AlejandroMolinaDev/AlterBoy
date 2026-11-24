// MetamorfoFuzz: deterministic "chaos" fuzzer for the processor.
//
// Each episode picks a sample rate (44.1-192 kHz), a layout (mono, stereo, mono->stereo)
// and a prepared block size, then runs random blocks (0..4096 samples, often larger
// than prepared) with random parameter automation, dense / out-of-range MIDI (including
// queue overflow, CC 120/123, pitch bend, sysex), hostile input audio (silence, noise,
// voice-like pulses, +20 dBFS, NaN, Inf, denormals), state save/restore with valid and
// garbage data, presets, A/B, host bypass and hot re-preparation.
//
// Invariants checked after every block:
//   * output finite and bounded (|y| <= 16 x max(1, input peak))
//   * reported latency constant for the sample rate
//   * zero heap allocations inside processBlock / processBlockBypassed
//
// Usage: MetamorfoFuzz [--seed N] [--episodes N] [--blocks N] [--no-bypass]

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <iostream>
#include <limits>
#include <new>
#include <random>

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

// ---- allocation probe (same idea as MetamorfoRender) ---------------------------------
namespace
{
thread_local bool insideAudio = false;
std::atomic<long long> audioAllocations { 0 };
} // namespace

void* operator new (std::size_t size)
{
    if (insideAudio)
        audioAllocations.fetch_add (1, std::memory_order_relaxed);
    if (void* p = std::malloc (size == 0 ? 1 : size))
        return p;
    throw std::bad_alloc();
}
void* operator new[] (std::size_t size) { return operator new (size); }
void operator delete (void* p) noexcept { std::free (p); }
void operator delete[] (void* p) noexcept { std::free (p); }
void operator delete (void* p, std::size_t) noexcept { std::free (p); }
void operator delete[] (void* p, std::size_t) noexcept { std::free (p); }

namespace
{
using Set = juce::AudioChannelSet;

struct Failure
{
    juce::String what;
};

class Fuzzer
{
public:
    Fuzzer (unsigned seed, bool allowBypass) : rng_ (seed), seed_ (seed), allowBypass_ (allowBypass) {}

    int run (int episodes, int blocksPerEpisode)
    {
        int failures = 0;
        for (int e = 0; e < episodes; ++e)
        {
            episode_ = e;
            if (! runEpisode (blocksPerEpisode))
                ++failures;
        }
        return failures;
    }

private:
    float uniform (float a, float b) { return std::uniform_real_distribution<float> (a, b) (rng_); }
    int integer (int a, int b) { return std::uniform_int_distribution<int> (a, b) (rng_); }
    bool chance (double p) { return std::bernoulli_distribution (p) (rng_); }

    bool fail (const juce::String& what)
    {
        std::cout << "FAIL seed " << seed_ << " episode " << episode_ << " block " << block_ << ": " << what << std::endl;
        return false;
    }

    bool runEpisode (int blocks)
    {
        static constexpr double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 };
        double rate = rates[integer (0, 5)];
        const Set layouts[][2] = { { Set::mono(), Set::mono() }, { Set::mono(), Set::stereo() }, { Set::stereo(), Set::stereo() } };
        const auto& layout = layouts[integer (0, 2)];
        int prepared = std::array<int, 6> { 16, 64, 128, 512, 1024, 4096 }[static_cast<std::size_t> (integer (0, 5))];

        metamorfo::MetamorfoProcessor p;
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (layout[0]);
        l.outputBuses.add (layout[1]);
        if (! p.setBusesLayout (l))
            return fail ("layout rejected");
        p.setRateAndBufferSizeDetails (rate, prepared);
        p.prepareToPlay (rate, prepared);
        int expectedLatency = p.getLatencySamples();

        const int numIn = layout[0].size(), numOut = layout[1].size();
        juce::AudioBuffer<float> buffer (std::max (numIn, numOut), 4096);
        juce::MidiBuffer midi;
        midi.ensureSize (8192 * 4);
        auto& params = p.getParameters().processor.getParameters();
        double phase = 0.0;
        float episodePeak = 1.0f; // output is delayed: bound it by every input seen so far

        for (block_ = 0; block_ < blocks; ++block_)
        {
            // ---- occasional host-level events (message thread, between blocks) ------------
            if (chance (0.02))
            {
                rate = rates[integer (0, 5)];
                prepared = integer (1, 4096);
                p.setRateAndBufferSizeDetails (rate, prepared);
                p.prepareToPlay (rate, prepared);
                expectedLatency = p.getLatencySamples();
                if (expectedLatency != 2 * static_cast<int> (std::ceil (rate / 60.0)) + 3)
                    return fail ("unexpected latency " + juce::String (expectedLatency) + " at " + juce::String (rate));
            }
            if (chance (0.03))
            {
                juce::MemoryBlock state;
                p.getStateInformation (state);
                if (chance (0.5))
                    p.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
                else
                {
                    juce::MemoryBlock garbage (static_cast<std::size_t> (integer (0, 300)));
                    for (std::size_t i = 0; i < garbage.getSize(); ++i)
                        garbage[i] = static_cast<char> (integer (0, 255));
                    p.setStateInformation (garbage.getData(), static_cast<int> (garbage.getSize()));
                }
            }
            if (chance (0.02))
                p.getPresets().load (integer (0, static_cast<int> (p.getPresets().entries().size()) - 1));
            if (chance (0.01))
                p.getPresets().toggleAB();
            if (chance (0.01))
            {
                // Real parameter IDs, plus an invalid one that must be ignored.
                auto* ranged = dynamic_cast<juce::RangedAudioParameter*> (params[integer (0, params.size() - 1)]);
                p.getLocks().toggle (chance (0.2) || ranged == nullptr ? juce::String ("no es un id") : ranged->paramID);
            }

            // ---- automation ---------------------------------------------------------------
            const int changes = chance (0.3) ? integer (1, 12) : 0;
            for (int c = 0; c < changes; ++c)
            {
                auto* param = params[integer (0, params.size() - 1)];
                const float v = chance (0.2) ? (chance (0.5) ? 0.0f : 1.0f) : uniform (0.0f, 1.0f);
                param->setValueNotifyingHost (v);
            }

            // ---- input audio ----------------------------------------------------------------
            const int size = chance (0.05) ? 0 : (chance (0.2) ? integer (prepared, 4096) : integer (1, std::min (4096, prepared)));
            buffer.setSize (buffer.getNumChannels(), size, false, false, true);
            const int kind = integer (0, 7);
            float inputPeak = 0.0f;
            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < size; ++i)
                {
                    float x = 0.0f;
                    switch (kind)
                    {
                        case 0: x = 0.0f; break;
                        case 1: x = uniform (-0.3f, 0.3f); break;
                        case 2: x = 0.5f * static_cast<float> (std::sin (phase + i * 0.03)); break;
                        case 3: x = std::fmod (phase + i, 220.0) < 3.0 ? 0.8f : -0.01f; break; // pulse train ("voice")
                        case 4: x = uniform (-10.0f, 10.0f); break;                              // +20 dBFS
                        case 5: x = chance (0.01) ? std::numeric_limits<float>::quiet_NaN() : uniform (-0.5f, 0.5f); break;
                        case 6: x = chance (0.01) ? std::numeric_limits<float>::infinity() : 0.2f; break;
                        default: x = uniform (-1.0f, 1.0f) * std::numeric_limits<float>::min() * 0.5f; break; // denormals
                    }
                    if (ch >= numIn)
                        x = uniform (-1.0f, 1.0f); // garbage in channels the host did not fill
                    buffer.setSample (ch, i, x);
                    if (ch < numIn && std::isfinite (x))
                        inputPeak = std::max (inputPeak, std::abs (x));
                }
            phase += size;
            episodePeak = std::max (episodePeak, inputPeak);

            // ---- MIDI (dense, overflowing the 1024-event queue, odd messages) -------------
            midi.clear();
            const int events = chance (0.05) ? integer (1000, 3000) : (chance (0.3) ? integer (1, 20) : 0);
            for (int ev = 0; ev < events; ++ev)
            {
                const int pos = size > 0 ? integer (0, size + 8) : 0;
                const int type = integer (0, 9);
                const int ch = integer (1, 16), note = integer (0, 127);
                if (type < 4)
                    midi.addEvent (juce::MidiMessage::noteOn (ch, note, static_cast<juce::uint8> (integer (0, 127))), pos);
                else if (type < 7)
                    midi.addEvent (juce::MidiMessage::noteOff (ch, note), pos);
                else if (type == 7)
                    midi.addEvent (juce::MidiMessage::controllerEvent (ch, chance (0.5) ? 123 : 120, 0), pos);
                else if (type == 8)
                    midi.addEvent (juce::MidiMessage::pitchWheel (ch, integer (0, 16383)), pos);
                else
                {
                    const juce::uint8 sysex[] = { 0x7e, 0x7f, 0x06, 0x01 };
                    midi.addEvent (juce::MidiMessage::createSysExMessage (sysex, 4), pos);
                }
            }

            // ---- process ------------------------------------------------------------------
            const bool bypass = allowBypass_ && chance (0.05);
            audioAllocations = 0;
            insideAudio = true;
            if (bypass)
                p.processBlockBypassed (buffer, midi);
            else
                p.processBlock (buffer, midi);
            insideAudio = false;

            // ---- invariants -----------------------------------------------------------------
            if (audioAllocations.load() != 0)
                return fail (juce::String (audioAllocations.load()) + " allocations inside the audio callback"
                             + (bypass ? " (bypass)" : ""));
            if (p.getLatencySamples() != expectedLatency)
                return fail ("latency changed to " + juce::String (p.getLatencySamples()));
            const float bound = 16.0f * episodePeak;
            for (int ch = 0; ch < numOut; ++ch)
                for (int i = 0; i < size; ++i)
                {
                    const float y = buffer.getSample (ch, i);
                    if (! std::isfinite (y))
                        return fail ("non-finite output on channel " + juce::String (ch) + (bypass ? " (bypass)" : ""));
                    if (std::abs (y) > bound)
                        return fail ("output " + juce::String (y) + " exceeds bound " + juce::String (bound));
                }
        }
        return true;
    }

    std::mt19937 rng_;
    unsigned seed_;
    bool allowBypass_ = true;
    int episode_ = 0, block_ = 0;
};
} // namespace

int main (int argc, char* argv[])
{
    const juce::ScopedJuceInitialiser_GUI init;
    unsigned seed = 1;
    int episodes = 50, blocks = 200;
    bool allowBypass = true;
    for (int i = 1; i < argc; ++i)
        if (juce::String (argv[i]) == "--no-bypass")
            allowBypass = false;
    for (int i = 1; i + 1 < argc; i += 2)
    {
        const juce::String a (argv[i]);
        const int v = juce::String (argv[i + 1]).getIntValue();
        if (a == "--seed") seed = static_cast<unsigned> (v);
        else if (a == "--episodes") episodes = v;
        else if (a == "--blocks") blocks = v;
    }
    Fuzzer fuzzer (seed, allowBypass);
    const int failures = fuzzer.run (episodes, blocks);
    std::cout << "MetamorfoFuzz seed " << seed << ": " << episodes << " episodes x " << blocks << " blocks, "
              << failures << " failing episode(s)" << std::endl;
    return failures == 0 ? 0 : 1;
}
