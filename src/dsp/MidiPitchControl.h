#pragma once

#include <array>
#include <cstdint>

namespace metamorfo::dsp
{

// Tracks held MIDI notes with last-note priority. Real-time safe: fixed-size
// storage, no allocation, no locks. Owned and driven by the audio thread only.
//
// Also provides the gliding note used by the engine: when the priority note
// changes while another is sounding, it moves linearly (in semitones) to the new
// note over the portamento time (0 = jump). The first note after silence jumps.
class MidiPitchControl
{
public:
    static constexpr int kNoNote = -1;
    static constexpr int kMaxNotes = 128;

    void reset() noexcept;

    // velocity == 0 is treated as note-off (running-status convention).
    void noteOn (int note, int velocity) noexcept;
    void noteOff (int note) noexcept;

    [[nodiscard]] bool hasActiveNote() const noexcept { return count_ > 0; }
    [[nodiscard]] int activeNote() const noexcept;     // kNoNote when nothing is held
    [[nodiscard]] int activeVelocity() const noexcept; // 0 when nothing is held
    [[nodiscard]] int numHeldNotes() const noexcept { return count_; }

    // Portamento -----------------------------------------------------------------
    void setPortamento (float milliseconds, double sampleRate) noexcept;

    // Advances the glide by `numSamples` and returns the current (fractional) note,
    // or -1 when no note is held.
    float advance (int numSamples) noexcept;
    [[nodiscard]] float glidingNote() const noexcept { return count_ > 0 ? glideNote_ : -1.0f; }

private:
    void remove (int note) noexcept;

    // Held notes in press order; the last entry has priority.
    std::array<std::uint8_t, kMaxNotes> notes_ {};
    std::array<std::uint8_t, kMaxNotes> velocities_ {};
    int count_ = 0;

    float glideNote_ = -1.0f;
    float glideSamples_ = 0.0f; // portamento time in samples
    float glideStep_ = 0.0f;    // semitones per sample for the current glide
    int glideTarget_ = kNoNote;
};

} // namespace metamorfo::dsp
