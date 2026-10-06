#pragma once
// EinkGfxDisplay's bodies. Include from exactly one .cpp (see EinkGfxDisplay.h):
// the font tables below are static arrays.

#include "EinkGfxDisplay.h"
#include "MiscFixedRenderer.h"
#if EINK_LARGE_FONT == 2
  #include "MiscFixed9x15Font.h"
  #define EINK_LARGE_GFXFONT MiscFixed9x15
#else
  #include "MiscFixed8x13Font.h"
  #define EINK_LARGE_GFXFONT MiscFixed8x13
#endif

EinkGfxDisplay::EinkGfxDisplay(int w, int h, Adafruit_GFX& gfx, uint16_t ink, uint16_t paper)
    : DisplayDriver(w, h), _gfx(gfx), _ink(ink), _paper(paper), _curr(ink), _font(&MiscFixed) {
  updateLayout();
}

void EinkGfxDisplay::updateLayout() {
  const bool landscape = width() >= height();
  _font = (EINK_LARGE_FONT && landscape) ? &EINK_LARGE_GFXFONT : &MiscFixed;
  _px = (!EINK_LARGE_FONT && landscape) ? 2 : 1;
  _cell_w = miscFixedCellW(*_font);
  _cell_h = _font->yAdvance;
  _vw_dirty = true;
  _force = true;
}

void EinkGfxDisplay::startFrame(Color bkg) {
  _gfx.fillScreen(_paper);
  _curr = _ink;
  _text_sz = 1;
  _hash = 2166136261u;
}

void EinkGfxDisplay::setTextSize(int sz) {
  _text_sz = sz;
  _vw_dirty = true;
  mix<int>(sz);
}

void EinkGfxDisplay::setColor(Color c) {
  mix<Color>(c);
  _curr = (c == DARK) ? _paper : _ink;   // DARK is the paper, LIGHT the ink
}

void EinkGfxDisplay::setCursor(int x, int y) {
  mix<int>(x);
  mix<int>(y);
  _cx = x;
  _cy = y;
}

void EinkGfxDisplay::print(const char* str) {
  mix(str, strlen(str));
  if (_text_sz == 4) {   // the built-in font, its cursor at the cell's top left
    _gfx.setFont(NULL);
    _gfx.setTextSize(BIG_TEXT_SCALE);
    _gfx.setTextColor(_curr);
    _gfx.setCursor(_cx, _cy);
    for (const char* p = str; *p; p++) _gfx.write((uint8_t)*p);
    _cx = _gfx.getCursorX();
    return;
  }
  _gfx.setCursor(_cx, _cy);
  miscFixedPrint(_gfx, str, _px * _text_sz, _curr, *_font);
  _cx = _gfx.getCursorX();
  _cy = _gfx.getCursorY();
}

uint16_t EinkGfxDisplay::getTextWidth(const char* str) {
  if (_text_sz == 4) return strlen(str) * getCharWidth();
  return miscFixedTextWidth(str, _px * _text_sz, *_font);
}

uint16_t EinkGfxDisplay::getCodepointWidth(uint32_t cp) {
  if (_text_sz == 4) return getCharWidth();
  return miscFixedXAdvance(cp, _px * _text_sz, *_font);
}

void EinkGfxDisplay::fillRect(int x, int y, int w, int h) {
  mix<int>(x); mix<int>(y); mix<int>(w); mix<int>(h);
  _gfx.fillRect(x, y, w, h, _curr);
}

void EinkGfxDisplay::drawRect(int x, int y, int w, int h) {
  mix<int>(x); mix<int>(y); mix<int>(w); mix<int>(h);
  _gfx.drawRect(x, y, w, h, _curr);
  if (_px == 2 && w > 2 && h > 2) _gfx.drawRect(x + 1, y + 1, w - 2, h - 2, _curr);
}

void EinkGfxDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  mix<int>(x); mix<int>(y); mix<int>(w); mix<int>(h);
  mix<uintptr_t>((uintptr_t)bits);
  const int widthInBytes = (w + 7) / 8;
  for (int by = 0; by < h; by++)
    for (int bx = 0; bx < w; bx++)
      if (pgm_read_byte(bits + by * widthInBytes + bx / 8) & (0x80 >> (bx & 7)))
        _gfx.drawPixel(x + bx, y + by, _curr);
}
