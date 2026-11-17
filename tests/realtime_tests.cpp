#include "audio_engine.hpp"
#include "midi.hpp"
#include "plugin_host.hpp"
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <vector>
static int checks = 0, failures = 0;
#define CHECK(expr, label) do { ++checks; if (!(expr)) { ++failures; std::cerr << "FAIL: " << label << "\n"; } } while (false)
class GainPlugin final : public hush::plugins::AudioPlugin {
 public:
  const char* id() const noexcept override { return "test.gain"; }
  fk::Status prepare(const hush::plugins::ProcessSpec& s) override { prepared = s.sample_rate == 48000 && s.channels == 2; return prepared ? fk::Status::success() : fk::Status::fail("bad spec"); }
  void process(float* x, std::size_t frames, std::uint16_t channels) noexcept override { for (std::size_t i = 0; i < frames * channels; ++i) x[i] *= 0.5f; }
  void reset() noexcept override {}
  bool prepared{};
};
int main() {
  using namespace fk;
  const auto dir = std::filesystem::temp_directory_path() / "hush_realtime_test";
  std::filesystem::create_directories(dir);
  audio::write_wav_pcm16(dir / "tone.wav", audio::make_tone(0.1, 440, 0.5, 48000, 1));
  engine::Session s; engine::add_track(s, "Tone"); engine::add_clip(s, 0, "tone.wav", 0);
  hush::rt::AudioEngine engine;
  CHECK(engine.load(s, dir).ok, "engine preloads session before playback");
  CHECK(engine.sample_rate() == 48000 && engine.seek(480).ok, "engine reports rate and permits stopped seek");
  CHECK(!engine.seek(100000).ok, "engine rejects seek beyond session");
  engine.seek(0); engine.play(); std::vector<float> block(256 * 2, -9.0f);
  CHECK(engine.process(block.data(), 256, 2) == 256, "engine callback renders requested frames");
  CHECK(std::abs(block[0]) < 1e-6f && std::abs(block[100]) > 0.01f, "engine callback produces fade-free audio except initial sine zero");
  CHECK(engine.playhead() == 256, "engine advances transport");
  engine.pause(); std::fill(block.begin(), block.end(), 1.0f);
  CHECK(engine.process(block.data(), 256, 2) == 0 && block[0] == 0.0f, "paused callback emits silence");
  engine.set_master_gain_db(-6); engine.play();
  CHECK(engine.process(block.data(), 256, 2) == 256, "engine resumes playback");
  engine.stop(); CHECK(engine.playhead() == 0 && !engine.playing(), "stop resets transport");
  hush::midi::SpscQueue<8> q;
  hush::midi::Event ev{123, 0x90, 60, 100}, got{};
  CHECK(hush::midi::validate_event(ev).ok && q.push(ev) && q.pop(got) && got.frame == 123 && got.data1 == 60,
        "MIDI event validates and passes through bounded SPSC queue");
  CHECK(!hush::midi::validate_event({0, 0xF0, 0, 0}).ok && !hush::midi::validate_event({0, 0x90, 128, 1}).ok,
        "invalid system/data MIDI events rejected");
  hush::plugins::PluginChain chain; auto plugin = std::make_shared<GainPlugin>();
  CHECK(chain.add(plugin, {48000, 256, 2}).ok && plugin->prepared && chain.size() == 1, "plugin chain prepares DSP plugin");
  std::vector<float> samples(32, 1.0f); chain.process(samples.data(), 16, 2);
  CHECK(std::abs(samples[0] - 0.5f) < 1e-6f, "plugin chain processes interleaved audio");
  CHECK(!chain.add({}, {48000, 256, 2}).ok, "plugin chain rejects null plugin");
  std::filesystem::remove_all(dir);
  std::cout << checks - failures << "/" << checks << " checks passed\n";
  return failures ? 1 : 0;
}
