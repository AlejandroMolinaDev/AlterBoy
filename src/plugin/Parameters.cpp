#include "plugin/Parameters.h"

namespace metamorfo::params
{
namespace
{
juce::ParameterID pid (const char* name)
{
    return juce::ParameterID { name, kVersionHint };
}

juce::String formatSigned (float value, int decimals, const juce::String& suffix)
{
    const auto rounded = juce::String (value, decimals);
    const auto sign = value > 0.0f && rounded != juce::String (0.0f, decimals) ? "+" : "";
    return sign + rounded + suffix;
}

juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4) + " (" + juce::String (midiNote) + ")";
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    const NormalisableRange<float> semitoneRange { -kPitchRange, kPitchRange, 0.1f };

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::pitch), "Pitch", semitoneRange, 0.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("st")
            .withStringFromValueFunction ([] (float v, int) { return formatSigned (v, 1, " st"); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::formant), "Formant", NormalisableRange<float> { -kFormantRange, kFormantRange, 0.1f }, 0.0f,
        AudioParameterFloatAttributes {}
            .withStringFromValueFunction ([] (float v, int) { return formatSigned (v, 1, {}); })));

    layout.add (std::make_unique<AudioParameterBool> (pid (id::link), "Link", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (id::mode), "Mode", StringArray { "Transponer", "Cuantizar", "Robot" },
        static_cast<int> (Mode::transpose)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::drive), "Drive", NormalisableRange<float> { 0.0f, kDriveMax, 0.01f }, 0.0f,
        AudioParameterFloatAttributes {}
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::mix), "Mix", NormalisableRange<float> { 0.0f, 100.0f, 0.1f }, 100.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; })));

    layout.add (std::make_unique<AudioParameterInt> (
        pid (id::midiReference), "MIDI Reference", 0, 127, kDefaultMidiReference,
        AudioParameterIntAttributes {}
            .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
            .withValueFromStringFunction ([] (const String& text) {
                // Accepts "C3 (60)" as produced above, or a plain MIDI number.
                const auto inner = text.contains ("(") ? text.fromFirstOccurrenceOf ("(", false, false) : text;
                return jlimit (0, 127, inner.getIntValue());
            })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::portamento), "Portamento", NormalisableRange<float> { 0.0f, kPortamentoMaxMs, 0.1f }, 0.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })));

    return layout;
}

} // namespace metamorfo::params
