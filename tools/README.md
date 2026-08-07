# Host-side tools

Helper scripts that run on your computer, not on the device.

| Tool | Purpose |
| ---- | ------- |
| `screenshot.py` | capture the device display over USB serial and save it as a PNG |
| `trail_export.py` | capture a GPX track from the device over USB serial |
| `gpx-downloader/index.html` | the same GPX capture in a browser, via Web Serial |
| `bdf2gfx.py` | convert a BDF bitmap font to an Adafruit-GFX header |

Python dependencies are managed with [uv](https://docs.astral.sh/uv/) — run the
scripts with `uv run tools/<script>.py` from the repository root.

> [!TIP]
> The hosted [Solo Tools Web App](https://marekzegare4.github.io/Solo-tools/)
> does both screenshots and GPX export with nothing to install, and is the
> easiest option for most people. The scripts here are the offline equivalents.

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

## Font conversion

```sh
uv run tools/bdf2gfx.py <font.bdf> <first_hex> <last_hex> <VarPrefix> > out.h
```

Emits a contiguous glyph table over the given codepoint range, with empty
placeholders for codepoints the BDF does not define, so the renderer can index
by `cp - first`. This is how [`src/helpers/ui/MiscFixedFont.h`](../src/helpers/ui/MiscFixedFont.h)
— the unified 6×9 Latin/Greek/Cyrillic display font — was generated from
`6x9.bdf`.
