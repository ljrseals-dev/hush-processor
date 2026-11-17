#include "audio_engine.hpp"
#include <algorithm>
#include <cmath>
#include <limits>

namespace hush::rt {
fk::Status AudioEngine::load(const fk::engine::Session& s, const std::filesystem::path& base) {
  if (playing()) return fk::Status::fail("pause playback before replacing the session");
  if (s.sample_rate < 8000 || s.sample_rate > 768000) return fk::Status::fail("invalid session sample rate");
  std::vector<TrackData> prepared;
  std::uint64_t end = 0;
  try {
    prepared.reserve(s.tracks.size());
    for (const auto& track : s.tracks) {
      TrackData td; td.gain->store(static_cast<float>(std::pow(10.0, track.gain_db / 20.0))); td.mute->store(track.mute);
      td.clips.reserve(track.clips.size());
      for (const auto& c : track.clips) {
        std::filesystem::path p(c.path); if (p.is_relative()) p = base / p;
        auto wav = fk::audio::read_wav(p);
        if (wav.sample_rate != s.sample_rate) return fk::Status::fail("clip sample rate differs from session: " + c.path);
        if (c.source_offset_frame > wav.frames()) return fk::Status::fail("clip source offset is beyond source: " + c.path);
        const auto available = static_cast<std::uint64_t>(wav.frames()) - c.source_offset_frame;
        const auto length = c.length_frames == 0 ? available : c.length_frames;
        if (length == 0 || length > available) return fk::Status::fail("clip range exceeds source: " + c.path);
        if (c.fade_in_frames > length || c.fade_out_frames > length || c.fade_in_frames > length - c.fade_out_frames)
          return fk::Status::fail("clip fades exceed clip range: " + c.path);
        if (c.start_frame > std::numeric_limits<std::uint64_t>::max() - length)
          return fk::Status::fail("clip timeline end overflow: " + c.path);
        ClipData cd; cd.start = c.start_frame; cd.source = c.source_offset_frame; cd.length = length;
        cd.fade_in = c.fade_in_frames; cd.fade_out = c.fade_out_frames; cd.channels = wav.channels;
        cd.samples = std::move(wav.samples);
        end = std::max(end, cd.start + cd.length); td.clips.push_back(std::move(cd));
      }
      prepared.push_back(std::move(td));
    }
  } catch (const std::exception& e) { return fk::Status::fail(std::string("session preload failed: ") + e.what()); }
  tracks_.swap(prepared); sample_rate_ = s.sample_rate; total_frames_ = end; playhead_.store(0); return fk::Status::success();
}
void AudioEngine::stop() noexcept { playing_.store(false, std::memory_order_release); playhead_.store(0, std::memory_order_release); }
fk::Status AudioEngine::seek(std::uint64_t frame) noexcept {
  if (playing()) return fk::Status::fail("pause before seeking");
  if (frame > total_frames_) return fk::Status::fail("seek position exceeds session duration");
  playhead_.store(frame, std::memory_order_release); return fk::Status::success();
}
void AudioEngine::set_master_gain_db(float db) noexcept {
  if (!std::isfinite(db)) return;
  db = std::clamp(db, -120.0f, 12.0f); master_gain_.store(std::pow(10.0f, db / 20.0f), std::memory_order_release);
}
fk::Status AudioEngine::set_track_gain_db(std::size_t track, float db) noexcept {
  if (track >= tracks_.size()) return fk::Status::fail("no such track");
  if (!std::isfinite(db) || db < -120.0f || db > 36.0f) return fk::Status::fail("track gain must be -120..+36 dB");
  tracks_[track].gain->store(std::pow(10.0f, db / 20.0f), std::memory_order_release); return fk::Status::success();
}
fk::Status AudioEngine::set_track_mute(std::size_t track, bool mute) noexcept {
  if (track >= tracks_.size()) return fk::Status::fail("no such track");
  tracks_[track].mute->store(mute, std::memory_order_release); return fk::Status::success();
}
std::size_t AudioEngine::process(float* out, std::size_t frames, std::uint16_t channels) noexcept {
  if (!out || (channels != 1 && channels != 2) || frames > std::numeric_limits<std::size_t>::max() / channels) return 0;
  std::fill_n(out, frames * channels, 0.0f);
  output_peak_.store(0.0f, std::memory_order_relaxed);
  if (!playing()) return 0;
  const auto pos = playhead_.load(std::memory_order_relaxed);
  if (pos >= total_frames_) { playing_.store(false, std::memory_order_release); return 0; }
  const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(frames, total_frames_ - pos));
  const float master = master_gain_.load(std::memory_order_acquire);
  for (const auto& track : tracks_) {
    if (track.mute->load(std::memory_order_acquire)) continue;
    const float gain = track.gain->load(std::memory_order_acquire) * master;
    for (const auto& clip : track.clips) {
      const std::uint64_t clip_end = clip.start + clip.length;
      const std::uint64_t block_end = pos + count;
      const auto begin = std::max(pos, clip.start), finish = std::min(block_end, clip_end);
      if (begin >= finish) continue;
      for (std::uint64_t timeline = begin; timeline < finish; ++timeline) {
        const std::uint64_t local = timeline - clip.start;
        float envelope = 1.0f;
        if (clip.fade_in && local < clip.fade_in) envelope *= static_cast<float>(local) / static_cast<float>(clip.fade_in);
        if (clip.fade_out && local >= clip.length - clip.fade_out)
          envelope *= static_cast<float>(clip.length - 1 - local) / static_cast<float>(clip.fade_out);
        const auto src_frame = clip.source + local;
        const auto out_frame = static_cast<std::size_t>(timeline - pos);
        if (channels == 1) {
          float v = 0.0f;
          for (std::uint16_t sc = 0; sc < clip.channels; ++sc) v += clip.samples[static_cast<std::size_t>(src_frame) * clip.channels + sc];
          out[out_frame] += v * (1.0f / static_cast<float>(clip.channels)) * gain * envelope;
        } else if (clip.channels == 1) {
          const float v = clip.samples[static_cast<std::size_t>(src_frame)] * gain * envelope;
          out[out_frame * 2] += v; out[out_frame * 2 + 1] += v;
        } else {
          float left = clip.samples[static_cast<std::size_t>(src_frame) * clip.channels];
          float right = clip.samples[static_cast<std::size_t>(src_frame) * clip.channels + 1];
          if (clip.channels > 2) {
            for (std::uint16_t sc = 2; sc < clip.channels; ++sc) {
              const float v = clip.samples[static_cast<std::size_t>(src_frame) * clip.channels + sc] * 0.5f;
              if ((sc & 1U) == 0) left += v; else right += v;
            }
          }
          out[out_frame * 2] += left * gain * envelope; out[out_frame * 2 + 1] += right * gain * envelope;
        }
      }
    }
  }
  float peak = 0.0f;
  for (std::size_t i = 0; i < count * channels; ++i) peak = std::max(peak, std::abs(out[i]));
  output_peak_.store(peak, std::memory_order_relaxed);
  playhead_.store(pos + count, std::memory_order_release);
  if (pos + count >= total_frames_) playing_.store(false, std::memory_order_release);
  return count;
}
} // namespace hush::rt
