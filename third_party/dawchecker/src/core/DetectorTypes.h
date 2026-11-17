#pragma once
#include <cstdint>
#include <string>

namespace dawchecker::dsp {

enum class DetectorStatus : std::uint8_t { Idle, Active, Alert };

struct DetectorResult
{
    DetectorStatus status   = DetectorStatus::Idle;
    double         confidence = 0.0; // 0..100
    double         locationHz = 0.0; // dominant frequency/region of the finding, if applicable
    std::string    label;            // human-readable summary for the GUI panel
};

// A single biquad notch/lowpass section, used by all neutralizers.
struct BiquadState
{
    double b0 = 1.0, b1 = 0.0, b2 = 0.0, a1 = 0.0, a2 = 0.0;
    double z1 = 0.0, z2 = 0.0;

    inline double process(double x) noexcept
    {
        const double y = b0 * x + z1;
        z1 = b1 * x - a1 * y + z2;
        z2 = b2 * x - a2 * y;
        return y;
    }
};

BiquadState makeNotch(double sampleRate, double freqHz, double q);
BiquadState makeOnePoleLowpass(double sampleRate, double cutoffHz);

} // namespace dawchecker::dsp
