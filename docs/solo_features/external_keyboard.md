## External Keyboard & Joystick

[Go back](../../README.md)

Two optional, auto-detected hardware add-ons — a build with them enabled runs
the same with nothing plugged in. Two of the newer boards also ship with
their own **built-in** keypad instead —
see [Built-in keyboards](#built-in-keyboards-cardputer-adv-t-echo-lite--keyshield).

- **CardKB** — an M5Stack I2C QWERTY keyboard (address `0x5F`), for typing
  messages, names and labels without walking the on-screen letter grid.
- **Wired joystick** — four direction contacts plus a Back button, replacing the
  single-button navigation on boards that have no joystick of their own.

### Support by device

| Device | CardKB | Wired joystick |
| ------ | :----: | :------------: |
| Seeed Wio Tracker L1 (OLED) | ✅ Grove connector | onboard |
| Seeed Wio Tracker L1 (E-ink) | ✅ Grove connector | onboard |
| GAT562 30S Mesh Kit | — | onboard |
| Heltec V3 *(experimental)* | ✅ solder to free GPIOs | ✅ solder to free GPIOs |
| Heltec V4 *(experimental)* | ✅ solder to free GPIOs | ✅ solder to free GPIOs |
| M5Stack Cardputer ADV *(experimental)* | — | built-in keyboard instead, see below |
| LilyGO T-Echo Lite + KeyShield *(experimental)* | — | built-in keypad instead, see below |

---

## CardKB

Plug it into the second I2C bus (the Grove connector on the Wio Tracker L1;
see [Wiring](#wiring-heltec-v3--v4) for the Heltec boards). The firmware probes
for it once at boot — nothing to enable in Settings.

The same bus is scanned for environment sensors, so a CardKB and a sensor can
share it.

### Typing

Printable characters insert straight at the cursor, bypassing the on-screen grid
completely. The alphabet and T9/ABC settings do not apply — a real keyboard sends
the right character already, so typing is always plain Latin ASCII regardless of
what Settings › Keyboard is set to.

| Key | Action |
| --- | ------ |
| letters / digits / symbols / space | insert at the cursor |
| Backspace | delete the character before the cursor |
| Esc | cancel / back |
| Arrows | same as the joystick |
| Enter | same as the centre button |
| **Fn+Enter** | **submit the field** — no need to find the DONE cell |
| **Fn+letter** | open the accent popup for that letter (e.g. Fn+A → á à ä ã…) |
| **Tab** | the Hold-Enter equivalent, everywhere — context menus, shift-lock, clear-all |
| **Fn+Esc** | lock / unlock the device (single press, works in both directions) |

Fn+Esc rather than the adjacent Fn+Backspace on purpose: Fn and Backspace sit
next to each other on CardKB's layout and would be far too easy to hit by
accident. See [Screen Lock](./screen_lock/screen_lock.md) for the physical
button equivalent.

### Ext. KB — Full vs Compact

**Settings › Keyboard › Ext. KB** picks how the on-screen keyboard behaves while
a CardKB is doing the typing.

| Mode | Behaviour |
| ---- | --------- |
| **Full** (default) | The letter grid stays on screen. Arrows and Enter drive the grid exactly as physical buttons do, so CardKB and the joystick can be used interchangeably. |
| **Compact** | The grid, the special-row icons and the status line are all hidden — only the text being typed and two shortcut hints remain. Arrows move the **text cursor** directly, and plain Enter submits the field (same as Fn+Enter). |

**Compact is designed to need no joystick at all** — the right choice when
CardKB is the only input device, e.g. a Heltec V3/V4 with no joystick
soldered on.

Cursor mode and the accent / placeholder popups draw their own visible feedback,
so they behave identically in both modes.

---

## Wired joystick

Four direction contacts plus a fifth "press" contact. Each contact simply
shorts its pin to ground — the firmware enables the internal pull-ups, so no
external resistors are needed.

- The stick's own press contact drives the centre / Enter press — your thumb
  is already on the stick, so pressing it in is the natural "confirm" action.
- The board's existing user button (PRG on the Heltec boards) becomes Back
  instead. Back is **not** optional: the UI uses it unconditionally once the
  joystick is enabled. Triple-clicking Back toggles the buzzer.
- **Settings › Display › Joystick rotation** rotates the direction mapping at
  runtime (0–3), for a stick mounted sideways in a custom enclosure. It is
  independent of display rotation.

---

## Wiring (Heltec V3 / V4)

Neither board ships with a joystick or a keyboard header, so both are soldered to
free GPIOs. V3 and V4 are pin-compatible per Heltec's documentation and the solo
builds use the same assignment for both — **confirmed working on real V4
hardware**; still worth checking against your own V3 module before soldering.

| Function | GPIO | Notes |
| -------- | ---- | ----- |
| CardKB SDA | 3 | second I2C bus (`Wire1`) — *not* the OLED's 17/18 |
| CardKB SCL | 4 | |
| Joystick UP | 23 | |
| Joystick DOWN | 6 | |
| Joystick LEFT | 47 | |
| Joystick RIGHT | 48 | |
| Joystick press — Enter | 33 | the stick's own fifth contact; required when the joystick is enabled |
| Back | 0 | the onboard PRG button — nothing to wire |

Everything above lives in the `[env:Heltec_v3_companion_solo_dual]` /
`[env:heltec_v4_companion_solo_dual]` blocks in
[`solo/heltec_v3/platformio.ini`](../../solo/heltec_v3/platformio.ini)
and [`solo/heltec_v4/platformio.ini`](../../solo/heltec_v4/platformio.ini),
with comments explaining which pins are safe to reuse. To build a CardKB-only
device, comment out the joystick block and set Ext. KB to Compact.

---

## Built-in keyboards (Cardputer ADV, T-Echo Lite + KeyShield)

*Experimental* — newly-added board support, not the CardKB/joystick add-ons
above. Both keypads are TCA8418-based and share one polling path, entirely
independent of the CardKB code — a board can have either, or neither.

- **M5Stack Cardputer ADV** — built-in QWERTY, no CardKB or joystick needed.
- **LilyGO T-Echo Lite + KeyShield** — the KeyShield add-on gives the T-Echo
  Lite a T9 keypad; without it the board has no usable input for the solo UI.

Neither keypad follows CardKB's exact Fn-shortcut table (Fn+Enter,
Fn+letter accent popups, Tab, Fn+Esc lock) — see each board's own
keyboard driver under `variants/` and its solo `platformio.ini` under `solo/`
for its current keymap.
