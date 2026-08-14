#include "SH1106Display.h"
#include <Adafruit_GrayOLED.h>
#include "Adafruit_SH110X.h"
#include "MiscFixedRenderer.h"

bool SH1106Display::i2c_probe(TwoWire &wire, uint8_t addr)
{
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

bool SH1106Display::begin()
{
  // Wire must already be initialised by board.begin() before this is called.
  // Boards with non-standard SH1106 addresses should define DISPLAY_ADDRESS
  // in their variant/platformio configuration. The SA0 strap selects 0x3C or
  // 0x3D and differs between revisions of the same board (e.g. T-Beam
  // Supreme), so fall back to the other address of the pair.
  uint8_t addr = 0;
  if (i2c_probe(Wire, DISPLAY_ADDRESS)) {
    addr = DISPLAY_ADDRESS;
  } else if (i2c_probe(Wire, DISPLAY_ADDRESS ^ 1)) {
    addr = DISPLAY_ADDRESS ^ 1;
  }
  // Run the Adafruit init even when no panel answered: it is what allocates
  // the frame buffer and the I2C device. Skipping it leaves i2c_dev and
  // spi_dev NULL, and UITask::begin() calls turnOn() regardless of our
  // return value, which then dereferences the null spi_dev.
  bool ok = display.begin(addr ? addr : DISPLAY_ADDRESS, true);
  return addr != 0 && ok;
}

void SH1106Display::turnOn()
{
  display.oled_command(SH110X_DISPLAYON);
  uint8_t pre[] = { 0xD9, _precharge };
  display.oled_commandList(pre, 2);
  display.setContrast(_contrast);
  _isOn = true;
  _force_redraw = true;   // panel was off — guarantee the next endFrame() flushes
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
  _force_redraw = true;   // next endFrame() must flush even if its CRC matches a pre-clear frame
}

void SH1106Display::startFrame(Color bkg)
{
  display.clearDisplay(); // TODO: apply 'bkg'
  _color = SH110X_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  _text_sz = 1;
  display.cp437(true); // Use full 256 char 'Code Page 437' font
}

void SH1106Display::setTextSize(int sz)
{
  _text_sz = sz;
  _vw_dirty = true;
  display.setTextSize(sz);
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

uint8_t SH1106Display::glyphXAdvance(uint32_t cp) {
  return miscFixedXAdvance(cp, _text_sz);
}

void SH1106Display::translateUTF8ToBlocks(char* dest, const char* src, size_t dest_size) {
  if (_single_font) {
    size_t n = strlen(src);
    if (n >= dest_size) n = dest_size - 1;
    memcpy(dest, src, n);
    dest[n] = '\0';
  } else {
    DisplayDriver::translateUTF8ToBlocks(dest, src, dest_size);
  }
}

void SH1106Display::print(const char *str)
{
  if (!_single_font) { display.print(str); return; }
  miscFixedPrint(display, str, _text_sz, _color);
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
  if (_single_font) return miscFixedTextWidth(str, _text_sz);
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
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
  // Skip the I²C flush when the frame is byte-identical to the last one pushed.
  // The most-shown screens (clock, home) are static between updates, so this
  // cuts redundant display() traffic and a little power. FNV-1a over the 1 KB
  // GFX buffer (~1k xor+mul, cheap); _force_redraw guarantees the first frame
  // and the frame after wake/clear.
  const uint8_t* buf = display.getBuffer();
  uint16_t n = (uint16_t)((width() * height()) / 8);
  uint32_t h = 2166136261u;
  for (uint16_t i = 0; i < n; i++) { h ^= buf[i]; h *= 16777619u; }
  if (!_force_redraw && h == _last_frame_hash) return;
  _force_redraw = false;
  _last_frame_hash = h;
  display.display();
}
