#pragma once
// Error-as-value helpers for malformed input and operational failures (spec section 3).
#include <optional>
#include <string>
#include <utility>
namespace fk {
struct Status {
  bool ok{true};
  std::string error;
  static Status success() { return {}; }
  static Status fail(std::string e) { return {false, std::move(e)}; }
  explicit operator bool() const { return ok; }
};
template <class T>
struct Result {
  std::optional<T> value;
  std::string error;
  static Result success(T v) { Result r; r.value = std::move(v); return r; }
  static Result fail(std::string e) { Result r; r.error = std::move(e); return r; }
  explicit operator bool() const { return value.has_value(); }
  const T& operator*() const { return *value; }
  T& operator*() { return *value; }
  const T* operator->() const { return &*value; }
};
}  // namespace fk
