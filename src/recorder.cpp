#include "recorder.hpp"
#include <cmath>
#include <limits>
namespace fk::lanes {
AutomationRecorder::AutomationRecorder(AutomationMode mode, AutoParam param, AutomationLane existing, RecorderConfig cfg)
    : eff_(mode), cfg_(cfg), work_(std::move(existing)) {
  work_.param = std::move(param);
  if (mode == AutomationMode::TouchLatch) eff_ = work_.param.is_fader() ? AutomationMode::Touch : AutomationMode::Latch;
  ignores_ = eff_ == AutomationMode::Off || eff_ == AutomationMode::Read || eff_ == AutomationMode::Trim;
  if (cfg_.return_samples < 0) cfg_.return_samples = 0;
}
Status AutomationRecorder::begin_pass(Samples at) {
  if (in_pass_) return Status::fail("pass already started");
  if (at < 0) return Status::fail("pass start must be >= 0");
  in_pass_ = true; writing_ = false; latched_ = false; last_update_ = at; rec_.clear();
  return Status::success();
}
Status AutomationRecorder::update(Samples at, double v, bool touching) {
  if (!in_pass_) return Status::fail("update outside a pass (call begin_pass first)");
  if (at < last_update_) return Status::fail("event time went backwards");
  if (!std::isfinite(v)) return Status::fail("control value must be finite");
  last_update_ = at;
  if (ignores_) return Status::success();
  bool want = false;
  if (eff_ == AutomationMode::Write) want = true;
  else if (eff_ == AutomationMode::Touch) want = touching;
  else { if (touching) latched_ = true; want = latched_; }   // Latch
  if (want && !writing_) start_segment(at, v);
  else if (want) add_point(at, v);
  else if (writing_) finish_segment(at, true);
  return Status::success();
}
Status AutomationRecorder::end_pass(Samples at) {
  if (!in_pass_) return Status::fail("no pass in progress");
  if (at < last_update_) return Status::fail("pass end is before the last event");
  if (writing_) finish_segment(at, false);
  in_pass_ = false; latched_ = false;
  return Status::success();
}
void AutomationRecorder::start_segment(Samples at, double v) {
  writing_ = true; seg_start_ = at; rec_.clear();
  rec_.push_back({at, v}); seen_ = {at, v};
}
void AutomationRecorder::add_point(Samples at, double v) {
  seen_ = {at, v};
  const auto& last = rec_.back();
  if (std::abs(v - last.value) >= cfg_.min_value_step || at - last.at >= cfg_.min_time_step) rec_.push_back({at, v});
}
void AutomationRecorder::finish_segment(Samples end_at, bool release) {
  if (rec_.back().at < end_at || rec_.back().value != seen_.value) {
    if (end_at > rec_.back().at) rec_.push_back({end_at, seen_.value});
  }
  const double def = work_.param.range().def;
  const Samples ret = release ? cfg_.return_samples : 0;
  const Samples anchor = (ret > 0) ? end_at + ret : end_at + 1;
  const double before = work_.value_at(seg_start_ - 1, def), after = work_.value_at(anchor, def);
  work_.clear_range(seg_start_, anchor + 1);
  if (seg_start_ > 0) work_.set_point(seg_start_ - 1, before);
  for (const auto& p : rec_) work_.set_point(p.at, p.value);
  work_.set_point(anchor, after);
  rec_.clear(); writing_ = false;
}
}  // namespace fk::lanes
