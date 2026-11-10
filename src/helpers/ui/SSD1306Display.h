#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#define SSD1306_NO_SPLASH
#include <Adafruit_SSD1306.h>
#include <helpers/RefCountedDigitalPin.h>

#ifndef PIN_OLED_RESET
  #define PIN_OLED_RESET        21 // Reset pin # (or -1 if sharing Arduino reset pin)
#endif

#ifndef DISPLAY_ADDRESS
  #define DISPLAY_ADDRESS   0x3C
#endif

// Adafruit's display() always sends the whole frame (1 KB, ~25 ms at 400 kHz),
// and startFrame() clears the whole buffer, so every frame went out in full.
// As on the SH1106 (SH1106Display.h): a sender for single 32-column blocks.
class SSD1306Paged : public Adafruit_SSD1306 {
public:
  using Adafruit_SSD1306::Adafruit_SSD1306;
  bool canSendBlocks() const { return wire != nullptr; }
  // A frame's blocks go out between these: the bus clock is set once.
  void blocksBegin() { wire->setClock(wireClk); }
  void blocksEnd() { wire->setClock(restoreClk); }
  void sendBlock(uint8_t page, uint8_t col, uint8_t len);
};

class SSD1306Display : public DisplayDriver {
  SSD1306Paged display;
  bool _isOn;
  bool _inv_want = false, _inv_shown = false;   // invertFrame(): this frame's, and the panel's
  uint8_t _color;
  int _text_sz = 1;
  RefCountedDigitalPin* _peripher_power;
  // Frame-skip: endFrame() hashes each 32-column block of each 8-px page and
  // sends only the blocks whose hash changed; _force_redraw sends the frame
  // after begin()/turnOn()/clear() whole.
  static const uint8_t BLOCK_W = 32;
  static const uint8_t BLOCKS = (128 / BLOCK_W) * (64 / 8);
  uint32_t _block_hash[BLOCKS] = {};
  bool     _force_redraw = true;

  bool i2c_probe(TwoWire& wire, uint8_t addr);
#ifdef OLED_MISC_FIXED_FONT
  // Thin wrapper over the shared misc-fixed renderer (MiscFixedRenderer.h), kept
  // out of this header so the font tables land in one translation unit only.
  uint8_t glyphXAdvance(uint32_t cp);
#endif
public:
  SSD1306Display(RefCountedDigitalPin* peripher_power=NULL) : DisplayDriver(128, 64), 
      display(128, 64, &Wire, PIN_OLED_RESET),
      _peripher_power(peripher_power)
  {
    _isOn = false; 
  }
  bool begin();

  int getCharWidth()  const override { return 6 * _text_sz; }
#ifdef OLED_MISC_FIXED_FONT
  // Same misc-fixed 6x9 font the SH1106 and e-ink drivers use: full
  // Latin/Greek/Cyrillic instead of the built-in font's ASCII-with-blocks, so
  // the keyboard's alphabets and accented names render as themselves. Opt-in
  // per variant (the font costs ~14 KB of flash) — see the boards that set
  // OLED_MISC_FIXED_FONT in their platformio.ini.
  int getLineHeight() const override { return 9 * _text_sz; }   // 6x9 box height
  // The font's own glyph table is ink-tight, unlike the built-in font (below).
  int textWidthTrailingGap() const override { return 0; }
  bool isSingleFont() const override { return true; }
  void setSingleFont(bool) override { }   // single-font: ignore toggles
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    // No transliteration needed — print() renders UTF-8 directly.
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
  uint16_t getCodepointWidth(uint32_t cp) override { return glyphXAdvance(cp); }
#else
  int getLineHeight() const override { return 8 * _text_sz; }
  // Classic Adafruit_GFX built-in font pads every measured string by one
  // trailing advance column (see DisplayDriver::textWidthTrailingGap()).
  int textWidthTrailingGap() const override { return 1; }
#endif

  bool isOn() override { return _isOn; }
  bool invertFrame() override { _inv_want = true; return true; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(ColorVal bkg = UIColor::window_bkg) override;
  void setTextSize(int sz) override;
  void setColor(ColorVal c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
  
#ifdef ENABLE_SCREENSHOT
  const uint8_t* getBuffer() override { return display.getBuffer(); }
  uint16_t getBufferSize() override { return (uint16_t)((width() * height()) / 8); }
  uint8_t getDisplayType() override { return 0; }
#endif
};
