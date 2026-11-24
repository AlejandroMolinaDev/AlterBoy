#include "dsp/PitchDetector.h"

#include <algorithm>
#include <cmath>

namespace metamorfo::dsp
{
namespace
{
std::uint32_t nextPowerOfTwo (std::uint32_t v)
{
    std::uint32_t p = 1;
    while (p < v)
        p <<= 1;
    return p;
}

float hzToCents (float hz) { return 1200.0f * std::log2 (hz / 440.0f); }
float centsToHz (float cents) { return 440.0f * std::exp2 (cents / 1200.0f); }

// Sum of squared differences with 8 independent accumulators: breaks the serial
// floating-point dependency so the compiler can vectorise (no -ffast-math needed).
float sumSquaredDiff (const float* a, const float* b, int n) noexcept
{
    float acc[8] {};
    int i = 0;
    for (; i + 8 <= n; i += 8)
        for (int k = 0; k < 8; ++k)
        {
            const float d = a[i + k] - b[i + k];
            acc[k] += d * d;
        }
    float sum = ((acc[0] + acc[1]) + (acc[2] + acc[3])) + ((acc[4] + acc[5]) + (acc[6] + acc[7]));
    for (; i < n; ++i)
    {
        const float d = a[i] - b[i];
        sum += d * d;
    }
    return sum;
}

// Parabolic interpolation of a minimum given three neighbouring values.
double parabolicOffset (double a, double b, double c)
{
    const double denom = a - 2.0 * b + c;
    if (std::abs (denom) < 1.0e-12)
        return 0.0;
    return std::clamp (0.5 * (a - c) / denom, -0.5, 0.5);
}
} // namespace

void PitchDetector::prepare (double sampleRate)
{
    sampleRate_ = sampleRate;
    decimation_ = std::max (1, static_cast<int> (std::lround (sampleRate / 11025.0)));
    decimatedRate_ = sampleRate / decimation_;

    maxPeriod_ = static_cast<int> (std::ceil (sampleRate / kMinF0));
    minPeriod_ = static_cast<int> (std::floor (sampleRate / kMaxF0));
    maxPeriodD_ = static_cast<int> (std::ceil (decimatedRate_ / kMinF0));
    minPeriodD_ = std::max (2, static_cast<int> (std::floor (decimatedRate_ / kMaxF0)));
    hop_ = std::max (1, static_cast<int> (std::lround (128.0 * sampleRate / 48000.0)));

    highPass_.setHighPass (sampleRate, 55.0);
    const double lowPassCutoff = std::min (4000.0, 0.42 * decimatedRate_);
    lowPassA_.setLowPass (sampleRate, lowPassCutoff, 0.54119610);
    lowPassB_.setLowPass (sampleRate, lowPassCutoff, 1.30656296); // 4th-order Butterworth

    const auto fullSize = nextPowerOfTwo (static_cast<std::uint32_t> (2 * maxPeriod_ + 4 * hop_ + 64));
    full_.assign (fullSize, 0.0f);
    fullMask_ = fullSize - 1;

    const auto decSize = nextPowerOfTwo (static_cast<std::uint32_t> (2 * maxPeriodD_ + 64));
    decimated_.assign (decSize, 0.0f);
    decimatedMask_ = decSize - 1;

    scratchD_.assign (static_cast<std::size_t> (2 * maxPeriodD_ + 2), 0.0f);
    scratchF_.assign (static_cast<std::size_t> (2 * maxPeriod_ + 2), 0.0f);
    diff_.assign (static_cast<std::size_t> (maxPeriodD_ + 2), 0.0f);
    cmnd_.assign (static_cast<std::size_t> (maxPeriodD_ + 2), 1.0f);

    reset();
}

void PitchDetector::reset() noexcept
{
    highPass_.reset();
    lowPassA_.reset();
    lowPassB_.reset();
    std::fill (full_.begin(), full_.end(), 0.0f);
    std::fill (decimated_.begin(), decimated_.end(), 0.0f);
    fullWrite_ = decimatedWrite_ = 0;
    pushed_ = decimatedCount_ = 0;
    voicing_.reset();
    medianCount_ = medianWrite_ = 0;
    hasStable_ = false;
    octaveJumpCount_ = 0;
    historyWrite_ = historyCount_ = 0;
    latest_ = {};
}

void PitchDetector::push (float sample) noexcept
{
    const float hp = highPass_.process (sample);
    full_[fullWrite_] = hp;
    fullWrite_ = (fullWrite_ + 1) & fullMask_;

    const float lp = lowPassB_.process (lowPassA_.process (hp));
    if (pushed_ % decimation_ == 0)
    {
        decimated_[decimatedWrite_] = lp;
        decimatedWrite_ = (decimatedWrite_ + 1) & decimatedMask_;
        ++decimatedCount_;
    }

    ++pushed_;

    if (pushed_ % hop_ == 0 && pushed_ >= 2 * maxPeriod_ && decimatedCount_ >= 2 * maxPeriodD_)
        analyse();
}

void PitchDetector::analyse() noexcept
{
    // ---- copy the latest windows into linear scratch buffers -------------------------
    const int nD = 2 * maxPeriodD_;
    for (int i = 0; i < nD; ++i)
        scratchD_[static_cast<std::size_t> (i)] = decimated_[(decimatedWrite_ - static_cast<std::uint32_t> (nD) + static_cast<std::uint32_t> (i)) & decimatedMask_];

    const int nF = 2 * maxPeriod_;
    for (int i = 0; i < nF; ++i)
        scratchF_[static_cast<std::size_t> (i)] = full_[(fullWrite_ - static_cast<std::uint32_t> (nF) + static_cast<std::uint32_t> (i)) & fullMask_];

    // ---- level and zero-crossing rate on the full-rate window ------------------------
    double energy = 0.0;
    int crossings = 0;
    for (int i = 0; i < nF; ++i)
    {
        const float x = scratchF_[static_cast<std::size_t> (i)];
        energy += static_cast<double> (x) * x;
        if (i > 0 && ((x >= 0.0f) != (scratchF_[static_cast<std::size_t> (i - 1)] >= 0.0f)))
            ++crossings;
    }
    const float rmsDb = static_cast<float> (10.0 * std::log10 (energy / nF + 1.0e-20));
    const float zcr = static_cast<float> (crossings) / static_cast<float> (nF);

    // ---- YIN on the decimated signal ---------------------------------------------------
    const int w = maxPeriodD_;
    const float* x = scratchD_.data();
    diff_[0] = 0.0f;
    for (int tau = 1; tau <= maxPeriodD_; ++tau)
        diff_[static_cast<std::size_t> (tau)] = sumSquaredDiff (x, x + tau, w);

    cmnd_[0] = 1.0f;
    double running = 0.0;
    for (int tau = 1; tau <= maxPeriodD_; ++tau)
    {
        running += diff_[static_cast<std::size_t> (tau)];
        cmnd_[static_cast<std::size_t> (tau)] = running > 0.0
            ? static_cast<float> (static_cast<double> (diff_[static_cast<std::size_t> (tau)]) * tau / running)
            : 1.0f;
    }

    constexpr float threshold = 0.15f;
    int best = -1;
    for (int tau = minPeriodD_; tau < maxPeriodD_; ++tau)
    {
        if (cmnd_[static_cast<std::size_t> (tau)] < threshold)
        {
            while (tau + 1 < maxPeriodD_ && cmnd_[static_cast<std::size_t> (tau + 1)] < cmnd_[static_cast<std::size_t> (tau)])
                ++tau;
            best = tau;
            break;
        }
    }
    if (best < 0)
    {
        best = minPeriodD_;
        for (int tau = minPeriodD_ + 1; tau < maxPeriodD_; ++tau)
            if (cmnd_[static_cast<std::size_t> (tau)] < cmnd_[static_cast<std::size_t> (best)])
                best = tau;
    }
    const float aperiodicity = cmnd_[static_cast<std::size_t> (best)];

    // ---- refinement on the full-rate signal -------------------------------------------
    const double coarse = (best + parabolicOffset (cmnd_[static_cast<std::size_t> (best - 1)],
                                                   cmnd_[static_cast<std::size_t> (best)],
                                                   cmnd_[static_cast<std::size_t> (best + 1)])) * decimation_;
    const int wF = std::min (maxPeriod_, nF - static_cast<int> (std::ceil (coarse)) - decimation_ - 2);
    const int lo = std::max (minPeriod_, static_cast<int> (std::floor (coarse)) - decimation_ - 1);
    const int hi = std::min (maxPeriod_, static_cast<int> (std::ceil (coarse)) + decimation_ + 1);

    double bestValue = 1.0e300, prevValue = 0.0, nextValue = 0.0;
    int bestLag = lo;
    double values[64] {};
    const int count = std::min (64, hi - lo + 1);
    const float* xf = scratchF_.data();
    for (int k = 0; k < count; ++k)
    {
        const int tau = lo + k;
        const double sum = sumSquaredDiff (xf, xf + tau, wF);
        values[k] = sum;
        if (sum < bestValue)
        {
            bestValue = sum;
            bestLag = tau;
        }
    }
    const int bk = bestLag - lo;
    double refined = bestLag;
    if (bk > 0 && bk < count - 1)
    {
        prevValue = values[bk - 1];
        nextValue = values[bk + 1];
        refined += parabolicOffset (prevValue, bestValue, nextValue);
    }

    const float rawF0 = static_cast<float> (sampleRate_ / refined);
    const bool voiced = voicing_.update (aperiodicity, rmsDb, zcr) && rawF0 >= kMinF0 * 0.97f && rawF0 <= kMaxF0 * 1.03f;

    PitchEstimate e;
    e.time = static_cast<double> (pushed_) - maxPeriod_;
    e.aperiodicity = aperiodicity;
    e.rmsDb = rmsDb;
    e.zcr = zcr;
    e.voiced = voiced;
    e.f0 = voiced ? smoothF0 (rawF0) : 0.0f;
    if (! voiced)
    {
        medianCount_ = 0;
        octaveJumpCount_ = 0;
    }

    history_[static_cast<std::size_t> (historyWrite_)] = e;
    historyWrite_ = (historyWrite_ + 1) % kHistory;
    historyCount_ = std::min (historyCount_ + 1, kHistory);
    latest_ = e;
}

float PitchDetector::smoothF0 (float rawF0) noexcept
{
    medianCents_[static_cast<std::size_t> (medianWrite_)] = hzToCents (rawF0);
    medianWrite_ = (medianWrite_ + 1) % static_cast<int> (medianCents_.size());
    medianCount_ = std::min (medianCount_ + 1, static_cast<int> (medianCents_.size()));

    // Insertion sort of at most 5 values (no allocation).
    std::array<float, 5> sorted {};
    for (int i = 0; i < medianCount_; ++i)
    {
        const float v = medianCents_[static_cast<std::size_t> ((medianWrite_ - 1 - i + 10) % 5)];
        int j = i;
        while (j > 0 && sorted[static_cast<std::size_t> (j - 1)] > v)
        {
            sorted[static_cast<std::size_t> (j)] = sorted[static_cast<std::size_t> (j - 1)];
            --j;
        }
        sorted[static_cast<std::size_t> (j)] = v;
    }
    const float median = sorted[static_cast<std::size_t> (medianCount_ / 2)];

    // Octave-jump hysteresis: a jump of ~an octave or more must persist before it is accepted.
    if (hasStable_ && std::abs (median - stableCents_) > 900.0f)
    {
        if (++octaveJumpCount_ < 3)
            return centsToHz (stableCents_);
    }
    octaveJumpCount_ = 0;
    stableCents_ = median;
    hasStable_ = true;
    return centsToHz (median);
}

PitchEstimate PitchDetector::estimateAt (double time) const noexcept
{
    if (historyCount_ == 0)
        return {};

    // Walk back from the newest estimate.
    for (int i = 0; i < historyCount_; ++i)
    {
        const auto& e = history_[static_cast<std::size_t> ((historyWrite_ - 1 - i + kHistory) % kHistory)];
        if (e.time <= time)
            return e;
    }
    return history_[static_cast<std::size_t> ((historyWrite_ - historyCount_ + kHistory) % kHistory)];
}

} // namespace metamorfo::dsp
