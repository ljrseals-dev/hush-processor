#include "audio.hpp"

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
int failures = 0;
void check(bool ok, const std::string& what) {
  if (!ok) { ++failures; std::cerr << "FAIL: " << what << '\n'; }
}
void put16(std::ostream& out, std::uint16_t v) {
  out.put(static_cast<char>(v & 0xffU));
  out.put(static_cast<char>((v >> 8) & 0xffU));
}
void put32(std::ostream& out, std::uint32_t v) {
  for (unsigned i = 0; i < 4; ++i) out.put(static_cast<char>((v >> (8U * i)) & 0xffU));
}
void write_pcm24_test_file(const std::filesystem::path& path) {
  std::ofstream out(path, std::ios::binary | std::ios::trunc);
  // Three mono frames: maximum positive, minimum negative, and -1 LSB.
  out.write("RIFF", 4); put32(out, 46); out.write("WAVE", 4);
  out.write("fmt ", 4); put32(out, 16); put16(out, 1); put16(out, 1);
  put32(out, 48000); put32(out, 144000); put16(out, 3); put16(out, 24);
  out.write("data", 4); put32(out, 9);
  const unsigned char samples[] = {0xff, 0xff, 0x7f, 0x00, 0x00, 0x80, 0xff, 0xff, 0xff};
  out.write(reinterpret_cast<const char*>(samples), sizeof(samples));
  out.put('\0'); // RIFF pad byte for the odd-sized data chunk.
}
}

int main() {
  using namespace fk::audio;
  const auto t = make_tone(.25, 1000, .5, 48000, 2);
  check(t.frames() == 12000, "tone frame count");
  const auto stats = analyze(t);
  check(stats.peak > .49 && stats.peak < .501, "tone peak");
  check(stats.rms > .34 && stats.rms < .36, "tone RMS");
  const auto q = apply_gain(t, -6);
  check(analyze(q).peak < analyze(t).peak * .51, "gain reduction");
  const auto m = mix({t, t}, {0, -6});
  check(m.frames() == t.frames(), "mix frame count");

  const auto temp = std::filesystem::temp_directory_path();
  const auto roundtrip = temp / "hush_test_roundtrip.wav";
  write_wav_pcm16(roundtrip, t);
  const auto restored = read_wav(roundtrip);
  check(restored.sample_rate == 48000 && restored.channels == 2 && restored.frames() == 12000,
        "PCM16 roundtrip metadata");
  check(analyze(restored).peak > .48, "PCM16 roundtrip signal");
  bool refused_overwrite = false;
  try { write_wav_pcm16(roundtrip, t, false); } catch (const std::exception&) { refused_overwrite = true; }
  check(refused_overwrite, "WAV writer enforces explicit no-overwrite mode");
  const auto linked = temp / "hush_test_linked.wav";
  std::error_code link_error; std::filesystem::create_symlink(roundtrip, linked, link_error);
  if (!link_error) {
    bool refused_link = false;
    try { write_wav_pcm16(linked, t, true); } catch (const std::exception&) { refused_link = true; }
    check(refused_link, "WAV writer refuses symlink targets");
    std::filesystem::remove(linked);
  }
  std::filesystem::remove(roundtrip);

  const auto pcm24 = temp / "hush_test_pcm24.wav";
  write_pcm24_test_file(pcm24);
  const auto decoded = read_wav(pcm24);
  check(decoded.frames() == 3 && decoded.channels == 1, "PCM24 frame metadata");
  check(decoded.samples[0] > .99999f && decoded.samples[0] <= 1.0f, "PCM24 positive full scale");
  check(decoded.samples[1] == -1.0f, "PCM24 negative full scale");
  check(decoded.samples[2] < 0.0f && decoded.samples[2] > -0.000001f, "PCM24 negative one LSB");
  std::filesystem::remove(pcm24);

  if (failures == 0) {
    std::cout << "PASS: tone, peak/RMS, gain, mix, atomic PCM16 output, symlink/overwrite guards, and signed PCM24 decoding\n";
    return 0;
  }
  std::cerr << failures << " audio test(s) failed\n";
  return 1;
}
