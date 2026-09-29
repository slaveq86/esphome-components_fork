# CLAUDE.md — esphome-components_fork

## Rules (read first)
- **Do not commit unless the user asks for it in that request.** No `git commit`, `git push`, `git rebase`, `git tag` or other history changes on your own initiative. Permission to commit covers only the commit it was given for. Never push unless explicitly asked.
- **Keep commit messages and PR descriptions short**: a one-line summary (≤ 72 chars, `component: what changed`), then at most a few lines on *why* if it isn't obvious. No long bullet lists, no restating the diff.
- Do not reformat or "fix" line endings (see *Line endings*).
- Keep the diff against upstream small (see *What this is*). Every extra change makes rebasing onto upstream harder.

## What this is
A **thin fork** of [SzczepanLeon/esphome-components](https://github.com/SzczepanLeon/esphome-components) (`slaveq86/esphome-components_fork`): ESPHome external components that receive wireless M-Bus (EN 13757-4, 868 MHz, T1/C1) telegrams from utility meters and expose decoded values as sensors in Home Assistant.

The main functional change on top of upstream is the SX1262 **`tcxo_voltage`** option (`wmbus_radio`, commit `c6b2058`). There are also two small bug fixes in `wmbus_radio` (`decode3of6.cpp` out-of-bounds read, `component.cpp` argument-order dependency; see `components/wmbus_radio/CLAUDE.md`) that are candidates for upstream. Upstream hardcodes the DIO3 TCXO supply to 3.0 V; the Heltec WiFi LoRa 32 V3/V4 needs **1.8 V** or the radio never locks. The fork also **deletes upstream's `components/esp32`**, a vendored copy of ESPHome's esp32 platform that only raised the main-loop stack to 32 KB. Newer ESPHome breaks with it ("Component not found: logger"), and it has `esp32: framework: advanced: loop_task_stack_size: 32768` for that now. Every config needs that setting, because wmbusmeters decodes in the main loop. If a rebase conflicts on `components/esp32`, keep it deleted. Everything else should stay identical to upstream so the fork can be rebased cleanly.

User's target: a Heltec WiFi LoRa 32 **V4** (ESP32-S3 + SX1262) reading a **Diehl IZAR** water meter (driver `izar`, T1, no key), in the style of [zibous/ha-watermeter](https://github.com/zibous/ha-watermeter).

Parent project: this checkout lives in `external_repos/` of `/mnt/esphome-components-wmbus5` (a different, older fork with `.cc` drivers). Do not copy files or merge history between the two.

## Layout
```
components/
  wmbus_radio/         radio drivers (SX1262, SX1276, CC1101), RX task, Frame, on_frame trigger
  wmbus_meter/         meter instances + sensor/text_sensor platforms
  wmbus_common/        vendored wmbusmeters C++ (parsers, crypto, 91 driver_*.cpp)
  socket_transmitter/  TCP/UDP send action
heltec_v4.yaml                    user's board config (SX1262, FEM switches, tcxo_voltage: 1.8V)
UltimateReader_v5.yaml            upstream example (SX1276, LilyGO T3-S3)
ESP32-C3_SuperMini_CC1101.yaml    upstream example (CC1101)
ESP32-NodeMcu-32s_CC1101.yaml     upstream example (CC1101)
upload_plan.md                    how to flash heltec_v4.yaml via HA's ESPHome Device Builder
tests/                            hardware-free tests (host C++ + simulated SX1262, codegen pytest)
```
Each component folder has its own `CLAUDE.md`.

## Data flow
```
radio IRQ ──ISR──▶ radio_recv task (core 1, prio 24) ──SPI read──▶ Packet
  ──queue(3)──▶ Radio::loop() ──3of6 decode / CRC──▶ Frame
  ──▶ on_frame triggers + every wmbus_meter::Meter::handle_frame()
        └─ id match → ::Meter::handleTelegram() → defer → on_telegram
             └─ sensor / text_sensor → publish_state → Home Assistant
```
Unhandled frames log a `https://wmbusmeters.org/analyze/<hex>` link.

## Testing
Local toolchain: **`~/esphome-venv/bin/esphome`**. Keep it on the same ESPHome version as the user's Home Assistant add-on (`~/esphome-venv/bin/pip install -U esphome`); `loop_task_stack_size` does not exist in 2025.10.

**0. Hardware-free tests first** (details in `tests/README.md`):
```bash
tests/run_all.sh            # host C++ tests (ASan/UBSan) + config/codegen pytest, ~2 min
tests/run_all.sh --full     # + real ESP-IDF builds of SX1262/SX1276/CC1101 and drivers: all
make -C tests run T=sx1262  # a subset of the host tests
```
- The host tests compile the unmodified component sources for Linux against fakes in `tests/host/stubs/`. The SX1262 is simulated at SPI level (`tests/host/sim/`), so setup commands, `tcxo_voltage`, RX timing and the whole receive → meter → sensor path are tested without a board.
- Expected result: `0 failed`. The listed `xfail`s are known upstream bugs (`KNOWN_BUG`, see `tests/README.md`). An `XPASS` means one got fixed: move the test out of `KNOWN_BUG`.
- Keep test code inside `tests/`; never change `components/` just to make something testable.

Then, for anything that touches build config or codegen, the manual steps below.

**1. Build against the local tree, not GitHub.** `heltec_v4.yaml` pulls `github://slaveq86/esphome-components_fork@main`, so it does *not* test local edits. Use a throwaway config in the scratchpad that points at the local components, e.g.:
```yaml
esphome: { name: wmbus-test }
esp32:
  board: heltec_wifi_lora_32_V3
  flash_size: 16MB
  framework:
    type: esp-idf
    advanced: { loop_task_stack_size: 32768 }   # wmbusmeters needs > 8 KB
logger: { level: DEBUG }
wifi: { ssid: test, password: testtest }   # dummy, only so `time:` can compile
time:
  - platform: sntp                         # wmbus_meter needs a time component (see its CLAUDE.md)
external_components:
  - source: { type: local, path: /mnt/esphome-components-wmbus5/external_repos/esphome-components_fork/components }
spi: { clk_pin: GPIO9, mosi_pin: GPIO10, miso_pin: GPIO11 }
wmbus_radio:
  radio_type: SX1262
  cs_pin: GPIO8
  reset_pin: GPIO12
  irq_pin: GPIO14
  busy_pin: GPIO13
  rf_switch: true
  tcxo_voltage: 1.8V
wmbus_meter:
  - id: water
    meter_id: 0x12345678
    type: izar
    mode: [T1]
sensor:
  - platform: wmbus_meter
    parent_id: water
    field: total_m3
    name: Water total
```
(Inline dummy Wi-Fi, no `api` → no `secrets.yaml` needed.)

**2. Validate, then compile.**
```bash
~/esphome-venv/bin/esphome config  <scratchpad>/wmbus-test.yaml   # schema + codegen only, seconds
~/esphome-venv/bin/esphome compile <scratchpad>/wmbus-test.yaml   # full ESP-IDF build, ~2 min
```
A change is verified only when `compile` ends with `SUCCESS`.

**3. Cover what the filters hide.** Unused sources are excluded from the build, so a green compile only proves the parts that config selected:
- `wmbus_radio` compiles only the selected `radio_type` — after touching shared radio code (`transceiver.*`, `component.*`, `packet.*`), compile with `SX1262`, `SX1276` and `CC1101`.
- `wmbus_common` compiles only drivers named in `type:` — after touching parser/driver code add `wmbus_common: { drivers: all }` to compile every driver.
- Anything used only from YAML lambdas/actions (`on_frame`, `socket_transmitter.send`, `wmbus_radio.send_frame_with_socket`, `wmbus_meter.send_telegram_with_mqtt`) must appear in the test config, or template errors stay hidden.

**4. Validate the real board config** after changing `heltec_v4.yaml` (it fetches the pushed fork, so it tests the GitHub copy): create a throwaway `secrets.yaml` next to it (`wifi_ssid`, `wifi_password`, `ap_password`, `api_encryption_key`; OTA reuses the api key), run `esphome config heltec_v4.yaml`, then delete it.

**5. Clean up.** `secrets.yaml` and the `.esphome/` build dir are **not gitignored** here — delete them after a run and never stage them. (`tests/build/` is ignored by `tests/.gitignore`.)

**On hardware** (user's step): `~/esphome-venv/bin/esphome run heltec_v4.yaml` builds, flashes over USB and streams logs. Success = SX1262 setup without `BUSY pin timeout`, then `RSSI: … T: …` lines from `on_frame`. See `upload_plan.md` for flashing through Home Assistant.

## Syncing with upstream
```bash
git fetch https://github.com/SzczepanLeon/esphome-components.git main
git rebase FETCH_HEAD        # keeps the tcxo_voltage commit on top
```
The user runs rebases/pushes. After a sync, re-run the tests above. If upstream changed `components/esp32`, the rebase stops with a modify/delete conflict: keep the deletion (`git rm -r components/esp32`).

## Conventions
- ESP-IDF only (no Arduino). Radios need the `spi:` component.
- Python: standard ESPHome codegen (`CONFIG_SCHEMA`, `async def to_code`, `cg.new_Pvariable`, `automation.register_action`).
- C++: namespace `esphome::<component>`; wmbusmeters code is in the global namespace (`::Meter`, `Telegram`, `LinkMode`). One `static const char *TAG` per file, `ESP_LOGx`.
- Leave `CODEOWNERS = ["@SzczepanLeon", "@kubasaw"]` as is.

## Line endings
The checkout is on `/mnt` under WSL: most working-tree files are **CRLF** while the committed blobs are LF, so `git status` lists nearly everything as modified. `git diff --ignore-cr-at-eol` shows the real changes. Match the line endings of the file you edit, never mass-convert, and stage only the files you changed (check with `git diff --cached --ignore-cr-at-eol`).
