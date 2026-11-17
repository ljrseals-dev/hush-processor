#pragma once
//==============================================================================
// MSV_Engine.h — DawChecker Secure forensic/DSP core
//
// Upgrade of the original single-file Goertzel prototype into a modular
// engine: each forensic technique lives in its own detector/neutralizer
// class (see ENFAnalyzer, UltrasonicScanner, WatermarkDetector,
// Neutralizers.h), and this class aggregates their results into the overall
// Tamper Score plus produces the cleaned output sample.
//==============================================================================
#include <atomic>
#include <array>
#include "DetectorTypes.h"
#include "ENFAnalyzer.h"
#include "UltrasonicScanner.h"
#include "WatermarkDetector.h"
#include "Neutralizers.h"

namespace dawchecker::dsp {

struct EngineSnapshot
{
    double tamperScore = 0.0; // 0..100 aggregate
    bool   isCompromised = false;
    DetectorResult watermark;
    DetectorResult enf;
    DetectorResult ultrasonic;
};

// Thread-safety note: `process()` runs on the real-time audio thread.
// `snapshot()` is safe to call from the GUI thread — all cross-thread state
// is exposed only through std::atomic<double>/enum stores below, so the GUI
// never blocks the audio callback and the audio callback never blocks on the
// GUI.
class MSV_Engine
{
public:
    explicit MSV_Engine(double sampleRate = 48000.0, double sensitivity = 5.0);

    void prepare(double sampleRate, double sensitivity);

    // Runs one sample through all detectors + (optionally enabled)
    // neutralizers. `out` receives the cleaned sample.
    void process(double referenceSample, double liveSample, double& out) noexcept;

    EngineSnapshot snapshot() const noexcept;

    // Per-module neutralization toggles (GUI-driven, audio-thread read).
    std::atomic<bool> watermarkScrubEnabled{true};
    std::atomic<bool> enfNotchEnabled{true};
    std::atomic<bool> ultrasonicRolloffEnabled{true};
    std::atomic<bool> bypassAll{false};

private:
    double sampleRate_ = 48000.0;
    double sensitivity_ = 5.0;

    WatermarkDetector  watermarkDetector_;
    ENFAnalyzer        enfAnalyzer_;
    UltrasonicScanner  ultrasonicScanner_;

    WatermarkScrubber  watermarkScrubber_;
    ENFNotchFilter     enfNotch_;
    UltrasonicRolloff  ultrasonicRolloff_;

    // Cross-thread published state (audio thread writes, GUI thread reads).
    std::atomic<double> tamperScore_{0.0};
    std::atomic<bool>   isCompromised_{false};

    std::atomic<double> watermarkConfidence_{0.0};
    std::atomic<std::uint8_t> watermarkStatus_{0};
    std::atomic<double> enfConfidence_{0.0};
    std::atomic<std::uint8_t> enfStatus_{0};
    std::atomic<double> enfLocationHz_{0.0};
    std::atomic<double> ultrasonicConfidence_{0.0};
    std::atomic<std::uint8_t> ultrasonicStatus_{0};
    std::atomic<double> ultrasonicLocationHz_{0.0};
};

} // namespace dawchecker::dsp
