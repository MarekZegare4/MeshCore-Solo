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
| Auto-lock                            | on / off                         | Locks device when display turns off                                                                   |
| Battery                              | icon / % / V                     | Display mode for the top-bar battery indicator                                                        |
| Clock seconds                        | show / hide                      | Hiding reduces OLED refresh from 1 s to 60 s                                                          |
| Clock format                         | 24 h / 12 h                      | 12 h appends AM/PM                                                                                    |
| Display rotation _(e-ink only)_      | 0° / 90° / 180° / 270°           | Applied immediately                                                                                   |
| Joystick rotation _(e-ink only)_     | 0° / 90° / 180° / 270°           | Rotates input mapping independently of display rotation; useful for custom enclosures                 |
| Full refresh interval _(e-ink only)_ | off / 5 / 10 / 20 / 30           | Partial refreshes between full clears; reduces ghosting on long sessions                              |

---

### Sound

| Setting        | Options                        | Notes                                                        |
| -------------- | ------------------------------ | ------------------------------------------------------------ |
| Buzzer         | On / Off / Auto                | Auto: silences while BLE connected, re-enables on disconnect |
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
| Pwr save  | ON / OFF   | **Battery saver.** Hardware duty-cycle receive: the SX126x cycles RX↔sleep on its own and wakes on a preamble, cutting average RX current. Trades a little receive latency; leave OFF for lowest-latency reception. Requires an SX126x radio (otherwise stays on continuous RX). **Forced off (shown as `--`) while the repeater is on** — a repeater must listen continuously; your setting is restored when the repeater is switched off. A background watchdog recovers automatically if the duty-cycle sequencer ever gets stuck (soft re-arm, then a full radio reset) — see Tools › Diagnostics for the recovery counts. |
| Auto pwr  | ON / OFF   | **Adaptive Power Control.** Lowers actual TX power on strong links to save energy, ramping back up — to the **TX Pwr** ceiling — on weak or lost links. Link quality comes from direct-message ACK SNR and, for channel messages (no ACK), from hearing a repeater rebroadcast your packet. The radio page / name bar shows the live power. Default OFF (fixed TX power). **Suppressed (shown as `--`) while the repeater is on** — a repeater holds full TX power for consistent relay reach; your setting is restored when the repeater is switched off. |

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
| Low battery | off / 3.0 V / 3.1 V / 3.2 V / 3.3 V / 3.4 V / 3.5 V | Auto-shutdown threshold; also sets the 0 % anchor for the battery percentage indicator |
| GPS pwr _(if GPS detected)_ | off / 1 min / 5 min / 15 min / 30 min / 1 h | **Battery saver.** Cycles GPS off between fixes instead of running it continuously; each wake waits for a fix (up to 60 s) before sleeping again for the chosen interval. Automatically stays continuously on regardless of this setting whenever something needs a live position — an active Trail recording, Map › Live share, an armed Locator, the Compass / Nearby navigate view, or an in-flight `!gps fix` bot request. `off` (default) matches earlier releases: GPS runs continuously whenever enabled. The GPS status icon blinks while napping between fixes. |
| Units       | Metric / Imperial                                   | Global unit system for every distance/speed shown in Tools (Nearby Nodes, Trail, navigate-to-point). Metric: m / km, km/h, min/km. Imperial: ft / mi, mph, min/mi |
| Reboot      | action (**Enter**)                                  | Restarts this device. Pending setting changes are saved first. Last row, so it isn't the default-selected one |

---

### Keyboard

| Setting  | Options    | Notes                                                                                              |
| -------- | ---------- | -------------------------------------------------------------------------------------------------- |
| Layout   | ABC / T9   | On-screen keyboard style. **ABC**: an a-b-c…z grid, one key per letter (the original layout). **T9**: phone-keypad multi-tap — each key is labelled with its **digit** and a letter group (e.g. `2abc`); repeated **Enter** presses cycle through the letters and then the digit itself. Applies to whichever script page is active (see Main/Additional below), not just Latin. |
| Main | Latin / Cyrillic / Greek | Which script the keyboard **opens on by default**. **Latin** (default) matches earlier releases; pick **Cyrillic** or **Greek** here instead to make that script the one you land on every time, with Latin becoming the one reached via cycling (see Additional below) instead of the other way round. |
| Additional | Latin / Cyrillic / Greek | The second script added to the same **#@/abc** key's cycle (Main → Additional → Symbols → Main) — no separate key to switch scripts. Setting Additional to the **same** script as Main drops the cycle back to just that script plus Symbols (no second script page at all). **Greek** covers the 24-letter alphabet plus final sigma (`ς`) but not the tonos stress accents used in proper Modern Greek spelling. Every script's letters render natively — the display font (a single unified Unicode font used everywhere on-screen) covers all of them, no separate toggle needed. |

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
| Resend  | off / 1×–5×    | Auto-resend an on-device direct message this many times when no delivery ACK is received (default 2×) |

Up to 10 quick reply templates (Q1–Q10). Press **Enter** on a slot to open the keyboard editor. Supports the same placeholders as the main keyboard (`{time}`, `{loc}`, and sensor placeholders when connected).
