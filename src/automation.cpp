#include "automation.hpp"
#include <algorithm>
#include <cctype>
#include <cfloat>
#include <cmath>
#include <limits>
namespace fk::lanes {
namespace {
Samples sat_add(Samples a, Samples b) {
  if (b > 0 && a > std::numeric_limits<Samples>::max() - b) return std::numeric_limits<Samples>::max();
  if (b < 0 && a < std::numeric_limits<Samples>::min() - b) return std::numeric_limits<Samples>::min();
  return a + b;
}
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
std::optional<std::uint8_t> letter_slot(const std::string& s) {
  if (s.empty()) return std::nullopt;
  if (s.size() == 1 && std::isalpha(static_cast<unsigned char>(s[0]))) return static_cast<std::uint8_t>(std::toupper(static_cast<unsigned char>(s[0])) - 'A');
  unsigned v = 0;
  for (char c : s) { if (!std::isdigit(static_cast<unsigned char>(c))) return std::nullopt; v = v * 10 + static_cast<unsigned>(c - '0'); if (v > 255) return std::nullopt; }
  return static_cast<std::uint8_t>(v);
}
bool valid_plugin_param(const std::string& p) {
  if (p.empty() || p.size() > 128) return false;
  for (unsigned char c : p) if (!(std::isalnum(c) || c == '_' || c == '-' || c == '.')) return false;
  return true;
}
}  // namespace
const char* mode_label(AutomationMode m) {
  switch (m) {
    case AutomationMode::Off: return "off";
    case AutomationMode::Read: return "read";
    case AutomationMode::Touch: return "touch";
    case AutomationMode::Latch: return "latch";
    case AutomationMode::TouchLatch: return "touch/latch";
    case AutomationMode::Write: return "write";
    case AutomationMode::Trim: return "trim";
  }
  return "read";
}
std::optional<AutomationMode> parse_mode(const std::string& in) {
  std::string s = lower(in);
  for (auto m : {AutomationMode::Off, AutomationMode::Read, AutomationMode::Touch, AutomationMode::Latch, AutomationMode::TouchLatch, AutomationMode::Write, AutomationMode::Trim}) {
    std::string l = mode_label(m), u = l;
    std::replace(u.begin(), u.end(), '/', '_');
    std::string camel = (m == AutomationMode::TouchLatch) ? "touchlatch" : l;
    if (s == l || s == u || s == camel) return m;
  }
  return std::nullopt;
}
char slot_letter(std::uint8_t i) { return static_cast<char>('A' + std::min<int>(i, 25)); }
std::optional<AutoParam> AutoParam::parse(const std::string& in) {
  std::string s = lower(in);
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front()))) s.erase(s.begin());
  while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back()))) s.pop_back();
  if (s == "volume" || s == "vol") return volume();
  if (s == "pan" || s == "pan1") return pan(0);
  if (s == "pan2") return pan(1);
  if (s == "mute") return mute();
  if (s.rfind("send_", 0) == 0) {
    std::string rest = s.substr(5);
    auto us = rest.find('_');
    if (us == std::string::npos) return std::nullopt;
    auto slot = letter_slot(rest.substr(0, us));
    if (!slot) return std::nullopt;
    std::string what = rest.substr(us + 1);
    if (what == "level") return send_level(*slot);
    if (what == "pan") return send_pan(*slot);
    if (what == "mute") return send_mute(*slot);
    return std::nullopt;
  }
  if (s.rfind("plugin:", 0) == 0) {
    std::string rest = in.substr(in.find(':') + 1);   // keep the plugin parameter's original case
    auto c = rest.find(':');
    if (c == std::string::npos) return std::nullopt;
    auto slot = letter_slot(lower(rest.substr(0, c)));
    std::string p = rest.substr(c + 1);
    if (!slot || !valid_plugin_param(p)) return std::nullopt;
    return plugin(*slot, p);
  }
  return std::nullopt;
}
std::string AutoParam::id() const {
  auto sl = [&] { return std::string(1, static_cast<char>(std::tolower(slot_letter(index)))); };
  switch (kind) {
    case ParamKind::Volume: return "volume";
    case ParamKind::Pan: return index == 0 ? "pan" : "pan2";
    case ParamKind::Mute: return "mute";
    case ParamKind::SendLevel: return "send_" + sl() + "_level";
    case ParamKind::SendPan: return "send_" + sl() + "_pan";
    case ParamKind::SendMute: return "send_" + sl() + "_mute";
    case ParamKind::Plugin: return "plugin:" + sl() + ":" + param;
  }
  return "volume";
}
std::string AutoParam::label() const {
  switch (kind) {
    case ParamKind::Volume: return "volume";
    case ParamKind::Pan: return index == 0 ? "pan" : "pan " + std::to_string(index + 1);
    case ParamKind::Mute: return "mute";
    case ParamKind::SendLevel: return std::string("send ") + slot_letter(index) + " level";
    case ParamKind::SendPan: return std::string("send ") + slot_letter(index) + " pan";
    case ParamKind::SendMute: return std::string("send ") + slot_letter(index) + " mute";
    case ParamKind::Plugin: return std::string("insert ") + slot_letter(index) + " " + param;
  }
  return "";
}
ParamRange AutoParam::range() const {
  switch (kind) {
    case ParamKind::Volume: case ParamKind::SendLevel: return {-144.0, 12.0, 0.0};
    case ParamKind::Pan: case ParamKind::SendPan: return {-1.0, 1.0, 0.0};
    case ParamKind::Mute: case ParamKind::SendMute: return {0.0, 1.0, 0.0};
    case ParamKind::Plugin: return {-static_cast<double>(FLT_MAX), static_cast<double>(FLT_MAX), 0.0};
  }
  return {0, 1, 0};
}
double AutomationLane::value_at(Samples at, double def) const {
  if (points.empty()) return def;
  if (at <= points.front().at) return points.front().value;
  auto it = std::upper_bound(points.begin(), points.end(), at, [](Samples t, const AutomationPoint& p) { return t < p.at; });
  if (it == points.begin()) return def;
  const AutomationPoint& a = *(it - 1);
  if (it != points.end() && it->at > a.at) {
    if (param.is_step()) return a.value;
    return a.value + (it->value - a.value) * (static_cast<double>(at - a.at) / static_cast<double>(it->at - a.at));
  }
  return a.value;
}
void AutomationLane::set_point(Samples at, double value) {
  auto r = param.range();
  value = std::isnan(value) ? 0.0 : std::clamp(value, r.lo, r.hi);
  auto it = std::lower_bound(points.begin(), points.end(), at, [](const AutomationPoint& p, Samples t) { return p.at < t; });
  if (it != points.end() && it->at == at) it->value = value;
  else points.insert(it, AutomationPoint{at, value});
}
std::size_t AutomationLane::clear_range(Samples start, Samples end) {
  auto before = points.size();
  points.erase(std::remove_if(points.begin(), points.end(), [&](const AutomationPoint& p) { return p.at >= start && p.at < end; }), points.end());
  return before - points.size();
}
void AutomationLane::write_range(Samples start, Samples end, double value, double def) {
  if (end <= start) return;
  const double before = value_at(sat_add(start, -1), def), after = value_at(end, def);
  clear_range(start, sat_add(end, 1));
  if (start > 0) set_point(start - 1, before);
  set_point(start, value);
  set_point(end, value);
  set_point(sat_add(end, 1), after);
}
std::size_t AutomationLane::thin(double tol) {
  if (points.size() < 3) return 0;
  std::vector<bool> keep(points.size(), true);
  std::size_t last = 0;
  for (std::size_t i = 1; i + 1 < points.size(); ++i) {
    const auto &a = points[last], &b = points[i], &c = points[i + 1];
    const double span = static_cast<double>(c.at - a.at);
    const double interp = span > 0.0 ? a.value + (c.value - a.value) * (static_cast<double>(b.at - a.at) / span) : a.value;
    if (std::abs(interp - b.value) <= tol) keep[i] = false; else last = i;
  }
  const auto before = points.size();
  std::size_t k = 0;
  points.erase(std::remove_if(points.begin(), points.end(), [&](const AutomationPoint&) { return !keep[k++]; }), points.end());
  return before - points.size();
}
void AutomationLane::shift_from(Samples from, Samples delta) {
  for (auto& p : points) if (p.at >= from) p.at = std::max<Samples>(0, sat_add(p.at, delta));
  std::stable_sort(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.at < b.at; });
  points.erase(std::unique(points.begin(), points.end(), [](const AutomationPoint& a, const AutomationPoint& b) { return a.at == b.at; }), points.end());
}
}  // namespace fk::lanes
