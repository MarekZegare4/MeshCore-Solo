#pragma once

#include <SPI.h>
#include <Adafruit_SharpMem.h>
#include "EinkGfxDisplay.h"

// Sharp Memory LCD (LS027B7DH01 400x240 and kin) through Adafruit_SharpMem:
// 1-bit, reflective, keeps its picture at a few uA. Drawn by EinkGfxDisplay,
// like the e-ink panels, but a frame costs one SPI write (~50 ms at 2 MHz for
// 400x240), not a refresh: so isEink() stays false and the UI animates as on
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

struct SharpPanel {
  SPIClass spi;
  Adafruit_SharpMem display;
  SharpPanel() : spi(VSPI), display(&spi, PIN_DISPLAY_CS, SHARP_W, SHARP_H, SHARP_SPI_HZ) {}
};

class SharpMemDisplay : private SharpPanel, public EinkGfxDisplay {
  bool _init = false;
  bool _isOn = false;
  bool _vcom = false;
  unsigned long _written_at = 0;

  void toggleVcom();

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
};
