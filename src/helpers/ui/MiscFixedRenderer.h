#pragma once

#include <Adafruit_GFX.h>
#include "DisplayDriver.h"
#include "MiscFixedFont.h"
#include "LemonIcons.h"

// Shared misc-fixed text renderer for the monochrome drivers (the OLEDs, the
// e-ink panels and their simulator twins).
//
// Each of them draws through an Adafruit_GFX and the font path is pure pixel
// plotting, so they all render off this one implementation instead of a copy
// each. Everything here bypasses GFX's own print(): GFX walks bytes and would
// treat each byte of a multi-byte UTF-8 sequence as its own CP437 glyph, and
// it can't reach the custom UI icons either.
//
// Every function takes the font, MiscFixed (6x9) unless said otherwise; any
// misc-fixed size converted by tools/bdf2gfx.py works (MiscFixed8x13Font.h).
//
// Include only from a .cpp — the font tables are static const arrays, so a
// header pulling this in would land a copy of them in every translation unit.
//
// `y` is the TOP of the text row in every function below, matching the UI's
// coordinate convention (GFX fonts would use the baseline).

// Every misc-fixed size has a 2px descent, so the ascent is the rest of the row.
static inline int miscFixedAscent(const GFXfont& f) { return f.yAdvance - 2; }
// A space's advance: the cell width, for the substitution box too.
static inline uint8_t miscFixedCellW(const GFXfont& f) {
  return pgm_read_byte(&f.glyph[' ' - f.first].xAdvance);
}

// Pixel advance of one codepoint at text size sz. Unmapped codepoints get the
// font's own cell, same as the substitution box drawn for them.
static inline uint8_t miscFixedXAdvance(uint32_t cp, int sz, const GFXfont& f = MiscFixed) {
  uint8_t xa;
  if (cp < f.first || cp > f.last) xa = miscFixedCellW(f);
  else xa = pgm_read_byte(&f.glyph[cp - f.first].xAdvance);
  return xa * sz;
}

// Draw one codepoint at (x, y); returns the x to continue from.
static inline int16_t miscFixedDrawGlyph(Adafruit_GFX& gfx, int16_t x, int16_t y,
                                        uint32_t cp, int sz, uint16_t color,
                                        const GFXfont& f = MiscFixed) {
  const int asc = miscFixedAscent(f);
  for (uint8_t i = 0; i < lemonIconCount; i++) {
    if (pgm_read_dword(&lemonIconCPs[i]) == cp) {
      const GFXglyph* g = &lemonIconGlyphs[i];
      uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
      int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
      uint8_t xa = pgm_read_byte(&g->xAdvance);
      uint16_t bo = pgm_read_word(&g->bitmapOffset);
      uint8_t bits = 0, bit = 0;
      // The UI icons keep a baseline 1px above the font's, so they sit 1px
      // higher in their cell (+6 against the 6x9's ascent of 7).
      const int base = asc - 1;
      for (uint8_t row = 0; row < h; row++)
        for (uint8_t col = 0; col < w; col++) {
          if (!bit) { bits = pgm_read_byte(&lemonIconBitmaps[bo++]); bit = 0x80; }
          if (bits & bit) {
            if (sz == 1) gfx.drawPixel(x + xo + col, y + base + yo + row, color);
            else gfx.fillRect(x + xo*sz + col*sz, y + base*sz + yo*sz + row*sz, sz, sz, color);
          }
          bit >>= 1;
        }
      return x + xa * sz;
    }
  }

  const int cw = miscFixedCellW(f);
  if (cp < f.first || cp > f.last) {
    // Substitution box for anything the font doesn't cover. Drawn at plain `y`
    // (not y - asc*sz): `y` is already the ascent top, so the box stays inside
    // its own row instead of bleeding into the one above.
    if (cp >= 0x20) gfx.fillRect(x + sz, y, (cw - 2)*sz, (asc - 1)*sz, color);
    return x + cw * sz;
  }

  const GFXglyph* g = &f.glyph[cp - f.first];
  uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
  int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
  uint8_t xa = pgm_read_byte(&g->xAdvance);
  uint16_t bo = pgm_read_word(&g->bitmapOffset);
  uint8_t bits = 0, bit = 0;
  for (uint8_t row = 0; row < h; row++)
    for (uint8_t col = 0; col < w; col++) {
      if (!bit) { bits = pgm_read_byte(&f.bitmap[bo++]); bit = 0x80; }
      if (bits & bit) {
        if (sz == 1) gfx.drawPixel(x + xo + col, y + asc + yo + row, color);
        else gfx.fillRect(x + xo*sz + col*sz, y + asc*sz + yo*sz + row*sz, sz, sz, color);
      }
      bit >>= 1;
    }
  return x + xa * sz;
}

// Draw a UTF-8 string from the driver's current cursor, honouring '\n', and
// leave the cursor where the text ended (same contract as GFX's print()).
static inline void miscFixedPrint(Adafruit_GFX& gfx, const char* str, int sz, uint16_t color,
                                  const GFXfont& f = MiscFixed) {
  int16_t cx = gfx.getCursorX();
  int16_t cy = gfx.getCursorY();
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp = DisplayDriver::decodeCodepoint(p);
    if (cp == '\n') { cy += f.yAdvance * sz; cx = 0; }
    else            { cx = miscFixedDrawGlyph(gfx, cx, cy, cp, sz, color, f); }
  }
  gfx.setCursor(cx, cy);
}

static inline uint16_t miscFixedTextWidth(const char* str, int sz, const GFXfont& f = MiscFixed) {
  uint16_t width = 0;
  const uint8_t* p = (const uint8_t*)str;
  while (*p) width += miscFixedXAdvance(DisplayDriver::decodeCodepoint(p), sz, f);
  return width;
}
