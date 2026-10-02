#pragma once

#include <gfxfont.h>
#include "DisplayDriver.h"

class Adafruit_GFX;

// Everything an e-ink panel draws, on top of any Adafruit_GFX: text in the
// misc-fixed font, rects, bitmaps, and a hash of each frame so an unchanged
// one never costs a refresh. GxEPDDisplay puts it on a real panel; the
// simulator's SimEinkDisplay on a canvas, so the two render identically.
//
// The bodies live in EinkGfxDisplayImpl.h, which exactly one .cpp includes
// (GxEPDDisplay.cpp on a board, the sim's target.cpp): it pulls in the font
// tables.

// EINK_LARGE_FONT: a landscape panel writes in the 8x13 at its own resolution.
// Without it a landscape panel doubles the 6x9 (every pixel 2x2) -- for small
// dense panels where 8x13 would be too fine. A portrait panel always uses the
// 6x9: its narrow width wants the smaller cell.
#ifndef EINK_LARGE_FONT
  #define EINK_LARGE_FONT 0
#endif

class EinkGfxDisplay : public DisplayDriver {
  Adafruit_GFX& _gfx;
  const uint16_t _ink, _paper;   // LIGHT and DARK in the panel's colours
  uint16_t _curr;
  const GFXfont* _font;
  int _cell_w = 6, _cell_h = 9;  // the font's cell, before _px and the text size
  int _px = 1;                   // pixel doubling (landscape without EINK_LARGE_FONT)
  int _text_sz = 1;
  int _cx = 0, _cy = 0;          // cursor, top of the text row
  uint32_t _hash = 0, _shown_hash = 0;
  bool _force = true;

  void mix(const void* p, size_t n) {
    const uint8_t* b = (const uint8_t*)p;
    while (n--) { _hash ^= *b++; _hash *= 16777619u; }   // FNV-1a
  }
  template <class T> void mix(T v) { mix(&v, sizeof(v)); }

protected:
  EinkGfxDisplay(int w, int h, Adafruit_GFX& gfx, uint16_t ink, uint16_t paper);

  // After a size or rotation change: picks the font for the new shape.
  void updateLayout();
  // At the end of a frame: whether it differs from the one on the panel
  // (and from now on counts as shown).
  bool frameChanged() {
    if (!_force && _hash == _shown_hash) return false;
    _shown_hash = _hash;
    _force = false;
    return true;
  }
  void forceRedraw() { _force = true; }

public:
  // Size 4 is the huge built-in font (6x8 cell x 7), only for the stacked
  // HH / MM clock on a portrait panel. Sizes 1-3 are the misc-fixed font at
  // that multiple, as on the OLEDs.
  static const int BIG_TEXT_SCALE = 7;
  int getCharWidth() const override {
    return _text_sz == 4 ? 6 * BIG_TEXT_SCALE : _cell_w * _px * _text_sz;
  }
  int getLineHeight() const override {
    return _text_sz == 4 ? 8 * BIG_TEXT_SCALE : _cell_h * _px * _text_sz;
  }
  int pixelScale() const override { return _px; }
  bool isSingleFont() const override { return true; }
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
  bool isEink() override { return true; }

  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  uint16_t getCodepointWidth(uint32_t cp) override;
};
