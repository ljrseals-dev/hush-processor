#include "clipboard.hpp"
#include <algorithm>
#include <cctype>
namespace fk::clipboard {
namespace {
bool luhn(const std::string& d) {
  int sum = 0; bool alt = false;
  for (auto it = d.rbegin(); it != d.rend(); ++it) {
    int n = *it - '0';
    if (alt) { n *= 2; if (n > 9) n -= 9; }
    sum += n; alt = !alt;
  }
  return sum % 10 == 0;
}
std::string lower(std::string s) { for (auto& c : s) c = static_cast<char>(std::tolower(static_cast<unsigned char>(c))); return s; }
bool has_card(const std::string& t) {
  std::string digits;
  for (std::size_t i = 0; i <= t.size(); ++i) {
    char c = i < t.size() ? t[i] : '\n';
    if (std::isdigit(static_cast<unsigned char>(c))) digits.push_back(c);
    else if ((c == ' ' || c == '-') && !digits.empty()) continue;
    else {
      if (digits.size() >= 13 && digits.size() <= 19 && luhn(digits)) return true;
      digits.clear();
    }
  }
  return false;
}
bool long_token(const std::string& t) {
  std::size_t run = 0;
  for (char c : t) {
    if (std::isalnum(static_cast<unsigned char>(c)) || c == '+' || c == '/' || c == '_' || c == '-' || c == '=') {
      if (++run >= 40) return true;
    } else run = 0;
  }
  return false;
}
bool keyword_assignment(const std::string& low) {
  static const char* kw[] = {"password", "passwd", "secret", "api_key", "apikey", "token", "authorization"};
  for (auto k : kw) {
    std::size_t pos = 0;
    while ((pos = low.find(k, pos)) != std::string::npos) {
      std::size_t p = pos + std::char_traits<char>::length(k);
      while (p < low.size() && (low[p] == ' ' || low[p] == '"' || low[p] == '\'')) ++p;
      if (p < low.size() && (low[p] == '=' || low[p] == ':')) {
        ++p;
        while (p < low.size() && (low[p] == ' ' || low[p] == '"' || low[p] == '\'')) ++p;
        std::size_t q = p;
        while (q < low.size() && !std::isspace(static_cast<unsigned char>(low[q]))) ++q;
        if (q - p >= 6) return true;
      }
      pos += 1;
    }
  }
  return false;
}
}  // namespace
std::vector<Finding> scan_sensitive(const std::string& t) {
  std::vector<Finding> f;
  std::string low = lower(t);
  if (t.find("-----BEGIN") != std::string::npos && low.find("private key") != std::string::npos) f.push_back({"private key block"});
  std::size_t a = t.find("AKIA");
  if (a != std::string::npos && a + 20 <= t.size()) {
    bool ok = true;
    for (std::size_t i = a + 4; i < a + 20; ++i)
      if (!(std::isupper(static_cast<unsigned char>(t[i])) || std::isdigit(static_cast<unsigned char>(t[i])))) ok = false;
    if (ok) f.push_back({"possible AWS access key id"});
  }
  if (t.find("ghp_") != std::string::npos) f.push_back({"possible GitHub token"});
  if (t.find("eyJ") != std::string::npos && std::count(t.begin(), t.end(), '.') >= 2 && long_token(t)) f.push_back({"possible JWT"});
  if (keyword_assignment(low)) f.push_back({"password/secret/token assignment"});
  if (has_card(t)) f.push_back({"possible payment card number"});
  if (long_token(t) && f.empty()) f.push_back({"long opaque token-like string"});
  return f;
}
Checker::Checker(Provider& p, std::function<std::int64_t()> clock) : provider_(p), clock_(std::move(clock)) {}
void Checker::enable() { enabled_ = true; }
void Checker::disable() {
  enabled_ = false; pending_token_ = 0; pending_text_.clear(); pending_is_clear_ = false;
  history_on_ = false; hist_.clear();
}
void Checker::prune() {
  std::int64_t now = clock_();
  while (!hist_.empty() && now - hist_.front().at >= ttl_) hist_.pop_front();
  while (hist_.size() > max_entries_) hist_.pop_front();
}
Result<Preview> Checker::read_preview() {
  using R = Result<Preview>;
  if (!enabled_) return R::fail("clipboard access is Off; enable it explicitly for this session");
  auto r = provider_.read();
  if (!r) return R::fail(r.error);
  if (r->size() > kMaxContentBytes) return R::fail("clipboard content exceeds 1 MiB limit");
  Preview p;
  p.bytes = r->size();
  p.lines = 1 + static_cast<std::size_t>(std::count(r->begin(), r->end(), '\n'));
  p.truncated = r->size() > kMaxPreviewBytes;
  p.text = p.truncated ? r->substr(0, kMaxPreviewBytes) : *r;
  p.findings = scan_sensitive(*r);
  if (history_on_ && p.findings.empty()) { prune(); hist_.push_back({clock_(), *r}); prune(); }
  return R::success(std::move(p));
}
Result<WritePlan> Checker::prepare_write(const std::string& text) {
  using R = Result<WritePlan>;
  if (!enabled_) return R::fail("clipboard access is Off");
  if (text.size() > kMaxContentBytes) return R::fail("text exceeds 1 MiB limit");
  pending_token_ = next_token_++; pending_text_ = text; pending_is_clear_ = false;
  return R::success({pending_token_, text.size(), text.substr(0, kMaxPreviewBytes)});
}
Status Checker::commit_write(std::uint64_t token, bool ok) {
  if (!enabled_) return Status::fail("clipboard access is Off");
  if (!ok) { pending_token_ = 0; return Status::fail("write not confirmed"); }
  if (!pending_token_ || token != pending_token_ || pending_is_clear_) return Status::fail("no matching pending write");
  std::string t = std::move(pending_text_);
  pending_token_ = 0; pending_text_.clear();
  return provider_.write(t);
}
Result<WritePlan> Checker::prepare_clear() {
  using R = Result<WritePlan>;
  if (!enabled_) return R::fail("clipboard access is Off");
  pending_token_ = next_token_++; pending_text_.clear(); pending_is_clear_ = true;
  return R::success({pending_token_, 0, "(clear the system clipboard)"});
}
Status Checker::commit_clear(std::uint64_t token, bool ok) {
  if (!enabled_) return Status::fail("clipboard access is Off");
  if (!ok) { pending_token_ = 0; return Status::fail("clear not confirmed"); }
  if (!pending_token_ || token != pending_token_ || !pending_is_clear_) return Status::fail("no matching pending clear");
  pending_token_ = 0; pending_is_clear_ = false;
  return provider_.write("");
}
Status Checker::enable_history(std::size_t n, std::int64_t ttl) {
  if (!enabled_) return Status::fail("enable clipboard access first");
  if (n == 0 || n > 50) return Status::fail("history size must be 1..50");
  if (ttl <= 0 || ttl > 3600) return Status::fail("history lifetime must be 1..3600 seconds");
  history_on_ = true; max_entries_ = n; ttl_ = ttl;
  return Status::success();
}
void Checker::clear_history() { hist_.clear(); }
std::size_t Checker::history_size() { if (history_on_) prune(); return hist_.size(); }
std::vector<std::string> Checker::history() {
  std::vector<std::string> out;
  if (history_on_) { prune(); for (auto& e : hist_) out.push_back(e.text); }
  return out;
}
}  // namespace fk::clipboard
