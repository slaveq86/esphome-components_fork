// Test stub for esphome/core/component.h. defer() queues work; tests call
// esphome::testing::run_deferred() to play the part of the main loop.
#pragma once
#include <functional>
#include <utility>
#include <vector>

#include "esphome/core/application.h"
#include "esphome/core/hal.h"
#include "esphome/core/helpers.h"
#include "esphome/core/log.h"
#include "esphome/core/optional.h"

namespace esphome {
namespace testing {
inline std::vector<std::function<void()>> &deferred() {
  static std::vector<std::function<void()>> queue;
  return queue;
}

// Runs deferred callbacks (including ones queued while running); returns count.
inline size_t run_deferred() {
  size_t count = 0;
  while (!deferred().empty()) {
    auto batch = std::move(deferred());
    deferred().clear();
    for (auto &fn : batch) {
      fn();
      count++;
    }
  }
  return count;
}
} // namespace testing

class Component {
public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
  virtual float get_setup_priority() const { return 0.0f; }

  void mark_failed() { this->failed_ = true; }
  bool is_failed() const { return this->failed_; }

protected:
  void defer(std::function<void()> &&f) {
    testing::deferred().push_back(std::move(f));
  }

  bool failed_{false};
};

} // namespace esphome
