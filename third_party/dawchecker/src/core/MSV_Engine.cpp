#include "MSV_Engine.h"
#include <algorithm>

namespace dawchecker::dsp {

MSV_Engine::MSV_Engine(double sampleRate, double sensitivity)
{
    prepare(sampleRate, sensitivity);
}

void MSV_Engine::prepare(double sampleRate, double sensitivity)
{
    sampleRate_  = sampleRate > 0.0 ? sampleRate : 48000.0;
    sensitivity_ = std::clamp(sensitivity, 1.0, 100.0);

    watermarkDetector_.prepare(sampleRate_, sensitivity_);
    enfAnalyzer_.prepare(sampleRate_);
    ultrasonicScanner_.prepare(sampleRate_);

    enfNotch_.prepare(sampleRate_);
    ultrasonicRolloff_.prepare(sampleRate_, 17000.0);
}

void MSV_Engine::process(double referenceSample, double liveSample, double& out) noexcept
{
    if (bypassAll.load(std::memory_order_relaxed))
    {
        out = liveSample;
        return;
    }

    const auto wmResult  = watermarkDetector_.process(liveSample, referenceSample);
    const auto enfResult = enfAnalyzer_.process(liveSample, referenceSample);
    const auto usResult  = ultrasonicScanner_.process(liveSample);

    double cleaned = liveSample;

    if (watermarkScrubEnabled.load(std::memory_order_relaxed))
        cleaned = watermarkScrubber_.process(cleaned, referenceSample, wmResult.confidence);

    if (enfNotchEnabled.load(std::memory_order_relaxed) && enfResult.status != DetectorStatus::Idle)
        cleaned = enfNotch_.process(cleaned);

    if (ultrasonicRolloffEnabled.load(std::memory_order_relaxed) && usResult.status != DetectorStatus::Idle)
        cleaned = ultrasonicRolloff_.process(cleaned);

    out = cleaned;

    // Aggregate tamper score: weighted max-plus-average blend so a single
    // strong alert dominates but multiple weak findings still add up.
    const double maxConf = std::max({wmResult.confidence, enfResult.confidence, usResult.confidence});
    const double avgConf = (wmResult.confidence + enfResult.confidence + usResult.confidence) / 3.0;
    const double aggregate = std::clamp(maxConf * 0.7 + avgConf * 0.3, 0.0, 100.0);

    const bool compromised = wmResult.status == DetectorStatus::Alert ||
                              enfResult.status == DetectorStatus::Alert ||
                              usResult.status == DetectorStatus::Alert;

    tamperScore_.store(aggregate, std::memory_order_relaxed);
    isCompromised_.store(compromised, std::memory_order_relaxed);

    watermarkConfidence_.store(wmResult.confidence, std::memory_order_relaxed);
    watermarkStatus_.store(static_cast<std::uint8_t>(wmResult.status), std::memory_order_relaxed);

    enfConfidence_.store(enfResult.confidence, std::memory_order_relaxed);
    enfStatus_.store(static_cast<std::uint8_t>(enfResult.status), std::memory_order_relaxed);
    enfLocationHz_.store(enfResult.locationHz, std::memory_order_relaxed);

    ultrasonicConfidence_.store(usResult.confidence, std::memory_order_relaxed);
    ultrasonicStatus_.store(static_cast<std::uint8_t>(usResult.status), std::memory_order_relaxed);
    ultrasonicLocationHz_.store(usResult.locationHz, std::memory_order_relaxed);
}

EngineSnapshot MSV_Engine::snapshot() const noexcept
{
    EngineSnapshot snap;
    snap.tamperScore   = tamperScore_.load(std::memory_order_relaxed);
    snap.isCompromised = isCompromised_.load(std::memory_order_relaxed);

    snap.watermark.confidence = watermarkConfidence_.load(std::memory_order_relaxed);
    snap.watermark.status     = static_cast<DetectorStatus>(watermarkStatus_.load(std::memory_order_relaxed));

    snap.enf.confidence = enfConfidence_.load(std::memory_order_relaxed);
    snap.enf.status     = static_cast<DetectorStatus>(enfStatus_.load(std::memory_order_relaxed));
    snap.enf.locationHz = enfLocationHz_.load(std::memory_order_relaxed);

    snap.ultrasonic.confidence = ultrasonicConfidence_.load(std::memory_order_relaxed);
    snap.ultrasonic.status     = static_cast<DetectorStatus>(ultrasonicStatus_.load(std::memory_order_relaxed));
    snap.ultrasonic.locationHz = ultrasonicLocationHz_.load(std::memory_order_relaxed);

    return snap;
}

} // namespace dawchecker::dsp
