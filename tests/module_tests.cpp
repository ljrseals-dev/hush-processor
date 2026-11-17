// HUSH module tests: time, hash, watermark, clipboard, forensics, session. No network, temp files only.
#include <cmath>
#include <filesystem>
#include <iostream>
#include <random>
#include "audio.hpp"
#include "clipboard.hpp"
#include "forensics.hpp"
#include "hash.hpp"
#include "session.hpp"
#include "time.hpp"
#include "watermark.hpp"
using namespace fk;
static int g_fail = 0, g_run = 0;
#define CHECK(c, m) do { ++g_run; if (!(c)) { ++g_fail; std::cout << "[FAIL] " << m << "  (" #c ")\n"; } else std::cout << "[ ok ] " << m << "\n"; } while (0)

static audio::AudioBuffer program(double seconds, unsigned seed, std::uint16_t ch = 1, double noise_amp = 0.02) {
  std::mt19937 g(seed);
  std::uniform_real_distribution<double> d(-1, 1);
  const std::size_t fr = static_cast<std::size_t>(seconds * 48000);
  audio::AudioBuffer a{48000, ch, std::vector<float>(fr * ch)};
  const double pi = 3.14159265358979323846;
  for (std::size_t i = 0; i < fr; ++i) {
    double t = double(i) / 48000;
    double v = 0.10 * std::sin(2 * pi * 220 * t) + 0.08 * std::sin(2 * pi * 659 * t) + 0.05 * std::sin(2 * pi * 2093 * t) + noise_amp * d(g);
    for (std::uint16_t c = 0; c < ch; ++c) a.samples[i * ch + c] = static_cast<float>(v);
  }
  return a;
}
static audio::AudioBuffer with_noise(audio::AudioBuffer a, double amp, unsigned seed) {
  std::mt19937 g(seed); std::uniform_real_distribution<double> d(-1, 1);
  for (auto& x : a.samples) x += static_cast<float>(amp * d(g));
  return a;
}
static std::vector<bool> bits(const char* s) { std::vector<bool> v; for (; *s; ++s) v.push_back(*s == '1'); return v; }

static void test_hash() {
  const unsigned char abc[] = {'a', 'b', 'c'};
  CHECK(hash::sha256_hex(abc, 3) == "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad", "SHA-256 of 'abc'");
  CHECK(hash::sha256_hex(abc, 0) == "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855", "SHA-256 of empty input");
  std::string big(1000000, 'a');
  CHECK(hash::sha256_hex(reinterpret_cast<const unsigned char*>(big.data()), big.size()) ==
        "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0", "SHA-256 of one million 'a'");
}
static void test_time() {
  time::TempoMap m;
  CHECK(std::abs(*m.beats_to_seconds(4) - 2.0) < 1e-12, "120 BPM: 4 beats = 2 s");
  CHECK(*m.beats_to_samples(2, 48000) == 48000, "120 BPM: 2 beats = 48000 samples @48k");
  auto b = m.to_bbt(4.0);
  CHECK(b && b->bar == 2 && b->beat == 1 && b->tick == 0, "4 quarter beats in 4/4 = bar 2 beat 1");
  CHECK(m.add_tempo(4.0, 60.0).ok, "add tempo change at beat 4");
  CHECK(std::abs(*m.beats_to_seconds(8) - 6.0) < 1e-12, "tempo map: 4 beats@120 + 4@60 = 6 s");
  CHECK(std::abs(*m.seconds_to_beats(6.0) - 8.0) < 1e-12, "seconds->beats inverts across a tempo change");
  CHECK(std::abs(*m.seconds_to_beats(*m.beats_to_seconds(5.37)) - 5.37) < 1e-9, "round trip arbitrary beat");
  CHECK(!m.add_tempo(1.0, 5.0).ok && !m.add_tempo(-1.0, 120).ok && !m.add_tempo(1.0, NAN).ok, "invalid tempo rejected");
  CHECK(!m.beats_to_seconds(-1) && !m.beats_to_seconds(INFINITY), "invalid beat positions rejected");
  time::TempoMap six8; CHECK(six8.set_meter({6, 8}).ok, "6/8 meter accepted");
  auto s = six8.to_bbt(3.0);
  CHECK(s && s->bar == 2 && s->beat == 1, "6/8: 3 quarter beats = one bar");
  CHECK(!six8.set_meter({4, 3}).ok && !six8.set_meter({0, 4}).ok, "invalid meters rejected");
  auto f = six8.from_bbt({2, 1, 0});
  CHECK(f && std::abs(*f - 3.0) < 1e-12, "6/8 from_bbt");
  CHECK(!six8.from_bbt({1, 7, 0}), "beat beyond meter rejected");
  using F = time::Fps;
  CHECK(*time::frames_to_timecode(108000, F::F30) == "01:00:00:00", "30 fps: 108000 frames = 1 hour");
  CHECK(*time::frames_to_timecode(1800, F::F2997Drop) == "00:01:00;02", "29.97 DF: frame 1800 = 00:01:00;02");
  CHECK(*time::frames_to_timecode(17982, F::F2997Drop) == "00:10:00;00", "29.97 DF: 17982 frames = 10:00;00");
  bool rt = true;
  for (std::int64_t n = 0; n < 40000; n += 7) {
    auto tc = time::frames_to_timecode(n, F::F2997Drop);
    auto back = time::timecode_to_frames(*tc, F::F2997Drop);
    if (!back || *back != n) rt = false;
  }
  CHECK(rt, "drop-frame timecode round trips for 40000 frames");
  CHECK(!time::timecode_to_frames("00:01:00;00", F::F2997Drop), "dropped frame label rejected");
  CHECK(!time::timecode_to_frames("00:00:00:30", F::F30) && !time::timecode_to_frames("garbage", F::F25), "bad timecode rejected");
}
static void test_watermark() {
  auto prog = program(20, 1);
  auto original = prog;
  watermark::EmbedParams p; p.key = 12345; p.payload = bits("10110010"); p.strength_db = -46;
  auto e = watermark::embed(prog, p);
  CHECK(e.value.has_value(), "embed succeeds on 20 s program");
  CHECK(prog.samples == original.samples, "embed does not modify the input buffer");
  CHECK(e && e->peak_after <= 1.0 && e->watermark_to_signal_db < -20 && e->watermark_to_signal_db > -40, "watermark level is reported (" + std::to_string(e ? e->watermark_to_signal_db : 0) + " dB re program RMS); audibility is NOT tested");
  auto d = watermark::detect(e->audio, 12345, 8);
  CHECK(d && d->verdict == watermark::Verdict::SignatureLikeResidualDetected, "known positive: signature-like residual detected (z=" + std::to_string(d ? d->z_score : 0) + ")");
  CHECK(d && d->decoded_bits == bits("10110010"), "payload decodes exactly");
  auto clean = watermark::detect(prog, 12345, 8);
  CHECK(clean && clean->verdict == watermark::Verdict::NoSignatureDetected, "known negative: clean program not flagged (z=" + std::to_string(clean ? clean->z_score : 0) + ")");
  auto wrong = watermark::detect(e->audio, 999, 8);
  CHECK(wrong && wrong->verdict == watermark::Verdict::NoSignatureDetected, "wrong key does not detect");
  auto noisy = watermark::detect(with_noise(e->audio, 0.03, 7), 12345, 8);
  CHECK(noisy && noisy->verdict == watermark::Verdict::SignatureLikeResidualDetected, "adversarial: +0.03 white noise still detected (z=" + std::to_string(noisy ? noisy->z_score : 0) + ")");
  auto path = std::filesystem::temp_directory_path() / "fk_wm_roundtrip.wav";
  audio::write_wav_pcm16(path, e->audio);
  auto rt = watermark::detect(audio::read_wav(path), 12345, 8);
  std::filesystem::remove(path);
  CHECK(rt && rt->verdict == watermark::Verdict::SignatureLikeResidualDetected, "survives 16-bit WAV requantisation (z=" + std::to_string(rt ? rt->z_score : 0) + ")");
  auto st = program(20, 3, 2); watermark::EmbedParams ps = p; ps.payload = bits("1100");
  auto es = watermark::embed(st, ps); auto ds = es ? watermark::detect(es->audio, 12345, 4) : fk::Result<watermark::Detection>{};
  CHECK(ds && ds->verdict == watermark::Verdict::SignatureLikeResidualDetected && ds->decoded_bits == bits("1100"), "stereo program works");
  // honest limits: lowering the level or resampling-like damage reduces detection; record the observed behaviour
  for (double db : {-52.0, -58.0, -64.0, -70.0}) {
    auto wp = p; wp.strength_db = db;
    auto we = watermark::embed(prog, wp);
    auto wd = watermark::detect(we->audio, 12345, 8);
    std::cout << "[info] strength " << db << " dBFS on this 20 s program: " << watermark::verdict_name(wd->verdict) << " (z=" << wd->z_score << ", decoded " << (wd->decoded_bits == bits("10110010") ? "correctly" : "incorrectly") << ")\n";
  }
  int flagged = 0;
  for (unsigned s2 = 100; s2 < 130; ++s2) {
    auto c = watermark::detect(program(10, s2, 1, 0.05), 12345, 8);
    if (c && c->verdict != watermark::Verdict::NoSignatureDetected) ++flagged;
  }
  CHECK(flagged == 0, "false positives on 30 clean synthetic programs: " + std::to_string(flagged) + "/30 (small sample, not a guarantee)");
  CHECK(!watermark::embed(program(0.1, 1), p) && !watermark::detect(program(0.1, 1), 1, 8), "too-short audio rejected");
  watermark::EmbedParams bad = p; bad.payload.clear();
  CHECK(!watermark::embed(prog, bad), "empty payload rejected");
  bad = p; bad.strength_db = -10;
  CHECK(!watermark::embed(prog, bad), "excessive strength rejected");
  audio::AudioBuffer loud = prog; for (auto& x : loud.samples) x = x > 0 ? 0.9999f : -0.9999f;
  CHECK(!watermark::embed(loud, p), "refuses to embed when it would clip");
}
struct FakeClip : clipboard::Provider {
  std::string content; int reads = 0, writes = 0; std::string last_written;
  Result<std::string> read() override { ++reads; return Result<std::string>::success(content); }
  Status write(const std::string& t) override { ++writes; last_written = t; content = t; return Status::success(); }
};
static void test_clipboard() {
  FakeClip fake; fake.content = "hello world";
  std::int64_t now = 1000;
  clipboard::Checker c(fake, [&] { return now; });
  CHECK(!c.enabled(), "new session starts with clipboard access Off");
  CHECK(!c.read_preview() && fake.reads == 0, "read refused while Off and provider never touched");
  CHECK(!c.prepare_write("x") && !c.prepare_clear(), "write/clear refused while Off");
  c.enable();
  auto p = c.read_preview();
  CHECK(p && p->text == "hello world" && p->bytes == 11 && p->findings.empty(), "read preview shows exact content once enabled");
  fake.content = std::string(10000, 'x');
  auto big = c.read_preview();
  CHECK(big && big->truncated && big->text.size() == 4096 && big->bytes == 10000, "long content preview truncated, size reported");
  fake.content = "-----BEGIN RSA PRIVATE KEY-----\nabc\n-----END RSA PRIVATE KEY-----";
  auto k = c.read_preview();
  CHECK(k && !k->findings.empty() && k->findings[0].kind.find("private key") != std::string::npos, "private key block flagged");
  CHECK(!clipboard::scan_sensitive("my password = hunter2hunter2").empty(), "password assignment flagged");
  CHECK(!clipboard::scan_sensitive("key AKIAABCDEFGHIJKLMNOP end").empty(), "AWS-style key id flagged");
  CHECK(!clipboard::scan_sensitive("card 4111 1111 1111 1111 exp").empty(), "Luhn-valid card number flagged");
  CHECK(clipboard::scan_sensitive("card 4111 1111 1111 1112 exp").empty(), "Luhn-invalid digits not flagged as card");
  CHECK(clipboard::scan_sensitive("just a normal sentence about mixing at 120 BPM").empty(), "ordinary text not flagged");
  fake.content = "orig";
  auto plan = c.prepare_write("new text");
  CHECK(plan && fake.writes == 0, "prepare_write does not write");
  CHECK(!c.commit_write(plan->token, false) && fake.writes == 0, "unconfirmed write is not performed");
  auto plan2 = c.prepare_write("new text");
  CHECK(!c.commit_write(plan2->token + 99, true) && fake.writes == 0, "wrong token rejected");
  CHECK(c.commit_write(plan2->token, true).ok && fake.last_written == "new text", "confirmed write performed");
  CHECK(!c.commit_write(plan2->token, true).ok, "token cannot be replayed");
  auto clr = c.prepare_clear();
  CHECK(!c.commit_write(clr->token, true).ok, "clear plan cannot be committed as a write");
  CHECK(c.commit_clear(clr->token, true).ok && fake.content.empty(), "confirmed clear performed");
  // history
  fake.content = "first copy";
  c.read_preview();
  CHECK(c.history_size() == 0, "history is off by default (nothing recorded)");
  CHECK(!c.enable_history(0, 10).ok && !c.enable_history(51, 10).ok && !c.enable_history(5, 0).ok && !c.enable_history(5, 99999).ok, "history bounds enforced");
  CHECK(c.enable_history(2, 60).ok, "history can be enabled explicitly");
  fake.content = "one"; c.read_preview(); fake.content = "two"; c.read_preview(); fake.content = "three"; c.read_preview();
  CHECK(c.history_size() == 2, "history is bounded to max entries");
  fake.content = "password=supersecret1"; c.read_preview();
  CHECK(c.history_size() == 2, "content with likely secrets is not stored in history");
  now += 61;
  CHECK(c.history_size() == 0, "history entries expire after the lifetime");
  fake.content = "again"; c.read_preview(); c.clear_history();
  CHECK(c.history_size() == 0, "clear history empties it");
  auto pend = c.prepare_write("zzz");
  c.disable();
  CHECK(!c.enabled() && !c.commit_write(pend->token, true).ok, "disable cancels a pending write and turns access Off");
  CHECK(!c.read_preview(), "after disable, reads are refused again");
  clipboard::NullProvider np; clipboard::Checker c2(np, [] { return 0; }); c2.enable();
  CHECK(!c2.read_preview(), "default provider has no OS clipboard backend (reports so)");
}
static void test_forensics() {
  auto rep_of = [](const audio::AudioBuffer& a) { return forensics::analyze(a, nullptr, "deadbeef"); };
  auto find = [](const forensics::Report& r, const char* m) -> const forensics::Evidence* {
    for (auto& e : r.evidence) if (e.module.find(m) != std::string::npos) return &e;
    return nullptr; };
  auto tone19 = audio::make_tone(3, 19200, 0.01, 48000, 1);
  auto r1 = rep_of(tone19);
  auto u = find(*r1, "ultrasonic narrowband");
  CHECK(u && u->status == "Narrowband component present" && std::abs(u->location_hz - 19200) < 1, "19.2 kHz beacon-like tone found by native scan");
  auto r2 = rep_of(audio::make_tone(3, 440, 0.5, 48000, 1));
  CHECK(find(*r2, "ultrasonic narrowband")->status == "Not detected" && find(*r2, "mains-hum")->status == "Not detected", "loud 440 Hz tone: native scans quiet");
  auto r3 = rep_of(audio::make_tone(6, 60, 0.01, 48000, 1));
  auto en = find(*r3, "mains-hum");
  CHECK(en && en->status == "Narrowband component present" && std::abs(en->location_hz - 60) < 0.5, "60 Hz hum found by native ENF scan");
  auto r4 = rep_of(program(5, 5));
  CHECK(find(*r4, "ultrasonic narrowband")->status == "Not detected" && find(*r4, "mains-hum")->status == "Not detected", "ordinary program material: native scans quiet");
  auto fu = find(*r2, "ultrasonic detector"); auto fe = find(*r2, "ENF detector");
  CHECK(fu && fu->status == "Idle" && fe && fe->status == "Idle", "CHANGED TEST (was a false-positive characterisation): hardened-fork detectors stay Idle on a loud 440 Hz tone");
  auto fu19 = find(*r1, "ultrasonic detector");
  CHECK(fu19 && fu19->status != "Idle", "hardened-fork ultrasonic detector still detects the 19.2 kHz tone (" + (fu19 ? fu19->status : std::string("?")) + ")");
  audio::AudioBuffer low{16000, 1, std::vector<float>(16000 * 3, 0.1f)};
  auto r5 = rep_of(low);
  CHECK(r5 && !find(*r5, "ultrasonic narrowband")->applicable, "16 kHz sample rate: ultrasonic scan reported not applicable");
  CHECK(!find(*r1, "reference-residual")->applicable, "reference-residual check not applicable without a reference");
  auto same = forensics::analyze(tone19, &tone19, "");
  CHECK(same && find(*same, "reference-residual")->applicable && find(*same, "reference-residual")->status == "Idle", "identical reference: no residual");
  auto wrong = audio::make_tone(1, 440, 0.5, 48000, 1);
  CHECK(!forensics::analyze(tone19, &wrong, ""), "mismatched reference rejected");
  std::string text = r1->to_text();
  CHECK(text.find("deadbeef") != std::string::npos && text.find("Limitations") != std::string::npos, "report includes hash and limitations");
  CHECK(text.find("indicators only") != std::string::npos, "report states findings are indicators only");
}
static void test_session() {
  using namespace engine;
  Session s;
  CHECK(add_track(s, "Lead vox %20 \"x\"").ok && add_track(s, "Bass").ok, "tracks added");
  CHECK(add_clip(s, 0, "my clip.wav", 480).ok && set_gain(s, 1, -6).ok && set_mute(s, 1, true).ok, "clip, gain, mute edits");
  CHECK(!add_track(s, "").ok && !set_gain(s, 0, 100).ok && !set_gain(s, 9, 0).ok && !add_clip(s, 9, "a", 0).ok, "invalid edits rejected");
  auto parsed = parse(serialize(s));
  CHECK(parsed && parsed->tracks.size() == 2 && parsed->tracks[0].name == "Lead vox %20 \"x\"" && parsed->tracks[0].clips[0].path == "my clip.wav" &&
        parsed->tracks[1].mute && parsed->tracks[1].gain_db == -6, "serialize/parse round trip incl. spaces and % in names");
  CHECK(!parse("nope\n") && !parse("FKSESSION 1\nsample_rate 48000\n") && !parse("FKSESSION 1\nsample_rate 5\nbpm 120\n"), "malformed header/fields rejected");
  auto legacy = parse("FKSESSION 1\nsample_rate 48000\nbpm 120\ntrack T 0 0\nclip x.wav 12\n");
  CHECK(legacy && legacy->tracks[0].clips[0].start_frame == 12 && legacy->tracks[0].clips[0].length_frames == 0,
        "legacy FKSESSION 1 clip remains readable");
  CHECK(!parse("FKSESSION 1\nsample_rate 48000\nbpm 120\nclip a 0\n") && !parse("FKSESSION 1\nsample_rate 48000\nbpm 120\nwhat 1\n"), "clip-before-track and unknown keyword rejected");
  CHECK(!parse("FKSESSION 1\nsample_rate 48000\nbpm 120\ntrack %ZZ 0 0\n") && !parse("FKSESSION 1\nsample_rate 48000\nbpm 120\ntrack a%4 0 0\n") && !parse("FKSESSION 1\nsample_rate 48000\nbpm 120\ntrack %Aq 0 0\n"), "bad percent escapes rejected");
  std::string too_many_clips = "FKSESSION 1\nsample_rate 48000\nbpm 120\ntrack T 0 0\n";
  for (int i = 0; i < 10001; ++i) too_many_clips += "clip a 0\n";
  CHECK(!parse(too_many_clips), "session parser enforces global clip limit");
  auto err = parse("FKSESSION 1\nsample_rate 48000\nbpm 9999\n");
  CHECK(!err && err.error.find("line 3") != std::string::npos, "errors carry line numbers");
  CHECK(!parse(std::string(5u << 20, 'x')), "oversized session rejected");
  UndoStack u; Session e;
  u.checkpoint(e); add_track(e, "A");
  u.checkpoint(e); add_track(e, "B");
  CHECK(e.tracks.size() == 2 && u.undo(e) && e.tracks.size() == 1 && u.undo(e) && e.tracks.empty() && !u.undo(e), "undo twice then nothing to undo");
  CHECK(u.redo(e) && e.tracks.size() == 1 && u.redo(e) && e.tracks.size() == 2 && !u.redo(e), "redo restores edits");
  u.undo(e); u.checkpoint(e); add_track(e, "C");
  CHECK(!u.can_redo(), "a new edit clears the redo stack");
  Session edits; add_track(edits, "Edit track"); add_clip(edits, 0, "edit.wav", 100);
  UndoStack edit_undo;
  edit_undo.checkpoint(edits); CHECK(trim_clip(edits, 0, 0, 20, 100).ok, "trim clip to finite source range");
  edit_undo.checkpoint(edits); CHECK(set_clip_fades(edits, 0, 0, 10, 20).ok, "set clip fades");
  edit_undo.checkpoint(edits); CHECK(move_clip(edits, 0, 0, 300).ok, "move clip on timeline");
  edit_undo.checkpoint(edits); CHECK(split_clip(edits, 0, 0, 40).ok && edits.tracks[0].clips.size() == 2,
                        "split finite clip into two source-contiguous clips");
  CHECK(edits.tracks[0].clips[0].length_frames == 40 && edits.tracks[0].clips[1].source_offset_frame == 60 &&
        edits.tracks[0].clips[1].start_frame == 340 && edits.tracks[0].clips[1].length_frames == 60,
        "split preserves source offsets, timeline position, and lengths");
  CHECK(edit_undo.undo(edits) && edits.tracks[0].clips.size() == 1 && edits.tracks[0].clips[0].start_frame == 300,
        "undo split restores original clip");
  CHECK(edit_undo.undo(edits) && edits.tracks[0].clips[0].start_frame == 100,
        "undo move restores timeline position");
  CHECK(edit_undo.redo(edits) && edits.tracks[0].clips[0].start_frame == 300,
        "redo restores clip move");
  CHECK(edit_undo.undo(edits) && edits.tracks[0].clips[0].start_frame == 100,
        "undo move after redo");
  CHECK(edit_undo.undo(edits) && edits.tracks[0].clips[0].fade_in_frames == 0 && edits.tracks[0].clips[0].length_frames == 100,
        "undo fade edit");
  CHECK(edit_undo.undo(edits) && edits.tracks[0].clips[0].length_frames == 0 && edits.tracks[0].clips[0].source_offset_frame == 0,
        "undo trim edit");
  CHECK(edit_undo.redo(edits) && edits.tracks[0].clips[0].length_frames == 100, "redo trim edit");
  CHECK(edit_undo.redo(edits) && edits.tracks[0].clips[0].fade_in_frames == 10, "redo fade edit");
  CHECK(edit_undo.redo(edits) && edits.tracks[0].clips[0].start_frame == 300, "redo move edit");
  auto edit_roundtrip = parse(serialize(edits));
  CHECK(edit_roundtrip && edit_roundtrip->tracks[0].clips[0].length_frames == 100 &&
        edit_roundtrip->tracks[0].clips[0].fade_in_frames == 10 && edit_roundtrip->tracks[0].clips[0].fade_out_frames == 20,
        "v2 serialization persists trim and fades");
  CHECK(!set_clip_fades(edits, 0, 0, 90, 20) && !split_clip(edits, 0, 0, 0) && !move_clip(edits, 9, 0, 0),
        "invalid fade/split/track edits rejected");
  auto dir = std::filesystem::temp_directory_path() / "fk_session_test";
  std::filesystem::create_directories(dir);
  audio::write_wav_pcm16(dir / "a.wav", audio::make_tone(0.5, 440, 0.4, 48000, 1));
  audio::write_wav_pcm16(dir / "b.wav", audio::make_tone(0.5, 440, 0.4, 48000, 1));
  Session r; add_track(r, "T1"); add_track(r, "T2"); add_track(r, "Muted");
  add_clip(r, 0, "a.wav", 0); add_clip(r, 1, "b.wav", 12000); add_clip(r, 2, "a.wav", 0);
  set_gain(r, 1, -6); set_mute(r, 2, true);
  auto out = render(r, dir);
  CHECK(out && out->frames() == 12000 + 24000 && out->sample_rate == 48000, "render length = latest clip end; muted track ignored");
  CHECK(out && std::abs(out->samples[100]) > 0.01f && std::abs(out->samples[100] - audio::read_wav(dir / "a.wav").samples[100]) < 1e-4, "render sums unmuted clips");
  Session fx; add_track(fx, "Trim and fade"); add_clip(fx, 0, "a.wav", 0); trim_clip(fx, 0, 0, 100, 1000);
  set_clip_fades(fx, 0, 0, 100, 100);
  auto faded = render(fx, dir);
  CHECK(faded && faded->frames() == 1000 && std::abs(faded->samples[0]) < 1e-6f &&
        std::abs(faded->samples.back()) < 1e-6f, "render honors trim length and fades");
  Session mism = r; mism.sample_rate = 44100;
  CHECK(!render(mism, dir), "sample-rate mismatch fails safely");
  Session missing; add_track(missing, "T"); add_clip(missing, 0, "nope.wav", 0);
  CHECK(!render(missing, dir), "missing clip file fails safely");
  CHECK(save(dir / "s.fks", r, false).ok && !save(dir / "s.fks", r, false).ok && save(dir / "s.fks", r, true).ok, "save refuses overwrite unless asked");
  std::error_code link_ec;
  std::filesystem::create_symlink(dir / "s.fks", dir / "linked.fks", link_ec);
  if (!link_ec) {
    CHECK(!save(dir / "linked.fks", r, true).ok, "session save refuses symbolic-link outputs");
    CHECK(!load(dir / "linked.fks"), "session load refuses symbolic-link inputs");
  }
  auto back = load(dir / "s.fks");
  CHECK(back && back->tracks.size() == 3, "load from disk");
  std::filesystem::remove_all(dir);
}
int main() {
  test_hash(); test_time(); test_watermark(); test_clipboard(); test_forensics(); test_session();
  std::cout << "\n" << (g_run - g_fail) << "/" << g_run << " checks passed\n";
  return g_fail == 0 ? 0 : 1;
}
