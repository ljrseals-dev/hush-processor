#pragma once
// fk::engine (partial): offline session model, undo/redo, strict text serialisation, offline render.
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>
#include "audio.hpp"
#include "automation.hpp"
#include "markers.hpp"
#include "result.hpp"
namespace fk::engine {
struct Clip {
  std::string path;
  std::uint64_t start_frame = 0;       // timeline position
  std::uint64_t source_offset_frame = 0; // source trim-in
  std::uint64_t length_frames = 0;     // 0 = from source offset to EOF
  std::uint64_t fade_in_frames = 0;
  std::uint64_t fade_out_frames = 0;
};
struct Track {
  std::string name; double gain_db = 0; bool mute = false; std::vector<Clip> clips;
  fk::lanes::AutomationMode mode = fk::lanes::AutomationMode::Read;   // lanes are read at render time unless Off
  std::vector<fk::lanes::AutomationLane> lanes;
};
struct Session {
  std::uint32_t sample_rate = 48000;
  double bpm = 120;
  std::vector<Track> tracks;
  fk::markers::MemoryLocations markers;
};
Status add_track(Session& s, const std::string& name);
Status set_gain(Session& s, std::size_t track, double db);
Status set_mute(Session& s, std::size_t track, bool mute);
Status add_clip(Session& s, std::size_t track, const std::string& path, std::uint64_t start_frame);
Status move_clip(Session& s, std::size_t track, std::size_t clip, std::uint64_t new_start_frame);
Status trim_clip(Session& s, std::size_t track, std::size_t clip, std::uint64_t source_offset_frame, std::uint64_t length_frames);
Status set_clip_fades(Session& s, std::size_t track, std::size_t clip, std::uint64_t fade_in_frames, std::uint64_t fade_out_frames);
Status split_clip(Session& s, std::size_t track, std::size_t clip, std::uint64_t split_offset_frames);
Status set_mode(Session& s, std::size_t track, fk::lanes::AutomationMode m);
Status set_point(Session& s, std::size_t track, const fk::lanes::AutoParam& p, fk::lanes::Samples at, double value);
Status write_range(Session& s, std::size_t track, const fk::lanes::AutoParam& p, fk::lanes::Samples start, fk::lanes::Samples end, double value);
Result<std::size_t> thin_lane(Session& s, std::size_t track, const fk::lanes::AutoParam& p, double tolerance);
Status replace_lane(Session& s, std::size_t track, fk::lanes::AutomationLane lane);   // e.g. the result of a live-record pass
const fk::lanes::AutomationLane* find_lane(const Track& t, const fk::lanes::AutoParam& p);
// Snapshot-based undo/redo (bounded depth).
class UndoStack {
 public:
  explicit UndoStack(std::size_t max_depth = 256) : max_(max_depth) {}
  void checkpoint(const Session& before);       // call before each edit
  bool can_undo() const { return !undo_.empty(); }
  bool can_redo() const { return !redo_.empty(); }
  bool undo(Session& s);
  bool redo(Session& s);
 private:
  std::size_t max_;
  std::vector<Session> undo_, redo_;
};
std::string serialize(const Session& s);
Result<Session> parse(const std::string& text);   // strict; reports line numbers
Result<Session> load(const std::filesystem::path& p);
Status save(const std::filesystem::path& p, const Session& s, bool overwrite);
// Reads the referenced WAV files (relative paths resolve against base_dir) only when called.
Result<fk::audio::AudioBuffer> render(const Session& s, const std::filesystem::path& base_dir);
}  // namespace fk::engine
