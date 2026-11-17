#include "forensics.hpp"
#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <sstream>
#include "core/CodecScanner.h"
#include "core/DetectorTypes.h"
#include "core/ENFAnalyzer.h"
#include "core/UltrasonicScanner.h"
#include "core/WatermarkDetector.h"
namespace fk::forensics {
namespace {
using dawchecker::dsp::DetectorResult;
using dawchecker::dsp::DetectorStatus;
const char* status_name(DetectorStatus s) {
  return s == DetectorStatus::Alert ? "Alert" : s == DetectorStatus::Active ? "Active" : "Idle";
}
std::vector<double> to_mono(const fk::audio::AudioBuffer& a) {
  std::size_t fr = a.frames();
  std::vector<double> m(fr);
  for (std::size_t i = 0; i < fr; ++i) {
    double s = 0;
    for (std::uint16_t c = 0; c < a.channels; ++c) s += a.samples[i * a.channels + c];
    m[i] = s / a.channels;
  }
  return m;
}
void keep_worst(Evidence& e, const DetectorResult& r) {
  bool worse = static_cast<int>(r.status) > static_cast<int>(
      e.status == "Alert" ? DetectorStatus::Alert : e.status == "Active" ? DetectorStatus::Active : DetectorStatus::Idle);
  if (e.label.empty() || worse || (r.status == DetectorStatus::Idle && e.status == "Idle" && r.confidence > e.confidence)) {
    if (worse || e.label.empty() || r.confidence > e.confidence) {
      e.status = status_name(r.status); e.label = r.label; e.location_hz = r.locationHz;
    }
  }
  e.confidence = std::max(e.confidence, r.confidence);
}

// ---- HUSH-native, amplitude-calibrated narrowband measurements (new code, not from DawChecker) ----
double hann_goertzel_dbfs(const double* x, std::size_t n, double f, double sr) {
  const double pi = 3.14159265358979323846;
  double w = 2.0 * pi * f / sr, c = 2.0 * std::cos(w), s1 = 0, s2 = 0, wsum = 0;
  for (std::size_t i = 0; i < n; ++i) {
    double h = 0.5 - 0.5 * std::cos(2.0 * pi * double(i) / double(n - 1));
    wsum += h;
    double s = x[i] * h + c * s1 - s2;
    s2 = s1; s1 = s;
  }
  double re = s1 - s2 * std::cos(w), im = s2 * std::sin(w);
  double amp = 2.0 * std::sqrt(re * re + im * im) / wsum;
  return amp > 1e-12 ? 20.0 * std::log10(amp) : -240.0;
}
Evidence native_ultrasonic(const std::vector<double>& m, double sr) {
  Evidence e; e.module = "ultrasonic narrowband scan (HUSH-native)";
  if (sr < 40000) { e.applicable = false; e.note = "sample rate below 40 kHz: scan bands are above Nyquist"; return e; }
  const double bands[] = {17400, 18000, 18500, 19200, 20150};
  const std::size_t N = 4096, maxw = static_cast<std::size_t>(600.0 * sr / N);
  double best_amp = -240, best_f = 0, best_margin = 0; std::size_t hits = 0, wins = 0;
  for (std::size_t s = 0; s + N <= m.size() && wins < maxw; s += N, ++wins) {
    bool hit = false;
    for (double f : bands) {
      if (f >= sr * 0.49) continue;
      double a = hann_goertzel_dbfs(&m[s], N, f, sr);
      double nb = std::max({hann_goertzel_dbfs(&m[s], N, f - 400, sr), hann_goertzel_dbfs(&m[s], N, f + 400, sr)});
      double margin = a - nb;
      if (a > -80.0 && margin >= 20.0) { hit = true; if (a > best_amp) { best_amp = a; best_f = f; best_margin = margin; } }
    }
    if (hit) ++hits;
  }
  e.location_hz = best_f; e.confidence = wins ? 100.0 * double(hits) / double(wins) : 0.0;
  e.status = (wins && hits * 2 >= wins) ? "Narrowband component present" : (hits ? "Intermittent narrowband component" : "Not detected");
  e.label = hits ? "tone-like energy near " + std::to_string(int(best_f)) + " Hz at " + std::to_string(best_amp) + " dBFS, " +
                   std::to_string(best_margin) + " dB above adjacent frequencies"
                 : "no tone-like energy above -80 dBFS in the 17.4-20.15 kHz bands";
  e.note = "confidence = percent of 4096-frame windows with a hit (not a probability); wins=" + std::to_string(wins);
  return e;
}
Evidence native_enf(const std::vector<double>& m, double sr) {
  Evidence e; e.module = "mains-hum (ENF) narrowband scan (HUSH-native)";
  if (sr < 1000) { e.applicable = false; e.note = "sample rate too low"; return e; }
  const std::size_t N = static_cast<std::size_t>(2.0 * sr), maxw = 60;
  if (m.size() < N) { e.applicable = false; e.note = "need at least 2 s of audio"; return e; }
  struct Track { double base; std::size_t hits = 0; double fmin = 1e9, fmax = -1e9, bestdb = -240; };
  Track t[2] = {{50.0}, {60.0}};
  std::size_t wins = 0;
  for (std::size_t s = 0; s + N <= m.size() && wins < maxw; s += N, ++wins) {
    for (auto& tr : t) {
      double pk = -240, pf = tr.base;
      for (double f = tr.base - 1.0; f <= tr.base + 1.0001; f += 0.05) {
        double a = hann_goertzel_dbfs(&m[s], N, f, sr);
        if (a > pk) { pk = a; pf = f; }
      }
      double nb = 0; int k = 0;
      for (double off : {-5.0, -4.0, 4.0, 5.0}) { nb += std::pow(10.0, hann_goertzel_dbfs(&m[s], N, tr.base + off, sr) / 20.0); ++k; }
      double nb_db = 20.0 * std::log10(std::max(nb / k, 1e-12));
      if (pk > -90.0 && pk - nb_db >= 15.0) {
        ++tr.hits; tr.fmin = std::min(tr.fmin, pf); tr.fmax = std::max(tr.fmax, pf); tr.bestdb = std::max(tr.bestdb, pk);
      }
    }
  }
  const Track& w = t[0].hits >= t[1].hits ? t[0] : t[1];
  e.location_hz = w.hits ? (w.fmin + w.fmax) / 2 : 0;
  e.confidence = wins ? 100.0 * double(w.hits) / double(wins) : 0;
  e.status = (wins && w.hits * 2 >= wins) ? "Narrowband component present" : (w.hits ? "Intermittent narrowband component" : "Not detected");
  e.label = w.hits ? "narrowband energy near " + std::to_string(int(w.base)) + " Hz (observed " + std::to_string(w.fmin) + "-" +
                       std::to_string(w.fmax) + " Hz, peak " + std::to_string(w.bestdb) + " dBFS)"
                   : "no narrowband component stands out near 50 or 60 Hz";
  e.note = "uses up to the first 120 s in 2 s windows; mains hum is common in ordinary recordings and is not by itself evidence of anything";
  return e;
}
}  // namespace
Result<Report> analyze(const fk::audio::AudioBuffer& live, const fk::audio::AudioBuffer* ref, const std::string& sha) {
  using R = Result<Report>;
  try { live.validate(); } catch (const std::exception& e) { return R::fail(e.what()); }
  Report rep;
  rep.file_sha256 = sha;
  rep.analysis_version = "hush-forensics 2.1.0 (DawChecker hardened fork + HUSH-native scans)";
  const double sr = live.sample_rate;
  rep.settings = "native scans: 4096-frame ultrasonic windows, 2 s mains windows; fork detectors: streaming, worst verdict reported";
  auto m = to_mono(live);
  rep.evidence.push_back(native_ultrasonic(m, sr));
  rep.evidence.push_back(native_enf(m, sr));
  const char* kForkNote = "hardened-fork detector (streaming, real-time safe); calibrated only on a synthetic corpus, see MSV_ENGINE_NOTES.md; indicator only";
  // ENF (hardened fork): streaming over the whole file, worst verdict reported
  Evidence enf; enf.module = "ENF detector (DawChecker hardened fork)";
  if (sr < 1000) { enf.applicable = false; enf.note = "sample rate too low"; }
  else {
    dawchecker::dsp::ENFAnalyzer a(sr);
    for (std::size_t i = 0; i < m.size(); ++i) keep_worst(enf, a.process(m[i]));
    enf.note = std::string(kForkNote) + "; needs >= 4 s of audio; never goes above Active";
  }
  rep.evidence.push_back(enf);
  // Ultrasonic (hardened fork)
  Evidence us; us.module = "ultrasonic detector (DawChecker hardened fork)";
  if (sr < 40000) { us.applicable = false; us.note = "sample rate below 40 kHz: the 17-20 kHz bands are too close to or above Nyquist"; }
  else {
    dawchecker::dsp::UltrasonicScanner a(sr);
    for (std::size_t i = 0; i < m.size(); ++i) keep_worst(us, a.process(m[i]));
    us.note = std::string(kForkNote) + "; detects any steady narrowband tone in the band, not specifically a beacon";
  }
  rep.evidence.push_back(us);
  // Watermark residual (hardened fork; needs a reference)
  Evidence wm; wm.module = "reference-residual detector (DawChecker hardened fork)";
  if (!ref) { wm.applicable = false; wm.note = "no reference signal supplied; this check compares live vs reference"; }
  else if (ref->sample_rate != live.sample_rate || ref->channels != live.channels || ref->frames() != live.frames()) {
    return R::fail("reference must match sample rate, channel count and length");
  } else {
    auto rm = to_mono(*ref);
    dawchecker::dsp::WatermarkDetector d(sr, 5.0);
    for (std::size_t i = 0; i < m.size(); ++i) keep_worst(wm, d.process(m[i], rm[i]));
    wm.note = std::string(kForkNote) + "; reports a persistent low-level difference, which is NOT specific to watermarks";
  }
  rep.evidence.push_back(wm);
  rep.limitations = {
      "The hardened-fork detectors and the HUSH-native scans are narrowband/level indicators calibrated only on a synthetic corpus (no real recordings). Real speech/music, codecs and room hum were not tested. Neither is a validated forensic method.",
      "Results are indicators only, not proof of tampering, authenticity, or authorship, and are not court-defensible.",
      "Absence of a finding does not prove absence of a watermark, beacon or ENF trace.",
      "The original file is never modified; this report is only written when you ask for it."};
  return R::success(std::move(rep));
}
std::string Report::to_text() const {
  std::ostringstream o;
  o << "HUSH forensic report\nanalysis: " << analysis_version << "\nsettings: " << settings << "\n";
  o << "file_sha256: " << (file_sha256.empty() ? "(not computed)" : file_sha256) << "\n\n";
  for (auto& e : evidence) {
    o << "- " << e.module << "\n";
    if (!e.applicable) { o << "    not applicable: " << e.note << "\n"; continue; }
    o << "    status=" << e.status << " confidence=" << e.confidence << " location_hz=" << e.location_hz << "\n";
    o << "    detector says: " << e.label << "\n";
    if (!e.note.empty()) o << "    note: " << e.note << "\n";
  }
  o << "\nLimitations:\n";
  for (auto& l : limitations) o << "  * " << l << "\n";
  return o.str();
}
std::string scan_devices_text() {
  dawchecker::codec::CodecScanner s;
  auto findings = s.scan();
  std::ostringstream o;
  if (findings.empty()) o << "no devices enumerated (no audio backend compiled into this build)\n";
  for (auto& f : findings)
    o << f.deviceName << " [" << f.hostApi << "] codec=" << f.matchedCodec << " risk="
      << (f.risk == dawchecker::codec::RiskLevel::Alert ? "Alert" : f.risk == dawchecker::codec::RiskLevel::Caution ? "Caution" : "Info")
      << " - " << f.note << "\n";
  o << "These are name-based indicators, not proof that a driver is malicious.\n";
  return o.str();
}
}  // namespace fk::forensics
