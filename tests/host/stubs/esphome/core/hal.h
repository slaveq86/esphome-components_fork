// Test stub for esphome/core/hal.h: a fake clock. delay() advances it, so
// timeouts run instantly and deterministically. A simulated chip can listen to
// the clock to fire its events (e.g. "packet received") at the right time.
#pragma once
#include <cstdint>
#include <functional>

namespace esphome {
namespace testing {
inline uint64_t &fake_micros() {
  static uint64_t us = 0;
  return us;
}

inline std::function<void()> &time_listener() {
  static std::function<void()> listener;
  return listener;
}

inline void advance_time(uint64_t us) {
  fake_micros() += us;
  if (time_listener())
    time_listener()();
}
} // namespace testing

inline uint32_t millis() { return testing::fake_micros() / 1000; }
inline uint32_t micros() { return testing::fake_micros(); }
inline void delay(uint32_t ms) { testing::advance_time(uint64_t(ms) * 1000); }
inline void delayMicroseconds(uint32_t us) { testing::advance_time(us); }
} // namespace esphome
