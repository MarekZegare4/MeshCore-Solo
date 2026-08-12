# MeshCore Solo Companion Firmware

A fork of the official [MeshCore](https://github.com/meshcore-dev/MeshCore) companion radio firmware with extended features and UI enhancements, targeting a growing set of supported devices.

Join the discussion on the official MeshCore Discord: https://discord.gg/sdhYArU2jr

Solo firmware thread: https://discord.com/channels/1495203904898728149/1505294337884553447

---

## Supported Devices

| Device | MCU | Display | Firmware file |
| ------ | --- | ------- | ------------- |
| Seeed Wio Tracker L1 (OLED) | nRF52840 | SSD1306 / SH1106 128 × 64 | `solo-<version>-WioTrackerL1.uf2` |
| Seeed Wio Tracker L1 (E-ink) | nRF52840 | GxEPD2 250 × 122 | `solo-<version>-WioTrackerL1Eink.uf2` |
| GAT562 30S Mesh Kit | nRF52840 | SSD1306 128 × 64 | `solo-<version>-GAT562-30S-Mesh-Kit.uf2` |
| Heltec LoRa32 V3 *(experimental)* | ESP32-S3 | SSD1306 128 × 64 | `solo-<version>-Heltec-v3-merged.bin` |
| Heltec LoRa32 V4 *(experimental)* | ESP32-S3 | SSD1306 128 × 64 | `solo-<version>-heltec-v4-merged.bin` |
| M5Stack Cardputer ADV *(experimental)* | ESP32-S3 | ST7789 TFT 240 × 135 | `solo-<version>-M5Stack-Cardputer-ADV-merged.bin` |
| LilyGO T-Echo Lite + KeyShield *(experimental)* | nRF52840 | GxEPD2 250 × 122 | `solo-<version>-LilyGo-T-Echo-Lite-keyshield.uf2` |

All firmware files are published on the [releases page](https://github.com/MarekZegare4/MeshCore-Solo/releases). Each binary supports both BLE and USB serial — there are no separate BLE/USB builds.

The MCU column decides how you flash: nRF52840 boards take a drag-and-drop `.uf2`, ESP32-S3 boards take a `.bin` written with a flasher — see [Flashing](#flashing).

The Wio Tracker L1s, GAT562 and the Cardputer ADV work out of the box. Heltec V3/V4 have no joystick and no keyboard of their own, so they need [a keyboard or a joystick wired up](#hardware-setup--heltec-v3--v4) before the solo UI can be driven. The Cardputer ADV has a built-in QWERTY keyboard; the T-Echo Lite needs the KeyShield add-on (its own T9 keypad) to be usable standalone — see [External Keyboard & Joystick](./docs/solo_features/external_keyboard.md).

<!-- **Enclosures (Wio Tracker L1)**
- [E-ink case](https://www.printables.com/model/1420534-seeed-wio-tracker-l1-e-ink-enclosure)
- [OLED case](https://www.printables.com/model/1380791-meshpack-seeed-l1-oled) -->

---

## Feature highlights

- Extended language support with native Unicode rendering and input — one unified 6×9 display font covering Latin, Greek and Cyrillic, plus on-screen keyboard alphabets for Cyrillic, Greek, Polish, Czech, Slovak, German, French, Spanish, Portuguese and Nordic (Danish/Norwegian/Swedish). Pick two in Settings › Keyboard (**Main** and **Additional**) and switch between them while typing

- Enabled sensor screens with support for onboard sensors (temperature, humidity, pressure, luminosity, CO₂) and GPS data

- **GPS navigation** — a full navigation suite that needs no extra hardware (details in the [Tools Screen](./docs/solo_features/tools_screen/tools_screen.md) docs):

  - **Waypoints** — mark a spot (car, camp, water…) with a short label, see it on the trail map, and get live bearing + distance back to it; the list always offers a one-tap backtrack to where your trail started
  - **GPS compass** — heading derived from course-over-ground (no magnetometer needed), shown as a clear scrolling heading tape with a large degrees + cardinal readout
  - **Navigate to anything** — a saved waypoint, a node straight from Nearby Nodes, or a location someone shares with you in a message
  - **Share & save locations** — send a waypoint to a contact or channel; on the other end, navigate to or save any shared location with one menu
  - **Live location sharing** — broadcast your position over the mesh as you move (movement-gated, to a channel or contact) and see others who share theirs as pins on the map and live distance/bearing in Nearby
  - **Locator** — arm a geofence around a target — a saved waypoint *or* a person (their live/last-known position) — and get an alert when you arrive/leave or they get near/far, with an optional homing beeper that ticks faster the closer you get. Set it from the Locator screen or straight from Nearby Nodes / Waypoints, and see the target as a flag on the map
  - **GPS trail** — background route recording with an auto-fit map (waypoints + live position), summary stats, auto-pause on stops, and [GPX export](#solo-tools)
  - **Metric or imperial** — one global Units setting drives every distance and speed across the UI

- [Messages Screen](./docs/solo_features/message_screen/message_screen.md) — view and send messages, open message details, reply with quick messages or custom text, navigate to / save locations shared in a message, per-channel notification and melody overrides, add/edit/delete channels on-device

- [Favourites Dial](./docs/solo_features/favourites_dial/favourites_dial.md) — pin up to six contacts for quick access from the home screen

- [Settings Screen](./docs/solo_features/settings_screen/settings_screen.md) — configure display, sound, home page order, radio and system settings

- [Clock Screen](./docs/solo_features/clock_screen/clock_screen.md) — view time and date plus up to three configurable data fields, with built-in clock tools (one-shot alarm, countdown timer, stopwatch)

- [Screen Lock](./docs/solo_features/screen_lock/screen_lock.md) — lock the device to prevent accidental keypresses, with a lock screen showing time and sensor data

- [Tools Screen](./docs/solo_features/tools_screen/tools_screen.md) — GPS trail & waypoints, compass, nearby nodes (with ping & navigate), ringtone editor, remote bot, auto-advert, live location sharing, locator, diagnostics, repeater, remote admin

- [External Keyboard & Joystick](./docs/solo_features/external_keyboard.md) — optional, auto-detected hardware: an M5Stack **CardKB** for typing messages without the on-screen grid (Fn+Enter submits, Fn+letter picks an accent, Tab is Hold-Enter, Fn+Esc locks), plus a **wired joystick** for boards without one. A Compact keyboard mode makes CardKB-only, joystick-free operation practical

- **Battery saving (radio)** — two optional, independent toggles under Settings › Radio:
  - **Pwr save** — hardware duty-cycle receive (SX126x `SetRxDutyCycle`): the radio cycles RX↔sleep on its own and wakes on a preamble, cutting average RX current with only a little added receive latency
  - **Auto pwr** — Adaptive Power Control: trims actual TX power on strong links (from ACK SNR) and ramps back up to the configured ceiling on weak/lost links; the home screen shows the live power

### E-ink Display (Wio Tracker L1)

The e-ink variant targets the Wio Tracker L1 fitted with a 2.13″ GxEPD2 panel (250 × 122 px). All screens have been adapted for the e-ink panel:

- **Adaptive layout** — every screen reflows correctly in both landscape (250 × 122) and portrait (122 × 250) orientations
- **Display rotation** — configurable in Settings › Display; applied immediately and persisted across reboots
- **Joystick rotation** — independent of display rotation; useful for custom enclosures
- **Full refresh interval** — configurable in Settings › Display; reduces ghosting on long sessions
- **Clock seconds suppressed by default** — seconds are hidden to reduce per-second panel refreshes and extend display lifetime; re-enable in Settings › Display

---

## Flashing

> [!WARNING]
> When migrating from official or other custom firmware, backup your data and **perform a factory reset** to prevent conflicts with existing settings:
>
> 1. Open device settings in the companion app and download a data backup
> 2. Go to [MeshCore Flasher](https://meshcore.io/flasher), select your device, and perform **Erase flash** before flashing
>
> Updating from an earlier Solo release does not need this, unless the release notes say otherwise.

### nRF52840 boards — Wio Tracker L1, GAT562, T-Echo Lite + KeyShield

1. Download the `.uf2` file for your device from the [releases page](https://github.com/MarekZegare4/MeshCore-Solo/releases)
2. Press reset twice quickly to enter bootloader mode — the device should appear as a mass storage drive on your computer
3. Copy the `.uf2` file to the drive to flash the firmware

### ESP32-S3 boards — Heltec V3, V4, Cardputer ADV

ESP32-S3 has no UF2 bootloader and no mass-storage mode. Releases ship a single **`-merged.bin`** per board — bootloader, partition table and app in one image — which goes to offset `0x0`:

- **[MeshCore Flasher](https://meshcore.io/flasher)** or any Web Serial ESP tool — select the `-merged.bin` and flash at `0x0`
- **esptool** — `esptool.py --chip esp32s3 write_flash 0x0 solo-<version>-<device>-merged.bin`

> [!NOTE]
> Building from source produces a second, app-only `firmware.bin` alongside the merged one. That one belongs at offset `0x10000` and only works if a bootloader is already on the chip — flashing it at `0x0`, or onto a freshly erased chip, leaves the device dead-silent. `pio run -e <env> -t upload` writes the bootloader, partition table and app at their correct offsets in one go, which is why it works where a hand-flashed single `.bin` does not. Releases only contain the merged image, so this only matters when building yourself.

### Connecting the companion app

Applies to every board: each binary serves the companion app over **both** BLE and USB serial, but not at once.

> [!IMPORTANT]
> BLE has priority over USB serial. While a BLE connection is active the USB protocol is suspended. To connect the app over USB, disconnect from BLE first or disable BLE directly on the device.

---

## Hardware setup — Heltec V3 / V4

A single button can't drive the solo UI, and neither Heltec board has a joystick or a keyboard of its own. Both stock builds therefore enable an **M5Stack CardKB** and a **wired joystick** on these pins — V3 and V4 are pin-compatible, so the assignment is identical for both:

| Function | GPIO | Notes |
| -------- | :--: | ----- |
| CardKB SDA | **3** | second I2C bus (`Wire1`) — *not* the OLED's 17/18 |
| CardKB SCL | **4** | |
| Joystick UP | **23** | |
| Joystick DOWN | **6** | |
| Joystick LEFT | **47** | |
| Joystick RIGHT | **48** | |
| Back button | **33** | required whenever the joystick is enabled |
| Centre / Enter | **0** | the onboard PRG button — nothing to wire |

Each joystick contact simply shorts its pin to GND — the firmware enables the internal pull-ups, so no external resistors are needed. CardKB needs power and ground alongside SDA/SCL; check your unit's own voltage rating before picking a rail.

> [!NOTE]
> This assignment is confirmed working on real **V4** hardware. V3 inherits it because Heltec documents the two boards as pin-compatible, but it hasn't been verified on a physical V3 — worth a continuity check against your own module before soldering.

Either device is enough on its own — pins with nothing attached read as not-pressed, and a missing CardKB is simply not detected at boot, so the unused half costs nothing. For a **CardKB-only** build, wire just SDA/SCL and set Settings › Keyboard › **Ext. KB = Compact**, which is designed to need no joystick at all.

These pins are only defaults: they live in the `[env:Heltec_v3_companion_solo_dual]` / `[env:heltec_v4_companion_solo_dual]` blocks in [`variants/heltec_v3/platformio.ini`](./variants/heltec_v3/platformio.ini) and [`variants/heltec_v4/platformio.ini`](./variants/heltec_v4/platformio.ini), with comments listing which GPIOs each board has already claimed if you want to wire yours differently. Full details in [External Keyboard & Joystick](./docs/solo_features/external_keyboard.md).

---

## Documentation

### This fork

| Document                                                                   | Description                                                           |
| -------------------------------------------------------------------------- | --------------------------------------------------------------------- |
| [Messages Screen](./docs/solo_features/message_screen/message_screen.md)   | Sending messages, context menus, reply, navigate to / save shared locations, Notif/Melody overrides |
| [Favourites Dial](./docs/solo_features/favourites_dial/favourites_dial.md) | Pinned contacts grid, unread badges, pin/unpin                        |
| [Clock Screen](./docs/solo_features/clock_screen/clock_screen.md)          | Clock page, date, configurable data fields, alarm / timer / stopwatch  |
| [Settings Screen](./docs/solo_features/settings_screen/settings_screen.md) | All settings sections with values and interactions                    |
| [Screen Lock](./docs/solo_features/screen_lock/screen_lock.md)             | Lock/unlock sequence, lock screen, auto-lock                          |
| [Tools Screen](./docs/solo_features/tools_screen/tools_screen.md)          | GPS trail & waypoints, compass, navigation, nearby nodes, ringtone editor, remote bot, auto-advert, live location sharing, locator, diagnostics, repeater, remote admin |
| [External Keyboard & Joystick](./docs/solo_features/external_keyboard.md)  | CardKB shortcuts, Full vs Compact mode, wired joystick, Heltec V3/V4 wiring |
| [Solo UI framework](./docs/design/solo_ui_framework.md)                    | **Developer guide** — the reusable building blocks (screens, lists, popups, mini-icons, geo/persistence helpers) and how to add a new feature |
| [Feature roadmap](./docs/development/roadmap.md)                           | **Developer notes** — planned / done / rejected features and the code-audit backlog |

### Upstream MeshCore

| Document                                           | Description                                      |
| -------------------------------------------------- | ------------------------------------------------ |
| [FAQ](./docs/faq.md)                               | Frequently asked questions                       |
| [CLI Commands](./docs/cli_commands.md)             | Commands for repeaters, room servers and sensors |
| [Terminal Chat CLI](./docs/terminal_chat_cli.md)   | Commands for the terminal chat client            |
| [Companion Protocol](./docs/companion_protocol.md) | Serial/BLE frame protocol between device and app |
| [Packet Format](./docs/packet_format.md)           | LoRa packet structure                            |
| [QR Codes](./docs/qr_codes.md)                     | Channel and contact QR code formats              |

---

## Solo Tools

All solo builds include screenshot and GPX trail export support out of the box — no special build flags required.

### Web app — nothing to install

Open [Solo Tools](https://marekzegare4.github.io/Solo-tools/) in a browser with Web Serial support (Chromium-based) and click **Connect device**:

- **Screenshot** — capture the current display contents as a PNG. Triggered entirely from the browser; nothing to press on the device.
- **GPX export** — stream the recorded GPS trail and download a timestamped `.gpx` file. Start it on the device with **Tools › Trail › Hold Enter › Export** once connected.

### Offline equivalents

The same two features are available as local scripts — `tools/screenshot.py` and `tools/trail_export.py` — plus a font converter. See [tools/README.md](./tools/README.md).

> [!IMPORTANT]
> Both routes use USB serial, which is suspended while a BLE connection is active — disconnect the companion app from BLE first. If the app is connected over **USB**, disconnect it too: the raw export stream would otherwise disrupt its frame protocol. (Over BLE the export is safe, since USB receive is ignored.)

---

## Development

This fork tracks the upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) repository. To prevent upstream changes from overwriting this README during merges, `README.md` is protected via `.gitattributes`. After cloning, run once:

```sh
git config merge.ours.driver true
```

### Building from source

| Environment | Device |
| ----------- | ------ |
| `WioTrackerL1_companion_solo_dual` | Wio Tracker L1 (OLED) |
| `WioTrackerL1Eink_companion_solo_dual` | Wio Tracker L1 (E-ink) |
| `GAT562_30S_Mesh_Kit_solo_dual` | GAT562 30S Mesh Kit |
| `Heltec_v3_companion_solo_dual` | Heltec LoRa32 V3 |
| `heltec_v4_companion_solo_dual` | Heltec LoRa32 V4 |

```sh
pio run -e <env>                                  # build only
pio run -e <env> -t upload                        # build and flash over USB
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware <env> # release artifacts into out/
```

The last command runs the same path CI does: a `.uf2` + DFU `.zip` on nRF52, or an app-only `.bin` plus a `-merged.bin` on ESP32. Releases carry the `.uf2`, the `.zip` and the `-merged.bin` only.

### Releasing

Pushing a `v*` tag runs [Build Solo Firmwares](./.github/workflows/build-solo-firmwares.yml), which discovers every `*_solo_dual` environment automatically, builds them all and opens a **draft** release with the artifacts attached. Write the notes from `release-notes.md` and publish it. See [RELEASE.md](./RELEASE.md) for the upstream companion/repeater/room-server tags.

### Repository layout

| Path | Contents |
| ---- | -------- |
| `examples/companion_radio/ui-new/` | the solo UI — screens, widgets, `UITask` |
| `src/helpers/ui/` | display drivers, fonts, buttons, buzzer |
| `variants/<board>/` | per-board `platformio.ini`, `target.h`, `target.cpp` |
| `docs/solo_features/` | user documentation for this fork |
| `docs/design/`, `docs/development/` | developer notes and the feature roadmap |
| `tools/` | host-side helpers (screenshot, GPX export, font conversion) |

### Contributing

Contributions are welcome. Fork the repository, make your changes, and open a pull request. Please follow the existing code style and keep changes focused.

---

## Contributors

Big thanks to the people who contributed to this fork:

- [vanous](https://github.com/vanous)
- [marczykm](https://github.com/marczykm)

Built on upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) and its [community](https://github.com/meshcore-dev/MeshCore/graphs/contributors).
