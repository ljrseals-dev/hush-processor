#include "midi.hpp"
namespace hush::midi {
fk::Status validate_event(const Event& e) noexcept {
  const auto family = static_cast<std::uint8_t>(e.status & 0xF0U);
  if ((e.status & 0x80U) == 0 || e.status >= 0xF0U) return fk::Status::fail("only channel voice MIDI messages are supported");
  if (e.data1 > 127 || e.data2 > 127) return fk::Status::fail("MIDI data bytes must be 0..127");
  if (family != 0x80U && family != 0x90U && family != 0xA0U && family != 0xB0U && family != 0xC0U && family != 0xD0U && family != 0xE0U)
    return fk::Status::fail("unsupported MIDI status family");
  if ((family == 0xC0U || family == 0xD0U) && e.data2 != 0) return fk::Status::fail("program/channel pressure messages require zero data2 in this event representation");
  return fk::Status::success();
}
} // namespace hush::midi
