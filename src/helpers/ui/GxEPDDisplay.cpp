#include "GxEPDDisplay.h"
#include "EinkGfxDisplayImpl.h"

#ifdef EXP_PIN_BACKLIGHT
  #include <PCA9557.h>
  extern PCA9557 expander;
#endif

#ifndef DISPLAY_ROTATION
  #define DISPLAY_ROTATION 0
#endif

#ifdef ESP32
  SPIClass SPI1 = SPIClass(FSPI);
#endif

bool GxEPDDisplay::begin() {
  display.epd2.selectSPI(SPI1, SPISettings(4000000, MSBFIRST, SPI_MODE0));
#ifdef ESP32
  SPI1.begin(PIN_DISPLAY_SCLK, PIN_DISPLAY_MISO, PIN_DISPLAY_MOSI, PIN_DISPLAY_CS);
#else
  SPI1.begin();
#endif
  display.init(115200, true, 2, false);
  // Runs ~every 1ms while _waitWhileBusy() polls the panel's BUSY pin, i.e.
  // for as long as a refresh blocks the main loop. See busyCallbackTrampoline
  // and callBusyPump()/setBusyPumpFn() (DisplayDriver.h).
  display.epd2.setBusyCallback(busyCallbackTrampoline, this);
  display.setRotation(DISPLAY_ROTATION);
  display.setPartialWindow(0, 0, display.width(), display.height());
  display.fillScreen(GxEPD_WHITE);
  display.display(true);
#if DISP_BACKLIGHT
  digitalWrite(DISP_BACKLIGHT, LOW);
  pinMode(DISP_BACKLIGHT, OUTPUT);
#endif
  _init = true;
  return true;
}

void GxEPDDisplay::turnOn() {
  if (!_init) begin();
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, HIGH);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, HIGH);
#endif
  _isOn = true;
}

void GxEPDDisplay::turnOff() {
#if defined(DISP_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  digitalWrite(DISP_BACKLIGHT, LOW);
#elif defined(EXP_PIN_BACKLIGHT) && !defined(BACKLIGHT_BTN)
  expander.digitalWrite(EXP_PIN_BACKLIGHT, LOW);
#endif
  _isOn = false;
}

void GxEPDDisplay::clear() {
  display.fillScreen(GxEPD_WHITE);
  forceRedraw();
}

void GxEPDDisplay::setDisplayRotation(uint8_t rot) {
  display.setRotation(rot & 3);
  setDimensions(display.width(), display.height());
  updateLayout();
}

void GxEPDDisplay::endFrame() {
  if (frameChanged()) {
    bool partial = true;
    if (_full_refresh_interval > 0 && ++_partial_count >= _full_refresh_interval) {
      partial = false;
      _partial_count = 0;
    }
    // Drive every pixel, not just the ones that changed since the last frame.
    // A partial update is differential — it drives only what differs from the
    // controller's "previous image" RAM and leaves the rest to hold its own
    // charge, which this panel doesn't do well: text went grey a few updates
    // after it was drawn while whatever had just changed stayed crisp. Priming
    // that RAM with the inverse of the incoming frame makes every pixel a
    // difference, so all of them get driven to their target.
    //
    // It must be the inverse and not a flat white — white makes only
    // white->black a difference, so ink gets re-driven but never erased and
    // every screen ever shown accumulates as a ghost.
    //
    // Costs one extra full-screen RAM write (a few ms of SPI); the refresh
    // itself takes the same time either way, as the waveform clocks the whole
    // panel regardless of how many pixels it actually drives. Clearing ghosts
    // is a separate job and stays with the periodic full refresh above.
    if (partial) display.writeInverseForRedrive();
    display.display(partial);
  }
}
