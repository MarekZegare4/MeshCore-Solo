# Solo UI framework — a guide for adding features

[Go back](../README.md)

This is a developer guide to the reusable building blocks behind the
`companion_radio` **solo** firmware UI (the `ui-new` screens). It is not a
user manual — for what each screen *does*, see the [documentation](../README.md).
The goal here is so that adding a new screen or feature means *wiring together
existing helpers*, not reinventing list scrolling, text wrapping, or persistence.

Everything below lives under `examples/companion_radio/` unless a path says
otherwise. The screen fragments (`ui-new/*.h`) are all `#include`d, in order,
into one translation unit (`ui-new/UITask.cpp`) — so a `static inline` helper in
an earlier header is visible to later ones. Header-include order in `UITask.cpp`
therefore matters; new screens go near the others.

> **Single-TU only.** These fragments compile *only* as part of `UITask.cpp`.
> Some define external-linkage symbols at file scope (e.g.
> `NearbyScreen::FILTER_LABELS`), so including a fragment from a second `.cpp`
> is a duplicate-symbol link error. Anything genuinely shared across TUs must
> live in a real header (`icons.h`, `GeoUtils.h`, `DisplayDriver.h`), not a
> screen fragment.

---

## 1. The screen model

Every screen implements `UIScreen` (`src/helpers/ui/UIScreen.h`):

```cpp
class UIScreen {
public:
  virtual int  render(DisplayDriver& display) = 0;  // returns ms until the next render
  virtual bool handleInput(char c) { return false; }
  virtual void poll() { }
  virtual void onShow() { }                         // reset per-visit state
};
```

- **`render()`** draws one frame and returns *how long until it wants to be
  drawn again*, in milliseconds. Return a big number (`2000`) for a static
  screen, a small one (`50`–`200`) while something animates or a popup is open.
  This return value is the main lever for the e-ink cost/latency trade-off — see
  §9. `UITask` owns `startFrame()`/`endFrame()`; **`render()` must not call them.**
- **`handleInput(c)`** gets one key (`KEY_*`, see §7). Return `true` if consumed.
- **`poll()`** runs every loop tick regardless of focus — rare, for background
  housekeeping (e.g. the shutdown button).
- **`onShow()`** is called by `setCurrScreen()` every time the screen becomes
  current — override it to reset per-visit state (`_sel = 0`, `_dirty = false`,
  sub-views). Default no-op for screens that keep state across visits. Because
  it's invoked centrally, a navigator can't forget to reset on show.

### Wiring a screen into UITask

1. Add a `UIScreen* my_screen;` member in `UITask.h` (near the others).
2. Construct it in `UITask::begin()` (`UITask.cpp`):
   `my_screen = new MyScreen(this, …);`
3. Add a navigator — usually just the one line (the cast-free `onShow()` runs
   inside `setCurrScreen`):
   ```cpp
   void UITask::gotoMyScreen() { setCurrScreen(my_screen); }
   ```
   Only screens needing a *parameter* at entry add a typed call after it (e.g.
   `gotoRingtoneEditor` → `selectSlot(slot)`, `gotoMapScreen` → `showMapView()`).
4. Reach it from somewhere — usually a row in `ToolsScreen.h` (add an `Action`
   enum value, a row in the right section table, and a `run()` case). The
   Tools home page lists the last two tools used; give the new one a state
   in `HomeScreen::toolState()` (`UITask.cpp`) if it has one worth showing.

That's the whole contract. Steps 1, 3 and 4 are compiler-checked (a mismatch
won't link); only a forgotten step 2 can slip through — every screen pointer is
nullptr-initialised in `UITask.h` and `setCurrScreen()` bails on null, so a
missed `new` is an inert no-op rather than a null deref.

The constructor takes `UITask* task` plus whatever it needs (`NodePrefs*`,
`KeyboardWidget*`, …); the task back-pointer is how a screen calls shared
services (`_task->showAlert(...)`, `_task->waypoints()`, …).

---

## 2. Layout metrics & text (DisplayDriver)

`DisplayDriver` (`src/helpers/ui/DisplayDriver.h`) abstracts OLED vs e-ink and,
crucially, font scale: landscape e-ink renders text at 2×, so **never hard-code
pixel sizes** — derive everything from these:

| Call | Meaning |
| --- | --- |
| `getLineHeight()` | pixel rows per text line (8 at 1×, 16 at 2×) |
| `lineStep()` | row pitch = line height + gap; use for row `y` stepping |
| `getCharWidth()` / `getTextWidth(s)` | advance width; `getTextWidth` is font-accurate |
| `headerH()` / `listStart()` | title-bar height / first content row `y` |
| `listVisible(itemH)` | how many rows fit below the header |
| `valCol()` | conventional x for a right-hand value column |
| `width()` / `height()` | panel size in px |
| `isEink()` | true only on landscape e-ink; branch on this, not on pixel counts |

Drawing helpers (all clip/measure for you):

- `drawScreenHeader(d, title, page=-1, pages=0, menu_hint=false, menu_open=false)`
  (`ui-new/icons.h`) — every screen's title bar: the title (title case, no
  icon) centred over the separator, page dots at the right for a screen with
  views (`page` of `pages`, e.g. Trail's Summary / Map / List), and the `≡`
  hint when the screen has a Hold-Enter menu, so the menu is discoverable
  without knowing the shortcut; `menu_open` highlights it while the menu is
  up. A title too long to centre goes left and ends in an ellipsis.
- `drawInvertedHeader(label, menu_hint=false, menu_open=false)` — a filled
  title bar, kept for the node detail and Navigate views.
- `drawSoftRect` / `fillSoftRect` — a box with its corner pixels left out;
  every frame and fill in the UI uses them (selections, pills, buttons).
  `drawPanel(x, y, w, h)` is a box floating over the view (alert, popup,
  hint): it clears a 1 px moat, fills dark and draws the soft border.
- `drawSelectionRow(x, y, w, h, sel)` — the highlight bar behind a list row.
- `drawTextEllipsized(x, y, max_w, str, selected=false)` — truncates with `…`;
  **use this for any user string** (names, labels) so long/UTF-8 text can't
  overrun. Pass `selected=true` for the currently-selected row and the text
  that doesn't fit marquee-scrolls into view (pause → scroll to the end →
  pause → scroll back), instead of just sitting behind the ellipsis; returns
  the ms until the next redraw is needed for that animation to stay smooth
  (0 when nothing is scrolling) — thread it into your screen's own `render()`
  return value the same way you already clamp for anything else that needs a
  faster redraw. Only one row UI-wide marquees at a time (whichever is
  currently selected), so there's no risk of two animations racing each
  other for the shared timing state.
- `drawTextCentered(mid_x, y, str)`.
- `translateUTF8ToBlocks(dst, src, n)` — map UTF-8 to the panel's glyph set for
  *display only*. Never run text through it before sending it over the air or
  storing it (it is lossy) — see the reply-prefix note in §5.

---

## 3. Lists — `drawList`

`drawList` (`ui-new/icons.h`) is the workhorse for any scrolling list. It
computes the visible window from font metrics, keeps `sel` in view, reserves the
scrollbar column, draws each visible row through your callback, and draws the
indicator:

```cpp
drawList(display, count, _sel, _scroll, [&](int idx, int y, bool sel, int reserve) {
  drawRowSelection(display, y, sel, reserve);   // canonical highlight bar
  display.drawTextEllipsized(2, y, display.width() - reserve - 4, items[idx].name);
});
```

`reserve` is the width the scrollbar took (0 when the list fits) — subtract it
from any right-aligned content so nothing slides under the indicator. The row
callback owns its own selection bar: `drawRowSelection(d, y, sel, reserve)`
(`ui-new/icons.h`) draws the standard one (full row minus reserve, one pixel
short); call `display.drawSelectionRow()` directly only when a row needs a
non-standard geometry (full-width, custom height). For the
fold-in-place pattern (sections that expand/collapse) use `AccordionList`
instead (`ui-new/AccordionList.h`) — same idea, two callbacks (header + item).

Standalone scroll indicators (`drawScrollIndicator`, `…Px`) and the reserve
calculator (`scrollIndicatorReserve`) are exposed for hand-laid lists.

---

## 4. Reusable components

| Component | Header | Use for |
| --- | --- | --- |
| `PopupMenu` | `PopupMenu.h` | a modal action menu over any screen |
| `AccordionList` | `AccordionList.h` | collapsible sectioned lists (Tools, Settings) |
| `KeyboardWidget` | `KeyboardWidget.h` | on-screen text entry |
| `DigitEditor` | `DigitEditor.h` | scroll-edit one number, digit by digit |
| `FullscreenMsgView` | `FullscreenMsgView.h` | scrollable full-message reader + word wrap |
| `NavView` | `NavView.h` | bearing/distance/ETA "navigate to a point" view |
| `TabBar` | `TabBar.h` | a row of tabs: the current one a soft pill, whole tabs only, ◂ ▸ where more are off screen |
| `info::` rows and blocks | `InfoKit.h` | settings rows, data screens (below) |

All follow the same shape: a `begin(...)` to open, an `active` flag, a
`handleInput(c)` returning a small `Result` enum, and a `render()`/`draw()`.
Typical embedding:

```cpp
if (_menu.active) {                       // popup eats input while open
  auto r = _menu.handleInput(c);
  if (r == PopupMenu::SELECTED) runAction(_menu.selectedIndex());
  return true;
}
...
_menu.begin("Options", 6);                // open it
_menu.addItem("Navigate"); _menu.addItem("Ping");
_menu.active = true;
```

`KeyboardWidget` additionally supports **placeholders** (`{loc}`, `{time}`,
sensor tokens) via `addPlaceholder()` / `clearPlaceholders()`; the shared
`kbAddSensorPlaceholders()` (`ui-new/SensorPlaceholders.h`) adds only the tokens
the board's sensors actually provide. Expand them with `expandMsg()` at send time.

Two layouts share every grid: **ABC** (one key per letter) and **T9**
(phone-keypad multi-tap — repeated Enter within `KB_T9_TIMEOUT_MS` cycles a
cell's letter group, then its digit). Page 0 is no longer hardcoded to Latin:
`NodePrefs::keyboard_main_alphabet`/`keyboard_alt_alphabet` (Settings >
Keyboard's Main/Additional rows) each pick a script — Latin, Cyrillic, or
Greek — for page 0 and page 1 respectively (`KeyboardWidget::mainScript()`/
`altScript()`); equal values collapse to a single script + Symbols (2 pages
instead of 3, see `hasAltAlphabet()`). `scriptCellStr()`/`scriptT9GroupStr()`
dispatch each script to its own ABC grid (`KB_CHARS`/`KB_CYRILLIC_CHARS`/
`KB_GREEK_CHARS`) and T9 group table (`KB_T9_GROUPS`/`KB_T9_GROUPS_CYRILLIC`/
`_GREEK`) so the two layouts always offer the same letters regardless of which
page they're on. Latin-diacritic letters (Polish, Czech, German, etc.) aren't
alt-alphabet pages — they're reached by Hold-Enter on whichever page currently
shows Latin instead (see `KB_ACCENT_VARIANTS` below).
Shift is one-shot by default (capitalises the next letter, including whichever
candidate a T9 multi-tap cycle settles on) or Hold-Enter to toggle caps-lock;
Hold-Clear erases the whole field. **UP from the top letter row** enters
**cursor mode** (LEFT/RIGHT move the insertion point; UP/DOWN jump to
start/end, then — pressed again once already at that boundary — continue on
to the special row / letter grid, the same destinations the plain grid wrap
used to reach directly) so edits/inserts can target any point in the typed
text, not just the end; Enter/Cancel exit immediately from anywhere.
Hold-Enter on a Latin-page letter cell with accented variants instead opens
the **accent popup**: one horizontal row of `KB_ACCENT_VARIANTS[group]`
(a UTF-8 string per base letter, same shape as a T9 group string), LEFT/RIGHT
to pick, Enter to insert via the shared `insertGlyph()` helper, Cancel to
dismiss. Holding a letter with no variants, or any T9/alt-alphabet/symbols
cell, is a no-op.

**InfoKit** (`ui-new/InfoKit.h`, namespace `info`) is the shared look of
settings and data screens. Rows (drawn after `drawRowSelection()`, in its
ink): `valueRow(d, y, label, value, sel, reserve, x0)` — label left, value
flush right with its unit after a space ("50 m"), cut and marquee-scrolled
when selected; `switchRow` — a yes/no setting as an on/off switch
(`toggle()`); `listRow` — picks between the two for a formatter that returns
exactly "ON" / "OFF". `rowWrapped` gives a long value its own line instead of
an ellipsis nobody can scroll. Blocks for data screens: `section` (a centred
"··· Font ···" divider), `meter`, `battery`, `chip`, `big` (a 2× number with
its unit), `History<N>` + `spark` (a sparkline), `rule` / `vrule`. `Flow`
places blocks top-down, skips the ones scrolled off and tells whether more
are below, which gives the scroll indicator for free (see `StatusScreen.h`).

Other drawing helpers in `icons.h`: `drawButton` (a push button, soft pill,
filled while selected), `drawDisclosure` (▸ / ▾ on an accordion section),
`drawPageDots`, `drawLoadingDots` (the waiting animation), `drawSignalBars`
(SNR as four bars).

`FullscreenMsgView::wrapLines()` is a standalone pixel-accurate word-wrapper
(O(n), variable-width-font aware) reusable by any multi-line layout; it writes
into the shared `s_wrap_trans` / `s_wrap_lines` scratch (single-threaded render,
never held across a yield — see §9).

---

## 5. Domain helpers

**Geo (`GeoUtils.h`, namespace `geo`, all pure/header-inline):**

- `haversineKm(lat1,lon1,lat2,lon2)`, `bearingDeg(...)`, `bearingCardinal(deg)`.
- `fmtDist(buf,n,km,imperial)` — "850m"/"2.3km" or feet/miles.
- `fmtAgeShort(buf,n,now,ts)` — compact "12s"/"5m"/"3h"/"2d" tag, "" for unknown.
  **This is the one age formatter** — don't reimplement the s/m/h ladder.
- `parseLatLon(text, lat, lon, label?, n?)` — pull a `lat,lon` out of message
  text; reads the `[WAY]` label if tagged.
- `parseLocShare(text, lat, lon)` — true only for an explicit `[LOC]` share.

Coordinates are **int32 degrees × 1e6** everywhere (GPS, contacts, trail,
prefs). The message tags are `LOCATION_MSG_TAG` (`[LOC]`, the sender's own live
position) and `WAYPOINT_MSG_TAG` (`[WAY]`, a saved point to share); both stay
human-readable on clients that don't know them.

**State stores:** `TrailStore` (`Trail.h`, GPS breadcrumb ring + GPX export),
`LiveTrackStore` (`LiveTrack.h`, RAM table of others' `[LOC]` positions, expiring),
`WaypointStore` (`Waypoint.h`, persisted saved points). Reach them via the task
(`_task->trail()`, `_task->liveTrack()`, `_task->waypoints()`).

**Message reply prefix:** `msgReplyBody(text, nick?, n?)` (`FullscreenMsgView.h`)
parses a leading `@[nick] ` reply marker, returning the body and optionally the
addressee. Use it instead of re-scanning for `@[`. The stored/sent prefix is raw
UTF-8 (it goes over the air) — never transliterate it.

---

## 6. Mini-icons & the status bar

Small glyphs are authored as ASCII art and packed at compile time
(`ui-new/icons.h`):

```cpp
MINI_ICON(ICON_FOO, 5,
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"));
```

Draw with `miniIconDraw(display, x, topY, ICON_FOO)` (auto-scaled & centered),
`miniIconDrawTop` (exact placement), `miniIconDrawCentered`, or `drawSlotIcon`
(centred in a status-bar slot). Use icons sparingly: headers and the Tools
list have none, the Home page row is the main place for them.

The home status bar (`HomeScreen::renderBatteryIndicator()`, `UITask.cpp`)
draws plain glyphs right to left from a fixed, priority-ordered table, with
no boxes and no blinking: each icon tells a state by its shape (Bluetooth only
while a phone is connected, the GPS reticle broken until a fix, a pin while
the trail records). Once the row runs out of room the loop stops, so the
lowest-priority icons drop first instead of the bar crushing the time. Add an
indicator as one more row in that table, at its priority.

Screens with a Hold-Enter context menu (Nodes, Messages, Bot, Admin, …) pass
`menu_hint=true` to their header call (see §2) so a `≡` glyph advertises the
menu; `KEY_CONTEXT_MENU` (Hold-Enter) opens it.

---

## 7. Input

Keys arrive as the `KEY_*` codes in `UIScreen.h`: `KEY_UP/DOWN/LEFT/RIGHT`,
`KEY_ENTER`, `KEY_CANCEL`, `KEY_CONTEXT_MENU` (the "Hold Enter" menu key).

Use the **prev/next convention** for value changes so the rotary encoder and the
D-pad agree: `keyIsPrev(c)` (LEFT or encoder-prev) and `keyIsNext(c)` (RIGHT or
encoder-next). `KEY_CANCEL` and `KEY_CONTEXT_MENU` stay screen-specific. Joystick
rotation is handled upstream (`rotateJoystickKey`) — screens see already-rotated
keys.

### From hardware to `handleInput`

Each physical button is a `MomentaryButton` (`src/helpers/ui/MomentaryButton.h`);
`UITask::begin()` must call `begin()` on **every** one (the joystick directions
and Back included, not just the user button) — that sets `pinMode` and, where
enabled, claims the interrupt. `UITask::loop()` polls each button, maps its event
to a `KEY_*` code, and dispatches it to the current screen.

Two mechanisms keep input responsive when **`endFrame()` blocks the loop** for a
slow e-ink refresh:

- **IRQ edge capture** (`-D BUTTON_USE_INTERRUPTS`, e-ink boards). A GPIO
  interrupt latches each press/release edge into a per-button ring buffer with
  its own timestamp, so taps that land *during* a refresh aren't lost; `check()`
  replays them afterwards. The nRF52 has only 8 GPIOTE channels (the radio takes
  one) — if none is free a button silently falls back to polling, so it still
  works, just without mid-refresh capture.
- **Key queue + coalesced redraw.** `loop()` drains *all* pending events from the
  buttons into a small key FIFO, applies the whole burst (`handleInput` per key),
  then redraws **once**. So three joystick flicks captured during one refresh move
  the selection three steps for the cost of a single panel update, instead of
  collapsing into an ignored multi-click or one-step-per-refresh. Buttons created
  with `multiclick=false` therefore emit one discrete `CLICK` per release;
  `multiclick=true` buttons still report double/triple-click.

---

## 8. Persistence

Device settings live in one `NodePrefs` struct (`NodePrefs.h`), saved via
`the_mesh.savePrefs()` and loaded by `DataStore.cpp`. Rules when adding a field:

- **Append only**, and bump `NodePrefs::SCHEMA_SENTINEL`. Serialization is
  binary-positional, so order is the on-disk format; never insert in the middle.
- Add a matching `rd(...)` in `DataStore::loadPrefsInt()` and a `file.write(...)`
  in `savePrefs()`, in the same position, and **clamp on load** (an upgrader's
  file lacks the field and reads stray bytes — clamp to a sane default). Saves
  are atomic (temp-file + rename), so a crash mid-save can't corrupt settings.

**The `_dirty` convention:** a multi-field editor screen mutates `_node_prefs`
live for instant feedback but only persists once, on exit, gated by a `_dirty`
flag — so LEFT/RIGHT value-cycling doesn't thrash flash. Set `_dirty = true` at
each edit site, then on the exit path call `_task->savePrefsIfDirty(_dirty)`
(`UITask`) — it saves once *iff* dirty and clears the flag, so every screen's
save-on-exit reads the same and the "did we touch flash?" decision lives in one
place. A one-shot action from a popup (no exit hook) calls `the_mesh.savePrefs()`
immediately. Follow whichever matches your screen.

**The shared "active target"** (Locator/Nav destination) is set through
`UITask::setTarget()` (defines it), `setTargetNow()` (defines + saves + toast),
or `clearTarget()` — one definition used by the Locator screen, the map, and the
Nearby/Waypoints "Set as target" actions. Resolve a person's current position
with `resolvePersonPos()` (live `[LOC]` share, else last-advertised fix).

---

## 9. Conventions & gotchas

- **Single-threaded render.** Rendering and input run on one thread, so shared
  static scratch (`s_wrap_*`) is safe *as long as it's never held across a
  yield*. Don't add scratch that outlives one `render()`.
- **e-ink blocks.** `endFrame()` on e-ink stalls the main loop for hundreds of
  ms. Keep `render()`'s return value honest so the panel isn't redrawn more than
  needed, and don't depend on `loop()` cadence for timing that must be exact
  (the ringtone player moved to a hardware timer for this reason).
- **Reference cleanup.** Anything that remembers a contact by pubkey (favourite
  slot, Locator/Live-Share target, per-contact mute/melody) or a channel by
  index must drop that reference when the entity goes away. The UI Core does
  it for every frontend: `UiCore::cleanupContactPrefs()` (ui-core/UiCore.h) and
  `chanctl::onRemoved()` (ui-core/ChannelControl.h). New per-contact or
  per-channel state should clear there too.
- **Toasts:** `_task->showAlert("msg", duration_ms)` overlays a transient banner
  over any screen (not over the keyboard); no redraw plumbing needed. Use one
  only for what the screen doesn't already show: a failure ("Send failed",
  "Contacts full") or an outcome somewhere else. No echo of an action whose
  result is in view ("Sent", "Deleted", "Counters reset").
- **Time:** `millis()` wraps after ~49.7 days. Compare deadlines as
  `(int32_t)(millis() - deadline) >= 0`, never `millis() >= deadline`, and use
  0 as "now / none" (`UITask::refreshDue()`, `alertShowing()`).
- **Strings:** always `strncpy`+NUL or `snprintf`; treat every name/label as
  untrusted-length and render through `drawTextEllipsized`.

---

## 10. Worked example — a new Tools screen

```cpp
// ui-new/MyToolScreen.h  — included by UITask.cpp near the other screens
#pragma once
#include "icons.h"          // drawList, drawScreenHeader, mini-icons
#include "InfoKit.h"        // valueRow / switchRow
#include "../NodePrefs.h"

class MyToolScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;
  int        _sel = 0, _scroll = 0;
  bool       _dirty = false;
  static const int ROWS = 3;
public:
  MyToolScreen(UITask* t, NodePrefs* p) : _task(t), _prefs(p) {}
  void onShow() override { _sel = 0; _scroll = 0; _dirty = false; }

  int render(DisplayDriver& d) override {
    d.setTextSize(1);
    drawScreenHeader(d, "My tool");
    int next = 500;
    drawList(d, ROWS, _sel, _scroll, [&](int i, int y, bool sel, int reserve) {
      drawRowSelection(d, y, sel, reserve);
      static const char* const L[ROWS] = { "Alpha", "Bravo", "Charlie" };
      const int m = info::valueRow(d, y, L[i], "50 m", sel, reserve);   // value flush right
      if (m > 0 && m < next) next = m;                                   // a marquee moving
    });
    return next;
  }

  bool handleInput(char c) override {
    if (c == KEY_CANCEL) {
      _task->savePrefsIfDirty(_dirty);   // saves once iff dirty, then clears
      _task->gotoToolsScreen();
      return true;
    }
    if (c == KEY_UP)   { _sel = (_sel + ROWS - 1) % ROWS; return true; }
    if (c == KEY_DOWN) { _sel = (_sel + 1) % ROWS;        return true; }
    if (keyIsPrev(c) || keyIsNext(c) || c == KEY_ENTER) {
      /* mutate _prefs…, set _dirty = true */ return true;
    }
    return false;
  }
};
```

Then: `#include "MyToolScreen.h"` in `UITask.cpp`, add the member + constructor +
`gotoMyToolScreen()` (§1), and add a row in `ToolsScreen.h`. Done — scrolling,
the scrollbar, font scaling, e-ink pacing and persistence batching all come from
the framework.
