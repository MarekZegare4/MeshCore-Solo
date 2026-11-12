# Host-side tools

Helper scripts that run on your computer, not on the device.

| Tool | Purpose |
| ---- | ------- |
| `screenshot.py` | capture the device display over USB serial and save it as a PNG |
| `trail_export.py` | capture a GPX track from the device over USB serial |
| `console.py` | the device's console (get / set settings, the radio, the name) over USB serial |
| `route.py` | put a planned GPX route on the device to follow; list and delete routes |
| `companion.py` | the companion-protocol framing `console.py` and `route.py` share |
| `gpx-downloader/index.html` | the same GPX capture in a browser, via Web Serial |
| `bdf2gfx.py` | convert a BDF bitmap font to an Adafruit-GFX header |

Python dependencies are managed with [uv](https://docs.astral.sh/uv/) — run the
scripts with `uv run tools/<script>.py` from the repository root.

> [!TIP]
> The [USB tools on the Solo site](https://solo.marekzegarek.com/#pc-tools) do all of this
> (screenshots, GPX export, the console, routes) with nothing to install (Chrome
> or Edge), and are the easiest option for most people. The scripts here are the
> offline equivalents: everything the site does stays possible without it.

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

---

## Planned routes

A route planned beforehand (a GPX from any planner) to follow on the device:
**Tools › Trail › Hold Enter › Routes**. The device points the way to the next
bend, shows how far is left, and beeps when you leave the route and when you
reach its end. Needs Solo 2.3 or newer on a board that keeps messages on flash
(Wio Tracker L1, Watch13, Heltec V3/V4, Cardputer): 8 routes, up to 4000 points
each.

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
