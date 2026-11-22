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
#ifdef PIN_SHARED_SPI_MISO
  spi.begin(PIN_DISPLAY_SCLK, PIN_SHARED_SPI_MISO, PIN_DISPLAY_MOSI, -1);   // shared: MISO too, for the others on it
#else
  spi.begin(PIN_DISPLAY_SCLK, -1, PIN_DISPLAY_MOSI, -1);
#endif
  if (!display.begin()) return false;   // allocates the 12 KB frame buffer
  if (!_sent) _sent = (uint8_t*)malloc(SHARP_W * SHARP_H / 8);   // none: every frame whole
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
    writeLines(true);
    _written_at = millis();
  }
  _isOn = false;
}

void SharpMemDisplay::showPoweredOff() {
  if (!_init) return;
  startFrame(SHARP_WHITE);
  setTextSize(1);
  drawTextCentered(width() / 2, (height() - getLineHeight()) / 2, "Powered off");
  writeLines(true);
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

// The rows that differ from what the panel shows, in one write command (each
// row: its address from 1, the pixels, a trailer byte; one more trailer at the
// end) -- a turning picture that fills a quarter of the screen costs a
// quarter of a frame. `all`: every row, whatever was sent before.
void SharpMemDisplay::writeLines(bool all) {
  const int bpl = SHARP_W / 8;
  const uint8_t* buf = display.getBuffer();
  if (!buf) return;
  bool open = false;
  for (int row = 0; row < SHARP_H; row++) {
    const uint8_t* src = buf + row * bpl;
    if (!all && _sent_ok && _sent && memcmp(src, _sent + row * bpl, bpl) == 0) continue;
    if (!open) {
      _vcom = !_vcom;
      spi.beginTransaction(SPISettings(SHARP_SPI_HZ, LSBFIRST, SPI_MODE0));
      digitalWrite(PIN_DISPLAY_CS, HIGH);
      spi.transfer(0x01 | (_vcom ? 0x02 : 0x00));   // write, with VCOM
      open = true;
    }
    uint8_t line[bpl + 2];
    line[0] = (uint8_t)(row + 1);
    memcpy(line + 1, src, bpl);
    line[bpl + 1] = 0x00;
    spi.transfer(line, bpl + 2);
    if (_sent) memcpy(_sent + row * bpl, src, bpl);
  }
  if (open) {
    spi.transfer(0x00);
    digitalWrite(PIN_DISPLAY_CS, LOW);
    spi.endTransaction();
    _written_at = millis();
  }
  _sent_ok = _sent != nullptr;
}

void SharpMemDisplay::endFrame() {
  if (!frameChanged()) return;
  writeLines(false);
}

void SharpMemDisplay::idle() {
  if (_init && millis() - _written_at >= SHARP_VCOM_MS) toggleVcom();
}
