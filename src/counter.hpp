#pragma once
// Big Counter: the position read-out shown by a large clock window, as pure formatting functions.
#include <cstdint>
#include <string>
#include "result.hpp"
#include "time.hpp"
namespace fk::counter {
struct Readout { std::string timecode, bars_beats, min_sec, samples; };
// Formats one position (in samples at `sample_rate`) in every counter mode. Errors: negative position, zero sample rate.
Result<Readout> format_all(std::int64_t samples, std::uint32_t sample_rate, const fk::time::TempoMap& tempo, fk::time::Fps fps);
Result<std::string> format_min_sec(std::int64_t samples, std::uint32_t sample_rate);   // H:MM:SS.mmm
}  // namespace fk::counter
