# wmbus_common

Vendored C++ from [wmbusmeters](https://github.com/wmbusmeters/wmbusmeters) plus a thin ESPHome glue layer. Versions in `_version.h`: `WMBUS_COMPONENT_VERSION` 5.1.7, `WMBUSMETERS_VERSION` 1.19.0-fe1b1e0. Auto-loaded by `wmbus_radio`.

## Glue (ESPHome side)
- `__init__.py`
  - Scans `driver_*.cpp` for `di.setName("…")` / `di.addNameAlias("…")` → `DRIVERS`. `type:` accepts any name or alias (e.g. `multical603` → `driver_kamheat.cpp`); duplicates or a missing `setName` fail at import.
  - `validate_driver` (used by `wmbus_meter`'s `type:`) records the driver as selected.
  - `wmbus_common: { drivers: all | [list] }` forces extra drivers in.
  - `FILTER_SOURCE_FILES()` excludes every unselected `driver_*.cpp` (keeps flash small).
  - `to_code` references each selected driver's `KEEP_DRIVER` symbol from `main.cpp`, so the linker keeps its self-registering object file.
- `_component.h` — `WMBusCommon`; `dump_config` logs versions and loaded drivers.
- `units.py` — parses `LIST_OF_UNITS` in `units.h` for the unit suffix → human-readable unit map used by `wmbus_meter/sensor`.

## Vendored (upstream wmbusmeters)
`meters.*`, `wmbus.*` (Telegram, LinkMode, frame checks, CRC), `dvparser.*`, `aes*`, `address.*`, `units.*`, `formula*`, `translatebits.*`, `manufacturer*`, `util.*`, `wmbus_utils.*`, and 91 `driver_*.cpp`.

## Editing rules
- This fork should not change anything here — upstream Szczepan owns these files. Fixes go upstream (Szczepan, or wmbusmeters for parser/driver logic).
- Existing local adaptations from upstream: `#undef HZ` in `units.h` (ESP-IDF defines `HZ`), the `KEEP_DRIVER` macro in `meters.h`.
- A new driver = `driver_<name>.cpp` modelled on an existing one: `static bool ok = registerDriver([](DriverInfo &di){ di.setName("<name>"); … });` plus `KEEP_DRIVER(<name>);` at the end (hyphens in the file name become `_`). It is selectable as `type: <name>` automatically.
- Compile with `wmbus_common: { drivers: all }` after touching shared parser code — otherwise only the drivers in the test config are built.

## Water meters relevant here
`driver_izar.cpp` (Diehl IZAR — the user's meter: T1, default Diehl keys, no `key:` needed), `driver_hydrus.cpp`, `driver_multical21.cpp`, `driver_iperl.cpp`, `driver_apator162.cpp`.
