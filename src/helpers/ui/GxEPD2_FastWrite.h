#pragma once
// Faster image writes for large GxEPD2 panels.
//
// GxEPD2 sends the framebuffer to the panel one byte at a time: every byte is
// its own SPI.transfer(uint8_t) call, and on nRF52 each of those is a complete
// EasyDMA transaction with several microseconds of fixed setup overhead --
// independent of the SPI clock. A partial refresh here writes the full screen
// five times (GxEPDDisplay::endFrame's inverse-redrive pass, then
// GxEPD2_BW::display()), so on an 800x480 panel (48 KB a screen) that overhead
// alone blocks the main loop for well over a second per refresh.
//
// These subclasses override only the image writes, sending each row as one
// bulk SPI.transfer(buf, nullptr, n) -- the same bytes, in the same order, to
// the same RAM window -- so the cost becomes just the wire time. Everything
// else (init, refresh, power) is the stock driver. The first write (panel init
// and the initial screen clear) and the rarely used mirror/PGM variants go
// through the stock path unchanged.
//
// Use by naming the subclass as the panel: -D EINK_DISPLAY_MODEL=GxEPD2_426_GDEQ0426T82_Fast

#include <string.h>

#ifdef _GxEPD2_426_GDEQ0426T82_H_
class GxEPD2_426_GDEQ0426T82_Fast : public GxEPD2_426_GDEQ0426T82 {
  typedef GxEPD2_426_GDEQ0426T82 Base;
  uint8_t _row[WIDTH / 8];   // one panel row, in RAM (EasyDMA can't read flash)

  bool canFast(bool mirror_y, bool pgm) const {
    return _init_display_done && !_initial_write && !mirror_y && !pgm;
  }

  // Same RAM window as the stock driver's (private) _setPartialRamArea():
  // the gates are reversed on this panel, so y is mirrored and entered decreasing.
  void setRamArea(uint16_t x, uint16_t y, uint16_t w, uint16_t h) {
    y = HEIGHT - y - h;
    _writeCommand(0x11);
    _writeData(0x01);
    _writeCommand(0x44);
    _writeData(x % 256);
    _writeData(x / 256);
    _writeData((x + w - 1) % 256);
    _writeData((x + w - 1) / 256);
    _writeCommand(0x45);
    _writeData((y + h - 1) % 256);
    _writeData((y + h - 1) / 256);
    _writeData(y % 256);
    _writeData(y / 256);
    _writeCommand(0x4e);
    _writeData(x % 256);
    _writeData(x / 256);
    _writeCommand(0x4f);
    _writeData((y + h - 1) % 256);
    _writeData((y + h - 1) / 256);
  }

  // The stock _writeImage(), with the per-byte loop replaced by one bulk
  // transfer per row (clipping and indexing unchanged).
  void fastWrite(uint8_t command, const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h, bool invert) {
    delay(1);   // as the stock driver: yield
    int16_t wb = (w + 7) / 8;
    x -= x % 8;
    w = wb * 8;
    int16_t x1 = x < 0 ? 0 : x;
    int16_t y1 = y < 0 ? 0 : y;
    int16_t w1 = x + w < int16_t(WIDTH) ? w : int16_t(WIDTH) - x;
    int16_t h1 = y + h < int16_t(HEIGHT) ? h : int16_t(HEIGHT) - y;
    int16_t dx = x1 - x;
    int16_t dy = y1 - y;
    w1 -= dx;
    h1 -= dy;
    if ((w1 <= 0) || (h1 <= 0)) return;
    setRamArea(x1, y1, w1, h1);
    _writeCommand(command);
    _startTransfer();
    const int16_t nb = w1 / 8;
    for (int16_t i = 0; i < h1; i++) {
      const uint8_t* src = &bitmap[dx / 8 + int32_t(i + dy) * wb];
      if (invert) { for (int16_t j = 0; j < nb; j++) _row[j] = ~src[j]; }
      else        { memcpy(_row, src, nb); }
      _pSPIx->transfer(_row, nullptr, nb);
    }
    _endTransfer();
    delay(1);
  }

public:
  GxEPD2_426_GDEQ0426T82_Fast(int16_t cs, int16_t dc, int16_t rst, int16_t busy) : Base(cs, dc, rst, busy) {}
  using Base::writeImage;   // keep the other overloads visible

  void writeImage(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                  bool invert = false, bool mirror_y = false, bool pgm = false) override {
    if (!canFast(mirror_y, pgm)) { Base::writeImage(bitmap, x, y, w, h, invert, mirror_y, pgm); return; }
    fastWrite(0x24, bitmap, x, y, w, h, invert);
  }
  void writeImageForFullRefresh(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                                bool invert = false, bool mirror_y = false, bool pgm = false) override {
    if (!canFast(mirror_y, pgm)) { Base::writeImageForFullRefresh(bitmap, x, y, w, h, invert, mirror_y, pgm); return; }
    fastWrite(0x26, bitmap, x, y, w, h, invert);   // set previous
    fastWrite(0x24, bitmap, x, y, w, h, invert);   // set current
  }
  void writeImageAgain(const uint8_t bitmap[], int16_t x, int16_t y, int16_t w, int16_t h,
                       bool invert = false, bool mirror_y = false, bool pgm = false) override {
    if (!canFast(mirror_y, pgm)) { Base::writeImageAgain(bitmap, x, y, w, h, invert, mirror_y, pgm); return; }
    fastWrite(0x26, bitmap, x, y, w, h, invert);   // set previous
    fastWrite(0x24, bitmap, x, y, w, h, invert);   // set current
  }
};
#endif
