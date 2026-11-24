#include "dsp/PsolaEngine.h"

#include <algorithm>
#include <cmath>

namespace metamorfo::dsp
{
namespace
{
constexpr double kPi = 3.14159265358979323846;

std::uint32_t nextPowerOfTwo (std::uint32_t v)
{
    std::uint32_t p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

// Pitch ratios are limited so grains stay meaningful (Robot from a low voice to
// C5 needs ~ x4.5; MIDI transposition up to two octaves).
constexpr double kMinLogRatio = -3.0; // x 1/8
constexpr double kMaxLogRatio = 3.0;  // x 8
} // namespace

void PsolaEngine::prepare (double sampleRate, int maxPeriodSamples)
{
    sampleRate_ = sampleRate;
    maxPeriod_ = std::max (16, maxPeriodSamples);
    latency_ = 2 * maxPeriod_;
    pseudoPeriod_ = std::min (static_cast<double> (maxPeriod_), sampleRate / 150.0);
    refineStride_ = std::max (2, static_cast<int> (std::lround (sampleRate / 24000.0)));
    refineStep_ = std::max (1, refineStride_ / 2);

    const auto inputSize = nextPowerOfTwo (static_cast<std::uint32_t> (6 * maxPeriod_ + 64));
    input_.assign (inputSize, 0.0f);
    inputMask_ = inputSize - 1;

    const auto outputSize = nextPowerOfTwo (static_cast<std::uint32_t> (4 * maxPeriod_ + 64));
    acc_.assign (outputSize, 0.0f);
    wsum_.assign (outputSize, 0.0f);
    outputMask_ = outputSize - 1;

    identityStep_ = static_cast<float> (1.0 / (0.02 * sampleRate)); // 20 ms cross-fade
    reset();
}

void PsolaEngine::reset() noexcept
{
    std::fill (input_.begin(), input_.end(), 0.0f);
    std::fill (acc_.begin(), acc_.end(), 0.0f);
    std::fill (wsum_.begin(), wsum_.end(), 0.0f);
    now_ = 0;
    lastEmitted_ = -1;
    markWrite_ = markCount_ = 0;
    nextAnalysis_ = static_cast<double> (maxPeriod_);
    nextSynthesis_ = static_cast<double> (maxPeriod_);
    modeController_.reset();
    logRatio_ = logFormant_ = 0.0;
    identityGain_ = 1.0f;
    lastRatio_ = 1.0f;
    lastF0_ = 0.0f;
    lastVoiced_ = false;
}

float PsolaEngine::readInput (double position) const noexcept
{
    const double floorPos = std::floor (position);
    const auto i = static_cast<std::int64_t> (floorPos);
    const auto frac = static_cast<float> (position - floorPos);
    const float a = input_[static_cast<std::uint32_t> (i) & inputMask_];
    const float b = input_[static_cast<std::uint32_t> (i + 1) & inputMask_];
    return a + frac * (b - a);
}

const PsolaEngine::Mark* PsolaEngine::nearestMark (double time) const noexcept
{
    const Mark* best = nullptr;
    double bestDistance = 1.0e300;
    for (int i = 0; i < markCount_; ++i)
    {
        const auto& m = marks_[static_cast<std::size_t> ((markWrite_ - 1 - i + kMarks) % kMarks)];
        const double d = std::abs (m.time - time);
        if (d < bestDistance)
        {
            bestDistance = d;
            best = &m;
        }
        else if (m.time < time)
        {
            break; // marks are ordered: going further back only increases the distance
        }
    }
    return best;
}

double PsolaEngine::refineMark (double previous, double predicted, double period) const noexcept
{
    // Pitch-synchronous refinement: choose the position near the prediction whose
    // surrounding period best matches (normalised cross-correlation) the period around
    // the previous mark. Keeps marks on the real glottal cycles through vibrato/jitter.
    //
    // Cost is bounded per mark (it all lands in one audio block): the correlation reads
    // every `stride`-th sample (~24 kHz resolution, plenty for the correlation peak) and
    // the candidates are scanned coarse-to-fine. At 44.1/48 kHz (stride 2, step 1) this is
    // the exhaustive search; at 192 kHz it is ~25x cheaper (B28: per-block CPU spikes).
    const int half = std::max (2, static_cast<int> (period * 0.5));
    const int search = std::max (1, static_cast<int> (period * 0.125));
    const auto prev = static_cast<std::int64_t> (std::lround (previous));
    const auto pred = static_cast<std::int64_t> (std::lround (predicted));

    const auto score = [&] (std::int64_t c) noexcept {
        double dot = 0.0, energy = 1.0e-12;
        for (int i = -half; i < half; i += refineStride_)
        {
            const float a = input_[static_cast<std::uint32_t> (prev + i) & inputMask_];
            const float b = input_[static_cast<std::uint32_t> (c + i) & inputMask_];
            dot += static_cast<double> (a) * b;
            energy += static_cast<double> (b) * b;
        }
        return dot / std::sqrt (energy);
    };

    double best = -1.0e300;
    std::int64_t bestPos = pred;
    for (auto c = pred - search; c <= pred + search; c += refineStep_)
        if (const double v = score (c); v > best)
        {
            best = v;
            bestPos = c;
        }
    if (refineStep_ > 1) // fine pass around the coarse winner
    {
        const auto centre = bestPos;
        for (auto c = centre - refineStep_ + 1; c < centre + refineStep_; ++c)
            if (c != centre && c >= pred - search && c <= pred + search)
                if (const double v = score (c); v > best)
                {
                    best = v;
                    bestPos = c;
                }
    }
    return static_cast<double> (bestPos);
}

void PsolaEngine::generateAnalysisMarks (const PitchDetector& detector) noexcept
{
    // A mark is usable once its whole grain (+/- one period) and its refinement search
    // window are in the input buffer. The detector describes the signal up to
    // `now - maxPeriod`; marks beyond that extrapolate the latest estimate.
    for (;;)
    {
        const auto estimate = detector.estimateAt (nextAnalysis_);
        const bool voiced = estimate.voiced && estimate.f0 > 0.0f;
        const double period = voiced ? std::clamp (sampleRate_ / estimate.f0, 2.0, static_cast<double> (maxPeriod_))
                                     : pseudoPeriod_;
        if (nextAnalysis_ + period * 1.25 + 2.0 > static_cast<double> (now_ - 1))
            break;

        double time = nextAnalysis_;
        double markPeriod = period;
        const Mark* previous = markCount_ > 0 ? &marks_[static_cast<std::size_t> ((markWrite_ - 1 + kMarks) % kMarks)] : nullptr;
        if (voiced && previous != nullptr && previous->voiced)
        {
            time = refineMark (previous->time, nextAnalysis_, period);
            markPeriod = std::clamp (time - previous->time, period * 0.8, period * 1.25);
            time = previous->time + markPeriod;
        }

        Mark& m = marks_[static_cast<std::size_t> (markWrite_)];
        m.time = time;
        m.period = markPeriod;
        // The mark's own period defines its f0, so output f0 = target exactly.
        m.f0 = voiced ? static_cast<float> (sampleRate_ / markPeriod) : 0.0f;
        m.voiced = voiced;
        markWrite_ = (markWrite_ + 1) % kMarks;
        markCount_ = std::min (markCount_ + 1, kMarks);
        nextAnalysis_ = time + period;
    }
}

void PsolaEngine::addGrain (const Mark& analysis, double synthesisTime, float formantRatio, double pitchRatio) noexcept
{
    // Output half-length in analysis periods, adaptive to the pitch ratio (chosen with the
    // quality matrix, tools/analysis/quality_matrix.py):
    //  * ratio <= 1 (pitch down): the classic 1.0 period. Synthesis marks are >= one period
    //    apart, so shorter grains would leave silent gaps (audible as clicks / chopping).
    //  * ratio >= 1.25 (pitch up): 0.8 period, which reduces the worst-case
    //    harmonic-envelope error by ~25 %; in between, 1/ratio.
    //  * never shorter than formant/ratio periods: the output half-length (divided by the
    //    formant ratio below) then stays >= the synthesis spacing (period / ratio), so this
    //    shortening can never open gaps between grains when the formant is raised too.
    const double halfPeriods = std::min (1.0, std::max (std::clamp (1.0 / pitchRatio, 0.8, 1.0),
                                                        static_cast<double> (formantRatio) / pitchRatio));
    // Capped at maxPeriod so the fixed latency always covers the grain.
    const double half = std::min (analysis.period * halfPeriods / formantRatio, static_cast<double> (maxPeriod_));
    const auto first = std::max (static_cast<std::int64_t> (std::ceil (synthesisTime - half)), lastEmitted_ + 1);
    const auto last = static_cast<std::int64_t> (std::floor (synthesisTime + half));

    for (auto n = first; n <= last; ++n)
    {
        const double offset = static_cast<double> (n) - synthesisTime;
        const auto window = static_cast<float> (0.5 * (1.0 + std::cos (kPi * offset / half)));
        const float sample = readInput (analysis.time + offset * formantRatio);
        const auto index = static_cast<std::uint32_t> (n) & outputMask_;
        acc_[index] += window * sample;
        wsum_[index] += window;
    }
}

void PsolaEngine::generateSynthesisMarks (const Controls& controls) noexcept
{
    const double horizon = static_cast<double> (now_ - maxPeriod_);
    while (nextSynthesis_ <= horizon)
    {
        const Mark* mark = nearestMark (nextSynthesis_);
        if (mark == nullptr)
        {
            nextSynthesis_ += pseudoPeriod_;
            continue;
        }

        // Pitch ratio: mode target for voiced marks, 1 for unvoiced (no correction on
        // breaths / sibilants), smoothed in the log domain between grains.
        const float target = mark->voiced ? modeController_.ratioFor (controls.mode, mark->f0) : 1.0f;
        const double targetLog = std::clamp (std::log2 (static_cast<double> (target)), kMinLogRatio, kMaxLogRatio);
        const double spacingSeconds = mark->period / sampleRate_;
        const double tau = std::max (0.0005, static_cast<double> (controls.retuneMs) * 0.001);
        const double voicingTau = 0.015; // soft voiced/unvoiced transitions
        const double k = 1.0 - std::exp (-spacingSeconds / (mark->voiced ? tau : voicingTau));
        logRatio_ += k * (targetLog - logRatio_);

        // Formant ratio: knob, or follows the pitch ratio with Link (tape-like shift).
        const double formantTargetLog = controls.link ? logRatio_ : static_cast<double> (controls.formantSemitones) / 12.0;
        logFormant_ += (1.0 - std::exp (-spacingSeconds / 0.03)) * (formantTargetLog - logFormant_);

        const auto ratio = std::exp2 (logRatio_);
        const auto formant = static_cast<float> (std::exp2 (logFormant_));
        addGrain (*mark, nextSynthesis_, formant, ratio);

        lastRatio_ = static_cast<float> (ratio);
        lastF0_ = mark->f0;
        lastVoiced_ = mark->voiced;
        nextSynthesis_ += mark->period / ratio;
    }
}

float PsolaEngine::processSample (float input, const PitchDetector& detector, const Controls& controls) noexcept
{
    input_[static_cast<std::uint32_t> (now_) & inputMask_] = input;
    ++now_;

    generateAnalysisMarks (detector);
    generateSynthesisMarks (controls);

    // Identity cross-fade target: exact transparency for neutral settings.
    const bool identity = ModeController::isIdentity (controls.mode) && std::fpclassify (controls.formantSemitones) == FP_ZERO;
    identityGain_ = identity ? std::min (1.0f, identityGain_ + identityStep_)
                             : std::max (0.0f, identityGain_ - identityStep_);

    const std::int64_t outTime = now_ - 1 - latency_;
    if (outTime < 0)
        return 0.0f;

    const auto index = static_cast<std::uint32_t> (outTime) & outputMask_;
    const float wet = acc_[index] / std::max (wsum_[index], 0.5f);
    acc_[index] = 0.0f;
    wsum_[index] = 0.0f;
    lastEmitted_ = outTime;

    return wet;
}

} // namespace metamorfo::dsp
