#pragma once

// Helpers for engine/mode tests: render a mono signal through the full processor
// (stereo, optional MIDI notes) and measure f0, spectral centroid and clicks.

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <map>
#include <string>
#include <vector>

#include "dsp/PitchDetector.h"
#include "plugin/Parameters.h"
#include "plugin/PluginProcessor.h"

#include "TestHelpers.h"
#include "TestSignals.h"

namespace metamorfo::test
{
using metamorfo::MetamorfoProcessor;
constexpr double kRate = 48000.0;

struct Rendered
{
    std::vector<float> out; // channel 0, latency removed
    int latency = 0;
};

struct Note
{
    double start = 0.0, duration = 0.0; // seconds
    int note = 60, velocity = 100;
};

inline Rendered render (const std::vector<float>& input, const std::map<std::string, float>& params, int block = 256,
                        const std::vector<Note>& notes = {})
{
    MetamorfoProcessor p;
    for (const auto& [name, value] : params)
    {
        auto* param = p.getParameters().getParameter (name);
        param->setValueNotifyingHost (param->convertTo0to1 (value));
    }
    REQUIRE (tryPrepare (p, Set::stereo(), Set::stereo(), block, kRate));

    Rendered r;
    r.latency = p.getLatencySamples();
    const int total = static_cast<int> (input.size()) + r.latency;
    std::vector<float> out;
    out.reserve (static_cast<std::size_t> (total));
    juce::AudioBuffer<float> buffer (2, block);
    juce::MidiBuffer midi;
    for (int pos = 0; pos < total; pos += block)
    {
        const int n = std::min (block, total - pos);
        midi.clear();
        for (const auto& note : notes)
        {
            const auto on = static_cast<int> (note.start * kRate), off = static_cast<int> ((note.start + note.duration) * kRate);
            if (on >= pos && on < pos + n)
                midi.addEvent (juce::MidiMessage::noteOn (1, note.note, static_cast<juce::uint8> (note.velocity)), on - pos);
            if (off >= pos && off < pos + n)
                midi.addEvent (juce::MidiMessage::noteOff (1, note.note), off - pos);
        }
        buffer.setSize (2, n, false, false, true);
        for (int i = 0; i < n; ++i)
        {
            const auto idx = static_cast<std::size_t> (pos + i);
            const float x = idx < input.size() ? input[idx] : 0.0f;
            buffer.setSample (0, i, x);
            buffer.setSample (1, i, x);
        }
        p.processBlock (buffer, midi);
        for (int i = 0; i < n; ++i)
            out.push_back (buffer.getSample (0, i));
    }
    r.out.assign (out.begin() + r.latency, out.end());
    return r;
}

// Median f0 of the voiced estimates between `from` and `to` seconds.
inline double medianF0 (const std::vector<float>& x, double from = 0.3, double to = 1.0e9)
{
    metamorfo::dsp::PitchDetector d;
    d.prepare (kRate);
    std::vector<double> f0s;
    double lastTime = -1.0;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        d.push (x[i]);
        const auto& e = d.latest();
        const double t = e.time / kRate;
        if (e.time != lastTime && t > from && t < to && e.voiced)
            f0s.push_back (e.f0);
        lastTime = e.time;
    }
    if (f0s.empty())
        return 0.0;
    std::nth_element (f0s.begin(), f0s.begin() + static_cast<long> (f0s.size() / 2), f0s.end());
    return f0s[f0s.size() / 2];
}

// Spectral centroid (Hann window, DFT on a log-spaced grid of bins, 80 Hz - 8 kHz).
inline double centroid (const std::vector<float>& x, double fromSeconds = 0.4, int length = 8192)
{
    const auto start = static_cast<std::size_t> (fromSeconds * kRate);
    double num = 0.0, den = 0.0;
    for (double f = 80.0; f < 8000.0; f *= 1.01)
    {
        double re = 0.0, im = 0.0;
        for (int n = 0; n < length; ++n)
        {
            const double w = 0.5 - 0.5 * std::cos (2.0 * kPi * n / (length - 1));
            const double v = w * x[start + static_cast<std::size_t> (n)];
            const double ph = 2.0 * kPi * f * n / kRate;
            re += v * std::cos (ph);
            im -= v * std::sin (ph);
        }
        const double mag = std::sqrt (re * re + im * im);
        num += f * mag * f; // weight by bin width (log grid: width ~ f)
        den += mag * f;
    }
    return num / den;
}

// Largest second difference relative to the signal's peak (click detector).
inline double clickiness (const std::vector<float>& x, std::size_t from)
{
    double peak = 1.0e-9, worst = 0.0;
    for (std::size_t i = from + 2; i < x.size(); ++i)
    {
        peak = std::max (peak, static_cast<double> (std::abs (x[i])));
        worst = std::max (worst, static_cast<double> (std::abs (x[i] - 2.0f * x[i - 1] + x[i - 2])));
    }
    return worst / peak;
}

} // namespace metamorfo::test
