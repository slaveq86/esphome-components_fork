// Test stub for esphome/core/gpio.h. Tests use sim/fake_pin.h implementations.
#pragma once
#include <cstdint>

namespace esphome {
namespace gpio {
enum InterruptType : uint8_t {
  INTERRUPT_RISING_EDGE = 1,
  INTERRUPT_FALLING_EDGE = 2,
  INTERRUPT_ANY_EDGE = 3,
  INTERRUPT_LOW_LEVEL = 4,
  INTERRUPT_HIGH_LEVEL = 5,
};
} // namespace gpio

class GPIOPin {
public:
  virtual ~GPIOPin() = default;
  virtual void setup() = 0;
  virtual bool digital_read() = 0;
  virtual void digital_write(bool value) = 0;
};

class InternalGPIOPin : public GPIOPin {
public:
  template <typename T>
  void attach_interrupt(void (*func)(T *), T *arg,
                        gpio::InterruptType type) const {
    this->attach_interrupt_(reinterpret_cast<void (*)(void *)>(func), arg,
                            type);
  }

protected:
  virtual void attach_interrupt_(void (*func)(void *), void *arg,
                                 gpio::InterruptType type) const = 0;
};
} // namespace esphome
