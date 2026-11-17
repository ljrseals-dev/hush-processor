#include "ENFAnalyzer.h"
#include <cmath>
#include <algorithm>

namespace dawchecker::dsp {

namespace {
constexpr std::array<double, 6> kTrackedFrequencies = {
    50.0, 60.0,     // fundamental mains frequencies (EU/US)
    100.0, 120.0,   // 2nd harmonics
    150.0, 180.0    // 3rd harmonics
};
}

ENFAnalyzer::ENFAnalyzer(double sampleRate)
{
    prepare(sampleRate);
}

void ENFAnalyzer::prepare(double sampleRate)
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    for (std::size_t i = 0; i < bins_.size(); ++i)
    {
        auto& bin = bins_[i];
        bin.freqHz = kTrackedFrequencies[i];
        bin.coeff  = 2.0 * std::cos(2.0 * M_PI * bin.freqHz / sampleRate_);
        bin.s1 = bin.s2 = 0.0;
    }
    smoothedPower_ = 0.0;
    baselinePower_ = 0.0;
}

DetectorResult ENFAnalyzer::process(double liveSample, double referenceSample) noexcept
{
    double fundamentalPower = 0.0;
    double harmonicPower = 0.0;
    double dominant = 0.0;
    double dominantPower = -1.0;

    for (std::size_t i = 0; i < bins_.size(); ++i)
    {
        auto& bin = bins_[i];
        const double s = liveSample + bin.coeff * bin.s1 - bin.s2;
        bin.s2 = bin.s1;
        bin.s1 = s;
        const double power = bin.s1 * bin.s1 + bin.s2 * bin.s2 - bin.coeff * bin.s1 * bin.s2;

        if (i < 2) fundamentalPower += power;
        else       harmonicPower += power;

        if (power > dominantPower)
        {
            dominantPower = power;
            dominant = bin.freqHz;
        }
    }

    // A reference-free single-ended check: an *asymmetric* fundamental (only
    // 50 or only 60, not both, with disproportionate harmonic energy) is more
    // consistent with an injected/embedded ENF signature than a genuine
    // ambient mains hum picked up acoustically or electrically.
    const double asymmetry = std::abs(bins_[0].s1 * bins_[0].s1 - bins_[1].s1 * bins_[1].s1);
    const double raw = harmonicPower * 12.0 + asymmetry * 150.0;

    smoothedPower_ += (raw - smoothedPower_) * 0.002;
    baselinePower_ += (std::abs(referenceSample) - baselinePower_) * 0.001;

    const double threshold = 0.0006 + baselinePower_ * 0.02;

    DetectorResult result;
    result.confidence = std::clamp(smoothedPower_ * 4000.0, 0.0, 100.0);
    result.locationHz = dominant;
    if (smoothedPower_ > threshold * 2.0)
    {
        result.status = DetectorStatus::Alert;
        result.label  = "ENF signature locked near " + std::to_string(static_cast<int>(dominant)) + " Hz";
    }
    else if (smoothedPower_ > threshold)
    {
        result.status = DetectorStatus::Active;
        result.label  = "Weak mains-frequency artifact present";
    }
    else
    {
        result.status = DetectorStatus::Idle;
        result.label  = "No ENF artifacts detected";
    }
    return result;
}

} // namespace dawchecker::dsp
