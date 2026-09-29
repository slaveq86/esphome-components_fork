// Test stub for esphome/core/automation.h.
#pragma once
#include <functional>
#include <vector>

namespace esphome {
template <typename... Ts> class Trigger {
public:
  void trigger(Ts... x) {
    for (auto &fn : this->listeners_)
      fn(x...);
  }
  void add_listener(std::function<void(Ts...)> fn) {
    this->listeners_.push_back(std::move(fn));
  }

protected:
  std::vector<std::function<void(Ts...)>> listeners_;
};
} // namespace esphome
