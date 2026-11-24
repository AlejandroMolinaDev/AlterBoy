#pragma once

#include <atomic>
#include <cstdint>

#include <clap-juce-extensions/clap-juce-extensions.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/MidiPitchControl.h"
#include "dsp/PitchDetector.h"

namespace metamorfo
{

// Snapshot of MIDI activity, written by the audio thread and polled by the UI.
// Lock-free: individual relaxed atomics are enough for a diagnostic display.
struct MidiActivity
{
    std::atomic<int> lastNote { -1 };
    std::atomic<int> lastVelocity { 0 };
    std::atomic<int> lastChannel { 0 };
    std::atomic<int> heldNote { -1 };
    std::atomic<std::uint32_t> eventCount { 0 };
};

// Analysis snapshot for the note display, written by the audio thread.
struct AnalysisDisplay
{
    std::atomic<float> inputF0 { 0.0f };  // detected f0 in Hz, 0 when unvoiced
    std::atomic<float> outputF0 { 0.0f }; // f0 after processing, 0 when unvoiced
    std::atomic<float> confidence { 0.0f };
    std::atomic<bool> voiced { false };
};

class MetamorfoProcessor final : public juce::AudioProcessor,
                                 public clap_juce_extensions::clap_properties
{
public:
    static constexpr const char* kStateType = "MetamorfoState";
    static constexpr const char* kStateVersionProperty = "stateVersion";
    static constexpr int kStateVersion = 1;

    MetamorfoProcessor();
    ~MetamorfoProcessor() override = default;

    // AudioProcessor ---------------------------------------------------------
    void prepareToPlay (double sampleRate, int maximumExpectedSamplesPerBlock) override;
    void releaseResources() override;
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "Metamorfo"; }
    bool acceptsMidi() const override { return true; }
    bool producesMidi() const override { return false; }
    bool isMidiEffect() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return "Default"; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // Metamorfo --------------------------------------------------------------
    juce::AudioProcessorValueTreeState& getParameters() noexcept { return parameters_; }
    const MidiActivity& getMidiActivity() const noexcept { return midiActivity_; }
    const AnalysisDisplay& getAnalysisDisplay() const noexcept { return analysisDisplay_; }

    // Human-readable plugin format ("VST3", "CLAP", "AU", "Standalone", ...).
    juce::String getFormatName() const;

    // Engine latency in samples. Fixed for a given sample rate (0 in phase 1).
    int getEngineLatencySamples() const noexcept { return 0; }

    double getPreparedSampleRate() const noexcept { return preparedSampleRate_.load (std::memory_order_relaxed); }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize_.load (std::memory_order_relaxed); }

private:
    void handleMidi (const juce::MidiBuffer& midi) noexcept;
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;

    juce::AudioProcessorValueTreeState parameters_;
    dsp::MidiPitchControl midiPitch_;
    MidiActivity midiActivity_;
    dsp::PitchDetector pitchDetector_;
    AnalysisDisplay analysisDisplay_;

    std::atomic<double> preparedSampleRate_ { 0.0 };
    std::atomic<int> preparedBlockSize_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetamorfoProcessor)
};

} // namespace metamorfo
