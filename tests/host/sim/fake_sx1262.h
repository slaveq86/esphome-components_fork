// SPI-level model of a Semtech SX1262, enough to exercise the wmbus_radio
// driver: it logs every command, keeps the RX buffer and IRQ status, and
// drives the BUSY and DIO1 pins. Opcodes come from the SX1261/2 datasheet
// (rev 2.1, section 11), not from the driver's header, so a wrong constant in
// the driver shows up as a test failure.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

#include "esphome/components/spi/spi.h"
#include "fake_pin.h"

namespace sim {
namespace sx1262 {
enum Opcode : uint8_t {
  CLEAR_IRQ_STATUS = 0x02,
  SET_DIO_IRQ_PARAMS = 0x08,
  WRITE_REGISTER = 0x0D,
  GET_IRQ_STATUS = 0x12,
  GET_PACKET_STATUS = 0x14,
  READ_BUFFER = 0x1E,
  SET_STANDBY = 0x80,
  SET_RX = 0x82,
  SET_RF_FREQUENCY = 0x86,
  SET_PACKET_TYPE = 0x8A,
  SET_MODULATION_PARAMS = 0x8B,
  SET_PACKET_PARAMS = 0x8C,
  SET_BUFFER_BASE_ADDRESS = 0x8F,
  CALIBRATE = 0x89,
  SET_RX_TX_FALLBACK_MODE = 0x93,
  SET_DIO3_AS_TCXO_CTRL = 0x97,
  CALIBRATE_IMAGE = 0x98,
  SET_DIO2_AS_RF_SWITCH_CTRL = 0x9D,
};
enum Irq : uint16_t {
  IRQ_RX_DONE = 1 << 1,
  IRQ_SYNC_WORD_VALID = 1 << 3,
};
enum Register : uint16_t {
  REG_SYNC_WORD_0 = 0x06C0,
  REG_RX_GAIN = 0x08AC,
};
} // namespace sx1262

struct SpiCommand {
  uint8_t opcode;
  std::vector<uint8_t> params; // bytes after the opcode, as sent by the MCU
};

class FakeSX1262 : public esphome::spi::SPIDelegate {
public:
  FakePin busy;
  FakePin dio1;
  FakePin reset;

  // --- SPIDelegate ---
  void begin_transaction() override;
  uint8_t transfer(uint8_t data) override;
  void end_transaction() override;

  // --- what the driver did ---
  std::vector<SpiCommand> commands;
  std::vector<const SpiCommand *> find(uint8_t opcode) const;
  const SpiCommand *last(uint8_t opcode) const;
  int index_of(uint8_t opcode) const; // first occurrence, -1 if none
  bool in_rx() const { return this->in_rx_; }
  uint16_t dio1_mask() const { return this->dio1_mask_; }

  // --- the air side ---
  // A packet arrives: fill the RX buffer, raise RX_DONE (and DIO1 if enabled).
  void receive(const std::vector<uint8_t> &on_air, uint8_t rssi_raw = 120);
  // Only the sync word was seen so far (ultra-low-latency mode).
  void sync_word_detected();
  // Real timing: a packet whose sync word ends `delay_us` from now. The chip
  // raises SYNC_WORD_VALID then, and RX_DONE once the configured fixed
  // payload length (SetPacketParams, 255 here) has been received at 100 kbps.
  // Needs attach_clock(); a packet is lost if the chip leaves RX before that.
  void transmit(const std::vector<uint8_t> &on_air, uint64_t delay_us = 0,
                uint8_t rssi_raw = 120);
  void attach_clock(); // listen to esphome::testing's fake clock
  void on_time();
  uint32_t payload_length() const { return this->payload_length_; }
  int lost_packets = 0;
  void set_irq(uint16_t bits);
  uint16_t irq_status() const { return this->irq_; }

  // SPI protocol violations by the driver (should stay empty).
  std::vector<std::string> protocol_errors;

  // Keep BUSY high forever (a dead or unpowered chip).
  bool busy_stuck = false;

protected:
  void update_dio1();
  uint8_t response(size_t pos);

  std::vector<uint8_t> current_;
  bool in_transaction_ = false;
  std::vector<uint8_t> buffer_ = std::vector<uint8_t>(256, 0);
  uint16_t irq_ = 0;
  uint16_t dio1_mask_ = 0;
  bool in_rx_ = false;
  uint8_t rssi_raw_ = 0;
  uint32_t payload_length_ = 255;

  struct Pending {
    std::vector<uint8_t> on_air;
    uint64_t sync_at, done_at;
    uint8_t rssi_raw;
    bool sync_fired;
  };
  std::vector<Pending> pending_;
};
} // namespace sim
