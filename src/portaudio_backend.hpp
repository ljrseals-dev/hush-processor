#pragma once
#include "audio_engine.hpp"
#include <memory>
#include <filesystem>
#include <string>
#include <vector>

namespace hush::rt {
struct OutputDeviceInfo { int index{-1}; std::string name; int max_output_channels{}; double default_sample_rate{}; };
class PortAudioBackend final : public AudioDeviceBackend {
 public:
  PortAudioBackend();
  ~PortAudioBackend() override;
  PortAudioBackend(const PortAudioBackend&) = delete;
  PortAudioBackend& operator=(const PortAudioBackend&) = delete;
  static bool available() noexcept;
  static fk::Result<std::vector<OutputDeviceInfo>> enumerate_outputs();
  fk::Status start(std::uint32_t sample_rate, std::uint16_t channels, std::size_t block_frames,
                   Callback callback, void* context) override;
  // Duplex capture mode. Audio is captured into a bounded preallocated buffer;
  // file I/O occurs only after stop_recording(), never in the PortAudio callback.
  fk::Status start_recording(std::uint32_t sample_rate, std::uint16_t channels, std::size_t block_frames,
                             Callback playback_callback, void* context, std::uint32_t max_seconds = 300);
  fk::Result<std::size_t> stop_recording(const std::filesystem::path& destination, bool overwrite = false);
  bool recording() const noexcept;
  std::size_t captured_frames() const noexcept;
  void stop() noexcept override;
  const char* name() const noexcept override { return "PortAudio"; }
 private:
  struct Impl;
  std::unique_ptr<Impl> impl_;
};
} // namespace hush::rt
