// End to end without hardware: simulated SX1262 -> DIO1 interrupt -> RX task
// (Radio::receive_frame) -> queue -> Radio::loop -> wmbus_meter -> sensor.
// The test plays the RX task and the main loop by calling receive_frame(),
// loop() and run_deferred() itself.
//
// These also guard receive_frame's buffer handling: x86-64 GCC evaluates
// function arguments right to left, so passing packet->rx_data_ptr() and
// packet->rx_capacity() (which resizes) in one call would overflow under ASan.
#include "esphome/components/wmbus_meter/sensor/sensor.h"
#include "esphome/components/wmbus_meter/wmbus_meter.h"
#include "esphome/components/wmbus_radio/component.h"
#include "esphome/components/wmbus_radio/transceiver_sx1262.h"
#include "freertos/task.h"
#include "sim/fake_sx1262.h"
#include "sim/wmbus_encode.h"
#include "test.h"
#include "vectors.h"

using esphome::testing::log_contains;
using esphome::testing::run_deferred;

namespace {
struct Pipeline {
  sim::FakeSX1262 chip;
  esphome::wmbus_radio::SX1262 sx;
  esphome::wmbus_radio::Radio radio;
  esphome::wmbus_meter::Meter meter;
  esphome::wmbus_meter::Sensor total;

  explicit Pipeline(bool ultra_low_latency = false) {
    freertos_fake::current_task() = nullptr;
    this->sx.set_spi_delegate(&this->chip);
    this->sx.set_reset_pin(&this->chip.reset);
    this->sx.set_irq_pin(&this->chip.dio1);
    this->sx.set_busy_pin(&this->chip.busy);
    this->sx.set_rf_switch(true);
    this->sx.set_tcxo_voltage(0x02);
    if (ultra_low_latency)
      this->sx.set_sync_mode("SYNC_MODE_ULTRA_LOW_LATENCY");
    this->sx.setup();
    this->chip.attach_clock();

    this->radio.set_radio(&this->sx);
    this->radio.setup();

    this->meter.set_meter_params(test_vectors::IZAR_1_ID, "izar", "",
                                 {LinkMode::T1});
    this->meter.set_radio(&this->radio);
    this->meter.setup();
    this->total.set_field_name("total_m3");
    this->total.set_parent(&this->meter);
  }
  ~Pipeline() { esphome::testing::time_listener() = nullptr; }

  // Main loop: handle everything queued, then deferred callbacks.
  void main_loop() {
    for (int i = 0; i < 5; i++)
      this->radio.loop();
    run_deferred();
  }
};

sim::Bytes izar1_air() {
  return sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1));
}
} // namespace

TEST(pipeline_setup_creates_rx_task) {
  Pipeline p;
  EXPECT(!p.radio.is_failed());
  FakeTask *task = freertos_fake::current_task();
  REQUIRE(task != nullptr);
  EXPECT_EQ(task->name, std::string("radio_recv"));
  EXPECT_EQ(task->stack, uint32_t(8 * 1024));
  EXPECT_EQ(task->priority, UBaseType_t(24));
  EXPECT_EQ(task->core, 1);
  EXPECT(p.chip.dio1.interrupt_attached());
}

TEST(pipeline_t1_izar_to_sensor) {
  Pipeline p;
  p.chip.receive(izar1_air(), 142); // DIO1 rises, ISR notifies the RX task
  EXPECT_EQ(freertos_fake::current_task()->notifications, uint32_t(1));

  p.radio.receive_frame();
  EXPECT(p.chip.in_rx()); // re-armed for the next packet
  p.main_loop();

  REQUIRE(p.total.published.size() == 1);
  EXPECT_NEAR(p.total.published[0], 3.488, 1e-6);
  EXPECT(!log_contains('W', "not handled"));
}

TEST(pipeline_real_airtime) {
  // The sync word arrives 5 ms from now; RX_DONE follows after the fixed
  // 255-byte payload (~20.4 ms at 100 kbps). The RX task blocks until then.
  Pipeline p;
  uint64_t start = esphome::testing::fake_micros();
  p.chip.transmit(izar1_air(), 5000, 142);
  p.radio.receive_frame();
  uint64_t elapsed = esphome::testing::fake_micros() - start;
  EXPECT(elapsed >= 25000);
  EXPECT_EQ(p.chip.lost_packets, 0);
  p.main_loop();
  REQUIRE(p.total.published.size() == 1);
  EXPECT_NEAR(p.total.published[0], 3.488, 1e-6);
}

TEST(pipeline_unknown_meter_logs_analyze_link) {
  Pipeline p;
  sim::Bytes other = sim::from_hex(test_vectors::IZAR_2);
  p.chip.transmit(izar1_air(), 1000);
  p.chip.transmit(sim::t1_on_air(other), 40000);
  p.radio.receive_frame();
  p.radio.receive_frame();
  p.main_loop();
  EXPECT_EQ(p.chip.lost_packets, 0);
  EXPECT_EQ(p.total.published.size(), size_t(1));
  EXPECT(log_contains('W', "https://wmbusmeters.org/analyze/" +
                               sim::to_hex(other)));
}

TEST(pipeline_idle_timeout_restarts_rx) {
  Pipeline p;
  size_t before = p.chip.commands.size();
  p.radio.receive_frame(); // nothing on air: 60 s wait, then restart_rx
  EXPECT_EQ(freertos_fake::current_task()->timeouts, uint32_t(1));
  REQUIRE(p.chip.commands.size() > before);
  EXPECT_EQ(p.chip.commands.back().opcode, uint8_t(sim::sx1262::SET_RX));
  p.main_loop();
  EXPECT(p.total.published.empty());
}

TEST(pipeline_queue_full_drops_packet) {
  // The main loop is stuck; the RX task queues 3 packets, the 4th is dropped.
  Pipeline p;
  for (int i = 0; i < 4; i++) {
    p.chip.receive(izar1_air());
    p.radio.receive_frame();
  }
  EXPECT(log_contains('W', "Queue send failed"));
  p.main_loop();
  EXPECT_EQ(p.total.published.size(), size_t(3));
}

TEST(pipeline_corrupted_packet_dropped) {
  Pipeline p;
  sim::Bytes frame =
      sim::add_crcs_format_a(sim::from_hex(test_vectors::IZAR_1));
  frame[15] ^= 0x10;
  p.chip.receive(sim::encode_3of6(frame));
  p.radio.receive_frame();
  p.main_loop();
  EXPECT(p.total.published.empty());
  EXPECT(!log_contains('W', "analyze"));
  // The radio keeps working afterwards.
  p.chip.receive(izar1_air());
  p.radio.receive_frame();
  p.main_loop();
  EXPECT_EQ(p.total.published.size(), size_t(1));
}

// In ULTRA_LOW_LATENCY mode SYNC_WORD_VALID wakes the RX task at the start of
// the packet. get_frame() sees no RX_DONE yet, and read_in_task() then waits
// only one tick (1 ms) for the next interrupt before giving up and calling
// restart_rx(). RX_DONE comes ~20 ms later (fixed 255-byte payload), so the
// packet is aborted.
KNOWN_BUG(pipeline_ultra_low_latency_receives_packet,
          "sync_mode ULTRA_LOW_LATENCY drops packets (1-tick wait for RX_DONE)") {
  Pipeline p(true);
  p.chip.transmit(izar1_air(), 5000);
  p.radio.receive_frame();
  if (p.total.published.empty()) // a second pass, as the RX task would loop
    p.radio.receive_frame();
  p.main_loop();
  EXPECT_EQ(p.chip.lost_packets, 0);
  EXPECT_EQ(p.total.published.size(), size_t(1));
}
