#include <catch2/catch_test_macros.hpp>

#include "dsp/MidiPitchControl.h"

using metamorfo::dsp::MidiPitchControl;

TEST_CASE ("MidiPitchControl starts empty", "[midi]")
{
    MidiPitchControl control;
    CHECK_FALSE (control.hasActiveNote());
    CHECK (control.activeNote() == MidiPitchControl::kNoNote);
    CHECK (control.activeVelocity() == 0);
    CHECK (control.numHeldNotes() == 0);
}

TEST_CASE ("MidiPitchControl uses last-note priority", "[midi]")
{
    MidiPitchControl control;
    control.noteOn (60, 100);
    control.noteOn (64, 90);
    control.noteOn (67, 80);
    CHECK (control.activeNote() == 67);
    CHECK (control.activeVelocity() == 80);

    SECTION ("releasing the top note falls back to the previous one")
    {
        control.noteOff (67);
        CHECK (control.activeNote() == 64);
        CHECK (control.activeVelocity() == 90);
    }

    SECTION ("releasing a lower note keeps the top note")
    {
        control.noteOff (60);
        CHECK (control.activeNote() == 67);
        CHECK (control.numHeldNotes() == 2);
    }

    SECTION ("re-pressing a held note moves it to the top")
    {
        control.noteOn (60, 50);
        CHECK (control.activeNote() == 60);
        CHECK (control.activeVelocity() == 50);
        CHECK (control.numHeldNotes() == 3);
    }

    SECTION ("releasing everything leaves no active note")
    {
        control.noteOff (60);
        control.noteOff (64);
        control.noteOff (67);
        CHECK_FALSE (control.hasActiveNote());
        CHECK (control.activeNote() == MidiPitchControl::kNoNote);
    }

    SECTION ("reset clears all held notes")
    {
        control.reset();
        CHECK_FALSE (control.hasActiveNote());
    }
}

TEST_CASE ("MidiPitchControl treats velocity 0 as note-off", "[midi]")
{
    MidiPitchControl control;
    control.noteOn (60, 100);
    control.noteOn (62, 100);
    control.noteOn (62, 0);
    CHECK (control.activeNote() == 60);
    CHECK (control.numHeldNotes() == 1);
}

TEST_CASE ("MidiPitchControl ignores out-of-range notes and unknown note-offs", "[midi]")
{
    MidiPitchControl control;
    control.noteOn (-1, 100);
    control.noteOn (128, 100);
    control.noteOff (70);
    CHECK_FALSE (control.hasActiveNote());

    control.noteOn (70, 200); // velocity clamped
    CHECK (control.activeVelocity() == 127);
}

TEST_CASE ("MidiPitchControl holds all 128 notes without overflow", "[midi]")
{
    MidiPitchControl control;
    for (int n = 0; n < 128; ++n)
        control.noteOn (n, 1 + n % 127);

    CHECK (control.numHeldNotes() == 128);
    CHECK (control.activeNote() == 127);

    // Pressing again must not grow the stack.
    control.noteOn (5, 64);
    CHECK (control.numHeldNotes() == 128);
    CHECK (control.activeNote() == 5);

    for (int n = 0; n < 128; ++n)
        control.noteOff (n);

    CHECK (control.numHeldNotes() == 0);
}
