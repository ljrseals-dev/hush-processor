#include "WatermarkDetector.h"
#include <cmath>
#include <algorithm>
#include <numeric>

namespace dawchecker::dsp {

WatermarkDetector::WatermarkDetector(double sampleRate, double sensitivity)
{
    prepare(sampleRate, sensitivity);
}

void WatermarkDetector::prepare(double sampleRate, double sensitivity)
{
    sampleRate_  = sampleRate > 0.0 ? sampleRate : 48000.0;
    sensitivity_ = std::clamp(sensitivity, 1.0, 100.0);
    prevRef_ = prevLive_ = prevDelta_ = 0.0;
    smoothedStego_ = 0.0;
    deltaHistory_.clear();
}

DetectorResult WatermarkDetector::process(double liveSample, double referenceSample) noexcept
{
    const double delta = liveSample - referenceSample;

    // Phase-deviation term: watermarking schemes that phase-modulate a
    // reference carrier show up as a nonzero cross term here even when
    // magnitude differences are negligible.
    const double phaseDeviation = std::abs(liveSample * prevRef_ - referenceSample * prevLive_);
    prevRef_  = referenceSample;
    prevLive_ = liveSample;

    // Structured-delta ("stego") term: a genuine watermark payload tends to
    // change the delta signal in small, consistent steps rather than the
    // larger, less-regular jumps typical of ordinary program material.
    const bool structuredStep = std::abs(delta - prevDelta_) > (0.000001 * sensitivity_);
    prevDelta_ = delta;

    deltaHistory_.push_back(delta);
    if (deltaHistory_.size() > kHistoryLen)
        deltaHistory_.pop_front();

    double deltaMean = 0.0;
    if (!deltaHistory_.empty())
        deltaMean = std::accumulate(deltaHistory_.begin(), deltaHistory_.end(), 0.0) / static_cast<double>(deltaHistory_.size());

    double deltaVariance = 0.0;
    for (double d : deltaHistory_)
        deltaVariance += (d - deltaMean) * (d - deltaMean);
    if (!deltaHistory_.empty())
        deltaVariance /= static_cast<double>(deltaHistory_.size());

    // Low variance + nonzero mean delta over a long window is the signature
    // of a persistent, low-amplitude additive watermark rather than transient
    // processing artifacts (which tend to have high variance).
    const double persistentBias = std::abs(deltaMean);
    const double raw = phaseDeviation * 8.0 + (structuredStep ? 0.02 : 0.0) +
                        (deltaVariance < 1e-6 ? persistentBias * 40.0 : 0.0);

    smoothedStego_ += (raw - smoothedStego_) * 0.004;

    const double adaptiveThreshold = 0.0004 * sensitivity_ / 5.0;

    DetectorResult result;
    result.confidence = std::clamp(smoothedStego_ * (500.0 / std::max(sensitivity_, 1.0)), 0.0, 100.0);

    if (smoothedStego_ > adaptiveThreshold * 2.5)
    {
        result.status = DetectorStatus::Alert;
        result.label  = "Structured watermark payload detected";
    }
    else if (smoothedStego_ > adaptiveThreshold)
    {
        result.status = DetectorStatus::Active;
        result.label  = "Possible watermark residual";
    }
    else
    {
        result.status = DetectorStatus::Idle;
        result.label  = "No watermark signature detected";
    }
    return result;
}

} // namespace dawchecker::dsp
