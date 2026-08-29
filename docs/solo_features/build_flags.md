## Build Flags

[Go back](../../README.md)

Reference for the `-D` build flags a Solo build understands beyond the
per-board defaults already set in `solo/<board>/platformio.ini`. Add any of
these to your own board's `build_flags` (in `solo/<board>/platformio.ini`) to
enable optional hardware you've wired up yourself, or to tune a default.
Everything below is a no-op when left unset — adding a flag for hardware that
isn't there costs nothing and can't brick a build; the exceptions (pin
conflicts, wrong polarity) are called out per flag.

None of this needs touching to get a board running — see the
[environment table](../../README.md#building-from-source) for the flag-free
default build for each supported board.

---

### Already part of every solo build

Set once per board in `solo/<board>/platformio.ini`, not usually touched
per-user. Listed here so the rest of this page can assume them.

| Flag | Meaning |
| --- | --- |
| `FIRMWARE_SOLO_BUILD=1` | Marks the build as Solo (full on-device UI) rather than a companion-only firmware. |
| `DUAL_SERIAL=1` | The companion app can attach over BLE *or* USB serial, whichever it finds; BLE wins if both are live. Every solo build sets this — Solo standardises on one build per board rather than splitting BLE-only / USB-only variants. |
| `MAX_CONTACTS=<n>` | Size of the contact table. Default is 32 if unset; solo builds set 350. |
| `MAX_GROUP_CHANNELS=<n>` | Size of the channel list. Required for channel support to compile in at all — not optional the way the rest of this page is. |
| `OFFLINE_QUEUE_SIZE=<n>` | How many messages queue for later delivery while the phone app is disconnected. Default 16; solo builds set 256. |
| `UI_SENSORS_PAGE=1` | Enables the on-device sensors dashboard page. |
| `BLE_PIN_CODE=<n>` | See below — not a plain fixed value in practice. |
| `DISPLAY_CLASS=<Class>` | Selects the display driver (e.g. `SSD1306Display`, `GxEPDDisplay`, `ST7789Display` — see `src/helpers/ui/` for the full set). Fixed by whatever panel the board actually has; only relevant if you're wiring on a *different* display than stock, in which case the matching driver's `.cpp` also needs adding to `build_src_filter`. |

**`BLE_PIN_CODE`** has a special case baked in: if it's left at the literal
value `123456` *and* the board has a display, pairing uses a random 6-digit
PIN generated fresh each session and shown on-device, rather than a fixed one.
Any other numeric value is used as a static PIN instead. Leaving the flag out
entirely disables the PIN prompt (BLE pairing is unauthenticated).

---

### Input hardware add-ons

Each of these assumes you're wiring something up yourself — check pins are
actually free on your board first (see the board's `variant.h` and its
existing `solo/<board>/platformio.ini` for what's already claimed).

| Flag | Adds |
| --- | --- |
| `ENV_PIN_SDA` / `ENV_PIN_SCL` | CardKB (M5Stack I2C keyboard, addr `0x5F`) on a second I2C bus (resolves to `Wire1`). Probed at boot — harmless with nothing plugged in. See [External Keyboard & Joystick](./external_keyboard.md). |
| `CARDKB_I2C=<Wire\|Wire1>` | Same CardKB support, naming the bus directly — for a board with no free pins for a second bus, set `CARDKB_I2C=Wire` to share the primary bus (already used by the display/RTC) instead of defining `ENV_PIN_SDA`/`ENV_PIN_SCL`. Takes precedence if both are somehow set. |
| `UI_HAS_JOYSTICK=1` + `UI_HAS_JOYSTICK_UPDOWN=1` (optional) + `JOYSTICK_UP` / `JOYSTICK_DOWN` / `JOYSTICK_LEFT` / `JOYSTICK_RIGHT` + `PIN_USER_BTN` + `PIN_BACK_BTN` | A wired joystick (four direction contacts + a press contact for Enter). Replaces single-button navigation entirely once enabled. See [External Keyboard & Joystick](./external_keyboard.md). |
| `PIN_GPIO1` .. `PIN_GPIO4` | Up to four general-purpose pins, each independently switchable between Off / Input / Output (GPIO1/GPIO2 also get an Analog step, if wired to an ADC-capable pin) from Tools › GPIO, and via the `!gpio1`..`!gpio4` bot commands. Not restricted to any particular board — works anywhere the pins are actually free. See [Tools Screen › GPIO](./tools_screen/tools_screen.md#gpio). |
| `PIN_HALL_SENSOR` + `HALL_ACTIVE_HIGH=1` (optional) | A Hall-effect or reed sensor for a magnetic flip cover: closing locks and blanks the screen instantly, opening unlocks and wakes it, no combo either way. `HALL_ACTIVE_HIGH` is only for a module wired to pull the pin high (rather than low) when the magnet is near. See [Screen Lock › Magnetic cover](./screen_lock/screen_lock.md#magnetic-cover-hall-sensor). |
| `PIN_GPS_SWITCH` | A physical on/off switch for GPS power, read alongside the software GPS toggle — the Tools screen shows `gps off(hw)` / `gps off(sw)` when the two disagree, instead of silently trusting one over the other. |

---

### Output / feedback hardware

| Flag | Adds |
| --- | --- |
| `PIN_BUZZER` (+ `PIN_BUZZER_EN`, optional) | A passive piezo buzzer, driven by direct PWM (nRF52) or the `NonBlockingRtttl` library (everything else) for RTTTL ringtones and UI beeps. `PIN_BUZZER_EN` is an optional enable line some boards wire separately from the signal pin. |
| `PIN_VIBRATION` | A vibration motor for haptic notification, via `GenericVibration`. |

---

### GPS

| Flag | Meaning |
| --- | --- |
| `ENV_INCLUDE_GPS=1` | Compiles in GPS support (NMEA parsing, location provider) at all. |
| `GPS_BAUD_RATE=<n>` | Baud rate for the GPS module's serial link (`Serial1`). Match your module's default. |

`PIN_GPS_SWITCH` (above) is independent of both — it's a hardware kill switch
layered on top of GPS support, not a requirement for it.

---

### Display & battery tuning

| Flag | Meaning |
| --- | --- |
| `OLED_MISC_FIXED_FONT=1` | Pulls in a full Latin/Greek/Cyrillic 6×9 fixed font (~14 KB flash) so typed text in those alphabets renders as itself instead of block placeholders. Worth it on any board with a keyboard; skip it on space-constrained builds without one. |
| `DISPLAY_ROTATION=<0-3>` | Rotates the panel in 90° steps, for a board mounted sideways or upside down. |
| `ENABLE_SCREENSHOT` | Lets Solo Tools read the framebuffer over USB to capture a screenshot. |
| `KEEP_DISPLAY_ON_USB` | Refreshes the auto-off deadline continuously while externally (USB) powered, so the auto-off timer only starts counting once power is actually removed. Off by default because OLED panels burn in quickly with a permanently-lit screen — only worth enabling for an LCD/e-ink target, or a display you don't mind replacing. |
| `AUTO_OFF_MILLIS=<ms>` | How long the display stays on before auto-off. Default 15000 (15s); `0` disables auto-off entirely. |
| `UI_RECENT_LIST_SIZE=<n>` | How many entries the recent-activity lists show before scrolling. Default 4. |

---

### Misc

| Flag | Meaning |
| --- | --- |
| `ADVERT_NAME='"name"'` | Sets the default node name baked into a fresh device, instead of the hex of the first 4 bytes of its public key. Note the doubled quoting — it's a C string literal passed through a build flag. |
| `MESHCORE_VERSION='"x.y"'` | Overrides the upstream MeshCore base-version string shown in diagnostics, for boards whose port hasn't been rebased onto the latest yet. Cosmetic only — doesn't change protocol behaviour. |
