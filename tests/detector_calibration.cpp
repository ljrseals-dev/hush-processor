// Calibration corpus for the hardened detectors. Synthetic signals only (no real recordings).
// Exit status 1 if any ORDINARY item raises Alert, or if a control signal is not detected.
#include <cmath>
#include <cstdio>
#include <functional>
#include <random>
#include <string>
#include <vector>
#include "core/ENFAnalyzer.h"
#include "core/UltrasonicScanner.h"
#include "core/WatermarkDetector.h"
using namespace dawchecker::dsp;

struct Item { std::string name; bool control; std::function<double(std::size_t, double)> gen; };
static const char* sname(DetectorStatus s) { return s == DetectorStatus::Alert ? "Alert" : s == DetectorStatus::Active ? "Active" : "Idle"; }

int main() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  const double levels_db[] = {-40.0, -20.0, -6.0, 0.0};
  const double seconds = 10.0;
  struct Counts { int idle = 0, active = 0, alert = 0; } us_c, enf_c, wm_c, wm2_c;
  int ordinary = 0, controls = 0, control_hits_us = 0, control_hits_enf = 0, fail = 0;
  std::printf("%-22s %-7s %-7s | %-7s %-7s %-7s %-7s\n", "item", "rate", "level", "ultras.", "ENF", "wm=ref", "wm!=ref");
  for (double sr : rates) for (double ldb : levels_db) {
    const double amp = std::pow(10.0, ldb / 20.0);
    std::mt19937 gw(1);
    std::uniform_real_distribution<double> uni(-1.0, 1.0);
    // pink noise: Paul Kellet filter state, speech-like: low-passed noise with 4 Hz AM
    double b0 = 0, b1 = 0, b2 = 0, b3 = 0, b4 = 0, b5 = 0, b6 = 0, lp = 0;
    std::vector<Item> items = {
      {"silence", false, [&](std::size_t, double) { return 0.0; }},
      {"white noise", false, [&](std::size_t, double) { return amp * uni(gw); }},
      {"pink noise", false, [&](std::size_t, double) { double w = uni(gw);
         b0 = 0.99886 * b0 + w * 0.0555179; b1 = 0.99332 * b1 + w * 0.0750759; b2 = 0.96900 * b2 + w * 0.1538520;
         b3 = 0.86650 * b3 + w * 0.3104856; b4 = 0.55000 * b4 + w * 0.5329522; b5 = -0.7616 * b5 - w * 0.0168980;
         double p = b0 + b1 + b2 + b3 + b4 + b5 + b6 + w * 0.5362; b6 = w * 0.115926; return amp * p * 0.11; }},
      {"speech-like AM noise", false, [&](std::size_t i, double s) { lp += 0.25 * (uni(gw) - lp);
         return amp * lp * (0.5 + 0.5 * std::sin(2 * kPi * 4.0 * double(i) / s)); }},
      {"440 Hz sine", false, [&](std::size_t i, double s) { return amp * std::sin(2 * kPi * 440.0 * double(i) / s); }},
      {"1 kHz sine", false, [&](std::size_t i, double s) { return amp * std::sin(2 * kPi * 1000.0 * double(i) / s); }},
      {"50 Hz sine (control)", true, [&](std::size_t i, double s) { return amp * std::sin(2 * kPi * 50.0 * double(i) / s); }},
      {"60 Hz sine (control)", true, [&](std::size_t i, double s) { return amp * std::sin(2 * kPi * 60.0 * double(i) / s); }},
      {"19.2 kHz sine (control)", true, [&](std::size_t i, double s) { return amp * std::sin(2 * kPi * 19200.0 * double(i) / s); }},
    };
    for (auto& it : items) {
      UltrasonicScanner us(sr); ENFAnalyzer enf(sr); WatermarkDetector wm(sr), wm2(sr);
      DetectorStatus mu = DetectorStatus::Idle, me = DetectorStatus::Idle, mw = DetectorStatus::Idle, mw2 = DetectorStatus::Idle;
      std::mt19937 gr(99); std::uniform_real_distribution<double> ur(-1.0, 1.0);
      const std::size_t N = static_cast<std::size_t>(seconds * sr);
      for (std::size_t i = 0; i < N; ++i) {
        double x = it.gen(i, sr);
        auto a = us.process(x); if (a.status > mu) mu = a.status;
        auto b = enf.process(x); if (b.status > me) me = b.status;
        auto c = wm.process(x, x); if (c.status > mw) mw = c.status;
        auto d = wm2.process(x, amp * ur(gr)); if (d.status > mw2) mw2 = d.status;   // unrelated reference
      }
      std::printf("%-22s %-7.0f %-7.0f | %-7s %-7s %-7s %-7s\n", it.name.c_str(), sr, ldb, sname(mu), sname(me), sname(mw), sname(mw2));
      auto bump = [](Counts& c, DetectorStatus s) { if (s == DetectorStatus::Alert) ++c.alert; else if (s == DetectorStatus::Active) ++c.active; else ++c.idle; };
      if (!it.control) {
        ++ordinary; bump(us_c, mu); bump(enf_c, me); bump(wm_c, mw); bump(wm2_c, mw2);
        if (mu == DetectorStatus::Alert || me == DetectorStatus::Alert || mw == DetectorStatus::Alert || mw2 == DetectorStatus::Alert) ++fail;
      } else {
        ++controls;
        if (it.name.find("19.2") != std::string::npos) { if (mu != DetectorStatus::Idle) ++control_hits_us; }
        else { if (me != DetectorStatus::Idle) ++control_hits_enf; }
      }
    }
  }
  const int ctl_us = controls / 3, ctl_enf = controls * 2 / 3;
  std::printf("\nORDINARY items: %d (6 signal types x 4 levels x 3 rates; silence included)\n", ordinary);
  std::printf("  ultrasonic : Idle=%d Active=%d Alert=%d\n  ENF        : Idle=%d Active=%d Alert=%d\n  residual (live==ref)  : Idle=%d Active=%d Alert=%d\n  residual (unrelated ref): Idle=%d Active=%d Alert=%d\n",
              us_c.idle, us_c.active, us_c.alert, enf_c.idle, enf_c.active, enf_c.alert, wm_c.idle, wm_c.active, wm_c.alert, wm2_c.idle, wm2_c.active, wm2_c.alert);
  std::printf("CONTROLS detected (>= Active): 19.2 kHz %d/%d, 50/60 Hz %d/%d\n", control_hits_us, ctl_us, control_hits_enf, ctl_enf);
  bool ok = fail == 0 && control_hits_us == ctl_us && control_hits_enf == ctl_enf;
  std::printf("%s: ordinary items with any Alert verdict = %d\n", ok ? "PASS" : "FAIL", fail);
  return ok ? 0 : 1;
}
