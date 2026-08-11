#ifdef ST7789

#include "ST7789Display.h"

#ifdef OLED_MISC_FIXED_FONT
  #include "MiscFixedFont.h"
  #include "LemonIcons.h"
#endif

#ifndef X_OFFSET
#define X_OFFSET 0  // No offset needed for landscape
#endif

#ifndef Y_OFFSET
#define Y_OFFSET 1  // Vertical offset to prevent top row cutoff
#endif

#ifdef HELTEC_VISION_MASTER_T190
  #define SCALE_X  2.5f        // 320 / 128
  #define SCALE_Y  2.65625f    // 170 / 64
#elif defined(CARDPUTER_ADV)
  #define SCALE_X  1.875f      // 240 / 128
  #define SCALE_Y  2.109375f   // 135 / 64
#else
  #define SCALE_X  1.875f      // 240 / 128
  #define SCALE_Y  2.109375f   // 135 / 64
#endif

#ifdef DISPLAY_SCALE_X
  #undef SCALE_X
  #define SCALE_X DISPLAY_SCALE_X
#endif

#ifdef DISPLAY_SCALE_Y
  #undef SCALE_Y
  #define SCALE_Y DISPLAY_SCALE_Y
#endif

#ifdef OLED_MISC_FIXED_FONT
// MiscFixedFont.h/LemonIcons.h store each glyph's rows bit-packed
// *continuously* (Adafruit_GFX's native GFXfont convention -- no per-row byte
// padding), but drawXbm() below expects classic XBM packing (each row padded
// to a whole byte). Re-pack into a small stack buffer so the glyph can be
// blitted through drawXbm() -- which already implements the correct
// logical->physical scaling for this panel's non-integer SCALE_X/Y (via
// per-pixel boundary differences), instead of duplicating that math here.
static bool repackGlyphToXbm(const uint8_t* bitmapProgmem, uint16_t byteOffset, uint8_t w, uint8_t h, uint8_t* out, uint8_t outCap) {
  uint8_t widthInBytes = (w + 7) / 8;
  uint16_t needed = (uint16_t)widthInBytes * h;
  if (needed == 0 || needed > outCap) return false;
  memset(out, 0, needed);
  uint16_t srcBit = 0;
  for (uint8_t row = 0; row < h; row++) {
    for (uint8_t col = 0; col < w; col++) {
      uint16_t byteIdx = byteOffset + (srcBit >> 3);
      uint8_t  bitMask  = 0x80 >> (srcBit & 7);
      if (pgm_read_byte(bitmapProgmem + byteIdx) & bitMask) {
        out[row * widthInBytes + (col >> 3)] |= (0x80 >> (col & 7));
      }
      srcBit++;
    }
  }
  return true;
}

uint8_t ST7789Display::glyphXAdvance(uint32_t cp) {
  for (uint8_t i = 0; i < lemonIconCount; i++)
    if (pgm_read_dword(&lemonIconCPs[i]) == cp) return pgm_read_byte(&lemonIconGlyphs[i].xAdvance);
  if (cp < MiscFixed.first || cp > MiscFixed.last) return 6;
  return pgm_read_byte(&MiscFixedGlyphs[cp - MiscFixed.first].xAdvance);
}
#endif

bool ST7789Display::begin() {
  if(!_isOn) {
    pinMode(PIN_TFT_VDD_CTL, OUTPUT);
    pinMode(PIN_TFT_LEDA_CTL, OUTPUT);
    digitalWrite(PIN_TFT_VDD_CTL, LOW);
  #ifdef PIN_TFT_LEDA_CTL_ACTIVE
    digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
  #else
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  #endif
    digitalWrite(PIN_TFT_RST, HIGH);

    display.init();
    display.landscapeScreen();
    #ifdef DISPLAY_FLIP_VERTICALLY
    display.flipScreenVertically();
    #endif
    display.displayOn();
    setCursor(0,0);

    _isOn = true;
  }
  return true;
}

void ST7789Display::turnOn() {
  if (!_isOn) {
    // Restore power to the display but keep backlight off
    digitalWrite(PIN_TFT_VDD_CTL, LOW);
    digitalWrite(PIN_TFT_RST, HIGH);
    
    // Re-initialize the display
    display.init();
    display.displayOn();
    #ifdef DISPLAY_FLIP_VERTICALLY
    display.flipScreenVertically();
    #endif
    delay(20);

    // Now turn on the backlight
  #ifdef PIN_TFT_LEDA_CTL_ACTIVE
    digitalWrite(PIN_TFT_LEDA_CTL, PIN_TFT_LEDA_CTL_ACTIVE);
  #else
    digitalWrite(PIN_TFT_LEDA_CTL, LOW);
  #endif    
    _isOn = true;
  }
}

void ST7789Display::turnOff() {
  digitalWrite(PIN_TFT_VDD_CTL, HIGH);
#ifdef PIN_TFT_LEDA_CTL_ACTIVE
  digitalWrite(PIN_TFT_LEDA_CTL, !PIN_TFT_LEDA_CTL_ACTIVE);
#else
  digitalWrite(PIN_TFT_LEDA_CTL, HIGH);
#endif
  digitalWrite(PIN_TFT_RST, LOW);
  _isOn = false;
}

void ST7789Display::clear() {
  display.clear();
}

void ST7789Display::startFrame(Color bkg) {
  display.clear();
  _color = ST77XX_WHITE;
  display.setRGB(_color);
  display.setFont(ArialMT_Plain_16);
}

void ST7789Display::setTextSize(int sz) {
  switch(sz) {
    case 1 :
      display.setFont(ArialMT_Plain_16);
      break;
    case 2 :
      display.setFont(ArialMT_Plain_24);
      break;
    default:
      display.setFont(ArialMT_Plain_16);
  }
}

void ST7789Display::setColor(Color c) {
  switch (c) {
    case DisplayDriver::DARK :
      _color = ST77XX_BLACK;
      display.setColor(OLEDDISPLAY_COLOR::BLACK);
      break;
#if 0
    case DisplayDriver::LIGHT :
      _color = ST77XX_WHITE;
      break;
    case DisplayDriver::RED :
      _color = ST77XX_RED;
      break;
    case DisplayDriver::GREEN :
      _color = ST77XX_GREEN;
      break;
    case DisplayDriver::BLUE :
      _color = ST77XX_BLUE;
      break;
    case DisplayDriver::YELLOW :
      _color = ST77XX_YELLOW;
      break;
    case DisplayDriver::ORANGE :
      _color = ST77XX_ORANGE;
      break;
#endif
    default:
      _color = ST77XX_WHITE;
      display.setColor(OLEDDISPLAY_COLOR::WHITE);
      break;
  }
  display.setRGB(_color);
}

void ST7789Display::setCursor(int x, int y) {
#ifdef OLED_MISC_FIXED_FONT
  _lx = x;
  _ly = y;
#endif
  _x = x*SCALE_X + X_OFFSET;
  _y = y*SCALE_Y + Y_OFFSET;
}

void ST7789Display::print(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  int lx = _lx, ly = _ly;
  uint8_t glyphBuf[16];
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp = DisplayDriver::decodeCodepoint(p);
    if (cp == '\n') { ly += MiscFixed.yAdvance; lx = _lx; continue; }

    bool drawn = false;
    for (uint8_t i = 0; i < lemonIconCount; i++) {
      if (pgm_read_dword(&lemonIconCPs[i]) != cp) continue;
      const GFXglyph* g = &lemonIconGlyphs[i];
      uint8_t gw = pgm_read_byte(&g->width), gh = pgm_read_byte(&g->height);
      int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
      uint16_t bo = pgm_read_word(&g->bitmapOffset);
      if (repackGlyphToXbm(lemonIconBitmaps, bo, gw, gh, glyphBuf, sizeof(glyphBuf)))
        drawXbm(lx + xo, ly + 6 + yo, glyphBuf, gw, gh);
      lx += pgm_read_byte(&g->xAdvance);
      drawn = true;
      break;
    }
    if (drawn) continue;

    if (cp < MiscFixed.first || cp > MiscFixed.last) {
      if (cp >= 0x20) fillRect(lx + 1, ly, 4, 6);
      lx += 6;
      continue;
    }

    const GFXglyph* g = &MiscFixedGlyphs[cp - MiscFixed.first];
    uint8_t gw = pgm_read_byte(&g->width), gh = pgm_read_byte(&g->height);
    int8_t  xo = (int8_t)pgm_read_byte(&g->xOffset), yo = (int8_t)pgm_read_byte(&g->yOffset);
    uint16_t bo = pgm_read_word(&g->bitmapOffset);
    if (repackGlyphToXbm(MiscFixedBitmaps, bo, gw, gh, glyphBuf, sizeof(glyphBuf)))
      drawXbm(lx + xo, ly + 7 + yo, glyphBuf, gw, gh);
    lx += pgm_read_byte(&g->xAdvance);
  }
#else
  display.drawString(_x, _y, str);
#endif
}

void ST7789Display::printWordWrap(const char* str, int max_width) {
  display.drawStringMaxWidth(_x, _y, max_width*SCALE_X, str);
}

void ST7789Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x*SCALE_X + X_OFFSET, y*SCALE_Y + Y_OFFSET, w*SCALE_X, h*SCALE_Y);
}

void ST7789Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x*SCALE_X + X_OFFSET, y*SCALE_Y + Y_OFFSET, w*SCALE_X, h*SCALE_Y);
}

void ST7789Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  // Calculate the base position in display coordinates
  uint16_t startX = x * SCALE_X + X_OFFSET;
  uint16_t startY = y * SCALE_Y + Y_OFFSET;
  
  // Width in bytes for bitmap processing
  uint16_t widthInBytes = (w + 7) / 8;
  
  // Process the bitmap row by row
  for (uint16_t by = 0; by < h; by++) {
    // Calculate the target y-coordinates for this logical row
    int y1 = startY + (int)(by * SCALE_Y);
    int y2 = startY + (int)((by + 1) * SCALE_Y);
    int block_h = y2 - y1;
    
    // Scan across the row bit by bit
    for (uint16_t bx = 0; bx < w; bx++) {
      // Calculate the target x-coordinates for this logical column
      int x1 = startX + (int)(bx * SCALE_X);
      int x2 = startX + (int)((bx + 1) * SCALE_X);
      int block_w = x2 - x1;
      
      // Get the current bit
      uint16_t byteOffset = (by * widthInBytes) + (bx / 8);
      uint8_t bitMask = 0x80 >> (bx & 7);
      bool bitSet = pgm_read_byte(bits + byteOffset) & bitMask;
      
      // If the bit is set, draw a block of pixels
      if (bitSet) {
        // Draw the block as a filled rectangle
        display.fillRect(x1, y1, block_w, block_h);
      }
    }
  }
}

uint16_t ST7789Display::getTextWidth(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  uint16_t w = 0;
  const uint8_t* p = (const uint8_t*)str;
  while (*p) w += glyphXAdvance(DisplayDriver::decodeCodepoint(p));
  return w;
#else
  return display.getStringWidth(str) / SCALE_X;
#endif
}

void ST7789Display::endFrame() {
  display.display();
}

#endif