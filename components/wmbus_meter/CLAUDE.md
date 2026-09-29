# wmbus_meter

One `wmbus_meter` entry = one physical meter. Wraps a wmbusmeters `::Meter` and exposes its fields to ESPHome sensors, via the `sensor/` and `text_sensor/` sub-platforms.

## YAML
```yaml
wmbus_meter:
  - id: water_meter
    meter_id: 0x12345678      # hex meter address (optional in the schema, but set it)
    type: izar                # driver name or alias, default "auto"
    key: !secret meter_key    # optional AES key: 32 hex chars or a 16-char ASCII string
    mode: [T1]                # Any (default) | T1 | C1
    on_telegram:
      then:
        - wmbus_meter.send_telegram_with_mqtt:   # payload = meter.as_json()
            topic: wmbus/water

sensor:
  - platform: wmbus_meter
    parent_id: water_meter
    field: total_m3           # <field>_<unit>
text_sensor:
  - platform: wmbus_meter
    parent_id: water_meter
    field: current_alarms
```
`radio_id` is resolved automatically when there is one `wmbus_radio`.

## Files
- `__init__.py` — schema, codegen, `TelegramTrigger`, and `wmbus_meter.send_telegram_with_mqtt` (reuses ESPHome's MQTT publish action; needs `mqtt:`). `type` goes through `wmbus_common.validate_driver`, which rejects unknown names and marks the driver for compilation.
- `wmbus_meter.{h,cpp}` — `Meter` component:
  - `set_meter_params` builds a `MeterInfo` and calls `createMeter`; `setup()` marks the component failed if no meter was created.
  - `set_radio` registers `handle_frame` as a radio frame handler.
  - `handle_frame`: link-mode filter → `::Meter::handleTelegram` → on ID match stores `last_telegram`, `defer`s the `on_telegram` callbacks (then clears it) and marks the frame handled.
  - `get_numeric_field` / `get_string_field` — used by the sensors; `as_json()`.
- `base_sensor.{h,cpp,py}` — shared `parent_id` + `field` schema; `handle_update()` runs on every `on_telegram`.
- `sensor/` — numeric sensor; default `unit_of_measurement` from the field suffix (`wmbus_common/units.py`).
- `text_sensor/` — string sensor.
- `automation.h` — `TelegramTrigger`.

## Field names
Numeric fields are `<name>_<unit>` (`total_m3`, `flow_m3h`, …). The unit must be the driver's display unit: values are stored under it and looked up by the requested unit *before* conversion, so e.g. `total_l` returns nothing (see `getNumericValue` in `wmbus_common/meters.cpp`). Special cases: `rssi_dbm` (from the last telegram), `timestamp`, `timestamp_zulu`.
Fields per driver: the `addNumericField…`/`addStringField…` calls in `wmbus_common/driver_<type>.cpp`, or paste a telegram into wmbusmeters.org/analyze. IZAR: `total_m3`, `last_month_total_m3`, `current_alarms`, `remaining_battery_life_y`, …

## Gotchas
- **Hidden `time` dependency**: `wmbus_meter.h` includes `esphome/components/time/real_time_clock.h`, but `__init__.py` neither depends on nor auto-loads `time`. A config with `wmbus_meter` but no `time:` block passes `esphome config` and then fails to compile (`real_time_clock.h: No such file or directory`). Always have a `time:` component (`homeassistant` or `sntp`). Upstream bug; fix belongs upstream (`DEPENDENCIES`/`AUTO_LOAD`).
- `last_telegram` (and so `rssi_dbm` and `as_json()`) is only valid inside the deferred `on_telegram` callbacks.
- A meter only sees frames whose link mode is in `mode`; others log a warning.
- `field` is not validated at config time — a wrong name just never publishes.
- `dump_config` prints the AES key in hex (upstream behaviour). Config dumps reach serial/API logs, so treat logs of keyed meters as secrets.
