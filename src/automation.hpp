#pragma once
// fk::lanes: breakpoint automation lanes (translated from SoundCraft crates/model/src/automation.rs, MIT OR Apache-2.0;
// see third_party/soundcraft_notice/). Pure data + math: no I/O, no threads, no network.
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "result.hpp"
namespace fk::lanes {
using Samples = std::int64_t;

enum class AutomationMode : std::uint8_t { Off, Read, Touch, Latch, TouchLatch, Write, Trim };
const char* mode_label(AutomationMode m);                       // "off","read","touch","latch","touch/latch","write","trim"
std::optional<AutomationMode> parse_mode(const std::string& s); // accepts label, label with '/'->'_', case-insensitive
inline bool reads(AutomationMode m) { return m != AutomationMode::Off; }

enum class ParamKind : std::uint8_t { Volume, Pan, Mute, SendLevel, SendPan, SendMute, Plugin };
struct ParamRange { double lo, hi, def; };
struct AutoParam {
  ParamKind kind = ParamKind::Volume;
  std::uint8_t index = 0;   // Pan: which panner (0 left, 1 right); Send*: send slot; Plugin: insert slot
  std::string param;        // Plugin only: parameter id
  static AutoParam volume() { return {}; }
  static AutoParam pan(std::uint8_t i = 0) { return {ParamKind::Pan, i, {}}; }
  static AutoParam mute() { return {ParamKind::Mute, 0, {}}; }
  static AutoParam send_level(std::uint8_t s) { return {ParamKind::SendLevel, s, {}}; }
  static AutoParam send_pan(std::uint8_t s) { return {ParamKind::SendPan, s, {}}; }
  static AutoParam send_mute(std::uint8_t s) { return {ParamKind::SendMute, s, {}}; }
  static AutoParam plugin(std::uint8_t slot, std::string p) { return {ParamKind::Plugin, slot, std::move(p)}; }
  // ids: volume, pan, pan2, mute, send_a_level, send_b_pan, send_c_mute, plugin:a:threshold
  static std::optional<AutoParam> parse(const std::string& id);
  std::string id() const;      // canonical id, round-trips through parse()
  std::string label() const;   // human label, e.g. "send A level"
  ParamRange range() const;
  bool is_step() const { return kind == ParamKind::Mute || kind == ParamKind::SendMute; }   // steps instead of ramping
  bool is_fader() const { return kind == ParamKind::Volume || kind == ParamKind::Pan || kind == ParamKind::SendLevel || kind == ParamKind::SendPan; }
  bool operator==(const AutoParam& o) const { return kind == o.kind && index == o.index && param == o.param; }
};
char slot_letter(std::uint8_t i);

struct AutomationPoint { Samples at = 0; double value = 0; };
struct AutomationLane {
  AutoParam param;
  std::vector<AutomationPoint> points;   // always sorted by time, unique times
  bool enabled = true;
  explicit AutomationLane(AutoParam p = {}) : param(std::move(p)) {}
  double value_at(Samples at, double default_value) const;   // linear interpolation; steps for mute lanes
  void set_point(Samples at, double value);                  // insert/replace; clamps to the param range; NaN -> 0
  std::size_t clear_range(Samples start, Samples end);       // removes points in [start, end)
  void write_range(Samples start, Samples end, double value, double default_value);  // constant over range, outside intact
  std::size_t thin(double tolerance);                        // drop points whose removal changes the curve <= tolerance
  void shift_from(Samples from, Samples delta);              // Insert Time / Shuffle
};
}  // namespace fk::lanes
