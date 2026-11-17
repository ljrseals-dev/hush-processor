#pragma once
#include <array>
#include "DetectorTypes.h"

namespace dawchecker::dsp {

// Scans for inaudible high-frequency content (typically >17 kHz) used for
// device fingerprinting, cross-device tracking beacons, or steganographic
// side channels.
class UltrasonicScanner
{
public:
    explicit UltrasonicScanner(double sampleRate = 48000.0);

    void prepare(double sampleRate);
    DetectorResult process(double liveSample) noexcept;

private:
    struct GoertzelBin { double coeff = 0.0, s1 = 0.0, s2 = 0.0, freqHz = 0.0; };

    double sampleRate_ = 48000.0;
    std::array<GoertzelBin, 5> bins_{}; // 17.4k, 18k, 18.5k, 19.2k, 20.15k
    double smoothed_ = 0.0;
};

} // namespace dawchecker::dsp
