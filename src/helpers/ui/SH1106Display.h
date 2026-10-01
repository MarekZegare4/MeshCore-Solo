#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SH110X_NO_SPLASH
#include <Adafruit_SH110X.h>

#ifndef PIN_OLED_RESET
#define PIN_OLED_RESET -1
#endif

#ifndef DISPLAY_ADDRESS
#define DISPLAY_ADDRESS 0x3C
#endif

// Adafruit's display() always sends whole pages from the first dirty one down,
// and startFrame() clears the whole buffer, so every frame went out in full
// (1 KB, ~31 ms at 400 kHz). This adds a sender for single 32-column blocks.
class SH1106Paged : public Adafruit_SH1106G {
public:
  using Adafruit_SH1106G::Adafruit_SH1106G;
  bool canSendBlocks() const { return i2c_dev != nullptr; }
  void sendBlock(uint8_t page, uint8_t col, uint8_t len);
};

class SH1106Display : public DisplayDriver
{
  SH1106Paged display;
  bool _isOn;
  uint8_t _color;
  uint8_t _contrast;
  uint8_t _precharge;
  bool _single_font = true;   // OLED is single-font (misc-fixed 6x9); the Lemon/default switch is retired here
  int  _text_sz;
  // Frame-skip: endFrame() hashes the GFX buffer (FNV-1a, no external dep — the
  // CRC32 lib is only wired into e-ink builds) and skips the I²C flush when it's
  // byte-identical to the last one pushed. _force_redraw guarantees the first
  // frame and the frame after turnOn()/clear() always flush.
  // Each frame is hashed per 32-column block of each 8-px page (FNV-1a), and
  // only blocks whose hash changed are sent: a clock tick or a moved selection
  // is a few blocks, not the whole screen. No frame copy needed.
  static const uint8_t BLOCK_W = 32;
  static const uint8_t BLOCKS = (128 / BLOCK_W) * (64 / 8);
  uint32_t _block_hash[BLOCKS] = {};
  bool     _force_redraw = true;
  // slideBegin()'s copy of the frame on screen (only rows >= _slide_y0 are used).
  uint8_t  _slide_old[128 * 64 / 8];
  int      _slide_y0 = 0;

  bool i2c_probe(TwoWire &wire, uint8_t addr);
  // Thin wrapper over the shared misc-fixed renderer (MiscFixedRenderer.h), kept
  // out of this header so the font tables land in one translation unit only.
  uint8_t glyphXAdvance(uint32_t cp);

public:
  SH1106Display() : DisplayDriver(128, 64), display(128, 64, &Wire, PIN_OLED_RESET) {
    _isOn = false; _contrast = 255; _precharge = 0x1F; _text_sz = 1;
  }
  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  bool slideBegin(int y0) override;
  void slideCompose(int dx) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char *str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t *bits, int w, int h) override;
  uint16_t getTextWidth(const char *str) override;
  uint16_t getCodepointWidth(uint32_t cp) override {
    if (_single_font) return glyphXAdvance(cp);
    return 6 * _text_sz;  // built-in 5x7 font: 6 px advance per glyph
  }
  int getCharWidth() const override { return 6 * _text_sz; }   // misc-fixed 6x9 is 6px wide
  int getLineHeight() const override { return (_single_font ? 9 : 8) * _text_sz; }  // misc-fixed 6x9 box height
  // Only the built-in classic font pads every measured string by one trailing
  // advance column (see DisplayDriver::textWidthTrailingGap()); the lemon
  // font's width comes from its own glyph table (ink-tight, no padding).
  int textWidthTrailingGap() const override { return _single_font ? 0 : 1; }
  void setSingleFont(bool) override { }   // single-font: ignore toggles, stay misc-fixed 6x9
  bool isSingleFont() const override { return _single_font; }
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override;
  void setBrightness(uint8_t level) override;
  void endFrame() override;
  
#ifdef ENABLE_SCREENSHOT
  const uint8_t* getBuffer() override { return display.getBuffer(); }
  uint16_t getBufferSize() override { return (uint16_t)((width() * height()) / 8); }
  uint8_t getDisplayType() override { return 0; }
#endif
};
