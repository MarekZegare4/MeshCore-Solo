# Hardware

## External keyboard and joystick

Two optional add-ons, detected at boot; a build with them enabled works the
same with nothing plugged in.

| Device | CardKB | Wired joystick |
| ------ | :----: | :------------: |
| Wio Tracker L1 (OLED / e-ink) | Grove connector | built in |
| GAT562 30S Mesh Kit | — | built in |
| Heltec V3 / V4 | soldered (below) | soldered (below) |
| ProMicro | on the main I2C bus (required: it has no buttons) | — |
| Cardputer ADV, T-Echo Lite + KeyShield | built-in keyboard instead | — |

### CardKB

An M5Stack CardKB types straight into any text field. It sends plain
characters, so the keyboard alphabet settings don't apply to it.

| Key | Does |
| --- | ---- |
| Arrows, Enter, Esc | Same as the joystick, Enter and Back |
| Backspace | Deletes before the cursor |
| **Fn+Enter** | Submits the field |
| **Fn+letter** | Accents for that letter (Fn+A → á à ä…) |
| **Tab** | Hold Enter (options menus) |
| **Fn+Esc** | Locks / unlocks the screen |

Settings › Keyboard › **Ext. KB**: **Full** keeps the on-screen grid, so the
CardKB and the joystick can be mixed; **Compact** hides it, the arrows move the
text cursor and Enter submits. Compact needs no joystick at all.

### Wired joystick

Four direction contacts and a press contact (Enter), each shorted to ground
when pressed; the firmware enables the pull-ups, so no resistors are needed.
The board's own button becomes Back. Settings › Display › **Joystick
rotation** turns the directions for a stick mounted sideways.

### Wiring on the Heltec V3 / V4

Neither board has a joystick or a keyboard connector, so both are soldered to
free pins. V3 and V4 use the same pins (confirmed on a V4).

| Function | GPIO |
| -------- | :--: |
| CardKB SDA / SCL | 3 / 4 (second I2C bus, not the display's) |
| Joystick up / down / left / right | 23 / 6 / 47 / 48 |
| Joystick press (Enter) | 33 |
| Back | 0 (the PRG button, nothing to wire) |

The pins are set in [`solo/heltec_v3/platformio.ini`](../../solo/heltec_v3/platformio.ini)
and [`solo/heltec_v4/platformio.ini`](../../solo/heltec_v4/platformio.ini).
For a CardKB-only build, comment out the joystick block there and use
Ext. KB = Compact.

### Built-in keyboards

The Cardputer ADV has a QWERTY keyboard, the T-Echo Lite a T9 keypad on its
KeyShield add-on (without it the board has no usable input). Their keymaps
are in each board's driver under `variants/`; they don't follow the CardKB's
Fn shortcuts.

## E-ink

The e-ink Wio Tracker L1 (250 × 122) has a few settings of its own in
Settings › Display:

- **Rotation**: landscape or portrait, applied at once; every screen reflows.
- **Joystick rotation**, independent of the display's.
- **Full refresh**: how many partial updates between full ones, against
  ghosting.

Clock seconds are hidden by default, and live timers refresh coarsely, to
spare the panel.

## Wio Tracker L2

- **Buttons**: the top one turns the screen off and on; the side one goes
  home, and held and let go mutes the sound. Side + top takes a screenshot
  (a BMP in `screenshots/` on the SD card); the site's USB tools take one over
  USB.
  Holding the side button in the first seconds after power-on starts the CLI
  rescue on USB serial.
- **SD card**: holds the message history, maps, GPX trails and live map tiles.
  Settings › Storage shows what takes the space, how many messages each
  conversation keeps, and deletes the history.
- **USB drive**: plugged into a computer, the device asks whether to keep the
  SD card or lend it to the computer. Charging and the USB connection (the
  site's USB tools, the app) work either way. While lent, the device can't use
  the card; eject it on the computer and the device restarts. With a screen
  PIN set, it doesn't ask until the screen is unlocked.
- **WiFi**: used only for map downloads, live tiles and updates, and off the
  rest of the time. Settings › WiFi saves several networks and joins the
  strongest; the WiFi switch in Settings forbids it entirely.

## Build flags

Extra hardware (a buzzer, a vibration motor, a Hall sensor for a magnetic
cover, GPIO, an external PA) is enabled with build flags in your own
`solo/<board>/platformio.ini`; see [Build flags](./developer/build-flags.md).
