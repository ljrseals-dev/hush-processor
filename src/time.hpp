#pragma once
// fk::time: tempo/meter map, bars|beats|ticks, sample conversion, SMPTE timecode.
#include <cstdint>
#include <string>
#include <vector>
#include "result.hpp"
namespace fk::time {
struct Meter { int numerator = 4; int denominator = 4; };
struct Bbt { std::int64_t bar = 1; int beat = 1; int tick = 0; };  // 1-based bar/beat, 960 ticks per beat
class TempoMap {
 public:
  static constexpr int kTicksPerBeat = 960;
  TempoMap();
  Status set_meter(Meter m);                  // denominator must be 1,2,4,8,16,32; numerator 1..64
  Status add_tempo(double beat, double bpm);  // beat in quarter notes >= 0; bpm 10..960
  const Meter& meter() const { return meter_; }
  Result<double> beats_to_seconds(double quarter_beats) const;
  Result<double> seconds_to_beats(double seconds) const;
  Result<std::int64_t> beats_to_samples(double quarter_beats, std::uint32_t sample_rate) const;
  Result<double> samples_to_beats(std::int64_t samples, std::uint32_t sample_rate) const;
  Result<Bbt> to_bbt(double quarter_beats) const;
  Result<double> from_bbt(const Bbt& b) const;
 private:
  struct Point { double beat; double bpm; };
  std::vector<Point> points_;
  Meter meter_;
  double beat_unit_quarters() const { return 4.0 / meter_.denominator; }
};
enum class Fps { F24, F25, F30, F2997Drop };
Result<std::string> frames_to_timecode(std::int64_t frame, Fps fps);
Result<std::int64_t> timecode_to_frames(const std::string& tc, Fps fps);
double fps_value(Fps fps);
}  // namespace fk::time
