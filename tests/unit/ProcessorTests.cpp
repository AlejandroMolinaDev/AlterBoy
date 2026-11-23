#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <limits>
#include <vector>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

using metamorfo::MetamorfoProcessor;
using Set = juce::AudioChannelSet;

namespace
{
juce::AudioProcessor::BusesLayout makeLayout (const Set& in, const Set& out)
{
    juce::AudioProcessor::BusesLayout layout;
    layout.inputBuses.add (in);
    layout.outputBuses.add (out);
    return layout;
}

float testSignal (int channel, int sampleIndex)
{
    // Deterministic, channel-dependent, non-trivial values.
    return 0.5f * std::sin (0.013f * static_cast<float> (sampleIndex) * static_cast<float> (channel + 1))
           + 0.001f * static_cast<float> (channel);
}

void prepare (MetamorfoProcessor& p, const Set& in, const Set& out, int blockSize, double rate = 48000.0)
{
    REQUIRE (p.setBusesLayout (makeLayout (in, out)));
    p.setRateAndBufferSizeDetails (rate, blockSize);
    p.prepareToPlay (rate, blockSize);
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

        for (int ch = 0; ch < numOut; ++ch)
        {
            const int sourceChannel = numIn == 1 ? 0 : ch;
            for (int i = 0; i < blockSize; ++i)
                REQUIRE (buffer.getSample (ch, i) == testSignal (sourceChannel, position + i));
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

TEST_CASE ("Processor reports zero latency in phase 1", "[processor]")
{
    MetamorfoProcessor p;
    prepare (p, Set::stereo(), Set::stereo(), 256, 44100.0);
    CHECK (p.getLatencySamples() == 0);
    CHECK (p.getLatencySamples() == p.getEngineLatencySamples());
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

    // Text round-trip for the MIDI reference (shown as a note name).
    auto* reference = state.getParameter (id::midiReference);
    const auto text = reference->getText (reference->getValue(), 32);
    CHECK (reference->getValueForText (text) == reference->getValue());
}
