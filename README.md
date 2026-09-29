<p align="center"><img src="./docs/solo/img/hero.png" alt="MeshCore Solo" width="640"></p>

# MeshCore Solo Companion Firmware

A fork of the official [MeshCore](https://github.com/meshcore-dev/MeshCore) companion radio firmware with a full standalone on-device UI — messages, contacts, GPS navigation and tools without a phone.

**Try it live in your browser: [solo.marekzegarek.com](https://solo.marekzegarek.com)**

[![Buy Me a Coffee](https://img.shields.io/badge/Buy%20me%20a%20coffee-FFDD00?logo=buymeacoffee&logoColor=black)](https://buymeacoffee.com/MarekZegarek)

Discussion: [MeshCore Discord](https://discord.gg/sdhYArU2jr) — [Solo firmware thread](https://discord.com/channels/1495203904898728149/1505294337884553447)

---

## Supported devices

| Device | MCU | Display | Firmware file |
| ------ | --- | ------- | ------------- |
| Seeed Wio Tracker L1 (OLED) | nRF52840 | SSD1306 / SH1106 128 × 64 | `solo-<version>-WioTrackerL1.uf2` |
| Seeed Wio Tracker L1 (E-ink) | nRF52840 | GxEPD2 250 × 122 | `solo-<version>-WioTrackerL1Eink.uf2` |
| Seeed Wio Tracker L2 | ESP32-S3 | 320 × 240 touch LCD | `solo-<version>-Wio-Tracker-L2-merged.bin` |
| GAT562 30S Mesh Kit | nRF52840 | SSD1306 128 × 64 | `solo-<version>-GAT562-30S-Mesh-Kit.uf2` |
| GAT562 Mesh Watch13 *(experimental)* | nRF52840 | SSD1306 128 × 64 | `solo-<version>-GAT562-Mesh-Watch13.uf2` |
| Heltec LoRa32 V3 *(experimental)* | ESP32-S3 | SSD1306 128 × 64 | `solo-<version>-Heltec-v3-merged.bin` |
| Heltec LoRa32 V4 *(experimental)* | ESP32-S3 | SSD1306 128 × 64 | `solo-<version>-heltec-v4-merged.bin` |
| M5Stack Cardputer ADV *(experimental)* | ESP32-S3 | ST7789 TFT 240 × 135 | `solo-<version>-M5Stack-Cardputer-ADV-merged.bin` |
| LilyGO T-Echo Lite + KeyShield *(experimental)* | nRF52840 | GxEPD2 250 × 122 | `solo-<version>-LilyGo-T-Echo-Lite-keyshield.uf2` |
| ProMicro *(experimental)* | nRF52840 | SSD1306 128 × 64 | `solo-<version>-ProMicro.uf2` |

Firmware files are on the [releases page](https://github.com/MarekZegare4/MeshCore-Solo/releases). Every binary serves the companion app over both BLE and USB serial.

Heltec V3/V4 need [a keyboard or joystick wired up](./docs/solo/hardware.md#wiring-on-the-heltec-v3--v4); ProMicro needs a CardKB; the T-Echo Lite needs the KeyShield. The rest work out of the box.

---

## Flashing

> [!WARNING]
> Coming from official or other custom firmware: back up your data in the companion app, then **Erase flash** in the [MeshCore Flasher](https://meshcore.io/flasher) before flashing. Updating from an earlier Solo release doesn't need this unless the release notes say so.

**nRF52840 boards** — press reset twice quickly; the device shows up as a USB drive. Copy the `.uf2` onto it.

**ESP32-S3 boards** — flash the `-merged.bin` at offset `0x0` with the [MeshCore Flasher](https://meshcore.io/flasher) or `esptool.py --chip esp32s3 write_flash 0x0 <file>-merged.bin`. The Wio Tracker L2 then updates itself over WiFi (Settings › System › Firmware update).

> [!IMPORTANT]
> BLE has priority over USB serial: while a BLE connection is active the USB protocol is suspended.

---

## Documentation

[Documentation](./docs/solo/README.md) — getting started, messages, navigation, tools, settings, hardware and developer guides.

**USB tools** — [on the Solo site](https://solo.marekzegarek.com/#pc-tools) (Chrome or Edge, Web Serial): screenshots of the device's screen and the GPS trail as GPX over USB, plus a preview of any GPX file. The same as the local scripts in [tools/](./tools/README.md).

---

## Building

```sh
pio run -e <env> -t upload                                  # build and flash over USB
FIRMWARE_VERSION=v1.0.0 bash build.sh build-firmware <env>  # release artifacts into out/
```

Environments are the `*_solo_dual` (OLED / e-ink) and `*_solo_lvgl` (touch) entries in `solo/<board>/platformio.ini`. Optional hardware (CardKB, joystick, GPIO, buzzer…) is enabled with [build flags](./docs/solo/developer/build-flags.md). Releasing: [RELEASE.md](./RELEASE.md).

This README is protected from upstream merges via `.gitattributes`; after cloning run once `git config merge.ours.driver true`.

---

## Contributors

Big thanks to [vanous](https://github.com/vanous), [marczykm](https://github.com/marczykm), [tchellow](https://github.com/tchellow) and [3urobeat](https://github.com/3urobeat).

Built on upstream [MeshCore](https://github.com/meshcore-dev/MeshCore) and its [community](https://github.com/meshcore-dev/MeshCore/graphs/contributors).
