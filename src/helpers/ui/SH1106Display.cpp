#include "SH1106Display.h"
#include <Adafruit_GrayOLED.h>
#include "Adafruit_SH110X.h"
#include "LemonFont.h"
#include "LemonIcons.h"

bool SH1106Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SH1106Display::begin()
{
  return display.begin(DISPLAY_ADDRESS, true) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void SH1106Display::turnOn()
{
  display.oled_command(SH110X_DISPLAYON);
  uint8_t pre[] = { 0xD9, _precharge };
  display.oled_commandList(pre, 2);
  display.setContrast(_contrast);
  _isOn = true;
}

void SH1106Display::turnOff()
{
  display.oled_command(SH110X_DISPLAYOFF);
  _isOn = false;
}

void SH1106Display::clear()
{
  display.clearDisplay();
  display.display();
}

void SH1106Display::startFrame(Color bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1106Display::setTextSize(int sz)
{
  _text_size = (uint8_t)sz;
  if (sz == 1) {
    display.setFont(nullptr);  // use default font path; we override print() ourselves
    display.setTextSize(1);
  } else {
    display.setFont(nullptr);
    display.setTextSize(sz);
  }
}

void SH1106Display::setColor(Color c)
{
  _color = (c != 0) ? SH110X_WHITE : SH110X_BLACK;
  display.setTextColor(_color);
}

void SH1106Display::setCursor(int x, int y)
{
  display.setCursor(x, y);
}

// Decode one UTF-8 codepoint from *p, advancing p past it.
uint32_t SH1106Display::decodeUtf8(const uint8_t*& p) {
  uint8_t c = *p++;
  if (c < 0x80) return c;
  if ((c & 0xE0) == 0xC0) {
    uint32_t cp = c & 0x1F;
    if (*p) cp = (cp << 6) | (*p++ & 0x3F);
    return cp;
  }
  if ((c & 0xF0) == 0xE0) {
    uint32_t cp = c & 0x0F;
    if (*p) cp = (cp << 6) | (*p++ & 0x3F);
    if (*p) cp = (cp << 6) | (*p++ & 0x3F);
    return cp;
  }
  // 4-byte and invalid — skip continuation bytes
  while (*p && (*p & 0xC0) == 0x80) p++;
  return '?';
}

// Draw one Lemon glyph at (x, y), return new cursor x.
int16_t SH1106Display::drawLemonChar(int16_t x, int16_t y, uint32_t cp) {
  // Check icon table for Private Use Area codepoints
  for (uint8_t i = 0; i < lemonIconCount; i++) {
    if (pgm_read_dword(&lemonIconCPs[i]) == cp) {
      const GFXglyph* g = &lemonIconGlyphs[i];
      uint8_t  w  = pgm_read_byte(&g->width);
      uint8_t  h  = pgm_read_byte(&g->height);
      int8_t   xo = (int8_t)pgm_read_byte(&g->xOffset);
      int8_t   yo = (int8_t)pgm_read_byte(&g->yOffset);
      uint8_t  xa = pgm_read_byte(&g->xAdvance);
      uint16_t bo = pgm_read_word(&g->bitmapOffset);
      uint8_t bits = 0, bit = 0;
      for (uint8_t row = 0; row < h; row++)
        for (uint8_t col = 0; col < w; col++) {
          if (!bit) { bits = pgm_read_byte(&lemonIconBitmaps[bo++]); bit = 0x80; }
          if (bits & bit) display.drawPixel(x + xo + col, y + 6 + yo + row, _color);
          bit >>= 1;
        }
      return x + xa;
    }
  }

  if (cp < Lemon.first || cp > Lemon.last) {
    // outside font range — use default advance
    return x + 6;
  }
  const GFXglyph* glyph = &lemonGlyphs[cp - Lemon.first];
  uint8_t w   = pgm_read_byte(&glyph->width);
  uint8_t h   = pgm_read_byte(&glyph->height);
  int8_t  xo  = (int8_t)pgm_read_byte(&glyph->xOffset);
  int8_t  yo  = (int8_t)pgm_read_byte(&glyph->yOffset);
  uint8_t xa  = pgm_read_byte(&glyph->xAdvance);
  uint16_t bo = pgm_read_word(&glyph->bitmapOffset);

  uint8_t bits = 0, bit = 0;
  for (uint8_t row = 0; row < h; row++) {
    for (uint8_t col = 0; col < w; col++) {
      if (!bit) { bits = pgm_read_byte(&lemonBitmaps[bo++]); bit = 0x80; }
      if (bits & bit)
        display.drawPixel(x + xo + col, y + 6 + yo + row, _color);  // +6 = cap height: cursor y aligns with top of uppercase/digit glyphs
      bit >>= 1;
    }
  }
  return x + xa;
}

uint8_t SH1106Display::lemonXAdvance(uint32_t cp) {
  if (cp < Lemon.first || cp > Lemon.last) return 6;
  const GFXglyph* glyph = &lemonGlyphs[cp - Lemon.first];
  return pgm_read_byte(&glyph->xAdvance);
}

void SH1106Display::print(const char *str)
{
  if (_text_size != 1) { display.print(str); return; }

  int16_t cx = display.getCursorX();
  int16_t cy = display.getCursorY();
  const uint8_t* p = (const uint8_t*)str;
  while (*p) {
    uint32_t cp = decodeUtf8(p);
    if (cp == '\n') {
      cy += Lemon.yAdvance;
      cx = 0;
    } else {
      cx = drawLemonChar(cx, cy, cp);
    }
  }
  display.setCursor(cx, cy);
}

void SH1106Display::fillRect(int x, int y, int w, int h)
{
  display.fillRect(x, y, w, h, _color);
}

void SH1106Display::drawRect(int x, int y, int w, int h)
{
  display.drawRect(x, y, w, h, _color);
}

void SH1106Display::drawXbm(int x, int y, const uint8_t *bits, int w, int h)
{
  display.drawBitmap(x, y, bits, w, h, SH110X_WHITE);
}

uint16_t SH1106Display::getTextWidth(const char *str)
{
  if (_text_size != 1) {
    int16_t x1, y1; uint16_t w, h;
    display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
    return w;
  }
  uint16_t width = 0;
  const uint8_t* p = (const uint8_t*)str;
  while (*p) width += lemonXAdvance(decodeUtf8(p));
  return width;
}

void SH1106Display::setBrightness(uint8_t level)
{
  // Contrast alone has limited effect on some OLED panels; combining with
  // pre-charge period (0xD9) gives a wider perceptible dimming range.
  // Pre-charge 0x11 = phase1=1,phase2=1 (minimum drive); 0x1F = default.
  static const uint8_t contrast_values[]  = {   0,  25,  60, 150, 255 };
  static const uint8_t precharge_values[] = { 0x11, 0x15, 0x1F, 0x1F, 0x1F };
  uint8_t idx = level < 5 ? level : 4;
  _contrast  = contrast_values[idx];
  _precharge = precharge_values[idx];
  uint8_t pre[] = { 0xD9, _precharge };
  display.oled_commandList(pre, 2);
  display.setContrast(_contrast);
}

void SH1106Display::endFrame()
{
  display.display();
}
