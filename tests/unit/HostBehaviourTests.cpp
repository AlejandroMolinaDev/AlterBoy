// Host-behaviour integration tests: things FL Studio (and other DAWs) do that a
// plain unit test does not cover — many instances on many threads, hot sample-rate
// changes, process before prepare, state access and editor churn while audio runs,
// MIDI on block edges. Catch2 assertions are only used on the main thread.

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <thread>
#include <vector>

#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"

using metamorfo::MetamorfoProcessor;
using metamorfo::test::IrregularBlocks;
using metamorfo::test::Set;
using metamorfo::test::testSignal;
using metamorfo::test::tryPrepare;

namespace
{
// Fills `buffer` with the deterministic signal, processes it and returns the number
// of samples that differ from the expected passthrough output.
int processAndCount (MetamorfoProcessor& p, juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi,
                     int numSamples, int position, int seed)
{
    buffer.setSize (2, numSamples, false, false, true);
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            buffer.setSample (ch, i, testSignal (ch, position + i, seed));

    p.processBlock (buffer, midi);

    // Neutral settings: output = input delayed by the reported latency, bit-exact.
    const int latency = p.getLatencySamples();
    int mismatches = 0;
    for (int ch = 0; ch < 2; ++ch)
        for (int i = 0; i < numSamples; ++i)
            if (buffer.getSample (ch, i) != metamorfo::test::expectedNeutral (ch, position + i, latency, seed))
                ++mismatches;
    return mismatches;
}

// Joins a thread on scope exit, so a failing REQUIRE never leaves a joinable
// std::thread behind (which would call std::terminate).
struct StopAndJoin
{
    std::atomic<bool>& running;
    std::thread& thread;
    ~StopAndJoin()
    {
        running = false;
        if (thread.joinable())
            thread.join();
    }
};

bool hasDisplay()
{
#if JUCE_LINUX
    return std::getenv ("DISPLAY") != nullptr;
#else
    return true;
#endif
}
} // namespace

TEST_CASE ("Many instances process concurrently without interfering", "[host][threads]")
{
    constexpr int numInstances = 12;
    constexpr int numBlocks = 500;

    std::vector<std::unique_ptr<MetamorfoProcessor>> instances;
    for (int i = 0; i < numInstances; ++i)
    {
        instances.push_back (std::make_unique<MetamorfoProcessor>());
        REQUIRE (tryPrepare (*instances.back(), Set::stereo(), Set::stereo(), 512));
    }

    std::atomic<int> mismatches { 0 };
    std::vector<std::thread> threads;

    for (int i = 0; i < numInstances; ++i)
    {
        threads.emplace_back ([&, i] {
            auto& p = *instances[static_cast<std::size_t> (i)];
            juce::AudioBuffer<float> buffer (2, 4096);
            juce::MidiBuffer midi;
            midi.ensureSize (256);
            IrregularBlocks blocks (static_cast<unsigned> (1000 + i));
            int position = 0;

            for (int b = 0; b < numBlocks; ++b)
            {
                midi.clear();
                if (b % 7 == 0)
                    midi.addEvent (juce::MidiMessage::noteOn (1, 40 + i, static_cast<juce::uint8> (100)), 0);
                if (b % 7 == 3)
                    midi.addEvent (juce::MidiMessage::noteOff (1, 40 + i), 0);

                const int size = blocks.next();
                mismatches += processAndCount (p, buffer, midi, size, position, i);
                position += size;
            }
        });
    }

    for (auto& t : threads)
        t.join();

    CHECK (mismatches.load() == 0);

    // Each instance only saw its own notes (no shared state between instances).
    for (int i = 0; i < numInstances; ++i)
        CHECK (instances[static_cast<std::size_t> (i)]->getMidiActivity().lastNote.load() == 40 + i);
}

TEST_CASE ("Sample rate and block size can change without releaseResources", "[host]")
{
    MetamorfoProcessor p;
    juce::AudioBuffer<float> buffer (2, 4096);
    juce::MidiBuffer midi;
    int position = 0;

    struct Config { double rate; int block; };
    for (const auto config : { Config { 44100.0, 256 }, Config { 96000.0, 1024 }, Config { 48000.0, 64 },
                               Config { 192000.0, 4096 }, Config { 44100.0, 16 } })
    {
        // Hold a note, then re-prepare: the note stack must be reset.
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (90)), 0);
        if (p.getPreparedBlockSize() > 0)
            processAndCount (p, buffer, midi, 32, position, 0);

        REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), config.block, config.rate));
        position = 0; // prepareToPlay clears all history: the stream restarts
        CHECK (p.getMidiActivity().heldNote.load() == -1);
        CHECK (p.getLatencySamples() == p.getEngineLatencySamples());
        CHECK (p.getPreparedSampleRate() == config.rate);

        midi.clear();
        int mismatches = 0;
        for (int b = 0; b < 20; ++b)
        {
            mismatches += processAndCount (p, buffer, midi, config.block, position, 0);
            position += config.block;
        }
        CHECK (mismatches == 0);
    }
}

TEST_CASE ("processBlock before prepareToPlay and with zero samples is safe", "[host]")
{
    MetamorfoProcessor p; // default layout: stereo -> stereo, never prepared
    juce::AudioBuffer<float> buffer (2, 4096);
    juce::MidiBuffer midi;
    midi.addEvent (juce::MidiMessage::noteOn (1, 64, static_cast<juce::uint8> (100)), 0);

    CHECK (processAndCount (p, buffer, midi, 128, 0, 0) == 0);
    CHECK (p.getMidiActivity().heldNote.load() == 64);

    juce::AudioBuffer<float> empty (2, 0);
    midi.clear();
    p.processBlock (empty, midi);

    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 512));
    CHECK (processAndCount (p, buffer, midi, 0, 0, 0) == 0);
    CHECK (processAndCount (p, buffer, midi, 512, 0, 0) == 0);
}

TEST_CASE ("State save/load and automation while audio is running", "[host][threads]")
{
    namespace id = metamorfo::params::id;
    MetamorfoProcessor p;
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 256));

    // A reference state with a non-default value that does not change the audio
    // (so the output can still be checked bit-exactly).
    auto* reference_ = p.getParameters().getParameter (id::midiReference);
    reference_->setValueNotifyingHost (reference_->convertTo0to1 (48.0f));
    juce::MemoryBlock reference;
    p.getStateInformation (reference);

    std::atomic<bool> running { true };
    std::atomic<int> mismatches { 0 };

    // Audio thread: processes continuously and automates `portamento` like a host would
    // (a parameter that does not change the audio without MIDI, so the output can be checked).
    std::thread audio ([&] {
        juce::AudioBuffer<float> buffer (2, 4096);
        juce::MidiBuffer midi;
        auto* automated = p.getParameters().getParameter (id::portamento);
        int position = 0, step = 0;
        while (running.load())
        {
            automated->setValueNotifyingHost (static_cast<float> (step++ % 100) / 100.0f);
            mismatches += processAndCount (p, buffer, midi, 256, position, 0);
            position += 256;
        }
    });

    {
        const StopAndJoin guard { running, audio }; // joins the audio thread at scope exit

        // Message thread: the host saves and restores the project state repeatedly.
        for (int i = 0; i < 200; ++i)
        {
            juce::MemoryBlock blob;
            p.getStateInformation (blob);
            REQUIRE (blob.getSize() > 0);
            p.setStateInformation (reference.getData(), static_cast<int> (reference.getSize()));
        }
    }

    CHECK (mismatches.load() == 0);
    CHECK (p.getParameters().getRawParameterValue (id::midiReference)->load() == 48.0f);
}

TEST_CASE ("Editor can be opened and closed repeatedly while audio is running", "[host][threads][ui]")
{
    if (! hasDisplay())
        SKIP ("No X display available");

    MetamorfoProcessor p;
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 128));

    std::atomic<bool> running { true };
    std::atomic<int> mismatches { 0 };

    std::thread audio ([&] {
        juce::AudioBuffer<float> buffer (2, 4096);
        juce::MidiBuffer midi;
        int position = 0, note = 0;
        while (running.load())
        {
            midi.clear();
            midi.addEvent (juce::MidiMessage::noteOn (1, 36 + (note++ % 48), static_cast<juce::uint8> (100)), 0);
            mismatches += processAndCount (p, buffer, midi, 128, position, 0);
            position += 128;
        }
    });

    {
        const StopAndJoin guard { running, audio }; // joins the audio thread at scope exit

        for (int i = 0; i < 50; ++i)
        {
            // The editor reads the MIDI atomics in its constructor while the audio thread writes them.
            std::unique_ptr<juce::AudioProcessorEditor> editor (p.createEditorAndMakeActive());
            REQUIRE (editor != nullptr);
            editor->setBounds (editor->getBounds());
            p.editorBeingDeleted (editor.get()); // what JUCE plugin wrappers do before deleting it
            editor.reset();
        }
    }

    CHECK (mismatches.load() == 0);
}

TEST_CASE ("MIDI events on block edges are tracked correctly", "[host][midi]")
{
    MetamorfoProcessor p;
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 64));
    const auto& activity = p.getMidiActivity();
    juce::AudioBuffer<float> buffer (2, 4096);
    juce::MidiBuffer midi;

    // Note-on on the last sample of a block, note-off on the first sample of the next.
    midi.addEvent (juce::MidiMessage::noteOn (1, 60, static_cast<juce::uint8> (100)), 63);
    processAndCount (p, buffer, midi, 64, 0, 0);
    CHECK (activity.heldNote.load() == 60);

    midi.clear();
    midi.addEvent (juce::MidiMessage::noteOn (1, 67, static_cast<juce::uint8> (100)), 0);
    midi.addEvent (juce::MidiMessage::noteOff (1, 67), 63);
    processAndCount (p, buffer, midi, 64, 64, 0);
    CHECK (activity.heldNote.load() == 60); // 67 pressed and released inside the block

    // One-sample blocks: every event lands on offset 0.
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), 1));
    const int sequence[] = { 48, 50, 52 };
    for (const int n : sequence)
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOn (1, n, static_cast<juce::uint8> (80)), 0);
        processAndCount (p, buffer, midi, 1, 0, 0);
        CHECK (activity.heldNote.load() == n);
    }
    for (const int n : { 52, 50 })
    {
        midi.clear();
        midi.addEvent (juce::MidiMessage::noteOff (1, n), 0);
        processAndCount (p, buffer, midi, 1, 0, 0);
    }
    CHECK (activity.heldNote.load() == 48);
}
