# UI Core + frontends — design

Status: written 2026-09-24 as the design; steps 0–3, 5 and 6 are done (marked ✅ below); step 4 in part (ui-new's Settings shows the schema's rows inside its own sections). Branch: `wio-tracker-l2`.

## Why

The Wio Tracker L2 (320×240 colour touch LCD, no joystick) gets a new LVGL 9
interface. Rather than a second, parallel copy of everything Solo does, the
application logic moves out of `ui-new` into a hardware-independent **UI Core**
that every frontend shares:

| Frontend | Devices | Toolkit | Input |
|---|---|---|---|
| `ui-lvgl` (rich) | Wio Tracker L2 first; later other ESP32-S3 colour boards | LVGL 9 | pointer (touch) + focus (buttons, CardKB) |
| `ui-new` (lite) | OLED / e-ink / nRF52 (Wio L1, T-Echo Lite, Heltec, …) | `DisplayDriver` | focus (joystick, buttons, CardKB) |

A feature written once in the Core (live share, locator, unread tracking, a new
setting) then shows up on both. `ui-new` is not rewritten: it is re-pointed at
the Core and stays the lite frontend.

Non-goals: changing the mesh protocol, `MyMesh`, persistence formats
(`NodePrefs` layout, `/scopes1`, trail files), or the companion app protocol.

## Current state

`ui-new` is 18.5k lines. `UITask` implements `AbstractUITask` (the interface
`MyMesh` calls into) *and* hosts the screens, so logic and drawing are
interleaved. Classified:

**Models — already UI-free, move as-is**
- `MessageHistory.h` — channel (48) / DM (32) rings, unread counters + overflow flags. ✅ moved to `ui-core/`.
- `Trail.h` / `TrailStore`, `ScopeList.h`. Waypoints ✅ `ui-core/WaypointModel.h` (store + `/waypoints` persistence + "delete clears the Locator target"; ui-new's `UITask::waypoints()` forwards to it).

**Engines — logic living inside `UITask.cpp` / `UITask.h`**
- Unread tracking — DM unread table ✅ (`ui-core/DmUnreadTable.h`), room unread (one counter in `UiCore`). Room logins ✅ `ui-core/RoomSessions.h`.
- Notifications ✅ split: the Core decides *what* happened (`MessageArrived` with kind / DM sender / channel slot, `AdvertHeard`); the frontend decides *how* to show it (`showAlert`, `SoundNotifier`, vibration, LED, wake-on-message) — those are platform services.
- Live share ✅ `ui-core/LiveShareEngine.h` — session timer, movement/heartbeat gate, send + scope guard, peers' `LiveTrackStore` + expiry.
- Locator ✅ `ui-core/LocatorEngine.h` — active target, geofence state machine, proximity beeper.
- Trail ✅ `ui-core/TrailEngine.h` — store, sampling, auto-pause, low-battery auto-save, recording actions (start/stop, reset, save/load `/trail`) used by both frontends. GPS averaging for marks ✅ `ui-core/GpsAverager.h`. Track back ✅ `ui-core/TrackBack.h`.
- Course over ground + current position ✅ `ui-core/CourseEngine.h`.
- Clock tools — alarm / countdown / ring ✅ `ui-core/ClockEngine.h`.
- Ping ✅ `ui-core/PingEngine.h` (timeout still decided by `NearbyScreen`).
- Device controls — GPS on/off ✅ (`UiCore::setGpsEnabled`, used by ui-new's toggle and ui-lvgl's Settings / map crosshair), GPIO (`setGpioMode`, bot GPIO), buzzer mode/volume, brightness, radio apply (`applyTxPower`, `applyApc`, `applyRadioParams`, …).
- Bot hooks — `botSetGPS`, `botBuzz`, `botSetGPIO`, …

**Screen-embedded logic (harder to extract)**
- `SettingsScreen.h` (1.3k lines) — ~40 items, each with inline get/format/step/apply code.
- `NearbyScreen.h` — contact discovery/sorting; `AdminScreen.h` — repeater admin session; `BotScreen.h`; `RepeaterScreen.h`; `MessagesScreen.h` (2.6k lines) — list ordering, favourites, context-menu actions mixed with rendering.

**Pure view / widgets — stay in `ui-new`**
- Rendering of every screen, `KeyboardWidget`, `PopupMenu`, `AccordionList`, `TabBar`, `icons.h`, `GfxUtils.h`, marquee, badges.

## Architecture

```
 MyMesh ──(AbstractUITask callbacks)──► UiCore ──events──► Frontend (ui-new | ui-lvgl)
   ▲                                      │  ▲                  │
   └──────────── mesh actions ────────────┘  └──── actions ─────┘
                                   engines, models, settings schema
 Platform services (per board): Sound, DisplayPower, Input sources, Led
```

### 1. `UiCore` owns the mesh-facing interface

`MyMesh` talks to the UI only through `MyMesh::Listener` (set with
`setListener()`), ported from upstream's "Abstract UI overhaul" (PR #3431 and
follow-ups `3caf033d`, `5c3d9281`, `30dd723c`, `b3b17025`, `64434c53`) with
identical names/signatures. A second block in the same interface holds this
fork's extensions (own-send mirroring, relay echoes, room login/admin replies,
`[LOC]` shares, contact/channel removal, bot device actions,
`requestShutdown`), each defaulting to a no-op.

For `ui-new`, `UiCore` is the Listener (`UITaskBase::meshListener()` hands it
to `MyMesh::setListener()`): it applies the display filter, labels room posts,
files history, keeps unread counters, runs the engines and emits events. The
frontend implements `UiCoreHost` (`ui-core/UiCoreHost.h`) for what the Core
still asks of it synchronously: "is this conversation on screen", "keep the
selection after an insert", and forwarding for not-yet-extracted parts (room
login / admin sessions, bot device actions, prefs cleanup on contact/channel
removal, shutdown). `ui-orig` / `ui-tiny` keep `AbstractUITask`, which is
`UITaskBase` + the Listener glue, unchanged. This is the single point that
makes both frontends receive identical behaviour.

### 2. Engines

One class per engine from the inventory above, each with `begin(prefs, …)`,
`loop(now_ms)` and a small public API. No drawing, no `DisplayDriver`, no
screen pointers. Engines that need to show something emit an event
(`AlertRequested{text, ms}`, `LocShareEnded`, `AlarmFired`, …) instead of
calling `showAlert()` directly.

Constraints (the lite frontend runs on nRF52 with ~215 KB flash / ~65 KB RAM
free): no heap allocation, no RTTI/exceptions, no STL containers, fixed-size
buffers — the same rules `ui-new` follows today. Extraction must be roughly
size-neutral on `WioTrackerL1_companion_solo_dual`.

### 3. Events: Core → frontend

A small fixed-size queue of tagged events plus coarse dirty flags
(`DIRTY_MESSAGES`, `DIRTY_CONTACTS`, `DIRTY_STATUS`, …). The frontend drains
the queue in its `loop()` and redraws what is dirty. No callbacks into the
frontend from inside mesh processing — which also keeps the e-ink busy-wait
pump rule ("never re-enter UI code from the radio path") trivially true.

### 4. Actions: frontend → Core

Plain methods on the Core facade: `sendDM`, `sendChannelMsg`, `markRead`,
`setLiveShare`, `setLocatorTarget`, `toggleGps`, `startPing`, … Both frontends
call the same ones, so e.g. "marking a channel read" has one implementation.

### 5. Declarative settings schema

`ui-core/SettingsSchema.h` holds five pages -- map options (trail, live share, arrival alert; ui-lvgl opens it from the map's tools), advert (auto-advert; in ui-lvgl's advert popup, the quick panel's Advert), display & power (brightness, screen off, wake on message, lock screen, battery display, battery shutdown, GPS power saving, time zone, 12-hour clock, clock seconds, units) messages & contacts (DM resends, contact expiry, favourites first) and sound (volume, the sound for direct messages / channels / adverts, advert sound scope). An entry is a one-byte index by default, or a `values` table over a 1/2/4-byte field (seconds, millivolts, an hour offset, `{1, 0}` for a field stored inverted); side effects run through `UiCore` (`applyGpsInterval()`, `host()->applyDisplayPrefs()`). ui-lvgl renders each page (`schemaRows()` draws a page's rows into any container, a screen or a popup); ui-new puts a schema section's rows into its own accordion sections (`SCHEMA_DISPLAY`, `SCHEMA_SYSTEM`, … in `SettingsScreen.h`), next to the rows it still draws itself (radio, keyboard, home pages, quick messages). Radio settings are not schema rows (preset list, typed frequency, radio side effects): `ui-core/RadioControl.h` applies them (`applyParams`, `applyTxPower`, `applyApc`, presets = built-ins + user slots) for both ui-new's Settings > Radio and ui-lvgl's Radio screen. Channel management is `ui-core/ChannelControl.h` (`chanctl::`): add Public / Hashtag / Private (passphrase or 32-hex key), rename, delete, notifications / favourite / scope, and `onRemoved()` -- the cleanup of every channel-keyed pref, run by `UiCore::onChannelRemoved` whoever deleted the channel (this device or the app); contacts get the same through `UiCore::cleanupContactPrefs`. Favourites dial slots: `ui-core/Favourites.h` (`pinContact` / `pinChannel` move an entry that is already on another slot; the slot's name is `contactctl::favName`). The schema's display page also carries the lock screen (`auto_lock`) and the 12-hour clock, which ui-lvgl implements (a slide-to-unlock card after the screen turns off), and ui-new's lock clock look (`lock_compact`, Big / Compact), which ui-lvgl leaves out of its rows. Remote admin is `ui-core/AdminSession.h` (`UiCore::admin`): login with the saved password (forgotten on a wrong password or a silent timeout), CLI round trips with timeouts, the field table (`admin::`), typed values; results come out one at a time through `take()`. `UiCore` offers login answers and CLI replies to it first; only room logins it wasn't waiting for reach the frontend. ui-new's Tools > Admin and ui-lvgl's Admin screen are views over it. The bot's settings are a table too, `ui-core/BotConfig.h` (`botcfg::`: tabs, rows, the NodePrefs flag / text / hour behind each, reply placeholders); ui-new's Tools > Bot and ui-lvgl's Bot screen render it, the bot itself runs in MyMesh. Room servers: `ui-core/RoomSessions.h` (`UiCore::rooms`) keeps who is logged in this session, the one login in flight (with a timeout) and the saved passwords (saved on success, forgotten on failure); `open(contact)` says whether to go straight in, wait for a background login or ask for a password, and outcomes come out through `take()`. Per-contact helpers are `ui-core/ContactControl.h` (`contactctl::`: DM notification / melody overrides, favourite flag, the `@[name] ` reply prefix, splitting a room post into author and text, naming a message's path hops). ui-new's Messages and ui-lvgl's Chats / thread use both. Diagnostics rows are `ui-core/Diagnostics.h` (`diag::`: live counters, font card, counter reset), shown by ui-new's Home > Status screen (Mesh and System tabs) and ui-lvgl's Home > Diagnostics. Auto-advert is a schema row (`advert_auto_interval_sec`, sent by MyMesh); ui-new keeps its Tools > Auto-Advert screen over the same pref. The compass (ui-lvgl Home > Compass, ui-new Tools > Compass) reads `UiCore::course`. Saving / deleting the user's radio presets is `radioctl::saveUserPreset` / `deleteUserPreset` (ui-new's preset picker and ui-lvgl's Radio > My presets); the scope list is edited through MyMesh (`addScope` / `renameScope` / `removeScope` / `setDefaultScope`), which ui-lvgl's Radio > Scopes and ui-new's Settings > Radio > Scope call directly. The battery percentage (the built-in LiPo curve or the user's `batt_curve_mv`, empty at the shutdown voltage) and the display mode are `ui-core/Battery.h`, used by both status bars; the schema carries Battery display and Clock seconds. The companion's own repeater mode is `ui-core/RepeaterControl.h` (`rptctl::`: on / off with its radio, RX power-save and APC side effects, Current / Custom network with the profile seeded from the chat params, the profile's preset, the filters' ranges and labels, the extra scopes mask); ui-new's Tools > Repeater and ui-lvgl's Home > Repeater use it, MyMesh does the relaying. Sound is `ui-core/SoundControl.h` (`soundctl::`: the On / Off / Auto mode over `buzzer_quiet` + `buzzer_auto`, the volume with its preview, the built-in sounds, the two user melodies as an editable note list) and `ui-core/SoundNotifier.h` (which sound a message / advert plays: global choice, per-contact / per-channel alert and melody overrides); the schema's Sound page holds the volume and the PLAYS FOR choices, the channel melody override is `chanctl::melody`. Both frontends drive a `genericBuzzer`: a PWM piezo on L1, the ES8311 codec on the L2 (`BUZZER_I2S`: an audio task synthesises the notes and counts samples, the board powers the amp through `buzzerAmpPower()`). Outgoing text is `ui-core/MessageText.h` (`msgtext::`: the ten quick messages, the placeholders on offer -- `{loc}`, `{time}`, `{batt}`, live sensor readings -- and expanding them at send time, keeping a reply's `@[name] ` prefix as typed); ui-new's Messages / keyboard and ui-lvgl's compose "+" use it. `UiCore::markAllRead()` clears every unread counter; `MyMesh::advertFlood()` (formerly sim-only) sends the self-advert through repeaters, `advert()` zero-hop. ui-lvgl's screens share their widgets from `UITask.cpp`: `segmented()` (a one-of-N button row; `radioview::rowSegmented` puts one at the right of a settings row), `textField()` (a one-line field padded to exactly one line), `listRow`, `switchRow`, `headerButton`.

Replaces the per-item code in `SettingsScreen`:

```cpp
struct SettingDef {
  const char* id;          // stable key, also used for search / CLI
  const char* label;
  uint8_t     section;     // Radio / System / Display / …
  SettingType type;        // Bool, Enum, Int, Text, Action
  // accessors over NodePrefs; enum labels; min/max/step
  int  (*get)(const NodePrefs&);
  void (*set)(NodePrefs&, int);
  const char* (*label_for)(int v);        // Enum/Int formatting
  bool (*visible)(const NodePrefs&);       // build- or state-dependent rows
  bool (*locked)(const NodePrefs&, const char** why); // "Off while repeating"
  void (*apply)(UiCore&);                  // side effect after set (radio, display…)
};
```

`ui-new` renders it as today's accordion list; `ui-lvgl` as switches, dropdowns
and sliders. Build-time gating (`FEAT_*`) stays in the table via `#if`, so a
hidden row costs no flash.

### 6. Platform services

Per-board implementations behind small interfaces: `Sound` (buzzer PWM today;
I2S ES8311 tone generator on L2), `DisplayPower` (auto-off, brightness), `Led`,
and input sources. Input reaches frontends in two forms: **focus keys** (the
existing `KEY_*` codes — joystick, buttons, CardKB) and **pointer events**
(`down/move/up` with coordinates — touch). LVGL consumes both natively; `ui-new`
consumes keys only.

## `ui-lvgl` specifics

- LVGL 9 (pinned), memory in a PSRAM pool; partial-render buffers in internal RAM, flush over QSPI via LovyanGFX.
- Theme tokens (colours, spacing, typography) in one place — the first chance to realise the "Amber Trace" direction in colour.
- Fonts ✅ Noto Sans generated by `ui-lvgl/fonts/generate.sh` (lv_font_conv): European Latin (Latin-1, Extended-A/B), Greek, Cyrillic + supplement, typographic punctuation, currency, LVGL symbols; 12/14/16/20 px, clock 40 px digits only. On-screen keyboard (`ui-lvgl/Keyboard.h`), phone-style: one layout per script (Latin QWERTY / Cyrillic ЙЦУКЕН / Greek) chosen by the same `keyboard_main_alphabet` / `keyboard_alt_alphabet` prefs as `ui-new` (globe key switches), hold a key for its variants; compose limit counted in UTF-8 bytes. Long-press variants and UTF-8 case mapping are shared with `ui-new` in `ui-core/KeyboardData.h`.
- Simulator ✅: `SIM_UI=lvgl variants/sim/build_wasm.sh` builds the same `ui-lvgl` code for the browser (`SimLcdDisplay` in `variants/sim/SimDisplayDriver.h` blits LVGL's RGB565 flush to a 320×240 canvas; the `SIM_PLATFORM` branch of `ui-lvgl/LvglPort.h` feeds the mouse in as touch). LVGL comes from the L2 env's PlatformIO libdeps; its objects are cached in `web/build/obj_lvgl/`. `web/lvgl.html` runs it next to a `ui-new` peer on a direct link (buttons: advert, peer DM, peer → Public, the board button) and exposes `window.sim` (`tap`, `hold`, `touch`, `peerDM`, `peerChannel`) for headless checks.
- Map (in progress): offline Web-Mercator raster tiles on the microSD (`/maps/{z}/{x}/{y}.png`, the Meshtastic MUI layout; optional per-column `.pak` from PR #3381's packer), SDMMC 1-bit mount in `LvglPort.h`. `ui-lvgl/map/TileProvider.h` is the swap point: the screen and `map/TileCache.h` (24 decoded RGB565 tiles in PSRAM, LRU) only call `renderTile(z, x, y, rgb565)`, so a vector renderer can replace the PNG provider later. `MapScreen.h`: pan by drag, +/-, re-centre/follow GPS, Nearby nodes with a position as markers (tap = node detail), one tile decoded per loop pass, never during a drag (the view fills in once the finger rests; an idle map decodes the ring around the view ahead, and a not-yet-decoded tile shows a cached coarser one meanwhile). `maps/attribution.txt` is behind a "©" on the map and in Settings > About. Two maps over the same tile view: Nearby's header opens the Nodes map (every node Nearby knows a position for), Home > Map the Navigation map (`NavMap.h`): saved waypoints, live shares, the recorded trail and the Locator target with a dashed line to it and a bar with distance / bearing / own course / ETA (`ui-core/EtaTracker.h`, shared with ui-new's NavView). Hold the map to drop a waypoint, the pin marks the GPS position; the bar opens the target list and the waypoint menu (go, rename, share as `[WAY]` into a conversation, delete). Node detail has Navigate (target followed by key); a person sharing on a channel is followed by sender name (Locator target kind 2). Settings > Navigation carries the `track_shared_loc` / `locator_enabled` switches ui-new has on its Live share / Locator screens. Tiles come from `tools/maps/fetch_tiles.py` (no default server: tile.openstreetmap.org blocks offline use) or on the device: the map's download button fetches the visible area (a few overview levels up to a chosen zoom) over WiFi (`map/TileDownloader.h`; skips tiles on the card, rejects non-PNG and placeholder answers; server from `maps/source.txt`, default OpenTopoMap). The HTTP GET runs on a worker task (`LvglPort.h`, keep-alive TLS); WiFi credentials (Settings > WiFi, with scan) live in NVS, and WiFi is on only during a download. The job (area + zoom range) stays in `maps/.job` until it completes, so a download cut short by power-off, lost WiFi or Stop is offered for Resume (tiles already on the card are skipped). Past the downloaded detail the map magnifies the nearest coarser tile it has (up to 2 levels -- more is just big pixels; zoom pill shows e.g. `z17 (map z15)`); known-missing tiles are remembered without a pixel buffer. Internal RAM is the constraint (Arduino-ESP32 2.0.17 / IDF 4.4: task stacks and, by default, mbedTLS buffers must be internal): with WiFi + Bluedroid up only ~13 KB was left, so the L2 LVGL env sets `MESH_IN_PSRAM` (`the_mesh`, ~120 KB, placement-new into PSRAM in `main.cpp`) and the fetch task points mbedTLS at PSRAM. The download popup shows IP, RSSI, free/largest internal heap and the last TLS/HTTP error. Arduino-ESP32 3.x (IDF 5: PSRAM stacks, NimBLE) is to be evaluated in the polishing phase. In the simulator the same code downloads through emscripten fetch.
- PR #3381's LVGL UI is a reference for platform glue (flush, GT911, PSRAM pool) only; its screens are not adopted.

## Migration plan

Each step keeps `WioTrackerL1_companion_solo_dual` behaviour identical and is
checked in the sim plus on L1 hardware before the next one.

0. **Listener boundary** ✅ (upstream `MyMesh::Listener` ported; `MyMesh` has no UI calls left).
1. **Skeleton** ✅. `examples/companion_radio/ui-core/` with `UiCore.h` (facade), `MessageHistory.h`, `DmUnreadTable.h`. Header-only for now, reached from `ui-new/UITask.cpp` by relative include, so none of the 66 variant `platformio.ini` files that build `ui-new` change. `UITask` heap-allocates one `UiCore` in `begin()` (before the screens, as `MessagesScreen` used to own the history on the heap); `MessagesScreen` binds to `core().history` by reference. When the Core grows real `.cpp` files, compile them through a unity `.cpp` inside each frontend directory.
2. **Engines, one per commit.** Clock tools ✅ (also introduced `ui-core/UiEvents.h`, the Core → frontend event queue; `UITask::tickCore()` runs `UiCore::loop()` and drains it) → ping ✅ → course-over-ground ✅ → live share ✅ → locator ✅ → trail ✅ → notifications ✅ (with step 3). `UITask` shrinks to screen management + drawing.
3. **Flip the interface** ✅. `UiCore` is `MyMesh::Listener`; `ui-new`'s `UITask` derives `UITaskBase` + `UiCoreHost` and is fed by events, drained at the start (mesh-originated) and end (engines) of its `loop()`. Mesh callbacks no longer touch the display or buzzer directly.
4. **Settings schema.** Convert `SettingsScreen` section by section.
5. **`ui-lvgl` skeleton** ✅ on L2 hardware (env `Wio_Tracker_L2_companion_solo_lvgl`, LVGL 9.2.2): status bar, home, conversation list, contact picker, conversation with keyboard, toasts, display sleep/wake. The Core gained the first actions (`sendDirectText`, `sendChannelText`) and runs DM resends itself. Then: European fonts + phone-style keyboard ✅, 320×240 sim target ✅. Next: screens by priority.
6. Contacts/Nearby, Admin, Bot logic extraction as the LVGL screens for them are built. Nearby ✅: `ui-core/NearbyModel.h` builds the list (contacts + live [LOC] shares + heard adverts, or scan results), filter and sort; `ui-new`'s `NearbyScreen` inherits it (entry array stays with the screen), `ui-lvgl` owns one for its Nearby list + node detail (Message, Ping, favourite, Add, Delete). Home became three tiles: Messages, Nearby, Settings.

## Decisions

- **Upstream merge cost accepted** (2026-09-24). Moving code out of `ui-new`
  will conflict with upstream edits to the same files; this fork's `ui-new` is
  already heavily diverged, and upstream is itself working toward a larger UI
  abstraction — revisit alignment when that lands.
- **Scope is UI only** (2026-09-24). The settings schema and event queue serve
  the on-device frontends; CLI and companion-app settings/push paths are
  untouched for now.
