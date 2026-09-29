// Test-side "transmitter": turns a telegram (as written in the wmbusmeters
// test vectors, without CRCs) into the bytes a radio receives after the sync
// word. Written from EN 13757-4, independently of the receive code under test.
#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace sim {
using Bytes = std::vector<uint8_t>;

Bytes from_hex(const std::string &hex); // ignores '|', '_', spaces
std::string to_hex(const Bytes &bytes);  // lowercase

uint16_t crc16_en13757(const uint8_t *data, size_t len);

// Frame format A: CRC after the first 10 bytes, then after every 16 bytes.
Bytes add_crcs_format_a(const Bytes &telegram);
// Frame format B: L-field counts the CRCs; CRC after byte 126 and at the end.
Bytes add_crcs_format_b(const Bytes &telegram);

// T1 mode: 3-out-of-6 encoding of a format A frame.
Bytes encode_3of6(const Bytes &data);
Bytes t1_on_air(const Bytes &telegram);
// C1 mode: 0x54 then 0xCD (format A) or 0x3D (format B), then the frame.
Bytes c1_on_air(const Bytes &telegram, char format);
} // namespace sim
