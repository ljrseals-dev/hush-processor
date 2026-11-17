#include "session.hpp"
#include "core/SecurityHardening.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <limits>
#include <atomic>
#include <cerrno>
#include <cstring>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <sstream>
namespace fk::engine {
namespace {
constexpr std::size_t kMaxTracks = 256, kMaxClips = 10000, kMaxFile = 4u << 20, kMaxName = 512, kMaxLanes = 512, kMaxPoints = 200000;
bool gain_ok(double g) { return std::isfinite(g) && g >= -120 && g <= 36; }
std::string esc(const std::string& s) {
  std::string o;
  for (unsigned char c : s) {
    if (c <= 32 || c == '%' || c >= 127) { char b[4]; std::snprintf(b, sizeof b, "%%%02X", c); o += b; }
    else o.push_back(static_cast<char>(c));
  }
  return o.empty() ? "%00" : o;  // "%00" encodes the empty string
}
int hex_nibble(unsigned char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}
bool unesc(const std::string& s, std::string& out) {
  out.clear();
  if (s == "%00") return true;
  for (std::size_t i = 0; i < s.size(); ++i) {
    if (s[i] != '%') { out.push_back(s[i]); continue; }
    if (i + 2 >= s.size()) return false;
    const int hi = hex_nibble(static_cast<unsigned char>(s[i + 1]));
    const int lo = hex_nibble(static_cast<unsigned char>(s[i + 2]));
    if (hi < 0 || lo < 0) return false;
    const unsigned v = static_cast<unsigned>((hi << 4) | lo);
    if (v == 0) return false;
    out.push_back(static_cast<char>(v));
    i += 2;
  }
  return true;
}
bool name_ok(const std::string& n) { return !n.empty() && n.size() <= kMaxName; }
}  // namespace
Status add_track(Session& s, const std::string& name) {
  if (!name_ok(name)) return Status::fail("track name must be 1..512 bytes");
  if (s.tracks.size() >= kMaxTracks) return Status::fail("track limit (256) reached");
  Track t; t.name = name;
  s.tracks.push_back(std::move(t));
  return Status::success();
}
Status set_gain(Session& s, std::size_t t, double db) {
  if (t >= s.tracks.size()) return Status::fail("no such track");
  if (!gain_ok(db)) return Status::fail("gain must be between -120 and +36 dB");
  s.tracks[t].gain_db = db;
  return Status::success();
}
Status set_mute(Session& s, std::size_t t, bool m) {
  if (t >= s.tracks.size()) return Status::fail("no such track");
  s.tracks[t].mute = m;
  return Status::success();
}
Status add_clip(Session& s, std::size_t t, const std::string& path, std::uint64_t start) {
  if (t >= s.tracks.size()) return Status::fail("no such track");
  if (!name_ok(path) || path.find('\0') != std::string::npos) return Status::fail("invalid clip path");
  std::size_t total = 0;
  for (auto& tr : s.tracks) total += tr.clips.size();
  if (total >= kMaxClips) return Status::fail("clip limit (10000) reached");
  s.tracks[t].clips.push_back({path, start, 0, 0, 0, 0});
  return Status::success();
}
Status move_clip(Session& s, std::size_t t, std::size_t ci, std::uint64_t start) {
  if (t >= s.tracks.size() || ci >= s.tracks[t].clips.size()) return Status::fail("no such clip");
  if (start > (1ULL << 48)) return Status::fail("clip start exceeds safe timeline limit");
  s.tracks[t].clips[ci].start_frame = start;
  return Status::success();
}
Status trim_clip(Session& s, std::size_t t, std::size_t ci, std::uint64_t source, std::uint64_t length) {
  if (t >= s.tracks.size() || ci >= s.tracks[t].clips.size()) return Status::fail("no such clip");
  if (source > (1ULL << 48) || length == 0 || length > (1ULL << 48) || source > (1ULL << 48) - length)
    return Status::fail("trim requires a non-zero length and safe source range");
  auto& c = s.tracks[t].clips[ci];
  c.source_offset_frame = source; c.length_frames = length;
  c.fade_in_frames = std::min(c.fade_in_frames, length);
  c.fade_out_frames = std::min(c.fade_out_frames, length);
  return Status::success();
}
Status set_clip_fades(Session& s, std::size_t t, std::size_t ci, std::uint64_t fi, std::uint64_t fo) {
  if (t >= s.tracks.size() || ci >= s.tracks[t].clips.size()) return Status::fail("no such clip");
  auto& c = s.tracks[t].clips[ci];
  if (c.length_frames == 0) return Status::fail("set a finite clip length before setting fades");
  if (fi > c.length_frames || fo > c.length_frames || fi + fo > c.length_frames)
    return Status::fail("fade lengths must fit within the clip without overlapping");
  c.fade_in_frames = fi; c.fade_out_frames = fo;
  return Status::success();
}
Status split_clip(Session& s, std::size_t t, std::size_t ci, std::uint64_t at) {
  if (t >= s.tracks.size() || ci >= s.tracks[t].clips.size()) return Status::fail("no such clip");
  auto& clips = s.tracks[t].clips;
  const Clip original = clips[ci];
  if (original.length_frames == 0) return Status::fail("cannot split an unbounded clip; trim it to a finite length first");
  if (at == 0 || at >= original.length_frames) return Status::fail("split point must be inside the clip");
  Clip left = original, right = original;
  left.length_frames = at;
  left.fade_out_frames = std::min(left.fade_out_frames, at);
  right.start_frame = original.start_frame + at;
  right.source_offset_frame = original.source_offset_frame + at;
  right.length_frames = original.length_frames - at;
  right.fade_in_frames = original.fade_in_frames > at ? original.fade_in_frames - at : 0;
  right.fade_out_frames = std::min(right.fade_out_frames, right.length_frames);
  left.fade_in_frames = std::min(left.fade_in_frames, left.length_frames);
  clips[ci] = std::move(left);
  clips.insert(clips.begin() + static_cast<std::ptrdiff_t>(ci + 1), std::move(right));
  return Status::success();
}
Status set_mode(Session& s, std::size_t t, fk::lanes::AutomationMode m) {
  if (t >= s.tracks.size()) return Status::fail("no such track");
  s.tracks[t].mode = m; return Status::success();
}
const fk::lanes::AutomationLane* find_lane(const Track& t, const fk::lanes::AutoParam& p) {
  for (auto& l : t.lanes) if (l.param == p) return &l;
  return nullptr;
}
static fk::lanes::AutomationLane* lane_for_edit(Session& s, std::size_t t, const fk::lanes::AutoParam& p, std::string& err) {
  if (t >= s.tracks.size()) { err = "no such track"; return nullptr; }
  auto& tr = s.tracks[t];
  for (auto& l : tr.lanes) if (l.param == p) return &l;
  if (tr.lanes.size() >= kMaxLanes) { err = "lane limit (512) reached"; return nullptr; }
  tr.lanes.emplace_back(p);
  return &tr.lanes.back();
}
Status set_point(Session& s, std::size_t t, const fk::lanes::AutoParam& p, fk::lanes::Samples at, double v) {
  std::string err; auto* l = lane_for_edit(s, t, p, err); if (!l) return Status::fail(err);
  if (at < 0) return Status::fail("time must be >= 0");
  if (!std::isfinite(v)) return Status::fail("value must be finite");
  if (l->points.size() >= kMaxPoints) return Status::fail("point limit (200000) reached");
  l->set_point(at, v); return Status::success();
}
Status write_range(Session& s, std::size_t t, const fk::lanes::AutoParam& p, fk::lanes::Samples a, fk::lanes::Samples b, double v) {
  std::string err; auto* l = lane_for_edit(s, t, p, err); if (!l) return Status::fail(err);
  if (a < 0 || b <= a) return Status::fail("range must satisfy 0 <= start < end");
  if (!std::isfinite(v)) return Status::fail("value must be finite");
  l->write_range(a, b, v, p.range().def); return Status::success();
}
Result<std::size_t> thin_lane(Session& s, std::size_t t, const fk::lanes::AutoParam& p, double tol) {
  using R = Result<std::size_t>;
  if (t >= s.tracks.size()) return R::fail("no such track");
  if (!std::isfinite(tol) || tol < 0) return R::fail("tolerance must be >= 0");
  for (auto& l : s.tracks[t].lanes) if (l.param == p) return R::success(l.thin(tol));
  return R::fail("no such lane");
}
Status replace_lane(Session& s, std::size_t t, fk::lanes::AutomationLane lane) {
  std::string err; auto* l = lane_for_edit(s, t, lane.param, err); if (!l) return Status::fail(err);
  if (lane.points.size() > kMaxPoints) return Status::fail("point limit (200000) reached");
  *l = std::move(lane); return Status::success();
}
void UndoStack::checkpoint(const Session& before) {
  undo_.push_back(before);
  if (undo_.size() > max_) undo_.erase(undo_.begin());
  redo_.clear();
}
bool UndoStack::undo(Session& s) {
  if (undo_.empty()) return false;
  redo_.push_back(s); s = std::move(undo_.back()); undo_.pop_back();
  return true;
}
bool UndoStack::redo(Session& s) {
  if (redo_.empty()) return false;
  undo_.push_back(s); s = std::move(redo_.back()); redo_.pop_back();
  return true;
}
std::string serialize(const Session& s) {
  std::ostringstream o;
  o.precision(17);
  o << "FKSESSION 2\nsample_rate " << s.sample_rate << "\nbpm " << s.bpm << "\n";
  for (auto& t : s.tracks) {
    o << "track " << esc(t.name) << " " << t.gain_db << " " << (t.mute ? 1 : 0) << "\n";
    for (auto& c : t.clips) o << "clip " << esc(c.path) << " " << c.start_frame << " " << c.source_offset_frame << " " << c.length_frames << " " << c.fade_in_frames << " " << c.fade_out_frames << "\n";
    if (t.mode != fk::lanes::AutomationMode::Read) { std::string m = fk::lanes::mode_label(t.mode); for (auto& ch : m) if (ch == '/') ch = '_'; o << "mode " << m << "\n"; }
    for (auto& l : t.lanes) {
      o << "lane " << esc(l.param.id()) << " " << (l.enabled ? 1 : 0) << "\n";
      for (auto& p : l.points) o << "pt " << p.at << " " << p.value << "\n";
    }
  }
  for (auto& m : s.markers.all()) {
    o << "marker " << m.number << " " << fk::markers::kind_label(m.kind) << " " << m.start << " " << m.end << " " << int(m.ruler) << " "
      << (m.track ? std::to_string(*m.track) : std::string("-1")) << " " << esc(m.name) << " " << esc(m.comments) << " ";
    if (m.color) o << int((*m.color)[0]) << "," << int((*m.color)[1]) << "," << int((*m.color)[2]); else o << "-";
    o << "\n";
  }
  return o.str();
}
Result<Session> parse(const std::string& text) {
  using R = Result<Session>;
  if (text.size() > kMaxFile) return R::fail("session file exceeds 4 MiB limit");
  std::istringstream in(text);
  std::string line;
  int ln = 0;
  auto err = [&](const std::string& m) { return R::fail("line " + std::to_string(ln) + ": " + m); };
  if (!std::getline(in, line)) return R::fail("line 1: missing session header");
  ++ln;
  const int schema = line == "FKSESSION 1" ? 1 : (line == "FKSESSION 2" ? 2 : 0);
  if (!schema) return R::fail("line 1: not a HUSH session (expected 'FKSESSION 1' or 'FKSESSION 2')");
  Session s;
  bool have_sr = false, have_bpm = false;
  std::size_t total_clips = 0;
  while (std::getline(in, line)) {
    ++ln;
    if (line.size() > 4096) return err("line too long");
    if (line.empty()) continue;
    std::istringstream ls(line);
    std::string kw; ls >> kw;
    if (kw == "sample_rate") {
      std::uint32_t v; std::string extra;
      if (!(ls >> v) || (ls >> extra) || v < 8000 || v > 768000) return err("sample_rate must be 8000..768000");
      s.sample_rate = v; have_sr = true;
    } else if (kw == "bpm") {
      double v; std::string extra;
      if (!(ls >> v) || (ls >> extra) || !std::isfinite(v) || v < 10 || v > 960) return err("bpm must be 10..960");
      s.bpm = v; have_bpm = true;
    } else if (kw == "track") {
      std::string en, name, extra; double g; int m;
      if (!(ls >> en >> g >> m) || (ls >> extra) || !unesc(en, name) || !name_ok(name) || !gain_ok(g) || (m != 0 && m != 1))
        return err("malformed track line");
      if (s.tracks.size() >= kMaxTracks) return err("too many tracks");
      Track nt; nt.name = name; nt.gain_db = g; nt.mute = (m == 1);
      s.tracks.push_back(std::move(nt));
    } else if (kw == "clip") {
      std::string ep, path, extra;
      unsigned long long st = 0, source = 0, length = 0, fi = 0, fo = 0;
      if (s.tracks.empty()) return err("clip before any track");
      if (total_clips >= kMaxClips) return err("too many clips (limit 10000)");
      if (!(ls >> ep >> st) || !unesc(ep, path) || !name_ok(path)) return err("malformed clip line");
      if (schema == 1) {
        if (ls >> extra) return err("malformed v1 clip line");
      } else {
        if (!(ls >> source >> length >> fi >> fo) || (ls >> extra)) return err("malformed v2 clip line");
        if (source > (1ULL << 48) || length > (1ULL << 48) || fi > length || fo > length || fi + fo > length)
          return err("invalid clip trim/fade range");
      }
      s.tracks.back().clips.push_back({path, st, source, length, fi, fo});
      ++total_clips;
    } else if (kw == "mode") {
      std::string m, extra;
      if (s.tracks.empty()) return err("mode before any track");
      auto pm = (ls >> m && !(ls >> extra)) ? fk::lanes::parse_mode(m) : std::nullopt;
      if (!pm) return err("unknown automation mode");
      s.tracks.back().mode = *pm;
    } else if (kw == "lane") {
      std::string eid, id, extra; int en;
      if (s.tracks.empty()) return err("lane before any track");
      if (!(ls >> eid >> en) || (ls >> extra) || !unesc(eid, id) || (en != 0 && en != 1)) return err("malformed lane line");
      auto ap = fk::lanes::AutoParam::parse(id);
      if (!ap) return err("unknown automation parameter id");
      auto& tr = s.tracks.back();
      if (tr.lanes.size() >= kMaxLanes) return err("too many lanes");
      for (auto& l : tr.lanes) if (l.param == *ap) return err("duplicate lane for this parameter");
      tr.lanes.emplace_back(*ap); tr.lanes.back().enabled = (en == 1);
    } else if (kw == "pt") {
      long long at; double v; std::string extra;
      if (s.tracks.empty() || s.tracks.back().lanes.empty()) return err("pt before any lane");
      if (!(ls >> at >> v) || (ls >> extra) || at < 0 || !std::isfinite(v)) return err("malformed pt line");
      auto& l = s.tracks.back().lanes.back();
      if (l.points.size() >= kMaxPoints) return err("too many points in lane");
      l.set_point(at, v);
    } else if (kw == "marker") {
      unsigned long long number; std::string kind, ename, ecom, color, extra; long long st, en, tr; int ruler;
      std::string comments, name;
      if (!(ls >> number >> kind >> st >> en >> ruler >> tr >> ename >> ecom >> color) || (ls >> extra) || number == 0 || number > 4000000000ULL)
        return err("malformed marker line");
      auto k = fk::markers::parse_kind(kind);
      if (!k || !unesc(ename, name) || !unesc(ecom, comments) || ruler < 1 || ruler > 5 || tr < -1) return err("malformed marker line");
      fk::markers::MemoryLocation m;
      m.number = static_cast<std::uint32_t>(number); m.kind = *k; m.start = st; m.end = en; m.ruler = static_cast<std::uint8_t>(ruler);
      m.name = name; m.comments = comments;
      if (tr >= 0) m.track = static_cast<std::uint32_t>(tr);
      if (color != "-") {
        int r, g, b; char c1, c2;
        std::istringstream cs(color);
        if (!(cs >> r >> c1 >> g >> c2 >> b) || c1 != ',' || c2 != ',' || r < 0 || r > 255 || g < 0 || g > 255 || b < 0 || b > 255) return err("bad marker color");
        m.color = std::array<std::uint8_t, 3>{static_cast<std::uint8_t>(r), static_cast<std::uint8_t>(g), static_cast<std::uint8_t>(b)};
      }
      if (auto st2 = s.markers.insert_raw(m); !st2) return err(st2.error);
    } else return err("unknown keyword '" + kw.substr(0, 32) + "'");
  }
  if (!have_sr || !have_bpm) return R::fail("missing sample_rate or bpm");
  return R::success(std::move(s));
}
Result<Session> load(const std::filesystem::path& p) {
  using R = Result<Session>;
  namespace fs = std::filesystem;
  std::error_code ec;
  const auto link = fs::symlink_status(p, ec);
  if (ec) return R::fail("cannot inspect session path: " + p.string());
  if (fs::is_symlink(link)) return R::fail("refusing to load a session through a symbolic link: " + p.string());
  if (!fs::is_regular_file(link)) return R::fail("session input must be a regular file: " + p.string());
  const auto size = fs::file_size(p, ec);
  if (ec) return R::fail("cannot inspect session size: " + p.string());
  if (size > kMaxFile) return R::fail("session file exceeds 4 MiB limit");
#if !defined(_WIN32)
  int flags = O_RDONLY;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  const int fd = ::open(p.c_str(), flags);
  if (fd < 0) return R::fail("cannot safely open session: " + p.string());
  struct stat st{};
  if (::fstat(fd, &st) != 0 || !S_ISREG(st.st_mode) || st.st_size < 0 || static_cast<std::uint64_t>(st.st_size) > kMaxFile) {
    ::close(fd); return R::fail("session input is not a bounded regular file");
  }
  std::string data(static_cast<std::size_t>(st.st_size), '\0');
  std::size_t read_count = 0;
  while (read_count < data.size()) {
    const auto n = ::read(fd, data.data() + read_count, data.size() - read_count);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { ::close(fd); return R::fail("short read while loading session"); }
    read_count += static_cast<std::size_t>(n);
  }
  ::close(fd);
  return parse(data);
#else
  std::ifstream f(p, std::ios::binary);
  if (!f) return R::fail("cannot open session: " + p.string());
  std::string data(static_cast<std::size_t>(size), '\0');
  f.read(data.data(), static_cast<std::streamsize>(data.size()));
  if (!f || f.peek() != std::char_traits<char>::eof()) return R::fail("session changed while loading");
  return parse(data);
#endif
}
Status save(const std::filesystem::path& p, const Session& s, bool overwrite) {
  namespace fs = std::filesystem;
  if (!dawchecker::security::isSafeExportPath(p.string()))
    return Status::fail("unsafe session output path: " + p.string());
  std::error_code ec;
  const auto link_status = fs::symlink_status(p, ec);
  if (ec && ec != std::errc::no_such_file_or_directory)
    return Status::fail("cannot inspect session output path: " + p.string());
  if (!ec && fs::is_symlink(link_status))
    return Status::fail("refusing to replace a symbolic-link output: " + p.string());
  ec.clear();
  const bool exists = fs::exists(p, ec);
  if (ec) return Status::fail("cannot inspect session output path: " + p.string());
  if (exists) {
    const auto st = fs::status(p, ec);
    if (ec || !fs::is_regular_file(st)) return Status::fail("session output must be a regular file: " + p.string());
    if (!overwrite) return Status::fail("refusing to overwrite existing file: " + p.string());
  }
  const std::string contents = serialize(s);
#if !defined(_WIN32)
  static std::atomic<std::uint64_t> nonce{0};
  const fs::path parent = p.parent_path().empty() ? fs::path(".") : p.parent_path();
  const fs::path temp = parent / ("." + p.filename().string() + ".hush-tmp-" + std::to_string(static_cast<unsigned long long>(::getpid())) + "-" + std::to_string(nonce.fetch_add(1)));
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  const int fd = ::open(temp.c_str(), flags, S_IRUSR | S_IWUSR);
  if (fd < 0) return Status::fail("cannot create safe temporary session file: " + std::string(std::strerror(errno)));
  bool okay = true; std::size_t written = 0;
  while (written < contents.size()) {
    const auto n = ::write(fd, contents.data() + written, contents.size() - written);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { okay = false; break; }
    written += static_cast<std::size_t>(n);
  }
  if (okay && ::fsync(fd) != 0) okay = false;
  const int close_result = ::close(fd);
  if (close_result != 0) okay = false;
  if (!okay) { const int saved_errno = errno; ::unlink(temp.c_str()); return Status::fail("failed writing temporary session file: " + std::string(std::strerror(saved_errno))); }
  if (overwrite) {
    if (::rename(temp.c_str(), p.c_str()) != 0) { const int saved_errno = errno; ::unlink(temp.c_str()); return Status::fail("atomic session replace failed: " + std::string(std::strerror(saved_errno))); }
  } else {
    if (::link(temp.c_str(), p.c_str()) != 0) { const int saved_errno = errno; ::unlink(temp.c_str()); return Status::fail("atomic no-overwrite session create failed: " + std::string(std::strerror(saved_errno))); }
    if (::unlink(temp.c_str()) != 0) return Status::fail("session was created but temporary link cleanup failed");
  }
  const int dirfd = ::open(parent.c_str(), O_RDONLY
#ifdef O_DIRECTORY
    | O_DIRECTORY
#endif
  );
  if (dirfd >= 0) { (void)::fsync(dirfd); (void)::close(dirfd); }
  return Status::success();
#else
  // Windows fallback keeps the temporary file beside the destination so rename stays on-volume.
  const fs::path temp = p.parent_path() / (p.filename().string() + ".hush-tmp");
  if (fs::exists(temp, ec)) return Status::fail("temporary session path already exists");
  { std::ofstream f(temp, std::ios::binary | std::ios::out); if (!f) return Status::fail("cannot create temporary session file"); f.write(contents.data(), static_cast<std::streamsize>(contents.size())); if (!f) { fs::remove(temp, ec); return Status::fail("temporary session write failed"); } }
  if (!overwrite && fs::exists(p, ec)) { fs::remove(temp, ec); return Status::fail("refusing to overwrite existing file: " + p.string()); }
  if (overwrite) fs::remove(p, ec);
  fs::rename(temp, p, ec); if (ec) { fs::remove(temp); return Status::fail("session replace failed: " + ec.message()); }
  return Status::success();
#endif
}
Result<fk::audio::AudioBuffer> render(const Session& s, const std::filesystem::path& base) {
  using R = Result<fk::audio::AudioBuffer>;
  std::vector<float> acc;
  std::uint16_t channels = 0;
  try {
    for (auto& t : s.tracks) {
      if (t.mute) continue;
      float g = static_cast<float>(std::pow(10.0, t.gain_db / 20.0));
      for (auto& c : t.clips) {
        std::filesystem::path p = c.path;
        if (p.is_relative()) p = base / p;
        auto a = fk::audio::read_wav(p);
        if (a.sample_rate != s.sample_rate) return R::fail("clip sample rate differs from session: " + c.path);
        if (!channels) channels = a.channels;
        if (a.channels != channels) return R::fail("clip channel count differs from other clips: " + c.path);
        if (c.start_frame > (1ULL << 32) || c.source_offset_frame > a.frames()) return R::fail("clip position exceeds source/timeline bounds");
        const std::size_t source_start = static_cast<std::size_t>(c.source_offset_frame);
        const std::size_t available = a.frames() - source_start;
        if (c.length_frames > available) return R::fail("clip length exceeds source range: " + c.path);
        const std::size_t clip_frames = c.length_frames == 0 ? available : static_cast<std::size_t>(c.length_frames);
        if (clip_frames == 0) return R::fail("clip has no source frames: " + c.path);
        if (c.fade_in_frames > clip_frames || c.fade_out_frames > clip_frames || c.fade_in_frames + c.fade_out_frames > clip_frames)
          return R::fail("clip fade lengths exceed source range: " + c.path);
        if (c.start_frame > (std::numeric_limits<std::size_t>::max() / channels) - clip_frames)
          return R::fail("clip timeline length overflows address space");
        const std::size_t offset = static_cast<std::size_t>(c.start_frame) * channels;
        const std::size_t sample_count = clip_frames * channels;
        if (offset > std::numeric_limits<std::size_t>::max() - sample_count) return R::fail("clip end overflows address space");
        if (acc.size() < offset + sample_count) acc.resize(offset + sample_count, 0.0f);
        const fk::lanes::AutomationLane* vol = fk::lanes::reads(t.mode) ? find_lane(t, fk::lanes::AutoParam::volume()) : nullptr;
        const fk::lanes::AutomationLane* mut = fk::lanes::reads(t.mode) ? find_lane(t, fk::lanes::AutoParam::mute()) : nullptr;
        if (vol && (vol->points.empty() || !vol->enabled)) vol = nullptr;
        if (mut && (mut->points.empty() || !mut->enabled)) mut = nullptr;
        // Volume and mute lanes are applied per frame; pan, send and plugin-parameter lanes are stored but NOT rendered.
        for (std::size_t f = 0; f < clip_frames; ++f) {
          float gg = g;
          if (c.fade_in_frames > 0 && f < c.fade_in_frames)
            gg *= static_cast<float>(f) / static_cast<float>(c.fade_in_frames);
          if (c.fade_out_frames > 0 && f >= clip_frames - c.fade_out_frames)
            gg *= static_cast<float>(clip_frames - 1 - f) / static_cast<float>(c.fade_out_frames);
          const fk::lanes::Samples pos = static_cast<fk::lanes::Samples>(c.start_frame + f);
          if (vol) gg *= static_cast<float>(std::pow(10.0, vol->value_at(pos, 0.0) / 20.0));
          if (mut && mut->value_at(pos, 0.0) >= 0.5) gg = 0.0f;
          for (std::uint16_t ch = 0; ch < channels; ++ch) acc[offset + f * channels + ch] += a.samples[(source_start + f) * channels + ch] * gg;
        }
      }
    }
  } catch (const std::exception& e) { return R::fail(e.what()); }
  if (acc.empty()) return R::fail("nothing to render (no unmuted clips)");
  return R::success(fk::audio::AudioBuffer{s.sample_rate, channels, std::move(acc)});
}
}  // namespace fk::engine
