## Clock Screen

[Go back](../../../README.md)

### Overview

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./overview_oled.png) | ![](./overview_eink.png) |

A full-screen clock page on the home screen. Shows the current time and date, with up to three configurable data fields below.

Time is synchronized from GPS or via the companion app. Timezone offset is applied from **Settings › System**.

If no time source is available, the screen shows _"! No time sync"_ with a hint to enable GPS or connect the app.

---

### Time display

- **Format** — 24 h or 12 h with AM/PM; configurable in **Settings › Display**
- **Seconds** — shown by default on OLED; hidden on e-ink (always) and optionally on OLED via **Settings › Display › Clock seconds**; hiding reduces the refresh rate from 1 s to 60 s

---

### Data fields

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./fields_oled.png) | ![](./fields_eink.png) |

Up to three data fields are shown below the date separator. Each field displays a label and a value on the same line.

| Field       | Label | Value                                                                     |
| ----------- | ----- | ------------------------------------------------------------------------- |
| None        | —     | —                                                                         |
| Batt V      | Batt  | Battery voltage (e.g. `3.92V`)                                            |
| Batt %      | Batt  | Battery percentage using LiPo curve anchored at the low-battery threshold |
| Temperature | Temp  | °C from onboard sensor                                                    |
| Humidity    | Hum   | % from onboard sensor                                                     |
| Pressure    | Pres  | hPa from onboard sensor                                                   |
| GPS         | GPS   | `lat lon` decimal degrees, or `no fix`                                    |
| Altitude    | Alt   | metres from onboard sensor (GPS or barometric)                            |
| Luminosity  | Lux   | lux from onboard sensor                                                   |
| CO₂         | CO2   | ppm from onboard sensor                                                   |
| Contacts    | Nodes | Total contacts in the mesh                                                |
| Messages    | Msgs  | Total unread message count                                                |

Sensor fields show `--` when the sensor is not connected or has no data.

---

### Configuring fields

|           OLED            |           E-Ink           |
| :-----------------------: | :-----------------------: |
| ![](./config_oled.png) | ![](./config_eink.png) |

<!-- screenshot pending: Dashboard Config — three field slots cycled with LEFT/RIGHT -->

**Hold Enter** (or press the **Context menu** key) on the Clock page to open the Dashboard Config screen, where each of the three field slots can be cycled with **LEFT/RIGHT**.

---

### Clock tools — Alarm, Timer, Stopwatch

**Press Enter** (short press) on the Clock page to open **Clock Tools**, a small menu with three time utilities. **Cancel** backs out one level (tool → menu → home). The same menu also has an entry under **Tools › System**, so it's reachable without the Clock page.

#### Alarm

A wake alarm with an optional repeat. Rows: **Hour**, **Minute**, **Repeat** and **Armed**. **Enter** on Hour or Minute opens the digit editor (LEFT/RIGHT moves between the tens/units, UP/DOWN changes the digit); **Enter** on Repeat cycles **OFF → Daily → Weekdays → Weekends → OFF**; **Enter** on Armed toggles ON/OFF. The configured time is shown next to the **Alarm** menu row when armed, and the setting persists across reboots.

While armed, a bell icon shows in the Clock page's top-left corner and in the status bar on other home pages (hidden on the Clock page itself, hence its own indicator). Icon-only — the exact time is on the **Alarm** row in Clock Tools.

The alarm fires at an absolute instant, so it survives clock re-syncs (mesh packets, companion app, GPS and the CLI can all jump the device clock). A jump past the alarm time still fires it, just late. With **Repeat** OFF (default) it disarms after firing once; with a pattern set, it re-arms for the next matching day.

The alarm only fires while the device is **awake** (it keeps running with the display off or locked). It cannot wake the device from a full **Shutdown** (the CPU and RAM are powered down), and needs a valid time source — it stays pending until the clock is synced.

#### Timer (countdown)

A large **HH:MM:SS** readout with one digit underlined. **LEFT/RIGHT** moves the cursor one digit at a time, **Up/Down** changes the digit under it (minute/second tens cap at 5, hours at 23), and **Enter** starts the countdown. While running it shows **H:MM:SS** — **Enter** stops it, **Cancel** returns to the menu and leaves it counting. When it reaches zero the device rings, even if you have navigated to another screen.

#### Stopwatch

**Enter** starts/stops; **Up/Down** resets when stopped; **Cancel** returns to the menu and leaves it running.

#### Ringing

When the alarm or timer fires the device plays a melody (overriding mute) and shows an alert. **Any key** silences it; otherwise it stops on its own after a minute.

> **E-ink note:** the live timer/stopwatch readouts would thrash a slow e-paper panel if redrawn every second, so on e-ink they refresh only coarsely (and immediately on any key press). The underlying timing is exact regardless, and the countdown's buzzer always fires on time.
