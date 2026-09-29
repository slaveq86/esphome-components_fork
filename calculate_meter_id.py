#!/usr/bin/env python3
"""Diehl IZAR: convert the serial number printed on the meter / bill
(e.g. C25UB123456) into the wM-Bus meter_id for heltec_v4.yaml, and back.

    ./calculate_meter_id.py C25UB123456     -> meter_id: 0x417F5A80
    ./calculate_meter_id.py 0x417F5A80      -> year 2025, size B, serial 123456

Derived from the prefix/serial decoding in
components/wmbus_common/driver_izar.cpp: the radio address holds
year * 1000000 + serial in its low 26 bits and the size letter's low 3 bits
in bits 29-31. The supplier (C) and type (U) letters are sent elsewhere in
the telegram, so they don't affect the id. Bits 26-28 were zero on every
known meter; if the result doesn't match, use the id from the device log.
"""

import re
import sys

SERIAL_RE = re.compile(r"^([A-Z])(\d{2})([A-Z])([A-Z])(\d{1,6})$")
ID_RE = re.compile(r"^(0X)?([0-9A-F]{8})$")


def serial_to_id(serial: str) -> int:
    supplier, year, meter_type, size, number = SERIAL_RE.match(serial).groups()
    size_code = ord(size) - ord("@")  # A=1, B=2, ...
    return (size_code & 0x07) << 29 | (int(year) * 1_000_000 + int(number))


def id_to_parts(meter_id: int) -> str:
    digits = f"{meter_id & 0x03FFFFFF:08d}"
    size_low_bits = meter_id >> 29
    return (
        f"year 20{digits[:2]}, serial {digits[2:]}, "
        f"size letter {chr(ord('@') + size_low_bits)} (if the size code is < 8)"
    )


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__.strip(), file=sys.stderr)
        return 2
    arg = sys.argv[1].strip().upper().replace(" ", "").replace("-", "")

    if SERIAL_RE.match(arg):
        meter_id = serial_to_id(arg)
        print(f"meter_id: 0x{meter_id:08X}")
        print(f"(wmbusmeters / log shows it as id {meter_id:08x})")
        return 0

    m = ID_RE.match(arg)
    if m:
        print(id_to_parts(int(m.group(2), 16)))
        return 0

    print(
        f"Not recognised: {sys.argv[1]!r}. Expected a serial like C25UB123456 "
        "or an 8-digit hex id like 0x417F5A80.",
        file=sys.stderr,
    )
    return 1


if __name__ == "__main__":
    sys.exit(main())
