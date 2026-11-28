# Host-side tools

Helper scripts that run on your computer, not on the device.

| Tool | Purpose |
| ---- | ------- |
| `screenshot.py` | capture the device display over USB serial and save it as a PNG |
| `trail_export.py` | capture a GPX track from the device over USB serial |
| `console.py` | the device's console (get / set settings, the radio, the name) over USB serial |
| `route.py` | put a planned GPX route on the device to follow; list and delete routes |
| `config.py` | the whole configuration to a JSON backup and back; the melodies (RTTTL); the order of Home's pages; waypoints from a GPX |
| `hwtest.py` | tests on a device plugged in: builds and flashes a firmware, then drives it over USB and checks what it does |
| `companion.py` | the companion-protocol framing `console.py`, `route.py` and `config.py` share |
| `gpx-downloader/index.html` | the same GPX capture in a browser, via Web Serial |
| `bdf2mono.py` | convert a fixed-cell BDF font to a MonoFont header |

Python dependencies are managed with [uv](https://docs.astral.sh/uv/) — run the
scripts with `uv run tools/<script>.py` from the repository root.

> [!TIP]
> The [configurator on the Solo site](https://solo.marekzegarek.com/configure.html) does all of
> this (settings, Home pages, quick messages, melodies, routes, screenshots, GPX export, the
> console, a backup) with nothing to install (Chrome or Edge), and is the easiest option for
> most people. The scripts here are the offline equivalents: everything the site does stays
> possible without it, and the backup files are the same.

> [!IMPORTANT]
> Solo builds serve Bluetooth and USB at the same time, so the companion app can
> stay connected over Bluetooth. If the app is connected over USB, disconnect
> it: two programs on one serial port disrupt each other's frames. (On the
> stock companion firmware, USB serial is suspended while Bluetooth is
> connected.)

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
uv run tools/config.py import backup.json         # radio, settings, Home, quick messages, melodies, waypoints
uv run tools/config.py import backup.json --only settings,melodies --dry-run
uv run tools/config.py import backup.json --identity   # also the name, Bluetooth PIN, time zone
uv run tools/config.py melody 1                   # melody 1 as RTTTL
uv run tools/config.py melody 2 "Tune:d=8,o=5,b=120:c,e,g,c6"
uv run tools/config.py melody 2 clear             # empty it
uv run tools/config.py pages                      # Home's pages in order
uv run tools/config.py pages Clock Messages Status Tools   # that order, the rest hidden
uv run tools/config.py waypoints                  # the device's waypoints
uv run tools/config.py waypoints walk.gpx         # add a GPX's waypoints (not those already there)
uv run tools/config.py waypoints --delete 3
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
uv run tools/route.py list                                  # numbered from 1
uv run tools/route.py delete 2                              # route 2 of the list
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

## Tests on a device

One command for an iteration on the firmware: build, flash, test.

```sh
uv run tools/hwtest.py --flash WioTrackerL1_companion_solo_dual   # build a test firmware, flash it, test
uv run tools/hwtest.py                                            # test what's on the device
uv run tools/hwtest.py --only settings,route-walk                 # some of them (--list names and explains them)
uv run tools/hwtest.py --reboot                                   # also what has to survive a restart
uv run tools/hwtest.py --all                                      # every device plugged in, one after another
uv run tools/hwtest.py --pair                                     # two devices: also messages over the air
uv run tools/hwtest.py --repeat 5                                 # a soak: five rounds, the heap compared
uv run tools/hwtest.py --flash <env> --release                    # the plain firmware back, without test hooks
```

Each test puts back what it changed: a setting flipped is set back, a waypoint
or route it added is deleted, a trail it recorded cleared (it won't start on a
device that holds a trail). Screenshots, a log of each walk and `report.json`
land in `.pio/hwtest/<board>-<time>/`; the exit status is non-zero when a test
failed, so it can stand in a script.

| Test | Checks |
| --- | --- |
| `identity`, `unknown-command` | `ver`, `board`, the reply to a command it doesn't know, the `xx\|` prefix |
| `batch` | a `set` inside `batch on` / `batch off` takes milliseconds, the save once at the end |
| `radio` | `get radio`, `set radio` and `set tx` to what they are |
| `clock` | the clock set from the CLI (seconds, local time, back too) and as the app does (an hour back at most); the computer's time put back after |
| `settings` | every setting set to another of its choices, read back, set back (not the ones that delete messages or contacts, can switch the device off, or send an advert) |
| `settings-persist` | `--reboot`: a setting changed in a batch the page never closed is there after a restart |
| `pages`, `quick-messages`, `melodies`, `waypoints`, `routes` | written, read back (a melody through RTTTL too, a waypoint twice is "already there"), put back |
| `screenshot` | the screen comes over as a PNG, not all black |
| `keys` | keys move the UI, Home's context menu opens and closes |
| `screens` | each of Home's pages, Tools / Settings / Map opened from it, a screenshot of each |
| `gps-fake` | a fake fix taken, bad ones refused |
| `route-walk` | 800 m walked by fake fixes: on it, 100 m off, back, to its end, with "Off route", "Back on route", "Route finished" |
| `route-reversed` | the same followed reversed, from its far end |
| `route-loop` | a loop (start = end) doesn't finish at its start, does once walked round |
| `route-far-start` | started 300 m away: off it with no "Off route"; "On route" on reaching it, not "Back on route" |
| `route-shortcut` | across a U-bend the guide takes up the far side instead of calling you off |
| `route-resume` | `--reboot`: a restart on the way back of an out-and-back route; picked up on the way back, not the way out |
| `route-resume-away` | `--reboot`: a restart halfway round a loop with the first fix 1.3 km away: off the route with the progress kept; back on it with the same distance left, not at the loop's start |
| `trackback` | a trail recorded on an L, Track back: "to start", walked back to "Back at the start" |
| `guide-switch` | Track back started during a route takes its place, and the route followed again takes Track back's |
| `coverage` | MatterMap recording (L2, an SD card): turned on with a fake fix, a row written; turned off, the file finished; deleted after, nothing uploaded |
| `coverage-home` | the home zone set after recording: a finished file keeps only its rows outside it, the file being written loses its rows inside it and is finished |
| `coverage-crash` | `--reboot`, L2: rows recorded, the loop hung, the watchdog's restart: the rows are still in the file |
| `loop-watchdog` | `--reboot`, L2: the loop hung; within 30 s the watchdog restarts the device, with the reason and a core dump after |
| `crash-report` | `--reboot`, L2: a hang after a few screens; the report on the card (`meshcore/crash/`) has the backtrace and the screen opened last; deleted after |
| `loops` | how often `loop()` runs and how busy it is, screen on and off |
| `one-button` | a one-button board: click, double click and hold through the firmware's own gesture timing |
| `memory` | free heap at the end against the start; with `--repeat`, the first round's end against the last's (more than 4 KB less fails) |
| `pair` | `--pair`: a direct message each way between the two devices, received (in the device's history) and ACKed |

Tests from `keys` on need the test build; `keys`, `screens` and `one-button`
the joystick / one-button UI (not the L2's touch screen); the route tests a
board with routes. Each is skipped, with the reason, where it can't run.

After every test the device's time since boot is read: had it gone back, the
device restarted (a crash, a watchdog) during that test, which then fails.

**Two devices.** `--pair` exchanges the two as contacts (as a QR code would,
removed again afterwards if they weren't contacts before) and sends a direct
message each way over the radio; both need a test build and the same radio
settings. A message with no known path goes out as a flood, so the repeaters
around carry it once; the messages stay in both devices' histories.

**Flashing.** Before anything the device has to answer as a Solo companion
(an observer or a repeater on that port does not), and with more than one
plugged in `--port` says which (flash each, then `--all` / `--pair`). An
nRF52 board goes into its bootloader by the 1200-baud touch and gets
`firmware.uf2` copied to the drive that appears (should none appear, a double
press of reset brings it up); an ESP32 board is flashed with `pio run -t
upload`. Afterwards the same board is waited for, whichever port it comes
back on.

**The test build** (`-D SOLO_TEST_HOOKS`, in `.pio/build-test`; `ver` says
`+test`) adds console commands to drive the device the way its user would,
from [`ui-core/TestHooks.h`](../examples/companion_radio/ui-core/TestHooks.h):

| Command | Does |
| --- | --- |
| `key <up\|down\|left\|right\|enter\|cancel\|next\|prev\|select\|home\|menu> [long]` | A key press (held, with `long`), waking the screen first as a real one does. Not on the L2 |
| `btn down`, `btn up` | The one button of a one-button board pressed, let go: gestures timed as for the real one |
| `ui` | `> <screen>\|<banner or ->\|<locked 0/1>\|<display on/off>\|<banners so far>`; Home with its page, `home/tools`; the count tells a new banner from one still up with the same text |
| `gps <lat>,<lon>[,<alt m>]`, `gps off` | A GPS fix there for every screen, the trail and the route guide; the real receiver again |
| `follow <slot> [reversed]`, `follow off` | Follow a planned route (as Routes › Follow), stop |
| `guide` | `> <state>,<slot>,<m left>,<m off>,<header>`: state 0 off, 1 on the route, 2 off it, 3 finished |
| `trail on\|off\|clear`, `trail` | Record the trail, stop, empty it; `> <recording 0/1>,<points>` |
| `trackback` | Track back, as Trail › Track back |
| `mem` | `> <free heap>,<total>,<stack free>,<seconds since boot>` |
| `lastdm`, `lastch` | The newest direct message (`> in\|out,<prefix>,<ack>,<text>`), channel post (`> <channel>,<text>`) |
| `loops` | `> <loop passes a second>,<busy %>,<seconds>` since the last ask |
| `cov ...` | MatterMap recording (L2): `cov` its state; `cov on\|off`, `cov auto on\|off`, `cov home <lat>,<lon> <r>`, `cov count <name>`, `cov rm <name>`, `cov clear` (in RAM, a test build's `coverage-test/` files) |
| `screen on\|off`, `idle <screen>`, `idle` | L2: the screen woken, put off; a screen opened; `> <areas>,<pixels>` sent to the panel since the last ask (what a screen left alone redraws) |
| `hang`, `boot` | L2: the loop stuck from its next pass (for the watchdog); `> <why it last started>,<the last crash>` |
| `crash`, `crash rm` | L2: the newest crash report, `> <file>,<things noted>,<backtrace 0/1>,<the last>`; deleted |

They are as reachable over Bluetooth as the rest of the console, which is why
only a test build has them: never give one to a device people carry
(`hwtest.py --flash <env> --release` puts the plain firmware back).

---

## Font conversion

```sh
uv run tools/bdf2mono.py <font.bdf> <first_hex> <last_hex> <VarPrefix> > out.h
```

Writes the font as [`MonoFont`](../src/helpers/ui/MonoFont.h): each glyph a
whole cell, one bit per codepoint in the range for whether the font draws it
(the rest are blank), and a rank table that finds a glyph's cell from that
bit -- about 230 bytes for U+0020-04FF where a GFX glyph table took ~10 KB.
The misc-fixed fonts in `src/helpers/ui/` -- [`MiscFixedFont.h`](../src/helpers/ui/MiscFixedFont.h)
(the unified 6×9 Latin/Greek/Cyrillic display font), `MiscFixed8x13Font.h`
and `MiscFixed9x15Font.h` -- come from `6x9.bdf`, `8x13.bdf` and `9x15.bdf`
over U+0020-04FF.
