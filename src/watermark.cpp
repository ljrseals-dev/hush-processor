#include "watermark.hpp"
#include <algorithm>
#include <cmath>
namespace fk::watermark {
namespace {
constexpr std::size_t kMinSegment = 8192;
constexpr int kNullKeys = 64;
std::uint64_t splitmix(std::uint64_t x) {
  x += 0x9E3779B97F4A7C15ULL;
  x = (x ^ (x >> 30)) * 0xBF58476D1CE4E5B9ULL;
  x = (x ^ (x >> 27)) * 0x94D049BB133111EBULL;
  return x ^ (x >> 31);
}
inline double pn(std::uint64_t key, std::uint64_t n) { return (splitmix(key ^ splitmix(n)) >> 63) ? 1.0 : -1.0; }
std::vector<double> mono(const fk::audio::AudioBuffer& a) {
  std::size_t fr = a.frames();
  std::vector<double> m(fr, 0.0);
  for (std::size_t i = 0; i < fr; ++i) {
    double s = 0;
    for (std::uint16_t c = 0; c < a.channels; ++c) s += a.samples[i * a.channels + c];
    m[i] = s / a.channels;
  }
  return m;
}
double rms(const std::vector<float>& v) {
  long double s = 0;
  for (float x : v) s += static_cast<double>(x) * x;
  return v.empty() ? 0.0 : std::sqrt(static_cast<double>(s / v.size()));
}
// Sum over bits of |correlation| for a given key; also returns per-bit signed correlation.
double statistic(const std::vector<double>& m, std::uint64_t key, std::size_t bits, std::size_t seg,
                 std::vector<double>* per_bit) {
  double total = 0;
  for (std::size_t b = 0; b < bits; ++b) {
    double c = 0;
    std::size_t base = b * seg;
    for (std::size_t n = 0; n < seg; ++n) c += m[base + n] * pn(key, base + n);
    c /= static_cast<double>(seg);
    if (per_bit) per_bit->push_back(c);
    total += std::abs(c);
  }
  return total;
}
}  // namespace
std::string verdict_name(Verdict v) {
  switch (v) {
    case Verdict::NoSignatureDetected: return "no signature detected";
    case Verdict::PossibleSignature: return "possible signature";
    case Verdict::SignatureLikeResidualDetected: return "signature-like residual detected";
  }
  return "unknown";
}
Result<EmbedReport> embed(const fk::audio::AudioBuffer& in, const EmbedParams& p) {
  using R = Result<EmbedReport>;
  try { in.validate(); } catch (const std::exception& e) { return R::fail(e.what()); }
  if (p.payload.empty() || p.payload.size() > 64) return R::fail("payload must be 1..64 bits");
  if (!std::isfinite(p.strength_db) || p.strength_db < -80 || p.strength_db > -20)
    return R::fail("strength must be between -80 and -20 dBFS");
  std::size_t seg = in.frames() / p.payload.size();
  if (seg < kMinSegment) return R::fail("audio too short for this payload length (need >= 8192 frames per bit)");
  const double amp = std::pow(10.0, p.strength_db / 20.0);
  EmbedReport rep;
  rep.audio = in;
  rep.segment_frames = seg;
  rep.peak_before = fk::audio::analyze(in).peak;
  const std::size_t used = seg * p.payload.size();
  for (std::size_t n = 0; n < used; ++n) {
    double v = amp * (p.payload[n / seg] ? 1.0 : -1.0) * pn(p.key, n);
    for (std::uint16_t c = 0; c < in.channels; ++c)
      rep.audio.samples[n * in.channels + c] += static_cast<float>(v);
  }
  rep.peak_after = fk::audio::analyze(rep.audio).peak;
  if (rep.peak_after > 1.0) return R::fail("watermark would clip; lower the strength or the program level first");
  double prog = rms(in.samples);
  rep.watermark_to_signal_db = prog > 0 ? 20.0 * std::log10(amp / prog) : 0.0;
  return R::success(std::move(rep));
}
Result<Detection> detect(const fk::audio::AudioBuffer& in, std::uint64_t key, std::size_t bits) {
  using R = Result<Detection>;
  try { in.validate(); } catch (const std::exception& e) { return R::fail(e.what()); }
  if (bits == 0 || bits > 64) return R::fail("payload length must be 1..64 bits");
  std::size_t seg = in.frames() / bits;
  if (seg < kMinSegment) return R::fail("audio too short for this payload length");
  auto m = mono(in);
  std::vector<double> per_bit;
  double s = statistic(m, key, bits, seg, &per_bit);
  double sum = 0, sum2 = 0;
  for (int k = 1; k <= kNullKeys; ++k) {
    double v = statistic(m, key ^ splitmix(0xA11CE5ULL + k), bits, seg, nullptr);
    sum += v; sum2 += v * v;
  }
  double mean = sum / kNullKeys;
  double var = std::max(0.0, sum2 / kNullKeys - mean * mean);
  double sd = std::sqrt(var);
  Detection d;
  d.z_score = sd > 1e-12 ? (s - mean) / sd : 0.0;
  for (double c : per_bit) d.decoded_bits.push_back(c > 0);
  if (d.z_score >= 8.0) d.verdict = Verdict::SignatureLikeResidualDetected;
  else if (d.z_score >= 4.0) d.verdict = Verdict::PossibleSignature;
  d.text = verdict_name(d.verdict) + " (z=" + std::to_string(d.z_score) +
           "). Indicator only: absence of a detection does not prove absence of a watermark, and presence of this "
           "specific carrier does not identify an author.";
  return R::success(std::move(d));
}
}  // namespace fk::watermark
