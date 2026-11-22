#pragma once

#include <SPI.h>
#include <Adafruit_SharpMem.h>
#include "EinkGfxDisplay.h"

// Sharp Memory LCD (LS027B7DH01 400x240 and kin) through Adafruit_SharpMem:
// 1-bit, reflective, keeps its picture at a few uA. Drawn by EinkGfxDisplay,
// like the e-ink panels, but a frame costs one SPI write (~50 ms at 2 MHz for
// 400x240, only the rows that changed are sent), not a refresh: so isEink() stays false and the UI animates as on
// an OLED.
//
// The panel wants its VCOM polarity flipped about once a second or the liquid
// crystal takes a DC bias (fades, burns in). Without an EXTCOMIN pin that is a
// bit in each SPI command: every frame sends one, idle() a bare one when no
// frame has gone out for a while.

#ifndef SHARP_W
  #define SHARP_W 400
#endif
#ifndef SHARP_H
  #define SHARP_H 240
#endif
#ifndef SHARP_SPI_HZ
  #define SHARP_SPI_HZ 2000000   // the LS027B7DH01's rated maximum
#endif
#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#define SHARP_DISP_W ((DISPLAY_ROTATION & 1) ? SHARP_H : SHARP_W)
#define SHARP_DISP_H ((DISPLAY_ROTATION & 1) ? SHARP_W : SHARP_H)

// The SPI bus: a classic ESP32's VSPI of its own, or with PIN_SHARED_SPI_MISO set the
// one bus the board shares with its radio and SD card (an ESP32-C6 has only
// the one): the Arduino SPI, begun with all three pins by whoever comes first.
struct SharpPanel {
#ifdef PIN_SHARED_SPI_MISO
  SPIClass& spi;
  Adafruit_SharpMem display;
  SharpPanel() : spi(SPI), display(&spi, PIN_DISPLAY_CS, SHARP_W, SHARP_H, SHARP_SPI_HZ) {}
#else
  SPIClass spi;
  Adafruit_SharpMem display;
  SharpPanel() : spi(VSPI), display(&spi, PIN_DISPLAY_CS, SHARP_W, SHARP_H, SHARP_SPI_HZ) {}
#endif
};

class SharpMemDisplay : private SharpPanel, public EinkGfxDisplay {
  bool _init = false;
  bool _isOn = false;
  bool _vcom = false;
  unsigned long _written_at = 0;
  uint8_t* _sent = nullptr;   // what the panel shows, to write only the rows that changed
  bool _sent_ok = false;      // false: the panel may differ (blanked, cleared), all rows go

  void toggleVcom();
  void writeLines(bool all);

public:
  SharpMemDisplay() : EinkGfxDisplay(SHARP_DISP_W, SHARP_DISP_H, display) {}

  bool begin();
  bool isEink() override { return false; }
  bool isOn() override { return _isOn; }
  void turnOn() override;
  void turnOff() override;
  void showPoweredOff() override;
  void clear() override;
  void setDisplayRotation(uint8_t rot) override;
  void endFrame() override;
  void idle() override;
#ifdef ENABLE_SCREENSHOT
  // Adafruit_SharpMem's buffer as it is: rows of SHARP_W pixels, LSB first,
  // 1 = white (tools/screenshot.py's type 3), in the panel's own orientation.
  const uint8_t* getBuffer() override { return display.getBuffer(); }
  uint16_t getBufferSize() override { return SHARP_W * SHARP_H / 8; }
  uint8_t getDisplayType() override { return 3; }
  int screenshotWidth() override { return SHARP_W; }
  int screenshotHeight() override { return SHARP_H; }
  uint8_t screenshotRotation() override { return DISPLAY_ROTATION; }
#endif
};
