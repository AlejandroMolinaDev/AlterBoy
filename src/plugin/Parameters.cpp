#include "plugin/Parameters.h"

#include <cmath>
#include <functional>

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

// Linear range snapped to exact decimal steps. JUCE's built-in interval snapping
// computes start + interval * n in float, which leaves platform-dependent residues
// (e.g. 1.79e-7 instead of 0 on arm64). Rounding in decimal units keeps 0 exactly
// 0 and every step identical on all platforms.
juce::NormalisableRange<float> steppedRange (float minimum, float maximum, float step)
{
    const double stepsPerUnit = std::round (1.0 / static_cast<double> (step)); // 10 or 100

    juce::NormalisableRange<float> range {
        minimum, maximum,
        [] (float start, float end, float normalised) { return start + normalised * (end - start); },
        [] (float start, float end, float value) { return (value - start) / (end - start); },
        [stepsPerUnit] (float start, float end, float value) {
            const auto snapped = std::round (static_cast<double> (value) * stepsPerUnit) / stepsPerUnit;
            return juce::jlimit (start, end, static_cast<float> (snapped));
        }
    };
    range.interval = step; // lets hosts know the number of steps
    return range;
}

// On/off parameter that snaps its value. juce::AudioParameterBool stores the raw
// normalised value (e.g. 0.37) and reports it back, but its saved state is the snapped
// 0/1: a host that sets 0.37 and compares after a state round-trip sees a different
// value (pluginval --randomise). Same ID, position, flags and On/Off texts as before.
class SnappingBoolParameter final : public juce::AudioParameterFloat
{
public:
    SnappingBoolParameter (const juce::ParameterID& parameterId, const juce::String& paramName, bool defaultValue)
        : juce::AudioParameterFloat (
              parameterId, paramName, juce::NormalisableRange<float> (0.0f, 1.0f, 1.0f), defaultValue ? 1.0f : 0.0f,
              juce::AudioParameterFloatAttributes {}
                  .withStringFromValueFunction ([] (float v, int) { return v >= 0.5f ? juce::String ("On") : juce::String ("Off"); })
                  .withValueFromStringFunction ([] (const juce::String& text) {
                      const auto t = text.trim().toLowerCase();
                      if (t == "on" || t == "true" || t == "yes" || t == "si" || t == juce::String::fromUTF8 ("sí"))
                          return 1.0f;
                      float v = 0.0f;
                      return parseNumber (t, v) && v >= 0.5f ? 1.0f : 0.0f;
                  }))
    {
    }

    bool isBoolean() const override { return true; }
    bool isDiscrete() const override { return true; }
    int getNumSteps() const override { return 2; } // VST3 stepCount 1, as AudioParameterBool
};

juce::String noteName (int midiNote)
{
    return juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4) + " (" + juce::String (midiNote) + ")";
}
} // namespace

bool parseNumber (const juce::String& text, float& result)
{
    // Accepts what users type in a Spanish UI: "3,5", "−3,5" (U+2212, as the UI scale
    // shows it), "+7 st", " 37,5 % ". Rejects text without a number or with extra digits
    // after the unit ("abc", "", "1.2.3", "3 st 4").
    const auto normalised = text.trim()
                                .replace (juce::String::charToString (static_cast<juce::juce_wchar> (0x2212)), "-")
                                .replace (juce::String::charToString (static_cast<juce::juce_wchar> (0x2013)), "-")
                                .replaceCharacter (',', '.');
    auto p = normalised.getCharPointer();
    juce::String number;
    if (*p == '+' || *p == '-')
        number += juce::String::charToString (p.getAndAdvance());
    while (*p == ' ')
        ++p;

    int digits = 0;
    bool point = false;
    for (; ! p.isEmpty(); ++p)
    {
        const auto c = *p;
        if (c >= '0' && c <= '9')
            ++digits;
        else if (c == '.' && ! point)
            point = true;
        else
            break;
        number += juce::String::charToString (c);
    }
    if (digits == 0)
        return false;
    for (; ! p.isEmpty(); ++p) // only a unit / spaces may follow
        if ((*p >= '0' && *p <= '9') || *p == '.')
            return false;

    const auto value = number.getDoubleValue();
    if (! std::isfinite (value))
        return false;
    result = static_cast<float> (value);
    return true;
}

namespace
{
// Value-from-text for float parameters: parseNumber, or the parameter default when the
// text is not a number (the editor validates first and then keeps the current value).
std::function<float (const juce::String&)> parseOr (float fallback)
{
    return [fallback] (const juce::String& text) {
        float v = fallback;
        return parseNumber (text, v) ? v : fallback;
    };
}
} // namespace

juce::AudioProcessorValueTreeState::ParameterLayout createLayout()
{
    using namespace juce;

    AudioProcessorValueTreeState::ParameterLayout layout;

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::pitch), "Pitch", steppedRange (-kPitchRange, kPitchRange, 0.1f), 0.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("st")
            .withStringFromValueFunction ([] (float v, int) { return formatSigned (v, 1, " st"); })
            .withValueFromStringFunction (parseOr (0.0f))));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::formant), "Formant", steppedRange (-kFormantRange, kFormantRange, 0.1f), 0.0f,
        AudioParameterFloatAttributes {}
            .withStringFromValueFunction ([] (float v, int) { return formatSigned (v, 1, {}); })
            .withValueFromStringFunction (parseOr (0.0f))));

    layout.add (std::make_unique<SnappingBoolParameter> (pid (id::link), "Link", false));

    layout.add (std::make_unique<AudioParameterChoice> (
        pid (id::mode), "Mode", StringArray { "Transponer", "Cuantizar", "Robot" },
        static_cast<int> (Mode::transpose)));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::drive), "Drive", steppedRange (0.0f, kDriveMax, 0.01f), 0.0f,
        AudioParameterFloatAttributes {}
            .withStringFromValueFunction ([] (float v, int) { return String (v, 1); })
            .withValueFromStringFunction (parseOr (0.0f))));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::mix), "Mix", steppedRange (0.0f, 100.0f, 0.1f), 100.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("%")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " %"; })
            .withValueFromStringFunction (parseOr (100.0f))));

    layout.add (std::make_unique<AudioParameterInt> (
        pid (id::midiReference), "MIDI Reference", 0, 127, kDefaultMidiReference,
        AudioParameterIntAttributes {}
            .withStringFromValueFunction ([] (int v, int) { return noteName (v); })
            .withValueFromStringFunction ([] (const String& text) {
                // Accepts "C3 (60)" as produced above, or a plain MIDI number.
                const auto inner = text.contains ("(") ? text.fromFirstOccurrenceOf ("(", false, false).upToFirstOccurrenceOf (")", false, false)
                                                       : text;
                float v = static_cast<float> (kDefaultMidiReference);
                parseNumber (inner, v);
                return jlimit (0, 127, static_cast<int> (std::lround (v)));
            })));

    layout.add (std::make_unique<AudioParameterFloat> (
        pid (id::portamento), "Portamento", steppedRange (0.0f, kPortamentoMaxMs, 0.1f), 0.0f,
        AudioParameterFloatAttributes {}
            .withLabel ("ms")
            .withStringFromValueFunction ([] (float v, int) { return String (v, 0) + " ms"; })
            .withValueFromStringFunction (parseOr (0.0f))));

    return layout;
}

} // namespace metamorfo::params
