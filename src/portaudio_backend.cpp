#include "portaudio_backend.hpp"
#include <algorithm>
#include <cstring>
#include <limits>
#include <memory>
#include <atomic>
#include <filesystem>

#if defined(HUSH_HAS_PORTAUDIO)
// Minimal declarations of the stable PortAudio C ABI. No PortAudio headers are copied
// into this project; the system-provided library is an optional build dependency.
extern "C" {
typedef int PaError; typedef void PaStream; typedef double PaTime;
typedef unsigned long PaSampleFormat; typedef unsigned long PaStreamFlags; typedef unsigned long PaStreamCallbackFlags;
typedef struct PaStreamParameters { int device; int channelCount; PaSampleFormat sampleFormat; PaTime suggestedLatency; void* hostApiSpecificStreamInfo; } PaStreamParameters;
typedef struct PaDeviceInfo { int structVersion; const char* name; int hostApi; int maxInputChannels; int maxOutputChannels; PaTime defaultLowInputLatency; PaTime defaultLowOutputLatency; PaTime defaultHighInputLatency; PaTime defaultHighOutputLatency; double defaultSampleRate; } PaDeviceInfo;
typedef struct PaStreamCallbackTimeInfo { PaTime inputBufferAdcTime; PaTime currentTime; PaTime outputBufferDacTime; } PaStreamCallbackTimeInfo;
typedef int (*PaStreamCallback)(const void*, void*, unsigned long, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void*);
PaError Pa_Initialize(void); PaError Pa_Terminate(void); PaError Pa_GetDeviceCount(void); int Pa_GetDefaultOutputDevice(void); int Pa_GetDefaultInputDevice(void);
const PaDeviceInfo* Pa_GetDeviceInfo(int); PaError Pa_OpenStream(PaStream**, const PaStreamParameters*, const PaStreamParameters*, double, unsigned long, PaStreamFlags, PaStreamCallback, void*);
PaError Pa_StartStream(PaStream*); PaError Pa_StopStream(PaStream*); PaError Pa_CloseStream(PaStream*); const char* Pa_GetErrorText(PaError);
}
namespace { constexpr PaSampleFormat paFloat32 = 0x00000001UL; constexpr int paContinue = 0; constexpr int paAbort = 2; }
#endif

namespace hush::rt {
struct PortAudioBackend::Impl {
  Callback callback{}; void* context{}; bool initialized{}; std::uint16_t channels{};
  std::vector<float> capture;
  std::atomic<std::size_t> captured_frames{0};
  std::atomic<bool> recording{false};
  std::uint32_t recording_rate{48000};
#if defined(HUSH_HAS_PORTAUDIO)
  PaStream* stream{};
#endif
};
PortAudioBackend::PortAudioBackend() : impl_(std::make_unique<Impl>()) {}
PortAudioBackend::~PortAudioBackend() { stop(); }
bool PortAudioBackend::available() noexcept {
#if defined(HUSH_HAS_PORTAUDIO)
  return true;
#else
  return false;
#endif
}
fk::Result<std::vector<OutputDeviceInfo>> PortAudioBackend::enumerate_outputs() {
#if defined(HUSH_HAS_PORTAUDIO)
  if (Pa_Initialize() != 0) return fk::Result<std::vector<OutputDeviceInfo>>::fail("PortAudio initialization failed");
  const int count = Pa_GetDeviceCount();
  std::vector<OutputDeviceInfo> out;
  if (count < 0) { Pa_Terminate(); return fk::Result<std::vector<OutputDeviceInfo>>::fail("PortAudio could not enumerate devices"); }
  for (int i = 0; i < count; ++i) {
    const auto* d = Pa_GetDeviceInfo(i);
    if (d && d->maxOutputChannels > 0) out.push_back({i, d->name ? d->name : "(unnamed device)", d->maxOutputChannels, d->defaultSampleRate});
  }
  Pa_Terminate(); return fk::Result<std::vector<OutputDeviceInfo>>::success(std::move(out));
#else
  return fk::Result<std::vector<OutputDeviceInfo>>::fail("PortAudio backend not built; install/approve PortAudio and rebuild with HUSH_ENABLE_PORTAUDIO=ON");
#endif
}
fk::Status PortAudioBackend::start(std::uint32_t rate, std::uint16_t channels, std::size_t block,
                                   Callback callback, void* context) {
#if defined(HUSH_HAS_PORTAUDIO)
  if (impl_->stream) return fk::Status::fail("PortAudio stream already started");
  if (!callback || (channels != 1 && channels != 2) || rate < 8000 || rate > 384000 || block == 0 || block > 8192)
    return fk::Status::fail("invalid PortAudio stream configuration");
  if (Pa_Initialize() != 0) return fk::Status::fail("PortAudio initialization failed");
  impl_->initialized = true; const int device = Pa_GetDefaultOutputDevice();
  if (device < 0) { stop(); return fk::Status::fail("no default output device; choose/configure a device in the operating system"); }
  const auto* info = Pa_GetDeviceInfo(device);
  if (!info || info->maxOutputChannels < channels) { stop(); return fk::Status::fail("default output device does not support requested channels"); }
  PaStreamParameters output{device, static_cast<int>(channels), paFloat32, info->defaultLowOutputLatency, nullptr};
  impl_->callback = callback; impl_->context = context; impl_->channels = channels;
  auto trampoline = [](const void*, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    if (!self || !self->callback || !out) return paAbort;
    self->callback(self->context, static_cast<float*>(out), static_cast<std::size_t>(frames), self->channels);
    return paContinue;
  };
  const auto err = Pa_OpenStream(&impl_->stream, nullptr, &output, static_cast<double>(rate), static_cast<unsigned long>(block), 0, trampoline, impl_.get());
  if (err != 0) { impl_->stream = nullptr; stop(); return fk::Status::fail(std::string("PortAudio open failed: ") + Pa_GetErrorText(err)); }
  const auto started = Pa_StartStream(impl_->stream);
  if (started != 0) { const std::string msg = std::string("PortAudio start failed: ") + Pa_GetErrorText(started); stop(); return fk::Status::fail(msg); }
  return fk::Status::success();
#else
  (void)rate; (void)channels; (void)block; (void)callback; (void)context;
  return fk::Status::fail("PortAudio backend not built; configure with HUSH_ENABLE_PORTAUDIO=ON");
#endif
}
bool PortAudioBackend::recording() const noexcept { return impl_ && impl_->recording.load(std::memory_order_acquire); }
std::size_t PortAudioBackend::captured_frames() const noexcept { return impl_ ? impl_->captured_frames.load(std::memory_order_acquire) : 0; }
fk::Status PortAudioBackend::start_recording(std::uint32_t rate, std::uint16_t channels, std::size_t block,
    Callback callback, void* context, std::uint32_t max_seconds) {
#if defined(HUSH_HAS_PORTAUDIO)
  if (impl_->stream) return fk::Status::fail("PortAudio stream already started");
  if (!callback || (channels != 1 && channels != 2) || rate < 8000 || rate > 192000 || block == 0 || block > 8192 || max_seconds == 0 || max_seconds > 600)
    return fk::Status::fail("invalid recording configuration (maximum capture duration is 600 seconds)");
  if (Pa_Initialize() != 0) return fk::Status::fail("PortAudio initialization failed");
  impl_->initialized = true;
  const int in_device = Pa_GetDefaultInputDevice();
  const int out_device = Pa_GetDefaultOutputDevice();
  if (in_device < 0 || out_device < 0) { stop(); return fk::Status::fail("recording requires default input and output devices"); }
  const auto* in_info = Pa_GetDeviceInfo(in_device); const auto* out_info = Pa_GetDeviceInfo(out_device);
  if (!in_info || !out_info || in_info->maxInputChannels < channels || out_info->maxOutputChannels < channels) { stop(); return fk::Status::fail("default input/output devices do not support requested channel count"); }
  try { impl_->capture.assign(static_cast<std::size_t>(rate) * channels * max_seconds, 0.0f); }
  catch (...) { stop(); return fk::Status::fail("not enough memory to allocate bounded recording buffer"); }
  impl_->recording_rate = rate;
  impl_->captured_frames.store(0, std::memory_order_release);
  impl_->callback = callback; impl_->context = context; impl_->channels = channels;
  PaStreamParameters input{in_device, static_cast<int>(channels), paFloat32, in_info->defaultLowInputLatency, nullptr};
  PaStreamParameters output{out_device, static_cast<int>(channels), paFloat32, out_info->defaultLowOutputLatency, nullptr};
  auto trampoline = [](const void* in, void* out, unsigned long frames, const PaStreamCallbackTimeInfo*, PaStreamCallbackFlags, void* user) -> int {
    auto* self = static_cast<Impl*>(user);
    if (!self || !self->callback || !out || !in) return paAbort;
    const auto captured = self->captured_frames.load(std::memory_order_relaxed);
    const auto capacity = self->capture.size() / self->channels;
    const auto count = std::min<std::size_t>(frames, capacity > captured ? capacity - captured : 0);
    if (count) std::memcpy(self->capture.data() + captured * self->channels, in, count * self->channels * sizeof(float));
    self->captured_frames.store(captured + count, std::memory_order_release);
    self->callback(self->context, static_cast<float*>(out), static_cast<std::size_t>(frames), self->channels);
    return count < frames ? paAbort : paContinue;
  };
  const auto err = Pa_OpenStream(&impl_->stream, &input, &output, static_cast<double>(rate), static_cast<unsigned long>(block), 0, trampoline, impl_.get());
  if (err != 0) { impl_->stream = nullptr; stop(); return fk::Status::fail(std::string("PortAudio duplex open failed: ") + Pa_GetErrorText(err)); }
  const auto started = Pa_StartStream(impl_->stream);
  if (started != 0) { const std::string msg = std::string("PortAudio recording start failed: ") + Pa_GetErrorText(started); stop(); return fk::Status::fail(msg); }
  impl_->recording.store(true, std::memory_order_release);
  return fk::Status::success();
#else
  (void)rate; (void)channels; (void)block; (void)callback; (void)context; (void)max_seconds;
  return fk::Status::fail("PortAudio backend not built; configure with HUSH_ENABLE_PORTAUDIO=ON");
#endif
}
fk::Result<std::size_t> PortAudioBackend::stop_recording(const std::filesystem::path& destination, bool overwrite) {
  if (!impl_) return fk::Result<std::size_t>::fail("recording backend is unavailable");
  stop();
  const auto frames = impl_->captured_frames.load(std::memory_order_acquire);
  if (frames == 0) return fk::Result<std::size_t>::fail("no audio frames were captured");
  fk::audio::AudioBuffer audio; audio.sample_rate = 48000; audio.channels = impl_->channels;
  // Capture is normalized to the rate used to start the device by storing it below.
  audio.sample_rate = impl_->recording_rate;
  audio.samples.assign(impl_->capture.begin(), impl_->capture.begin() + static_cast<std::ptrdiff_t>(frames * impl_->channels));
  try { fk::audio::write_wav_pcm16(destination, audio, overwrite); }
  catch (const std::exception& e) { return fk::Result<std::size_t>::fail(std::string("recording write failed: ") + e.what()); }
  impl_->capture.clear(); impl_->recording.store(false, std::memory_order_release);
  return fk::Result<std::size_t>::success(frames);
}

void PortAudioBackend::stop() noexcept {
#if defined(HUSH_HAS_PORTAUDIO)
  if (impl_ && impl_->stream) { Pa_StopStream(impl_->stream); Pa_CloseStream(impl_->stream); impl_->stream = nullptr; }
  if (impl_ && impl_->initialized) { Pa_Terminate(); impl_->initialized = false; }
  if (impl_) impl_->recording.store(false, std::memory_order_release);
#endif
}
} // namespace hush::rt
