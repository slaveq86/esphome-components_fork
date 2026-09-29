// wmbus_radio/decode3of6: T1 mode's 3-out-of-6 line code.
#include "esphome/components/wmbus_radio/decode3of6.h"
#include "sim/wmbus_encode.h"
#include "test.h"

using esphome::wmbus_radio::decode3of6;
using esphome::wmbus_radio::encoded_size;

// Regression: decode3of6 used to read data[byte_idx + 1] whenever
// bit_offset > 0, one byte past the end for every T1 frame. With an exact-size
// buffer ASan catches that.
TEST(decode3of6_reads_only_its_input) {
  sim::Bytes coded = sim::encode_3of6({0x19, 0x44});
  coded.shrink_to_fit();
  auto decoded = decode3of6(coded);
  REQUIRE(decoded.has_value());
  EXPECT(*decoded == sim::Bytes({0x19, 0x44}));
}

TEST(decode3of6_all_nibbles_round_trip) {
  sim::Bytes data;
  for (int i = 0; i < 256; i++)
    data.push_back(uint8_t(i));
  auto coded = sim::encode_3of6(data);
  auto decoded = decode3of6(coded);
  REQUIRE(decoded.has_value());
  EXPECT(*decoded == data);
}

TEST(decode3of6_odd_length_round_trip) {
  // An odd byte count leaves 4 padding bits in the last coded byte.
  sim::Bytes data = {0x19, 0x44, 0x30};
  auto coded = sim::encode_3of6(data);
  EXPECT_EQ(coded.size(), encoded_size(data.size()));
  auto decoded = decode3of6(coded);
  REQUIRE(decoded.has_value());
  EXPECT(*decoded == data);
}

TEST(decode3of6_known_value) {
  // 0x19 -> nibbles 1, 9 -> codes 001101 100101 -> 0011 0110 0101 = 0x36 0x5x
  sim::Bytes coded = {0x36, 0x50};
  auto decoded = decode3of6(coded);
  REQUIRE(decoded.has_value());
  REQUIRE(decoded->size() >= 1);
  EXPECT_EQ((*decoded)[0], uint8_t(0x19));
}

TEST(decode3of6_invalid_code_rejected) {
  // 000000 is not one of the 16 valid codes.
  sim::Bytes coded = {0x00, 0x00, 0x00};
  EXPECT(!decode3of6(coded).has_value());
  // Valid first symbol, invalid second one.
  coded = {0x58, 0x00, 0x00};
  EXPECT(!decode3of6(coded).has_value());
}

TEST(decode3of6_empty_input) {
  sim::Bytes coded;
  auto decoded = decode3of6(coded);
  REQUIRE(decoded.has_value());
  EXPECT(decoded->empty());
}

TEST(decode3of6_encoded_size) {
  for (size_t n = 0; n < 300; n++)
    EXPECT_EQ(encoded_size(n), sim::encode_3of6(sim::Bytes(n, 0x44)).size());
}
