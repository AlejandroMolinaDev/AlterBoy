#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"

using metamorfo::MetamorfoProcessor;
using metamorfo::test::makeLayout;
using metamorfo::test::testSignal;
using Set = juce::AudioChannelSet;

namespace
{
void prepare (MetamorfoProcessor& p, const Set& in, const Set& out, int blockSize, double rate = 48000.0)
{
    REQUIRE (metamorfo::test::tryPrepare (p, in, out, blockSize, rate));
}

// Processes `total` samples split into the given block sizes; checks the output.
void checkPassthrough (const Set& in, const Set& out, int preparedBlockSize, const std::vector<int>& blocks)
{
    MetamorfoProcessor p;
    prepare (p, in, out, preparedBlockSize);

    const int numIn = in.size();
    const int numOut = out.size();
    const int numChannels = std::max (numIn, numOut);
    juce::MidiBuffer midi;
    int position = 0;

    for (const int blockSize : blocks)
    {
        juce::AudioBuffer<float> buffer (numChannels, blockSize);
        buffer.clear();
        for (int ch = 0; ch < numIn; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, testSignal (ch, position + i));

        // Garbage in channels the host has not filled from an input.
        for (int ch = numIn; ch < numChannels; ++ch)
            for (int i = 0; i < blockSize; ++i)
                buffer.setSample (ch, i, 0.75f);

        p.processBlock (buffer, midi);

        // Neutral settings: bit-exact copy of the input, delayed by the reported latency.
        const int latency = p.getLatencySamples();
        for (int ch = 0; ch < numOut; ++ch)
        {
            const int sourceChannel = numIn == 1 ? 0 : ch;
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (buffer.getSample (ch, i) == metamorfo::test::expectedNeutral (sourceChannel, position + i, latency));
        }

        position += blockSize;
    }

    p.releaseResources();
}
} // namespace

TEST_CASE ("Processor is a MIDI-receiving audio effect", "[processor]")
{
    MetamorfoProcessor p;
    CHECK (p.acceptsMidi());
    CHECK_FALSE (p.producesMidi());
    CHECK_FALSE (p.isMidiEffect());
    CHECK (p.getName() == "Metamorfo");
    CHECK (p.getTailLengthSeconds() == 0.0);
}

TEST_CASE ("Processor bus layouts", "[processor]")
{
    MetamorfoProcessor p;
    CHECK (p.checkBusesLayoutSupported (makeLayout (Set::mono(), Set::mono())));
    CHECK (p.checkBusesLayoutSupported (makeLayout (Set::mono(), Set::stereo())));
    CHECK (p.checkBusesLayoutSupported (makeLayout (Set::stereo(), Set::stereo())));
    CHECK_FALSE (p.checkBusesLayoutSupported (makeLayout (Set::stereo(), Set::mono())));
    CHECK_FALSE (p.checkBusesLayoutSupported (makeLayout (Set::create5point1(), Set::create5point1())));
    CHECK_FALSE (p.checkBusesLayoutSupported (makeLayout (Set::disabled(), Set::stereo())));
}

TEST_CASE ("Processor passthrough is bit-exact", "[processor]")
{
    // Includes blocks larger than the prepared size (FL Studio can do this).
    const std::vector<int> blocks { 16, 512, 1, 333, 4096, 64, 2000, 17 };

    SECTION ("mono -> mono") { checkPassthrough (Set::mono(), Set::mono(), 512, blocks); }
    SECTION ("stereo -> stereo") { checkPassthrough (Set::stereo(), Set::stereo(), 512, blocks); }
    SECTION ("mono -> stereo duplicates the input") { checkPassthrough (Set::mono(), Set::stereo(), 512, blocks); }
    SECTION ("tiny prepared block") { checkPassthrough (Set::stereo(), Set::stereo(), 16, blocks); }
    SECTION ("huge prepared block") { checkPassthrough (Set::stereo(), Set::stereo(), 4096, blocks); }
}

TEST_CASE ("Processor reports a fixed latency: two 60 Hz periods + drive oversampling", "[processor][latency]")
{
    for (const double rate : { 44100.0, 48000.0, 96000.0, 192000.0 })
    {
        MetamorfoProcessor p;
        prepare (p, Set::stereo(), Set::stereo(), 256, rate);
        const int expected = 2 * static_cast<int> (std::ceil (rate / 60.0)) + metamorfo::dsp::TubeDrive::kLatency;
        CHECK (p.getLatencySamples() == expected);
        CHECK (p.getLatencySamples() == p.getEngineLatencySamples());
        CHECK (1000.0 * p.getLatencySamples() / rate < 34.0); // 33.3 ms (SPEC §4.5 cap: 60 ms)

        // Fixed: parameter changes never alter it.
        auto* pitch = p.getParameters().getParameter (metamorfo::params::id::pitch);
        pitch->setValueNotifyingHost (pitch->convertTo0to1 (7.0f));
        juce::AudioBuffer<float> buffer (2, 256);
        buffer.clear();
        juce::MidiBuffer midi;
        p.processBlock (buffer, midi);
        CHECK (p.getLatencySamples() == expected);
    }
}

TEST_CASE ("Processor sanitises NaN and Inf", "[processor]")
{
    MetamorfoProcessor p;
    prepare (p, Set::stereo(), Set::stereo(), 64);

    juce::AudioBuffer<float> buffer (2, 64);
    buffer.clear();
    buffer.setSample (0, 3, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample (1, 7, std::numeric_limits<float>::infinity());
    buffer.setSample (1, 8, -std::numeric_limits<float>::infinity());

    juce::MidiBuffer midi;
    p.processBlock (buffer, midi);

    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < 64; ++i)
            REQUIRE (std::isfinite (buffer.getSample (ch, i)));
}

TEST_CASE ("Processor flushes denormals coming from the input", "[processor]")
{
    MetamorfoProcessor p;
    prepare (p, Set::stereo(), Set::stereo(), 64);

    const float tiny = std::numeric_limits<float>::min();
    const int latency = p.getLatencySamples();
    juce::AudioBuffer<float> buffer (2, 64);
    juce::MidiBuffer midi;
    float survivor = 0.0f;

    for (int position = 0; position < latency + 128; position += 64)
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 64; ++i)
                buffer.setSample (ch, i, (i % 2 == 0 ? 0.25f : -0.25f) * tiny); // subnormal values
        if (position == 0)
            buffer.setSample (0, 10, 0.5f); // a normal value must survive (delayed)

        p.processBlock (buffer, midi);

        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 64; ++i)
            {
                const float x = buffer.getSample (ch, i);
                REQUIRE ((x == 0.0f || std::abs (x) >= tiny));
                if (ch == 0 && position + i == 10 + latency)
                    survivor = x;
            }
    }
    CHECK (survivor == 0.5f);
}

TEST_CASE ("Processor publishes received MIDI notes", "[processor][midi]")
{
    MetamorfoProcessor p;
    prepare (p, Set::stereo(), Set::stereo(), 128);
    const auto& activity = p.getMidiActivity();

    juce::AudioBuffer<float> buffer (2, 128);
    const auto run = [&] (std::initializer_list<juce::MidiMessage> messages) {
        juce::MidiBuffer midi;
        int offset = 0;
        for (const auto& m : messages)
            midi.addEvent (m, offset++);
        buffer.clear();
        p.processBlock (buffer, midi);
    };

    CHECK (activity.lastNote.load() == -1);
    CHECK (activity.heldNote.load() == -1);

    run ({ juce::MidiMessage::noteOn (3, 64, static_cast<juce::uint8> (100)) });
    CHECK (activity.lastNote.load() == 64);
    CHECK (activity.lastVelocity.load() == 100);
    CHECK (activity.lastChannel.load() == 3);
    CHECK (activity.heldNote.load() == 64);

    run ({ juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (90)) });
    CHECK (activity.heldNote.load() == 67);

    run ({ juce::MidiMessage::noteOff (1, 67) });
    CHECK (activity.lastNote.load() == 67); // last *received* note-on
    CHECK (activity.heldNote.load() == 64);

    run ({ juce::MidiMessage::allNotesOff (1) });
    CHECK (activity.heldNote.load() == -1);

    run ({ juce::MidiMessage::noteOn (1, 50, static_cast<juce::uint8> (80)),
           juce::MidiMessage::noteOn (1, 52, static_cast<juce::uint8> (0)) }); // velocity 0 = off for a non-held note
    CHECK (activity.heldNote.load() == 50);

    // Controllers and other messages are not counted as note activity.
    const auto before = activity.eventCount.load();
    run ({ juce::MidiMessage::controllerEvent (1, 1, 64) });
    CHECK (activity.eventCount.load() == before);
}

TEST_CASE ("Processor exposes the frozen parameter contract", "[processor][params]")
{
    namespace id = metamorfo::params::id;
    MetamorfoProcessor p;
    const auto& parameters = p.getParameters().processor.getParameters();

    REQUIRE (parameters.size() == metamorfo::params::kNumParameters);

    // Order matters: FL Studio automation is stored by parameter index.
    const char* expected[] = { id::pitch, id::formant, id::link, id::mode,
                               id::drive, id::mix, id::midiReference, id::portamento };
    for (int i = 0; i < parameters.size(); ++i)
    {
        const auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (parameters[i]);
        REQUIRE (withId != nullptr);
        CHECK (withId->paramID == juce::String (expected[i]));
        CHECK (withId->getVersionHint() == metamorfo::params::kVersionHint);
    }

    auto& state = p.getParameters();
    CHECK (state.getRawParameterValue (id::pitch)->load() == 0.0f);
    CHECK (state.getRawParameterValue (id::formant)->load() == 0.0f);
    CHECK (state.getRawParameterValue (id::link)->load() == 0.0f);
    CHECK (state.getRawParameterValue (id::mode)->load() == 0.0f);
    CHECK (state.getRawParameterValue (id::drive)->load() == 0.0f);
    CHECK (state.getRawParameterValue (id::mix)->load() == 100.0f);
    CHECK (state.getRawParameterValue (id::midiReference)->load() == 60.0f);
    CHECK (state.getRawParameterValue (id::portamento)->load() == 0.0f);

    // Stepped parameters must land on exact decimal values on every platform
    // (regression: arm64 produced 1.79e-7 for the pitch default).
    for (const char* paramId : { id::pitch, id::formant })
    {
        auto* param = state.getParameter (paramId);
        for (int step = -120; step <= 120; ++step)
        {
            const float expected = static_cast<float> (step) / 10.0f;
            param->setValueNotifyingHost (param->convertTo0to1 (expected));
            REQUIRE (state.getRawParameterValue (paramId)->load() == expected);
        }
        param->setValueNotifyingHost (param->getDefaultValue());
        CHECK (state.getRawParameterValue (paramId)->load() == 0.0f);
    }

    // Text round-trip for the MIDI reference (shown as a note name).
    auto* reference = state.getParameter (id::midiReference);
    const auto text = reference->getText (reference->getValue(), 32);
    CHECK (reference->getValueForText (text) == reference->getValue());
}
