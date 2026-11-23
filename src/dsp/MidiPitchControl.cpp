#include "dsp/MidiPitchControl.h"

namespace metamorfo::dsp
{

void MidiPitchControl::reset() noexcept
{
    count_ = 0;
}

void MidiPitchControl::noteOn (int note, int velocity) noexcept
{
    if (note < 0 || note >= kMaxNotes)
        return;

    if (velocity <= 0)
    {
        noteOff (note);
        return;
    }

    // Re-pressing a held note moves it to the top of the priority stack.
    remove (note);

    // Each note appears at most once, so count_ < kMaxNotes here.
    const auto index = static_cast<std::size_t> (count_);
    notes_[index] = static_cast<std::uint8_t> (note);
    velocities_[index] = static_cast<std::uint8_t> (velocity > 127 ? 127 : velocity);
    ++count_;
}

void MidiPitchControl::noteOff (int note) noexcept
{
    if (note < 0 || note >= kMaxNotes)
        return;

    remove (note);
}

int MidiPitchControl::activeNote() const noexcept
{
    return count_ > 0 ? static_cast<int> (notes_[static_cast<std::size_t> (count_ - 1)]) : kNoNote;
}

int MidiPitchControl::activeVelocity() const noexcept
{
    return count_ > 0 ? static_cast<int> (velocities_[static_cast<std::size_t> (count_ - 1)]) : 0;
}

void MidiPitchControl::remove (int note) noexcept
{
    for (int i = 0; i < count_; ++i)
    {
        if (static_cast<int> (notes_[static_cast<std::size_t> (i)]) != note)
            continue;

        for (int j = i; j < count_ - 1; ++j)
        {
            notes_[static_cast<std::size_t> (j)] = notes_[static_cast<std::size_t> (j + 1)];
            velocities_[static_cast<std::size_t> (j)] = velocities_[static_cast<std::size_t> (j + 1)];
        }

        --count_;
        return; // a note is stored at most once
    }
}

} // namespace metamorfo::dsp
