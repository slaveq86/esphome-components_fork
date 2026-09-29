# Uploading the Heltec V4 firmware via ESPHome Device Builder (device on PC, not HA)

Your board is plugged into your **PC over USB**, but ESPHome Device Builder runs on the
**Home Assistant** machine. The add-on can only flash over USB when the board is plugged into
the *HA host*, so the first flash has to happen from the PC. After that, the device joins Wi-Fi
and every later update is wireless (OTA) straight from HA.

The clean split:
- **Build the firmware** in ESPHome Device Builder (on HA).
- **Do the very first flash** from the PC, in the browser, over USB.
- **All future updates**: OTA from HA — no cable.

---

## 0. One-time prep of the config (do this first)

- [x] `heltec_v4.yaml` already pulls the components straight from the fork on GitHub, so it
  builds anywhere (including HA's Device Builder) with no `components/` folder next to it:
  ```yaml
  external_components:
    - source: github://slaveq86/esphome-components_fork@main
      refresh: 0s
  ```
  (The `tcxo_voltage` patch is on `origin/main`, so this resolves. `refresh: 0s` means ESPHome
  caches the fork and won't re-pull on rebuilds — after you push new fork changes, pin a
  commit/tag or clear the ESPHome cache to pick them up.)
- [ ] Decide your board revision and set the FEM switches accordingly (GC1109 for V4.2 vs
  KCT8103L for V4.3) — see the comments in `heltec_v4.yaml`.

## 1. Create the config in ESPHome Device Builder (on HA)

- [ ] Open **Home Assistant → Settings → Add-ons → ESPHome Device Builder** (install it first if
  you haven't; enable "Show in sidebar").
- [ ] In the ESPHome dashboard click **+ New device → Continue → Skip / manual**, or use the
  **three-dots → Edit** on a device to paste raw YAML.
- [ ] Paste the contents of `heltec_v4.yaml` as-is (it already points at the GitHub fork).
- [ ] Fill in **secrets** (top-right **Secrets** editor in the ESPHome dashboard):
  - `wifi_ssid`
  - `wifi_password`
  - `ap_password`         (fallback hotspot password)
  - `api_encryption_key`  (generate one: ESPHome dashboard suggests it, or any 32-byte base64 key)
  - `ota_password`
- [ ] Click **Save**, then **Install → dropdown → Validate** (or run **`esphome config`** on the
  PC) to confirm it compiles clean before flashing.

## 2. Build the firmware and get the binary

- [ ] In the device's **⋮ (three dots) → Install**.
- [ ] Choose **"Manual download"** (NOT "Plug into this computer" — the board is on your PC, not HA).
- [ ] Pick the **"Modern format" / factory .bin** (full image) for the **first** flash. ESPHome
  builds it and downloads `wmbus-heltec-v4.factory.bin` to your PC.

## 3. First flash from the PC (over USB, in the browser)

- [ ] Plug the Heltec V4 into the PC. Use a **data** USB-C cable, not charge-only.
- [ ] Open **https://web.esphome.io** in **Chrome or Edge** (Web Serial isn't in Firefox/Safari).
- [ ] Click **Connect**, pick the board's serial port. If none appears, install the USB-serial
  driver (Heltec V4 uses a **CP2102 / CP210x**; some units use CH340) and re-plug.
- [ ] Click **Install → choose file →** select the `*.factory.bin` from step 2 → **Install**.
- [ ] If it won't enter flash mode: hold **BOOT**, tap **RST**, release **BOOT**, then retry Connect/Install.
- [ ] Wait for "Installation complete."

> Alternative to the browser: you already have the ESPHome CLI on this PC
> (`~/esphome-venv/bin/esphome`). With the board plugged in you can flash + watch logs directly:
> `~/esphome-venv/bin/esphome run heltec_v4.yaml` and pick the USB port. This does the build,
> flash, and log stream in one step and skips steps 2–3.

## 4. Verify it's alive

- [ ] In **web.esphome.io** click **Logs** (or `esphome logs heltec_v4.yaml`), or watch the serial
  logs — you should see Wi-Fi connect and the SX1262 init.
- [ ] Confirm the radio **locks** (no TCXO/PLL error) — this is what the `1.8V` TCXO patch fixes.
- [ ] Wait for wM-Bus telegrams: each one logs a line like
  `RSSI: -78dBm T: <hex> (<len>) T1` plus a `https://wmbusmeters.org/analyze/<hex>` link.
  Keep one of those links — it identifies your IZAR meter for the next step.

## 5. Adopt in Home Assistant (wireless from here on)

- [ ] Back in HA: **Settings → Devices & Services** should show a discovered **ESPHome** device
  (`wmbus-heltec-v4`). Click **Configure**, enter the `api_encryption_key`, and add it.
- [ ] In ESPHome Device Builder the device now shows **ONLINE** — future edits install via
  **⋮ → Install → Wirelessly (OTA)**. No more cable.

---

## Notes / gotchas

- **Local vs GitHub source**: the config now uses the GitHub source (works everywhere, including
  HA's Device Builder). The local-path source is kept as a commented alternative in
  `heltec_v4.yaml` for compiling against a local checkout on this PC.
- **Wi-Fi band**: ESP32-S3 is **2.4 GHz only** — make sure `wifi_ssid` is a 2.4 GHz network.
- **Meter block is still commented** in `heltec_v4.yaml` on purpose — no sensors will appear in HA
  yet. Once step 4 gives us the analyze link, we fill in the IZAR `meter_id` / sensors and
  re-flash over OTA.
- **First flash must be the factory image**; OTA-only images won't boot on a blank chip.
