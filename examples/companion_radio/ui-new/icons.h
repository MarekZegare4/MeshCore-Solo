#pragma once

#include <stdint.h>
#include <math.h>
#include <helpers/ui/DisplayDriver.h>

// ── Scalable mini-icons ──────────────────────────────────────────────────────
// Small procedural glyphs (delivery markers, etc.) authored on a 1× pixel grid
// and scaled to the current font, so they stay legible on large-font layouts
// (e.g. landscape e-ink renders text at 2×). Distinct from the big page icons
// further down, which are fixed-size 32 px glyphs drawn via bigIconDraw().
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

// Tiny 3×5 digits — for a small count that needs to sit in an icon-sized slot
// (e.g. next to ICON_CHECK) where the normal font is too tall to fit. See
// miniIconDrawNumber/miniIconNumberWidth below.
MINI_ICON(ICON_DIGIT_0, 3,
  packRow("###"), 
  packRow("#.#"), 
  packRow("#.#"), 
  packRow("#.#"),
  packRow("###"));
MINI_ICON(ICON_DIGIT_1, 3,
  packRow(".#."), 
  packRow("##."), 
  packRow(".#."), 
  packRow(".#."), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_2, 3,
  packRow("###"), 
  packRow("..#"), 
  packRow("###"), 
  packRow("#.."), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_3, 3,
  packRow("###"), 
  packRow("..#"), 
  packRow("###"), 
  packRow("..#"), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_4, 3,
  packRow("#.#"), 
  packRow("#.#"), 
  packRow("###"), 
  packRow("..#"), 
  packRow("..#"));
MINI_ICON(ICON_DIGIT_5, 3,
  packRow("###"), 
  packRow("#.."), 
  packRow("###"), 
  packRow("..#"), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_6, 3,
  packRow("###"), 
  packRow("#.."), 
  packRow("###"), 
  packRow("#.#"), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_7, 3,
  packRow("###"), 
  packRow("..#"), 
  packRow("..#"), 
  packRow("..#"), 
  packRow("..#"));
MINI_ICON(ICON_DIGIT_8, 3,
  packRow("###"), 
  packRow("#.#"), 
  packRow("###"), 
  packRow("#.#"), 
  packRow("###"));
MINI_ICON(ICON_DIGIT_9, 3,
  packRow("###"), 
  packRow("#.#"), 
  packRow("###"), 
  packRow("..#"), 
  packRow("###"));

static constexpr const MiniIcon* MINI_ICON_DIGITS[10] = {
  &ICON_DIGIT_0, &ICON_DIGIT_1, &ICON_DIGIT_2, &ICON_DIGIT_3, &ICON_DIGIT_4,
  &ICON_DIGIT_5, &ICON_DIGIT_6, &ICON_DIGIT_7, &ICON_DIGIT_8, &ICON_DIGIT_9,
};

// Width a count would occupy via miniIconDrawNumber (digit width + 1px gap
// between digits, scaled) — needed up front to size a header around it.
// Clamped to 2 digits (0-99): callers showing a repeater/echo count never see
// more than MAX_HIST_PATH_BYTES distinct hashes anyway (16 max).
inline int miniIconNumberWidth(DisplayDriver& d, int n) {
  const int s = miniIconScale(d);
  int digits = (n >= 10) ? 2 : 1;
  return digits * 3 * s + (digits - 1) * s;
}

// Draws `n` (clamped to 0-99) as a left-to-right run of tiny digit icons —
// e.g. a repeater/echo count too small a slot for the normal font to fit
// legibly. Vertically centred in the text line the same way miniIconDraw is.
inline void miniIconDrawNumber(DisplayDriver& d, int x, int top_y, int n) {
  const int s = miniIconScale(d);
  if (n < 0) n = 0;
  if (n > 99) n = 99;
  if (n >= 10) { miniIconDraw(d, x, top_y, *MINI_ICON_DIGITS[n / 10]); x += 3 * s + s; }
  miniIconDraw(d, x, top_y, *MINI_ICON_DIGITS[n % 10]);
}

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
MINI_ICON(ICON_KEY, 5,   // padlock — remote admin (privileged/password-gated access)
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#####"),
  packRow("##.##"),
  packRow("#####"));
MINI_ICON(ICON_PINS, 5,   // 3-pin header — GPIO
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow("#####"));

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
  packRow(".#.#."),
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
MINI_ICON(ICON_KEYBOARD, 7,   // PIN keyboard to ABC keyboard switch icon
  packRow("#.#.#.#"),
  packRow("#######"),
  packRow("#.#.#.#"),
  packRow("#######"));
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
  return 3 * miniIconScale(d) + 2;            // thumb (3*scale) + 2px gap
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

  // A dotted 1 px track down the middle of a 3*scale column and a soft-cornered
  // thumb sized to the share of the list on screen. The column is narrower
  // than the old arrow-capped one, so rows keep 2 px more for text.
  const int s     = miniIconScale(d);
  const int bar_w = 3 * s;
  const int x     = right_x - bar_w;
  const int mid   = x + bar_w / 2;

  int thumb_h = (int)((long)track_h * view_px / total_px);
  if (thumb_h < 4 * s) thumb_h = 4 * s;
  if (thumb_h > track_h) thumb_h = track_h;
  const int thumb_y = top_y + (int)((long)(track_h - thumb_h) * scroll_px / span);

  d.setColor(DisplayDriver::LIGHT);
  for (int y = top_y; y < top_y + track_h; y += 2)
    if (y < thumb_y - 1 || y > thumb_y + thumb_h) d.fillRect(mid, y, 1, 1);
  d.fillSoftRect(x, thumb_y, bar_w, thumb_h);
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

// Signal strength as four rising bars, from a LoRa SNR in quarter-dB (the
// unit packets carry). Bars that aren't lit leave a 1 px stub so the scale
// stays readable. Bottom-aligned to a text row at y; right edge at x_right.
// Returns the width used. Draws in the current ink.
inline int signalBarsFromSnr(int snr_x4) {
  int snr = snr_x4 / 4;
  return snr >= 5 ? 4 : snr >= 0 ? 3 : snr >= -7 ? 2 : snr >= -13 ? 1 : 0;
}
inline int signalBarsWidth(DisplayDriver& d) { int s = miniIconScale(d); return 4 * 2 * s + 3 * s; }
inline int drawSignalBars(DisplayDriver& d, int x_right, int y, int snr_x4) {
  const int s = miniIconScale(d);
  const int bw = 2 * s, gap = s, w = signalBarsWidth(d);
  const int base = y + d.getLineHeight() - 2;   // sits on the text baseline
  const int lit = signalBarsFromSnr(snr_x4);
  int x = x_right - w;
  for (int i = 0; i < 4; i++) {
    int h = (i + 1) * 2 * s;
    if (i < lit) d.fillRect(x, base - h + 1, bw, h);
    else         d.fillRect(x, base, bw, s);
    x += bw + gap;
  }
  return w;
}

// Three dots rising in turn, each a beat after the last: the "working on it"
// mark shared by the boot splash and every screen waiting on the radio or GPS.
// Centred on cx with their resting bottom at y_bottom. Returns the redraw
// delay the animation needs; e-ink draws them still and asks for none sooner.
inline int drawLoadingDots(DisplayDriver& d, int cx, int y_bottom) {
  const int s = miniIconScale(d);
  const int dot = 3 * s, pitch = 9 * s, lift_max = 3 * s;
  const bool anim = !d.isEink();
  const unsigned long t = millis();
  d.setColor(DisplayDriver::LIGHT);
  for (int i = 0; i < 3; i++) {
    int lift = 0;
    if (anim) {
      long ph = (long)((t + 940UL - 140UL * i) % 940UL);
      if (ph < 260)      lift = (int)(lift_max * ph / 260);
      else if (ph < 520) lift = (int)(lift_max * (520 - ph) / 260);
    }
    d.fillRect(cx + (i - 1) * pitch - dot / 2, y_bottom - dot - lift, dot, dot);
  }
  return anim ? 40 : 1000;
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

// ── Big ASCII-art icons ─────────────────────────────
// Same authoring idea as the mini-icons but for full page glyphs up to 32 px
// wide: one uint32_t per row, readable in source. Drawn at 1× (page icons
// aren't font-scaled).
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
// `dim` draws only every other pixel (50% checker): the icon's "off" state.
inline void bigIconDraw(DisplayDriver& d, int x, int y, const BigIcon& ic, bool dim = false) {
  for (int r = 0; r < ic.h; r++) {
    if (dim) {
      for (int c = (r + x + y) & 1; c < ic.w; c += 2)
        if (ic.rows[r] & (1u << c)) d.fillRect(x + c, y + r, 1, 1);
      continue;
    }
    for (int c = 0; c < ic.w; ) {          // runs of set bits as one rect
      if (!(ic.rows[r] & (1u << c))) { c++; continue; }
      int e = c;
      while (e + 1 < ic.w && (ic.rows[r] & (1u << (e + 1)))) e++;
      d.fillRect(x + c, y + r, e - c + 1, 1);
      c = e + 1;
    }
  }
}

// Home page icons, 32x32, one family: 3 px strokes, rounded ends. Drawn
// through drawHoverIcon() below. Rasterised from vector shapes, so keep any
// redraw to the same stroke width rather than touching single pixels.
// Bluetooth rune
BIG_ICON(BIG_BLUETOOTH, 32,
  packRow32("................................"),
  packRow32("................................"),
  packRow32("..............##................"),
  packRow32(".............####..............."),
  packRow32(".............#####.............."),
  packRow32(".............######............."),
  packRow32(".............#######............"),
  packRow32(".............#########.........."),
  packRow32(".............####.#####........."),
  packRow32("........##...####..####........."),
  packRow32(".......####..####..####........."),
  packRow32("........####.####.#####........."),
  packRow32(".........############..........."),
  packRow32("..........##########............"),
  packRow32("...........########............."),
  packRow32("............######.............."),
  packRow32("............######.............."),
  packRow32("...........########............."),
  packRow32("..........##########............"),
  packRow32(".........############..........."),
  packRow32("........####.####.#####........."),
  packRow32(".......####..####..####........."),
  packRow32("........##...####..####........."),
  packRow32(".............####.#####........."),
  packRow32(".............#########.........."),
  packRow32(".............#######............"),
  packRow32(".............######............."),
  packRow32(".............#####.............."),
  packRow32(".............####..............."),
  packRow32("..............##................"),
  packRow32("................................"),
  packRow32("................................"));
// advert: a dot between two pairs of arcs
BIG_ICON(BIG_ADVERT, 32,
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("....#......................#...."),
  packRow32("...####..................####..."),
  packRow32("...###....................###..."),
  packRow32("..###......................###.."),
  packRow32("..###...##............##...###.."),
  packRow32(".###...####..........####...###."),
  packRow32(".###...###.....##.....###...###."),
  packRow32(".###..###....######....###..###."),
  packRow32(".##...###....######....###...##."),
  packRow32(".##...###...########...###...##."),
  packRow32(".##...###...########...###...##."),
  packRow32(".##...###....######....###...##."),
  packRow32(".###..###....######....###..###."),
  packRow32(".###...###.....##.....###...###."),
  packRow32(".###...####..........####...###."),
  packRow32("..###...##............##...###.."),
  packRow32("..###......................###.."),
  packRow32("...###....................###..."),
  packRow32("...####..................####..."),
  packRow32("....#......................#...."),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"));
// power (Hibernate)
BIG_ICON(BIG_POWER, 32,
  packRow32("................................"),
  packRow32("................................"),
  packRow32("...............##..............."),
  packRow32("..............####.............."),
  packRow32("..............####.............."),
  packRow32("..............####.............."),
  packRow32("..............####.............."),
  packRow32("..............####.............."),
  packRow32(".......##.....####.....##......."),
  packRow32("......####....####....####......"),
  packRow32(".....####.....####.....####....."),
  packRow32("....####......####......####...."),
  packRow32("....###.......####.......###...."),
  packRow32("....###.......####.......###...."),
  packRow32("...###........####........###..."),
  packRow32("...###.........##.........###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("....###..................###...."),
  packRow32("....###..................###...."),
  packRow32("....####................####...."),
  packRow32(".....####..............####....."),
  packRow32("......####............####......"),
  packRow32(".......#####........#####......."),
  packRow32("........################........"),
  packRow32(".........##############........."),
  packRow32("...........##########..........."),
  packRow32("................................"),
  packRow32("................................"));
// gear
BIG_ICON(BIG_SETTINGS, 32,
  packRow32("................................"),
  packRow32(".............######............."),
  packRow32(".............######............."),
  packRow32(".............######............."),
  packRow32(".......##....######....##......."),
  packRow32("......####...######...####......"),
  packRow32(".....######################....."),
  packRow32("....########################...."),
  packRow32("....########################...."),
  packRow32(".....######################....."),
  packRow32("......####################......"),
  packRow32("......#######......#######......"),
  packRow32("......######........######......"),
  packRow32(".##########..........##########."),
  packRow32(".##########..........##########."),
  packRow32(".##########..........##########."),
  packRow32(".##########..........##########."),
  packRow32(".##########..........##########."),
  packRow32(".##########..........##########."),
  packRow32("......######........######......"),
  packRow32("......#######......#######......"),
  packRow32("......####################......"),
  packRow32(".....######################....."),
  packRow32("....########################...."),
  packRow32("....########################...."),
  packRow32(".....######################....."),
  packRow32("......####...######...####......"),
  packRow32(".......##....######....##......."),
  packRow32(".............######............."),
  packRow32(".............######............."),
  packRow32(".............######............."),
  packRow32("................................"));
// wrench
BIG_ICON(BIG_TOOLS, 32,
  packRow32("................................"),
  packRow32("................................"),
  packRow32(".....................##........."),
  packRow32("..................#######......."),
  packRow32(".................#######........"),
  packRow32("................#######........."),
  packRow32("...............#######.........."),
  packRow32("...............######.......#..."),
  packRow32("...............#####.......##..."),
  packRow32("..............#######.....####.."),
  packRow32("..............########...#####.."),
  packRow32("...............########.#####..."),
  packRow32("...............##############..."),
  packRow32("...............##############..."),
  packRow32("..............##############...."),
  packRow32(".............##############....."),
  packRow32("............##############......"),
  packRow32("...........#######...##........."),
  packRow32("..........#######..............."),
  packRow32(".........#######................"),
  packRow32("........#######................."),
  packRow32(".......#######.................."),
  packRow32("......#######..................."),
  packRow32(".....#######...................."),
  packRow32("....#######....................."),
  packRow32("....######......................"),
  packRow32("....#####......................."),
  packRow32(".....###........................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"));
// chat bubble with the loading-dot trio in it
BIG_ICON(BIG_MESSAGES, 32,
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("......####################......"),
  packRow32("....########################...."),
  packRow32("....########################...."),
  packRow32("...####..................####..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###..####..####..####..###..."),
  packRow32("...###..####..####..####..###..."),
  packRow32("...###..####..####..####..###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...###....................###..."),
  packRow32("...####..................####..."),
  packRow32("....########################...."),
  packRow32("....########################...."),
  packRow32("......####################......"),
  packRow32("........#####..................."),
  packRow32("........####...................."),
  packRow32("........###....................."),
  packRow32("........##......................"),
  packRow32("........#......................."),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"),
  packRow32("................................"));

// A Home page icon hovering over its spot: it bobs up to 2 px on a slow
// cosine while a dithered blob below it, its shadow, shrinks as it rises and
// spreads as it sinks. Centred on cx in the 32 px box at y: the glyphs leave
// a row or two blank at the top, so the icon rests 1 px high and the shadow
// fits in the rows the old static icon left above its title. Put the title at
// y + h + HOVER_GAP. E-ink gets the resting frame. Returns ms until the next
// frame is due.
constexpr int HOVER_GAP = 3;
// Where the bob is now: 0 = resting, 1 = top. Anything riding on the icon (a
// badge) lifts by hoverLift() so it moves with it.
inline float hoverPhase(DisplayDriver& d) {
  if (d.isEink()) return 0;
  const unsigned long period = 2400;
  return (1.0f - cosf(6.2831853f * (float)(millis() % period) / period)) * 0.5f;
}
inline int hoverLift(DisplayDriver& d) { return 1 + (int)(2 * hoverPhase(d) + 0.5f); }
inline int drawHoverIcon(DisplayDriver& d, int cx, int y, const BigIcon& ic, bool dim = false) {
  const bool anim = !d.isEink();
  const float up = hoverPhase(d);
  d.setColor(DisplayDriver::LIGHT);
  bigIconDraw(d, cx - ic.w / 2, y - 1 - (int)(2 * up + 0.5f), ic, dim);
  // Shadow: three rows, a checker oval with a solid core that fades as it rises.
  const int w = (int)(28 - 10 * up + 0.5f), core = (int)(w * 0.5f * (1 - up) + 0.5f);
  const int sy = y + ic.h - 1;
  for (int r = 0; r < 3; r++) {
    const int rw = r == 1 ? w : w - 8, x0 = cx - rw / 2;
    for (int x = x0; x < x0 + rw; x++)
      if (((x + sy + r) & 1) == 0 || (r == 1 && 2 * (x - cx) + 1 < core && 2 * (cx - x) - 1 < core))
        d.fillRect(x, sy + r, 1, 1);
  }
  return anim ? 60 : 1000;
}

// Favourite marker for a list row, on every screen that lists something
// starrable. Reuses the Favourites page's own icon so the two read as one idea.
// Width includes the gap that separates it from the text to its left.
inline int favStarWidth(DisplayDriver& d) {
  return ICON_PG_STAR.w * miniIconScale(d) + 2;
}
inline void drawFavStar(DisplayDriver& d, int x, int top_y) {
  miniIconDraw(d, x, top_y, ICON_PG_STAR);
}

// Multi-select toggle glyph -- an outlined box, filled solid when `on`, same
// visual language as SettingsScreen's volume/brightness renderBar() (a
// fillable square) rather than a "[x]"/"[ ]" text glyph that competes with
// the row's own label for width. Used by PopupMenu's checklist rows and any
// other multi-select list.
inline int checkboxWidth(DisplayDriver& d) {
  return d.getLineHeight() - 2;
}
inline void drawCheckbox(DisplayDriver& d, int x, int y, bool on) {
  int box = checkboxWidth(d);
  d.drawRect(x, y, box, box);
  if (on) d.fillRect(x + 2, y + 2, box - 4, box - 4);
}
