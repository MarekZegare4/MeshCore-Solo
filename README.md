# Wio Tracker L1 — Extended Companion Radio Firmware

This branch extends the official MeshCore companion radio firmware for the **Seeed Wio Tracker L1**.

## New Features

### Messages Screen

View and send messages using the on-screen keyboard or predefined quick replies. The keyboard supports placeholders that insert live sensor data — `{time}` and `{loc}` are available by default.

Hold Enter on a message or channel to open a context menu: change per-channel notification settings (overrides the global sound setting) or mark messages as read.

### Settings Screen

All settings are saved to flash and restored on next boot.

- **Display**
  - Brightness
  - Auto-off timeout
  - Battery display mode (icon, %, V)
  - Clock seconds (show/hide — hiding reduces display refresh from 1 s to 60 s)
- **Sound**
  - Buzzer: On / Off / **Auto** — Auto mode silences the device while connected via Bluetooth, and re-enables sound when the connection drops
  - Volume
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
  - Edit up to 10 quick reply templates

### Clock Screen

A dedicated clock page on the home screen shows the current time and date, synchronized from GPS or via Bluetooth. Timezone offset is applied from Settings.

### Tools Screen

#### Ringtone Editor

A step sequencer for composing custom ringtones stored on the device. Supports up to 32 notes with adjustable pitch, octave, duration, and BPM. Playback preview is available directly from the editor menu.

> Custom ringtones as per-channel or per-contact notification sounds are planned for a future update.

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
