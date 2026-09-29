// wmbus_radio SX1262 driver against the simulated chip (sim/fake_sx1262).
// Expected register values are worked out from the SX1261/2 datasheet, not
// copied from the driver.
#include <cmath>

#include "esphome/components/wmbus_radio/transceiver_sx1262.h"
#include "sim/fake_sx1262.h"
#include "sim/wmbus_encode.h"
#include "test.h"
#include "vectors.h"

using namespace sim::sx1262;
using esphome::wmbus_radio::SX1262;
using Bytes = std::vector<uint8_t>;

namespace {
struct Rig {
  sim::FakeSX1262 chip;
  SX1262 radio;
  Rig() {
    this->radio.set_spi_delegate(&this->chip);
    this->radio.set_reset_pin(&this->chip.reset);
    this->radio.set_irq_pin(&this->chip.dio1);
    this->radio.set_busy_pin(&this->chip.busy);
  }
  std::vector<uint8_t> opcodes() const {
    std::vector<uint8_t> out;
    for (const auto &c : this->chip.commands)
      out.push_back(c.opcode);
    return out;
  }
  // The WriteRegister command for `reg`, or nullptr.
  const sim::SpiCommand *register_write(uint16_t reg) const {
    for (const auto *c : this->chip.find(WRITE_REGISTER))
      if (c->params.size() >= 2 && (c->params[0] << 8 | c->params[1]) == reg)
        return c;
    return nullptr;
  }
};

std::string hex(const Bytes &b) { return sim::to_hex(b); }
} // namespace

TEST(sx1262_setup_command_sequence) {
  Rig rig;
  rig.radio.setup();
  EXPECT(!rig.radio.is_failed());
  EXPECT(rig.chip.protocol_errors.empty());
  EXPECT(rig.chip.reset.writes == std::vector<bool>({false, true}));

  Bytes expected = {SET_STANDBY,
                    SET_PACKET_TYPE,
                    SET_RF_FREQUENCY,
                    SET_BUFFER_BASE_ADDRESS,
                    SET_MODULATION_PARAMS,
                    SET_PACKET_PARAMS,
                    WRITE_REGISTER, // RX gain
                    SET_DIO_IRQ_PARAMS,
                    WRITE_REGISTER, // sync word
                    SET_DIO3_AS_TCXO_CTRL,
                    CALIBRATE,
                    CALIBRATE_IMAGE,
                    SET_RX_TX_FALLBACK_MODE,
                    SET_STANDBY,
                    SET_RX};
  EXPECT_EQ(hex(rig.opcodes()), hex(expected));
  EXPECT(rig.chip.in_rx());
}

TEST(sx1262_setup_parameters) {
  Rig rig;
  rig.radio.setup();
  auto params = [&](uint8_t op) {
    auto *c = rig.chip.last(op);
    return c ? hex(c->params) : std::string("<missing>");
  };

  EXPECT_EQ(hex(rig.chip.find(SET_STANDBY).front()->params), "00"); // STDBY_RC
  EXPECT_EQ(params(SET_PACKET_TYPE), "00");                            // GFSK

  // RF frequency: Freq = RfFreq * 32 MHz / 2^25; 1 LSB ~ 0.95 Hz.
  auto *rf = rig.chip.last(SET_RF_FREQUENCY);
  REQUIRE(rf != nullptr && rf->params.size() == 4);
  uint32_t frf = rf->params[0] << 24 | rf->params[1] << 16 |
                 rf->params[2] << 8 | rf->params[3];
  double hz = frf * 32e6 / double(1 << 25);
  EXPECT_NEAR(hz, 868.95e6, 100);

  // BR = 32 * Fxtal / bitrate = 10240 (100 kbps); Gaussian filter off;
  // RX bandwidth 234.3 kHz = 0x0A; Fdev = 50 kHz * 2^25 / Fxtal = 52428.
  EXPECT_EQ(params(SET_MODULATION_PARAMS), "002800000a00cccc");

  // Preamble 16 bits, detector 8 bits (0x04), sync word 16 bits, no address
  // filtering, fixed length (0x00), 255 bytes, CRC off (0x01), no whitening.
  EXPECT_EQ(params(SET_PACKET_PARAMS), "001004100000ff0100");

  // IRQ: RX_DONE routed to DIO1.
  EXPECT_EQ(params(SET_DIO_IRQ_PARAMS), "0002000200000000");

  // Sync word 0x543D; with a 16-bit sync length only the first 2 bytes count.
  auto *sync = rig.register_write(REG_SYNC_WORD_0);
  REQUIRE(sync != nullptr);
  EXPECT_EQ(hex(sync->params).substr(0, 8), "06c0543d");

  // Boosted RX gain (0x96).
  auto *gain = rig.register_write(REG_RX_GAIN);
  REQUIRE(gain != nullptr);
  EXPECT_EQ(hex(gain->params), "08ac96");

  // Calibrate all blocks except image, then image for 863-870 MHz.
  EXPECT_EQ(params(CALIBRATE), "3f");
  EXPECT_EQ(params(CALIBRATE_IMAGE), "d7db");
  EXPECT_EQ(params(SET_RX_TX_FALLBACK_MODE), "30"); // STDBY_XOSC
  EXPECT_EQ(hex(rig.chip.find(SET_STANDBY).back()->params), "01");
}

TEST(sx1262_tcxo_default_is_3v0) {
  Rig rig;
  rig.radio.setup();
  auto *tcxo = rig.chip.last(SET_DIO3_AS_TCXO_CTRL);
  REQUIRE(tcxo != nullptr);
  // 0x06 = 3.0 V, then a 24-bit delay of 64 * 15.625 us = 1 ms.
  EXPECT_EQ(hex(tcxo->params), "06000040");
}

TEST(sx1262_tcxo_voltage_is_sent) {
  // Datasheet table 13-35: 1.6, 1.7, 1.8, 2.2, 2.4, 2.7, 3.0, 3.3 V.
  for (uint8_t v = 0; v <= 7; v++) {
    Rig rig;
    rig.radio.set_tcxo_voltage(v);
    rig.radio.setup();
    auto *tcxo = rig.chip.last(SET_DIO3_AS_TCXO_CTRL);
    REQUIRE(tcxo != nullptr);
    EXPECT_EQ(tcxo->params[0], v);
  }
}

TEST(sx1262_tcxo_before_calibration) {
  // Calibration needs the TCXO running, so DIO3 must be set up first.
  Rig rig;
  rig.radio.set_tcxo_voltage(0x02); // Heltec V3/V4: 1.8 V
  rig.radio.setup();
  int tcxo = rig.chip.index_of(SET_DIO3_AS_TCXO_CTRL);
  int cal = rig.chip.index_of(CALIBRATE);
  REQUIRE(tcxo >= 0 && cal >= 0);
  EXPECT(tcxo < cal);
  EXPECT(tcxo < rig.chip.index_of(CALIBRATE_IMAGE));
}

TEST(sx1262_without_tcxo) {
  Rig rig;
  rig.radio.set_tcxo(false);
  rig.radio.set_tcxo_voltage(0x02);
  rig.radio.setup();
  EXPECT(rig.chip.find(SET_DIO3_AS_TCXO_CTRL).empty());
}

TEST(sx1262_rf_switch) {
  Rig off;
  off.radio.setup();
  EXPECT(off.chip.find(SET_DIO2_AS_RF_SWITCH_CTRL).empty());

  Rig on;
  on.radio.set_rf_switch(true);
  on.radio.setup();
  auto *c = on.chip.last(SET_DIO2_AS_RF_SWITCH_CTRL);
  REQUIRE(c != nullptr);
  EXPECT_EQ(hex(c->params), "01");
}

TEST(sx1262_power_saving_gain) {
  Rig rig;
  rig.radio.set_rx_gain_mode("RX_GAIN_POWER_SAVING");
  rig.radio.setup();
  auto *gain = rig.register_write(REG_RX_GAIN);
  REQUIRE(gain != nullptr);
  EXPECT_EQ(hex(gain->params), "08ac94");
}

TEST(sx1262_interrupt_on_rising_edge) {
  Rig rig;
  EXPECT_EQ(rig.radio.get_interrupt_type(),
            esphome::gpio::INTERRUPT_RISING_EDGE);
}

TEST(sx1262_busy_timeout_marks_failed) {
  Rig rig;
  rig.chip.busy_stuck = true;
  rig.chip.busy.set_level(true); // chip never becomes ready
  rig.radio.setup();
  EXPECT(rig.radio.is_failed());
  EXPECT(esphome::testing::log_contains('E', "BUSY pin timeout"));
}

TEST(sx1262_get_frame_reads_buffer) {
  Rig rig;
  rig.radio.setup();
  uint8_t buf[8] = {};
  EXPECT_EQ(rig.radio.get_frame(buf, 3, 0), size_t(0)); // DIO1 low: nothing

  Bytes air = sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1));
  rig.chip.receive(air, 150);
  EXPECT(rig.chip.dio1.digital_read());
  EXPECT_EQ(rig.radio.get_frame(buf, 3, 0), size_t(3));
  EXPECT_EQ(hex(Bytes(buf, buf + 3)), hex(Bytes(air.begin(), air.begin() + 3)));
  EXPECT(rig.chip.dio1.digital_read()); // offset 0: IRQ kept

  size_t before = rig.chip.commands.size();
  EXPECT_EQ(rig.radio.get_frame(buf, 5, 3), size_t(5));
  EXPECT_EQ(hex(Bytes(buf, buf + 5)),
            hex(Bytes(air.begin() + 3, air.begin() + 8)));
  // offset > 0: RX_DONE cleared and RX re-armed.
  EXPECT(!rig.chip.dio1.digital_read());
  EXPECT(rig.chip.in_rx());
  EXPECT(rig.chip.commands.size() > before);

  EXPECT_EQ(rig.radio.get_rssi(), int8_t(-75)); // RssiSync 150 -> -75 dBm
}

TEST(sx1262_restart_rx) {
  Rig rig;
  rig.radio.setup();
  rig.chip.receive({0x00}, 100);
  rig.radio.restart_rx();
  EXPECT(rig.chip.in_rx());
  EXPECT_EQ(rig.chip.irq_status(), uint16_t(0));
  EXPECT(!rig.chip.dio1.digital_read());
}

TEST(sx1262_ultra_low_latency_irq_handling) {
  Rig rig;
  rig.radio.set_sync_mode("SYNC_MODE_ULTRA_LOW_LATENCY");
  rig.radio.setup();
  // RX_DONE | SYNC_WORD_VALID on DIO1.
  EXPECT_EQ(hex(rig.chip.last(SET_DIO_IRQ_PARAMS)->params), "000a000a00000000");

  uint8_t buf[3];
  rig.chip.sync_word_detected();
  EXPECT(rig.chip.dio1.digital_read());
  // Only the sync word so far: nothing to read, SYNC_WORD_VALID cleared so
  // RX_DONE can produce a new rising edge.
  EXPECT_EQ(rig.radio.get_frame(buf, 3, 0), size_t(0));
  EXPECT(!rig.chip.dio1.digital_read());

  rig.chip.receive(sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1)));
  EXPECT_EQ(rig.radio.get_frame(buf, 3, 0), size_t(3));
}
