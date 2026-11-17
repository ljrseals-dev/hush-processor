#pragma once
#include <array>
#include "DetectorTypes.h"

namespace dawchecker::dsp {

// Detects Electric Network Frequency (mains hum, 50/60 Hz + harmonics)
// signatures that are anomalously strong, phase-locked, or time-inconsistent
// relative to a reference signal — a common forensic timestamp artifact.
class ENFAnalyzer
{
public:
    explicit ENFAnalyzer(double sampleRate = 48000.0);

    void prepare(double sampleRate);

    // Feed one sample of "live" (post-chain) and "reference" (pre-chain /
    // sidechain) audio. Returns the current detector result.
    DetectorResult process(double liveSample, double referenceSample) noexcept;

    double lastTamperContribution() const noexcept { return smoothedPower_; }

private:
    struct GoertzelBin
    {
        double coeff = 0.0;
        double s1 = 0.0, s2 = 0.0;
        double freqHz = 0.0;
    };

    double sampleRate_ = 48000.0;
    std::array<GoertzelBin, 6> bins_{}; // 50, 60, and two harmonics of each
    double smoothedPower_ = 0.0;
    double baselinePower_ = 0.0;
};

} // namespace dawchecker::dsp
