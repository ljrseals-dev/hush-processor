#pragma once
#include "result.hpp"
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace hush::plugins {
struct ProcessSpec { std::uint32_t sample_rate{}; std::uint32_t max_block_frames{}; std::uint16_t channels{}; };
// Explicit in-process DSP contract. Implementations must not allocate, block, perform I/O,
// or throw from process(). No binary plugin loading is performed by this interface.
class AudioPlugin {
 public:
  virtual ~AudioPlugin() = default;
  virtual const char* id() const noexcept = 0;
  virtual fk::Status prepare(const ProcessSpec& spec) = 0;
  virtual void process(float* interleaved, std::size_t frames, std::uint16_t channels) noexcept = 0;
  virtual void reset() noexcept = 0;
};
class PluginChain {
 public:
  fk::Status add(std::shared_ptr<AudioPlugin> plugin, const ProcessSpec& spec);
  void clear() noexcept { plugins_.clear(); prepared_ = false; }
  void process(float* samples, std::size_t frames, std::uint16_t channels) noexcept;
  std::size_t size() const noexcept { return plugins_.size(); }
 private:
  std::vector<std::shared_ptr<AudioPlugin>> plugins_;
  ProcessSpec spec_{};
  bool prepared_{};
};
} // namespace hush::plugins
