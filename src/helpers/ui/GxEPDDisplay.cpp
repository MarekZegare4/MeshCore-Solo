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

// SPI clock for the panel. 4 MHz is the conservative default every board has
// always used; a short FPC run can go faster (SSD16xx-family controllers are
// rated ~20 MHz for writes). On nRF52840 the Adafruit core puts SPI1 on SPIM2,
// which caps at 8 MHz -- higher requests are silently clamped to 8 MHz there.
#ifndef EINK_SPI_HZ
  #define EINK_SPI_HZ 4000000
#endif

// Color scheme
ColorVal UIColor::window_bkg = GxEPD_WHITE;
ColorVal UIColor::title_bkg = GxEPD_WHITE;
ColorVal UIColor::title_txt = GxEPD_BLACK;
ColorVal UIColor::primary_txt = GxEPD_BLACK;
ColorVal UIColor::secondary_txt = GxEPD_BLACK;
ColorVal UIColor::warning_txt = GxEPD_BLACK;
ColorVal UIColor::popup_bkg = GxEPD_WHITE;
ColorVal UIColor::popup_txt = GxEPD_BLACK;
ColorVal UIColor::corp_blue = GxEPD_BLACK;

bool GxEPDDisplay::begin() {
  display.epd2.selectSPI(SPI1, SPISettings(EINK_SPI_HZ, MSBFIRST, SPI_MODE0));
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
  _awake = true;
  _refreshed_at = millis();
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

// The last frame the panel will show, then the controller into deep sleep. Only
// a shutdown gets here: a power switch cuts the board with no warning. A full
// refresh, for a clean page, with no radio to serve during its busy wait.
void GxEPDDisplay::showPoweredOff() {
  if (!_init) return;
  setBusyPumpFn(nullptr, nullptr);
  startFrame(GxEPD_WHITE);
  setTextSize(1);
  drawTextCentered(width() / 2, (height() - getLineHeight()) / 2, "Powered off");
  display.display(false);
  display.hibernate();
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
    // A full refresh ends with the controller turning the high voltage off
    // (GxEPD2's _Update_Full, 0x22 0xF7), and on this panel a picture left
    // without it fades to grey within a while -- a full one too, not only a
    // partial: the full refresh clears what earlier screens left behind, it
    // doesn't make the picture keep. Seen on 0213BN-B74 "V4" panels; others
    // keep it, as the maker's datasheet says (deep sleep after each refresh).
    // A colder forced temperature (0x1A, a longer OTP waveform, down to 0 C)
    // didn't help. So, unless the panel is to sleep anyway
    // (Panel sleep, idle()), the same frame once more as a redriven partial,
    // which leaves the voltage on as after any other frame.
    if (!partial && !_panel_sleep) {
      display.writeInverseForRedrive();
      display.display(true);
    }
    _awake = true;
    _refreshed_at = millis();
  }
}

// Panel sleep on: a second after the last refresh the controller goes into deep
// sleep (the maker's flow: power off after each refresh). The picture stays on a
// panel that keeps it without voltage, nothing is held on the glass for hours
// (no burn-in), and the booster stops drawing; the next frame's write resets and
// re-inits it on its own. The second keeps scrolling through a list awake.
void GxEPDDisplay::idle() {
  static const unsigned long SLEEP_AFTER_MS = 1000;
  if (_panel_sleep && _awake && millis() - _refreshed_at >= SLEEP_AFTER_MS) {
    _awake = false;
    display.hibernate();
  }
}
