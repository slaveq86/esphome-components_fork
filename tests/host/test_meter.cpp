// wmbus_meter::Meter: driver creation, link-mode filter, id matching,
// deferred on_telegram, field lookup and the sensor/text_sensor platforms.
#include "esphome/components/wmbus_meter/sensor/sensor.h"
#include "esphome/components/wmbus_meter/text_sensor/text_sensor.h"
#include "esphome/components/wmbus_meter/wmbus_meter.h"
#include "sim/wmbus_encode.h"
#include "test.h"
#include "vectors.h"

using esphome::testing::run_deferred;
using WMeter = esphome::wmbus_meter::Meter;
using esphome::wmbus_radio::Frame;
using esphome::wmbus_radio::Packet;

namespace {
// Radio::add_frame_handler is the only thing Meter::set_radio needs; this
// Radio is never set up, the test calls the handlers itself.
struct TestRadio : esphome::wmbus_radio::Radio {
  void deliver(Frame *frame) {
    for (auto &handler : this->handlers_)
      handler(frame);
  }
};

// A decoded Frame for `telegram`, received in `mode`.
Frame make_frame(const sim::Bytes &telegram, LinkMode mode) {
  sim::Bytes air = mode == LinkMode::C1 ? sim::c1_on_air(telegram, 'A')
                                        : sim::t1_on_air(telegram);
  auto *packet = new Packet();
  size_t got = 0;
  auto fill = [&]() {
    uint8_t *dst = packet->rx_data_ptr();
    size_t n = packet->rx_capacity();
    for (size_t i = 0; i < n; i++, got++)
      dst[i] = got < air.size() ? air[got] : 0;
  };
  fill();
  packet->calculate_payload_size();
  fill();
  packet->set_rssi(-71);
  auto frame = packet->convert_to_frame();
  if (!frame)
    throw std::runtime_error("test telegram did not decode");
  return std::move(*frame);
}

struct Rig {
  TestRadio radio;
  WMeter meter;
  Rig(const std::string &id, const std::string &driver,
      std::initializer_list<LinkMode> modes, const std::string &key = "") {
    this->meter.set_meter_params(id, driver, key, modes);
    this->meter.set_radio(&this->radio);
    this->meter.setup();
  }
};
} // namespace

TEST(meter_izar_created) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  EXPECT(!rig.meter.is_failed());
  EXPECT_EQ(rig.meter.get_driver(), std::string("izar"));
  EXPECT_EQ(rig.meter.get_id(), std::string(test_vectors::IZAR_1_ID));
  EXPECT_EQ(rig.meter.get_key(), std::string("not-encrypted"));
}

TEST(meter_unknown_driver_fails_setup) {
  Rig rig("12345678", "no_such_driver", {LinkMode::T1});
  EXPECT(rig.meter.is_failed());
  rig.meter.dump_config(); // must survive a null meter
  EXPECT(esphome::testing::log_contains('E', "NOT AVAILABLE"));
  // A frame for a failed meter is ignored, not a crash.
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  EXPECT_EQ(frame.handlers_count(), uint8_t(0));
}

TEST(meter_decodes_izar_on_telegram) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  int calls = 0;
  std::optional<float> total, battery, rssi;
  std::optional<std::string> alarms;
  rig.meter.on_telegram([&]() {
    calls++;
    total = rig.meter.get_numeric_field("total_m3");
    battery = rig.meter.get_numeric_field("remaining_battery_life_y");
    rssi = rig.meter.get_numeric_field("rssi_dbm");
    alarms = rig.meter.get_string_field("current_alarms");
  });

  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  EXPECT_EQ(frame.handlers_count(), uint8_t(1));
  EXPECT_EQ(calls, 0); // on_telegram is deferred to the main loop
  EXPECT_EQ(run_deferred(), size_t(1));
  EXPECT_EQ(calls, 1);

  REQUIRE(total.has_value());
  EXPECT_NEAR(*total, 3.488, 1e-6);
  REQUIRE(battery.has_value());
  EXPECT_NEAR(*battery, 14.5, 1e-6);
  REQUIRE(rssi.has_value());
  EXPECT_NEAR(*rssi, -71, 1e-6);
  REQUIRE(alarms.has_value());
  EXPECT_EQ(*alarms, std::string("meter_blocked,underflow"));
}

TEST(meter_values_persist_after_telegram) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  run_deferred();
  // The last telegram is dropped after the callbacks; meter values stay.
  EXPECT(!rig.meter.get_numeric_field("rssi_dbm").has_value());
  auto total = rig.meter.get_numeric_field("total_m3");
  REQUIRE(total.has_value());
  EXPECT_NEAR(*total, 3.488, 1e-6);
  EXPECT(!rig.meter.get_numeric_field("no_such_field_m3").has_value());
}

// Numeric values are stored under the driver's display unit and looked up by
// the requested unit before any conversion, so only that unit works:
// `total_l` never publishes for a driver that reports `total_m3`.
TEST(meter_field_unit_must_match_driver) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  run_deferred();
  EXPECT(rig.meter.get_numeric_field("total_m3").has_value());
  EXPECT(!rig.meter.get_numeric_field("total_l").has_value());
}

TEST(meter_ignores_other_ids) {
  Rig rig("11111111", "izar", {LinkMode::T1});
  int calls = 0;
  rig.meter.on_telegram([&]() { calls++; });
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  run_deferred();
  EXPECT_EQ(calls, 0);
  EXPECT_EQ(frame.handlers_count(), uint8_t(0));
}

TEST(meter_link_mode_filter) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  int calls = 0;
  rig.meter.on_telegram([&]() { calls++; });
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::C1);
  rig.radio.deliver(&frame);
  run_deferred();
  EXPECT_EQ(calls, 0);
  EXPECT(esphome::testing::log_contains('W', "not supported by meter"));
}

TEST(meter_two_meters_one_radio) {
  TestRadio radio;
  WMeter a, b;
  a.set_meter_params(test_vectors::IZAR_1_ID, "izar", "", {LinkMode::T1});
  b.set_meter_params(test_vectors::IZAR_2_ID, "izar", "", {LinkMode::T1});
  a.set_radio(&radio);
  b.set_radio(&radio);
  int calls_a = 0, calls_b = 0;
  a.on_telegram([&]() { calls_a++; });
  b.on_telegram([&]() { calls_b++; });

  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_2), LinkMode::T1);
  radio.deliver(&frame);
  run_deferred();
  EXPECT_EQ(calls_a, 0);
  EXPECT_EQ(calls_b, 1);
  auto total = b.get_numeric_field("total_m3");
  REQUIRE(total.has_value());
  EXPECT_NEAR(*total, 16.76, 1e-6);
}

TEST(meter_sensor_platforms_publish) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  esphome::wmbus_meter::Sensor total;
  total.set_field_name("total_m3");
  total.set_parent(&rig.meter);
  esphome::wmbus_meter::Sensor missing;
  missing.set_field_name("no_such_field_m3");
  missing.set_parent(&rig.meter);
  esphome::wmbus_meter::TextSensor alarms;
  alarms.set_field_name("current_alarms");
  alarms.set_parent(&rig.meter);

  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  run_deferred();

  REQUIRE(total.published.size() == 1);
  EXPECT_NEAR(total.published[0], 3.488, 1e-6);
  EXPECT(missing.published.empty()); // a wrong field name never publishes
  REQUIRE(alarms.published.size() == 1);
  EXPECT_EQ(alarms.published[0], std::string("meter_blocked,underflow"));
}

TEST(meter_json) {
  Rig rig(test_vectors::IZAR_1_ID, "izar", {LinkMode::T1});
  std::string json;
  rig.meter.on_telegram([&]() { json = rig.meter.as_json(); });
  auto frame = make_frame(sim::from_hex(test_vectors::IZAR_1), LinkMode::T1);
  rig.radio.deliver(&frame);
  run_deferred();
  EXPECT(json.find("\"total_m3\":3.488") != std::string::npos);
  EXPECT(json.find("\"id\":\"21242472\"") != std::string::npos);
}
