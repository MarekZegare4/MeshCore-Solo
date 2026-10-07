#pragma once

#include <stdint.h>
#include <math.h>
#include <helpers/ui/DisplayDriver.h>

// ── Scalable mini-icons ──────────────────────────────────────────────────────
// Small procedural glyphs (delivery markers, etc.) authored on a 1× pixel grid
// and scaled to the current font, so they stay legible on large-font layouts
// (e.g. landscape e-ink renders text at 2×).
//
// To add a mini-icon:
//   1. Draw it as ASCII-art rows, one string per row (width ≤ 16): any char
//      other than ' ' or '.' is a filled pixel. packRow() packs each string
//      into a 16-bit word at compile time, so the strings never reach flash —
//      the binary holds only the packed words, identical to hand-written hex.
//   2. Wrap the rows in a MINI_ICON(name, width, ...) — width and row count are
//      bundled with the data, so the draw site carries no magic numbers.
//   3. Draw it with miniIconDraw(display, x, topY, name).
// Scaling is automatic — no per-display handling needed.
//
// packRow() is written as a single-expression recursion so it stays a valid
// constant expression on any C++ standard the firmware targets (≥ C++11), not
// just the relaxed C++14 constexpr with loops. It packs up to the string's NUL
// (capped at 16 cols), so width lives once in MINI_ICON, not in every row.
constexpr uint16_t packBit(const char* s, int x) {
  return (s[x] && s[x] != ' ' && s[x] != '.') ? (uint16_t)(1u << x) : 0;
}
constexpr uint16_t packRow(const char* s, int x = 0) {
  return (!s[x] || x >= 16) ? 0 : (uint16_t)(packBit(s, x) | packRow(s, x + 1));
}

// A mini-icon bundles its pixel data with its dimensions, so call sites can't
// pass a stale width/height. Built via the MINI_ICON macro below.
struct MiniIcon { uint8_t w, h; const uint16_t* rows; };

// Define a mini-icon: the row count (height) is derived from the initializer,
// the width is stated once. Emits a packed word array plus a MiniIcon view.
#define MINI_ICON(name, width, ...)                                            \
  static constexpr uint16_t name##_rows[] = { __VA_ARGS__ };                   \
  static constexpr MiniIcon name = { (uint8_t)(width),                         \
      (uint8_t)(sizeof(name##_rows) / sizeof(name##_rows[0])), name##_rows }

// The landscape e-ink build writes in the 8x13, whose digits stand 9 px:
// there the status, page and check glyphs come in a 7 px set instead of 5.
// The 4.2" build's 9x15 (digits 10 px) takes a 9 px set: MINI_ICONS_LARGE 2.
#if defined(EINK_LARGE_FONT) && EINK_LARGE_FONT
  #define MINI_ICONS_LARGE EINK_LARGE_FONT
#else
  #define MINI_ICONS_LARGE 0
#endif
static constexpr int PAGE_ICON_PX = MINI_ICONS_LARGE == 2 ? 9 : MINI_ICONS_LARGE ? 7 : 5;   // the page glyphs' box

// Pixel scale from the font: 1× on an 8px OLED line, 2× on a 16px landscape
// e-ink line, etc. Bitmaps are authored on the 1× grid.
inline int miniIconScale(DisplayDriver& d) {
  int s = d.getLineHeight() / 8;
  return s < 1 ? 1 : s;
}

// Draw a w×h (w ≤ 16) bitmap with the current ink colour, scaled by the font and
// vertically centred in the text line that starts at top_y.
inline void miniIconDraw(DisplayDriver& d, int x, int top_y,
                         const uint16_t* rows, int w, int h) {
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

// Horizontal row of `count` square dots (scaled, vertically centred). Used by
// the "awaiting ACK" marker, where the dot count = number of send attempts.
// The dots are 3 px beside the larger icon sets.
static constexpr int DOT_ROW_PX = MINI_ICONS_LARGE ? 3 : 2;
inline int miniIconDotRowWidth(DisplayDriver& d, int count) { return count * (DOT_ROW_PX + 1) * miniIconScale(d); }
inline void miniIconDotRow(DisplayDriver& d, int x, int top_y, int count) {
  const int s = miniIconScale(d);
  const int dot = DOT_ROW_PX * s, pitch = (DOT_ROW_PX + 1) * s;   // dot + 1px gap, scaled
  int y = top_y + (d.getLineHeight() - dot) / 2;
  if (y < top_y) y = top_y;
  for (int i = 0; i < count; i++) d.fillRect(x + i * pitch, y, dot, dot);
}

// Mini-icon bitmaps (authored on the 1× grid as ASCII-art; see packRow above).
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_CHECK, 9,   // ✓
  packRow("........#"),
  packRow(".......#."),
  packRow("#.....#.."),
  packRow(".#...#..."),
  packRow("..#.#...."),
  packRow("...#....."));
MINI_ICON(ICON_CROSS, 7,   // ✗
  packRow("#.....#"),
  packRow(".#...#."),
  packRow("..#.#.."),
  packRow("...#..."),
  packRow("..#.#.."),
  packRow(".#...#."),
  packRow("#.....#"));
#elif MINI_ICONS_LARGE
MINI_ICON(ICON_CHECK, 7,   // ✓
  packRow("......#"),
  packRow(".....#."),
  packRow("#...#.."),
  packRow(".#.#..."),
  packRow("..#...."));
MINI_ICON(ICON_CROSS, 5,   // ✗
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"));
#else
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
#endif

// Tiny 3×5 digits (5x7 and 5x10 in the large sets) — for a small count that needs to sit in an icon-sized slot
// (e.g. next to ICON_CHECK) where the normal font is too tall to fit. See
// miniIconDrawNumber/miniIconNumberWidth below.
#if MINI_ICONS_LARGE == 2   // 5x10, as tall as the 9x15's own digits
MINI_ICON(ICON_DIGIT_0, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_1, 5,
  packRow("..#.."),
  packRow(".##.."),
  packRow("#.#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("#####"));
MINI_ICON(ICON_DIGIT_2, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("....#"),
  packRow("....#"),
  packRow("...#."),
  packRow("..#.."),
  packRow(".#..."),
  packRow("#...."),
  packRow("#...."),
  packRow("#####"));
MINI_ICON(ICON_DIGIT_3, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("....#"),
  packRow("....#"),
  packRow("..##."),
  packRow("....#"),
  packRow("....#"),
  packRow("....#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_4, 5,
  packRow("...#."),
  packRow("..##."),
  packRow(".#.#."),
  packRow("#..#."),
  packRow("#..#."),
  packRow("#..#."),
  packRow("#####"),
  packRow("...#."),
  packRow("...#."),
  packRow("...#."));
MINI_ICON(ICON_DIGIT_5, 5,
  packRow("#####"),
  packRow("#...."),
  packRow("#...."),
  packRow("#...."),
  packRow("####."),
  packRow("....#"),
  packRow("....#"),
  packRow("....#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_6, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...."),
  packRow("#...."),
  packRow("####."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_7, 5,
  packRow("#####"),
  packRow("....#"),
  packRow("....#"),
  packRow("...#."),
  packRow("...#."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."));
MINI_ICON(ICON_DIGIT_8, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_9, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".####"),
  packRow("....#"),
  packRow("....#"),
  packRow("#...#"),
  packRow(".###."));
#elif MINI_ICONS_LARGE   // 5x7 beside the 7 px ones
MINI_ICON(ICON_DIGIT_0, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_1, 5,
  packRow("..#.."),
  packRow(".##.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow("..#.."),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_2, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("....#"),
  packRow("...#."),
  packRow("..#.."),
  packRow(".#..."),
  packRow("#####"));
MINI_ICON(ICON_DIGIT_3, 5,
  packRow("#####"),
  packRow("...#."),
  packRow("..#.."),
  packRow("...#."),
  packRow("....#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_4, 5,
  packRow("...#."),
  packRow("..##."),
  packRow(".#.#."),
  packRow("#..#."),
  packRow("#####"),
  packRow("...#."),
  packRow("...#."));
MINI_ICON(ICON_DIGIT_5, 5,
  packRow("#####"),
  packRow("#...."),
  packRow("####."),
  packRow("....#"),
  packRow("....#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_6, 5,
  packRow("..##."),
  packRow(".#..."),
  packRow("#...."),
  packRow("####."),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_7, 5,
  packRow("#####"),
  packRow("....#"),
  packRow("...#."),
  packRow("..#.."),
  packRow(".#..."),
  packRow(".#..."),
  packRow(".#..."));
MINI_ICON(ICON_DIGIT_8, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_DIGIT_9, 5,
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".####"),
  packRow("....#"),
  packRow("...#."),
  packRow(".##.."));
#else
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

#endif

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
  return digits * ICON_DIGIT_0.w * s + (digits - 1) * s;
}

// Draws `n` (clamped to 0-99) as a left-to-right run of tiny digit icons —
// e.g. a repeater/echo count too small a slot for the normal font to fit
// legibly. Vertically centred in the text line the same way miniIconDraw is.
inline void miniIconDrawNumber(DisplayDriver& d, int x, int top_y, int n) {
  const int s = miniIconScale(d);
  // The 9x15's digits stand on its baseline a pixel below the centred box.
  if (MINI_ICONS_LARGE == 2) top_y += 1;
  if (n < 0) n = 0;
  if (n > 99) n = 99;
  if (n >= 10) { miniIconDraw(d, x, top_y, *MINI_ICON_DIGITS[n / 10]); x += ICON_DIGIT_0.w * s + s; }
  miniIconDraw(d, x, top_y, *MINI_ICON_DIGITS[n % 10]);
}

// Top-bar status glyphs (replace the single-letter M / B / A indicators).
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MUTE, 10,   // speaker + cross (sound off)
  packRow("...#......"),
  packRow("..##......"),
  packRow("####.#...#"),
  packRow("####..#.#."),
  packRow("####...#.."),
  packRow("####..#.#."),
  packRow("####.#...#"),
  packRow("..##......"),
  packRow("...#......"));
MINI_ICON(ICON_BLUETOOTH, 7,   // ᛒ bluetooth rune
  packRow("...#..."),
  packRow("...##.."),
  packRow("#..#.#."),
  packRow(".#.##.."),
  packRow("..##..."),
  packRow(".#.##.."),
  packRow("#..#.#."),
  packRow("...##.."),
  packRow("...#..."));
MINI_ICON(ICON_ADVERT, 9,   // ((•)) advert
  packRow(".#.....#."),
  packRow("#.......#"),
  packRow("#..###..#"),
  packRow("#.#####.#"),
  packRow("#.#####.#"),
  packRow("#.#####.#"),
  packRow("#..###..#"),
  packRow("#.......#"),
  packRow(".#.....#."));
MINI_ICON(ICON_ALARM, 9,   // bell — an alarm is armed
  packRow("....#...."),
  packRow("...###..."),
  packRow("..#####.."),
  packRow("..#####.."),
  packRow("..#####.."),
  packRow(".#######."),
  packRow("#########"),
  packRow("........."),
  packRow("...###..."));
MINI_ICON(ICON_CHARGE, 9,   // lightning bolt -- on external power
  packRow("......##."),
  packRow(".....##.."),
  packRow("....##..."),
  packRow("...#####."),
  packRow(".#####..."),
  packRow("...##...."),
  packRow("..##....."),
  packRow(".##......"),
  packRow(".#......."));
MINI_ICON(ICON_TRAIL, 7,   // map pin (GPS trail logging)
  packRow("..###.."),
  packRow(".#####."),
  packRow("##...##"),
  packRow("##...##"),
  packRow("##...##"),
  packRow(".#####."),
  packRow("..###.."),
  packRow("...#..."),
  packRow("...#..."));
MINI_ICON(ICON_REPEATER, 9,   // » relaying (repeater active)
  packRow("#...#...."),
  packRow(".#...#..."),
  packRow("..#...#.."),
  packRow("...#...#."),
  packRow("....#...#"),
  packRow("...#...#."),
  packRow("..#...#.."),
  packRow(".#...#..."),
  packRow("#...#...."));
MINI_ICON(ICON_GPS, 9,   // reticle with its dot: GPS has a fix
  packRow("...###..."),
  packRow(".##...##."),
  packRow(".#.....#."),
  packRow("#...#...#"),
  packRow("#..###..#"),
  packRow("#...#...#"),
  packRow(".#.....#."),
  packRow(".##...##."),
  packRow("...###..."));
MINI_ICON(ICON_GPS_SEARCH, 9,   // the reticle broken: still searching
  packRow("...#.#..."),
  packRow(".##...##."),
  packRow(".#.....#."),
  packRow("#.......#"),
  packRow("........."),
  packRow("#.......#"),
  packRow(".#.....#."),
  packRow(".##...##."),
  packRow("...#.#..."));

MINI_ICON(ICON_CHART, 9,   // ascending bars — Home › Status
  packRow("........#"),
  packRow("......#.#"),
  packRow("......#.#"),
  packRow("....#.#.#"),
  packRow("....#.#.#"),
  packRow("..#.#.#.#"),
  packRow("..#.#.#.#"),
  packRow("#.#.#.#.#"),
  packRow("#########"));
#elif MINI_ICONS_LARGE
MINI_ICON(ICON_MUTE, 8,   // speaker + cross (sound off)
  packRow("...#...."),
  packRow("..##...."),
  packRow("####.#.#"),
  packRow("####..#."),
  packRow("####.#.#"),
  packRow("..##...."),
  packRow("...#...."));
MINI_ICON(ICON_BLUETOOTH, 5,   // ᛒ bluetooth rune
  packRow("..#.."),
  packRow("..##."),
  packRow("#.#.#"),
  packRow(".###."),
  packRow("#.#.#"),
  packRow("..##."),
  packRow("..#.."));
MINI_ICON(ICON_ADVERT, 7,   // ((•)) advert
  packRow(".#...#."),
  packRow("#.....#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.....#"),
  packRow(".#...#."));
MINI_ICON(ICON_ALARM, 7,   // bell — an alarm is armed
  packRow("...#..."),
  packRow("..###.."),
  packRow(".#####."),
  packRow(".#####."),
  packRow(".#####."),
  packRow("#######"),
  packRow("...#..."));
MINI_ICON(ICON_CHARGE, 7,   // lightning bolt -- on external power
  packRow("....##."),
  packRow("...##.."),
  packRow("..##..."),
  packRow(".#####."),
  packRow("...##.."),
  packRow("..##..."),
  packRow("..#...."));
MINI_ICON(ICON_TRAIL, 7,   // map pin (GPS trail logging)
  packRow("..###.."),
  packRow(".#####."),
  packRow("##...##"),
  packRow("##...##"),
  packRow(".#####."),
  packRow("..###.."),
  packRow("...#..."));
MINI_ICON(ICON_REPEATER, 8,   // » relaying (repeater active)
  packRow("#...#..."),
  packRow(".#...#.."),
  packRow("..#...#."),
  packRow("...#...#"),
  packRow("..#...#."),
  packRow(".#...#.."),
  packRow("#...#..."));
MINI_ICON(ICON_GPS, 7,   // reticle with its dot: GPS has a fix
  packRow("..###.."),
  packRow(".#...#."),
  packRow("#..#..#"),
  packRow("#.###.#"),
  packRow("#..#..#"),
  packRow(".#...#."),
  packRow("..###.."));
MINI_ICON(ICON_GPS_SEARCH, 7,   // the reticle broken: still searching
  packRow("..#.#.."),
  packRow(".#...#."),
  packRow("#.....#"),
  packRow("......."),
  packRow("#.....#"),
  packRow(".#...#."),
  packRow("..#.#.."));

MINI_ICON(ICON_CHART, 7,   // ascending bars — Home › Status
  packRow("......#"),
  packRow("....#.#"),
  packRow("....#.#"),
  packRow("..#.#.#"),
  packRow("..#.#.#"),
  packRow("#.#.#.#"),
  packRow("#######"));
#else
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
MINI_ICON(ICON_ADVERT, 7,   // ((•)) a dot sending waves both ways: advert
  packRow(".#...#."),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow(".#...#."));

MINI_ICON(ICON_ALARM, 5,   // bell — an alarm is armed
  packRow("..#.."),
  packRow(".###."),
  packRow(".###."),
  packRow(".###."),
  packRow("#####"),
  packRow("..#.."));
MINI_ICON(ICON_CHARGE, 5,   // lightning bolt -- on external power
  packRow("...#."),
  packRow("..##."),
  packRow(".####"),
  packRow("..##."),
  packRow(".##.."),
  packRow(".#..."));

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

MINI_ICON(ICON_GPS, 5,   // reticle with its dot: GPS has a fix
  packRow(".###."),
  packRow("#...#"),
  packRow("#.#.#"),
  packRow("#...#"),
  packRow(".###."));
MINI_ICON(ICON_GPS_SEARCH, 5,   // the reticle broken, no dot: GPS on, still searching
  packRow(".#.#."),
  packRow("#...#"),
  packRow("....."),
  packRow("#...#"),
  packRow(".#.#."));

// The Status page's glyph.
MINI_ICON(ICON_CHART, 5,   // ascending bars — Home › Status
  packRow("....#"),
  packRow("..#.#"),
  packRow("..#.#"),
  packRow("#.#.#"),
  packRow("#.#.#"),
  packRow("#####"));
#endif

// Home-carousel page glyphs — a uniform 5x5 set, deliberately smaller than the
// menu/status icons above, used in place of the page-indicator dots. One per
// HomePage; see UITask HomeScreen::pageIcon().
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_PG_CLOCK, 9,   // clock face + hands
  packRow("...###..."),
  packRow(".##.#.##."),
  packRow(".#..#..#."),
  packRow("#...#...#"),
  packRow("#...###.#"),
  packRow("#.......#"),
  packRow(".#.....#."),
  packRow(".##...##."),
  packRow("...###..."));
MINI_ICON(ICON_PG_STAR, 9,   // favourites
  packRow("....#...."),
  packRow("....#...."),
  packRow("...###..."),
  packRow("#########"),
  packRow(".#######."),
  packRow("..#####.."),
  packRow("..##.##.."),
  packRow(".##...##."),
  packRow(".#.....#."));
MINI_ICON(ICON_PG_RADIO, 9,   // antenna with waves — radio
  packRow(".#.....#."),
  packRow("#...#...#"),
  packRow("#..###..#"),
  packRow("#...#...#"),
  packRow(".#..#..#."),
  packRow("....#...."),
  packRow("...#.#..."),
  packRow("..#...#.."),
  packRow(".#.....#."));
MINI_ICON(ICON_PG_BT, 7,   // bluetooth
  packRow("...#..."),
  packRow("...##.."),
  packRow("#..#.#."),
  packRow(".#.##.."),
  packRow("..##..."),
  packRow(".#.##.."),
  packRow("#..#.#."),
  packRow("...##.."),
  packRow("...#..."));
MINI_ICON(ICON_PG_ADVERT, 9,   // advert page: the same waves
  packRow(".#.....#."),
  packRow("#.......#"),
  packRow("#..###..#"),
  packRow("#.#####.#"),
  packRow("#.#####.#"),
  packRow("#.#####.#"),
  packRow("#..###..#"),
  packRow("#.......#"),
  packRow(".#.....#."));
MINI_ICON(ICON_PG_SETTINGS, 9,   // cog with a hub hole
  packRow("...#.#..."),
  packRow(".#######."),
  packRow(".##...##."),
  packRow("##.....##"),
  packRow(".#.....#."),
  packRow("##.....##"),
  packRow(".##...##."),
  packRow(".#######."),
  packRow("...#.#..."));
MINI_ICON(ICON_PG_MAP, 10,   // folded map
  packRow("##########"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("#..#..#..#"),
  packRow("##########"));
MINI_ICON(ICON_PG_TOOLS, 9,   // wrench, open jaw top right
  packRow("......#.#"),
  packRow("......#.#"),
  packRow("......###"),
  packRow(".....##.."),
  packRow("....##..."),
  packRow("...##...."),
  packRow("..##....."),
  packRow(".##......"),
  packRow("##......."));
MINI_ICON(ICON_PG_MSG, 9,   // speech bubble with three dots
  packRow("#########"),
  packRow("#.......#"),
  packRow("#.#.#.#.#"),
  packRow("#.......#"),
  packRow("#########"),
  packRow("##......."),
  packRow("#........"));
MINI_ICON(ICON_PG_POWER, 9,   // power symbol
  packRow("....#...."),
  packRow("..#.#.#.."),
  packRow(".#..#..#."),
  packRow("#...#...#"),
  packRow("#.......#"),
  packRow("#.......#"),
  packRow(".#.....#."),
  packRow("..#...#.."),
  packRow("...###..."));
#elif MINI_ICONS_LARGE
MINI_ICON(ICON_PG_CLOCK, 7,   // clock face + hands
  packRow("..###.."),
  packRow(".#.#.#."),
  packRow("#..#..#"),
  packRow("#..##.#"),
  packRow("#.....#"),
  packRow(".#...#."),
  packRow("..###.."));
MINI_ICON(ICON_PG_STAR, 7,   // favourites
  packRow("...#..."),
  packRow("...#..."),
  packRow("#######"),
  packRow(".#####."),
  packRow("..###.."),
  packRow(".##.##."),
  packRow(".#...#."));
MINI_ICON(ICON_PG_RADIO, 7,   // antenna with waves — radio
  packRow(".#...#."),
  packRow("#..#..#"),
  packRow("#.###.#"),
  packRow(".#.#.#."),
  packRow("...#..."),
  packRow("..#.#.."),
  packRow(".#...#."));
MINI_ICON(ICON_PG_BT, 5,   // bluetooth
  packRow("..#.."),
  packRow("..##."),
  packRow("#.#.#"),
  packRow(".###."),
  packRow("#.#.#"),
  packRow("..##."),
  packRow("..#.."));
MINI_ICON(ICON_PG_ADVERT, 7,   // advert page: the same waves
  packRow(".#...#."),
  packRow("#.....#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.....#"),
  packRow(".#...#."));
MINI_ICON(ICON_PG_SETTINGS, 7,   // cog with a hub hole
  packRow("..#.#.."),
  packRow(".#####."),
  packRow("##...##"),
  packRow(".#...#."),
  packRow("##...##"),
  packRow(".#####."),
  packRow("..#.#.."));
MINI_ICON(ICON_PG_MAP, 7,   // folded map
  packRow("#######"),
  packRow("#..#..#"),
  packRow("#..#..#"),
  packRow("#..#..#"),
  packRow("#..#..#"),
  packRow("#..#..#"),
  packRow("#######"));
MINI_ICON(ICON_PG_TOOLS, 7,   // wrench, open jaw top right
  packRow("....#.#"),
  packRow("....#.#"),
  packRow("....###"),
  packRow("...##.."),
  packRow("..##..."),
  packRow(".##...."),
  packRow("##....."));
MINI_ICON(ICON_PG_MSG, 7,   // speech bubble with three dots
  packRow("#######"),
  packRow("#.....#"),
  packRow("#.#.#.#"),
  packRow("#.....#"),
  packRow("#######"),
  packRow("##....."),
  packRow("#......"));
MINI_ICON(ICON_PG_POWER, 7,   // power symbol
  packRow("...#..."),
  packRow(".#.#.#."),
  packRow("#..#..#"),
  packRow("#.....#"),
  packRow("#.....#"),
  packRow(".#...#."),
  packRow("..###.."));
#else
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
MINI_ICON(ICON_PG_ADVERT, 7,   // advert page: the same waves
  packRow(".#...#."),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow("#.###.#"),
  packRow(".#...#."));
MINI_ICON(ICON_PG_SETTINGS, 5,   // cog with a hub hole: settings
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
MINI_ICON(ICON_PG_TOOLS, 6,   // wrench on the diagonal, open jaw top right: tools
  packRow("...#.#"),
  packRow("...###"),
  packRow("..##.."),
  packRow(".##..."),
  packRow("##...."));
MINI_ICON(ICON_PG_MSG, 7,   // speech bubble with three dots: messages
  packRow("#######"),
  packRow("#.#.#.#"),
  packRow("#######"),
  packRow("#......"));
MINI_ICON(ICON_PG_POWER, 5,      // power symbol — shutdown
  packRow("..#.."),
  packRow("#.#.#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
#endif

// Trail-map markers — centred on a point (see miniIconDrawCentered) rather
// than anchored to a text line.
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_DOT, 5,   // ● filled trail point
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"),
  packRow(".###."),
  packRow("..#.."));
#else
MINI_ICON(ICON_MAP_DOT, 3,        // ● filled trail point
  packRow("###"),
  packRow("###"),
  packRow("###"));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_RING, 5,   // ○ hollow ring — a new trail segment start
  packRow(".###."),
  packRow("#...#"),
  packRow("#...#"),
  packRow("#...#"),
  packRow(".###."));
#else
MINI_ICON(ICON_MAP_RING, 3,       // ○ hollow ring — marks a new trail segment start
  packRow("###"),
  packRow("#.#"),
  packRow("###"));
#endif
#if MINI_ICONS_LARGE == 2   // the markers that also head rows of text
MINI_ICON(ICON_MAP_WAYPOINT, 9,   // ◇ hollow diamond — saved waypoint
  packRow("....#...."),
  packRow("...#.#..."),
  packRow("..#...#.."),
  packRow(".#.....#."),
  packRow("#.......#"),
  packRow(".#.....#."),
  packRow("..#...#.."),
  packRow("...#.#..."),
  packRow("....#...."));
#elif MINI_ICONS_LARGE
MINI_ICON(ICON_MAP_WAYPOINT, 7,   // ◇ hollow diamond — saved waypoint
  packRow("...#..."),
  packRow("..#.#.."),
  packRow(".#...#."),
  packRow("#.....#"),
  packRow(".#...#."),
  packRow("..#.#.."),
  packRow("...#..."));
#else
MINI_ICON(ICON_MAP_WAYPOINT, 5,   // ◇ hollow diamond — saved waypoint
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_START, 9,   // + trail start marker
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("#########"),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."));
#else
MINI_ICON(ICON_MAP_START, 5,      // + trail start marker
  packRow("..#.."),
  packRow("..#.."),
  packRow("#####"),
  packRow("..#.."),
  packRow("..#.."));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_CURRENT, 9,   // ✕ live position / last trail point
  packRow("##.....##"),
  packRow("###...###"),
  packRow(".###.###."),
  packRow("..#####.."),
  packRow("...###..."),
  packRow("..#####.."),
  packRow(".###.###."),
  packRow("###...###"),
  packRow("##.....##"));
#else
MINI_ICON(ICON_MAP_CURRENT, 5,    // ✕ live position / last trail point
  packRow("#...#"),
  packRow(".#.#."),
  packRow("..#.."),
  packRow(".#.#."),
  packRow("#...#"));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_CONTACT, 9,   // ◆ filled diamond — a live-tracked contact
  packRow("....#...."),
  packRow("...###..."),
  packRow("..#####.."),
  packRow(".#######."),
  packRow("#########"),
  packRow(".#######."),
  packRow("..#####.."),
  packRow("...###..."),
  packRow("....#...."));
#elif MINI_ICONS_LARGE
MINI_ICON(ICON_MAP_CONTACT, 7,   // ◆ filled diamond — a live-tracked contact
  packRow("...#..."),
  packRow("..###.."),
  packRow(".#####."),
  packRow("#######"),
  packRow(".#####."),
  packRow("..###.."),
  packRow("...#..."));
#else
MINI_ICON(ICON_MAP_CONTACT, 5,    // ◆ filled diamond — a live-tracked contact ([LOC] share)
  packRow("..#.."),
  packRow(".###."),
  packRow("#####"),
  packRow(".###."),
  packRow("..#.."));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_NORTH, 7,   // "N" with a peaked roof — compass north marker
  packRow("...#..."),
  packRow("..###.."),
  packRow(".#...#."),
  packRow("#.....#"),
  packRow("##....#"),
  packRow("#.#...#"),
  packRow("#..#..#"),
  packRow("#...#.#"),
  packRow("#....##"),
  packRow("#.....#"));
#else
MINI_ICON(ICON_MAP_NORTH, 5,      // "N" with a peaked roof — compass north marker
  packRow("..#.."),
  packRow(".###."),
  packRow("#...#"),
  packRow("##..#"),
  packRow("#.#.#"),
  packRow("#..##"),
  packRow("#...#"));
#endif
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_MAP_TARGET, 7,   // ⚑ flag on a pole — the active Locator/Nav target
  packRow("#######"),
  packRow("#.....#"),
  packRow("#.....#"),
  packRow("#######"),
  packRow("#......"),
  packRow("#......"),
  packRow("#......"),
  packRow("#......"),
  packRow("#......"));
#else
MINI_ICON(ICON_MAP_TARGET, 5,     // ⚑ flag on a pole — the active Locator/Nav target
  packRow("####."),
  packRow("#..#."),
  packRow("####."),
  packRow("#...."),
  packRow("#...."));
#endif

// Arrows towards the eight compass points (north up), for a bearing beside
// a distance: ICON_ARROWS[((deg + 22) % 360) / 45].
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_ARROW_N, 9,
  packRow("....#...."),
  packRow("...###..."),
  packRow("..#.#.#.."),
  packRow(".#..#..#."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."));
MINI_ICON(ICON_ARROW_NE, 9,
  packRow("....#####"),
  packRow(".......##"),
  packRow("......#.#"),
  packRow(".....#..#"),
  packRow("....#...#"),
  packRow("...#....."),
  packRow("..#......"),
  packRow(".#......."),
  packRow("#........"));
MINI_ICON(ICON_ARROW_E, 9,
  packRow("........."),
  packRow(".....#..."),
  packRow("......#.."),
  packRow(".......#."),
  packRow("#########"),
  packRow(".......#."),
  packRow("......#.."),
  packRow(".....#..."),
  packRow("........."));
MINI_ICON(ICON_ARROW_SE, 9,
  packRow("#........"),
  packRow(".#......."),
  packRow("..#......"),
  packRow("...#....."),
  packRow("....#...#"),
  packRow(".....#..#"),
  packRow("......#.#"),
  packRow(".......##"),
  packRow("....#####"));
MINI_ICON(ICON_ARROW_S, 9,
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow("....#...."),
  packRow(".#..#..#."),
  packRow("..#.#.#.."),
  packRow("...###..."),
  packRow("....#...."));
MINI_ICON(ICON_ARROW_SW, 9,
  packRow("........#"),
  packRow(".......#."),
  packRow("......#.."),
  packRow(".....#..."),
  packRow("#...#...."),
  packRow("#..#....."),
  packRow("#.#......"),
  packRow("##......."),
  packRow("#####...."));
MINI_ICON(ICON_ARROW_W, 9,
  packRow("........."),
  packRow("...#....."),
  packRow("..#......"),
  packRow(".#......."),
  packRow("#########"),
  packRow(".#......."),
  packRow("..#......"),
  packRow("...#....."),
  packRow("........."));
MINI_ICON(ICON_ARROW_NW, 9,
  packRow("#####...."),
  packRow("##......."),
  packRow("#.#......"),
  packRow("#..#....."),
  packRow("#...#...."),
  packRow(".....#..."),
  packRow("......#.."),
  packRow(".......#."),
  packRow("........#"));
#else
MINI_ICON(ICON_ARROW_N, 7,
  packRow("...#..."),
  packRow("..###.."),
  packRow(".#.#.#."),
  packRow("...#..."),
  packRow("...#..."),
  packRow("...#..."),
  packRow("...#..."));
MINI_ICON(ICON_ARROW_NE, 7,
  packRow("...####"),
  packRow(".....##"),
  packRow("....#.#"),
  packRow("...#..#"),
  packRow("..#...."),
  packRow(".#....."),
  packRow("#......"));
MINI_ICON(ICON_ARROW_E, 7,
  packRow("......."),
  packRow("....#.."),
  packRow(".....#."),
  packRow("#######"),
  packRow(".....#."),
  packRow("....#.."),
  packRow("......."));
MINI_ICON(ICON_ARROW_SE, 7,
  packRow("#......"),
  packRow(".#....."),
  packRow("..#...."),
  packRow("...#..#"),
  packRow("....#.#"),
  packRow(".....##"),
  packRow("...####"));
MINI_ICON(ICON_ARROW_S, 7,
  packRow("...#..."),
  packRow("...#..."),
  packRow("...#..."),
  packRow("...#..."),
  packRow(".#.#.#."),
  packRow("..###.."),
  packRow("...#..."));
MINI_ICON(ICON_ARROW_SW, 7,
  packRow("......#"),
  packRow(".....#."),
  packRow("....#.."),
  packRow("#..#..."),
  packRow("#.#...."),
  packRow("##....."),
  packRow("####..."));
MINI_ICON(ICON_ARROW_W, 7,
  packRow("......."),
  packRow("..#...."),
  packRow(".#....."),
  packRow("#######"),
  packRow(".#....."),
  packRow("..#...."),
  packRow("......."));
MINI_ICON(ICON_ARROW_NW, 7,
  packRow("####..."),
  packRow("##....."),
  packRow("#.#...."),
  packRow("#..#..."),
  packRow("....#.."),
  packRow(".....#."),
  packRow("......#"));
#endif
static constexpr const MiniIcon* ICON_ARROWS[8] = { &ICON_ARROW_N, &ICON_ARROW_NE, &ICON_ARROW_E, &ICON_ARROW_SE,
                                                 &ICON_ARROW_S, &ICON_ARROW_SW, &ICON_ARROW_W, &ICON_ARROW_NW };

// Keyboard special-key glyphs.
#if MINI_ICONS_LARGE == 2
MINI_ICON(ICON_KEYBOARD, 9,   // PIN keyboard to ABC keyboard switch icon
  packRow("#.#.#.#.#"),
  packRow("#########"),
  packRow("#.#.#.#.#"),
  packRow("#########"),
  packRow("#.#.#.#.#"));
MINI_ICON(ICON_SHIFT, 9,   // ⇧  caps
  packRow("....#...."),
  packRow("...###..."),
  packRow("..#####.."),
  packRow(".#######."),
  packRow("#########"),
  packRow("...###..."),
  packRow("...###..."),
  packRow("...###..."),
  packRow("...###..."));
MINI_ICON(ICON_BACKSPACE, 11,   // ⌫  delete-left (× knocked out of the arrow body)
  packRow("....#######"),
  packRow("...########"),
  packRow("..###.###.#"),
  packRow(".#####.#.##"),
  packRow("#######.###"),
  packRow(".#####.#.##"),
  packRow("..###.###.#"),
  packRow("...########"),
  packRow("....#######"));
MINI_ICON(ICON_SPACE_L, 7,
  packRow("#......"),
  packRow("#......"),
  packRow("#......"),
  packRow("#......"),
  packRow("#######"));
MINI_ICON(ICON_SPACE_R, 7,
  packRow("......#"),
  packRow("......#"),
  packRow("......#"),
  packRow("......#"),
  packRow("#######"));
#else
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
// Space ⎵ is two halves drawn
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

#endif

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
// right_x narrows the list to a left column (a list beside a detail pane).
template <class RenderRow>
inline int drawList(DisplayDriver& d, int total, int sel, int& scroll, RenderRow row, int right_x = -1) {
  if (right_x < 0) right_x = d.width();
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
  drawScrollIndicator(d, right_x, start_y, visible * item_h, total, visible, scroll);
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
inline void drawRowSelection(DisplayDriver& d, int y, bool sel, int reserve, int right_x = -1) {
  d.drawSelectionRow(0, y - 1, (right_x < 0 ? d.width() : right_x) - reserve, d.lineStep() - 1, sel);
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

// Page dots for a screen with views, right edge at x_right, centred on cy:
// the current page a filled square, the others a point.
inline int pageDotsWidth(DisplayDriver& d, int pages) {
  const int s = miniIconScale(d);
  return pages > 1 ? (pages - 1) * 5 * s + 3 * s : 0;
}
inline void drawPageDots(DisplayDriver& d, int x_right, int cy, int page, int pages) {
  const int s = miniIconScale(d);
  for (int i = 0; i < pages; i++) {
    const int cx = x_right - 2 * s - (pages - 1 - i) * 5 * s;   // centre of dot i
    if (i == page) d.fillRect(cx - s, cy - s, 3 * s, 3 * s);
    else           d.fillRect(cx, cy, s, s);
  }
}

// A screen's title bar: the title centred over the separator line, page
// dots at the right when the screen has pages, and the ≡ hint when it has a
// Hold-Enter menu (menu_open highlights it). A title too long to centre goes
// left and ends in an ellipsis. No icon: the screen was just picked by one.
// Leaves the ink LIGHT.
inline void drawScreenHeader(DisplayDriver& d, const char* title, int page = -1, int pages = 0,
                             bool menu_hint = false, bool menu_open = false) {
  const int s = miniIconScale(d);
  d.setColor(DisplayDriver::LIGHT);
  const int hint = menu_hint ? d.menuHintWidth() : 0;
  const int dots = pages > 1 ? pageDotsWidth(d, pages) + 3 * s : 0;
  const int reserve = hint + dots;
  char buf[96];
  d.translateUTF8ToBlocks(buf, (title && title[0]) ? title : "", sizeof(buf));
  const int tw = d.getTextWidth(buf), avail = d.width() - reserve - 4;
  if (tw <= avail) {
    int x = d.width() / 2 - tw / 2;                                  // centred on the screen
    if (x + tw > d.width() - reserve - 2) x = (d.width() - reserve) / 2 - tw / 2;   // or clear of dots / ≡
    d.setCursor(x, 0);
    d.print(buf);
  } else {
    d.drawTextEllipsized(2, 0, avail > 0 ? avail : 0, buf);
  }
  d.fillRect(0, d.headerH() - d.sepH(), d.width(), d.sepH());
  if (pages > 1) drawPageDots(d, d.width() - hint - s, (d.headerH() - d.sepH()) / 2, page, pages);
  if (menu_hint) d.drawContextMenuHint(DisplayDriver::LIGHT, menu_open);
}

// An accordion section's disclosure mark, one character wide: a triangle
// pointing right while the section is folded, down while it's open.
inline void drawDisclosure(DisplayDriver& d, int x, int y, bool open) {
  const int s = miniIconScale(d), cy = y + d.getLineHeight() / 2;
  if (open) for (int i = 0; i < 3; i++) d.fillRect(x + i * s, cy - s + i * s, (5 - 2 * i) * s, s);
  else      for (int i = 0; i < 3; i++) d.fillRect(x + s + i * s, cy - 2 * s + i * s, s, (5 - 2 * i) * s);
}

// A push button: its label in a soft pill, filled while selected. One line
// tall (the labels have no descenders worth padding for). Returns its width.
inline int buttonWidth(DisplayDriver& d, const char* label) { return d.getTextWidth(label) + 6; }
inline int drawButton(DisplayDriver& d, int x, int y, const char* label, bool sel) {
  const int w = buttonWidth(d, label), h = d.getLineHeight();
  d.setColor(DisplayDriver::LIGHT);
  if (sel) { d.fillSoftRect(x, y, w, h); d.setColor(DisplayDriver::DARK); }
  else       d.drawSoftRect(x, y, w, h);
  d.setCursor(x + 3, y);
  d.print(label);
  d.setColor(DisplayDriver::LIGHT);
  return w;
}
