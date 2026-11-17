#include "markers.hpp"
#include <algorithm>
#include <cctype>
namespace fk::markers {
const char* kind_label(MarkerKind k) { return k == MarkerKind::Marker ? "marker" : k == MarkerKind::Selection ? "selection" : "none"; }
std::optional<MarkerKind> parse_kind(const std::string& in) {
  std::string s = in; for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  if (s == "marker") return MarkerKind::Marker;
  if (s == "selection") return MarkerKind::Selection;
  if (s == "none") return MarkerKind::None;
  return std::nullopt;
}
Status MemoryLocations::validate(const MemoryLocation& m) {
  if (m.number == 0) return Status::fail("number must be >= 1");
  if (m.name.size() > kMaxName) return Status::fail("name too long (max 256 bytes)");
  if (m.comments.size() > kMaxComment) return Status::fail("comments too long (max 1024 bytes)");
  if (m.start < 0 || m.end < 0) return Status::fail("positions must be >= 0");
  if (m.kind == MarkerKind::Selection && m.end < m.start) return Status::fail("selection end is before its start");
  if (m.ruler < 1 || m.ruler > 5) return Status::fail("ruler must be 1..5");
  return Status::success();
}
Result<std::uint32_t> MemoryLocations::add(MarkerKind kind, std::string name, Samples start, Samples end, std::uint8_t ruler, std::uint32_t number) {
  using R = Result<std::uint32_t>;
  if (items_.size() >= kMax) return R::fail("memory location limit (1000) reached");
  if (number == 0) { number = 1; while (find(number)) ++number; }
  else if (find(number)) return R::fail("number " + std::to_string(number) + " is already in use");
  MemoryLocation m; m.id = next_id_; m.number = number; m.name = std::move(name); m.kind = kind;
  m.start = start; m.end = (kind == MarkerKind::Selection) ? end : start; m.ruler = ruler;
  if (auto s = validate(m); !s) return R::fail(s.error);
  ++next_id_;
  items_.insert(std::lower_bound(items_.begin(), items_.end(), number, [](const MemoryLocation& a, std::uint32_t n) { return a.number < n; }), m);
  return R::success(number);
}
Status MemoryLocations::insert_raw(MemoryLocation m) {
  if (items_.size() >= kMax) return Status::fail("memory location limit (1000) reached");
  if (find(m.number)) return Status::fail("duplicate memory location number " + std::to_string(m.number));
  if (m.kind != MarkerKind::Selection) m.end = m.start;
  if (auto s = validate(m); !s) return s;
  m.id = next_id_++;
  items_.insert(std::lower_bound(items_.begin(), items_.end(), m.number, [](const MemoryLocation& a, std::uint32_t n) { return a.number < n; }), m);
  return Status::success();
}
static MemoryLocation* find_mut(std::vector<MemoryLocation>& v, std::uint32_t n) {
  for (auto& m : v) if (m.number == n) return &m;
  return nullptr;
}
Status MemoryLocations::remove(std::uint32_t n) {
  auto it = std::find_if(items_.begin(), items_.end(), [&](const MemoryLocation& m) { return m.number == n; });
  if (it == items_.end()) return Status::fail("no memory location " + std::to_string(n));
  items_.erase(it); return Status::success();
}
Status MemoryLocations::rename(std::uint32_t n, std::string name) {
  auto* m = find_mut(items_, n); if (!m) return Status::fail("no memory location " + std::to_string(n));
  if (name.size() > kMaxName) return Status::fail("name too long (max 256 bytes)");
  m->name = std::move(name); return Status::success();
}
Status MemoryLocations::set_comments(std::uint32_t n, std::string c) {
  auto* m = find_mut(items_, n); if (!m) return Status::fail("no memory location " + std::to_string(n));
  if (c.size() > kMaxComment) return Status::fail("comments too long (max 1024 bytes)");
  m->comments = std::move(c); return Status::success();
}
Status MemoryLocations::set_color(std::uint32_t n, std::optional<std::array<std::uint8_t, 3>> c) {
  auto* m = find_mut(items_, n); if (!m) return Status::fail("no memory location " + std::to_string(n));
  m->color = c; return Status::success();
}
Status MemoryLocations::set_track(std::uint32_t n, std::optional<std::uint32_t> t) {
  auto* m = find_mut(items_, n); if (!m) return Status::fail("no memory location " + std::to_string(n));
  m->track = t; return Status::success();
}
Status MemoryLocations::relocate(std::uint32_t n, Samples s, Samples e) {
  auto* m = find_mut(items_, n); if (!m) return Status::fail("no memory location " + std::to_string(n));
  MemoryLocation t = *m; t.start = s; t.end = (t.kind == MarkerKind::Selection) ? e : s;
  if (auto v = validate(t); !v) return v;
  *m = t; return Status::success();
}
std::optional<MemoryLocation> MemoryLocations::find(std::uint32_t n) const {
  for (auto& m : items_) if (m.number == n) return m;
  return std::nullopt;
}
Result<MemoryLocation> MemoryLocations::recall(std::uint32_t n) const {
  auto m = find(n);
  return m ? Result<MemoryLocation>::success(*m) : Result<MemoryLocation>::fail("no memory location " + std::to_string(n));
}
std::vector<MemoryLocation> MemoryLocations::sorted_by_time() const {
  auto v = items_;
  std::stable_sort(v.begin(), v.end(), [](const MemoryLocation& a, const MemoryLocation& b) { return a.start != b.start ? a.start < b.start : a.number < b.number; });
  return v;
}
std::optional<MemoryLocation> MemoryLocations::next_after(Samples pos) const {
  for (auto& m : sorted_by_time()) if (m.kind != MarkerKind::None && m.start > pos) return m;
  return std::nullopt;
}
std::optional<MemoryLocation> MemoryLocations::previous_before(Samples pos) const {
  auto v = sorted_by_time();
  for (auto it = v.rbegin(); it != v.rend(); ++it) if (it->kind != MarkerKind::None && it->start < pos) return *it;
  return std::nullopt;
}
}  // namespace fk::markers
