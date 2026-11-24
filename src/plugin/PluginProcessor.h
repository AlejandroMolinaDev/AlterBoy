#pragma once

#include <array>
#include <atomic>
#include <cstdint>

#include <clap-juce-extensions/clap-juce-extensions.h>
#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/MidiPitchControl.h"
#include "dsp/PitchDetector.h"
#include "dsp/PsolaEngine.h"

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

    // Total latency in samples (engine: 2 x the 60 Hz period). Fixed per sample rate.
    int getEngineLatencySamples() const noexcept { return totalLatency_; }

    double getPreparedSampleRate() const noexcept { return preparedSampleRate_.load (std::memory_order_relaxed); }
    int getPreparedBlockSize() const noexcept { return preparedBlockSize_.load (std::memory_order_relaxed); }

private:
    void handleMidiMessage (const juce::MidiMessage& message) noexcept; // display / activity
    void applyMidiToEngine (const juce::MidiMessage& message) noexcept; // note stack (delayed)
    void processChunk (juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                       const juce::MidiBuffer& midi) noexcept;
    void renderSegment (juce::AudioBuffer<float>& buffer, int startSample, int numSamples) noexcept;
    void prepareEngine (double sampleRate, int maxBlock);
    [[nodiscard]] dsp::PsolaEngine::Controls readControls (float midiNote) const noexcept;

    juce::AudioProcessorValueTreeState parameters_;
    dsp::MidiPitchControl midiPitch_;    // drives the engine (events delayed to the synthesis horizon)
    dsp::MidiPitchControl displayNotes_; // what the display shows (events applied on reception)
    MidiActivity midiActivity_;
    dsp::PitchDetector pitchDetector_;
    dsp::PsolaEngine engine_;
    AnalysisDisplay analysisDisplay_;

    // Dry path delayed by exactly the reported latency (one ring per output channel).
    std::array<std::vector<float>, 2> dryDelay_;
    std::uint32_t dryMask_ = 0, dryWrite_ = 0;
    int totalLatency_ = 0;

    juce::SmoothedValue<float> mixSmoothed_ { 1.0f };

    // MIDI reaches the engine delayed by maxPeriod samples, the distance between the
    // input and the synthesis horizon, so pitch changes line up with the timeline.
    struct QueuedMidi
    {
        std::int64_t due = 0;
        juce::uint8 bytes[3] {};
        int size = 0;
    };
    std::array<QueuedMidi, 1024> midiQueue_ {};
    std::uint32_t midiQueueRead_ = 0, midiQueueWrite_ = 0;
    std::int64_t streamPosition_ = 0;
    int midiDelay_ = 0;

    // Cached parameter pointers (lock-free reads on the audio thread).
    std::atomic<float>* pitchParam_ = nullptr;
    std::atomic<float>* formantParam_ = nullptr;
    std::atomic<float>* linkParam_ = nullptr;
    std::atomic<float>* modeParam_ = nullptr;
    std::atomic<float>* driveParam_ = nullptr;
    std::atomic<float>* mixParam_ = nullptr;
    std::atomic<float>* midiReferenceParam_ = nullptr;
    std::atomic<float>* portamentoParam_ = nullptr;

    std::atomic<double> preparedSampleRate_ { 0.0 };
    std::atomic<int> preparedBlockSize_ { 0 };

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MetamorfoProcessor)
};

} // namespace metamorfo
