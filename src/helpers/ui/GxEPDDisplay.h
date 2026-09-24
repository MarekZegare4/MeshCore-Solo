#pragma once

#include <SPI.h>
#include <Wire.h>

#define ENABLE_GxEPD2_GFX 0

// Always use the patched copy of GxEPD2_BW.h from lib/GxEPD2-patch/src/: it
// adds writeInverseForRedrive() (needed by every e-ink build, see endFrame())
// and, under ENABLE_SCREENSHOT, getBuffer()/getBufferSize().  Both need the
// class's private framebuffer, so they have to live in the header itself.  The
// include guard (_GxEPD2_BW_H_) prevents the installed library version from
// being compiled as well; the copy tracks the 1.6.2 pin every e-ink variant's
// lib_deps uses.
#include "../../../lib/GxEPD2-patch/src/GxEPD2_BW.h"
#include <GxEPD2_3C.h>
#include <GxEPD2_4C.h>
#include <GxEPD2_7C.h>
#include <Fonts/FreeSans9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSans18pt7b.h>
#include <CRC32.h>

#include "DisplayDriver.h"
#include "MiscFixedFont.h"

// This driver calls callBusyPump() during its BUSY-pin waits; app code keys
// its setBusyPumpFn() wiring off this so it isn't compiled for displays (or
// the sim, whose radio isn't a RadioLibWrapper) that never would.
#define DISPLAY_HAS_BUSY_PUMP 1

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

// Panel native dimensions before rotation. Derived from the model class when
// EINK_DISPLAY_MODEL is set; override with EINK_PANEL_W/H for other panels.
#if defined(EINK_DISPLAY_MODEL)
  #ifndef EINK_PANEL_W
    #define EINK_PANEL_W EINK_DISPLAY_MODEL::WIDTH
  #endif
  #ifndef EINK_PANEL_H
    #define EINK_PANEL_H EINK_DISPLAY_MODEL::HEIGHT
  #endif
#else
  #ifndef EINK_PANEL_W
    #define EINK_PANEL_W 200
    #define EINK_PANEL_H 200
  #endif
#endif

// Odd rotations (1, 3) swap width and height.
#define EINK_DISP_W ((DISPLAY_ROTATION & 1) ? EINK_PANEL_H : EINK_PANEL_W)
#define EINK_DISP_H ((DISPLAY_ROTATION & 1) ? EINK_PANEL_W : EINK_PANEL_H)

class GxEPDDisplay : public DisplayDriver {
#if defined(EINK_DISPLAY_MODEL)
  GxEPD2_BW<EINK_DISPLAY_MODEL, EINK_DISPLAY_MODEL::HEIGHT> display;
#else
  GxEPD2_BW<GxEPD2_150_BN, 200> display;
#endif
  bool _init = false;
  bool _isOn = false;
  bool _single_font = true;   // e-ink is single-font too now (misc-fixed 6x9), matching the OLED driver
  uint16_t _curr_color;
  CRC32 display_crc;
  int last_display_crc_value = 0;
  int _text_sz = 1;
  uint8_t _full_refresh_interval = 0;
  uint8_t _partial_count = 0;

  int16_t drawGlyph(int16_t x, int16_t y, uint32_t cp, int sc);
  uint8_t glyphXAdvance(uint32_t cp, int sc);
  int scale() const { return (width() >= height()) ? 2 : 1; }

  // GxEPD2_EPD::setBusyCallback() wants a plain function pointer with a
  // void* param, not a member function -- this trampolines back into the
  // instance so callBusyPump() (DisplayDriver.h) can reach whatever board
  // setup registered via setBusyPumpFn(). _waitWhileBusy() calls the callback
  // *instead of* its own delay(1), so keep that delay here: without it the
  // wait becomes a hard spin that starves the RTOS idle task (no CPU sleep)
  // and equal-priority tasks for the whole refresh.
  static void busyCallbackTrampoline(const void* param) {
    ((GxEPDDisplay*)param)->callBusyPump();
    delay(1);
  }

public:
#if defined(EINK_DISPLAY_MODEL)
  GxEPDDisplay() : DisplayDriver(EINK_DISP_W, EINK_DISP_H),
    display(EINK_DISPLAY_MODEL(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_BUSY)) {}
#else
  GxEPDDisplay() : DisplayDriver(EINK_DISP_W, EINK_DISP_H),
    display(GxEPD2_150_BN(DISP_CS, DISP_DC, DISP_RST, DISP_BUSY)) {}
#endif

#ifdef ENABLE_SCREENSHOT
  const uint8_t* getBuffer() override { return display.getBuffer(); }
  uint16_t getBufferSize() override { return display.getBufferSize(); }
  uint8_t getDisplayType() override { return 1; }  // 1 = e-ink
  // Return GxEPD2's own reported dimensions (uses WIDTH_VISIBLE, not the full physical WIDTH
  // stored in DisplayDriver).  After setRotation(1): width()=HEIGHT, height()=WIDTH_VISIBLE.
  int screenshotWidth()         override { return (int)display.width(); }
  int screenshotHeight()        override { return (int)display.height(); }
  uint8_t screenshotRotation()  override { return (uint8_t)display.getRotation(); }
#endif

  // Line height and approx. char width for each font size:
  //   1 = FreeSans9pt      (lineH=16, charW≈9)
  //   2 = FreeSansBold12pt (lineH=20, charW≈12)
  //   3 = FreeSans18pt     (lineH=28, charW≈17)
  // Built-in font scale used for size 4 (huge clock digits): 6×8 cell × 7 = 42×56.
  static const int BIG_TEXT_SCALE = 7;
  // Size 1 scales with orientation (portrait 1×, landscape 2×); size 2 is always 12×16;
  // size 3 is FreeSans18pt (~17×28); size 4 is the built-in font at 7× (~42×56),
  // used only for the stacked HH/MM clock on portrait e-ink. Landscape = width >= height.
  int getCharWidth() const override {
    int sc = scale();
    if (_text_sz == 4) return 6 * BIG_TEXT_SCALE;
    if (_text_sz == 3) return 17;
    if (_text_sz == 2) return 12 * sc;
    return 6 * sc;   // misc-fixed 6x9 is 6px wide
  }
  int getLineHeight() const override {
    int sc = scale();
    if (_text_sz == 4) return 8 * BIG_TEXT_SCALE;
    if (_text_sz == 3) return 28;
    if (_text_sz == 2) return 16 * sc;
    return (_single_font ? 9 : 8) * sc;   // misc-fixed 6x9 box height
  }
  void setSingleFont(bool) override { }   // single-font: ignore toggles, stay misc-fixed 6x9
  bool isSingleFont() const override { return _single_font; }
  void translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) override {
    if (_single_font) {
      strncpy(dest, src, dest_size - 1);
      dest[dest_size - 1] = '\0';
    } else {
      translateUTF8Static(dest, src, dest_size);
    }
  }
  bool begin();

  bool isOn() override { return _isOn; }
  bool isEink() override { return true; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void startFrame(Color bkg = DARK) override;
  void setTextSize(int sz) override;
  void setColor(Color c) override;
  void setCursor(int x, int y) override;
  void print(const char* str) override;
  void fillRect(int x, int y, int w, int h) override;
  void drawRect(int x, int y, int w, int h) override;
  void drawXbm(int x, int y, const uint8_t* bits, int w, int h) override;
  uint16_t getTextWidth(const char* str) override;
  uint16_t getCodepointWidth(uint32_t cp) override {
    if (_single_font && _text_sz == 1) return glyphXAdvance(cp, scale());
    return DisplayDriver::getCodepointWidth(cp);
  }
  void setDisplayRotation(uint8_t rot) override;
  void setFullRefreshInterval(uint8_t n) override { _full_refresh_interval = n; _partial_count = 0; }
  void endFrame() override;
};
