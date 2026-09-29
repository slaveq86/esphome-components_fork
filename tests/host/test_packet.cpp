// wmbus_radio/packet: length detection from the L-field, T1/C1 handling,
// CRC checking and Frame formatting.
#include <cstring>

#include "esphome/components/wmbus_radio/packet.h"
#include "sim/wmbus_encode.h"
#include "test.h"
#include "vectors.h"

using esphome::wmbus_radio::Frame;
using esphome::wmbus_radio::Packet;
using sim::Bytes;

namespace {
// Feeds on-air bytes through a Packet the way Radio::receive_frame does:
// first 3 bytes, then the rest once the expected size is known. Missing bytes
// (a short read) are zeros, as in the radio buffer.
std::optional<Frame> receive(const Bytes &on_air, size_t *expected = nullptr) {
  auto *packet = new Packet();
  size_t got = 0;
  auto fill = [&]() {
    uint8_t *dst = packet->rx_data_ptr();
    size_t n = packet->rx_capacity();
    for (size_t i = 0; i < n; i++, got++)
      dst[i] = got < on_air.size() ? on_air[got] : 0;
  };
  fill();
  bool sized = packet->calculate_payload_size();
  fill();
  if (expected != nullptr)
    *expected = got;
  if (!sized) {
    delete packet;
    return {};
  }
  packet->set_rssi(-60);
  return packet->convert_to_frame(); // deletes the packet
}

// A syntactically valid telegram with L-field `l` (manufacturer DME, id
// 12345678, CI 0x7A, zero payload).
Bytes synthetic_telegram(uint8_t l) {
  Bytes t = {l, 0x44, 0xA5, 0x11, 0x78, 0x56, 0x34, 0x12, 0x01, 0x07, 0x7A};
  t.resize(size_t(l) + 1, 0x00);
  return t;
}
} // namespace

TEST(packet_t1_izar_frame) {
  Bytes telegram = sim::from_hex(test_vectors::IZAR_1);
  size_t expected = 0;
  auto frame = receive(sim::t1_on_air(telegram), &expected);
  EXPECT_EQ(expected, sim::t1_on_air(telegram).size());
  REQUIRE(frame.has_value());
  EXPECT(frame->data() == telegram);
  EXPECT_EQ(frame->link_mode(), LinkMode::T1);
  EXPECT_EQ(frame->format(), std::string("A"));
  EXPECT_EQ(frame->rssi(), int8_t(-60));
}

TEST(packet_c1_format_a) {
  Bytes telegram = sim::from_hex(test_vectors::IZAR_1);
  auto frame = receive(sim::c1_on_air(telegram, 'A'));
  REQUIRE(frame.has_value());
  EXPECT(frame->data() == telegram);
  EXPECT_EQ(frame->link_mode(), LinkMode::C1);
  EXPECT_EQ(frame->format(), std::string("A"));
}

TEST(packet_c1_format_b) {
  Bytes telegram = sim::from_hex(test_vectors::IZAR_1);
  auto frame = receive(sim::c1_on_air(telegram, 'B'));
  REQUIRE(frame.has_value());
  EXPECT(frame->data() == telegram);
  EXPECT_EQ(frame->format(), std::string("B"));
}

TEST(packet_all_lengths_round_trip) {
  // Covers the block-count formula at every boundary (L = 25/26, 41/42, ...).
  for (int l = 10; l <= 255; l++) {
    Bytes telegram = synthetic_telegram(uint8_t(l));
    auto t1 = receive(sim::t1_on_air(telegram));
    if (!t1 || t1->data() != telegram)
      wmbus_test::fail(__FILE__, __LINE__, "T1 L=" + std::to_string(l));
    auto c1a = receive(sim::c1_on_air(telegram, 'A'));
    if (!c1a || c1a->data() != telegram)
      wmbus_test::fail(__FILE__, __LINE__, "C1/A L=" + std::to_string(l));
    // Format B's L-field includes the CRCs, so it must still fit in a byte.
    if (telegram.size() + 4 <= 256) {
      auto c1b = receive(sim::c1_on_air(telegram, 'B'));
      if (!c1b || c1b->data() != telegram)
        wmbus_test::fail(__FILE__, __LINE__, "C1/B L=" + std::to_string(l));
    }
  }
}

TEST(packet_crc_error_rejected) {
  Bytes telegram = sim::from_hex(test_vectors::IZAR_1);
  for (size_t pos : {3u, 11u, 20u}) { // first block, its CRC, second block
    Bytes frame = sim::add_crcs_format_a(telegram);
    frame[pos] ^= 0x01;
    EXPECT(!receive(sim::encode_3of6(frame)).has_value());
  }
  Bytes c1 = sim::c1_on_air(telegram, 'B');
  c1.back() ^= 0x80;
  EXPECT(!receive(c1).has_value());
}

TEST(packet_invalid_3of6_rejected) {
  Bytes air = sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1));
  air[10] = 0x00; // not a valid 3of6 symbol
  EXPECT(!receive(air).has_value());
}

TEST(packet_noise_rejected) {
  // Random bytes after a (false) sync word detection.
  uint32_t x = 12345;
  for (int round = 0; round < 500; round++) {
    Bytes air(64);
    for (auto &b : air) {
      x = x * 1103515245 + 12345;
      b = uint8_t(x >> 16);
    }
    receive(air); // must not crash or trip ASan; result is irrelevant
  }
  EXPECT(true);
}

TEST(packet_truncated_air_rejected) {
  // The radio delivered fewer bytes than the L-field promises (zeros follow).
  Bytes air = sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1));
  air.resize(air.size() / 2);
  EXPECT(!receive(air).has_value());
}

TEST(packet_c1_unknown_block_byte) {
  // 0x54 selects C1, but the second byte is neither 0xCD (A) nor 0x3D (B).
  Bytes air = sim::c1_on_air(sim::from_hex(test_vectors::IZAR_1), 'A');
  air[1] = 0x00;
  EXPECT(!receive(air).has_value());
}

TEST(frame_as_hex_and_rtlwmbus) {
  Bytes telegram = sim::from_hex(test_vectors::IZAR_1);
  auto frame = receive(sim::t1_on_air(telegram));
  REQUIRE(frame.has_value());
  EXPECT_EQ(frame->as_hex(), sim::to_hex(telegram));
  EXPECT(frame->as_raw() == telegram);

  std::string rtl = frame->as_rtlwmbus();
  // T1;1;1;YYYY-MM-DD HH:MM:SS.00Z;<rssi>;;;0x<hex>\n
  EXPECT_EQ(rtl.substr(0, 7), std::string("T1;1;1;"));
  EXPECT_EQ(rtl.substr(7 + 23, 1), std::string(";"));
  std::string tail = ";-60;;;0x" + sim::to_hex(telegram) + "\n";
  EXPECT_EQ(rtl.substr(rtl.size() - tail.size()), tail);
}

TEST(frame_handled_counter) {
  auto frame = receive(sim::t1_on_air(sim::from_hex(test_vectors::IZAR_1)));
  REQUIRE(frame.has_value());
  EXPECT_EQ(frame->handlers_count(), uint8_t(0));
  frame->mark_as_handled();
  frame->mark_as_handled();
  EXPECT_EQ(frame->handlers_count(), uint8_t(2));
}
