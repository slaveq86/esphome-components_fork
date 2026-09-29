# wmbus_radio

Receives raw wM-Bus frames from an SPI radio and hands them to handlers (meters, `on_frame` automations). Contains this fork's patches: `tcxo_voltage` and two receive-path bug fixes (below).

## YAML
```yaml
wmbus_radio:
  radio_type: SX1262     # SX1262 | SX1276 | CC1101 (from transceiver_*.cpp file names)
  cs_pin: GPIO8          # via spi_device_schema (1 MHz, MODE0)
  reset_pin: GPIO12      # optional, output pin schema (I/O expanders allowed)
  irq_pin: GPIO14        # required, internal GPIO (SX1262: DIO1)
  busy_pin: GPIO13       # optional; SX1262 BUSY
  frequency: 868.95MHz   # CC1101 only (300–928 MHz)
  rx_gain: BOOSTED       # SX1262: BOOSTED | POWER_SAVING
  rf_switch: false       # SX1262: DIO2 drives the RF switch
  sync_mode: NORMAL      # SX1262: NORMAL | ULTRA_LOW_LATENCY
  has_tcxo: true         # SX1262: DIO3 powers a TCXO
  tcxo_voltage: 3.0V     # SX1262: 1.6V..3.3V, default 3.0V (FORK PATCH)
  on_frame:
    - mark_as_handled: false
      then:
        - logger.log: { format: "%s", args: [ frame->as_hex().c_str() ] }
```

## Files
- `__init__.py` — schema + codegen. `TRANSCEIVER_NAMES` is built from `transceiver_*.cpp`; `FILTER_SOURCE_FILES()` drops the unselected transceivers from the build. Also registers `wmbus_radio.send_frame_with_socket` (`format: hex|raw|rtlwmbus`) when `socket_transmitter` is importable.
- `component.{h,cpp}` — `Radio`: RX task, packet queue, handler dispatch, "not handled" warning.
- `transceiver.{h,cpp}` — abstract `RadioTransceiver` (SPIDevice + Component): pin/option setters, `reset()`, `common_setup()`, `wait_busy()`, `read_in_task()`, and two SPI styles — `spi_transaction/spi_read/spi_write` (register-based, SX1276/CC1101) and `spi_command/spi_read_frame` (command-based, SX1262, waits for BUSY).
- `transceiver_sx1262.{h,cpp}` — SX1262 (header is mostly RadioLib defines).
- `transceiver_sx1276.{h,cpp}` — SX1276, byte-by-byte FIFO reads (`read()`), falling-edge IRQ.
- `transceiver_cc1101.{h,cpp}` — CC1101, 26 MHz crystal, uses `frequency`, FIFO overflow/errata handling.
- `packet.{h,cpp}` — `Packet` (raw buffer, L-field, expected size, link mode, format A/B, CRC strip) and `Frame` (decoded telegram, RSSI, mode, format).
- `decode3of6.{h,cpp}` — T1 3-out-of-6 decoding.
- `automation.h` — `FrameTrigger` (with `mark_as_handled`).

## Receive pipeline / threading
1. IRQ edge (`get_interrupt_type()`, rising by default) → `wakeup_receiver_task_from_isr` → task notify.
2. `radio_recv` task (8 KB stack, priority 24, pinned to core 1 on dual-core) in `receive_frame()`:
   - waits up to 60 s for a notify, else `restart_rx()` (watchdog for a stuck radio);
   - `read_in_task()` reads the header, `calculate_payload_size()` from the L-field, then the rest from offset 3; stores RSSI; `restart_rx()`;
   - every early exit calls `restart_rx()` — keep it that way, or the radio stays deaf until the watchdog;
   - pushes `Packet*` into a depth-3 queue (ownership transferred; dropped with a warning when full).
3. `Radio::loop()` (main loop) pops a packet, `convert_to_frame()` (decode + CRC; failures dropped), calls every handler.
4. No handler called `frame->mark_as_handled()` → warning + `https://wmbusmeters.org/analyze/<hex>`.

SPI only from the RX task; ESPHome entities only from `loop()`.

## Link modes
From the first byte (`packet.cpp`): `0x54` → C1 (next byte `0xCD` = frame A, `0x3D` = frame B), otherwise T1 (3of6, frame A).

## SX1262 (`transceiver_sx1262.cpp`)
Setup order: reset → STANDBY_RC → GFSK → RF freq → buffer base → modulation → packet params → RX gain → DIO2 RF switch (if `rf_switch`) → IRQ mask → sync word → **DIO3 TCXO (`tcxo_voltage_`, if `has_tcxo`)** → Calibrate → CalibrateImage 863 MHz → fallback STDBY_XOSC → STANDBY_XOSC → RX.
- RX is started with timeout 0, i.e. **single mode**: the chip drops back to standby after each packet, and the driver re-arms it (`get_frame` at offset > 0, `restart_rx`).
- Hardcoded: **868.950 MHz** (`frequency` is ignored here), 100 kbps, 50 kHz deviation, no shaping, RX BW 234.3 kHz, 16-bit preamble, sync `0x54 0x3D`, no HW CRC/whitening.
- The payload is a fixed 255 bytes, so long frames are truncated and RX_DONE comes ~20 ms after the sync word.
- `ULTRA_LOW_LATENCY` also enables the SYNC_WORD_VALID IRQ, and `get_frame()` returns 0 until RX_DONE. **Known bug:** `read_in_task` then waits only 1 tick for RX_DONE and aborts the packet, so this mode loses packets. Use `NORMAL`.

The whole sequence and every parameter are checked against the datasheet by `tests/host/test_sx1262.cpp`.

## Bug fixes in this fork (not yet upstream)
Found by the host tests (`tests/`); both are regression-tested there. Offer them upstream, and expect them as rebase conflicts until then.
- `decode3of6.cpp`: a symbol at bit offset 2 fits in the current byte, but the code read `data[byte_idx + 1]` for any offset > 0, one byte past the input on every T1 frame. Now `if (bit_offset > 2)`.
- `component.cpp` `receive_frame`: `read_in_task(packet->rx_data_ptr(), packet->rx_capacity(), n)` depended on argument evaluation order, because `rx_capacity()` resizes the buffer. It worked with the ESP32 toolchain (left to right) and overflowed with x86-64 GCC. The pointer and capacity are now taken in separate statements.

Still open: `sync_mode: ULTRA_LOW_LATENCY` loses packets (see above; `KNOWN_BUG` test in `tests/host/test_rx_pipeline.cpp`).

## The fork patch (`tcxo_voltage`)
Touches `__init__.py` (`CONF_TCXO_VOLTAGE`, `TCXO_VOLTAGES` map `1.6V→0x00 … 3.3V→0x07`, codegen), `transceiver.{h,cpp}` (`set_tcxo_voltage`, `tcxo_voltage_{0x06}`), and `transceiver_sx1262.cpp` (uses `tcxo_voltage_` in `SET_DIO3_AS_TCXO_CTRL`). Default 3.0 V = upstream behaviour. When rebasing onto upstream, conflicts will be in exactly these spots.

Board notes: Heltec WiFi LoRa 32 V3/V4 → `tcxo_voltage: 1.8V`, `busy_pin: GPIO13`, `rf_switch: true`; V4 also needs its FEM powered (see `heltec_v4.yaml`).

## Adding a transceiver
Add `transceiver_<name>.{h,cpp}` with class `<NAME> : public RadioTransceiver` in `esphome::wmbus_radio`, implementing `setup()`, `restart_rx()`, `get_rssi()`, `get_name()` and either `get_frame()` (block reads) or `read()` (byte reads). It becomes a `radio_type` automatically.

## Frame API (YAML lambdas: `frame->...`)
`data()`, `rssi()`, `link_mode()` (use `toString(...)`), `format()` ("A"/"B"), `as_hex()`, `as_raw()`, `as_rtlwmbus()`, `mark_as_handled()`, `handlers_count()`.
