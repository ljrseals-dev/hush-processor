#include "time.hpp"
#include <algorithm>
#include <cmath>
#include <cstdio>
namespace fk::time {
TempoMap::TempoMap() { points_.push_back({0.0, 120.0}); }
Status TempoMap::set_meter(Meter m) {
  bool den_ok = m.denominator == 1 || m.denominator == 2 || m.denominator == 4 ||
                m.denominator == 8 || m.denominator == 16 || m.denominator == 32;
  if (!den_ok) return Status::fail("meter denominator must be 1, 2, 4, 8, 16 or 32");
  if (m.numerator < 1 || m.numerator > 64) return Status::fail("meter numerator must be 1..64");
  meter_ = m;
  return Status::success();
}
Status TempoMap::add_tempo(double beat, double bpm) {
  if (!std::isfinite(beat) || beat < 0) return Status::fail("tempo position must be finite and >= 0");
  if (!std::isfinite(bpm) || bpm < 10 || bpm > 960) return Status::fail("tempo must be 10..960 BPM");
  for (auto& p : points_)
    if (p.beat == beat) { p.bpm = bpm; return Status::success(); }
  points_.push_back({beat, bpm});
  std::sort(points_.begin(), points_.end(), [](const Point& a, const Point& b) { return a.beat < b.beat; });
  return Status::success();
}
Result<double> TempoMap::beats_to_seconds(double beats) const {
  if (!std::isfinite(beats) || beats < 0) return Result<double>::fail("beat position must be finite and >= 0");
  double sec = 0;
  for (std::size_t i = 0; i < points_.size(); ++i) {
    double start = points_[i].beat;
    if (beats <= start) break;
    double end = (i + 1 < points_.size()) ? std::min(points_[i + 1].beat, beats) : beats;
    sec += (end - start) * 60.0 / points_[i].bpm;
  }
  return Result<double>::success(sec);
}
Result<double> TempoMap::seconds_to_beats(double seconds) const {
  if (!std::isfinite(seconds) || seconds < 0) return Result<double>::fail("time must be finite and >= 0");
  double remaining = seconds;
  for (std::size_t i = 0; i < points_.size(); ++i) {
    double spb = 60.0 / points_[i].bpm;
    if (i + 1 < points_.size()) {
      double seg = (points_[i + 1].beat - points_[i].beat) * spb;
      if (remaining >= seg) { remaining -= seg; continue; }
    }
    return Result<double>::success(points_[i].beat + remaining / spb);
  }
  return Result<double>::fail("internal: empty tempo map");
}
Result<std::int64_t> TempoMap::beats_to_samples(double beats, std::uint32_t sr) const {
  if (sr == 0) return Result<std::int64_t>::fail("sample rate must be > 0");
  auto s = beats_to_seconds(beats);
  if (!s) return Result<std::int64_t>::fail(s.error);
  double v = *s * sr;
  if (v > 9.0e15) return Result<std::int64_t>::fail("position too large");
  return Result<std::int64_t>::success(static_cast<std::int64_t>(std::llround(v)));
}
Result<double> TempoMap::samples_to_beats(std::int64_t samples, std::uint32_t sr) const {
  if (sr == 0 || samples < 0) return Result<double>::fail("invalid sample position or rate");
  return seconds_to_beats(static_cast<double>(samples) / sr);
}
Result<Bbt> TempoMap::to_bbt(double quarter_beats) const {
  if (!std::isfinite(quarter_beats) || quarter_beats < 0 || quarter_beats > 1.0e9)
    return Result<Bbt>::fail("beat position out of range");
  std::int64_t total = std::llround(quarter_beats / beat_unit_quarters() * kTicksPerBeat);
  std::int64_t tpb = static_cast<std::int64_t>(meter_.numerator) * kTicksPerBeat;
  Bbt b;
  b.bar = total / tpb + 1;
  std::int64_t in_bar = total % tpb;
  b.beat = static_cast<int>(in_bar / kTicksPerBeat) + 1;
  b.tick = static_cast<int>(in_bar % kTicksPerBeat);
  return Result<Bbt>::success(b);
}
Result<double> TempoMap::from_bbt(const Bbt& b) const {
  if (b.bar < 1 || b.beat < 1 || b.beat > meter_.numerator || b.tick < 0 || b.tick >= kTicksPerBeat)
    return Result<double>::fail("invalid bar|beat|tick for current meter");
  double beats_units = double(b.bar - 1) * meter_.numerator + (b.beat - 1) + double(b.tick) / kTicksPerBeat;
  return Result<double>::success(beats_units * beat_unit_quarters());
}
double fps_value(Fps f) {
  switch (f) {
    case Fps::F24: return 24.0;
    case Fps::F25: return 25.0;
    case Fps::F30: return 30.0;
    case Fps::F2997Drop: return 30000.0 / 1001.0;
  }
  return 30.0;
}
namespace {
int nominal(Fps f) { return f == Fps::F24 ? 24 : f == Fps::F25 ? 25 : 30; }
}
Result<std::string> frames_to_timecode(std::int64_t n, Fps fps) {
  if (n < 0 || n > 24LL * 3600 * 31) return Result<std::string>::fail("frame count out of range (0..24h)");
  int fr = nominal(fps);
  if (fps == Fps::F2997Drop) {
    std::int64_t d = n / 17982, m = n % 17982;
    n += 18 * d + (m < 2 ? 0 : 2 * ((m - 2) / 1798));
  }
  int ff = static_cast<int>(n % fr);
  std::int64_t secs = n / fr;
  char buf[32];
  std::snprintf(buf, sizeof buf, "%02lld:%02lld:%02lld%c%02d", static_cast<long long>((secs / 3600) % 24),
                static_cast<long long>((secs / 60) % 60), static_cast<long long>(secs % 60),
                fps == Fps::F2997Drop ? ';' : ':', ff);
  return Result<std::string>::success(buf);
}
Result<std::int64_t> timecode_to_frames(const std::string& tc, Fps fps) {
  int hh, mm, ss, ff;
  char sep;
  char tail;
  if (std::sscanf(tc.c_str(), "%d:%d:%d%c%d%c", &hh, &mm, &ss, &sep, &ff, &tail) != 5 || (sep != ':' && sep != ';'))
    return Result<std::int64_t>::fail("timecode must look like HH:MM:SS:FF (or HH:MM:SS;FF for drop-frame)");
  int fr = nominal(fps);
  if (hh < 0 || hh > 23 || mm < 0 || mm > 59 || ss < 0 || ss > 59 || ff < 0 || ff >= fr)
    return Result<std::int64_t>::fail("timecode field out of range");
  std::int64_t n = (static_cast<std::int64_t>(hh) * 3600 + mm * 60 + ss) * fr + ff;
  if (fps == Fps::F2997Drop) {
    if (ss == 0 && ff < 2 && mm % 10 != 0) return Result<std::int64_t>::fail("dropped frame label does not exist");
    std::int64_t total_min = static_cast<std::int64_t>(hh) * 60 + mm;
    n -= 2 * (total_min - total_min / 10);
  }
  return Result<std::int64_t>::success(n);
}
}  // namespace fk::time
