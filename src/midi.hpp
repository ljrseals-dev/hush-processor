#pragma once
#include "result.hpp"
#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>

namespace hush::midi {
struct Event { std::uint64_t frame{}; std::uint8_t status{}, data1{}, data2{}; };
// Fixed-capacity single-producer/single-consumer queue. No allocation or locks in push/pop.
template <std::size_t Capacity = 1024> class SpscQueue {
  static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0, "Capacity must be a power of two");
 public:
  bool push(const Event& e) noexcept {
    const auto head = head_.load(std::memory_order_relaxed);
    const auto next = (head + 1) & (Capacity - 1);
    if (next == tail_.load(std::memory_order_acquire)) return false;
    data_[head] = e; head_.store(next, std::memory_order_release); return true;
  }
  bool pop(Event& e) noexcept {
    const auto tail = tail_.load(std::memory_order_relaxed);
    if (tail == head_.load(std::memory_order_acquire)) return false;
    e = data_[tail]; tail_.store((tail + 1) & (Capacity - 1), std::memory_order_release); return true;
  }
 private:
  std::array<Event, Capacity> data_{};
  alignas(64) std::atomic<std::size_t> head_{0};
  alignas(64) std::atomic<std::size_t> tail_{0};
};

fk::Status validate_event(const Event& event) noexcept;
} // namespace hush::midi
