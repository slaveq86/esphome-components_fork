#include "wmbus_encode.h"

#include <stdexcept>

namespace sim {

Bytes from_hex(const std::string &hex) {
  Bytes out;
  int high = -1;
  for (char c : hex) {
    int v;
    if (c >= '0' && c <= '9')
      v = c - '0';
    else if (c >= 'a' && c <= 'f')
      v = c - 'a' + 10;
    else if (c >= 'A' && c <= 'F')
      v = c - 'A' + 10;
    else
      continue;
    if (high < 0) {
      high = v;
    } else {
      out.push_back(uint8_t(high << 4 | v));
      high = -1;
    }
  }
  if (high >= 0)
    throw std::invalid_argument("odd number of hex digits: " + hex);
  return out;
}

std::string to_hex(const Bytes &bytes) {
  static const char digits[] = "0123456789abcdef";
  std::string out;
  for (uint8_t b : bytes) {
    out += digits[b >> 4];
    out += digits[b & 0x0F];
  }
  return out;
}

// CRC-16, polynomial 0x3D65, initial 0, final complement (EN 13757-4).
uint16_t crc16_en13757(const uint8_t *data, size_t len) {
  uint16_t crc = 0;
  for (size_t i = 0; i < len; i++) {
    crc ^= uint16_t(data[i]) << 8;
    for (int bit = 0; bit < 8; bit++)
      crc = (crc & 0x8000) ? uint16_t(crc << 1) ^ 0x3D65 : uint16_t(crc << 1);
  }
  return ~crc;
}

static void append_block(Bytes &out, const uint8_t *data, size_t len) {
  out.insert(out.end(), data, data + len);
  uint16_t crc = crc16_en13757(data, len);
  out.push_back(crc >> 8);
  out.push_back(crc & 0xFF);
}

Bytes add_crcs_format_a(const Bytes &telegram) {
  Bytes out;
  size_t first = std::min<size_t>(10, telegram.size());
  append_block(out, telegram.data(), first);
  for (size_t pos = first; pos < telegram.size(); pos += 16)
    append_block(out, telegram.data() + pos,
                 std::min<size_t>(16, telegram.size() - pos));
  return out;
}

Bytes add_crcs_format_b(const Bytes &telegram) {
  Bytes frame = telegram;
  bool two_blocks = frame.size() + 2 > 128;
  frame[0] = uint8_t(frame.size() + (two_blocks ? 4 : 2) - 1);
  Bytes out;
  if (!two_blocks) {
    append_block(out, frame.data(), frame.size());
  } else {
    append_block(out, frame.data(), 126);
    append_block(out, frame.data() + 126, frame.size() - 126);
  }
  return out;
}

Bytes encode_3of6(const Bytes &data) {
  static const uint8_t codes[16] = {
      0b010110, 0b001101, 0b001110, 0b001011, 0b011100, 0b011001,
      0b011010, 0b010011, 0b101100, 0b100101, 0b100110, 0b100011,
      0b110100, 0b110001, 0b110010, 0b101001,
  };
  Bytes out;
  uint32_t acc = 0;
  int bits = 0;
  auto push6 = [&](uint8_t code) {
    acc = (acc << 6) | code;
    bits += 6;
    while (bits >= 8) {
      out.push_back(uint8_t(acc >> (bits - 8)));
      bits -= 8;
    }
  };
  for (uint8_t b : data) {
    push6(codes[b >> 4]);
    push6(codes[b & 0x0F]);
  }
  if (bits > 0) // pad the last byte with zero bits
    out.push_back(uint8_t(acc << (8 - bits)));
  return out;
}

Bytes t1_on_air(const Bytes &telegram) {
  return encode_3of6(add_crcs_format_a(telegram));
}

Bytes c1_on_air(const Bytes &telegram, char format) {
  Bytes out = {0x54, uint8_t(format == 'B' ? 0x3D : 0xCD)};
  Bytes frame = format == 'B' ? add_crcs_format_b(telegram)
                              : add_crcs_format_a(telegram);
  out.insert(out.end(), frame.begin(), frame.end());
  return out;
}

} // namespace sim
