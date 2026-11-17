// Tests for the hardened MSV engine fork: real-time safety, neutralizer policy, coherence, rate invariance.
#include <atomic>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <iostream>
#include <new>
#include <random>
#include <thread>
#include <vector>
#include "core/MSV_Engine.h"
#include "core/Neutralizers.h"
#include "core/RunningWindow.h"
#include "core/SecurityHardening.h"

// GCC emits a known false positive when a replacement operator new is built on malloc/free; test-only suppression.
#pragma GCC diagnostic ignored "-Wmismatched-new-delete"
// ---- allocation counter: counts operator new calls only while g_count is set ----
static std::atomic<long> g_allocs{0};
static std::atomic<bool> g_count{false};
void* operator new(std::size_t n) { if (g_count.load(std::memory_order_relaxed)) ++g_allocs; void* p = std::malloc(n ? n : 1); if (!p) throw std::bad_alloc(); return p; }
void* operator new[](std::size_t n) { return operator new(n); }
void operator delete(void* p) noexcept { std::free(p); }
void operator delete[](void* p) noexcept { std::free(p); }
void operator delete(void* p, std::size_t) noexcept { std::free(p); }
void operator delete[](void* p, std::size_t) noexcept { std::free(p); }

using namespace dawchecker::dsp;
static int g_fail = 0, g_run = 0;
#define CHECK(c, m) do { ++g_run; if (!(c)) { ++g_fail; std::cout << "[FAIL] " << m << "  (" #c ")\n"; } else std::cout << "[ ok ] " << m << "\n"; } while (0)

static double tone(double hz, double amp, std::size_t i, double sr) { return amp * std::sin(2.0 * kPi * hz * double(i) / sr); }
static const char* sname(DetectorStatus s) { return s == DetectorStatus::Alert ? "Alert" : s == DetectorStatus::Active ? "Active" : "Idle"; }

static void test_ring_equivalence() {
  RunningWindow<8> w; std::deque<double> ref;
  std::mt19937_64 g(42); std::uniform_real_distribution<double> d(-70.0, -20.0);
  double worst_mean = 0, worst_var = 0;
  for (int i = 0; i < 200000; ++i) {
    double x = d(g); w.push(x); ref.push_back(x); if (ref.size() > 8) ref.pop_front();
    double m = 0; for (double v : ref) m += v; m /= double(ref.size());
    double var = 0; for (double v : ref) var += (v - m) * (v - m); var /= double(ref.size());
    worst_mean = std::max(worst_mean, std::abs(m - w.mean())); worst_var = std::max(worst_var, std::abs(var - w.variance()));
  }
  CHECK(worst_mean < 1e-9 && worst_var < 1e-7, "RunningWindow matches a naive std::deque over 200k random pushes (max |dmean|=" + std::to_string(worst_mean) + ", max |dvar|=" + std::to_string(worst_var) + ")");
  RunningWindow<4> small; small.push(1); small.push(2);
  CHECK(small.count() == 2 && !small.full() && std::abs(small.mean() - 1.5) < 1e-12, "partial window statistics");
}

static void test_neutralizer_policy() {
  ENFNotchFilter n; UltrasonicRolloff r; NeutralizeParams p;
  CHECK(!n.enabled && !r.enabled && !p.enfNotch && !p.ultrasonicRolloff, "all neutralizers default to OFF after construction");
  std::vector<double> in(48000); for (std::size_t i = 0; i < in.size(); ++i) in[i] = tone(60, 0.3, i, 48000) + tone(19200, 0.1, i, 48000);
  auto copy = in;
  auto none = render_neutralized(in, 48000, p);
  CHECK(none.size() == in.size() && std::memcmp(none.data(), in.data(), in.size() * sizeof(double)) == 0, "render_neutralized with an empty enable set is bit-identical");
  CHECK(in == copy, "render_neutralized never modifies its input");
  NeutralizeParams on; on.enfNotch = true; on.ultrasonicRolloff = true;
  auto out = render_neutralized(in, 48000, on);
  auto rms = [](const std::vector<double>& v, std::size_t from) { double s = 0; for (std::size_t i = from; i < v.size(); ++i) s += v[i] * v[i]; return std::sqrt(s / double(v.size() - from)); };
  CHECK(rms(out, 24000) < 0.5 * rms(in, 24000) || rms(out, 24000) < 0.1, "with both enabled (explicit) the 60 Hz + 19.2 kHz content is reduced");
  UltrasonicScanner us(48000.0); ENFAnalyzer enf(48000.0);
  DetectorStatus u = DetectorStatus::Idle, e = DetectorStatus::Idle;
  for (std::size_t i = 0; i < 48000 * 6; ++i) { double x = tone(19200, 0.1, i, 48000) + tone(60, 0.1, i, 48000);
    auto a = us.process(x); auto b = enf.process(x); if (a.status > u) u = a.status; if (b.status > e) e = b.status; }
  CHECK(u != DetectorStatus::Idle && e != DetectorStatus::Idle, "detectors detect that content (so the neutralizers have something to act on)");
}

static void test_process_is_detection_only() {
  MSV_Engine eng(48000.0, 5.0);
  std::mt19937 g(7); std::uniform_real_distribution<double> d(-0.05, 0.05);
  bool identical = true; DetectorStatus worst = DetectorStatus::Idle;
  for (std::size_t i = 0; i < 48000 * 3; ++i) {
    double live = tone(19200, 0.2, i, 48000) + d(g), ref = d(g), out = -999.0;
    eng.process(live, ref, out);
    if (std::memcmp(&out, &live, sizeof out) != 0) identical = false;
    if (eng.snapshot().ultrasonic.status > worst) worst = eng.snapshot().ultrasonic.status;
  }
  CHECK(identical, "process() output is bit-identical to the live input for 144000 samples (even while an Alert is raised)");
  CHECK(worst == DetectorStatus::Alert, "the engine did raise an ultrasonic verdict during that run (" + std::string(sname(worst)) + ")");
}

static void test_zero_allocation() {
  MSV_Engine eng(48000.0, 5.0);
  const std::size_t N = 48000 * 6;
  std::vector<double> live(N), ref(N); std::mt19937 g(3); std::uniform_real_distribution<double> d(-0.1, 0.1);
  for (std::size_t i = 0; i < N; ++i) { live[i] = tone(19200, 0.05, i, 48000) + tone(60, 0.05, i, 48000) + d(g); ref[i] = d(g); }
  long before = g_allocs.load(); g_count = true;
  double out = 0, sink = 0;
  for (std::size_t i = 0; i < N; ++i) { eng.process(live[i], ref[i], out); sink += out; if ((i & 1023) == 0) sink += eng.snapshot().tamperScore; }
  g_count = false;
  long n = g_allocs.load() - before;
  CHECK(n == 0 && std::isfinite(sink), "zero heap allocations across " + std::to_string(N) + " process() calls + snapshot() reads (allocations counted: " + std::to_string(n) + ")");
}

static void test_rate_invariance() {
  const double rates[] = {44100.0, 48000.0, 96000.0};
  DetectorStatus us19[3], enf60[3], noise_us[3], noise_enf[3];
  for (int k = 0; k < 3; ++k) {
    double sr = rates[k]; std::size_t N = std::size_t(10 * sr);
    UltrasonicScanner u1(sr), u2(sr); ENFAnalyzer e1(sr), e2(sr);
    DetectorStatus a = DetectorStatus::Idle, b = DetectorStatus::Idle, c = DetectorStatus::Idle, dd = DetectorStatus::Idle;
    std::mt19937 g(11); std::uniform_real_distribution<double> d(-0.3, 0.3);
    for (std::size_t i = 0; i < N; ++i) {
      auto r1 = u1.process(tone(19200, 0.1, i, sr)); if (r1.status > a) a = r1.status;
      auto r2 = e1.process(tone(60, 0.1, i, sr));    if (r2.status > b) b = r2.status;
      double nz = d(g);
      auto r3 = u2.process(nz); if (r3.status > c) c = r3.status;
      auto r4 = e2.process(nz); if (r4.status > dd) dd = r4.status;
    }
    us19[k] = a; enf60[k] = b; noise_us[k] = c; noise_enf[k] = dd;
  }
  CHECK(us19[0] == us19[1] && us19[1] == us19[2] && us19[1] == DetectorStatus::Alert, std::string("19.2 kHz tone: same verdict at 44.1/48/96 kHz (") + sname(us19[0]) + "/" + sname(us19[1]) + "/" + sname(us19[2]) + ")");
  CHECK(enf60[0] == enf60[1] && enf60[1] == enf60[2] && enf60[1] == DetectorStatus::Active, std::string("60 Hz tone: same verdict at 44.1/48/96 kHz (") + sname(enf60[0]) + "/" + sname(enf60[1]) + "/" + sname(enf60[2]) + ")");
  CHECK(noise_us[0] == DetectorStatus::Idle && noise_us[1] == DetectorStatus::Idle && noise_us[2] == DetectorStatus::Idle &&
        noise_enf[0] == DetectorStatus::Idle && noise_enf[1] == DetectorStatus::Idle && noise_enf[2] == DetectorStatus::Idle, "white noise: Idle at all three rates");
}

static void test_residual_detector() {
  const double sr = 48000.0; std::mt19937 g(21); std::uniform_real_distribution<double> d(-1.0, 1.0);
  auto run = [&](double added_amp, double ref_gain) {
    WatermarkDetector w(sr); DetectorStatus worst = DetectorStatus::Idle;
    for (std::size_t i = 0; i < std::size_t(5 * sr); ++i) {
      double ref = ref_gain * (0.2 * std::sin(2 * kPi * 330.0 * double(i) / sr) + 0.1 * std::sin(2 * kPi * 1200.0 * double(i) / sr) + 0.02 * d(g));
      auto r = w.process(ref + added_amp * d(g), ref); if (r.status > worst) worst = r.status;
    }
    return worst;
  };
  CHECK(run(0.0, 1.0) == DetectorStatus::Idle, "residual: live identical to reference stays Idle");
  CHECK(run(0.005, 1.0) != DetectorStatus::Idle, "residual: persistent additive noise ~ -36 dB re reference is reported (Active)");
  CHECK(run(0.0, 0.0) == DetectorStatus::Idle, "residual: silent reference is not reported");
  CHECK(run(0.00001, 1.0) == DetectorStatus::Idle, "residual: a difference below the -60 dB floor is not reported");
  CHECK(run(0.5, 1.0) != DetectorStatus::Alert, "residual: never goes above Active (a strongly different signal is not an Alert)");
}

static void test_hysteresis() {
  const double sr = 48000.0;
  auto run = [&](double start, double burst_sec) {
    UltrasonicScanner u(sr); DetectorStatus worst = DetectorStatus::Idle;
    for (std::size_t i = 0; i < std::size_t(4 * sr); ++i) {
      double t = double(i) / sr;
      double x = (t >= start && t < start + burst_sec) ? tone(19200, 0.2, i, sr) : 0.0;
      auto r = u.process(x); if (r.status > worst) worst = r.status;
    }
    return worst;
  };
  // A burst no longer than one 85 ms block touches at most two blocks, and three consecutive hits are required.
  bool all_idle = true;
  for (int k = 0; k < 9; ++k) if (run(1.0 + 0.01 * k, 0.085) != DetectorStatus::Idle) all_idle = false;
  CHECK(all_idle, "a 85 ms excursion at 9 different alignments never trips a verdict (needs 3 consecutive blocks)");
  CHECK(run(1.0, 0.6) != DetectorStatus::Idle, "a sustained 600 ms tone does");
  std::cout << "[info] documented limit: a burst of 2+ blocks (>= ~170 ms) can reach Active depending on alignment\n";
}

static void test_snapshot_coherence() {
  MSV_Engine eng(48000.0, 5.0);
  std::atomic<bool> done{false}; std::atomic<long> reads{0}, torn{0};
  std::thread reader([&] {
    while (!done.load(std::memory_order_acquire)) {
      EngineSnapshot s = eng.snapshot();
      double agg = MSV_Engine::aggregate(s.watermark, s.enf, s.ultrasonic);
      bool alert = s.watermark.status == DetectorStatus::Alert || s.enf.status == DetectorStatus::Alert || s.ultrasonic.status == DetectorStatus::Alert;
      if (std::memcmp(&agg, &s.tamperScore, sizeof agg) != 0 || alert != s.isCompromised || s.watermark.label == nullptr || s.enf.label == nullptr || s.ultrasonic.label == nullptr) ++torn;
      ++reads;
    }
  });
  std::mt19937 g(5); std::uniform_real_distribution<double> d(-0.2, 0.2);
  double out = 0;
  for (std::size_t i = 0; i < 48000 * 8; ++i) {
    double burst = ((i / 24000) % 2) ? 0.3 : 0.0;     // toggle states so the snapshot keeps changing
    eng.process(tone(19200, burst, i, 48000) + d(g) * 0.01, d(g) * 0.01, out);
  }
  done.store(true, std::memory_order_release); reader.join();
  CHECK(torn.load() == 0 && reads.load() > 1000, "snapshot coherence: " + std::to_string(reads.load()) + " concurrent reads, " + std::to_string(torn.load()) + " torn (tamperScore always equals the aggregate of the confidences in the same snapshot)");
}

static void test_denormals() {
  CHECK(flushDenormal(1e-310) == 0.0 && flushDenormal(-1e-310) == 0.0 && flushDenormal(0.5) == 0.5, "flushDenormal zeroes subnormals only");
  MSV_Engine eng(48000.0, 5.0); double out = 0;
  for (std::size_t i = 0; i < 48000 * 5; ++i) eng.process(1e-310 * std::sin(double(i)), 1e-310, out);
  auto s = eng.snapshot();
  CHECK(std::fpclassify(s.ultrasonic.confidence) != FP_SUBNORMAL && std::fpclassify(s.enf.confidence) != FP_SUBNORMAL && std::fpclassify(s.watermark.confidence) != FP_SUBNORMAL && std::isfinite(s.tamperScore), "denormal-range input produces no subnormal or non-finite published values");
  UltrasonicScanner u(-5.0); ENFAnalyzer e(std::nan("")); WatermarkDetector w(1e12);
  CHECK(u.process(0.1).status == DetectorStatus::Idle && e.process(0.1).status == DetectorStatus::Idle && w.process(0.1, 0.1).status == DetectorStatus::Idle, "invalid sample rates fall back safely");
}

static void test_selfcheck_honesty() {
  auto r = dawchecker::security::runStartupSelfCheck();
#if defined(__SSP_STRONG__) || defined(__SSP__) || defined(__SSP_ALL__)
  bool ssp = true;
#else
  bool ssp = false;
#endif
#if defined(__CET__)
  bool cet = true;
#else
  bool cet = false;
#endif
  CHECK(r.stackProtectorPresumed == ssp && r.controlFlowGuardOrCET == cet, std::string("self-check mirrors the compiler flags actually used (stack protector=") + (ssp ? "yes" : "no") + ", CET=" + (cet ? "yes" : "no") + ")");
  bool fort = false; for (auto& n : r.notes) if (n.find("FORTIFY_SOURCE level") != std::string::npos) fort = true;
  CHECK(fort, "self-check reports the FORTIFY_SOURCE level it was built with");
}

int main() {
  test_ring_equivalence(); test_neutralizer_policy(); test_process_is_detection_only(); test_zero_allocation();
  test_rate_invariance(); test_residual_detector(); test_hysteresis(); test_snapshot_coherence(); test_denormals(); test_selfcheck_honesty();
  std::cout << "\n" << (g_run - g_fail) << "/" << g_run << " checks passed\n";
  return g_fail == 0 ? 0 : 1;
}
