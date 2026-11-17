#include "UltrasonicScanner.h"
#include <cmath>
#include <algorithm>

namespace dawchecker::dsp {

namespace {
constexpr std::array<double, 5> kBands = {17400.0, 18000.0, 18500.0, 19200.0, 20150.0};
}

UltrasonicScanner::UltrasonicScanner(double sampleRate) { prepare(sampleRate); }

void UltrasonicScanner::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    const double nyquist = sampleRate_ * 0.5;
    for (std::size_t i = 0; i < bins_.size(); ++i)
    {
        auto& bin = bins_[i];
        bin.freqHz = std::min(kBands[i], nyquist * 0.98);
        bin.coeff  = 2.0 * std::cos(2.0 * M_PI * bin.freqHz / sampleRate_);
        bin.s1 = bin.s2 = 0.0;
    }
    smoothed_ = 0.0;
}

DetectorResult UltrasonicScanner::process(double liveSample) noexcept
{
    double total = 0.0;
    double dominant = 0.0, dominantPower = -1.0;

    for (auto& bin : bins_)
    {
        const double s = liveSample + bin.coeff * bin.s1 - bin.s2;
        bin.s2 = bin.s1;
        bin.s1 = s;
        const double power = bin.s1 * bin.s1 + bin.s2 * bin.s2 - bin.coeff * bin.s1 * bin.s2;
        total += power;
        if (power > dominantPower) { dominantPower = power; dominant = bin.freqHz; }
    }

    smoothed_ += (total - smoothed_) * 0.003;

    DetectorResult result;
    result.confidence = std::clamp(smoothed_ * 6000.0, 0.0, 100.0);
    result.locationHz = dominant;

    if (smoothed_ > 0.0009)
    {
        result.status = DetectorStatus::Alert;
        result.label  = "Ultrasonic beacon near " + std::to_string(static_cast<int>(dominant)) + " Hz";
    }
    else if (smoothed_ > 0.00025)
    {
        result.status = DetectorStatus::Active;
        result.label  = "Low-level inaudible content present";
    }
    else
    {
        result.status = DetectorStatus::Idle;
        result.label  = "No ultrasonic content detected";
    }
    return result;
}

} // namespace dawchecker::dsp
