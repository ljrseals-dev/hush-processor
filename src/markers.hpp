#pragma once
// fk::markers: memory locations / markers (translated from SoundCraft crates/model/src/markers.rs, MIT OR Apache-2.0).
// Track groups from that file were NOT ported.
#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
#include "result.hpp"
namespace fk::markers {
using Samples = std::int64_t;
enum class MarkerKind : std::uint8_t { Marker, Selection, None };   // point marker / stored selection range / view-settings only
const char* kind_label(MarkerKind k);
std::optional<MarkerKind> parse_kind(const std::string& s);
struct MemoryLocation {
  std::uint64_t id = 0;
  std::uint32_t number = 0;           // user-visible, 1-based, unique
  std::string name, comments;
  MarkerKind kind = MarkerKind::Marker;
  Samples start = 0, end = 0;         // Marker: end == start
  std::optional<std::array<std::uint8_t, 3>> color;
  std::uint8_t ruler = 1;             // marker ruler lane 1..5
  std::optional<std::uint32_t> track; // track-lane marker on this track index
};
class MemoryLocations {
 public:
  static constexpr std::size_t kMax = 1000, kMaxName = 256, kMaxComment = 1024;
  Result<std::uint32_t> add(MarkerKind kind, std::string name, Samples start, Samples end, std::uint8_t ruler = 1,
                            std::uint32_t number = 0 /*0 = lowest free number*/);
  Status remove(std::uint32_t number);
  Status rename(std::uint32_t number, std::string name);
  Status set_comments(std::uint32_t number, std::string comments);
  Status set_color(std::uint32_t number, std::optional<std::array<std::uint8_t, 3>> color);
  Status set_track(std::uint32_t number, std::optional<std::uint32_t> track);
  Status relocate(std::uint32_t number, Samples start, Samples end);
  std::optional<MemoryLocation> find(std::uint32_t number) const;
  Result<MemoryLocation> recall(std::uint32_t number) const;                 // same as find, with an error message
  std::optional<MemoryLocation> next_after(Samples pos) const;               // next Marker/Selection start strictly after pos
  std::optional<MemoryLocation> previous_before(Samples pos) const;
  std::vector<MemoryLocation> sorted_by_time() const;
  const std::vector<MemoryLocation>& all() const { return items_; }          // ordered by number
  std::size_t size() const { return items_.size(); }
  Status insert_raw(MemoryLocation m);                                       // used by the session parser; validates
 private:
  static Status validate(const MemoryLocation& m);
  std::vector<MemoryLocation> items_;
  std::uint64_t next_id_ = 1;
};
}  // namespace fk::markers
