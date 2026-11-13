# Host-side tools

Helper scripts that run on your computer, not on the device.

| Tool | Purpose |
| ---- | ------- |
| `screenshot.py` | capture the device display over USB serial and save it as a PNG |
| `trail_export.py` | capture a GPX track from the device over USB serial |
| `console.py` | the device's console (get / set settings, the radio, the name) over USB serial |
| `route.py` | put a planned GPX route on the device to follow; list and delete routes |
| `config.py` | the whole configuration to a JSON backup and back; the melodies (RTTTL); the order of Home's pages |
| `companion.py` | the companion-protocol framing `console.py`, `route.py` and `config.py` share |
| `gpx-downloader/index.html` | the same GPX capture in a browser, via Web Serial |
| `bdf2gfx.py` | convert a BDF bitmap font to an Adafruit-GFX header |

Python dependencies are managed with [uv](https://docs.astral.sh/uv/) — run the
scripts with `uv run tools/<script>.py` from the repository root.

> [!TIP]
> The [configurator on the Solo site](https://solo.marekzegarek.com/configure.html) does all of
> this (settings, Home pages, quick messages, melodies, routes, screenshots, GPX export, the
> console, a backup) with nothing to install (Chrome or Edge), and is the easiest option for
> most people. The scripts here are the offline equivalents: everything the site does stays
> possible without it, and the backup files are the same.

> [!IMPORTANT]
> USB serial is suspended while a BLE connection is active, so disconnect the
> companion app from BLE before using any of these. If the app is connected over
> USB, disconnect it too — the raw stream would otherwise disrupt its frame
> protocol.

---

## Display screenshot

All solo firmware environments are built with `-D ENABLE_SCREENSHOT`, so no
special build is needed. On other environments (repeater, room server, the
non-solo companion builds) add the flag yourself:

```sh
PLATFORMIO_BUILD_FLAGS="-D ENABLE_SCREENSHOT" pio run -e <env> -t upload
```

**Usage:**

1. Connect the device over USB, with no companion app attached
2. Run the tool:

   ```sh
   uv run tools/screenshot.py
   ```

   Options:
   - `--port PORT` — serial port to use (default: auto-detect)
   - `--scale SCALE` — upscale factor for the output image (1 = none; default: 1)

3. Press **S** in the tool's interactive menu to capture
4. The PNG lands in `tools/pngs/` with a timestamped filename

**How it works:**

- The tool sends `CMD_GET_SCREENSHOT` (66) to the device
- The device replies with `RESP_CODE_SCREENSHOT` (29) carrying the framebuffer
- The framebuffer arrives in chunks (a 128 × 64 display is 1024 bytes, split
  across several frames), which the tool reassembles into a PNG
- A colour screen (Wio Tracker L2) sends a freshly rendered frame as RGB565
  (display type 2): 320 × 240 is 150 KB, about a second over USB

---

## GPX trail export

1. Connect the device over USB, with no companion app attached
2. Start the listener:

   ```sh
   uv run tools/trail_export.py
   ```

   Options:
   - `--port PORT` — serial port to use (default: auto-detect)
   - `--out OUT` — output file (default: a timestamped file in `tools/gpx/`)

3. On the device: **Tools › Trail › Hold Enter › Export (live)** or
   **Export (saved)**
4. The script captures the GPX 1.1 XML stream and writes it to disk

---

## Console

```sh
uv run tools/console.py                 # interactive; exit or Ctrl-D to leave
uv run tools/console.py get radio       # one command
```

The same commands as the phone app's console: `get radio`, `set tx 14`,
`get name`, and every setting by its key (`keys` lists them). Needs Solo 2.0 or
newer. Options: `--port PORT`.

The ten quick messages a conversation offers (one press to send; on a
one-button board the way to write a message) are set the same way, numbered
from 1; `{loc}`, `{time}`, `{batt}` fill in when sent. Needs Solo 2.3 or newer.

```bash
uv run tools/console.py msgs                          # the slots in use
uv run tools/console.py set msg.2 "On my way, there by {time}"
uv run tools/console.py set msg.2                     # empties it
```

---

## Configuration: backup, melodies, Home pages

```sh
uv run tools/config.py export                     # solo-config_<name>_<date>.json
uv run tools/config.py import backup.json         # radio, settings, Home, quick messages, melodies
uv run tools/config.py import backup.json --only settings,melodies --dry-run
uv run tools/config.py import backup.json --identity   # also the name, Bluetooth PIN, time zone
uv run tools/config.py melody 1                   # melody 1 as RTTTL
uv run tools/config.py melody 2 "Tune:d=8,o=5,b=120:c,e,g,c6"
uv run tools/config.py pages                      # Home's pages in order
uv run tools/config.py pages Clock Messages Status Tools   # that order, the rest hidden
```

A backup is everything the configurator shows: the settings (by their keys,
as `keys` lists them), Home's pages, the quick messages, both melodies, the
radio and, with `--identity`, the name, Bluetooth PIN and time zone. Contacts,
channels and keys are not in it. Load it back on the same device, or on another
one to set it up the same way; settings a board doesn't have are skipped.

The melodies are the Ringtone editor's two (Settings › Sound plays them for
messages and adverts): up to 32 notes, C4 to B6, 1/4 to 1/32, at 60-180 BPM.
RTTTL in is folded to fit: sharps drop to the note below, octaves to 4-6.
Home's page order matters most on a one-button board, where it can't be
changed by hand; the color screen (L2) sets its Home on the device. Needs Solo
2.3 or newer.

**How it works:** CLI commands (`CMD_RUN_CLI_COMMAND`, as the console):
`opts <key>` gives a setting's label, value and choices, `get/set melody.N
<bpm> <hex notes>` a melody (a byte a note, see
[`SoundControl.h`](../examples/companion_radio/ui-core/SoundControl.h)),
`pages` / `set pages <names>` Home's order.

---

## Planned routes

A route planned beforehand (a GPX from any planner) to follow on the device:
**Tools › Trail › Hold Enter › Routes** (on the Wio Tracker L2: **Map ›
tools › Route**). The device points the way to the next bend, shows how far is
left, and beeps when you leave the route and when you reach its end. Needs
Solo 2.3 or newer on a Wio Tracker L1, Watch13, Heltec V3/V4, Cardputer (8
routes, up to 4000 points each) or Wio Tracker L2 (up to 10 000 points; it
also imports a .gpx straight from its SD card, from `/routes` or the card's top
folder).

```sh
uv run tools/route.py upload walk.gpx                       # as many points as the device holds
uv run tools/route.py upload walk.gpx -n 300 --name "Hala"  # fewer points, another name
uv run tools/route.py upload walk.gpx --reverse --dry-run   # just the numbers, nothing sent
uv run tools/route.py list
uv run tools/route.py delete 2
```

The GPX's track segments (or its route points) are joined into one line and
simplified to the point count (Douglas-Peucker by rank): the bends stay, the
straight stretches go, and the tool prints how far the simplified line strays
from the original at most. Heights are kept when the GPX has them.

**How it works:** `CMD_ROUTE` (0x7D) frames, documented in
[`MyMeshRoutes.h`](../examples/companion_radio/MyMeshRoutes.h): `INFO` for
the limits, `BEGIN` / `DATA` × n / `END` to write one, `GET` and `DELETE`. The
device writes the route to a temporary file and only renames it into place
once it's whole, so a cut-short upload changes nothing.

---

## Font conversion

```sh
uv run tools/bdf2gfx.py <font.bdf> <first_hex> <last_hex> <VarPrefix> > out.h
```

Emits a contiguous glyph table over the given codepoint range, with empty
placeholders for codepoints the BDF does not define, so the renderer can index
by `cp - first`. This is how [`src/helpers/ui/MiscFixedFont.h`](../src/helpers/ui/MiscFixedFont.h)
— the unified 6×9 Latin/Greek/Cyrillic display font — was generated from
`6x9.bdf`.
