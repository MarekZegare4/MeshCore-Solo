## Settings Screen

[Go back](../../../README.md)

### Overview

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

All settings are saved to flash and restored on next boot. Settings are organised into collapsible sections. Press **Enter** on a section header to expand or collapse it — all sections start collapsed for faster navigation. Press **LEFT/RIGHT** to change a value, or **Enter** for toggle items.

Press **Cancel/Back** to save and return to the home screen.

---

### Display

| Setting                              | Options                          | Notes                                                                                                 |
| ------------------------------------ | -------------------------------- | ----------------------------------------------------------------------------------------------------- |
| Brightness                           | 1–5                              | LEFT/RIGHT; preview applies immediately                                                               |
| Auto-off                             | 5 s / 15 s / 30 s / 60 s / never | LEFT/RIGHT                                                                                            |
| Auto-lock                            | ON / OFF                         | Locks device when display turns off                                                                   |
| Battery                              | icon / % / V                     | Display mode for the top-bar battery indicator                                                        |
| Clock seconds                        | show / hide                      | Hiding reduces OLED refresh from 1 s to 60 s                                                          |
| Clock format                         | 24 h / 12 h                      | 12 h appends AM/PM                                                                                    |
| Display rotation _(e-ink only)_      | 0° / 90° / 180° / 270°           | Applied immediately                                                                                   |
| Joystick rotation _(e-ink only)_     | 0° / 90° / 180° / 270°           | Rotates input mapping independently of display rotation; useful for custom enclosures                 |
| Full refresh interval _(e-ink only)_ | OFF / 5 / 10 / 20 / 30           | Partial refreshes between full clears; reduces ghosting on long sessions                              |

---

### Sound

| Setting        | Options                        | Notes                                                        |
| -------------- | ------------------------------ | ------------------------------------------------------------ |
| Buzzer         | ON / OFF / Auto                | Auto: silences while BLE connected, re-enables on disconnect |
| Volume         | 1–5                            | LEFT/RIGHT; preview tone plays on each change                |
| DM Melody      | built-in / Melody 1 / Melody 2 / None | Notification sound for incoming private messages. `None` disables the sound for this event. |
| Channel Melody | built-in / Melody 1 / Melody 2 / None | Notification sound for incoming channel messages. `None` disables the sound for this event. |
| AD sound       | built-in / Melody 1 / Melody 2 / None | Sound played whenever an **advert** is received from *any* node — pairs with Auto-Advert as an audible "in range" heartbeat (see Tools › Auto-Advert). `None` disables the sound for this event. |
| AD scope       | All / Zero-hop                | Filters the AD sound so it plays for every advert or only for local zero-hop adverts. |

Melody 1 and Melody 2 are custom sequences editable in **Tools › Ringtone Editor**.

---

### Home Pages

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./homepages_oled.png) | ![](./homepages_eink.png) |

Lists all available home screen pages. For each entry:

- **LEFT / RIGHT** — move the page earlier or later in the navigation sequence
- **Enter** — toggle the page ON / OFF

**Settings** and **Messages** are always visible and cannot be disabled.

---

### Radio

| Setting   | Options    | Notes                                                                                                                                                              |
| --------- | ---------- | ------------------------------------------------------------------------------------------------------------------------------------------------------------------ |
| TX Pwr    | 2–22 dBm   | LEFT/RIGHT. With **Auto pwr** on this is the *ceiling* — the radio may transmit lower. |
| Preset    | named presets | LEFT/RIGHT cycles community RF presets (region frequency + bandwidth/SF/CR). **Enter** opens a popup to pick one, save the current settings as a named preset, or delete a saved one. Applies frequency, bandwidth, SF and CR together. |
| Freq      | chip range | **Enter** opens a digit-by-digit editor: LEFT/RIGHT moves between decimal places, UP/DOWN steps that digit. Bounds come from the radio chip's own validated range, so a value the radio would reject can't be entered. |
| SF        | 5–12       | LEFT/RIGHT. Spreading factor. |
| BW        | 7.8–500 kHz | LEFT/RIGHT cycles the standard LoRa bandwidths. |
| CR        | 5–8        | LEFT/RIGHT. Coding rate (4/5–4/8). |
| Pwr save  | ON / OFF   | **Battery saver.** Hardware duty-cycle receive (SX126x only): cycles RX↔sleep, wakes on preamble, cuts average RX current at the cost of some latency. **Forced off (`--`) while the repeater is on** — restored once it's switched off. A background watchdog auto-recovers if the sequencer gets stuck (soft re-arm, then a full reset) — see Tools › Diagnostics for the counts. |
| Auto pwr  | ON / OFF   | **Adaptive Power Control.** Lowers TX power on strong links, ramps back to the **TX Pwr** ceiling on weak/lost ones. Link quality from DM ACK SNR, or — for channels (no ACK) — a repeater's rebroadcast. Live power shown on the radio page/name bar. Default OFF. **Suppressed (`--`) while the repeater is on** — restored once it's switched off. |

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./radio_oled.png) | ![](./radio_eink.png) |

<!-- screenshot pending: Radio — preset popup (pick/save/delete) and/or the digit-by-digit frequency editor -->

The **repeater** mode and its flood filters live on their own screen — see **Tools › Repeater**.

---

### System

| Setting     | Options                                             | Notes                                                                                  |
| ----------- | --------------------------------------------------- | -------------------------------------------------------------------------------------- |
| Name        | keyboard entry (up to 31 chars)                     | This device's node name, shown to others and in every advert. **Enter** opens the keyboard pre-filled with the current name; applied and saved on submit |
| Timezone    | −12 h … +14 h                                       | UTC offset in whole hours                                                              |
| Low battery | OFF / 3.0 V / 3.1 V / 3.2 V / 3.3 V / 3.4 V / 3.5 V | Auto-shutdown threshold; also sets the 0 % anchor for the battery percentage indicator |
| GPS pwr _(if GPS detected)_ | OFF / 1 min / 5 min / 15 min / 30 min / 1 h | **Battery saver.** Cycles GPS off between fixes; each wake waits up to 60 s for a fix before sleeping again. Stays continuously on whenever something needs a live position — Trail recording, Live share, an armed Locator, Compass/Nearby, or an in-flight `!gps fix`. `OFF` (default) = always-on, as before. Status icon blinks while napping. |
| Units       | Metric / Imperial                                   | Global unit system for every distance/speed shown in Tools (Nearby Nodes, Trail, navigate-to-point). Metric: m / km, km/h, min/km. Imperial: ft / mi, mph, min/mi |
| Reboot      | action (**Enter**)                                  | Restarts this device. Pending setting changes are saved first. Last row, so it isn't the default-selected one |

---

### Keyboard

| Setting  | Options    | Notes                                                                                              |
| -------- | ---------- | -------------------------------------------------------------------------------------------------- |
| Layout   | ABC / T9   | On-screen keyboard style. **ABC**: a-b-c…z grid, one key per letter. **T9**: phone-keypad multi-tap — each key labelled digit+letters (e.g. `2abc`); repeated **Enter** cycles the letters then the digit. Applies to whichever script page is active, not just Latin. |
| Main | Latin / Cyrillic / Greek | Which script the keyboard opens on by default. **Latin** is the default; picking **Cyrillic** or **Greek** makes that the one you land on, with Latin moving to the Additional cycle instead. |
| Additional | Latin / Cyrillic / Greek | The second script in the **#@/abc** key's cycle (Main → Additional → Symbols → Main). Setting it to the same script as Main drops the cycle to just that script plus Symbols. **Greek** covers the 24-letter alphabet plus final sigma (`ς`), not the tonos stress accents. Every script renders natively via one shared Unicode font — no separate toggle needed. |

Applies to every on-screen text field (messages, waypoint labels, room passwords, preset names). Earlier releases labelled the grid *QWERTY*; the layout has always been alphabetical, so it is now named **ABC**.

European Latin-diacritic letters (Polish, Czech, Slovak, German, French, Spanish, Portuguese, Nordic, etc.) aren't separate alphabet pages — instead, **Hold Enter** on a plain Latin letter that has accented variants (`a c d e i l n o r s t u y z`) opens a one-row popup of its accents (e.g. holding `a` offers `á à â ã ä å ą`); **LEFT/RIGHT** picks, **Enter** inserts it, **Cancel** dismisses with no change. Holding a letter with no accented variants (e.g. `b`) does nothing. Works on whichever page is currently showing Latin, whether that's Main or Additional.

---

### Contacts

| Setting  | Options          | Notes                                                |
| -------- | ---------------- | ---------------------------------------------------- |
| DMs      | all / favourites | Show all chat contacts or only upstream-starred ones |
| Channels | all / favourites | Show all channels or only favourited ones            |
| Rooms    | all / favourites | Show all room servers or only favourited ones        |

---

### Messages

| Setting | Options        | Notes                                                                                          |
| ------- | -------------- | ---------------------------------------------------------------------------------------------- |
| Resend  | OFF / 1×–5×    | Auto-resend an on-device direct message this many times when no delivery ACK is received (default 2×) |

Up to 10 quick reply templates (Q1–Q10). Press **Enter** on a slot to open the keyboard editor. Supports the same placeholders as the main keyboard (`{time}`, `{loc}`, and sensor placeholders when connected).
