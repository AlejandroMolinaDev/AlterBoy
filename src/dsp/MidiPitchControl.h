#pragma once

#include <array>
#include <cstdint>

namespace metamorfo::dsp
{

// Tracks held MIDI notes with last-note priority. Real-time safe: fixed-size
// storage, no allocation, no locks. Owned and driven by the audio thread only.
//
// Phase 1: note tracking only. Phase 4 adds target-pitch computation per mode
// and portamento on top of this class.
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

private:
    void remove (int note) noexcept;

    // Held notes in press order; the last entry has priority.
    std::array<std::uint8_t, kMaxNotes> notes_ {};
    std::array<std::uint8_t, kMaxNotes> velocities_ {};
    int count_ = 0;
};

} // namespace metamorfo::dsp
