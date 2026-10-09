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

#include "EinkGfxDisplay.h"
#include "GxEPD2_FastWrite.h"

// EINK_FAST_WRITES=1: drive the panel through its <model>_Fast subclass
// (GxEPD2_FastWrite.h) -- the same bytes, sent a row at a time in bulk instead
// of one SPI call per byte. Only for panels that have one.
#define EINK_CAT_(a, b) a##b
#define EINK_CAT(a, b) EINK_CAT_(a, b)
#if defined(EINK_DISPLAY_MODEL) && defined(EINK_FAST_WRITES) && EINK_FAST_WRITES
  #define EINK_PANEL_TYPE EINK_CAT(EINK_DISPLAY_MODEL, _Fast)
#elif defined(EINK_DISPLAY_MODEL)
  #define EINK_PANEL_TYPE EINK_DISPLAY_MODEL
#endif

// This driver calls callBusyPump() during its BUSY-pin waits; app code keys
// its setBusyPumpFn() wiring off this so it isn't compiled for displays (or
// the sim, whose radio isn't a RadioLibWrapper) that never would.
#define DISPLAY_HAS_BUSY_PUMP 1

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

// Panel native dimensions before rotation. Derived from the model class when
// EINK_DISPLAY_MODEL is set (its visible width: some controllers carry a few
// columns more than the glass shows); override with EINK_PANEL_W/H for other
// panels.
#if defined(EINK_DISPLAY_MODEL)
  #ifndef EINK_PANEL_W
    #define EINK_PANEL_W EINK_DISPLAY_MODEL::WIDTH_VISIBLE
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

// The panel object, as a base so it's built before EinkGfxDisplay draws on it.
struct GxEPDPanel {
#if defined(EINK_DISPLAY_MODEL)
  GxEPD2_BW<EINK_PANEL_TYPE, EINK_PANEL_TYPE::HEIGHT> display;
  GxEPDPanel() : display(EINK_PANEL_TYPE(PIN_DISPLAY_CS, PIN_DISPLAY_DC, PIN_DISPLAY_RST, PIN_DISPLAY_BUSY)) {}
#else
  GxEPD2_BW<GxEPD2_150_BN, 200> display;
  GxEPDPanel() : display(GxEPD2_150_BN(DISP_CS, DISP_DC, DISP_RST, DISP_BUSY)) {}
#endif
};

class GxEPDDisplay : private GxEPDPanel, public EinkGfxDisplay {
  bool _init = false;
  bool _isOn = false;
  uint8_t _full_refresh_interval = 0;
  uint8_t _partial_count = 0;

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
  GxEPDDisplay() : EinkGfxDisplay(EINK_DISP_W, EINK_DISP_H, display, GxEPD_BLACK, GxEPD_WHITE) {}

#ifdef ENABLE_SCREENSHOT
  const uint8_t* getBuffer() override { return display.getBuffer(); }
  uint16_t getBufferSize() override { return display.getBufferSize(); }
  uint8_t getDisplayType() override { return 1; }  // 1 = e-ink
  // GxEPD2's own dimensions after rotation, of the visible area.
  int screenshotWidth()         override { return (int)display.width(); }
  int screenshotHeight()        override { return (int)display.height(); }
  uint8_t screenshotRotation()  override { return (uint8_t)display.getRotation(); }
#endif

  bool begin();

  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void clear() override;
  void setDisplayRotation(uint8_t rot) override;
  void setFullRefreshInterval(uint8_t n) override { _full_refresh_interval = n; _partial_count = 0; }
  void endFrame() override;
};
