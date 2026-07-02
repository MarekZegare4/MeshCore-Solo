#pragma once

#include <stdint.h>
#include <helpers/ui/DisplayDriver.h>

// ── Scalable mini-icons ──────────────────────────────────────────────────────
// Small procedural glyphs (delivery markers, etc.) authored on a 1× pixel grid
// and scaled to the current font, so they stay legible on large-font layouts
// (e.g. landscape e-ink renders text at 2×). Distinct from the XBM page icons
// further down, which are fixed-size full bitmaps drawn via drawXbm().
//
// To add a mini-icon:
//   1. Draw it as ASCII-art rows, one string per row (width ≤ 8): any char
//      other than ' ' or '.' is a filled pixel. packRow() packs each string
//      into one byte at compile time, so the strings never reach flash — the
//      binary holds only the packed bytes, identical to hand-written hex.
//   2. Wrap the rows in a MINI_ICON(name, width, ...) — width and row count are
//      bundled with the data, so the draw site carries no magic numbers.
//   3. Draw it with miniIconDraw(display, x, topY, name).
// Scaling is automatic — no per-display handling needed.
//
// packRow() is written as a single-expression recursion so it stays a valid
// constant expression on any C++ standard the firmware targets (≥ C++11), not
// just the relaxed C++14 constexpr with loops. It packs up to the string's NUL
// (capped at 8 cols), so width lives once in MINI_ICON, not in every row.
constexpr uint8_t packBit(const char* s, int x) {
  return (s[x] && s[x] != ' ' && s[x] != '.') ? (uint8_t)(1u << x) : 0;
}
constexpr uint8_t packRow(const char* s, int x = 0) {
  return (!s[x] || x >= 8) ? 0 : (uint8_t)(packBit(s, x) | packRow(s, x + 1));
}

// A mini-icon bundles its pixel data with its dimensions, so call sites can't
// pass a stale width/height. Built via the MINI_ICON macro below.
struct MiniIcon { uint8_t w, h; const uint8_t* rows; };

// Define a mini-icon: the row count (height) is derived from the initializer,
// the width is stated once. Emits a packed byte array plus a MiniIcon view.
#define MINI_ICON(name, width, ...)                                            \
  static constexpr uint8_t name##_rows[] = { __VA_ARGS__ };                    \
  static constexpr MiniIcon name = { (uint8_t)(width),                         \
                                     (uint8_t)sizeof(name##_rows), name##_rows }

// Pixel scale from the font: 1× on an 8px OLED line, 2× on a 16px landscape
// e-ink line, etc. Bitmaps are authored on the 1× grid.
inline int miniIconScale(DisplayDriver& d) {
  int s = d.getLineHeight() / 8;
  return s < 1 ? 1 : s;
}

// Draw a w×h (w ≤ 8) bitmap with the current ink colour, scaled by the font and
// vertically centred in the text line that starts at top_y.
inline void miniIconDraw(DisplayDriver& d, int x, int top_y,
                         const uint8_t* rows, int w, int h) {
  const int s = miniIconScale(d);
  int y = top_y + (d.getLineHeight() - h * s) / 2;
  if (y < top_y) y = top_y;
  for (int r = 0; r < h; r++)
    for (int c = 0; c < w; c++)
      if (rows[r] & (1 << c)) d.fillRect(x + c * s, y + r * s, s, s);
}

// Preferred overload: dimensions travel with the icon — no magic numbers.
inline void miniIconDraw(DisplayDriver& d, int x, int top_y, const MiniIcon& ic) {
  miniIconDraw(d, x, top_y, ic.rows, ic.w, ic.h);
}

// Draw a mini-icon at an exact top-left (no vertical centring). Used where the
// caller controls placement, e.g. flush to the top/bottom of a scroll track.
inline void miniIconDrawTop(DisplayDriver& d, int x, int y, const MiniIcon& ic) {
  const int s = miniIconScale(d);
  for (int r = 0; r < ic.h; r++)
    for (int c = 0; c < ic.w; c++)
      if (ic.rows[r] & (1 << c)) d.fillRect(x + c * s, y + r * s, s, s);
}

// Draw a mini-icon centred on a point (scaled) — used for map markers, which
// are positioned by their centre rather than a text-line top.
inline void miniIconDrawCentered(DisplayDriver& d, int cx, int cy, const MiniIcon& ic) {
  const int s = miniIconScale(d);
  miniIconDrawTop(d, cx - (ic.w * s) / 2, cy - (ic.h * s) / 2, ic);
}

// Centre a mini-icon inside the slot [x, 0, box_w, box_h] using the current ink
// colour (no background). Centres on the box itself, not the text line, so the
// glyph sits dead-centre regardless of font line height.
inline void drawSlotIcon(DisplayDriver& d, int x, int box_w, int box_h, const MiniIcon& ic) {
  const int s = miniIconScale(d);
  int ix = x + (box_w - ic.w * s) / 2;
  int iy = (box_h - ic.h * s) / 2;
  if (ix < x) ix = x;
  if (iy < 0) iy = 0;
  miniIconDrawTop(d, ix, iy, ic);
}

// Inverted status indicator: fill the box [x, 0, box_w, box_h] LIGHT, draw the
// glyph DARK and centred, then restore ink to LIGHT.
inline void drawBoxedIcon(DisplayDriver& d, int x, int box_w, int box_h, const MiniIcon& ic) {
  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(x, 0, box_w, box_h);
  d.setColor(DisplayDriver::DARK);
  drawSlotIcon(d, x, box_w, box_h, ic);
  d.setColor(DisplayDriver::LIGHT);
}

// Horizontal row of `count` square dots (scaled, vertically centred). Used by
// the "awaiting ACK" marker, where the dot count = number of send attempts.
inline void miniIconDotRow(DisplayDriver& d, int x, int top_y, int count) {
  const int s = miniIconScale(d);
  const int dot = 2 * s, pitch = 3 * s;   // 2px dot + 1px gap, scaled
  int y = top_y + (d.getLineHeight() - dot) / 2;
  if (y < top_y) y = top_y;
  for (int i = 0; i < count; i++) d.fillRect(x + i * pitch, y, dot, dot);
}

// Mini-icon bitmaps (authored on the 1× grid as ASCII-art; see packRow above).
MINI_ICON(ICON_CHECK, 5,   // ✓
  packRow("....#"),
  packRow("...#."),
  packRow("#.#.."),
  packRow(".#..."));
MINI_ICON(ICON_CROSS, 4,   // ✗
  packRow("#..#"),
  packRow(".##."),
  packRow(".##."),
  packRow("#..#"));

// Top-bar status glyphs (replace the single-letter M / B / A indicators).
MINI_ICON(ICON_MUTE, 6,   // speaker + cross (sound off)
  packRow("..#..."),
  packRow(".##..."),
  packRow("####.#"),
  packRow("###.#."),
  packRow("####.#"),
  packRow(".##..."),
  packRow("..#..."));
MINI_ICON(ICON_BLUETOOTH, 5,   // ᛒ bluetooth rune
  packRow("..#.."),
  packRow("..##."),
  packRow("#.#.#"),
  packRow(".###."),
  packRow("#.#.#"),
  packRow("..##."),
  packRow("..#.."));
MINI_ICON(ICON_ADVERT, 6,   // broadcast mast + radiating waves (auto-advert)
  packRow("#....#"),
  packRow(".#..#."),
  packRow("..##.."),
  packRow("..##.."),
  packRow(".####."),
  packRow(".####."));

MINI_ICON(ICON_ALARM, 5,   // bell — an alarm is armed
  packRow("..#.."),
  packRow(".###."),
  packRow(".###."),
  packRow(".###."),
  packRow("#####"),
  packRow("..#.."));

MINI_ICON(ICON_TOMATO, 5,   // tomato (leaf + round body) — a Pomodoro is running/paused
  packRow(".#.#."),
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"),
  packRow("#####"),
  packRow(".###."));

MINI_ICON(ICON_TRAIL, 6,   // map pin / location marker (GPS trail logging)
  packRow(".####."),
  packRow("######"),
  packRow("##..##"),
  packRow("######"),
  packRow(".####."),
  packRow("..##.."));

MINI_ICON(ICON_REPEATER, 6,   // » double chevron — relaying/forwarding (repeater active)
  packRow("#..#.."),
  packRow(".#..#."),
  packRow("..#..#"),
  packRow(".#..#."),
  packRow("#..#.."));

MINI_ICON(ICON_GPS, 5,   // reticle — GPS fix status (boxed = fix, plain = searching)
  packRow(".###."),
  packRow("#...#"),
  packRow("#.#.#"),
  packRow("#...#"),
  packRow(".###."));

// Tools-menu glyphs (auto-reply bot, ringtone editor, diagnostics, system).
MINI_ICON(ICON_BOT, 5,   // robot head: antenna + eyes + grille (auto-reply bot)
  packRow("..#.."),
  packRow("#####"),
  packRow("#.#.#"),
  packRow("#####"),
  packRow("#.#.#"),
  packRow("#####"));
MINI_ICON(ICON_NOTE, 5,   // ♪ quaver — ringtone editor
  packRow("...##"),
  packRow("...##"),
  packRow("...#."),
  packRow("...#."),
  packRow("...#."),
  packRow("####."),
  packRow("####."));
MINI_ICON(ICON_CHART, 5,   // ascending bars — diagnostics / stats
  packRow("....#"),
  packRow("..#.#"),
  packRow("..#.#"),
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow("#####"));
MINI_ICON(ICON_GEAR, 7,   // ⚙ cog with hub hole — system
  packRow("..#.#.."),
  packRow(".#####."),
  packRow("#######"),
  packRow(".##.##."),
  packRow("#######"),
  packRow(".#####."),
  packRow("..#.#.."));

// Home-carousel page glyphs — a uniform 5x5 set, deliberately smaller than the
// menu/status icons above, used in place of the page-indicator dots. One per
// HomePage; see UITask HomeScreen::pageIcon().
MINI_ICON(ICON_PG_CLOCK, 5,      // clock face + hands
  packRow(".###."),
  packRow("#.#.#"),
  packRow("#.###"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_PG_STAR, 5,       // favourites
  packRow("..#.."),
  packRow("#####"),
  packRow(".###."),
  packRow("##.##"),
  packRow("#...#"));
MINI_ICON(ICON_PG_RECENT, 5,     // stacked lines — recent list
  packRow("#####"),
  packRow("....."),
  packRow("#####"),
  packRow("....."),
  packRow("#####"));
MINI_ICON(ICON_PG_RADIO, 5,      // antenna tower — radio params
  packRow("..#.."),
  packRow(".###."),
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"));
MINI_ICON(ICON_PG_BT, 5,         // bluetooth (compact)
  packRow("..#.."),
  packRow("#.##."),
  packRow(".###."),
  packRow("#.##."),
  packRow("..#.."));
MINI_ICON(ICON_PG_ADVERT, 5,     // mast + radiating waves — advert
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."));
MINI_ICON(ICON_PG_GPS, 5,        // location pin — GPS
  packRow(".###."),
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow(".#.#."),
  packRow("..#.."));
MINI_ICON(ICON_PG_SENSORS, 5,    // thermometer/gauge — sensors
  packRow("..#.."),
  packRow(".#.#."),
  packRow(".#.#."),
  packRow(".###."),
  packRow(".###."));
MINI_ICON(ICON_PG_SETTINGS, 5,   // small cog — settings
  packRow(".#.#."),
  packRow("#####"),
  packRow("##.##"),
  packRow("#####"),
  packRow(".#.#."));
MINI_ICON(ICON_PG_MAP, 5,        // folded map — map page
  packRow("#####"),
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow("#####"));
MINI_ICON(ICON_PG_TOOLS, 5,      // wrench (open jaw + handle) — tools
  packRow(".#.#."),
  packRow(".###."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."));
MINI_ICON(ICON_PG_MSG, 5,        // speech bubble — quick messages
  packRow("#####"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#####"),
  packRow(".#..."));
MINI_ICON(ICON_PG_POWER, 5,      // power symbol — shutdown
  packRow("..#.."),
  packRow("#.#.#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));

// Trail-map markers — centred on a point (see miniIconDrawCentered) rather
// than anchored to a text line.
MINI_ICON(ICON_MAP_DOT, 3,        // ● filled trail point
  packRow("###"),
  packRow("###"),
  packRow("###"));
MINI_ICON(ICON_MAP_RING, 3,       // ○ hollow ring — marks a new trail segment start
  packRow("###"),
  packRow("#.#"),
  packRow("###"));
MINI_ICON(ICON_MAP_WAYPOINT, 5,   // ◇ hollow diamond — saved waypoint
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."));
MINI_ICON(ICON_MAP_START, 5,      // + trail start marker
  packRow("..#.."),
  packRow("..#.."),
  packRow("#####"),
  packRow("..#.."),
  packRow("..#.."));
MINI_ICON(ICON_MAP_CURRENT, 5,    // ✕ live position / last trail point
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"));
MINI_ICON(ICON_MAP_CONTACT, 5,    // ◆ filled diamond — a live-tracked contact ([LOC] share)
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"),
  packRow(".###."),
  packRow("..#.."));
MINI_ICON(ICON_MAP_NORTH, 5,      // "N" with a peaked roof — compass north marker
  packRow("..#.."),
  packRow(".###."),
  packRow("#...#"),
  packRow("##..#"),
  packRow("#.#.#"),
  packRow("#..##"),
  packRow("#...#"));
MINI_ICON(ICON_MAP_ARROW, 5,      // → distance-to-nearest-tracked-contact indicator
  packRow("..#.."),
  packRow("...#."),
  packRow("#####"),
  packRow("...#."),
  packRow("..#.."));
MINI_ICON(ICON_MAP_TARGET, 5,     // ⚑ flag on a pole — the active Locator/Nav target
  packRow("####."),
  packRow("#..#."),
  packRow("####."),
  packRow("#...."),
  packRow("#...."));

// Keyboard special-key glyphs.
MINI_ICON(ICON_SHIFT, 7,   // ⇧  caps
  packRow("...#..."),
  packRow("..###.."),
  packRow(".#####."),
  packRow("#######"),
  packRow("..###.."),
  packRow("..###.."),
  packRow("..###.."));
MINI_ICON(ICON_BACKSPACE, 8,   // ⌫  delete-left (× knocked out of the arrow body)
  packRow("...#####"),
  packRow("..######"),
  packRow(".###.#.#"),
  packRow("#####.##"),
  packRow(".###.#.#"),
  packRow("..######"),
  packRow("...#####"));
// Space ⎵ is wider than the 8-px mini-icon limit, so it is two halves drawn
// side by side (offset by ICON_SPACE_L.w * scale): ticks at both far ends + bar.
MINI_ICON(ICON_SPACE_L, 8,
  packRow("#......."),
  packRow("#......."),
  packRow("#......."),
  packRow("########"));
MINI_ICON(ICON_SPACE_R, 8,
  packRow(".......#"),
  packRow(".......#"),
  packRow(".......#"),
  packRow("########"));

// Scroll-indicator caps — small up/down triangles (authored on the 1× grid).
MINI_ICON(ICON_SCROLL_UP, 5,   // ▲
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"));
MINI_ICON(ICON_SCROLL_DOWN, 5, // ▼
  packRow("#####"),
  packRow(".###."),
  packRow("..#.."));

// Width of the right-edge column drawScrollIndicator occupies, or 0 when the
// list fits and no indicator is drawn. Subtract from a row's content width so
// text never runs under the scrollbar. Mirrors the column math below (5*scale
// triangle + 1px gap).
inline int scrollIndicatorColWidth(DisplayDriver& d) {
  return 5 * miniIconScale(d) + 1;            // triangle (5*scale) + 1px gap
}
inline int scrollIndicatorReserve(DisplayDriver& d, int total, int visible) {
  return (total > visible) ? scrollIndicatorColWidth(d) : 0;
}

// Right-edge scroll indicator: a proportional track + thumb topped/tailed with
// up/down triangle mini-icon caps. Drop-in replacement for
// DisplayDriver::drawScrollArrows that also shows how much of the list is on
// screen and where you are within it. Everything lives in a ~5px column at the
// right edge and scales with the font (mini-icon scale).
//   top_y   : y of the first visible row (top of the viewport)
//   track_h : pixel height of the viewport (e.g. visible_rows * row_pitch)
//
// Core takes the proportions in arbitrary units. For uniform-height lists pass
// item counts (see the item wrapper below). For variable-height lists (e.g. the
// portrait DM history, where each message box wraps to a different height) pass
// pixel sums — total_px = Σ all box heights, view_px = pixels currently on
// screen, scroll_px = pixels above the first visible box — so the thumb size and
// position track real content extent instead of jumping as box counts fluctuate.
//   total_px  : total extent of the whole list (items or pixels)
//   view_px   : extent currently visible
//   scroll_px : extent scrolled past (offset of the first visible item)
// right_x : column origin's right edge, in screen pixels (the indicator
//           occupies [right_x - col, right_x)). Full-screen lists pass
//           d.width(); a narrower container (e.g. a centred popup) passes its
//           own right edge so the indicator lands inside the box, not the screen.
inline void drawScrollIndicatorPx(DisplayDriver& d, int right_x, int top_y, int track_h,
                                  long total_px, long view_px, long scroll_px) {
  if (total_px <= view_px || track_h <= 0) return;  // whole list fits — no indicator
  const long span = total_px - view_px;             // > 0 here
  if (scroll_px < 0) scroll_px = 0;
  if (scroll_px > span) scroll_px = span;

  const int s    = miniIconScale(d);
  const int col  = 5 * s;                  // triangle / column width
  const int th   = 3 * s;                  // triangle height
  const int x    = right_x - col;          // indicator column origin

  // Caps are static end-markers, always drawn flush at the very top / bottom of
  // the track; the thumb travels in the fixed band between them. (They mark the
  // track ends, not "more above/below", so they never vanish at the extremes.)
  const int cap = th + 2;                  // triangle height + 2px gap
  const int band_t = top_y + cap;
  const int band_b = top_y + track_h - cap;
  int band = band_b - band_t;
  if (band < s) band = s;

  const int bar_w = 3 * s;                  // odd width → centres exactly in the 5*s column
  const int bar_x = x + (col - bar_w) / 2;

  int thumb_h = (int)((long)band * view_px / total_px);
  if (thumb_h < th) thumb_h = th;
  if (thumb_h > band)  thumb_h = band;
  const int thumb_y = band_t + (int)((long)(band - thumb_h) * scroll_px / span);

  // No halo: every caller already keeps its selection bar clear of this column
  // (reserve/_reserve), so the markers never need to fight a LIGHT background
  // for contrast — and a halo on the bottom arrow had no symmetric clip like
  // the top one, so it bled 1px into the container's bottom border.
  d.setColor(DisplayDriver::LIGHT);
  d.fillRect(bar_x, thumb_y, bar_w, thumb_h);

  miniIconDrawTop(d, x, top_y, ICON_SCROLL_UP);
  miniIconDrawTop(d, x, top_y + track_h - th, ICON_SCROLL_DOWN);
}

// Convenience overload anchored to the screen's right edge — the common case
// for full-width lists.
inline void drawScrollIndicatorPx(DisplayDriver& d, int top_y, int track_h,
                                  long total_px, long view_px, long scroll_px) {
  drawScrollIndicatorPx(d, d.width(), top_y, track_h, total_px, view_px, scroll_px);
}

// Uniform-height wrapper: one item = one unit. Drop-in replacement for
// DisplayDriver::drawScrollArrows.
//   total   : total number of items
//   visible : items shown at once
//   first   : index of the first visible item (scroll offset)
inline void drawScrollIndicator(DisplayDriver& d, int top_y, int track_h,
                                int total, int visible, int first) {
  drawScrollIndicatorPx(d, top_y, track_h, total, visible, first);
}

// right_x variant — see drawScrollIndicatorPx's right_x for why a narrower
// container (e.g. a popup) needs this instead of the screen-edge default.
inline void drawScrollIndicator(DisplayDriver& d, int right_x, int top_y, int track_h,
                                int total, int visible, int first) {
  drawScrollIndicatorPx(d, right_x, top_y, track_h, total, visible, first);
}

// Scrollable item-list skeleton shared by the list screens. Computes the visible
// window from the font metrics, keeps `sel` in view, reserves the scroll-column,
// draws each visible row through `row`, then the indicator. The callback
// `row(idx, y, sel, reserve)` draws one item — including its own selection bar —
// using `reserve` to keep right-aligned content clear of the indicator. Returns
// the visible row count (callers cache it for input handling).
template <class RenderRow>
inline int drawList(DisplayDriver& d, int total, int sel, int& scroll, RenderRow row) {
  const int item_h  = d.lineStep();
  const int start_y = d.listStart();
  int visible = d.listVisible(item_h);
  if (visible < 1) visible = 1;
  if (sel < scroll)            scroll = sel;
  if (sel >= scroll + visible) scroll = sel - visible + 1;
  if (scroll < 0) scroll = 0;
  const int reserve = scrollIndicatorReserve(d, total, visible);
  for (int i = 0; i < visible && (scroll + i) < total; i++)
    row(scroll + i, start_y + i * item_h, scroll + i == sel, reserve);
  drawScrollIndicator(d, start_y, visible * item_h, total, visible, scroll);
  return visible;
}

// Canonical selection bar for a drawList() row: spans the row width minus the
// scroll-indicator `reserve`, one pixel short of the row height, anchored one
// pixel above `y` (the row's text baseline-top). Call as the first line of a
// row callback, then draw content over it. Captures the geometry every list row
// repeated by hand; rows that intentionally differ (full-width, custom height)
// still call display.drawSelectionRow() directly.
inline void drawRowSelection(DisplayDriver& d, int y, bool sel, int reserve) {
  d.drawSelectionRow(0, y - 1, d.width() - reserve, d.lineStep() - 1, sel);
}

// ── Big ASCII-art icons (skeleton, not yet used) ─────────────────────────────
// Same authoring idea as the mini-icons but for full page glyphs up to 32 px
// wide: one uint32_t per row. The existing XBM bitmaps below (logo/bluetooth/…)
// stay hand-encoded; reach for this when adding a *new* big icon so it's
// readable in source. Drawn at 1× (page icons aren't font-scaled).
//
// To add one:
//   BIG_ICON(MY_ICON, 16,
//     packRow32("......####......"),
//     packRow32(".....######....."),
//     ...);
//   bigIconDraw(display, x, y, MY_ICON);
constexpr uint32_t packBit32(const char* s, int x) {
  return (s[x] && s[x] != ' ' && s[x] != '.') ? (1u << x) : 0u;
}
constexpr uint32_t packRow32(const char* s, int x = 0) {
  return (!s[x] || x >= 32) ? 0u : (packBit32(s, x) | packRow32(s, x + 1));
}

struct BigIcon { uint8_t w, h; const uint32_t* rows; };

#define BIG_ICON(name, width, ...)                                             \
  static constexpr uint32_t name##_rows[] = { __VA_ARGS__ };                   \
  static constexpr BigIcon name = {                                            \
      (uint8_t)(width),                                                        \
      (uint8_t)(sizeof(name##_rows) / sizeof(uint32_t)), name##_rows }

// Draw a packed big icon at 1× with the current ink colour, top-left at (x, y).
inline void bigIconDraw(DisplayDriver& d, int x, int y, const BigIcon& ic) {
  for (int r = 0; r < ic.h; r++)
    for (int c = 0; c < ic.w; c++)
      if (ic.rows[r] & (1u << c)) d.fillRect(x + c, y + r, 1, 1);
}

// 'meshcore', 128x13px
static const uint8_t meshcore_logo [] = {
    0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 
    0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe, 
    0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc, 
    0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00, 
    0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00, 
    0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8, 
    0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0, 
    0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00, 
    0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00, 
    0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8, 
    0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8, 
};

static const uint8_t bluetooth_on[] = {
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x30, 0x00, 0x00, 
  0x00, 0x3C, 0x00, 0x00, 
  0x00, 0x3E, 0x00, 0x00, 
  0x00, 0x3F, 0x80, 0x00, 
  0x00, 0x3F, 0xC0, 0x00, 
  0x00, 0x3B, 0xE0, 0x00, 
  0x30, 0x38, 0xF8, 0x00, 
  0x3C, 0x38, 0x7C, 0x00, 
  0x3E, 0x38, 0x7C, 0x00, 
  0x1F, 0xB8, 0xF8, 0x70, 
  0x07, 0xF9, 0xF0, 0x78, 
  0x03, 0xFF, 0xC0, 0x78, 
  0x00, 0xFF, 0x80, 0x3C, 
  0x00, 0x7F, 0x07, 0x1C, 
  0x00, 0x7E, 0x07, 0x1C, 
  0x03, 0xFF, 0x82, 0x1C, 
  0x03, 0xFF, 0xC0, 0x78, 
  0x07, 0xFB, 0xE0, 0x78, 
  0x0F, 0xB8, 0xF8, 0x70, 
  0x3E, 0x38, 0x7C, 0x00, 
  0x3C, 0x38, 0x7C, 0x00, 
  0x38, 0x38, 0xF8, 0x00, 
  0x00, 0x39, 0xF0, 0x00, 
  0x00, 0x3F, 0xC0, 0x00, 
  0x00, 0x3F, 0x80, 0x00, 
  0x00, 0x3E, 0x00, 0x00, 
  0x00, 0x3C, 0x00, 0x00, 
  0x00, 0x38, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
};

static const uint8_t bluetooth_off[] = {
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x03, 0x80, 0x00, 
  0x00, 0x03, 0xC0, 0x00, 
  0x00, 0x03, 0xE0, 0x00, 
  0x38, 0x03, 0xF8, 0x00, 
  0x3C, 0x03, 0xFC, 0x00, 
  0x3E, 0x03, 0xBF, 0x00, 
  0x0F, 0x83, 0x8F, 0x80, 
  0x07, 0xC3, 0x87, 0xC0, 
  0x03, 0xF0, 0x03, 0xC0, 
  0x00, 0xF8, 0x0F, 0x80, 
  0x00, 0x7C, 0x0F, 0x00, 
  0x00, 0x1F, 0x0E, 0x00, 
  0x00, 0x0F, 0x80, 0x00, 
  0x00, 0x07, 0xE0, 0x00, 
  0x00, 0x07, 0xF0, 0x00, 
  0x00, 0x0F, 0xF8, 0x00, 
  0x00, 0x3F, 0xBE, 0x00, 
  0x00, 0x7F, 0x9F, 0x00, 
  0x00, 0xFB, 0x8F, 0xC0, 
  0x03, 0xE3, 0x83, 0xE0, 
  0x03, 0xC3, 0x87, 0xF0, 
  0x03, 0x83, 0x8F, 0xFC, 
  0x00, 0x03, 0xBF, 0x3C, 
  0x00, 0x03, 0xFC, 0x1C, 
  0x00, 0x03, 0xF8, 0x00, 
  0x00, 0x03, 0xE0, 0x00, 
  0x00, 0x03, 0xC0, 0x00, 
  0x00, 0x03, 0x80, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
  0x00, 0x00, 0x00, 0x00, 
};

static const uint8_t power_icon[] = {
    0x00, 0x01, 0x80, 0x00, 0x00, 0x03, 0xC0, 0x00, 0x00, 0x03, 0xC0, 0x00,
    0x00, 0x33, 0xCC, 0x00, 0x00, 0xF3, 0xCF, 0x00, 0x01, 0xF3, 0xCF, 0x80,
    0x03, 0xF3, 0xCF, 0xC0, 0x07, 0xF3, 0xCF, 0xE0, 0x0F, 0xE3, 0xC7, 0xF0,
    0x1F, 0xC3, 0xC3, 0xF8, 0x1F, 0x83, 0xC1, 0xF8, 0x3F, 0x03, 0xC0, 0xFC,
    0x3E, 0x03, 0xC0, 0x7C, 0x3E, 0x03, 0xC0, 0x7C, 0x7E, 0x01, 0x80, 0x7E,
    0x7C, 0x00, 0x00, 0x3E, 0x7C, 0x00, 0x00, 0x3E, 0x7C, 0x00, 0x00, 0x3E,
    0x7C, 0x00, 0x00, 0x3E, 0x7C, 0x00, 0x00, 0x3E, 0x3E, 0x00, 0x00, 0x7C,
    0x3E, 0x00, 0x00, 0x7C, 0x3F, 0x00, 0x00, 0xFC, 0x1F, 0x80, 0x01, 0xF8,
    0x1F, 0xC0, 0x03, 0xF8, 0x0F, 0xE0, 0x07, 0xF0, 0x0F, 0xF8, 0x1F, 0xF0,
    0x07, 0xFF, 0xFF, 0xE0, 0x03, 0xFF, 0xFF, 0xC0, 0x00, 0xFF, 0xFF, 0x00,
    0x00, 0x3F, 0xFC, 0x00, 0x00, 0x0F, 0xF0, 0x00,
};

static const uint8_t advert_icon[] = {
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x0C, 0x00, 0x00, 0x30,
0x1C, 0x00, 0x00, 0x38, 0x18, 0x00, 0x00, 0x18, 0x30, 0x00, 0x00, 0x0C,
0x30, 0x60, 0x06, 0x0C, 0x60, 0xE0, 0x07, 0x06, 0x61, 0xC0, 0x03, 0x86,
0xE1, 0x81, 0x81, 0x87, 0xC3, 0x07, 0xE0, 0xC3, 0xC3, 0x0F, 0xF0, 0xC3,
0xC3, 0x0F, 0xF0, 0xC3, 0xC3, 0x0F, 0xF0, 0xC3, 0xC3, 0x0F, 0xF0, 0xC3,
0xC3, 0x07, 0xE0, 0xC3, 0xC1, 0x83, 0xC1, 0x83, 0x61, 0x80, 0x01, 0x86,
0x60, 0xC0, 0x03, 0x06, 0x70, 0xE0, 0x07, 0x0E, 0x30, 0x40, 0x02, 0x0C,
0x38, 0x00, 0x00, 0x1C, 0x18, 0x00, 0x00, 0x18, 0x0C, 0x00, 0x00, 0x30,
0x04, 0x00, 0x00, 0x20, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
