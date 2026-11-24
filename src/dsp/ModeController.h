#pragma once

#include <cmath>

namespace metamorfo::dsp
{

// Computes the pitch ratio (output f0 / input f0) the engine should apply to a
// voiced pitch mark, for the three modes of SPEC §4.2. Real-time safe; the only
// state is the quantiser's current note (hysteresis).
class ModeController
{
public:
    enum class Mode : int { transpose = 0, quantize = 1, robot = 2 };

    struct Settings
    {
        Mode mode = Mode::transpose;
        float knobSemitones = 0.0f;  // PITCH knob, -12..+12
        float midiNote = -1.0f;      // held MIDI note (fractional while gliding), < 0 = none
        int midiReference = 60;      // MIDI note meaning "no transposition"
    };

    static constexpr int kRobotBaseNote = 72; // one octave above middle C (SPEC §4.2)
    static constexpr float kQuantizeHysteresis = 0.15f; // semitones beyond the half-way point

    void reset() noexcept { hasNote_ = false; }

    // True when the settings leave the voice untouched (the engine can bypass exactly).
    static bool isIdentity (const Settings& s) noexcept
    {
        return s.mode == Mode::transpose && s.midiNote < 0.0f && s.knobSemitones == 0.0f;
    }

    // Transposition in semitones requested by the knob or by MIDI (relative to the reference).
    static float transposition (const Settings& s) noexcept
    {
        return s.midiNote >= 0.0f ? s.midiNote - static_cast<float> (s.midiReference) : s.knobSemitones;
    }

    // Target pitch ratio for a voiced mark with detected frequency `f0`.
    float ratioFor (const Settings& s, float f0) noexcept
    {
        if (f0 <= 0.0f)
            return 1.0f;

        switch (s.mode)
        {
            case Mode::transpose:
                return semitonesToRatio (transposition (s));

            case Mode::quantize:
            {
                const float detected = hzToMidi (f0);
                if (! hasNote_ || std::abs (detected - static_cast<float> (note_)) > 0.5f + kQuantizeHysteresis)
                {
                    note_ = static_cast<int> (std::lround (detected));
                    hasNote_ = true;
                }
                return midiToHz (static_cast<float> (note_) + transposition (s)) / f0;
            }

            case Mode::robot:
            {
                const float target = s.midiNote >= 0.0f ? s.midiNote
                                                        : static_cast<float> (kRobotBaseNote) + std::round (s.knobSemitones);
                return midiToHz (target) / f0;
            }
        }
        return 1.0f;
    }

    static float semitonesToRatio (float semitones) noexcept { return std::exp2 (semitones / 12.0f); }
    static float hzToMidi (float hz) noexcept { return 69.0f + 12.0f * std::log2 (hz / 440.0f); }
    static float midiToHz (float note) noexcept { return 440.0f * std::exp2 ((note - 69.0f) / 12.0f); }

private:
    int note_ = 0;
    bool hasNote_ = false;
};

} // namespace metamorfo::dsp
