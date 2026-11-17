#include <cmath>
#include <atomic>
#include <cerrno>
#include <cstring>
#if !defined(_WIN32)
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#endif
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>
#include "audio.hpp"
#include "automation.hpp"
#include "counter.hpp"
#include "markers.hpp"
#include "recorder.hpp"
#include "clipboard.hpp"
#include "core/SecurityHardening.h"
#include "forensics.hpp"
#include "hash.hpp"
#include "session.hpp"
#include "time.hpp"
#include "watermark.hpp"
namespace {
using Args = std::vector<std::string>;
namespace fs = std::filesystem;
void usage() {
  std::cout <<
R"(HUSH offline DAW 2.2.0  (no network, no telemetry; every write is an explicit command; existing files are never overwritten without --overwrite)
Audio:
  hush tone OUT.wav SECONDS HZ [AMPLITUDE] [SAMPLE_RATE] [CHANNELS] [--overwrite]
  hush info IN.wav | scan IN.wav          (basic statistics)
  hush gain IN.wav OUT.wav DB [--overwrite]
  hush mix OUT.wav IN.wav DB [IN.wav DB ...] [--overwrite]
Forensics (local, read-only; indicators not proof):
  hush forensics IN.wav [--ref REF.wav] [--report OUT.txt [--overwrite]]
  hush codec-scan                          (explicit device/codec-name scan)
  hush selfcheck                           (read-only OS security posture report)
Watermark (explicit, offline, writes a NEW file):
  hush wm-detect IN.wav --key N --bits N
  hush wm-embed IN.wav OUT.wav --key N --payload 10110010 [--strength-db -52] --confirm [--overwrite]
Session (offline, partial engine):
  hush session-new S.fks [SAMPLE_RATE] [BPM]
  hush session-info S.fks
  hush session-add-track S.fks NAME
  hush session-add-clip S.fks TRACK_INDEX CLIP.wav START_FRAME
  hush session-gain S.fks TRACK_INDEX DB
  hush session-move-clip S.fks TRACK_INDEX CLIP_INDEX START_FRAME
  hush session-trim-clip S.fks TRACK_INDEX CLIP_INDEX SOURCE_OFFSET LENGTH_FRAMES
  hush session-split-clip S.fks TRACK_INDEX CLIP_INDEX SPLIT_OFFSET_FRAMES
  hush session-fades S.fks TRACK_INDEX CLIP_INDEX FADE_IN_FRAMES FADE_OUT_FRAMES
  hush session-render S.fks OUT.wav [--overwrite]
Automation lanes and markers (offline):
  hush lane-mode S.fks TRACK off|read|touch|latch|touch/latch|write|trim
  hush lane-point S.fks TRACK PARAM SAMPLE VALUE   (PARAM: volume, pan, pan2, mute, send_a_level, send_a_pan, send_a_mute, plugin:a:threshold)
  hush lane-range S.fks TRACK PARAM START END VALUE | lane-thin S.fks TRACK PARAM TOL | lane-show S.fks TRACK [--param ID]
  hush lane-record S.fks TRACK PARAM EVENTS.txt [--return SAMPLES]   (replay a live Write/Touch/Latch pass)
  hush marker-add S.fks NAME SAMPLE [--kind marker|selection|none] [--end N] [--ruler 1-5] [--number N]
  hush marker-list S.fks | marker-remove S.fks N | marker-recall S.fks N | marker-next S.fks SAMPLE | marker-prev S.fks SAMPLE
  hush counter SAMPLE [--sr N] [--bpm B] [--fps 24|25|30|29.97df]   (big counter read-out)
Time:
  hush bbt BPM NUM DEN QUARTER_BEATS | timecode FRAME 24|25|30|29.97df
Text safety:
  hush sensitive-check FILE                (kinds only; never prints the matches)
)";
}
double num(const std::string& s, const char* what) {
  std::size_t n = 0; double x;
  try { x = std::stod(s, &n); } catch (...) { throw std::runtime_error(std::string("invalid ") + what); }
  if (n != s.size() || !std::isfinite(x)) throw std::runtime_error(std::string("invalid ") + what);
  return x;
}
std::uint64_t unum(const std::string& s, const char* what) {
  std::size_t n = 0; unsigned long long x;
  try { x = std::stoull(s, &n); } catch (...) { throw std::runtime_error(std::string("invalid ") + what); }
  if (n != s.size() || s[0] == '-') throw std::runtime_error(std::string("invalid ") + what);
  return x;
}
bool has_flag(Args& a, const std::string& f) {
  for (std::size_t i = 0; i < a.size(); ++i) if (a[i] == f) { a.erase(a.begin() + static_cast<long>(i)); return true; }
  return false;
}
bool take_opt(Args& a, const std::string& f, std::string& v) {
  for (std::size_t i = 0; i + 1 < a.size(); ++i)
    if (a[i] == f) { v = a[i + 1]; a.erase(a.begin() + static_cast<long>(i), a.begin() + static_cast<long>(i) + 2); return true; }
  return false;
}
void require_new_output(const fs::path& in, const fs::path& out, bool overwrite) {
  std::error_code ec;
  if (!dawchecker::security::isSafeExportPath(out.string()))
    throw std::runtime_error("unsafe output path (empty, contains '..', or embedded NUL): " + out.string());
  if (fs::is_symlink(fs::symlink_status(out, ec)))
    throw std::runtime_error("refusing to write through a symbolic link: " + out.string());
  if (fs::is_directory(out, ec)) throw std::runtime_error("output is a directory: " + out.string());
  if (!in.empty() && fs::exists(in, ec) && fs::exists(out, ec) && fs::equivalent(in, out, ec))
    throw std::runtime_error("output must differ from the input; originals are never overwritten");
  if (fs::exists(out, ec) && !overwrite) throw std::runtime_error("output exists; pass --overwrite to replace it: " + out.string());
}
void write_text_output(const fs::path& path, const std::string& contents, bool overwrite) {
  std::error_code ec;
  const auto link = fs::symlink_status(path, ec);
  if (ec && ec != std::errc::no_such_file_or_directory) throw std::runtime_error("cannot inspect text output path: " + path.string());
  if (!ec && fs::is_symlink(link)) throw std::runtime_error("refusing to write through a symbolic link: " + path.string());
  ec.clear(); const bool exists = fs::exists(path, ec);
  if (ec) throw std::runtime_error("cannot inspect text output path: " + path.string());
  if (exists) {
    const auto st = fs::status(path, ec);
    if (ec || !fs::is_regular_file(st)) throw std::runtime_error("text output must be a regular file: " + path.string());
    if (!overwrite) throw std::runtime_error("output exists; explicit overwrite permission required: " + path.string());
  }
  const fs::path parent = path.parent_path().empty() ? fs::path(".") : path.parent_path();
#if !defined(_WIN32)
  static std::atomic<std::uint64_t> nonce{0};
  const fs::path temp = parent / ("." + path.filename().string() + ".hush-text-tmp-" + std::to_string(static_cast<unsigned long long>(::getpid())) + "-" + std::to_string(nonce.fetch_add(1)));
  int flags = O_WRONLY | O_CREAT | O_EXCL;
#ifdef O_NOFOLLOW
  flags |= O_NOFOLLOW;
#endif
#ifdef O_CLOEXEC
  flags |= O_CLOEXEC;
#endif
  const int fd = ::open(temp.c_str(), flags, S_IRUSR | S_IWUSR);
  if (fd < 0) throw std::runtime_error("cannot create safe text output: " + std::string(std::strerror(errno)));
  std::size_t at = 0; bool okay = true;
  while (at < contents.size()) {
    const auto n = ::write(fd, contents.data() + at, contents.size() - at);
    if (n < 0 && errno == EINTR) continue;
    if (n <= 0) { okay = false; break; }
    at += static_cast<std::size_t>(n);
  }
  if (okay && ::fsync(fd) != 0) okay = false;
  if (::close(fd) != 0) okay = false;
  if (!okay) { const int e = errno; ::unlink(temp.c_str()); throw std::runtime_error("failed writing text output: " + std::string(std::strerror(e))); }
  if (overwrite) {
    if (::rename(temp.c_str(), path.c_str()) != 0) { const int e = errno; ::unlink(temp.c_str()); throw std::runtime_error("atomic text replace failed: " + std::string(std::strerror(e))); }
  } else {
    if (::link(temp.c_str(), path.c_str()) != 0) { const int e = errno; ::unlink(temp.c_str()); throw std::runtime_error("atomic no-overwrite text create failed: " + std::string(std::strerror(e))); }
    if (::unlink(temp.c_str()) != 0) throw std::runtime_error("text output created but temporary link cleanup failed");
  }
#else
  const fs::path temp = parent / (path.filename().string() + ".hush-text-tmp");
  if (fs::exists(temp, ec)) throw std::runtime_error("temporary text output path already exists");
  { std::ofstream f(temp, std::ios::binary | std::ios::out); if (!f) throw std::runtime_error("cannot create temporary text output"); f.write(contents.data(), static_cast<std::streamsize>(contents.size())); if (!f) { fs::remove(temp, ec); throw std::runtime_error("text output write failed"); } }
  if (overwrite) fs::remove(path, ec);
  fs::rename(temp, path, ec); if (ec) { fs::remove(temp); throw std::runtime_error("text output replace failed: " + ec.message()); }
#endif
}
template <class T> T& need(fk::Result<T>& r) { if (!r) throw std::runtime_error(r.error); return *r; }
void need(const fk::Status& s) { if (!s) throw std::runtime_error(s.error); }
fk::engine::Session load_session(const std::string& p) { auto r = fk::engine::load(p); return std::move(need(r)); }

int run(Args a) {
  if (a.empty() || a[0] == "help" || a[0] == "--help") { usage(); return a.empty() ? 2 : 0; }
  const std::string cmd = a[0];
  a.erase(a.begin());
  if (cmd == "info" || cmd == "scan") {
    if (a.size() != 1) throw std::runtime_error(cmd + " requires IN.wav");
    std::cout << fk::audio::describe(fk::audio::read_wav(a[0])) << "\n";
    if (cmd == "scan") std::cout << "scope=basic signal statistics only; no authenticity or malware verdict\n";
  } else if (cmd == "gain") {
    bool ow = has_flag(a, "--overwrite");
    if (a.size() != 3) throw std::runtime_error("gain requires IN.wav OUT.wav DB");
    require_new_output(a[0], a[1], ow);
    fk::audio::write_wav_pcm16(a[1], fk::audio::apply_gain(fk::audio::read_wav(a[0]), num(a[2], "gain dB")), ow);
    std::cout << "wrote " << a[1] << "\n";
  } else if (cmd == "mix") {
    bool ow = has_flag(a, "--overwrite");
    if (a.size() < 3 || (a.size() - 1) % 2 != 0) throw std::runtime_error("mix requires OUT.wav followed by IN.wav DB pairs");
    for (std::size_t i = 1; i < a.size(); i += 2) require_new_output(a[i], a[0], ow);
    std::vector<fk::audio::AudioBuffer> in; std::vector<double> g;
    for (std::size_t i = 1; i < a.size(); i += 2) { in.push_back(fk::audio::read_wav(a[i])); g.push_back(num(a[i + 1], "gain dB")); }
    auto out = fk::audio::mix(in, g);
    fk::audio::write_wav_pcm16(a[0], out, ow);
    std::cout << "wrote " << a[0] << "\n" << fk::audio::describe(out) << "\n";
  } else if (cmd == "tone") {
    bool ow = has_flag(a, "--overwrite");
    if (a.size() < 3 || a.size() > 6) throw std::runtime_error("tone requires OUT.wav SECONDS HZ [AMPLITUDE] [SAMPLE_RATE] [CHANNELS]");
    require_new_output({}, a[0], ow);
    double amp = a.size() >= 4 ? num(a[3], "amplitude") : 0.5;
    auto sr = a.size() >= 5 ? static_cast<std::uint32_t>(num(a[4], "sample rate")) : 48000U;
    auto ch = a.size() >= 6 ? static_cast<std::uint16_t>(num(a[5], "channels")) : std::uint16_t{2};
    fk::audio::write_wav_pcm16(a[0], fk::audio::make_tone(num(a[1], "duration"), num(a[2], "frequency"), amp, sr, ch), ow);
    std::cout << "wrote " << a[0] << "\n";
  } else if (cmd == "forensics") {
    std::string ref, report;
    bool have_ref = take_opt(a, "--ref", ref), have_rep = take_opt(a, "--report", report);
    bool overwrite = has_flag(a, "--overwrite");
    if (a.size() != 1) throw std::runtime_error("forensics requires IN.wav");
    if (have_rep) require_new_output(a[0], report, overwrite);
    auto live = fk::audio::read_wav(a[0]);
    fk::audio::AudioBuffer refbuf;
    if (have_ref) refbuf = fk::audio::read_wav(ref);
    auto sha = fk::hash::sha256_file(a[0]);
    auto rep = fk::forensics::analyze(live, have_ref ? &refbuf : nullptr, sha ? *sha : "");
    std::string text = need(rep).to_text();
    std::cout << text;
    if (have_rep) {
      require_new_output(a[0], report, overwrite);
      write_text_output(report, text, overwrite);
      std::cout << "report written to " << report << "\n";
    }
  } else if (cmd == "selfcheck") {
    // Explicit, read-only report of this process's OS security posture (never runs at startup, never changes anything).
    auto r = dawchecker::security::runStartupSelfCheck();
    auto yn = [](bool b) { return b ? "yes" : "no"; };
    std::cout << "ASLR active:            " << yn(r.aslrActive) << "\ndebugger attached:      " << yn(r.debuggerDetected)
              << "\nrunning elevated (root): " << yn(r.runningElevated) << "\n";
    for (auto& n : r.notes) std::cout << "note: " << n << "\n";
    std::cout << "Informational only: HUSH does not alter its behaviour based on these values and sends nothing anywhere.\n";
  } else if (cmd == "codec-scan") {
    std::cout << fk::forensics::scan_devices_text();
  } else if (cmd == "wm-detect") {
    std::string key, bits;
    if (!take_opt(a, "--key", key) || !take_opt(a, "--bits", bits) || a.size() != 1)
      throw std::runtime_error("wm-detect requires IN.wav --key N --bits N");
    auto d = fk::watermark::detect(fk::audio::read_wav(a[0]), unum(key, "key"), unum(bits, "bits"));
    auto& det = need(d);
    std::cout << det.text << "\ndecoded_bits=";
    for (bool b : det.decoded_bits) std::cout << (b ? '1' : '0');
    std::cout << "\n(decoded bits are meaningful only if the verdict is not 'no signature detected')\n";
  } else if (cmd == "wm-embed") {
    std::string key, payload, strength;
    bool confirm = has_flag(a, "--confirm"), overwrite = has_flag(a, "--overwrite");
    bool ok = take_opt(a, "--key", key) && take_opt(a, "--payload", payload);
    double db = take_opt(a, "--strength-db", strength) ? num(strength, "strength") : -52.0;
    if (!ok || a.size() != 2) throw std::runtime_error("wm-embed requires IN.wav OUT.wav --key N --payload BITS --confirm");
    if (!confirm) throw std::runtime_error("watermark embedding writes a new file and needs --confirm (no silent watermarking)");
    fk::watermark::EmbedParams p;
    p.key = unum(key, "key"); p.strength_db = db;
    for (char c : payload) { if (c != '0' && c != '1') throw std::runtime_error("payload must be 0/1 bits"); p.payload.push_back(c == '1'); }
    require_new_output(a[0], a[1], overwrite);
    const fs::path label_path = a[1] + ".fkwm.txt";
    require_new_output({}, label_path, overwrite);
    auto r = fk::watermark::embed(fk::audio::read_wav(a[0]), p);
    auto& rep = need(r);
    fk::audio::write_wav_pcm16(a[1], rep.audio, overwrite);
    std::ostringstream label;
    label << "WATERMARKED by HUSH (experimental spread-spectrum)\nsource=" << fs::path(a[0]).filename().string()
          << "\npayload_bits=" << payload << "\nstrength_dbfs=" << db
          << "\nwatermark_to_signal_db=" << rep.watermark_to_signal_db
          << "\nkey=(not stored here; keep it yourself)\n";
    write_text_output(label_path, label.str(), overwrite);
    std::cout << "wrote " << a[1] << " (labelled by " << a[1] << ".fkwm.txt)\nwatermark_to_signal_db=" << rep.watermark_to_signal_db
              << "\npeak_before=" << rep.peak_before << " peak_after=" << rep.peak_after
              << "\nOriginal untouched. Compare both files (A/B) before using the output.\n";
  } else if (cmd == "session-new") {
    if (a.size() < 1 || a.size() > 3) throw std::runtime_error("session-new requires S.fks [SAMPLE_RATE] [BPM]");
    fk::engine::Session s;
    if (a.size() >= 2) s.sample_rate = static_cast<std::uint32_t>(num(a[1], "sample rate"));
    if (a.size() >= 3) s.bpm = num(a[2], "bpm");
    need(fk::engine::save(a[0], s, false));
    std::cout << "created " << a[0] << "\n";
  } else if (cmd == "session-info") {
    if (a.size() != 1) throw std::runtime_error("session-info requires S.fks");
    auto s = load_session(a[0]);
    std::cout << "sample_rate=" << s.sample_rate << " bpm=" << s.bpm << " tracks=" << s.tracks.size() << "\n";
    for (std::size_t i = 0; i < s.tracks.size(); ++i)
      std::cout << "  [" << i << "] " << s.tracks[i].name << " gain=" << s.tracks[i].gain_db << " dB clips=" << s.tracks[i].clips.size() << "\n";
  } else if (cmd == "session-add-track") {
    if (a.size() != 2) throw std::runtime_error("session-add-track requires S.fks NAME");
    auto s = load_session(a[0]); need(fk::engine::add_track(s, a[1])); need(fk::engine::save(a[0], s, true));
    std::cout << "track added\n";
  } else if (cmd == "session-add-clip") {
    if (a.size() != 4) throw std::runtime_error("session-add-clip requires S.fks TRACK_INDEX CLIP.wav START_FRAME");
    auto s = load_session(a[0]);
    need(fk::engine::add_clip(s, unum(a[1], "track index"), a[2], unum(a[3], "start frame"))); need(fk::engine::save(a[0], s, true));
    std::cout << "clip added\n";
  } else if (cmd == "session-gain") {
    if (a.size() != 3) throw std::runtime_error("session-gain requires S.fks TRACK_INDEX DB");
    auto s = load_session(a[0]);
    need(fk::engine::set_gain(s, unum(a[1], "track index"), num(a[2], "gain dB"))); need(fk::engine::save(a[0], s, true));
    std::cout << "gain set\n";
  } else if (cmd == "session-move-clip") {
    if (a.size() != 4) throw std::runtime_error("session-move-clip requires S.fks TRACK_INDEX CLIP_INDEX START_FRAME");
    auto s = load_session(a[0]);
    need(fk::engine::move_clip(s, static_cast<std::size_t>(unum(a[1], "track index")), static_cast<std::size_t>(unum(a[2], "clip index")), unum(a[3], "start frame")));
    need(fk::engine::save(a[0], s, true)); std::cout << "clip moved\n";
  } else if (cmd == "session-trim-clip") {
    if (a.size() != 5) throw std::runtime_error("session-trim-clip requires S.fks TRACK_INDEX CLIP_INDEX SOURCE_OFFSET LENGTH_FRAMES");
    auto s = load_session(a[0]);
    need(fk::engine::trim_clip(s, static_cast<std::size_t>(unum(a[1], "track index")), static_cast<std::size_t>(unum(a[2], "clip index")), unum(a[3], "source offset"), unum(a[4], "length")));
    need(fk::engine::save(a[0], s, true)); std::cout << "clip trimmed\n";
  } else if (cmd == "session-split-clip") {
    if (a.size() != 4) throw std::runtime_error("session-split-clip requires S.fks TRACK_INDEX CLIP_INDEX SPLIT_OFFSET_FRAMES");
    auto s = load_session(a[0]);
    need(fk::engine::split_clip(s, static_cast<std::size_t>(unum(a[1], "track index")), static_cast<std::size_t>(unum(a[2], "clip index")), unum(a[3], "split offset")));
    need(fk::engine::save(a[0], s, true)); std::cout << "clip split\n";
  } else if (cmd == "session-fades") {
    if (a.size() != 5) throw std::runtime_error("session-fades requires S.fks TRACK_INDEX CLIP_INDEX FADE_IN_FRAMES FADE_OUT_FRAMES");
    auto s = load_session(a[0]);
    need(fk::engine::set_clip_fades(s, static_cast<std::size_t>(unum(a[1], "track index")), static_cast<std::size_t>(unum(a[2], "clip index")), unum(a[3], "fade-in frames"), unum(a[4], "fade-out frames")));
    need(fk::engine::save(a[0], s, true)); std::cout << "clip fades set\n";
  } else if (cmd == "session-render") {
    bool overwrite = has_flag(a, "--overwrite");
    if (a.size() != 2) throw std::runtime_error("session-render requires S.fks OUT.wav");
    require_new_output({}, a[1], overwrite);
    auto s = load_session(a[0]);
    auto r = fk::engine::render(s, fs::path(a[0]).parent_path());
    auto& out = need(r);
    fk::audio::write_wav_pcm16(a[1], out, overwrite);
    std::cout << "wrote " << a[1] << "\n" << fk::audio::describe(out) << "\n";
  } else if (cmd == "lane-mode") {
    if (a.size() != 3) throw std::runtime_error("lane-mode requires S.fks TRACK_INDEX off|read|touch|latch|touch/latch|write|trim");
    auto m = fk::lanes::parse_mode(a[2]); if (!m) throw std::runtime_error("unknown automation mode: " + a[2]);
    auto s = load_session(a[0]); need(fk::engine::set_mode(s, unum(a[1], "track index"), *m)); need(fk::engine::save(a[0], s, true));
    std::cout << "mode set to " << fk::lanes::mode_label(*m) << "\n";
  } else if (cmd == "lane-point") {
    if (a.size() != 5) throw std::runtime_error("lane-point requires S.fks TRACK_INDEX PARAM SAMPLE VALUE");
    auto p = fk::lanes::AutoParam::parse(a[2]); if (!p) throw std::runtime_error("unknown parameter id: " + a[2]);
    auto s = load_session(a[0]); need(fk::engine::set_point(s, unum(a[1], "track index"), *p, static_cast<std::int64_t>(unum(a[3], "sample")), num(a[4], "value")));
    need(fk::engine::save(a[0], s, true)); std::cout << "point set on " << p->label() << "\n";
  } else if (cmd == "lane-range") {
    if (a.size() != 6) throw std::runtime_error("lane-range requires S.fks TRACK_INDEX PARAM START END VALUE");
    auto p = fk::lanes::AutoParam::parse(a[2]); if (!p) throw std::runtime_error("unknown parameter id: " + a[2]);
    auto s = load_session(a[0]);
    need(fk::engine::write_range(s, unum(a[1], "track index"), *p, static_cast<std::int64_t>(unum(a[3], "start")), static_cast<std::int64_t>(unum(a[4], "end")), num(a[5], "value")));
    need(fk::engine::save(a[0], s, true)); std::cout << "range written on " << p->label() << "\n";
  } else if (cmd == "lane-thin") {
    if (a.size() != 4) throw std::runtime_error("lane-thin requires S.fks TRACK_INDEX PARAM TOLERANCE");
    auto p = fk::lanes::AutoParam::parse(a[2]); if (!p) throw std::runtime_error("unknown parameter id: " + a[2]);
    auto s = load_session(a[0]); auto r = fk::engine::thin_lane(s, unum(a[1], "track index"), *p, num(a[3], "tolerance"));
    std::size_t removed = need(r); need(fk::engine::save(a[0], s, true)); std::cout << "removed " << removed << " points\n";
  } else if (cmd == "lane-show") {
    std::string only; take_opt(a, "--param", only);
    if (a.size() != 2) throw std::runtime_error("lane-show requires S.fks TRACK_INDEX [--param ID]");
    auto s = load_session(a[0]); std::size_t ti = unum(a[1], "track index");
    if (ti >= s.tracks.size()) throw std::runtime_error("no such track");
    auto& t = s.tracks[ti];
    std::cout << "track " << ti << " \"" << t.name << "\" automation mode: " << fk::lanes::mode_label(t.mode) << "\n";
    for (auto& l : t.lanes) {
      if (!only.empty()) { auto p = fk::lanes::AutoParam::parse(only); if (!p || !(*p == l.param)) continue; }
      std::cout << "  " << l.param.id() << " (" << l.param.label() << ") points=" << l.points.size() << (l.enabled ? "" : " [disabled]") << "\n";
      if (!only.empty()) for (auto& pt : l.points) std::cout << "    " << pt.at << " " << pt.value << "\n";
    }
  } else if (cmd == "lane-record") {
    // Replays a recorded stream of control events through the live-capture state machine (Write/Touch/Latch) offline.
    std::string ret; std::int64_t retn = 0; if (take_opt(a, "--return", ret)) retn = static_cast<std::int64_t>(unum(ret, "return samples"));
    if (a.size() != 4) throw std::runtime_error("lane-record requires S.fks TRACK_INDEX PARAM EVENTS.txt [--return SAMPLES]");
    auto p = fk::lanes::AutoParam::parse(a[2]); if (!p) throw std::runtime_error("unknown parameter id: " + a[2]);
    auto s = load_session(a[0]); std::size_t ti = unum(a[1], "track index");
    if (ti >= s.tracks.size()) throw std::runtime_error("no such track");
    std::ifstream f(a[3]); if (!f) throw std::runtime_error("cannot open " + a[3]);
    fk::lanes::AutomationLane existing(*p);
    if (auto* l = fk::engine::find_lane(s.tracks[ti], *p)) existing = *l;
    fk::lanes::RecorderConfig cfg; cfg.return_samples = retn;
    fk::lanes::AutomationRecorder rec(s.tracks[ti].mode, *p, existing, cfg);
    std::string line; int ln = 0; std::size_t events = 0;
    while (std::getline(f, line)) {
      ++ln; if (line.empty() || line[0] == '#') continue;
      std::istringstream ls(line); std::string kw; ls >> kw;
      fk::Status st;
      if (kw == "begin") { long long at; if (!(ls >> at)) throw std::runtime_error("line " + std::to_string(ln) + ": begin AT"); st = rec.begin_pass(at); }
      else if (kw == "end") { long long at; if (!(ls >> at)) throw std::runtime_error("line " + std::to_string(ln) + ": end AT"); st = rec.end_pass(at); }
      else if (kw == "u") { long long at; double v; int touch; if (!(ls >> at >> v >> touch) || (touch != 0 && touch != 1)) throw std::runtime_error("line " + std::to_string(ln) + ": u AT VALUE 0|1"); st = rec.update(at, v, touch == 1); ++events; }
      else throw std::runtime_error("line " + std::to_string(ln) + ": unknown event '" + kw.substr(0, 16) + "'");
      if (!st) throw std::runtime_error("line " + std::to_string(ln) + ": " + st.error);
    }
    need(fk::engine::replace_lane(s, ti, rec.lane())); need(fk::engine::save(a[0], s, true));
    std::cout << "replayed " << events << " control events in mode " << fk::lanes::mode_label(s.tracks[ti].mode) << "; lane now has " << rec.lane().points.size() << " points\n";
  } else if (cmd == "marker-add") {
    std::string kind = "marker", ends, rul = "1", numS;
    take_opt(a, "--kind", kind); bool has_end = take_opt(a, "--end", ends); take_opt(a, "--ruler", rul); bool has_num = take_opt(a, "--number", numS);
    if (a.size() != 3) throw std::runtime_error("marker-add requires S.fks NAME SAMPLE [--kind marker|selection|none] [--end SAMPLE] [--ruler 1-5] [--number N]");
    auto k = fk::markers::parse_kind(kind); if (!k) throw std::runtime_error("kind must be marker, selection or none");
    auto s = load_session(a[0]); auto st = static_cast<std::int64_t>(unum(a[2], "sample"));
    auto r = s.markers.add(*k, a[1], st, has_end ? static_cast<std::int64_t>(unum(ends, "end")) : st, static_cast<std::uint8_t>(unum(rul, "ruler")), has_num ? static_cast<std::uint32_t>(unum(numS, "number")) : 0);
    auto n = need(r); need(fk::engine::save(a[0], s, true)); std::cout << "memory location " << n << " added\n";
  } else if (cmd == "marker-list") {
    if (a.size() != 1) throw std::runtime_error("marker-list requires S.fks");
    auto s = load_session(a[0]);
    for (auto& m : s.markers.all()) std::cout << m.number << "  " << fk::markers::kind_label(m.kind) << "  start=" << m.start << " end=" << m.end << " ruler=" << int(m.ruler) << "  \"" << m.name << "\"\n";
    if (s.markers.size() == 0) std::cout << "no memory locations\n";
  } else if (cmd == "marker-remove" || cmd == "marker-recall") {
    if (a.size() != 2) throw std::runtime_error(cmd + " requires S.fks NUMBER");
    auto s = load_session(a[0]); auto n = static_cast<std::uint32_t>(unum(a[1], "number"));
    if (cmd == "marker-remove") { need(s.markers.remove(n)); need(fk::engine::save(a[0], s, true)); std::cout << "removed\n"; }
    else { auto r = s.markers.recall(n); auto& m = need(r); std::cout << m.name << " start=" << m.start << " end=" << m.end << "\n"; }
  } else if (cmd == "marker-next" || cmd == "marker-prev") {
    if (a.size() != 2) throw std::runtime_error(cmd + " requires S.fks SAMPLE");
    auto s = load_session(a[0]); auto pos = static_cast<std::int64_t>(unum(a[1], "sample"));
    auto m = cmd == "marker-next" ? s.markers.next_after(pos) : s.markers.previous_before(pos);
    if (!m) std::cout << "none\n"; else std::cout << m->number << " \"" << m->name << "\" at " << m->start << "\n";
  } else if (cmd == "counter") {
    std::string srs = "48000", bpm = "120", fpss = "30";
    take_opt(a, "--sr", srs); take_opt(a, "--bpm", bpm); take_opt(a, "--fps", fpss);
    if (a.size() != 1) throw std::runtime_error("counter requires SAMPLE [--sr N] [--bpm B] [--fps 24|25|30|29.97df]");
    using F = fk::time::Fps;
    F f = fpss == "24" ? F::F24 : fpss == "25" ? F::F25 : fpss == "30" ? F::F30 : fpss == "29.97df" ? F::F2997Drop : throw std::runtime_error("fps must be 24, 25, 30 or 29.97df");
    fk::time::TempoMap tm; need(tm.add_tempo(0, num(bpm, "bpm")));
    auto r = fk::counter::format_all(static_cast<std::int64_t>(unum(a[0], "sample")), static_cast<std::uint32_t>(unum(srs, "sample rate")), tm, f);
    auto& c = need(r);
    std::cout << "  " << c.timecode << "   |   " << c.bars_beats << "   |   " << c.min_sec << "   |   " << c.samples << " samples\n";
  } else if (cmd == "bbt") {
    if (a.size() != 4) throw std::runtime_error("bbt requires BPM NUM DEN QUARTER_BEATS");
    fk::time::TempoMap m;
    need(m.add_tempo(0, num(a[0], "bpm")));
    need(m.set_meter({static_cast<int>(num(a[1], "numerator")), static_cast<int>(num(a[2], "denominator"))}));
    double beats = num(a[3], "beats");
    auto b = m.to_bbt(beats); auto sec = m.beats_to_seconds(beats);
    std::cout << need(b).bar << "|" << need(b).beat << "|" << need(b).tick << "  seconds=" << need(sec) << "\n";
  } else if (cmd == "timecode") {
    if (a.size() != 2) throw std::runtime_error("timecode requires FRAME 24|25|30|29.97df");
    using F = fk::time::Fps;
    F f = a[1] == "24" ? F::F24 : a[1] == "25" ? F::F25 : a[1] == "30" ? F::F30 : a[1] == "29.97df" ? F::F2997Drop
          : throw std::runtime_error("fps must be 24, 25, 30 or 29.97df");
    auto t = fk::time::frames_to_timecode(static_cast<std::int64_t>(unum(a[0], "frame")), f);
    std::cout << need(t) << "\n";
  } else if (cmd == "sensitive-check") {
    if (a.size() != 1) throw std::runtime_error("sensitive-check requires FILE");
    std::ifstream f(a[0], std::ios::binary);
    if (!f) throw std::runtime_error("cannot open " + a[0]);
    std::ostringstream ss; ss << f.rdbuf();
    auto fnd = fk::clipboard::scan_sensitive(ss.str());
    if (fnd.empty()) std::cout << "no likely-sensitive patterns found (heuristic; not a guarantee)\n";
    for (auto& x : fnd) std::cout << "possible: " << x.kind << "\n";
  } else {
    usage();
    throw std::runtime_error("unknown command: " + cmd);
  }
  return 0;
}
}  // namespace
int main(int argc, char** argv) {
  try { return run(Args(argv + 1, argv + argc)); }
  catch (const std::exception& e) { std::cerr << "HUSH error: " << e.what() << "\n"; return 1; }
}
