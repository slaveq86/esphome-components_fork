// A GPIO pin for the simulation: tests set its input level, the driver's
// writes are logged, and an attached interrupt fires on the matching edge.
#pragma once
#include <vector>

#include "esphome/core/gpio.h"

namespace sim {
class FakePin : public esphome::InternalGPIOPin {
public:
  void setup() override { this->setup_calls++; }
  bool digital_read() override { return this->level_; }
  void digital_write(bool value) override {
    this->writes.push_back(value);
    this->level_ = value;
  }

  // Drive the pin from "outside" (the radio chip), firing the ISR on an edge.
  void set_level(bool level) {
    bool rising = !this->level_ && level;
    bool falling = this->level_ && !level;
    this->level_ = level;
    if (this->isr_ == nullptr)
      return;
    using esphome::gpio::InterruptType;
    if ((rising && (this->type_ == InterruptType::INTERRUPT_RISING_EDGE ||
                    this->type_ == InterruptType::INTERRUPT_ANY_EDGE)) ||
        (falling && (this->type_ == InterruptType::INTERRUPT_FALLING_EDGE ||
                     this->type_ == InterruptType::INTERRUPT_ANY_EDGE)))
      this->isr_(this->isr_arg_);
  }

  bool interrupt_attached() const { return this->isr_ != nullptr; }
  esphome::gpio::InterruptType interrupt_type() const { return this->type_; }

  int setup_calls = 0;
  std::vector<bool> writes;

protected:
  void attach_interrupt_(void (*func)(void *), void *arg,
                         esphome::gpio::InterruptType type) const override {
    this->isr_ = func;
    this->isr_arg_ = arg;
    this->type_ = type;
  }

  bool level_{false};
  mutable void (*isr_)(void *){nullptr};
  mutable void *isr_arg_{nullptr};
  mutable esphome::gpio::InterruptType type_{};
};
} // namespace sim
