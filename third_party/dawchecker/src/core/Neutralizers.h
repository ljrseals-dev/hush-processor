#pragma once
#include <array>
#include "DetectorTypes.h"

namespace dawchecker::dsp {

// Cascaded notch filters at the ENF fundamentals + harmonics.
class ENFNotchFilter
{
public:
    explicit ENFNotchFilter(double sampleRate = 48000.0);
    void prepare(double sampleRate);
    double process(double x) noexcept;
    bool enabled = true;

private:
    std::array<BiquadState, 6> notches_{};
};

// One-pole steep rolloff above the audible/near-ultrasonic boundary.
class UltrasonicRolloff
{
public:
    explicit UltrasonicRolloff(double sampleRate = 48000.0, double cutoffHz = 17000.0);
    void prepare(double sampleRate, double cutoffHz = 17000.0);
    double process(double x) noexcept;
    bool enabled = true;

private:
    BiquadState lowpass_{};
};

// Subtracts the estimated persistent watermark bias from the live signal,
// scaled by detection confidence so clean audio is left untouched.
class WatermarkScrubber
{
public:
    double process(double liveSample, double referenceSample, double detectorConfidence) noexcept;
    bool enabled = true;

private:
    double smoothedDelta_ = 0.0;
};

} // namespace dawchecker::dsp
