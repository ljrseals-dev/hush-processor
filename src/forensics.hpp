#pragma once
// fk::forensics: local-only wrapper over the vendored DawChecker detectors. Findings are indicators with
// stated limitations, never proof of tampering, authenticity or authorship. Nothing here modifies audio.
#include <string>
#include <vector>
#include "audio.hpp"
#include "result.hpp"
namespace fk::forensics {
struct Evidence {
  std::string module;    // e.g. "ultrasonic scan (DawChecker UltrasonicScanner)"
  std::string status;    // Idle / Active / Alert, as reported by the detector
  std::string label;     // detector's own summary text
  double confidence = 0; // 0..100, uncalibrated detector score
  double location_hz = 0;
  bool applicable = true;
  std::string note;      // why not applicable, or extra context
};
struct Report {
  std::string file_sha256;
  std::string analysis_version;
  std::string settings;
  std::vector<Evidence> evidence;
  std::vector<std::string> limitations;
  std::string to_text() const;
};
// reference may be null; the watermark-residual check only runs when a matching reference is supplied.
Result<Report> analyze(const fk::audio::AudioBuffer& live, const fk::audio::AudioBuffer* reference,
                       const std::string& file_sha256);
// Explicit, user-requested device/codec-name scan (DawChecker CodecScanner). Enumerates nothing unless a
// device backend (PortAudio/ALSA) was compiled in; this build has none.
std::string scan_devices_text();
}  // namespace fk::forensics
