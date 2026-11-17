#include "audio_engine.hpp"
#include "portaudio_backend.hpp"
#include "session.hpp"
#include "audio.hpp"
#include <X11/Xlib.h>
#include <X11/keysym.h>
#include <algorithm>
#include <cmath>
#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>
#include <unistd.h>
#include <chrono>
#include <iomanip>
#include <sstream>

namespace {
struct WaveView { std::vector<float> peaks; std::uint64_t frames{}; };
std::size_t audio_callback(void* context, float* output, std::size_t frames, std::uint16_t channels) noexcept {
  return static_cast<hush::rt::AudioEngine*>(context)->process(output, frames, channels);
}
unsigned long color(Display* d, const char* hex) {
  XColor c{}; Colormap cmap = DefaultColormap(d, DefaultScreen(d));
  if (!XParseColor(d, cmap, hex, &c) || !XAllocColor(d, cmap, &c)) return BlackPixel(d, DefaultScreen(d));
  return c.pixel;
}
void text(Display* d, Window w, GC gc, int x, int y, const std::string& s) { XDrawString(d, w, gc, x, y, s.c_str(), static_cast<int>(s.size())); }
void rect(Display* d, Window w, GC gc, int x, int y, int ww, int hh, bool fill = true) {
  if (fill) XFillRectangle(d, w, gc, x, y, static_cast<unsigned>(std::max(0, ww)), static_cast<unsigned>(std::max(0, hh)));
  else XDrawRectangle(d, w, gc, x, y, static_cast<unsigned>(std::max(0, ww)), static_cast<unsigned>(std::max(0, hh)));
}
}
int main(int argc, char** argv) {
  if (argc != 2) { std::cerr << "Usage: hush_gui <session.fks>\n"; return 2; }
  const std::filesystem::path session_path = argv[1];
  auto loaded = fk::engine::load(session_path);
  if (!loaded) { std::cerr << "HUSH GUI: " << loaded.error << "\n"; return 1; }
  auto session = *loaded;
  fk::engine::UndoStack undo_stack;
  hush::rt::AudioEngine engine;
  auto status = engine.load(session, session_path.parent_path().empty() ? std::filesystem::current_path() : session_path.parent_path());
  if (!status) { std::cerr << "HUSH audio preload: " << status.error << "\n"; return 1; }
  hush::rt::PortAudioBackend device;
  const auto base = session_path.parent_path().empty() ? std::filesystem::current_path() : session_path.parent_path();
  bool device_started = false;
  bool recording = false;
  auto new_take_path = [&] {
    const auto stamp = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    return base / ("HUSH-Take-" + std::to_string(stamp) + ".wav");
  };
  std::filesystem::path active_take;
  std::vector<std::vector<WaveView>> waves(session.tracks.size());
  for (std::size_t ti = 0; ti < session.tracks.size(); ++ti) {
    for (const auto& clip : session.tracks[ti].clips) {
      WaveView w;
      try {
        auto p = std::filesystem::path(clip.path); if (p.is_relative()) p = base / p;
        auto a = fk::audio::read_wav(p); w.frames = clip.length_frames ? std::min<std::uint64_t>(clip.length_frames, a.frames() - std::min<std::uint64_t>(clip.source_offset_frame, a.frames())) : a.frames() - std::min<std::uint64_t>(clip.source_offset_frame, a.frames());
        const auto count = static_cast<std::size_t>(std::min<std::uint64_t>(w.frames, 1000000));
        constexpr std::size_t bins = 256; w.peaks.resize(bins, 0.0f);
        for (std::size_t i = 0; i < count; ++i) {
          const auto source_frame = static_cast<std::size_t>(clip.source_offset_frame) + i;
          float peak = 0.0f;
          for (std::uint16_t ch = 0; ch < a.channels; ++ch) peak = std::max(peak, std::abs(a.samples[source_frame * a.channels + ch]));
          const auto bin = std::min(bins - 1, i * bins / std::max<std::size_t>(count, 1)); w.peaks[bin] = std::max(w.peaks[bin], peak);
        }
      } catch (...) { w.peaks.clear(); }
      waves[ti].push_back(std::move(w));
    }
  }
  Display* display = XOpenDisplay(nullptr);
  if (!display) { std::cerr << "HUSH GUI: no X11 display. Use a desktop session or build the GUI on a supported display host.\n"; return 1; }
  const int screen = DefaultScreen(display); const unsigned long black = BlackPixel(display, screen), white = WhitePixel(display, screen);
  const auto bg = color(display, "#11141a"), panel = color(display, "#1c222c"), grid = color(display, "#333c49"), teal = color(display, "#5eead4"), orange = color(display, "#f5b85b"), red = color(display, "#ff647c"), muted = color(display, "#9aa8b8"), clipfill = color(display, "#234f59");
  Window win = XCreateSimpleWindow(display, RootWindow(display, screen), 40, 40, 1100, 720, 1, grid, bg);
  XStoreName(display, win, "HUSH — Private Digital Audio Workstation");
  XSelectInput(display, win, ExposureMask | KeyPressMask | ButtonPressMask | StructureNotifyMask);
  GC gc = XCreateGC(display, win, 0, nullptr); XMapWindow(display, win);
  bool running = true; int width = 1100, height = 720; std::string message = "Offline session loaded. PLAY opens the default PortAudio output. Ctrl+S save, Ctrl+Z undo, Ctrl+Y redo.";
  int selected_track = 0; float selected_gain = 0.0f; const int top = 86, track_h = 82, timeline_x = 205, timeline_w = 850;
  auto draw = [&] {
    XSetForeground(display, gc, bg); rect(display, win, gc, 0, 0, width, height);
    XSetForeground(display, gc, white); text(display, win, gc, 24, 32, "H U S H");
    XSetForeground(display, gc, muted); text(display, win, gc, 160, 32, "PRIVATE DIGITAL AUDIO WORKSTATION");
    XSetForeground(display, gc, panel); rect(display, win, gc, 16, 46, width - 32, 52);
    XSetForeground(display, gc, teal); rect(display, win, gc, 28, 57, 88, 30); XSetForeground(display, gc, black); text(display, win, gc, 51, 77, engine.playing() ? "PAUSE" : "PLAY");
    XSetForeground(display, gc, orange); rect(display, win, gc, 124, 57, 74, 30); XSetForeground(display, gc, black); text(display, win, gc, 144, 77, "STOP");
    XSetForeground(display, gc, recording ? red : 0xff647c); rect(display, win, gc, 204, 57, 82, 30); XSetForeground(display, gc, black); text(display, win, gc, recording ? 214 : 220, 77, recording ? "STOP REC" : "RECORD");
    XSetForeground(display, gc, white); const auto pos = engine.playhead(); text(display, win, gc, 296, 77, "FRAME " + std::to_string(pos) + "   /   " + std::to_string(session.sample_rate) + " Hz");
    const float peak = engine.output_peak(); XSetForeground(display, gc, panel); rect(display, win, gc, width - 225, 57, 190, 30);
    XSetForeground(display, gc, peak > 0.98f ? red : teal); rect(display, win, gc, width - 220, 62, static_cast<int>(180 * std::clamp(peak, 0.0f, 1.0f)), 20);
    XSetForeground(display, gc, white); text(display, win, gc, width - 225, 45, "OUTPUT PEAK");
    XSetForeground(display, gc, muted); text(display, win, gc, 24, top - 14, "TRACK / MIXER"); text(display, win, gc, timeline_x, top - 14, "ARRANGEMENT — seconds (horizontal scroll not yet implemented)");
    const double seconds_visible = 20.0; const double px_per_sec = static_cast<double>(timeline_w) / seconds_visible;
    for (int sec = 0; sec <= 20; sec += 2) { int x = timeline_x + static_cast<int>(sec * px_per_sec); XSetForeground(display, gc, grid); XDrawLine(display, win, gc, x, top, x, height - 100); XSetForeground(display, gc, muted); text(display, win, gc, x + 2, top - 2, std::to_string(sec) + "s"); }
    for (std::size_t ti = 0; ti < session.tracks.size(); ++ti) {
      const int y = top + static_cast<int>(ti) * track_h; if (y > height - 105) break;
      XSetForeground(display, gc, panel); rect(display, win, gc, 16, y, width - 32, track_h - 3);
      XSetForeground(display, gc, ti == static_cast<std::size_t>(selected_track) ? teal : white); text(display, win, gc, 28, y + 23, session.tracks[ti].name.substr(0, 24));
      XSetForeground(display, gc, muted); text(display, win, gc, 28, y + 43, "GAIN " + std::to_string(static_cast<int>(session.tracks[ti].gain_db)) + " dB");
      XSetForeground(display, gc, session.tracks[ti].mute ? red : teal); rect(display, win, gc, 28, y + 51, 48, 18); XSetForeground(display, gc, black); text(display, win, gc, 39, y + 64, session.tracks[ti].mute ? "MUTE" : "LIVE");
      for (std::size_t ci = 0; ci < session.tracks[ti].clips.size(); ++ci) {
        const auto& c = session.tracks[ti].clips[ci]; const double start_s = static_cast<double>(c.start_frame) / session.sample_rate;
        double length_s = static_cast<double>(waves[ti][ci].frames) / session.sample_rate;
        const int x = timeline_x + static_cast<int>(start_s * px_per_sec); const int ww = std::max(3, static_cast<int>(length_s * px_per_sec));
        if (x > timeline_x + timeline_w || x + ww < timeline_x) continue;
        XSetForeground(display, gc, clipfill); rect(display, win, gc, x, y + 10, ww, 54);
        XSetForeground(display, gc, teal); rect(display, win, gc, x, y + 10, ww, 54, false);
        if (!waves[ti][ci].peaks.empty()) {
          XSetForeground(display, gc, teal); const auto& p = waves[ti][ci].peaks;
          const int maxdraw = std::min(ww - 4, 400);
          for (int i = 0; i < maxdraw; ++i) { const auto bi = std::min(p.size() - 1, static_cast<std::size_t>(i) * p.size() / std::max(1, maxdraw)); const int amp = static_cast<int>(p[bi] * 22.0f); XDrawLine(display, win, gc, x + 2 + i, y + 37 - amp, x + 2 + i, y + 37 + amp); }
        }
      }
    }
    const int mixer_y = height - 88; XSetForeground(display, gc, panel); rect(display, win, gc, 16, mixer_y, width - 32, 70);
    XSetForeground(display, gc, white); text(display, win, gc, 28, mixer_y + 20, "MIXER");
    XSetForeground(display, gc, muted); text(display, win, gc, 120, mixer_y + 20, "Select track row, then click the gain strip to change it (playback pauses for safe update)");
    XSetForeground(display, gc, grid); rect(display, win, gc, 120, mixer_y + 32, 360, 14);
    XSetForeground(display, gc, orange); rect(display, win, gc, 120, mixer_y + 32, static_cast<int>(360 * std::clamp((selected_gain + 60.0f) / 96.0f, 0.0f, 1.0f)), 14);
    XSetForeground(display, gc, white); text(display, win, gc, 500, mixer_y + 44, "Selected gain: " + std::to_string(static_cast<int>(selected_gain)) + " dB (-60 to +36)");
    XSetForeground(display, gc, muted); text(display, win, gc, 24, height - 8, message.substr(0, static_cast<std::size_t>(std::max(0, width - 40) / 7)));
    XFlush(display);
  };
  while (running) {
    while (XPending(display)) {
      XEvent e{}; XNextEvent(display, &e);
      if (e.type == Expose) draw();
      else if (e.type == ConfigureNotify) { width = e.xconfigure.width; height = e.xconfigure.height; draw(); }
      else if (e.type == KeyPress) {
        KeySym key = XLookupKeysym(&e.xkey, 0);
        if (key == XK_Escape || key == XK_q) running = false;
        if (key == XK_space) { if (engine.playing()) engine.pause(); else engine.play(); draw(); }
        if (key == XK_s && (e.xkey.state & ControlMask)) {
          engine.pause(); device.stop(); device_started = false;
          auto saved = fk::engine::save(session_path, session, true);
          message = saved ? "Session saved atomically." : "Save failed: " + saved.error; draw();
        } else if (key == XK_s) { engine.stop(); draw(); }
        if ((e.xkey.state & ControlMask) && (key == XK_z || key == XK_y)) {
          engine.pause(); device.stop(); device_started = false;
          const bool changed = key == XK_z ? undo_stack.undo(session) : undo_stack.redo(session);
          if (changed) {
            status = engine.load(session, base);
            message = status ? (key == XK_z ? "Undo applied." : "Redo applied.") : "Reload after undo/redo failed: " + status.error;
          } else message = key == XK_z ? "Nothing to undo." : "Nothing to redo.";
          draw();
        }
      } else if (e.type == ButtonPress) {
        const int x = e.xbutton.x, y = e.xbutton.y;
        if (y >= 57 && y <= 87 && x >= 28 && x <= 116) {
          if (!device_started) {
            status = device.start(session.sample_rate, 2, 256, audio_callback, &engine);
            if (status) { device_started = true; message = "PortAudio output started; playback is local and no network is used."; }
            else message = "Audio device unavailable: " + status.error;
          }
          if (device_started) { if (engine.playing()) engine.pause(); else engine.play(); }
        } else if (y >= 57 && y <= 87 && x >= 124 && x <= 198) {
          if (recording) {
            auto out = active_take;
            auto saved_take = device.stop_recording(out, false); recording = false; device_started = false;
            if (saved_take) message = "Take saved: " + out.filename().string() + " (" + std::to_string(*saved_take.value) + " frames).";
            else message = "Recording stop/save: " + saved_take.error;
          } else engine.stop();
        } else if (y >= 57 && y <= 87 && x >= 204 && x <= 286) {
          if (!recording) {
            engine.pause();
            active_take = new_take_path();
            status = device.start_recording(session.sample_rate, 2, 256, audio_callback, &engine, 300);
            if (status) { recording = true; device_started = true; engine.play(); message = "Recording to " + active_take.filename().string() + "; 5-minute bounded capture."; }
            else message = "Cannot record: " + status.error;
          } else {
            auto out = active_take;
            auto saved_take = device.stop_recording(out, false); recording = false; device_started = false;
            if (saved_take) message = "Take saved: " + out.filename().string() + " (" + std::to_string(*saved_take.value) + " frames).";
            else message = "Recording stop/save: " + saved_take.error;
          }
        }
        else if (y >= top && y < top + static_cast<int>(session.tracks.size()) * track_h) {
          const int ti = (y - top) / track_h;
          if (ti >= 0 && static_cast<std::size_t>(ti) < session.tracks.size()) {
            selected_track = ti; selected_gain = static_cast<float>(session.tracks[static_cast<std::size_t>(ti)].gain_db);
            if (x >= 28 && x <= 80 && y - (top + ti * track_h) >= 50) {
              undo_stack.checkpoint(session);
              engine.pause(); device.stop(); device_started = false;
              session.tracks[static_cast<std::size_t>(ti)].mute = !session.tracks[static_cast<std::size_t>(ti)].mute;
              engine.set_track_mute(static_cast<std::size_t>(ti), session.tracks[static_cast<std::size_t>(ti)].mute);
            }
          }
        } else if (y >= height - 56 && y <= height - 36 && x >= 120 && x <= 480) {
          selected_gain = -60.0f + 96.0f * static_cast<float>(x - 120) / 360.0f;
          undo_stack.checkpoint(session);
          engine.pause(); device.stop(); device_started = false;
          auto gain_status = engine.set_track_gain_db(static_cast<std::size_t>(std::max(0, selected_track)), selected_gain);
          if (!gain_status) message = gain_status.error;
          else { session.tracks[static_cast<std::size_t>(selected_track)].gain_db = selected_gain; message = "Track gain updated in playback state; save/export UI is not yet implemented."; }
        }
        draw();
      }
    }
    if (engine.playing()) draw();
    else { XFlush(display); usleep(16000); }
  }
  if (recording) { (void)device.stop_recording(active_take, false); recording = false; }
  device.stop(); XFreeGC(display, gc); XDestroyWindow(display, win); XCloseDisplay(display);
  return 0;
}
