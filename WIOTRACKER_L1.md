# Wio Tracker L1 — Extended Companion Radio Firmware

This branch extends the official MeshCore companion radio firmware for the **Seeed Wio Tracker L1** (NRF52840 + SX1262 + SH1106 OLED 128×64 + GPS + buzzer + 5-way joystick).

## New Build Variants

In addition to the standard `_companion_radio_usb` and `_companion_radio_ble` builds, two new variants are available with a full **settings screen**:

| Environment | Interface |
|---|---|
| `WioTrackerL1_companion_radio_usb_settings` | USB serial |
| `WioTrackerL1_companion_radio_ble_settings` | Bluetooth LE |

The settings screen is accessed by navigating to the last page of the home screen and pressing Enter.

## New Features

### Settings Screen

All settings are saved to flash and restored on next boot.

| Setting | Options | Default |
|---|---|---|
| Display brightness | 5 levels (very dim → max) | Medium |
| Buzzer | ON / OFF | ON |
| TX Power | 2 – 22 dBm | Board default |
| Auto-off delay | 5 s / 15 s / 30 s / 60 s / never | 15 s |
| GPS update interval | off / 30 s / 1 min / 5 min / 15 min / 30 min | off |
| Timezone | UTC−12 .. UTC+14 | UTC |
| Low battery shutdown | off / 3.0 V – 3.5 V | 3.4 V |
| Battery display | icon / % / voltage | icon |

### Clock Screen

A dedicated clock page on the home screen shows the current time and date, synchronized from GPS. Timezone is applied from the settings above.

### Battery Display Modes

The top-right battery indicator can be switched between:
- **Icon** — classic fill bar
- **%** — percentage, where 0% = low battery shutdown threshold (if set), otherwise 3.2 V
- **V** — live voltage (e.g. `3.84V`)

Battery voltage is filtered with an exponential moving average (α = 0.2) to smooth out ADC noise from uneven load.

### Low Battery Auto-Shutdown

When the filtered battery voltage drops below the configured threshold, the device displays a warning on screen for 2 seconds and then shuts down gracefully. The threshold is configurable from the settings screen (3.0 V – 3.5 V) or can be disabled.

## Bug Fixes

- **Buzzer mute at startup** — the saved mute preference is now respected during the startup sound; previously the buzzer was always unmuted on boot regardless of the saved setting.
- **Brightness change delay** — brightness changes are now applied instantly without triggering a flash write on every keypress; flash is written only when exiting the settings screen.

## Build & Release

Firmware is built automatically via GitHub Actions on every `wio-tracker-*` tag push.
Pre-built `.uf2` files are published in the [Releases](../../releases) section.

To flash: copy the `.uf2` file to the device while it is in bootloader mode (double-press reset).

## Syncing with Upstream

This branch tracks [meshcore-dev/MeshCore](https://github.com/meshcore-dev/MeshCore).
To pull upstream updates:

```bash
git fetch upstream
git rebase upstream/main
git push origin wio-tracker-l1-improvements --force-with-lease
```
