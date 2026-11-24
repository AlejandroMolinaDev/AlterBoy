// Tube drive (SPEC §4.3, §7): even harmonics dominate, level compensation, DRIVE 0
// transparent, oversampler behaviour.

#include "EngineTestUtils.h"

#include "dsp/TubeDrive.h"

using namespace metamorfo::test;
namespace id = metamorfo::params::id;

namespace
{
// Level (dBFS) of the harmonic k of f0, Goertzel over the steady part.
double harmonicDb (const std::vector<float>& x, double f0, int k, double from = 0.5)
{
    const auto start = static_cast<std::size_t> (from * kRate);
    const std::size_t n = x.size() - start;
    const double w = 2.0 * kPi * k * f0 / kRate;
    double re = 0.0, im = 0.0, windowSum = 0.0;
    for (std::size_t i = 0; i < n; ++i)
    {
        const double win = 0.5 - 0.5 * std::cos (2.0 * kPi * static_cast<double> (i) / static_cast<double> (n - 1));
        re += win * x[start + i] * std::cos (w * static_cast<double> (i));
        im -= win * x[start + i] * std::sin (w * static_cast<double> (i));
        windowSum += win;
    }
    return 20.0 * std::log10 (2.0 * std::sqrt (re * re + im * im) / windowSum + 1.0e-12);
}

double rmsDb (const std::vector<float>& x, double from = 0.5)
{
    double e = 0.0;
    const auto start = static_cast<std::size_t> (from * kRate);
    for (std::size_t i = start; i < x.size(); ++i)
        e += static_cast<double> (x[i]) * x[i];
    return 10.0 * std::log10 (e / static_cast<double> (x.size() - start) + 1.0e-20);
}
} // namespace

TEST_CASE ("Drive: H2 > H3 across levels and settings (SPEC 7)", "[drive]")
{
    for (const float level : { 0.06f, 0.25f, 0.5f })
        for (const float drive : { 3.0f, 6.0f, 9.0f, 12.0f })
        {
            const auto r = render (sine (220.0, 1.5, kRate, level), { { id::drive, drive } });
            const double h1 = harmonicDb (r.out, 220.0, 1), h2 = harmonicDb (r.out, 220.0, 2), h3 = harmonicDb (r.out, 220.0, 3);
            INFO ("level " << level << " drive " << drive << ": H2 " << h2 - h1 << " dB, H3 " << h3 - h1 << " dB");
            CHECK (h2 > h3);
            CHECK (h2 - h1 > -60.0); // audible character
        }
}

TEST_CASE ("Drive: level compensation within +-1.5 dB (SPEC 4.3)", "[drive]")
{
    for (const float level : { 0.06f, 0.25f, 0.5f })
    {
        const auto input = syntheticVoice (180.0, 2.0, kRate, 20.0, 5.5, level);
        const double base = rmsDb (render (input, {}).out, 1.0);
        for (const float drive : { 3.0f, 6.0f, 9.0f, 12.0f })
        {
            const double d = rmsDb (render (input, { { id::drive, drive } }).out, 1.0) - base;
            INFO ("level " << level << " drive " << drive << ": " << d << " dB");
            CHECK (std::abs (d) <= 1.5);
        }
    }
}

TEST_CASE ("Drive 0 is transparent (bit-exact delayed input)", "[drive]")
{
    metamorfo::dsp::TubeDrive d;
    d.prepare (kRate);
    const auto x = syntheticVoice (200.0, 0.3, kRate);
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        const float y = d.process (x[i], 0.0f);
        const float expected = i >= static_cast<std::size_t> (metamorfo::dsp::TubeDrive::kLatency)
                                   ? x[i - static_cast<std::size_t> (metamorfo::dsp::TubeDrive::kLatency)] : 0.0f;
        REQUIRE (y == expected);
    }
}

TEST_CASE ("Drive output stays aligned with the dry path (mix 50 % shows no comb)", "[drive]")
{
    // A low sine through drive 1 mixed 50/50 with dry: a misalignment would cancel part of it.
    const auto r = render (sine (150.0, 1.5, kRate, 0.1f), { { id::drive, 1.0f }, { id::mix, 50.0f } });
    CHECK (std::abs (harmonicDb (r.out, 150.0, 1) - 20.0 * std::log10 (0.1)) < 0.5);
}

TEST_CASE ("HalfBand2x: unity pass band, images rejected", "[drive][oversampling]")
{
    for (const double f : { 100.0, 1000.0, 8000.0, 18000.0 })
    {
        metamorfo::dsp::HalfBand2x os;
        const auto x = sine (f, 0.3, kRate, 0.5f);
        std::vector<float> up;
        std::vector<float> back;
        for (const float v : x)
        {
            float e = 0.0f, o = 0.0f;
            os.upsample (v, e, o);
            up.push_back (e);
            up.push_back (o);
            back.push_back (os.downsample (e, o));
        }
        // Round-trip level by RMS (sample peaks of a high sine fall between samples).
        double eIn = 0.0, eOut = 0.0;
        for (std::size_t i = back.size() / 2; i < back.size(); ++i)
        {
            eIn += static_cast<double> (x[i]) * x[i];
            eOut += static_cast<double> (back[i]) * back[i];
        }
        const double gainDb = 10.0 * std::log10 (eOut / eIn);
        INFO ("f " << f << " round-trip gain " << gainDb << " dB");
        CHECK (std::abs (gainDb) < 0.2);

        // Image at (2*sr/2) - f on the 2x stream must be far below the signal.
        double reS = 0.0, imS = 0.0, reI = 0.0, imI = 0.0;
        const double rate2x = 2.0 * kRate;
        for (std::size_t i = up.size() / 2; i < up.size(); ++i)
        {
            const double t = static_cast<double> (i) / rate2x;
            reS += up[i] * std::cos (2.0 * kPi * f * t);
            imS += up[i] * std::sin (2.0 * kPi * f * t);
            reI += up[i] * std::cos (2.0 * kPi * (kRate - f) * t);
            imI += up[i] * std::sin (2.0 * kPi * (kRate - f) * t);
        }
        const double rejection = 10.0 * std::log10 ((reI * reI + imI * imI) / (reS * reS + imS * imS));
        INFO ("image rejection " << rejection << " dB");
        CHECK (rejection < -70.0);
    }
}
