# esp32 (vendored ESPHome platform override)

A copy of ESPHome's built-in `esphome/components/esp32` platform. Because it lives in `components/`, it **replaces** the built-in platform for every config that loads this repo's components (local or GitHub source).

## Why it exists
One intentional change — a bigger main loop task stack in `core.cpp`:
```cpp
xTaskCreate(loop_task, "loopTask", 32768, nullptr, 1, &loop_task_handle);   // stock: 8192
```
wmbusmeters parsing (`handleTelegram`, JSON printing, AES) runs in the ESPHome loop and overflows the stock stack. History: 65536 → 40960 → 32768.

## Version drift
- Upstream labels it "Align to ESPHome 2025.10.3" (`git log -- components/esp32`), but it is **behind** the real 2025.10.3 platform, e.g. `core.cpp` lacks the OTA-rollback block (`esp_ota_mark_app_valid_cancel_rollback`). With `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE` an OTA image could be rolled back on reboot.
- When Home Assistant's ESPHome version changes, this folder can break the build (API changes in `esphome.const`, `cv`, `CORE.data`, …). Check it first when a build fails inside `esp32`.

## Rules
- Do not edit here. It is upstream's file; changes belong upstream (Szczepan). If a re-sync is ever needed: copy `esphome/components/esp32/` from the matching ESPHome release tag and re-apply only the stack-size line.
- To compare with the installed ESPHome:
  `diff <(tr -d '\r' < components/esp32/core.cpp) ~/esphome-venv/lib/python3.12/site-packages/esphome/components/esp32/core.cpp`
