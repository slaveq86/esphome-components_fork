// Test stub for esphome/components/spi/spi.h. The radio drivers only use
// delegate_->begin_transaction/transfer/end_transaction, so a simulated chip
// implements SPIDelegate and is plugged in with set_spi_delegate().
#pragma once
#include <cstdint>

#include "esphome/core/component.h"

namespace esphome {
namespace spi {
enum SPIBitOrder { BIT_ORDER_LSB_FIRST, BIT_ORDER_MSB_FIRST };
enum SPIClockPolarity { CLOCK_POLARITY_LOW, CLOCK_POLARITY_HIGH };
enum SPIClockPhase { CLOCK_PHASE_LEADING, CLOCK_PHASE_TRAILING };
enum SPIDataRate : uint32_t {
  DATA_RATE_1MHZ = 1000000,
  DATA_RATE_8MHZ = 8000000,
};

class SPIDelegate {
public:
  virtual ~SPIDelegate() = default;
  virtual void begin_transaction() {}
  virtual void end_transaction() {}
  virtual uint8_t transfer(uint8_t data) = 0;
};

template <SPIBitOrder BIT_ORDER, SPIClockPolarity CLOCK_POLARITY,
          SPIClockPhase CLOCK_PHASE, SPIDataRate DATA_RATE>
class SPIDevice {
public:
  void set_spi_delegate(SPIDelegate *delegate) { this->delegate_ = delegate; }
  void spi_setup() {}

protected:
  SPIDelegate *delegate_{nullptr};
};
} // namespace spi
} // namespace esphome
