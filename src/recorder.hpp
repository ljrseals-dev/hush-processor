#pragma once
// fk::lanes::AutomationRecorder: live automation capture in Write / Touch / Latch / Touch-Latch (ORIGINAL code; the
// SoundCraft zip has no live-capture logic). It is a pure state machine fed with (time, control value, touched) events by
// the engine/UI thread; it allocates, so it must NOT be driven from the real-time audio callback.
// Rules (documented in LANES_AND_MARKERS_NOTES.md):
//   Write      writes from begin_pass() to end_pass() whether or not the control is touched.
//   Touch      writes only while touched; on release the lane returns to its existing curve (optionally over return_samples).
//   Latch      starts at the first touch and keeps writing the last value until end_pass().
//   TouchLatch faders (volume, pan, send level/pan) behave like Touch; switches and plugin parameters like Latch.
//   Read / Off / Trim: events ignored (live Trim capture is NOT implemented).
// Anything outside the written span keeps the original curve (anchor points, as in write_range).
#include "automation.hpp"
namespace fk::lanes {
struct RecorderConfig {
  Samples return_samples = 0;     // Touch release glide back to the original curve (0 = step)
  double min_value_step = 0.01;   // skip points that change less than this, unless min_time_step has passed
  Samples min_time_step = 480;    // ...or this many samples have passed since the last stored point
};
class AutomationRecorder {
 public:
  AutomationRecorder(AutomationMode mode, AutoParam param, AutomationLane existing, RecorderConfig cfg = {});
  Status begin_pass(Samples at);
  Status update(Samples at, double control_value, bool touching);
  Status end_pass(Samples at);
  const AutomationLane& lane() const { return work_; }
  bool writing() const { return writing_; }
 private:
  void start_segment(Samples at, double v);
  void add_point(Samples at, double v);
  void finish_segment(Samples end_at, bool release);
  AutomationMode eff_;
  RecorderConfig cfg_;
  AutomationLane work_;
  bool in_pass_ = false, writing_ = false, latched_ = false, ignores_ = false;
  Samples seg_start_ = 0, last_update_ = 0;
  std::vector<AutomationPoint> rec_;
  AutomationPoint seen_{};
};
}  // namespace fk::lanes
