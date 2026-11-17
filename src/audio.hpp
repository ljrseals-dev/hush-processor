#pragma once
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
namespace fk::audio {
struct AudioBuffer { std::uint32_t sample_rate{}; std::uint16_t channels{}; std::vector<float> samples; std::size_t frames() const; void validate() const; };
struct Stats { double peak{}; double rms{}; double dc{}; std::size_t clipped_samples{}; };
AudioBuffer read_wav(const std::filesystem::path& path);
void write_wav_pcm16(const std::filesystem::path& path, const AudioBuffer& audio, bool overwrite = true);
Stats analyze(const AudioBuffer& audio);
AudioBuffer apply_gain(const AudioBuffer& audio, double db);
AudioBuffer mix(const std::vector<AudioBuffer>& inputs, const std::vector<double>& gains_db);
AudioBuffer make_tone(double seconds, double hz, double amplitude, std::uint32_t sample_rate, std::uint16_t channels);
std::string describe(const AudioBuffer& audio);
}
