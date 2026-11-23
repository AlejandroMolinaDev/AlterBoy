#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace metamorfo::params
{

// Host-facing parameter contract. The ORDER of createLayout() and every ID below
// are frozen: FL Studio stores automation by parameter index and every host by ID.
// New parameters may only be appended at the end, with a higher version hint.
inline constexpr int kVersionHint = 1;

namespace id
{
inline constexpr const char* pitch = "pitch";
inline constexpr const char* formant = "formant";
inline constexpr const char* link = "link";
inline constexpr const char* mode = "mode";
inline constexpr const char* drive = "drive";
inline constexpr const char* mix = "mix";
inline constexpr const char* midiReference = "midiReference";
inline constexpr const char* portamento = "portamento";
} // namespace id

enum class Mode : int
{
    transpose = 0,
    quantize = 1,
    robot = 2
};

inline constexpr float kPitchRange = 12.0f;
inline constexpr float kFormantRange = 12.0f;
inline constexpr float kDriveMax = 12.0f;
inline constexpr float kPortamentoMaxMs = 200.0f;
inline constexpr int kDefaultMidiReference = 60; // middle C

juce::AudioProcessorValueTreeState::ParameterLayout createLayout();

// Number of parameters in the frozen contract (used by tests).
inline constexpr int kNumParameters = 8;

} // namespace metamorfo::params
