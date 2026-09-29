# esphome-components_fork — wM-Bus for ESPHome, with SX1262 TCXO voltage support

ESPHome external components that receive **wireless M-Bus** (EN 13757-4, 868 MHz, T1/C1) telegrams from utility meters (water, heat, electricity, …), decode them with the built-in [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters) drivers and publish the values as sensors to **Home Assistant**.

This is a **thin fork** of [SzczepanLeon/esphome-components](https://github.com/SzczepanLeon/esphome-components) (version 5), which is itself based on [IoTLabs-pl/esphome-components](https://github.com/IoTLabs-pl/esphome-components). All credit for the components goes to their authors.

## Why this fork exists

Upstream's SX1262 driver always powers the radio's crystal oscillator (TCXO, via DIO3) with **3.0 V**, and only lets you turn it on or off (`has_tcxo`).

Boards such as the **Heltec WiFi LoRa 32 V3 / V4** use a **1.8 V TCXO**. With 3.0 V the radio's clock never locks, so the board receives nothing. Other projects work around this by depending on unmaintained forks.

This fork adds a single, backward-compatible option instead:

```yaml
wmbus_radio:
  radio_type: SX1262
  tcxo_voltage: 1.8V   # 1.6V | 1.7V | 1.8V | 2.2V | 2.4V | 2.7V | 3.0V (default) | 3.3V
```

The default is `3.0V`, so existing configs behave exactly as upstream. Apart from this option and two small receive-path bug fixes found by the [tests](tests/README.md), the fork tracks upstream `main` unchanged and is rebased onto it to pick up fixes.

## Features

- Radios: **SX1262**, **SX1276**, **CC1101** (SPI), ESP-IDF only
- T1 and C1 link modes, frame formats A and B, CRC checking
- 90+ meter drivers from wmbusmeters, compiled in only when used; AES keys (hex or ASCII)
- Radio reception in a dedicated FreeRTOS task
- Triggers: `on_frame` (every received frame) and `on_telegram` (per meter)
- Forward raw frames over TCP/UDP (`socket_transmitter`) or MQTT, in hex / raw / rtlwmbus format
- Unknown telegrams are logged with a `https://wmbusmeters.org/analyze/<hex>` link to identify the meter

## Quick start: Heltec WiFi LoRa 32 V4

[`heltec_v4.yaml`](heltec_v4.yaml) is a complete config for the Heltec V4 (ESP32-S3 + SX1262), including the V4's RF front-end power switches and `tcxo_voltage: 1.8V`. The core of it:

```yaml
external_components:
  - source: github://slaveq86/esphome-components_fork@main

esp32:
  board: heltec_wifi_lora_32_V3   # no V4 board definition yet; same ESP32-S3
  flash_size: 16MB
  framework:
    type: esp-idf

time:
  - platform: homeassistant      # required by wmbus_meter

spi:
  clk_pin: GPIO9
  mosi_pin: GPIO10
  miso_pin: GPIO11

wmbus_radio:
  radio_type: SX1262
  cs_pin: GPIO8
  reset_pin: GPIO12
  irq_pin: GPIO14      # DIO1
  busy_pin: GPIO13
  rf_switch: true      # DIO2 drives the RF switch
  tcxo_voltage: 1.8V

wmbus_meter:
  - id: water_meter
    meter_id: 0x12345678
    type: izar         # Diehl IZAR, no key needed
    mode: [T1]

sensor:
  - platform: wmbus_meter
    parent_id: water_meter
    field: total_m3
    name: Water total
    device_class: water
    state_class: total_increasing
```

Start without the `wmbus_meter` / `sensor` blocks: every received telegram is then logged with a wmbusmeters.org analyze link, which tells you the meter ID, driver and available fields. [`upload_plan.md`](upload_plan.md) explains how to flash the board through Home Assistant's ESPHome Device Builder.

## Configuration

### `wmbus_radio`

| Option | Radios | Default | Description |
|---|---|---|---|
| `radio_type` | all | — | `SX1262`, `SX1276` or `CC1101` |
| `cs_pin` | all | — | SPI chip select |
| `irq_pin` | all | — | Interrupt pin (SX1262: DIO1, SX1276: DIO1, CC1101: GDO0) |
| `reset_pin` | SX1262, SX1276 | — | Reset pin (CC1101 uses a software reset) |
| `busy_pin` | SX1262 | — | BUSY pin, recommended |
| `rx_gain` | SX1262 | `BOOSTED` | `BOOSTED` (sensitivity) or `POWER_SAVING` |
| `rf_switch` | SX1262 | `false` | `true` if DIO2 controls the RF switch |
| `sync_mode` | SX1262 | `NORMAL` | `NORMAL` or `ULTRA_LOW_LATENCY` (currently loses packets, see [tests](tests/README.md#known-upstream-bugs)) |
| `has_tcxo` | SX1262 | `true` | DIO3 powers an external TCXO |
| `tcxo_voltage` | SX1262 | `3.0V` | **Added by this fork.** TCXO supply voltage, `1.6V`…`3.3V` |
| `frequency` | CC1101 | `868.95MHz` | 300–928 MHz |
| `on_frame` | all | — | Automation per received frame (`frame->as_hex()`, `frame->rssi()`, …); `mark_as_handled: true` suppresses the "not handled" warning |

All radios need an ESPHome `spi:` bus.

Tested boards:
- **SX1262**: Heltec WiFi LoRa 32 V4 ([`heltec_v4.yaml`](heltec_v4.yaml)), M5Stack Stamp C6LoRa
- **SX1276**: LilyGO T3-S3 ([`UltimateReader_v5.yaml`](UltimateReader_v5.yaml))
- **CC1101**: ESP32-C3 Super Mini + E07-M1101D ([`ESP32-C3_SuperMini_CC1101.yaml`](ESP32-C3_SuperMini_CC1101.yaml)), NodeMCU-32S ([`ESP32-NodeMcu-32s_CC1101.yaml`](ESP32-NodeMcu-32s_CC1101.yaml))

### `wmbus_meter`

```yaml
wmbus_meter:
  - id: electricity_meter
    meter_id: 0x12345678
    type: amiplus                  # driver name, default: auto
    key: !secret electricity_key   # optional, 32 hex chars or 16 ASCII chars
    mode: [T1, C1]                 # default: Any
    on_telegram:
      then:
        - wmbus_meter.send_telegram_with_mqtt:
            topic: wmbus/electricity
```

A `time:` component must be present in the config.

### Sensors

```yaml
sensor:
  - platform: wmbus_meter
    parent_id: electricity_meter
    field: total_energy_consumption_kwh   # <field>_<unit>
    name: Energy
    device_class: energy
    state_class: total_increasing

  - platform: wmbus_meter
    parent_id: electricity_meter
    field: rssi_dbm
    name: Meter RSSI

text_sensor:
  - platform: wmbus_meter
    parent_id: electricity_meter
    field: current_alarms
    name: Meter alarms
```

Numeric fields are named `<field>_<unit>`, where the unit must be the one the driver reports (`total_m3` works, `total_l` never publishes). It also becomes the default `unit_of_measurement`. Available fields per meter are listed on the wmbusmeters.org analyze page. Special fields: `rssi_dbm`, `timestamp`, `timestamp_zulu`.

### Forwarding raw frames

```yaml
socket_transmitter:
  id: my_socket
  ip_address: 192.168.1.10
  port: 3333
  protocol: TCP      # or UDP

wmbus_radio:
  # ...
  on_frame:
    - mark_as_handled: true
      then:
        - wmbus_radio.send_frame_with_socket:
            format: rtlwmbus   # hex | raw | rtlwmbus
        - mqtt.publish:
            topic: wmbus/raw
            payload: !lambda return frame->as_hex();
```

## Staying in sync with upstream

```bash
git fetch https://github.com/SzczepanLeon/esphome-components.git main
git rebase FETCH_HEAD
```

The `tcxo_voltage` patch touches only `components/wmbus_radio/` (`__init__.py`, `transceiver.h`, `transceiver.cpp`, `transceiver_sx1262.cpp`); the bug fixes touch `decode3of6.cpp` and `component.cpp`. If upstream ever gains an equivalent option (and the fixes), this fork can be retired.
