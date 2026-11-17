#pragma once
#include "audio.hpp"
#include "result.hpp"
#include "session.hpp"
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <vector>

namespace hush::rt {
// Audio callback core. Session loading allocates and decodes ahead of playback;
// process() is bounded, does not allocate, and is intended for one device callback thread.
class AudioEngine {
 public:
  fk::Status load(const fk::engine::Session& session, const std::filesystem::path& base_dir);
  void play() noexcept { playing_.store(true, std::memory_order_release); }
  void pause() noexcept { playing_.store(false, std::memory_order_release); }
  void stop() noexcept;
  bool playing() const noexcept { return playing_.load(std::memory_order_acquire); }
  std::uint64_t playhead() const noexcept { return playhead_.load(std::memory_order_acquire); }
  fk::Status seek(std::uint64_t frame) noexcept;
  void set_master_gain_db(float db) noexcept;
  fk::Status set_track_gain_db(std::size_t track, float db) noexcept;
  fk::Status set_track_mute(std::size_t track, bool mute) noexcept;
  float output_peak() const noexcept { return output_peak_.load(std::memory_order_relaxed); }
  // Output must be preallocated interleaved mono or stereo float audio.
  // Returns number of timeline frames rendered. Remaining output is silence.
  std::size_t process(float* output, std::size_t frames, std::uint16_t output_channels) noexcept;
  std::uint32_t sample_rate() const noexcept { return sample_rate_; }
 private:
  struct ClipData { std::uint64_t start{}, source{}, length{}, fade_in{}, fade_out{}; std::uint16_t channels{}; std::vector<float> samples; };
  struct TrackData {
    std::shared_ptr<std::atomic<float>> gain{std::make_shared<std::atomic<float>>(1.0f)};
    std::shared_ptr<std::atomic<bool>> mute{std::make_shared<std::atomic<bool>>(false)};
    std::vector<ClipData> clips;
  };
  std::vector<TrackData> tracks_;
  std::uint32_t sample_rate_{48000};
  std::uint64_t total_frames_{};
  std::atomic<std::uint64_t> playhead_{0};
  std::atomic<bool> playing_{false};
  std::atomic<float> master_gain_{1.0f};
  std::atomic<float> output_peak_{0.0f};
};

// The engine is device-independent. A platform backend owns the hardware callback
// and calls AudioEngine::process; this interface intentionally does not guess or
// silently select a device. No hardware backend is bundled in this source release.
class AudioDeviceBackend {
 public:
  using Callback = std::size_t (*)(void* context, float* output, std::size_t frames, std::uint16_t channels) noexcept;
  virtual ~AudioDeviceBackend() = default;
  virtual fk::Status start(std::uint32_t sample_rate, std::uint16_t channels, std::size_t block_frames,
                           Callback callback, void* context) = 0;
  virtual void stop() noexcept = 0;
  virtual const char* name() const noexcept = 0;
};
} // namespace hush::rt
