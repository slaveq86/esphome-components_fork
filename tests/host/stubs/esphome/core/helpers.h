// Test stub for esphome/core/helpers.h: only what the wM-Bus components use.
#pragma once
#include <cmath>
#include <cstdint>
#include <functional>
#include <string>
#include <utility>
#include <vector>

#include "esphome/core/hal.h"
#include "esphome/core/optional.h"

namespace esphome {

// Same output as ESPHome: lowercase, no separators.
inline std::string format_hex(const uint8_t *data, size_t length) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  out.reserve(length * 2);
  for (size_t i = 0; i < length; i++) {
    out += digits[data[i] >> 4];
    out += digits[data[i] & 0x0F];
  }
  return out;
}
inline std::string format_hex(const std::vector<uint8_t> &data) {
  return format_hex(data.data(), data.size());
}

template <typename T> class CallbackManager;
template <typename... Ts> class CallbackManager<void(Ts...)> {
public:
  void add(std::function<void(Ts...)> &&callback) {
    this->callbacks_.push_back(std::move(callback));
  }
  void call(Ts... args) {
    for (auto &cb : this->callbacks_)
      cb(args...);
  }
  void operator()(Ts... args) { this->call(args...); }
  size_t size() const { return this->callbacks_.size(); }

protected:
  std::vector<std::function<void(Ts...)>> callbacks_;
};

template <typename T> class Parented {
public:
  Parented() = default;
  Parented(T *parent) : parent_(parent) {}
  T *get_parent() const { return this->parent_; }
  void set_parent(T *parent) { this->parent_ = parent; }

protected:
  T *parent_{nullptr};
};

} // namespace esphome
