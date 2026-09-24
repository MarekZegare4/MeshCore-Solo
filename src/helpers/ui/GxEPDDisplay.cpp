#include "GxEPDDisplay.h"
#include "LemonIcons.h"

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

// GFX fonts use the baseline as the cursor origin. UI code assumes top-of-cell
// coordinates (same convention as the OLED driver). Add the font ascender so
// the two conventions match.
static int fontAscender(int sz, bool single_font, int scale) {
  if (sz == 3) return 26;                       // FreeSans18pt7b: proportional, baseline origin
  if (sz == 1 && single_font) return 7 * scale;   // misc-fixed 6x9 GFX font: baseline origin, ascent 7px×scale
  return 0;                                     // GFX built-in font: cursor is top-left of cell
}

// y is the GFX baseline (display.getCursorY()), which equals original_y + 8*sc.
// Pixels are placed at y + yo*sc + row*sc — identical to how GFX would render
// a scaled GFX font, but bypassing GFX so multi-byte UTF-8 is decoded correctly.
int16_t GxEPDDisplay::drawGlyph(int16_t x, int16_t y, uint32_t cp, int sc) {
  for (uint8_t i = 0; i < lemonIconCount; i++) {
    if (pgm_read_dword(&lemonIconCPs[i]) == cp) {
      const GFXglyph* g = &lemonIconGlyphs[i];
      uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
      int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
      uint8_t xa = pgm_read_byte(&g->xAdvance);
      uint16_t bo = pgm_read_word(&g->bitmapOffset);
      uint8_t bits = 0, bit = 0;
      for (uint8_t row = 0; row < h; row++)
        for (uint8_t col = 0; col < w; col++) {
          if (!bit) { bits = pgm_read_byte(&lemonIconBitmaps[bo++]); bit = 0x80; }
          if (bits & bit) {
            if (sc == 1) display.drawPixel(x + xo + col, y + yo + row, _curr_color);
            else display.fillRect(x + xo*sc + col*sc, y + yo*sc + row*sc, sc, sc, _curr_color);
          }
          bit >>= 1;
        }
      return x + xa * sc;
    }
  }
  if (cp < MiscFixed.first || cp > MiscFixed.last) {
    if (cp >= 0x20) display.fillRect(x + sc, y - 7*sc, 4*sc, 6*sc, _curr_color);
    return x + 6 * sc;
  }
  const GFXglyph* g = &MiscFixedGlyphs[cp - MiscFixed.first];
  uint8_t w = pgm_read_byte(&g->width), h = pgm_read_byte(&g->height);
  int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
  uint8_t xa = pgm_read_byte(&g->xAdvance);
  uint16_t bo = pgm_read_word(&g->bitmapOffset);
  uint8_t bits = 0, bit = 0;
  for (uint8_t row = 0; row < h; row++)
    for (uint8_t col = 0; col < w; col++) {
      if (!bit) { bits = pgm_read_byte(&MiscFixedBitmaps[bo++]); bit = 0x80; }
      if (bits & bit) {
        if (sc == 1) display.drawPixel(x + xo + col, y + yo + row, _curr_color);
        else display.fillRect(x + xo*sc + col*sc, y + yo*sc + row*sc, sc, sc, _curr_color);
      }
      bit >>= 1;
    }
  return x + xa * sc;
}

uint8_t GxEPDDisplay::glyphXAdvance(uint32_t cp, int sc) {
  uint8_t xa;
  if (cp < MiscFixed.first || cp > MiscFixed.last) xa = 6;
  else xa = pgm_read_byte(&MiscFixedGlyphs[cp - MiscFixed.first].xAdvance);
  return xa * sc;
}

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
  setTextSize(1);
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
  display.setTextColor(GxEPD_BLACK);
  display_crc.reset();
}

void GxEPDDisplay::startFrame(Color bkg) {
  display.fillScreen(GxEPD_WHITE);
  display.setTextColor(_curr_color = GxEPD_BLACK);
  _text_sz = 1;
  int sc = scale();
  display.setFont(_single_font ? &MiscFixed : NULL);
  display.setTextSize(sc);
  display_crc.reset();
}

void GxEPDDisplay::setTextSize(int sz) {
  _text_sz = sz;
  _vw_dirty = true;
  display_crc.update<int>(sz);
  // Size 1 scales with orientation: 1× in portrait (≈OLED width), 2× in landscape.
  // Size 2 always uses 2× built-in (12×16) — fixed because layout Y-positions are hardcoded.
  // Size 3 always uses FreeSans18pt for large headings.
  int sc = scale();
  switch (sz) {
    case 4:
      // Huge clock digits: built-in font scaled up. Cursor stays top-left
      // (fontAscender returns 0 for the built-in font), so layout maths is plain.
      display.setFont(NULL);
      display.setTextSize(BIG_TEXT_SCALE);
      break;
    case 3:
      display.setFont(&FreeSans18pt7b);
      display.setTextSize(1);
      break;
    case 2:
      display.setFont(NULL);
      display.setTextSize(scale() * 2);
      break;
    default:
      display.setFont(_single_font ? &MiscFixed : NULL);
      display.setTextSize(sc);
      break;
  }
}

void GxEPDDisplay::setColor(Color c) {
  display_crc.update<Color>(c);
  // e-ink: DARK background = white paper, LIGHT foreground = black ink
  if (c == DARK) {
    display.setTextColor(_curr_color = GxEPD_WHITE);
  } else {
    display.setTextColor(_curr_color = GxEPD_BLACK);
  }
}

void GxEPDDisplay::setCursor(int x, int y) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  // Offset y by the font ascender: callers pass top-of-cell y, GFX fonts
  // expect baseline y. Without this, text would be clipped at the top.
  int sc = scale();
  display.setCursor(x, y + fontAscender(_text_sz, _single_font, sc));
}

void GxEPDDisplay::print(const char* str) {
  display_crc.update<char>(str, strlen(str));
  // misc-fixed path only for sz=1 — setTextSize(2/3) switches GFX to other fonts.
  if (_single_font && _text_sz == 1) {
    int16_t cx = display.getCursorX();
    int16_t cy = display.getCursorY();
    const int sc = scale();
    const uint8_t* p = (const uint8_t*)str;
    while (*p) {
      uint32_t cp = decodeCodepoint(p);
      if (cp == '\n') { cy += MiscFixed.yAdvance * sc; cx = 0; }
      else            { cx = drawGlyph(cx, cy, cp, sc); }
    }
    display.setCursor(cx, cy);
    return;
  }
  int sc = scale();
  for (const char* p = str; *p; p++) {
    if ((uint8_t)*p == 0xDB) {
      int cx = display.getCursorX();
      int cy = display.getCursorY();
      // Default GFX font: cursor is top-left of cell (fontAscender=0), so cy=original_y.
      // Draw block at cy (not cy-8*sc — that formula assumes Lemon's baseline offset).
      display.fillRect(cx, cy, 5 * sc, 8 * sc, _curr_color);
      display.setCursor(cx + 6 * sc, cy);
    } else {
      char tmp[2] = {*p, 0};
      display.print(tmp);
    }
  }
}

void GxEPDDisplay::fillRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.fillRect(x, y, w, h, _curr_color);
}

void GxEPDDisplay::drawRect(int x, int y, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display.drawRect(x, y, w, h, _curr_color);
  if (scale() == 2 && w > 2 && h > 2)
    display.drawRect(x + 1, y + 1, w - 2, h - 2, _curr_color);
}

void GxEPDDisplay::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display_crc.update<int>(x);
  display_crc.update<int>(y);
  display_crc.update<int>(w);
  display_crc.update<int>(h);
  display_crc.update<uintptr_t>((uintptr_t)bits);
  uint16_t widthInBytes = (w + 7) / 8;
  for (uint16_t by = 0; by < h; by++) {
    for (uint16_t bx = 0; bx < w; bx++) {
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      if (pgm_read_byte(bits + byteOffset) & bitMask) {
        display.drawPixel(x + bx, y + by, _curr_color);
      }
    }
  }
}

uint16_t GxEPDDisplay::getTextWidth(const char* str) {
  if (_single_font && _text_sz == 1) {
    uint16_t total = 0;
    const int sc = scale();
    const uint8_t* p = (const uint8_t*)str;
    while (*p) total += glyphXAdvance(decodeCodepoint(p), sc);
    return total;
  }
  display.setTextWrap(false);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  display.setTextWrap(true);
  return w;
}

void GxEPDDisplay::setDisplayRotation(uint8_t rot) {
  display.setRotation(rot & 3);
  setDimensions(display.width(), display.height());
  last_display_crc_value = -1;  // force redraw on next endFrame
}

void GxEPDDisplay::endFrame() {
  uint32_t crc = display_crc.finalize();
  if (crc != last_display_crc_value) {
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
    last_display_crc_value = crc;
  }
}
