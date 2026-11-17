#include "SharpMemDisplay.h"
#include "EinkGfxDisplayImpl.h"

#define SHARP_BLACK 0
#define SHARP_WHITE 1

// Without a frame for this long, idle() flips VCOM on its own (the panel
// wants it at about 1 Hz).
#define SHARP_VCOM_MS 500

ColorVal UIColor::window_bkg = SHARP_WHITE;
ColorVal UIColor::title_bkg = SHARP_WHITE;
ColorVal UIColor::title_txt = SHARP_BLACK;
ColorVal UIColor::primary_txt = SHARP_BLACK;
ColorVal UIColor::secondary_txt = SHARP_BLACK;
ColorVal UIColor::warning_txt = SHARP_BLACK;
ColorVal UIColor::popup_bkg = SHARP_WHITE;
ColorVal UIColor::popup_txt = SHARP_BLACK;
ColorVal UIColor::corp_blue = SHARP_BLACK;

bool SharpMemDisplay::begin() {
  spi.begin(PIN_DISPLAY_SCLK, -1, PIN_DISPLAY_MOSI, -1);
  if (!display.begin()) return false;   // allocates the 12 KB frame buffer
  display.setRotation(DISPLAY_ROTATION);
  display.clearDisplay();
  _written_at = millis();
  _init = true;
  return true;
}

// The "maintain" command: mode 0x00 with the VCOM bit, then a trailer byte.
// The panel latches on CS high (active high, unlike most SPI parts).
void SharpMemDisplay::toggleVcom() {
  _vcom = !_vcom;
  spi.beginTransaction(SPISettings(SHARP_SPI_HZ, LSBFIRST, SPI_MODE0));
  digitalWrite(PIN_DISPLAY_CS, HIGH);
  spi.transfer(_vcom ? 0x02 : 0x00);
  spi.transfer(0x00);
  digitalWrite(PIN_DISPLAY_CS, LOW);
  spi.endTransaction();
  _written_at = millis();
}

void SharpMemDisplay::turnOn() {
  if (!_init) begin();
  if (!_isOn) forceRedraw();   // turnOff() blanked the panel: the next frame goes out whatever its hash
  _isOn = true;
}

// Off blanks the panel. It would hold the last picture at a few uA, but the UI
// stops drawing while off and takes the next press as a wake: a screen that
// still looked on then seemed to ignore the first press and show a stale clock.
void SharpMemDisplay::turnOff() {
  if (_init && _isOn) {
    display.fillScreen(SHARP_WHITE);   // a white frame, written as any other
    display.refresh();
    _written_at = millis();
  }
  _isOn = false;
}

void SharpMemDisplay::showPoweredOff() {
  if (!_init) return;
  startFrame(SHARP_WHITE);
  setTextSize(1);
  drawTextCentered(width() / 2, (height() - getLineHeight()) / 2, "Powered off");
  display.refresh();
}

void SharpMemDisplay::clear() {
  display.fillScreen(SHARP_WHITE);
  forceRedraw();
}

void SharpMemDisplay::setDisplayRotation(uint8_t rot) {
  display.setRotation(rot & 3);
  setDimensions(display.width(), display.height());
  updateLayout();
}

void SharpMemDisplay::endFrame() {
  if (!frameChanged()) return;
  display.refresh();   // the whole buffer; flips VCOM as it goes
  _written_at = millis();
}

void SharpMemDisplay::idle() {
  if (_init && millis() - _written_at >= SHARP_VCOM_MS) toggleVcom();
}
