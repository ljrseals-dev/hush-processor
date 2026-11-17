// Tests for automation lanes (translated Rust tests + extras), live Write/Touch/Latch capture, markers, big counter, session.
#include <cmath>
#include <filesystem>
#include <iostream>
#include "audio.hpp"
#include "automation.hpp"
#include "counter.hpp"
#include "markers.hpp"
#include "recorder.hpp"
#include "session.hpp"
using namespace fk::lanes;
static int g_fail = 0, g_run = 0;
#define CHECK(c, m) do { ++g_run; if (!(c)) { ++g_fail; std::cout << "[FAIL] " << m << "  (" #c ")\n"; } else std::cout << "[ ok ] " << m << "\n"; } while (0)
static bool near(double a, double b, double e = 1e-6) { return std::abs(a - b) < e; }

static void rust_translated() {
  AutomationLane l(AutoParam::volume());
  CHECK(l.value_at(10, -3.0) == -3.0, "empty lane returns the default");
  l.set_point(0, 0.0); l.set_point(100, -10.0);
  CHECK(near(l.value_at(50, 0.0), -5.0) && l.value_at(1000, 0.0) == -10.0, "linear interpolation, holds last value");
  AutomationLane m(AutoParam::mute()); m.set_point(0, 0.0); m.set_point(100, 1.0);
  CHECK(m.value_at(99, 0.0) == 0.0 && m.value_at(100, 0.0) == 1.0, "mute lanes step instead of ramping");
  AutomationLane w(AutoParam::volume()); w.set_point(0, 0.0); w.set_point(1000, 0.0); w.write_range(200, 400, -20.0, 0.0);
  CHECK(w.value_at(100, 0.0) == 0.0 && w.value_at(300, 0.0) == -20.0 && w.value_at(600, 0.0) == 0.0, "write_range keeps the curve outside the range");
  AutomationLane t(AutoParam::volume()); for (int i = 0; i <= 10; ++i) t.set_point(i * 10, -double(i));
  CHECK(t.thin(0.01) == 9 && t.points.size() == 2, "thin removes collinear points (9 of 11)");
  CHECK(AutoParam::parse("send_b_level") && *AutoParam::parse("send_b_level") == AutoParam::send_level(1), "parse send_b_level");
  CHECK(AutoParam::parse("plugin:a:threshold") && *AutoParam::parse("plugin:a:threshold") == AutoParam::plugin(0, "threshold"), "parse plugin:a:threshold");
  CHECK(!AutoParam::parse("nope") && !AutoParam::parse("plugin:a:bad name") && !AutoParam::parse("send_a_foo"), "invalid parameter ids rejected");
  CHECK(parse_mode("touch/latch") == AutomationMode::TouchLatch && parse_mode("TOUCH_LATCH") == AutomationMode::TouchLatch && !parse_mode("x"), "mode ids");
  AutomationLane p(AutoParam::pan(0)); p.set_point(0, std::nan("")); p.set_point(1, 5.0);
  CHECK(p.points[0].value == 0.0 && p.points[1].value == 1.0, "set_point clamps NaN and out-of-range values");
  bool rt = true;
  for (auto id : {"volume", "pan", "pan2", "mute", "send_a_level", "send_c_pan", "send_z_mute", "plugin:b:Cutoff_1"}) { auto q = AutoParam::parse(id); if (!q || q->id() != id) rt = false; }
  CHECK(rt, "parameter ids round-trip through parse()/id()");
  AutomationLane s(AutoParam::volume()); s.set_point(0, 0); s.set_point(100, -6); s.set_point(200, -12); s.shift_from(100, 50);
  CHECK(s.points.size() == 3 && s.points[1].at == 150 && s.points[2].at == 250 && s.points[0].at == 0, "shift_from moves later points");
  CHECK(AutoParam::plugin(0, "x").range().hi > 1e30, "plugin parameters have an unbounded range");
}

static AutomationLane flat(double v = 0.0, AutoParam p = AutoParam::volume()) { AutomationLane l(p); l.set_point(0, v); l.set_point(1000, v); return l; }
static void recorder_modes() {
  { AutomationRecorder r(AutomationMode::Write, AutoParam::volume(), flat()); r.begin_pass(200);
    for (Samples at = 200; at <= 400; at += 50) { r.update(at, -20.0, false); }
    r.end_pass(400);
    auto& l = r.lane(); CHECK(near(l.value_at(100, 0), 0) && near(l.value_at(300, 0), -20) && near(l.value_at(600, 0), 0), "Write: overwrites the pass even if the control is never touched; outside keeps the original"); }
  { AutomationRecorder r(AutomationMode::Touch, AutoParam::volume(), flat()); r.begin_pass(0);
    r.update(100, -5, false); r.update(250, -5, false); r.update(300, -20, true); r.update(350, -20, true); r.update(400, -20, true); r.update(410, -20, false); r.end_pass(1000);
    auto& l = r.lane(); CHECK(near(l.value_at(250, 0), 0) && near(l.value_at(350, 0), -20) && near(l.value_at(450, 0), 0), "Touch: writes only while touched, returns to the original after release"); }
  { RecorderConfig c; c.return_samples = 100;
    AutomationRecorder r(AutomationMode::Touch, AutoParam::volume(), flat(), c); r.begin_pass(0);
    r.update(300, -20, true); r.update(400, -20, true); r.update(410, -20, false); r.end_pass(1000);
    auto& l = r.lane(); double mid = l.value_at(460, 0);
    CHECK(near(l.value_at(410, 0), -20) && mid < -5 && mid > -15 && near(l.value_at(520, 0), 0), "Touch with a return time glides back to the original curve (value at midpoint " + std::to_string(mid) + ")"); }
  { AutomationRecorder r(AutomationMode::Latch, AutoParam::volume(), flat()); r.begin_pass(0);
    r.update(100, -3, false); r.update(300, -20, true); r.update(350, -20, false); r.update(500, -20, false); r.end_pass(600);
    auto& l = r.lane(); CHECK(near(l.value_at(200, 0), 0) && near(l.value_at(550, 0), -20) && near(l.value_at(700, 0), 0), "Latch: starts at first touch, keeps writing after release until the pass ends"); }
  { AutomationRecorder rf(AutomationMode::TouchLatch, AutoParam::volume(), flat()); rf.begin_pass(0);
    rf.update(300, -20, true); rf.update(350, -20, false); rf.update(500, -20, false); rf.end_pass(600);
    CHECK(near(rf.lane().value_at(500, 0), 0), "Touch/Latch: a fader acts like Touch (released fader stops writing)");
    AutomationLane mlane(AutoParam::mute()); mlane.set_point(0, 0);
    AutomationRecorder rm(AutomationMode::TouchLatch, AutoParam::mute(), mlane); rm.begin_pass(0);
    rm.update(300, 1, true); rm.update(350, 1, false); rm.update(500, 1, false); rm.end_pass(600);
    CHECK(near(rm.lane().value_at(500, 0), 1) && near(rm.lane().value_at(700, 0), 0), "Touch/Latch: a mute switch acts like Latch"); }
  for (auto m : {AutomationMode::Read, AutomationMode::Off, AutomationMode::Trim}) {
    AutomationRecorder r(m, AutoParam::volume(), flat()); r.begin_pass(0); r.update(100, -20, true); r.update(200, -20, true); r.end_pass(300);
    CHECK(r.lane().points.size() == 2 && near(r.lane().value_at(150, 0), 0), std::string("mode ") + mode_label(m) + ": events are ignored, lane unchanged");
  }
  { AutomationRecorder r(AutomationMode::Write, AutoParam::volume(), flat());
    CHECK(!r.update(10, 0, true).ok, "update before begin_pass is rejected");
    r.begin_pass(100); r.update(200, 0, true);
    CHECK(!r.update(150, 0, true).ok && !r.update(300, std::nan(""), true).ok && !r.begin_pass(0).ok, "backwards time, NaN value and double begin are rejected");
    CHECK(!r.end_pass(100).ok && r.end_pass(300).ok && !r.end_pass(400).ok, "end before the last event / end without a pass are rejected"); }
  { AutomationLane e(AutoParam::pan(0));
    AutomationRecorder r(AutomationMode::Write, AutoParam::pan(0), e); r.begin_pass(0);
    for (int i = 0; i <= 100; ++i) { r.update(i * 10, -1.0 + 0.02 * i, true); }
    r.end_pass(1000);
    CHECK(near(r.lane().value_at(500, 9), 0.0, 0.03) && near(r.lane().value_at(0, 9), -1.0), "a pan sweep recorded in Write reads back correctly"); }
  { RecorderConfig c; c.min_value_step = 0.5; c.min_time_step = 1000000;
    AutomationRecorder r(AutomationMode::Write, AutoParam::volume(), AutomationLane(AutoParam::volume())); (void)c;
    AutomationRecorder r2(AutomationMode::Write, AutoParam::volume(), AutomationLane(AutoParam::volume()), c);
    r2.begin_pass(0);
    for (int i = 0; i < 1000; ++i) { r2.update(i, 0.001 * (i % 3), true); }
    r2.end_pass(1000);
    CHECK(r2.lane().points.size() < 10, "tiny control jitter is thinned (" + std::to_string(r2.lane().points.size()) + " points from 1000 events)"); }
}

static void markers_and_counter() {
  fk::markers::MemoryLocations ml;
  auto a = ml.add(fk::markers::MarkerKind::Marker, "Verse", 48000, 0), b = ml.add(fk::markers::MarkerKind::Marker, "Chorus", 480000, 0);
  auto c = ml.add(fk::markers::MarkerKind::Selection, "Solo", 960000, 1440000, 2);
  CHECK(a && *a == 1 && b && *b == 2 && c && *c == 3, "numbers are assigned lowest-free-first");
  CHECK(ml.find(1)->end == ml.find(1)->start, "a point marker's end equals its start");
  CHECK(!ml.add(fk::markers::MarkerKind::Marker, "dup", 1, 1, 1, 2) && !ml.add(fk::markers::MarkerKind::Selection, "bad", 100, 50) && !ml.add(fk::markers::MarkerKind::Marker, "r", 0, 0, 9), "duplicate number, backwards selection and bad ruler are rejected");
  ml.remove(2); auto d = ml.add(fk::markers::MarkerKind::Marker, "Bridge", 700000, 0);
  CHECK(d && *d == 2, "a freed number is reused");
  CHECK(ml.next_after(48000)->name == "Bridge" && ml.previous_before(700001)->name == "Bridge" && !ml.next_after(2000000), "next/previous marker navigation");
  CHECK(ml.recall(3)->end == 1440000 && !ml.recall(99), "recall returns the stored range; unknown number is an error");
  CHECK(ml.rename(1, "Verse 1").ok && !ml.rename(99, "x").ok && ml.relocate(1, 100, 100).ok && !ml.relocate(3, 9, 1).ok, "rename / relocate with validation");
  fk::time::TempoMap tm;
  auto r = fk::counter::format_all(48000 * 61 + 4800, 48000, tm, fk::time::Fps::F30);
  CHECK(r && r->min_sec == "0:01:01.100" && r->samples == "2932800" && r->timecode == "00:01:01:03" && r->bars_beats == "31|3|192", "big counter formats every mode (" + (r ? r->timecode + " | " + r->bars_beats + " | " + r->min_sec : r.error) + ")");
  CHECK(!fk::counter::format_all(-1, 48000, tm, fk::time::Fps::F30) && !fk::counter::format_all(0, 0, tm, fk::time::Fps::F30), "counter rejects negative positions and zero sample rate");
}

static void session_integration() {
  using namespace fk::engine;
  Session s; add_track(s, "Vox"); add_track(s, "Bass");
  set_mode(s, 0, AutomationMode::TouchLatch);
  set_point(s, 0, AutoParam::volume(), 0, 0); set_point(s, 0, AutoParam::volume(), 48000, -12);
  set_point(s, 0, AutoParam::plugin(0, "threshold"), 100, -18.5); set_point(s, 1, AutoParam::send_level(1), 10, -3);
  s.markers.add(fk::markers::MarkerKind::Selection, "Hook \"A\" 100%", 1000, 5000, 3);
  s.markers.set_color(1, std::array<std::uint8_t, 3>{255, 128, 0}); s.markers.set_comments(1, "take 2 is better");
  auto text = serialize(s); auto back = parse(text);
  CHECK(back && back->tracks[0].mode == AutomationMode::TouchLatch && back->tracks[0].lanes.size() == 2 && back->tracks[0].lanes[0].points.size() == 2 &&
        near(back->tracks[0].lanes[1].points[0].value, -18.5) && back->tracks[1].lanes[0].param == AutoParam::send_level(1), "lanes, modes and plugin parameters survive serialize/parse");
  CHECK(back && back->markers.size() == 1 && back->markers.find(1)->name == "Hook \"A\" 100%" && back->markers.find(1)->color && (*back->markers.find(1)->color)[1] == 128 &&
        back->markers.find(1)->comments == "take 2 is better" && back->markers.find(1)->end == 5000, "memory locations survive serialize/parse");
  CHECK(serialize(*back) == text, "serialize(parse(x)) is stable");
  const char* hdr = "FKSESSION 1\nsample_rate 48000\nbpm 120\n";
  CHECK(!parse(std::string(hdr) + "mode read\n") && !parse(std::string(hdr) + "track a 0 0\nmode sideways\n") && !parse(std::string(hdr) + "pt 1 2\n"), "mode/pt before track or lane, and unknown modes, are rejected");
  CHECK(!parse(std::string(hdr) + "track a 0 0\nlane volume 1\nlane volume 1\n") && !parse(std::string(hdr) + "track a 0 0\nlane bogus 1\n") && !parse(std::string(hdr) + "track a 0 0\nlane volume 1\npt -5 1\n"), "duplicate lanes, unknown params and negative times are rejected");
  CHECK(!parse(std::string(hdr) + "marker 1 marker 0 0 9 -1 a b -\n") && !parse(std::string(hdr) + "marker 1 marker 0 0 1 -1 a b -\nmarker 1 marker 5 5 1 -1 c d -\n") && !parse(std::string(hdr) + "marker 0 marker 0 0 1 -1 a b -\n"), "bad ruler, duplicate number and number 0 are rejected");
  auto err = parse(std::string(hdr) + "track a 0 0\nlane volume 1\npt 1 nan\n");
  CHECK(!err && err.error.find("line 6") != std::string::npos, "malformed lane data reports its line number");
  UndoStack u; Session e; add_track(e, "T"); u.checkpoint(e); set_point(e, 0, AutoParam::volume(), 0, -6);
  CHECK(e.tracks[0].lanes.size() == 1 && u.undo(e) && e.tracks[0].lanes.empty() && u.redo(e) && e.tracks[0].lanes.size() == 1, "lane edits are undoable and redoable");
  CHECK(!set_point(e, 5, AutoParam::volume(), 0, 0).ok && !set_point(e, 0, AutoParam::volume(), -1, 0).ok && !write_range(e, 0, AutoParam::volume(), 10, 10, 0).ok && !thin_lane(e, 0, AutoParam::pan(), 0.1), "invalid lane edits fail safely");
  // render with a volume lane and a mute lane
  auto dir = std::filesystem::temp_directory_path() / "fk_lane_render"; std::filesystem::create_directories(dir);
  fk::audio::AudioBuffer dc{48000, 1, std::vector<float>(48000, 0.5f)}; fk::audio::write_wav_pcm16(dir / "dc.wav", dc);
  Session r; add_track(r, "T"); add_clip(r, 0, "dc.wav", 0);
  set_point(r, 0, AutoParam::volume(), 0, 0); set_point(r, 0, AutoParam::volume(), 24000, -20);
  set_point(r, 0, AutoParam::mute(), 0, 0); set_point(r, 0, AutoParam::mute(), 36000, 1);
  auto out = render(r, dir);
  float s0 = out->samples[0], smid = out->samples[24000 - 100], smute = out->samples[40000];
  CHECK(out && near(s0, 0.5, 2e-3) && smid < 0.1f && smid > 0.04f && near(smute, 0.0, 1e-6), "offline render applies volume (ramp to -20 dB) and mute (step) lanes (" + std::to_string(s0) + ", " + std::to_string(smid) + ", " + std::to_string(smute) + ")");
  set_mode(r, 0, AutomationMode::Off); auto off = render(r, dir);
  CHECK(off && near(off->samples[40000], 0.5, 2e-3) && near(off->samples[0], 0.5, 2e-3), "mode Off ignores all lanes at render time");
  std::filesystem::remove_all(dir);
}
int main() {
  rust_translated(); recorder_modes(); markers_and_counter(); session_integration();
  std::cout << "\n" << (g_run - g_fail) << "/" << g_run << " checks passed\n";
  return g_fail == 0 ? 0 : 1;
}
