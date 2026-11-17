#include "counter.hpp"
#include <cmath>
#include <cstdio>
namespace fk::counter {
Result<std::string> format_min_sec(std::int64_t s, std::uint32_t sr) {
  if (sr == 0 || s < 0) return Result<std::string>::fail("invalid position or sample rate");
  std::int64_t ms = static_cast<std::int64_t>(std::llround(static_cast<double>(s) * 1000.0 / sr));
  char b[48];
  std::snprintf(b, sizeof b, "%lld:%02lld:%02lld.%03lld", static_cast<long long>(ms / 3600000), static_cast<long long>((ms / 60000) % 60),
                static_cast<long long>((ms / 1000) % 60), static_cast<long long>(ms % 1000));
  return Result<std::string>::success(b);
}
Result<Readout> format_all(std::int64_t s, std::uint32_t sr, const fk::time::TempoMap& tempo, fk::time::Fps fps) {
  using R = Result<Readout>;
  if (sr == 0 || s < 0) return R::fail("invalid position or sample rate");
  Readout r;
  r.samples = std::to_string(s);
  auto ms = format_min_sec(s, sr); if (!ms) return R::fail(ms.error); r.min_sec = *ms;
  auto beats = tempo.samples_to_beats(s, sr); if (!beats) return R::fail(beats.error);
  auto bbt = tempo.to_bbt(*beats); if (!bbt) return R::fail(bbt.error);
  char b[48]; std::snprintf(b, sizeof b, "%lld|%d|%03d", static_cast<long long>(bbt->bar), bbt->beat, bbt->tick); r.bars_beats = b;
  auto frame = static_cast<std::int64_t>(std::floor(static_cast<double>(s) / sr * fk::time::fps_value(fps)));
  auto tc = fk::time::frames_to_timecode(frame, fps); if (!tc) return R::fail(tc.error); r.timecode = *tc;
  return R::success(std::move(r));
}
}  // namespace fk::counter
