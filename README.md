# Wio Tracker L1 — Extended Companion Radio Firmware

This branch extends the official MeshCore companion radio firmware for the **Seeed Wio Tracker L1**.

Join the discussion on offical MeshCore discord: https://discord.gg/sdhYArU2jr

## New Features

### Messages Screen

View and send messages using the on-screen keyboard or predefined quick replies. The keyboard supports placeholders that insert live sensor data — `{time}` and `{loc}` are always available; additional placeholders (`{temp}`, `{hum}`, `{pres}`, `{batt}`, `{alt}`, `{lux}`, `{co2}`) appear automatically for sensors that are connected and returning data.

Press Enter on a message to open it in fullscreen. Navigate between messages with left (newer) and right (older).

Hold Enter on a message or channel to open a context menu: change per-channel notification settings (mute, follow global, or force-on) and per-channel melody override (follow global, Melody 1, or Melody 2), or mark messages as read.

### Settings Screen

All settings are saved to flash and restored on next boot.

- **Display**
  - Brightness
  - Auto-off timeout
  - Battery display mode (icon, %, V)
  - Clock seconds (show/hide — hiding reduces display refresh from 1 s to 60 s)
- **Sound**
  - Buzzer: On / Off / **Auto** — Auto mode silences the device while connected via Bluetooth, and re-enables sound when the connection drops
  - Volume (1–5; preview tone plays on each change)
  - DM Melody — notification sound for incoming private messages: built-in, Melody 1, or Melody 2
  - Channel Melody — notification sound for incoming channel messages: built-in, Melody 1, or Melody 2
- **Home Pages** — toggle visibility of individual home screen pages
- **Radio**
  - TX power
- **System**
  - Timezone (UTC offset in hours)
  - Low battery shutdown threshold
- **GPS**
  - Position broadcast interval
- **Contacts**
  - Show all DMs or favourites only
  - Show all room servers or favourites only
- **Messages**
  - Edit up to 10 quick reply templates (Q1–Q10)

### Clock Screen

A dedicated clock page on the home screen shows the current time and date, synchronized from GPS or via Bluetooth. Timezone offset is applied from Settings.

Up to three configurable data fields are displayed below the clock. Available fields: battery voltage, temperature, humidity, pressure, GPS coordinates, altitude, luminosity, CO₂, contact count, and total unread message count.

### Nearby Nodes

Browse nodes that have recently advertised on the mesh. Filter by category (Favourites, All, Companion, Repeater, Room, Sensor). Select a node to see its coordinates, distance, bearing with cardinal direction, type, and last-heard time.

Use **Send my advert** (hold Enter → context menu) to broadcast your position and prompt nearby nodes to respond with theirs.

### Tools Screen

#### Auto-Advert

Periodically broadcasts a 0-hop advert with your GPS position so nearby nodes can track your location automatically. Configurable interval: off / 1 min / 2 min / 5 min / 10 min / 30 min / 1 h.

A blinking **A** indicator appears in the status bar while Auto-Advert is active.

#### Ringtone Editor

A step sequencer for composing custom notification melodies stored on the device. Two independent melody slots are available — **Melody 1** and **Melody 2** — switchable from within the editor. Each melody supports up to 32 notes with adjustable pitch (C–B + pause), octave (4–7), duration (whole / half / quarter / eighth), and BPM (60 / 90 / 120 / 150 / 180). Playback preview is available directly from the editor.

Melodies can be assigned as notification sounds per message type (DM / channel) in Settings, and individually overridden per channel or per contact from the message screen context menu.

#### Auto-Reply Bot

Automatically replies to incoming messages that contain a configured trigger word (case-insensitive).

- **DM mode** — when enabled, the bot listens to all incoming private messages and replies with the DM reply text.
- **Channel mode** — optionally, select a channel for the bot to monitor. When a trigger is matched, it replies with a separate channel reply text.
- Both modes can be active simultaneously and share the same trigger word but use independent reply texts.
- Replies support placeholders (`{time}`, `{loc}`).
- A 10-second cooldown prevents repeated replies in quick succession.

---

Feel free to explore, share feedback and feature requests!

## Development

This fork tracks the upstream [MeshCore](https://github.com/ripplebiz/MeshCore) repository. To prevent upstream changes from overwriting this README during merges, `README.md` is protected via `.gitattributes`. After cloning, run once:

```sh
git config merge.ours.driver true
```
