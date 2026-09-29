#include "fake_sx1262.h"

#include <algorithm>

#include "esphome/core/hal.h"

namespace sim {
using namespace sx1262;

void FakeSX1262::begin_transaction() {
  if (this->in_transaction_)
    this->protocol_errors.push_back("nested SPI transaction");
  if (this->busy.digital_read())
    this->protocol_errors.push_back("command sent while BUSY is high");
  this->in_transaction_ = true;
  this->current_.clear();
}

uint8_t FakeSX1262::transfer(uint8_t data) {
  if (!this->in_transaction_)
    this->protocol_errors.push_back("SPI transfer outside a transaction");
  size_t pos = this->current_.size();
  this->current_.push_back(data);
  return this->response(pos);
}

// Byte the chip shifts out while byte `pos` of the command is shifted in.
uint8_t FakeSX1262::response(size_t pos) {
  const uint8_t status = this->in_rx_ ? 0x52 : 0x22; // chip mode bits only
  if (pos == 0)
    return status;
  switch (this->current_[0]) {
  case READ_BUFFER: // opcode, offset, NOP, data...
    if (pos >= 3) {
      size_t addr = (this->current_[1] + (pos - 3)) & 0xFF;
      return this->buffer_[addr];
    }
    return status;
  case GET_IRQ_STATUS: // opcode, status, irq[15:8], irq[7:0]
    if (pos == 2)
      return this->irq_ >> 8;
    if (pos == 3)
      return this->irq_ & 0xFF;
    return status;
  case GET_PACKET_STATUS: // opcode, status, RxStatus, RssiSync, RssiAvg
    if (pos == 2)
      return 0x00;
    if (pos == 3 || pos == 4)
      return this->rssi_raw_;
    return status;
  default:
    return status;
  }
}

void FakeSX1262::end_transaction() {
  if (!this->in_transaction_)
    this->protocol_errors.push_back("end_transaction without begin");
  this->in_transaction_ = false;
  if (this->current_.empty())
    return;

  SpiCommand cmd{this->current_[0],
                 {this->current_.begin() + 1, this->current_.end()}};
  const auto &p = cmd.params;
  switch (cmd.opcode) {
  case SET_STANDBY:
    this->in_rx_ = false;
    // Leaving RX aborts a packet that is being received.
    for (size_t i = 0; i < this->pending_.size();) {
      if (this->pending_[i].sync_fired) {
        this->lost_packets++;
        this->pending_.erase(this->pending_.begin() + i);
      } else {
        i++;
      }
    }
    break;
  case SET_PACKET_PARAMS:
    if (p.size() >= 7)
      this->payload_length_ = p[6];
    break;
  case SET_RX:
    this->in_rx_ = true;
    break;
  case SET_DIO_IRQ_PARAMS:
    if (p.size() >= 4)
      this->dio1_mask_ = uint16_t(p[2] << 8 | p[3]);
    this->update_dio1();
    break;
  case CLEAR_IRQ_STATUS:
    if (p.size() >= 2)
      this->irq_ &= ~uint16_t(p[0] << 8 | p[1]);
    this->update_dio1();
    break;
  default:
    break;
  }
  this->commands.push_back(std::move(cmd));

  // BUSY is high while the chip processes a command; model it as released
  // immediately unless the chip is "dead".
  this->busy.set_level(this->busy_stuck);
}

std::vector<const SpiCommand *> FakeSX1262::find(uint8_t opcode) const {
  std::vector<const SpiCommand *> out;
  for (const auto &cmd : this->commands)
    if (cmd.opcode == opcode)
      out.push_back(&cmd);
  return out;
}

const SpiCommand *FakeSX1262::last(uint8_t opcode) const {
  for (auto it = this->commands.rbegin(); it != this->commands.rend(); ++it)
    if (it->opcode == opcode)
      return &*it;
  return nullptr;
}

int FakeSX1262::index_of(uint8_t opcode) const {
  for (size_t i = 0; i < this->commands.size(); i++)
    if (this->commands[i].opcode == opcode)
      return int(i);
  return -1;
}

void FakeSX1262::receive(const std::vector<uint8_t> &on_air,
                         uint8_t rssi_raw) {
  std::fill(this->buffer_.begin(), this->buffer_.end(), 0);
  std::copy_n(on_air.begin(), std::min<size_t>(on_air.size(), 256),
              this->buffer_.begin());
  this->rssi_raw_ = rssi_raw;
  this->set_irq(IRQ_RX_DONE);
}

void FakeSX1262::transmit(const std::vector<uint8_t> &on_air,
                          uint64_t delay_us, uint8_t rssi_raw) {
  uint64_t sync_at = esphome::testing::fake_micros() + delay_us;
  uint64_t airtime_us = uint64_t(this->payload_length_) * 8 * 10; // 100 kbps
  this->pending_.push_back({on_air, sync_at, sync_at + airtime_us, rssi_raw,
                            false});
  this->on_time();
}

void FakeSX1262::attach_clock() {
  esphome::testing::time_listener() = [this]() { this->on_time(); };
}

void FakeSX1262::on_time() {
  uint64_t now = esphome::testing::fake_micros();
  for (size_t i = 0; i < this->pending_.size();) {
    Pending &p = this->pending_[i];
    if (!this->in_rx_) { // not listening when the packet started
      if (!p.sync_fired && now >= p.sync_at) {
        this->lost_packets++;
        this->pending_.erase(this->pending_.begin() + i);
        continue;
      }
      i++;
      continue;
    }
    if (!p.sync_fired && now >= p.sync_at) {
      p.sync_fired = true;
      this->set_irq(IRQ_SYNC_WORD_VALID);
    }
    if (p.sync_fired && now >= p.done_at) {
      Pending done = std::move(p);
      this->pending_.erase(this->pending_.begin() + i);
      this->in_rx_ = false; // single mode: back to standby after RX_DONE
      this->receive(done.on_air, done.rssi_raw);
      continue;
    }
    i++;
  }
}

void FakeSX1262::sync_word_detected() { this->set_irq(IRQ_SYNC_WORD_VALID); }

void FakeSX1262::set_irq(uint16_t bits) {
  this->irq_ |= bits;
  this->update_dio1();
}

void FakeSX1262::update_dio1() {
  this->dio1.set_level((this->irq_ & this->dio1_mask_) != 0);
}
} // namespace sim
