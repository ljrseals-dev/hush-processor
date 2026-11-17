#include "plugin_host.hpp"
#include <algorithm>
#include <cmath>
namespace hush::plugins {
fk::Status PluginChain::add(std::shared_ptr<AudioPlugin> plugin, const ProcessSpec& spec) {
  if (!plugin) return fk::Status::fail("plugin instance is null");
  if (!spec.sample_rate || !spec.max_block_frames || (spec.channels != 1 && spec.channels != 2))
    return fk::Status::fail("invalid plugin process specification");
  const char* id = plugin->id();
  if (!id || !*id || std::char_traits<char>::length(id) > 128) return fk::Status::fail("plugin id must be 1..128 bytes");
  if (prepared_ && (spec.sample_rate != spec_.sample_rate || spec.max_block_frames != spec_.max_block_frames || spec.channels != spec_.channels))
    return fk::Status::fail("all plugins in a chain must share one process specification");
  auto status = plugin->prepare(spec);
  if (!status) return status;
  try { plugins_.push_back(std::move(plugin)); }
  catch (...) { return fk::Status::fail("could not allocate plugin chain entry"); }
  spec_ = spec; prepared_ = true; return fk::Status::success();
}
void PluginChain::process(float* samples, std::size_t frames, std::uint16_t channels) noexcept {
  if (!prepared_ || !samples || channels != spec_.channels || frames > spec_.max_block_frames) return;
  for (auto& plugin : plugins_) plugin->process(samples, frames, channels);
}
} // namespace hush::plugins
