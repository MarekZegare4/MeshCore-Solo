# Contributing to MeshCore Solo

Thanks for helping with Solo! Solo is a fork of
[MeshCore](https://github.com/meshcore-dev/MeshCore): its companion firmware
with an interface on the device itself. Reports, ideas and code are all
welcome.

## Where to ask

- **Questions and quick feedback:** the [Solo firmware thread](https://discord.com/channels/1495203904898728149/1505294337884553447)
  on the [MeshCore Discord](https://discord.gg/sdhYArU2jr).
- **Bugs and feature requests:** an [issue](https://github.com/MarekZegare4/MeshCore-Solo/issues) here.
- **The mesh itself** (routing, repeaters, room servers, the phone app):
  those come from upstream MeshCore. If it happens on stock MeshCore too,
  report it [there](https://github.com/meshcore-dev/MeshCore/issues).

## Reporting a bug

Please include:

- **The device** (board and screen, e.g. Wio Tracker L1 e-ink 2.13″, Heltec V4
  one-button) and **the firmware version** (Settings › About, or `ver` in
  the [console](./docs/solo/configurator.md)).
- **What you did, what you expected, what happened.** Steps that bring it
  back are the most useful thing you can send.
- **A screenshot** if it's about the screen: the
  [configurator](https://solo.marekzegarek.com/configure.html) takes one
  over USB (Screenshots).
- **Wio Tracker L2:** after a hang or a crash the device writes a report to
  `meshcore/crash/` on the SD card. Please attach it.

## Suggesting a feature

Open an issue that says what you want to do and on which device; describe
the problem, not only the solution. Screens are small and every row costs
something on the OLED and e-ink devices, so the use case decides where a
feature goes.

## Code

### Branches

- Work from **`dev`** and open pull requests against **`dev`**.
- **`main`** is what was released: it only moves when a version is tagged.
- One fix or feature per pull request. For anything big, open an issue
  first, so we can agree on the approach before you spend the time.

### Where things are

| Path | What |
| --- | --- |
| `examples/companion_radio/ui-core/` | Logic shared by every interface: routes, trail, settings schema, sound, battery… |
| `examples/companion_radio/ui-new/` | The interface for OLED, e-ink, keyboard and one-button devices |
| `examples/companion_radio/ui-lvgl/` | The Wio Tracker L2's touch interface (LVGL) |
| `solo/<board>/platformio.ini` | The release builds: `*_solo_dual` and `*_solo_lvgl` |
| `variants/sim/` | The same firmware in a browser (WebAssembly), for trying the UI without hardware |
| `tools/` | Host tools: console, configuration backup, routes, device tests |
| `docs/solo/` | User docs; `docs/solo/developer/` for developers |

Start with [UI Core](./docs/solo/developer/ui-core.md) and the
[UI framework](./docs/solo/developer/ui-framework.md).

### Building and testing

- Build: see [Building](./README.md#building) in the README. Please build
  at least the boards your change touches; the shared code builds for
  nRF52 and ESP32 alike.
- UI changes: check them in the [simulator](./variants/sim/) (OLED, e-ink
  and the L2), and on hardware if you have it.
- With a device plugged in, [`tools/hwtest.py`](./tools/README.md#tests-on-a-device)
  builds a test firmware, flashes it and runs the device tests. Flash the
  plain firmware back afterwards (`--release`).

### Please keep in mind

- **Upstream merges.** Solo takes in new MeshCore releases. Keep changes to
  the mesh core (`src/`) small and clearly marked, so the next merge stays
  easy.
- **The phone app.** New companion frame codes or changed replies affect
  the apps; open an issue first.
- **Settings** go through the settings schema (`ui-core/SettingsSchema.h`)
  so they show on every device, in the console and in the configurator.
  New ones start at a safe default; we don't reset users' settings in an
  update.
- **Memory.** The nRF52 boards have little RAM to spare (the Wio Tracker L1
  about 20 KB free). Avoid large static buffers in shared code.
- **Docs.** If you change what the user sees, update the page in
  `docs/solo/` too, in plain English.
- **Style.** Follow the code around you: 2 spaces, `camelCase` functions and
  variables, `UpperCamelCase` types, `ALL_CAPS` macros (see `.clang-format`).
  Comments say why, not what.
- **Commit messages:** what changed and why, e.g.
  `fix(l2): Settings > Clock on wheels -- the typed field took no digit`.
