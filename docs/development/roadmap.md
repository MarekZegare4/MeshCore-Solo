# Feature roadmap

Joystick-only UX constraints: 4 directions + Enter + Back. No text entry except inside KeyboardWidget. Everything else navigable with cursor + press.

Status legend: 📋 planned · 🚧 in progress · ✅ done · ❌ rejected/deferred

---

## Priority queue

### ✅ Mark-all-read at type level

Hold Enter on the MESSAGE mode-select screen (DM / Channels / Rooms) opens a 1-item context menu "Mark all read". Acts on the currently highlighted mode and shows a brief confirmation alert.

Implementation:
- New `UITask::clearAllDMUnread()` — `memset` over `_dm_unread_table`
- `MessagesScreen::clearAllChannelUnread()` already existed
- `UITask::clearRoomUnread()` already existed
- Title is a `static const char*` table (PopupMenu stores the title pointer verbatim — locals would dangle)
- Zero schema impact, all counters live in RAM

### ✅ Favourites dial

Phase 1 ✅ (storage + read-only render + grid nav)
Phase 2 ✅ (pin from Contact options menu in QuickMsg + slot picker submenu)
  - Enter on a filled tile opens that contact's DM directly
  - Cancel from a Favourites-opened DM returns to the home screen
Phase 3 ✅ (in-place pin picker on empty tile: upstream-favourited contacts first,
   then recent DM contacts deduped; selecting a contact that's already pinned
   elsewhere moves it to the new slot)

**Follow-up done**: OLED unread-badge overlap fixed — badge and name share the
same baseline, drawTextEllipsized's max width subtracts badge width + a 3 px
gap so names shorten to "Nam…" before the digit.

A 2×3 grid (six slots) of pinned contacts on its own home page, between Clock and Messages. Joystick picks a tile, Enter opens the existing DM conversation or sends a pre-set quick reply.

Data model:
- New field in NodePrefs: `uint8_t favourite_contacts[6][6]` — first 6 bytes of each contact's `pub_key` (enough to disambiguate locally)
- Lookup at render time: walk contacts, match prefix, render name + unread badge
- Empty slot renders as "+" placeholder; Enter on empty opens a contact picker (existing UI)

Pinning UX:
- In QuickMsg DM list, long-press on a contact → context menu → "Pin to dial" → asks which of the 6 slots
- Unpin via the same menu (only shown when contact is already pinned)

Schema bump: add `favourite_contacts` to NodePrefs, bump `SCHEMA_SENTINEL` low byte.

Render layout (250×122 landscape e-ink):
```
╔══════════════════════════════╗
║ Favourites                   ║
╠══════════════════════════════╣
║ ┌──────┐ ┌──────┐ ┌──────┐   ║
║ │Alice │ │Bob 3 │ │  +   │   ║
║ └──────┘ └──────┘ └──────┘   ║
║ ┌──────┐ ┌──────┐ ┌──────┐   ║
║ │Carol │ │  +   │ │  +   │   ║
║ └──────┘ └──────┘ └──────┘   ║
╚══════════════════════════════╝
```

Joystick navigation is natural with 6 tiles (UP/DOWN between rows, LEFT/RIGHT within row).

### ✅ GPS trail (renamed from breadcrumb)

Phase 1 ✅ (storage + sampling + Summary view + G indicator in status bar)
Phase 2 ✅ (auto-fit Map view with cos(lat) aspect compensation; LEFT/RIGHT cycles views)
  - Summary scrolls on short panels (OLED) so hint stops overlapping
  - Status-bar G blinks at the same cadence as A (forces 1 s home refresh)
  - Default sampling 30 s + 5 m min-delta (was 60 s + 25 m — too sparse on foot)
  - "Avg speed" replaces "Speed"; Time uses RTC so it ticks every render
  - Stop → start creates a new segment; the map doesn't bridge dead time,
    and total distance skips segment boundaries
Phase 3 ✅ (per-point list view with HH:MM local time + delta-from-previous;
   segment-start rows show "start" instead of a delta)
Phase 4 ✅ (Hold-Enter popup grows Save / Load / Reset / Export GPX /
   Export saved entries. Single flash slot at /trail (binary header with
   magic+version+count+accumulated_ms then raw TrailPoint records). GPX 1.1
   dump goes over USB Serial; "Export GPX" streams the live RAM ring,
   "Export saved" streams the flash file straight to USB without touching
   the live ring. Segments respect SEG_START boundaries. Alert reflects
   BLE-app-collision state)
Phase 5 ✅ (Settings + actions consolidated into a single Hold-Enter popup —
   Min dist + Units cycled with LEFT/RIGHT (popup stays open), plus
   Start/Stop tracking and Reset action items. Short Enter never toggles —
   both start and stop go through the popup, so a stray tap can never change
   tracking state. View counter (N/3) lives in the title bar; the bottom hint
   row is gone, content fills the freed space. Sampling cadence fixed at 1 s,
   GPS upd setting also removed; both rely on the sensor manager's defaults)
Polish ✅ (map view: filled/open dot markers around segment breaks;
   "Waiting for GPS fix" status when started without a lock;
   capacity bumped to 512 points; elapsed/avg-speed run on millis() instead
   of RTC so they tick even before GPS time is synced)

Tools › Breadcrumb. Periodically samples `(lat, lon, ts)` into a RAM ring buffer; user explicitly saves snapshots to flash.

**Logging is a runtime state**, not a settings value. User starts/stops from the
Tools › Breadcrumb screen. Once active, sampling continues in the background
regardless of which screen is shown, and a `G` indicator appears in the status
bar (analogous to `A` for auto-advert). A reboot resets the active state to
off; the RAM trail is also lost on reboot unless saved to a flash slot first.

Settings only control sampling cadence and the min-distance gate — they don't
enable/disable the feature.

**Storage model — RAM ring with explicit save**

Rationale: auto-off only blanks the display, the firmware keeps running, so the RAM trail survives every idle scenario. Typical use is a single trip start→stop while wearing the device; persisting across reboots is rarely wanted. RAM-only avoids ~1400 flash writes/day and the LittleFS wear that comes with continuous logging.

- Live ring: `BreadcrumbEntry[BC_RAM_CAP]` in `UITask` (or a dedicated component). Each entry `int32_t lat_1e6, int32_t lon_1e6, uint32_t ts` = 12 B. Cap = 256 → 3 KB RAM. nRF52840 (256 KB RAM) has plenty of headroom.
- Wrap-on-write: oldest entry replaced when buffer full.
- Reboot wipes the live trail (intentional; matches the "this trip" model).

**Snapshot slots on flash** (user-initiated only):
- `/breadcrumb.0`, `/breadcrumb.1`, `/breadcrumb.2` — three named slots
- Each file: small header (count, start_ts, end_ts, total_distance_m) + entry array
- Written only on explicit "Save trail" action — zero background writes, zero wear concern
- Optional: auto-save to slot 0 on detected low-battery shutdown (single write before going dark)

UI screens (LEFT/RIGHT cycles):
1. **Summary** — total distance (km), elapsed time (h:mm), point count, current speed (from last 2 samples), GPS fix indicator
2. **Trail map** — ASCII bounding-box plot. Auto-fit the polygon, current position marked `X`, start marked `*`. UP/DOWN zoom, LEFT/RIGHT pan when zoomed.
3. **Last N entries list** — scroll through recent points with timestamp + delta from previous.

Joystick actions:
- Enter → toggle live logging on/off (status bar shows `*` when active, like auto-advert `A`)
- Back → exit
- Hold Enter → context menu:
  - "Reset trail" — clear RAM ring
  - "Save trail → slot N" — snapshot RAM into chosen flash slot
  - "Load trail ← slot N" — restore from chosen slot into RAM ring
  - "Export over USB" — dump live RAM trail as KML/GPX over serial

Statistics computed on the fly walking the ring:
- Total distance: sum of Haversine(p[i], p[i-1])
- Elapsed time: ts[last] - ts[first]
- Current speed: dist(last, prev) / (ts[last] - ts[prev])

Settings:
- Settings › GPS › Breadcrumb interval: 30 s / 1 min / 5 min / 15 min (default 1 min) — only the cadence; logging on/off is a Tools toggle
- Settings › GPS › Breadcrumb min delta: 5 m / 25 m / 100 m (skip near-stationary samples to keep the ring densely populated with real movement)
- Export format: GPX (standard for GPS tracks; OSMAnd / Garmin compatible)

Schema impact: new prefs fields `uint8_t breadcrumb_interval_idx`, `uint8_t breadcrumb_min_delta_idx`. Sentinel bump. The slot files are separate from prefs.

Edge cases:
- No GPS fix: skip sampling, status indicator dims
- Low-batt shutdown: optional auto-save to slot 0 (one write) before powerdown
- Memory: 3 KB RAM is negligible on this MCU; if RAM ever tightens, drop to 128 entries

---

## Backlog (not yet prioritised)

### ✅ Waypoints + navigation cluster — mark a spot, navigate back

**✅ Shipped** (branch `feat/waypoints-nav`). The whole navigation suite landed. Notable deltas from the original spec that follows:

- **Waypoints** — Mark here / list / Rename / Delete / **Clear waypoints** / **Send**; stored in `/waypoints` (16 max), independent of trail recording and kept across Reset trail.
- **Map** — waypoint marker shows the **first two** label chars (not one), placed edge-aware so it stays on-map. With a trail the view frames the recorded route and clamps far waypoints to the nearest edge; with no trail it auto-fits to waypoints + live position. Degenerate single-point case handled.
- **Shared NavView** — one `navview::draw(...)` reused by waypoints, Trail-start backtrack, Nearby-node nav and message-location nav. Shows distance + `To:` + `Hdg:` (two absolute bearings), honouring the global Units setting.
- **COG ring in UITask** — heading source decoupled from trail logging, time-sampled with gross-error rejection + min-displacement gate; restarts after a >15 s GPS gap so a reacquired fix can't imply a teleport heading.
- **Standalone Compass** (Tools › Compass) — heading-up **scrolling tape** with a fixed travel-direction pointer + large degrees/cardinal readout. (A north-up circular dial was tried first and dropped — only a few-px needle fits the OLED's vertical space.)
- **Global Units** (Settings › System: Metric/Imperial) — drives every distance/speed in Tools, the min-distance gate, and the map scale-bar. New `units_imperial` + `trail_show_pace` prefs (schema 0xC0DE0006); the old combined `trail_units_idx` retired.
- **Location over mesh** — Waypoints list → **Send** shares `[WAY]lat,lon label`; a received location ({loc} text or a [WAY] share) offers **Navigate / Save waypoint** from both the message list row and the fullscreen view. Backed by a shared `geo::parseLatLon`.

Shared helpers extracted: `geo::` (haversineKm/bearingDeg/bearingCardinal/fmtDist/parseLatLon) in `GeoUtils.h`; 1-px `gfx::drawLine/drawCircle` in `GfxUtils.h`; one `UITask::currentLocation()` GPS accessor.

The original design spec is kept below as a record.

---

Turns Solo from a comms device into a basic GPS navigator. Mark the current
position with a short label, then later get bearing + distance back to it —
ideal for off-grid use (car, camp, trailhead, water source).

**Storage** — dedicated flash file `/waypoints` (separate from prefs, like
`/trail`). Fixed table, no schema-sentinel impact:

```
struct Waypoint {
  int32_t  lat_1e6, lon_1e6;   // saved fix
  uint32_t ts;                 // when marked (RTC)
  char     label[12];          // short name, NUL-terminated ("CAR", "CAMP", "H2O"…)
};
static const int WAYPOINT_MAX = 16;   // 16 × 24 B = 384 B file
```

**Marking** — from the GPS or Trail screen: Hold Enter → "Mark here".
- Requires a GPS fix; otherwise alert "No GPS fix".
- Opens the existing `KeyboardWidget` to type the label (≤11 chars). Empty
  input auto-labels `WP<n>`.
- Saves the current fix + label, appends to the file.

**Visible on the trail Map** — waypoints render on the existing Map view as a
distinct marker (e.g. a hollow diamond or a small flag) so they show in
context with the recorded track:
- Fold waypoint coords into the map's bounding box so off-track waypoints
  stay in frame. Today `renderMap()` derives the box from
  `TrailStore::boundingBox()`; extend it to also span the waypoint table
  (and handle the "waypoints but empty trail" case — map still renders).
- Generalise the `project()` lambda to take raw (lat, lon) instead of a
  `TrailPoint&` so the same projection draws both track points and waypoints.
- Marker shows the label's first character beside it when there's room
  (122 px is tight with many waypoints); the full label lives in the list /
  nav view.

**Trail workflow integration** — waypoints live *inside* the Trail screen, not
a separate Tools entry, because marking points of interest happens while you
are recording:
- **Mark**: Trail → Hold Enter → "Mark here" (new action-menu row). Opens the
  keyboard for the label, saves the current fix. Works whether or not tracking
  is active — a waypoint is independent of trail recording state.
- **Manage / navigate**: Trail → Hold Enter → "Waypoints" → a PopupMenu list
  of saved waypoints (label + distance). Selecting one:
    - Enter → fullscreen nav (see below).
    - Hold Enter → Rename / Delete (later: **Share over mesh**).
- Waypoints persist across trail Reset / reboot (separate `/waypoints` file),
  unlike the RAM trail ring.

**Navigation view — two bearings, no compass needed**

The L1 has no magnetometer, so we can't show "you are facing X". Instead the
nav view shows two *absolute* bearings and lets the user do the comparison —
robust against GPS jitter, no relative-turn maths:

```
   CAMP            ← waypoint label
   1.4 km          ← distance to target
   To:  145° SE    ← absolute bearing target-from-me (haversine/bearingDeg)
   Hdg: 090° E     ← my course over ground, derived from recent GPS movement
```

Reading it: target is at 145°, I'm travelling at 90° → I need to bear right.
When standing still (course undefined) the Hdg line shows `--` instead of a
stale value.

**Course over ground (COG)** — a small `TrailStore` helper:
`bool currentCourse(int& deg)` walks back from the newest fix until the
cumulative distance from it exceeds a threshold (~10–15 m so GPS noise
doesn't dominate) and returns the bearing from that older fix to the newest.
Returns false (→ `--`) when there isn't enough recent movement. Refreshed ~1 s.
`bearingDeg` / `bearingCardinal` are currently private statics in NearbyScreen;
lift them into a shared header (or TrailStore) so both screens use them.

This COG value can later back a standalone "heading" trail view if wanted, but
the two-bearing nav view already covers the practical need.

**Shared nav view — also navigate to a node** ⭐

The nav view's target is just `(lat, lon, label)` — it doesn't care whether
that came from a waypoint, the trail start (Backtrack), or a node's
last-known advert position. Make it a small reusable component
(`NavView` / `drawNavTo(display, target_lat, target_lon, label)`) and wire it
in from **Nearby Nodes** too: node detail → "Navigate" → same To/Hdg/distance
screen, retargeting the selected node. This folds the backlog "Compass to
contact" idea into one screen and means Nearby stops being a static snapshot —
you can actually walk toward a person.

Node target is the contact's `gps_lat/gps_lon` from its last advert (already
read in NearbyScreen). It's a *last-known* fix, not live, so the label could
show the advert age (e.g. `Alice (5m)`); pair it with Auto-Advert on the other
device for a moving target.

**COG source must be decoupled from trail recording.** Deriving the heading
from `TrailStore` only works while a trail is actively being recorded — but
node/waypoint navigation shouldn't require the user to start trail logging.
Fix: a tiny independent COG ring in UITask (≈4 fixes), maintained on every GPS
poll regardless of trail state. The trail ring stays a separate concern, so
the Hdg line is available everywhere (waypoints, nodes, backtrack).

The COG ring is **time-sampled with guards**, not raw displacement-gated:
push every GPS fix at the normal poll cadence (~1 s) into a short rolling
window (a few seconds of fixes), and compute the heading as the bearing
across the window (oldest→newest), optionally low-pass smoothed. Time-based
sampling gives a steadier, averaged course than bearing between just two
points, and tracks slow movement without lagging.

Two guards keep it honest:
- **Gross-error rejection** — drop a fix before it enters the window if it
  implies an impossible jump (implied speed over a sane cap, e.g. > ~50 m/s
  between consecutive fixes), or if the provider exposes a usable
  validity/HDOP signal. One bad fix shouldn't swing the heading.
- **Minimum displacement over the window** — only emit a heading once the
  total movement across the window exceeds a small threshold (a few m). Below
  that the user is effectively stationary: hold the last good heading, and
  show `--` until the window has cleared the threshold at least once. This
  is what stops a standing user from getting a spinning bearing.

Threshold(s) can be fixed constants to start; expose in Settings later if it
proves worth tuning.

Open question: whether the nav view should also be reachable as a 4th Map
overlay state (cycle a "highlighted" waypoint with LEFT/RIGHT on the Map) or
stay list-driven only. Start list-driven; add map cycling later if wanted.

### ✅ Backtrack — navigate home along the recorded trail

Shipped: the Waypoints list always begins with a synthetic **Trail start** row
whenever a trail exists, opening the shared nav view to the first recorded
point. No new storage. (Navigates to the start point, not progressive
nearest-point following — adequate for "get me back".)

### ✅ Compass to contact — folded into the Waypoints nav view

Superseded by the shared nav view described under **Waypoints** above:
navigate to a node = open that nav view with the contact's last-advert
`gps_lat/gps_lon` as the target. No separate compass screen needed; the
"two absolute bearings (To / Hdg)" approach replaces the relative-heading
arrow this entry originally assumed. Kept here only as a cross-reference.

### 📋 Heading-up (track-up) map orientation

Today the trail Map is north-up. Optionally orient it to the current course
(COG, same source as the compass tape) so the travel direction is up — a Trail
action-menu toggle **Orientation: North-up / Heading-up**.

Lightweight approach: rotate the already-fitted map around its centre by
`-cog` (rotate each projected point before drawing). Point-glyph markers rotate
cleanly; label text stays upright; the north arrow then points to actual north
instead of straight up.

Caveats that make it more than a one-line toggle:
- **COG-only heading** (no magnetometer) — undefined while stationary, so hold
  the last good course or fall back to north-up; it can't be heading-up when
  you're standing still.
- **Jitter** — rotating the whole map by raw COG makes it shake; needs heading
  smoothing / hysteresis (only re-rotate past ~10–15° of change).
- **Fit** — rotated content overflows the rectangle. Refitting to the rotated
  bbox makes the scale "breathe" as you turn; alternative is to accept minor
  edge clipping.
- **Grid** — the axis-aligned scale grid becomes diagonal; drop it in
  heading-up mode or rewrite it.
- **e-ink** — a rotating map ghosts badly and refreshes slowly; this is really
  an OLED feature, flag it as degraded on e-ink.

A fuller position-centred navigator (you fixed at screen centre, fixed/preset
zoom, pan) is a larger separate feature; start with the rotate-the-fit version
if pursued.

### 🚧 Battery / power optimization (duty-cycle RX + APC)

**On branch `feat/power-saving`** — two independent toggles under Settings ›
Radio, both **default OFF**. Under field testing; not yet merged.

**✅ Done — hardware duty-cycle RX ("Pwr save")**
- Uses the SX126x's own **RX duty-cycle** (`SetRxDutyCycle`, datasheet 13.1.7) via
  RadioLib `startReceiveDutyCycleAuto(preamble, 8)`: the chip's sequencer cycles
  RX↔sleep, latches a preamble and stays in RX to receive the packet (RX_DONE on
  DIO1). No MCU state machine — `recvRaw()` reads the packet exactly as in
  continuous RX. `armRecv()` arms duty-cycle when power-save is on, else a normal
  `startReceive()`; `loop()` re-arms only on a toggle. Falls back to continuous RX
  if the modem doesn't support duty-cycle (non-SX126x). `state` stays `STATE_RX`
  so the dispatcher's not-in-RX watchdog never trips.
- Duty-cycle engages when the configured preamble ≥ 2·8+1 symbols. At SF≤8 the
  preamble is 32 → full duty-cycle; at SF9–12 it is 16 → RadioLib transparently
  stays on continuous RX (no power saving on the slow SFs).
- Companion: `rx_powersave` pref (schema `0xC0DE0009`), **Settings › Radio ›
  "Pwr save"**, applied at boot (MyMesh) and on change (UITask). Noise-floor
  sampling is skipped while on (chip is asleep most of the time) — the radio page
  shows "Noise floor: n/a".

  > **History:** an earlier attempt used a *software* CAD state machine (scan →
  > warm-sleep window → on-detect full RX, with `standbyXOSC`/burst windows). It
  > fought the hardware — querying a warm-sleeping chip from `checkSend()` gave a
  > phantom-busy channel that stalled TX for ~4 s, and ACKs dropped in the scan
  > gaps. Replaced wholesale by the hardware duty-cycle above, which fixed both.

**✅ Done — Adaptive Power Control ("Auto pwr")**
- `tx_power_dbm` becomes a *ceiling*; APC drives the radio's actual power within
  `[APC_MIN_DBM −9, ceiling]` to hold the link margin near a target. Lives in
  `MyMesh` (`applyApc` + `apcSampleSnr`/`apcOnFailure` controller), `tx_apc` pref.
- **Two feedback sources**, both the *reverse* link (no protocol change):
  - **Direct messages** — ACK SNR (`onAckRecv`); missed ACK (`onSendTimeout`) =
    lost confirmation.
  - **Channel/flood messages** — no ACK exists, so we hash each originated flood
    (`apcTrackFloodSend` in `sendFloodScoped`, hash excludes the path) and listen
    in `filterRecvFloodPacket` for a repeater rebroadcasting it; the heard echo's
    SNR is a sample, and **no echo within ~6 s** counts as a lost confirmation.
    This is what lets a channel send recover after APC trimmed power below what the
    repeaters can hear (previously it could strand channel TX at the floor).
- Margin is measured **above the per-SF demod floor** (`−7.5 − 2.5·(SF−7)` dB) so
  one target works across SF7–12. Each SNR sample is smoothed with an EWMA (α=0.4);
  power steps **proportionally** to the error (capped ±2 dB) with a ±2 dB deadband
  to avoid hunting; the EWMA is nudged by each step so it doesn't re-trigger on a
  stale sample.
- Lost confirmation → step up `+4 dB`; **2 consecutive** losses → jump to the
  ceiling (ramp gradually first since a loss is an ambiguous power signal). Any
  confirmation clears the streak. Live power shown on the radio page + name bar.

**⏸ To return to (not done)**
- **Current measurement** — never taken (no PPK2/meter to hand). Reliability is
  confirmed in use; the actual mA win is still unquantified. Do this first.
- **APC sample targeting** — a direct ACK's SNR is the last hop of the *return*
  path (sound on symmetric/direct links); a flood echo is the *first* hop from us
  to a repeater, which is exactly what our TX power controls. Both feed one shared
  controller; per-source weighting or direct-only (0-hop) gating could be explored.

**Original analysis (kept for context):**

Goal: multiply battery life **without losing functionality**. The dominant
draw on this node is the radio in **continuous RX** (always-on `startReceive()`
with boosted gain in [`RadioLibWrappers.cpp`](src/helpers/radiolib/RadioLibWrappers.cpp));
the MCU already sleeps between iterations (`sd_app_evt_wait()`/WFE in
[`NRF52Board.cpp`](src/helpers/NRF52Board.cpp)), so the framework is not the
bottleneck — the radio is. The win is framework-agnostic and can land in this
Arduino tree while keeping the Solo UI and upstream sync.

Inspired by **ZephCore** (Zephyr MeshCore port, https://github.com/liquidraver/ZephCore),
whose battery edge comes from radio/peripheral power technique, not the kernel:

- **CAD-based RX windowing** — the biggest lever. Instead of continuous RX, use
  the SX1262 RX-duty-cycle / Channel Activity Detection to briefly sample for a
  preamble, then sleep, repeat. ZephCore quotes ~10–15 mA → ~3–5 mA RX. RadioLib
  supports CAD / `startReceiveDutyCycle`. **Tradeoff:** slightly higher receive
  latency / a small sensitivity hit — acceptable for a companion, must be a
  toggle/setting so users who want lowest latency can keep continuous RX.
- **Adaptive Power Control (APC)** — drop `tx_power_dbm` dynamically when link
  quality (SNR/RSSI of acks) allows; raise it back when needed.
- **Config-level wins** (mostly already exposed): BLE off when idle, OLED
  auto-off (e-ink idle ≈ 0), longer advert intervals.

**Explicitly out of scope: GPS power gating.** ZephCore powers the GNSS only
during a fix; this device is used as a *live* navigator + trail recorder, so GPS
stays continuously powered. Don't gate it.

Cross-check: as of the **v1.16 upstream merge**, MeshCore now ships native
NRF52 companion power-saving ([PR #1238](https://github.com/meshcore-dev/MeshCore/pull/1238),
[docs/nrf52_power_management.md](docs/nrf52_power_management.md)). The companion
loop now sleeps via `board.sleep(0)` whenever `hasPendingWork()` is false — but
that only covers **MCU idle** (it does not touch the radio, which is the real
draw), and there is no on/off toggle because not-sleeping would only waste power.
The same merge also brought the **preamble 16→32 bump for SF<9**, which is what
makes the hardware RX duty-cycle viable at SF8 (it needs ≥ 2·8+1 = 17 preamble
symbols to latch). Prefer adopting/extending upstream work over a parallel
implementation. Note ZephCore's licence before copying any code verbatim
(architecture inspiration is fine).

Sequencing: hardware duty-cycle RX and APC are both done and in field test on
`feat/power-saving`; see the status block at the top of this entry for what's
left (PPK2 current measurement, multi-hop APC gating).

### ✅ Companion repeater + forwarding filters + diagnostics

On-device repeater for the Solo companion, scoped to the SX1262 boards (Wio
Tracker L1 OLED/e-ink, GAT562 30S). All on `feature/companion-repeater-presets`.

- **Repeater toggle** (`client_repeat`, Tools › Repeater) — the companion relays
  flood/direct traffic, still working as a normal companion. By default it
  switches to a dedicated band on enable (see profile note below) rather than
  relaying on whatever network it's chatting on. `MyMesh::allowPacketForward`
  gates it; loop
  detection (`isRepeatLooped`, ported from `simple_repeater`) and an advert
  flood-depth cap are always applied. Packet pool bumped 16→32 to match the
  repeater workload (a too-small pool starved channel/DM reception once relaying
  queued retransmits).
- **Consolidated on Tools › Repeater** — the toggle and the five filters share
  one full-width screen, rather than the original Settings › Radio sub-items
  (whose indented labels collided with the value column on a 128px OLED). Live
  forwarding stats (Forwarded / Pool free / Queue) stay on Tools › Diagnostics.
- **Optional dedicated radio profile** (`Network: Current/Custom`,
  `repeater_use_profile` + `repeater_freq/bw/sf/cr`). Custom switches the radio to
  a preset/manual profile when the repeater is enabled and restores the
  companion's params when disabled (user-chosen revert-on-disable); a profile
  equal to the companion = "same network", a different one = drop onto a separate
  repeater network. `MyMesh::applyRepeaterRadio()` is the single decision point,
  called at boot and on every toggle/edit; `repeaterProfileValid()` gates it.
  Schema sentinel `0xC0DE0010`. **Custom is the default**: a never-configured
  device (or one upgrading from a pre-`0x10` file, which has no saved profile)
  turns the profile on and seeds `repeater_sf/bw/cr` from `LORA_SF/BW/CR` plus
  a band-matched `repeater_freq` — relaying on the same network the operator is
  chatting on isn't the MeshCore community norm, so "Current" stays opt-in.
  `defaultRepeaterFreqForBand()` (`NodePrefs.h`) buckets the companion's own
  `freq` into whichever of the three license-exempt bands MeshCore's app-driven
  repeat toggle historically restricted to (433.000 / 869.495 / 918.000 MHz —
  see `repeat_freq_ranges` in `MyMesh.cpp`), so the seeded default can't land
  outside what's legal for wherever the companion's own network already is.
  `LORA_FREQ/BW/SF/CR` fallback `#define`s moved from `MyMesh.h` to
  `NodePrefs.h` so `DataStore.cpp`'s migration code can see them too.
- **Forwarding filters** (all opt-in, default off, flood-only — a direct route's
  named next hop is never dropped). Shown only while the repeater is on:
  - **Skip advert** — don't re-flood adverts (highest-volume flood).
  - **Max hops** — drop a flood past N hops.
  - **Yield** — scale the retransmit delay for *forwarded* floods only
    (`getRetransmitDelay`); own sends pass their own delay to `sendFlood`, so the
    companion's own traffic is never slowed. Widens the overhear window.
  - **Min SNR** — drop a flood copy below a dB threshold (`-128` sentinel = off,
    so an upgraded prefs file can't read as "filter at 0 dB").
  - **Suppress dup** — overhear cancel: a received flood whose hash matches a
    queued retransmit cancels our copy (`Dispatcher::suppressQueuedDuplicate`,
    `wantsOverhearSuppress` hook). MeshCore had no overhear-cancel before — the
    unused `removeOutboundByIdx`/`getOutboundByIdx` finally have a caller. Packet
    hash ignores the path for non-TRACE, so our copy and the peer's relayed copy
    hash equal. Pairs with Yield (longer delay → wider window to hear a peer).
- **Diagnostics screen** (Tools › Diagnostics) — single read-only page: per-type
  RX/TX counters (generic `Dispatcher::n_recv_by_type`/`n_sent_by_type`), uptime,
  heap + stack (new `DeviceDiag` helper, nRF52 linker-symbol/`sbrk` heap +
  FreeRTOS stack high-water), noise floor, RSSI/SNR, pool free, outbound queue,
  **Forwarded** (`Mesh::n_forwarded` — actual retransmits; backed out on overhear
  cancel so it reflects what hits the air), and **Errors** (Dispatcher
  `ERR_EVENT_*` flags decoded to F/C/R). Hold Enter opens a one-item "Reset
  counters" menu (Back dismisses) — `resetStats` made virtual; `Mesh` override
  also clears `n_forwarded`.
- **Radio settings locked while relaying** — a repeater must hear all traffic and
  relay at consistent power, so duty-cycle RX ("Pwr save") and APC ("Auto pwr")
  are forced off whenever `client_repeat` is on (effective `pref && !client_repeat`
  via `applyPowerSave()` / `apcActive()`), applied at boot, on the on-device
  toggle, and on the app's `CMD_SET_RADIO_PARAMS`; Settings shows `--` and blocks
  the toggle, preserving the user's pref for when the repeater goes off. A
  blinking `»` status-bar indicator (`ICON_REPEATER`) shows relaying at a glance.
- Prefs persisted behind schema sentinels `0xC0DE000E` (four knobs),
  `0xC0DE000F` (suppress-dup), `0xC0DE0010` (radio profile), with stray-byte
  clamps for upgraders.
- **Open question:** the app-side dedicated-band gate in `CMD_SET_RADIO_PARAMS`
  was commented out (not deleted) to match the on-device toggle's any-frequency
  behaviour — undecided whether that gate was UX-only or regulatory.
- **Not done:** live two-device mesh verification (A→repeater→C); counters make
  it observable but don't replace the field test.

### SOS broadcast

Configurable in Settings › System › SOS:
- Target: channel index or DM contact
- Message template (uses placeholders)

Trigger: Hold Back + Hold Enter for 3 s on any screen → confirmation popup ("Send SOS?") → Enter to send. Sends with `{loc}` and `{batt}` filled. 30 s cooldown.

### ✅ Range test — shipped as Nearby Nodes ping

The practical need is covered by **ping** rather than a dedicated screen: in
Nearby Nodes, a node's detail view → **Hold Enter → Ping** sends a direct mesh
ping and shows RTT + SNR (own and remote), repeatable on demand. Available from
both the stored-node detail and the active-discovery detail.

The original idea below (a Tools › Range Test screen with continuous 5 s
pinging and a 30-sample sparkline) was **not** built — kept as a possible
future enhancement on top of the existing ping.

Tools › Range Test:
- Pick a node from contacts/nearby
- Enter starts pinging every 5 s, logs RTT + RSSI + SNR (ring ~30)
- Display shows current values + 30-sample sparkline (block characters)
- Enter stops; Hold Enter for context menu (reset, change target)

### Quiet hours

Settings › Sound › Quiet Hours:
- Enable on/off
- Start HH (LEFT/RIGHT to change, 24 h)
- End HH

When within window: buzzer set to "off" (overrides setting), display brightness → 0. Restores prefs values when window ends. Time source: rtc_clock.

### Channel scanner home page

Toggleable in Settings › Home Pages. Lists channels with: name, unread count, last message age. Enter opens the channel. Sort by recency by default; LEFT/RIGHT toggles to alphabetical.

### Contact distance sort

QuickMsg DM list: a 4-th sort mode (currently sorted by message count). LEFT/RIGHT on the list header cycles: name | message-count | recency | distance. Distance uses GPS pos from contact's last advert.

### Signal stats screen

Tools › Stats:
- Battery voltage 60-min sparkline
- RSSI of last 30 received packets
- Noise floor current
- Free heap (if available)

Read-only. UP/DOWN switches between metrics. Bottom shows current value as text.

### ✅ Auto-reply query commands with live data

Realised as a **command bot** rather than a trigger/reply table: with **Commands** ON, a DM is scanned for `!word` tokens and answered with live node data via `expandMsg` — `!batt`/`!loc`/`!time`/`!temp`/`!status`/`!ping`/`!help`, plus `!hops` (per-message hop count via `getPathHashCount()`, `direct` if heard directly). Multiple commands in one message are merged into a single ` | `-joined reply (one transmission/throttle/counter tick) via the shared `botScanCommands`. Works in DMs (per-contact throttle, ignores quiet hours — a pull) and on the bot's **monitored channel** (broadcast: per-channel cooldown, respects quiet hours). Toggled independently of the trigger bot. See [`MyMeshBot.h`](examples/companion_radio/MyMeshBot.h) `tryBotCommand` / `tryBotChannelCommand` / `botCommandReply`.

### ✅ Lock-screen unread count

Shipped: the lock screen shows a total unread badge (`<n> unread`, summing DM +
channel + room counters) below the time. Reuses the existing unread counters;
no schema change.

### Power profile presets

Settings › Profile: Indoor / Outdoor / Expedition. Each pre-fills:
- Auto-off seconds
- GPS interval
- Auto-advert interval
- Brightness

Single Enter applies. Stored as `uint8_t profile_idx` with hardcoded value tables.

### Battery curve calibration

Settings › System › Batt Calibration: edit 5 voltage breakpoints used to convert mV → %. UP/DOWN selects breakpoint, LEFT/RIGHT changes voltage in 50 mV steps. Helps users with non-standard LiPos report accurate %.

### ✅ Mark-read at type level — done (see "Mark-all-read at type level" above)

### Display test pattern

Tools › Display Test: full-screen grid + bars + Lemon glyph dump. Useful for verifying driver/font changes after flashing.

### Group "who's online" ping

From channel view, Hold Enter → "Who's online?". Sends 0-hop discovery to channel members, collects responses for 10 s, shows a list with RSSI. Similar to existing Nearby active discovery but scoped to a channel.

---

## New ideas (unprioritised)

Captured for later triage. None designed in detail yet (DM delivery status has since shipped — see below).

### ✅ DM delivery status

**✅ Shipped** (in `main`). A per-message marker sits at the end of each outgoing
DM row in the history and in the fullscreen message view. Deltas from the spec
that follows:
- **Pending** isn't a single `·/…` — it draws **one square dot per send attempt**,
  so an auto-resent message shows its retry count at a glance.
- **Delivered / failed** use drawn **scalable mini-icons** (`✓` / `✗`) that scale
  with the font, not glyph-font characters — legible on every layout (see
  `icons.h`, authored as compile-time ASCII-art).
- Adds **DM auto-resend + incoming dedup**, plus a channel **"relayed into mesh"**
  marker (`✓` when a repeater echo confirms the channel message went out).

Original spec:

Show whether an outgoing direct message reached the recipient, using the ACK
that MeshCore already produces (no protocol change). `sendMessage()` returns
`expected_ack` + `est_timeout`; the ACK arrives via `onAckRecv` / `isAckPending`
— the same mechanism APC and ping already consume.

Per-message status glyph at the end of each outgoing DM row in the history:
- `·` / `…` — sent, awaiting ACK (pending)
- `✓` — delivered to the recipient (ACK matched)
- `✗` / `!` — timed out, no confirmation

Data model (RAM only, no schema bump — DM history already lives in RAM):
- `DmHistEntry` gains `uint8_t ack_status` (0=incoming/none, 1=pending,
  2=delivered, 3=failed) and `uint32_t ack_tag` (the `expected_ack` CRC).

Wiring:
- On send: store `ack_tag = expected_ack`, `ack_status = pending`, record the
  send time + `est_timeout`.
- On ACK: route `onAckRecv(ack_crc)` to the UI; find the entry whose `ack_tag`
  matches and set it delivered (single shared callback, like `onPingResult`).
- Timeout: in the UI loop, a pending entry older than `est_timeout` → failed.

Edge cases:
- Sends with no path / `expected_ack == 0` (and channel messages, which have no
  ACK) can't be confirmed → show plain "sent" (`→`) and no delivery state.
- Glyph rendering: prefer a Lemon-font check; on the plain ASCII font fall back
  to drawn 1-px marks or letters so it reads on the OLED.

### 📋 Periodic location beacon ("live share")

Auto-send `{loc}` every N minutes to a chosen channel or DM contact — group
trip tracking. Builds on the existing auto-advert cadence pattern and `{loc}`
expansion. A status-bar indicator (like `A`/`G`) while active; off by default.

### 📋 Arrival / proximity alert

Buzzer + alert when within X m of the active nav target (waypoint / node /
backtrack). Closes the loop on the navigator — you no longer have to stare at
the distance readout. Radius configurable (e.g. 20/50/100 m).

### 📋 "Where am I" location screen

Tools entry showing current lat/lon (optionally MGRS/UTM grid ref), fix quality
(sats / HDOP if the provider exposes it), altitude, and a one-press share to a
channel/DM. Complements the nav suite with an at-a-glance position readout.

### 📋 On-screen QR — share own contact / channel

Render the device's own contact (or a channel) as a QR on the display so a
phone can import it without the companion app. The QR payload format already
exists (`docs/qr_codes.md`); this is the on-device render side.

### 📋 Sunrise / sunset + golden hour

Computed from GPS position + RTC date — pure math, no extra hardware. A Clock
dashboard field or a small Tools readout. Useful for planning outdoor activity.

### 📋 "Find my device" — remote buzzer

A received command (from a paired contact, or a dedicated channel keyword) makes
the node play a locator tone for a few seconds. Helps find a dropped/misplaced
device. Gate behind a setting to avoid abuse.

### 📋 Trail auto-pause

Stop counting elapsed/avg-speed (and optionally skip sampling) when stationary,
so "moving time" and average speed reflect actual travel. Reuses the COG ring's
min-displacement gate to detect standing still.

### 📋 Night mode

Quick toggle for an inverted / minimum-brightness scheme for night use, separate
from the brightness levels. On e-ink flag as degraded (inversion ghosts).

### 📋 Stopwatch / timer

Simple Tools utility — count-up stopwatch and a count-down timer with a buzzer
at zero. Joystick: Enter start/stop, Hold Enter reset.

### 📋 Trail elevation / ascent

If the GPS provider exposes altitude, add total ascent + current elevation to
the trail Summary and `<ele>` tags to the GPX export. Skip cleanly when no
altitude is available.

---

## Deferred

### ❌ Morse keyboard

Cute but niche. Skip unless explicitly requested.

### ❌ Buzzer EVENTS_STOPPED IRQ chaining

Marginal real-world gain (2 ms between notes during melody playback only), high risk on the PWM peripheral.

### ❌ Vibration feedback

Wio Tracker L1 doesn't have a haptic motor. N/A.

---

## Implementation order suggestion

1. **Mark-all-read** — smallest patch, biggest day-to-day comfort gain
2. **Favourites dial** — new home page + new prefs field; touches familiar areas (QuickMsg context menu, NodePrefs, schema sentinel bump)
3. **GPS breadcrumb** — largest of the three; introduces a new flash file and a Tools sub-screen with multiple views

After #3, re-prioritise the backlog with the user.

---

# Code audit — known bugs / hardening backlog

Pass through wio-unified after commit `321d769e`. Grouped by severity. Listed but **not yet fixed**.

## Critical

### ✅ `onChannelMessageRecv` / `onChannelDataRecv` — guard for `findChannelIdx == -1`

[`MyMesh.cpp:561-602`](examples/companion_radio/MyMesh.cpp#L561-L602), [`MyMesh.cpp:619-625`](examples/companion_radio/MyMesh.cpp#L619-L625)

Both group-channel receive paths now check the result of `findChannelIdx()` before continuing:

```cpp
int idx = findChannelIdx(channel);
if (idx < 0) {
  MESH_DEBUG_PRINTLN("...: unknown channel secret — dropping message");
  return;
}
uint8_t channel_idx = (uint8_t)idx;
```

Unknown-secret packets no longer pollute the offline queue, UI history or trigger the bot with a bogus `idx=255`.

### ✅ `addChannelMsg` guards against bogus index

[`MessagesScreen.h:414-433`](examples/companion_radio/ui-new/MessagesScreen.h#L414-L433)

Defensive `if (ch_idx >= MAX_GROUP_CHANNELS) return;` at function entry — prevents ring-buffer pollution in case any future caller forgets the upstream guard. With C1 fixed this should never trigger, but the cost is zero.

## High

### ✅ `findChannelIdx` scans all-zero secret in uninitialised slots

[`BaseChatMesh.cpp:908`](src/helpers/BaseChatMesh.cpp#L908)

Fixed (local override): `findChannelIdx()` now returns `-1` immediately when the queried secret is all-zero, so a corrupted/empty channel can't match an unused all-zero slot. Complements the load-side skip already in `loadChannels()`.

### ✅ `saveChannels` writes all 40 slots to `/channels2`

[`DataStore.cpp:687`](examples/companion_radio/DataStore.cpp#L687)

Fixed: the save loop now skips unused slots (all-zero secret) instead of writing every slot up to `MAX_GROUP_CHANNELS`, so the file holds only the channels actually configured (was always ~2.7 KB). `loadChannels()` already compacted empty entries on read, so the loaded result is unchanged — only on-flash size and write wear drop.

### 📋 `msgRead(0)` wipes the whole DM unread table

[`UITask.cpp:1403-1410`](examples/companion_radio/ui-new/UITask.cpp#L1403-L1410)

```cpp
if (msgcount == 0) {
    memset(_dm_unread_table, 0, sizeof(_dm_unread_table));
    ((MessagesScreen*)messages_screen)->clearAllChannelUnread();
}
```

When the companion app reads the last message from the offline queue, all on-device badges disappear. Previously discussed and a fix was reverted as "intended sync behaviour" — keep as known limitation; document or restrict to "Favourites Dial badges only".

### ✅ Message buffers sized below the protocol maximum (clipped long messages)

[`MessagesScreen.h`](examples/companion_radio/ui-new/MessagesScreen.h), [`KeyboardWidget.h`](examples/companion_radio/ui-new/KeyboardWidget.h)

`ChHistEntry::text` was 140 B and `DmHistEntry::text` only 80 B, while the
keyboard capped input at 139 B — all below MeshCore's `MAX_TEXT_LEN` (160 B).
Channel messages embed the sender as `"Name: body"` in the payload, so the
prefix ate into the 140 and clipped the tail; DMs over ~80 B were cut outright;
and Polish text (2 bytes per accented char) roughly halved the visible limit.
Fixed: history + fullscreen/preview copies sized to `MAX_TEXT_LEN + 1`, keyboard
cap raised to 160 with per-field maxima kept on the smaller stores (custom_msgs,
bot reply). Full-length messages now compose, send, store and display intact.

### ✅ `loadPrefsInt` scopes `trail_units_idx` reset to the 0xC0DE0003 jump

[`DataStore.cpp:326-343`](examples/companion_radio/DataStore.cpp#L326-L343)

The reset is now gated on `sentinel == 0xC0DE0003` so newer mismatches (e.g. 0xC0DE0004 → 0xC0DE0005, which both saved the field correctly) no longer clobber the user's choice.

## Medium

### ❌ `CMD_SET_DEFAULT_FLOOD_SCOPE` off-by-one — not a bug

[`MyMesh.cpp:2143`](examples/companion_radio/MyMesh.cpp#L2143)

Re-checked: `default_scope_name` is declared `char[31]` (not 32), so `n < 31` correctly admits the maximum 30-character string + NUL. The audit entry was a misread.

### ✅ `strlen` on `cmd_frame` without null-termination — replaced with `strnlen`

[`MyMesh.cpp:2140-2147`](examples/companion_radio/MyMesh.cpp#L2140-L2147)

The 31-byte name slot in `CMD_SET_DEFAULT_FLOOD_SCOPE` doesn't have to be NUL-terminated by the sender. Switched to `strnlen(…, 31)` so the search can't run past the field into the 16-byte key (or beyond the frame).

### 📋 `PopupMenu._cap` updated only in `render()`

[`PopupMenu.h:37-44, 77-90`](examples/companion_radio/ui-new/PopupMenu.h#L37-L90)

Left as-is: the framework always renders before forwarding input, so the fragile invariant doesn't fire in practice. Worth a refactor only if the call order ever changes.

### ✅ Scan detail guarded against narrow displays

[`NearbyScreen.h:448-451`](examples/companion_radio/ui-new/NearbyScreen.h#L448-L451)

Pub-key line is skipped entirely when `max_chars < 4` instead of feeding a negative length to `strncpy`. (Lived in `renderDiscoverDetail` before the one-list refactor; now in `renderScanDetail`.)

### 📋 `expandMsg` GPS validity test treats (0, 0) as invalid

[`MyMeshBot.h:95, 132, 182`](examples/companion_radio/MyMeshBot.h#L95)

```cpp
sensors.node_lat != 0.0 || sensors.node_lon != 0.0  // proxy for "valid GPS"
```

Point (0, 0) is a legitimate location (Gulf of Guinea). Corner case but a logic error. Left for a future pass with a proper GPS-validity bool.

## Low

### ✅ SNR division by 4 — now uses `%.1f`

[`NearbyScreen.h:467-470`](examples/companion_radio/ui-new/NearbyScreen.h#L467-L470), [`330-332`](examples/companion_radio/ui-new/NearbyScreen.h#L330-L332)

Scan detail view (`SNR: %.1f dB`, `Rem: %.1f dB`) and the ping popup keep the 0.25 dB resolution. (After the one-list refactor the scan list cards show **RSSI** in the right column, not SNR.)

### ✅ Trail `_count` cast to `uint16_t`

[`Trail.h:27`](examples/companion_radio/Trail.h#L27)

A `static_assert(CAPACITY <= 0xFFFF, …)` next to the CAPACITY definition now fails the build if it is ever grown past what the uint16_t save-header count can hold, instead of silently truncating. Safe today (CAPACITY=512).

### ✅ Bot `strstr` on truncated 199-char buffer — not reachable

[`MyMeshBot.h:11`](examples/companion_radio/MyMeshBot.h#L11)

Re-checked: `BOT_SCRATCH` is 200 and `MAX_TEXT_LEN` is 160, so an incoming message never reaches the 199-char truncation point — the scratch buffer (used by the centralised `botTriggerMatches()`) always holds the whole message. No fix needed.

### ✅ `strncpy("?", buf, sizeof(buf))` replaced with `strcpy`

[`MessagesScreen.h:785, 858`](examples/companion_radio/ui-new/MessagesScreen.h#L785)

Two fallback "?" sender names now use a plain `strcpy` so we don't memset 21 unused bytes for a one-character string.

### ❌ Title truncation in MSG_PICK reply mode — not an overflow

[`MessagesScreen.h:1279-1287`](examples/companion_radio/ui-new/MessagesScreen.h#L1279)

Re-checked: `rlen` is clamped to 20 before building `"RE:" + nick`, so the title is ≤23 chars and fits `title[24]` with no overflow. A nick longer than 20 chars is shown truncated, but that's an intentional fit-to-header limit (the OLED header only fits ~21 chars anyway), not a bug.

## Priority for merge

Fix status after this pass:

- ✅ C1 + C2 — `findChannelIdx == -1` guarded at both channel-recv paths; `addChannelMsg` defends against bogus index
- ✅ H4 — `trail_units_idx` reset scoped to the 0xC0DE0003 jump
- ✅ M2 — `strnlen` instead of `strlen` on default scope name
- ✅ M4 — `renderDiscoverDetail` skips pub-key line on very narrow displays
- ✅ L1 — SNR shown with 0.25 dB precision everywhere
- ✅ L4 — fallback `"?"` sender no longer memsets through `strncpy`
- ✅ Trail map grid silent loss — superseded by the Trail refactor: `renderGrid` now picks a round labelled step (`1m…100km` / `10ft…100mi`) nearest ~1/3 of the shorter side and enforces a `MIN_GRID_PX` floor, so the grid can never silently vanish on an elongated trail. [`TrailScreen.h:635`](examples/companion_radio/ui-new/TrailScreen.h#L635)
- ❌ M1 — re-checked, not a bug (`default_scope_name[31]`)
- 📋 H1 + H2 — still open; need coordinated fix in upstream `BaseChatMesh` (`findChannelIdx` should iterate `num_channels`, not `MAX_GROUP_CHANNELS`; `saveChannels` should stop at the first uninitialised slot) or a local override
- 📋 H3 — left as known limitation pending UX call
- 📋 M3, M5, L2, L3, L6 — minor or stylistic; left in backlog
