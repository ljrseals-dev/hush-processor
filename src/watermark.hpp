#pragma once
// fk::watermark: explicit, offline, non-destructive spread-spectrum watermark authoring and detection.
// Experimental: robust against mild noise and 16-bit requantisation in tests, NOT against resampling,
// time-stretch, lossy codecs or deliberate attack. Absence of a detection never proves absence of a watermark.
#include <cstdint>
#include <string>
#include <vector>
#include "audio.hpp"
#include "result.hpp"
namespace fk::watermark {
struct EmbedParams {
  std::uint64_t key = 0;              // shared secret for the pseudo-random carrier
  std::vector<bool> payload;          // 1..64 user-chosen bits; never personal or tracking data
  double strength_db = -52.0;         // carrier amplitude in dBFS, range -80..-20
};
struct EmbedReport {
  fk::audio::AudioBuffer audio;       // NEW buffer; the input is never modified
  double watermark_to_signal_db = 0;  // watermark RMS relative to program RMS
  double peak_before = 0, peak_after = 0;
  std::size_t segment_frames = 0;
};
Result<EmbedReport> embed(const fk::audio::AudioBuffer& in, const EmbedParams& p);
enum class Verdict { NoSignatureDetected, PossibleSignature, SignatureLikeResidualDetected };
struct Detection {
  Verdict verdict = Verdict::NoSignatureDetected;
  double z_score = 0;                 // statistic vs. 64 wrong-key nulls
  std::vector<bool> decoded_bits;     // best-effort decode with the supplied key
  std::string text;                   // human-readable summary incl. limitations
};
Result<Detection> detect(const fk::audio::AudioBuffer& in, std::uint64_t key, std::size_t payload_bits);
std::string verdict_name(Verdict v);
}  // namespace fk::watermark
