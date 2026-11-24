// Quantize, Robot, MIDI control and portamento (SPEC §4.2, §7).

#include "EngineTestUtils.h"

using namespace metamorfo::test;
namespace id = metamorfo::params::id;

namespace
{
constexpr float kQuantize = 1.0f, kRobot = 2.0f;

// Median f0 measured on the output between two times (seconds).
double f0Between (const Rendered& r, double from, double to) { return medianF0 (r.out, from, to); }
} // namespace

TEST_CASE ("Quantize: detuned input lands on the nearest semitone (SPEC 7)", "[modes][quantize]")
{
    for (const double detune : { 35.0, -40.0, 20.0, -15.0 })
    {
        const double a3 = midiToHz (57.0);
        const auto voice = syntheticVoice (a3 * std::pow (2.0, detune / 1200.0), 1.5, kRate);
        const auto r = render (voice, { { id::mode, kQuantize } });
        const double f = medianF0 (r.out);
        INFO ("detune " << detune << " -> " << centsBetween (f, a3) << " cents from A3");
        CHECK (std::abs (centsBetween (f, a3)) < 5.0);
    }
}

TEST_CASE ("Quantize: flattens vibrato onto the note (hard-tune)", "[modes][quantize]")
{
    const auto voice = syntheticVoice (midiToHz (60.0) * std::pow (2.0, 25.0 / 1200.0), 2.0, kRate, 30.0);
    const auto r = render (voice, { { id::mode, kQuantize } });
    CHECK (std::abs (centsBetween (medianF0 (r.out), midiToHz (60.0))) < 5.0);
}

TEST_CASE ("Quantize then transpose with PITCH", "[modes][quantize]")
{
    const auto voice = syntheticVoice (midiToHz (57.0) * std::pow (2.0, 30.0 / 1200.0), 1.5, kRate);
    const auto r = render (voice, { { id::mode, kQuantize }, { id::pitch, 3.0f } });
    CHECK (std::abs (centsBetween (medianF0 (r.out), midiToHz (60.0))) < 5.0);
}

TEST_CASE ("Robot: PITCH 0 sings MIDI 72, PITCH moves in whole semitones (SPEC 7)", "[modes][robot]")
{
    const auto voice = syntheticVoice (180.0, 1.5, kRate, 40.0);
    {
        const auto r = render (voice, { { id::mode, kRobot } });
        CHECK (std::abs (centsBetween (medianF0 (r.out), midiToHz (72.0))) < 5.0);
    }
    {
        const auto r = render (voice, { { id::mode, kRobot }, { id::pitch, -5.3f } }); // rounds to -5
        CHECK (std::abs (centsBetween (medianF0 (r.out), midiToHz (67.0))) < 5.0);
    }
}

TEST_CASE ("Robot: the output follows the received MIDI notes (SPEC 7)", "[modes][robot][midi]")
{
    const auto voice = syntheticVoice (200.0, 2.4, kRate, 30.0);
    const std::vector<Note> melody { { 0.0, 0.8, 64 }, { 0.8, 0.8, 67 }, { 1.6, 0.8, 55 } };
    const auto r = render (voice, { { id::mode, kRobot } }, 256, melody);
    CHECK (std::abs (centsBetween (f0Between (r, 0.3, 0.75), midiToHz (64.0))) < 5.0);
    CHECK (std::abs (centsBetween (f0Between (r, 1.1, 1.55), midiToHz (67.0))) < 5.0);
    CHECK (std::abs (centsBetween (f0Between (r, 1.9, 2.35), midiToHz (55.0))) < 5.0);
}

TEST_CASE ("MIDI in Transpose: held note sets the interval from the reference", "[modes][midi]")
{
    const auto voice = syntheticVoice (220.0, 2.0, kRate);
    // Reference C4 (60): G4 held -> +7 st; then released -> back to the knob (0).
    const auto r = render (voice, { { id::mode, 0.0f } }, 256, { { 0.0, 1.0, 67 } });
    CHECK (std::abs (centsBetween (f0Between (r, 0.3, 0.95), 220.0 * std::pow (2.0, 7.0 / 12.0))) < 5.0);
    CHECK (std::abs (centsBetween (f0Between (r, 1.4, 1.95), 220.0)) < 5.0);
}

TEST_CASE ("MIDI reference is configurable", "[modes][midi]")
{
    const auto voice = syntheticVoice (220.0, 1.5, kRate);
    const auto r = render (voice, { { id::mode, 0.0f }, { id::midiReference, 48.0f } }, 256, { { 0.0, 1.5, 52 } });
    CHECK (std::abs (centsBetween (medianF0 (r.out), 220.0 * std::pow (2.0, 4.0 / 12.0))) < 5.0);
}

TEST_CASE ("MIDI in Quantize: quantised note transposed by the held interval", "[modes][midi][quantize]")
{
    const auto voice = syntheticVoice (midiToHz (57.0) * std::pow (2.0, -30.0 / 1200.0), 1.5, kRate);
    const auto r = render (voice, { { id::mode, kQuantize } }, 256, { { 0.0, 1.5, 64 } }); // +4 st
    CHECK (std::abs (centsBetween (medianF0 (r.out), midiToHz (61.0))) < 5.0);
}

TEST_CASE ("Last-note priority drives the engine", "[modes][midi]")
{
    const auto voice = syntheticVoice (200.0, 2.0, kRate);
    // 60 held all along, 72 pressed on top between 0.6 and 1.3 s.
    const auto r = render (voice, { { id::mode, kRobot } }, 256, { { 0.0, 2.0, 60 }, { 0.6, 0.7, 72 } });
    CHECK (std::abs (centsBetween (f0Between (r, 0.2, 0.55), midiToHz (60.0))) < 5.0);
    CHECK (std::abs (centsBetween (f0Between (r, 0.9, 1.25), midiToHz (72.0))) < 5.0);
    CHECK (std::abs (centsBetween (f0Between (r, 1.6, 1.95), midiToHz (60.0))) < 5.0);
}

TEST_CASE ("Portamento glides between MIDI notes", "[modes][midi][portamento]")
{
    const auto voice = syntheticVoice (200.0, 2.0, kRate);
    const std::vector<Note> legato { { 0.0, 1.0, 60 }, { 0.9, 1.1, 72 } }; // overlapping -> glide
    const auto glide = render (voice, { { id::mode, kRobot }, { id::portamento, 200.0f } }, 256, legato);
    const auto jump = render (voice, { { id::mode, kRobot }, { id::portamento, 0.0f } }, 256, legato);

    // Mid-glide (0.9 s + ~100 ms) the pitch is between the notes; without portamento it is already there.
    const double midGlide = f0Between (glide, 0.97, 1.03);
    const double midJump = f0Between (jump, 0.97, 1.03);
    INFO ("glide " << midGlide << " Hz, jump " << midJump << " Hz");
    CHECK (midGlide > midiToHz (61.0));
    CHECK (midGlide < midiToHz (71.0));
    CHECK (std::abs (centsBetween (midJump, midiToHz (72.0))) < 15.0);
    // After the glide both reach the target.
    CHECK (std::abs (centsBetween (f0Between (glide, 1.4, 1.9), midiToHz (72.0))) < 5.0);
}

TEST_CASE ("Robot does not touch unvoiced segments", "[modes][robot][voicing]")
{
    const auto noise = whiteNoise (1.0, kRate, 0.2f);
    const auto r = render (noise, { { id::mode, kRobot } });
    // Unvoiced input is reconstructed (pitch ratio 1): the level is preserved.
    double eIn = 0.0, eOut = 0.0;
    for (std::size_t i = static_cast<std::size_t> (0.2 * kRate); i < noise.size(); ++i)
    {
        eIn += static_cast<double> (noise[i]) * noise[i];
        eOut += static_cast<double> (r.out[i]) * r.out[i];
    }
    CHECK (std::abs (10.0 * std::log10 (eOut / eIn)) < 2.0);
}
