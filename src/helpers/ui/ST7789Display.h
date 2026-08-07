#pragma once

#include "DisplayDriver.h"
#include <Wire.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include "ST7789Spi.h"

class ST7789Display : public DisplayDriver {
  ST7789Spi display;
  bool _isOn;
  uint16_t _color;
  int _x=0, _y=0;     // physical (SCALE_X/Y-scaled) cursor, used by the ArialMT path
#ifdef OLED_MISC_FIXED_FONT
  int _lx=0, _ly=0;   // logical (pre-scale) cursor, used by the misc-fixed path
#endif

  bool i2c_probe(TwoWire& wire, uint8_t addr);
#ifdef OLED_MISC_FIXED_FONT
  // Thin wrapper over the shared misc-fixed glyph metrics, kept out of the
  // header so the font tables land in one translation unit only.
  uint8_t glyphXAdvance(uint32_t cp);
#endif
public:
#ifdef HELTEC_VISION_MASTER_T190
  ST7789Display() : DisplayDriver(128, 64), display(&SPI, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, GEOMETRY_RAWMODE, 320, 170,PIN_TFT_SDA,-1,PIN_TFT_SCL) {_isOn = false;}
#elif defined(CARDPUTER_ADV)
  ST7789Display() : DisplayDriver(128, 64), display(&SPI, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, GEOMETRY_RAWMODE, 240, 135, PIN_TFT_SDA, -1, PIN_TFT_SCL) {_isOn = false;}
#else
  ST7789Display() : DisplayDriver(128, 64), display(&SPI1, PIN_TFT_RST, PIN_TFT_DC, PIN_TFT_CS, GEOMETRY_RAWMODE, 240, 135) {_isOn = false;}
#endif
  bool begin();

#ifdef OLED_MISC_FIXED_FONT
  // Same misc-fixed 6x9 font the SH1106 and e-ink drivers use: full
  // Latin/Greek/Cyrillic instead of the built-in ArialMT font's Latin-only
  // coverage, so the keyboard's alphabets render as themselves. Opt-in per
  // variant (the font costs ~14 KB of flash) — see the boards that set
  // OLED_MISC_FIXED_FONT in their platformio.ini.
  int getLineHeight() const override { return 9; }   // 6x9 box height, logical units
  // The font's own glyph table is ink-tight, unlike ArialMT (below).
  int textWidthTrailingGap() const override { return 0; }
  bool isSingleFont() const override { return true; }
  void setSingleFont(bool) override { }   // single-font: ignore toggles
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    // No transliteration needed — print() renders UTF-8 directly.
    strncpy(dest, src, dest_size - 1);
    dest[dest_size - 1] = '\0';
  }
  uint16_t getCodepointWidth(uint32_t cp) override { return glyphXAdvance(cp); }
#endif

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void printWordWrap(const char* str, int max_width) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  void endFrame() override;
};
