# Configurator

**[solo.marekzegarek.com/configure.html](https://solo.marekzegarek.com/configure.html)**
sets up a Solo device from a computer, over a USB cable: the settings, Home's
pages, quick messages, the melodies, routes to follow, waypoints, and a backup
of all of it to a file.

It needs Chrome or Edge on a computer (the browser talks to the device over
Web Serial) and Solo 2.3 or newer. Older firmware gets the tabs it can
answer: the device's name and radio, screenshots, the trail and the console.

1. Plug the device in with a USB cable.
2. Disconnect the phone app if it is connected over USB.
3. Press **Connect device** and pick the device's port.

Nothing is changed until you save it, except on the **Settings** tab, where
a change goes to the device straight away.

## Tabs

| Tab | Does |
| --- | ---- |
| **Device** | The board, firmware and battery; the name, Bluetooth PIN, radio (frequency, bandwidth, spreading factor, coding rate), TX power and time zone |
| **Settings** | Every setting of the device's Settings, grouped the same way, with a search |
| **Home screen** | The pages Home turns through: drag them into order, untick the ones to hide. Not on the Wio Tracker L2, which arranges its home screen itself |
| **Quick messages** | The ten messages a conversation offers to send with one press; `{loc}`, `{time}` and `{batt}` fill in when sent |
| **Melodies** | The two melodies of the Ringtone editor, on a piano roll; play them, or start from a ringtone in RTTTL |
| **Routes & trail** | A GPX to follow on the device, its waypoints, and the trail exported from it ([Navigation](./navigation.md#planned-routes)) |
| **Screenshots** | What's on the screen, as a PNG |
| **Console** | The same text commands as the phone app's console ([Settings](./settings.md)) |
| **Backup** | Everything above to a `.json` file, and back onto this device or another one |

### Melodies

Across is the order of the notes, up is the pitch: C4 to B6, a rest row under
them, and a row with each note's length (1/4 to 1/32) at the bottom. Click a
cell to put a note there; click the length row to make that note shorter.
With the roll selected, the keys **A**–**G** type a note, **P** a rest,
**1**–**4** the length, the arrows move and change the pitch, **Space**
plays.

The device has no sharps: an RTTTL ringtone's sharps go to the note below,
and its octaves to 4–6.

### Routes & trail

Open a `.gpx` (or drop it on the map); a trail exported on the device opens
here by itself. Then:

- **Click** a point to pick it, **drag** it to move it, **double-click** the
  line to add one. **Start here** and **End here** cut the route at the
  picked point; **Reverse** turns it round. **Undo** takes back the last
  change. The wheel zooms, dragging the map pans.
- **Points to send** sets how many points go to the device: the straight
  stretches go first and the bends stay; the ± figure is how far the
  simplified line strays from the original at most. Points you moved or
  added always go.
- **Send to device** puts it on the device as a route; **Save GPX** saves the
  whole line, with its times and waypoints.

A trail sent back as a route (**Reverse** first) leads you home the way you
came.

Under the routes on the device are its **waypoints**, each with a button to
delete it. When the open GPX has waypoints, **Add them to the device** puts
them after the device's own, leaving out any already there and any past what
the device holds (16, the Wio Tracker L2 64). A name is cut to 11 bytes.

### Backup

A backup holds the radio, the settings, Home's pages, the quick messages, the
melodies and the waypoints, and the name, Bluetooth PIN and time zone.
Waypoints from a backup are added to those on the device, not put in their
place. Loading one, you
pick which parts go back: the name, PIN and time zone are left out unless
you tick them, so a backup sets up a second device without making it a copy
of the first. Contacts, channels and keys are not in it.

## Without a browser

The same from a terminal, with [uv](https://docs.astral.sh/uv/), in the
firmware's repository:

| Script | Does |
| ------ | ---- |
| `tools/config.py` | Backup to the same `.json` and back, a melody from RTTTL, Home's pages, waypoints from a GPX |
| `tools/route.py` | Send, list and delete routes |
| `tools/trail_export.py` | Receive the trail as GPX |
| `tools/screenshot.py` | Screenshots |
| `tools/console.py` | The console |

[`tools/README.md`](../../tools/README.md) has their options.
