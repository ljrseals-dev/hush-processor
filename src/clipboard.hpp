#pragma once
// fk::clipboard: consent-gated, local-only clipboard checker. Off by default, no polling, no logging,
// no network. The OS clipboard backend is NOT implemented in this build: only an injected Provider can touch a clipboard.
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <vector>
#include "result.hpp"
namespace fk::clipboard {
class Provider {
 public:
  virtual ~Provider() = default;
  virtual Result<std::string> read() = 0;
  virtual Status write(const std::string& text) = 0;
};
class NullProvider final : public Provider {
 public:
  Result<std::string> read() override { return Result<std::string>::fail("no OS clipboard backend in this build"); }
  Status write(const std::string&) override { return Status::fail("no OS clipboard backend in this build"); }
};
struct Finding { std::string kind; };  // never contains the matched text
std::vector<Finding> scan_sensitive(const std::string& text);  // heuristic; never claims completeness
struct Preview {
  std::size_t bytes = 0, lines = 0;
  std::string text;      // exact content up to max_preview_bytes
  bool truncated = false;
  std::vector<Finding> findings;
};
struct WritePlan { std::uint64_t token = 0; std::size_t bytes = 0; std::string preview; };
class Checker {
 public:
  static constexpr std::size_t kMaxPreviewBytes = 4096;
  static constexpr std::size_t kMaxContentBytes = 1 << 20;
  Checker(Provider& p, std::function<std::int64_t()> clock_seconds);
  bool enabled() const { return enabled_; }
  void enable();    // explicit per-session opt-in; a new Checker always starts Off
  void disable();   // one-click off; cancels any pending write and stops history
  Result<Preview> read_preview();
  Result<WritePlan> prepare_write(const std::string& text);
  Status commit_write(std::uint64_t token, bool user_confirmed);
  Result<WritePlan> prepare_clear();
  Status commit_clear(std::uint64_t token, bool user_confirmed);
  Status enable_history(std::size_t max_entries, std::int64_t ttl_seconds);  // opt-in; <=50 entries, <=3600 s
  void clear_history();
  std::size_t history_size();
  std::vector<std::string> history();
 private:
  struct Entry { std::int64_t at; std::string text; };
  Provider& provider_;
  std::function<std::int64_t()> clock_;
  bool enabled_ = false;
  bool history_on_ = false;
  std::size_t max_entries_ = 0;
  std::int64_t ttl_ = 0;
  std::deque<Entry> hist_;
  std::uint64_t next_token_ = 1, pending_token_ = 0;
  bool pending_is_clear_ = false;
  std::string pending_text_;
  void prune();
};
}  // namespace fk::clipboard
