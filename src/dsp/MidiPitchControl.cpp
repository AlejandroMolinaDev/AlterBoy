#include "dsp/MidiPitchControl.h"

#include <algorithm>
#include <cmath>

namespace metamorfo::dsp
{

void MidiPitchControl::reset() noexcept
{
    count_ = 0;
    glideNote_ = -1.0f;
    glideTarget_ = kNoNote;
    glideStep_ = 0.0f;
}

void MidiPitchControl::setPortamento (float milliseconds, double sampleRate) noexcept
{
    glideSamples_ = static_cast<float> (std::max (0.0, static_cast<double> (milliseconds)) * 0.001 * sampleRate);
}

float MidiPitchControl::advance (int numSamples) noexcept
{
    const int target = activeNote();
    if (target == kNoNote)
    {
        glideNote_ = -1.0f;
        glideTarget_ = kNoNote;
        return -1.0f;
    }

    if (target != glideTarget_)
    {
        if (glideNote_ < 0.0f || glideSamples_ < 1.0f)
        {
            glideNote_ = static_cast<float> (target); // first note after silence, or no portamento
            glideStep_ = 0.0f;
        }
        else
        {
            glideStep_ = std::abs (static_cast<float> (target) - glideNote_) / glideSamples_;
        }
        glideTarget_ = target;
    }

    const float t = static_cast<float> (target);
    const float delta = glideStep_ * static_cast<float> (numSamples);
    if (glideNote_ < t)
        glideNote_ = std::min (t, glideNote_ + delta);
    else if (glideNote_ > t)
        glideNote_ = std::max (t, glideNote_ - delta);
    return glideNote_;
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
