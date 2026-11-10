#include "SSD1306Display.h"
#ifdef OLED_MISC_FIXED_FONT
  #include "MiscFixedRenderer.h"
#endif

bool SSD1306Display::i2c_probe(TwoWire& wire, uint8_t addr) {
  wire.beginTransmission(addr);
  uint8_t error = wire.endTransmission();
  return (error == 0);
}

// Color scheme
ColorVal UIColor::window_bkg = SSD1306_BLACK;
ColorVal UIColor::title_bkg = SSD1306_BLACK;
ColorVal UIColor::title_txt = SSD1306_WHITE;
ColorVal UIColor::primary_txt = SSD1306_WHITE;
ColorVal UIColor::secondary_txt = SSD1306_WHITE;
ColorVal UIColor::warning_txt = SSD1306_WHITE;
ColorVal UIColor::popup_bkg = SSD1306_BLACK;
ColorVal UIColor::popup_txt = SSD1306_WHITE;
ColorVal UIColor::corp_blue = SSD1306_WHITE;

bool SSD1306Display::begin() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;
  }
  #ifdef DISPLAY_ROTATION
  display.setRotation(DISPLAY_ROTATION);
  #endif
  _force_redraw = true;
  return display.begin(SSD1306_SWITCHCAPVCC, DISPLAY_ADDRESS, true, false) && i2c_probe(Wire, DISPLAY_ADDRESS);
}

void SSD1306Display::turnOn() {
  if (!_isOn) {
    if (_peripher_power) _peripher_power->claim();
    _isOn = true;  // set before begin() to prevent double claim
    if (_peripher_power) begin();  // re-init display after power was cut
  }
  display.ssd1306_command(SSD1306_DISPLAYON);
  _force_redraw = true;   // panel was off -- guarantee the next endFrame() flushes
}

void SSD1306Display::turnOff() {
  display.ssd1306_command(SSD1306_DISPLAYOFF);
  if (_isOn) {
    if (_peripher_power) {
#if PIN_OLED_RESET >= 0
      digitalWrite(PIN_OLED_RESET, LOW);
#endif
      _peripher_power->release();
    }
    _isOn = false;
  }
}

void SSD1306Display::clear() {
  display.clearDisplay();
  display.display();
  _force_redraw = true;   // next endFrame() must flush even if its hash matches a pre-clear frame
}

void SSD1306Display::startFrame(ColorVal bkg) {
  _inv_want = false;
  display.clearDisplay();  // TODO: apply 'bkg'
  _color = SSD1306_WHITE;
  display.setTextColor(_color);
  display.setTextSize(1);
  display.cp437(true);         // Use full 256 char 'Code Page 437' font
}

void SSD1306Display::setTextSize(int sz) {
  _text_sz = sz;
  _vw_dirty = true;
  display.setTextSize(sz);
}

void SSD1306Display::setColor(ColorVal c) {
  _color = c;
  display.setTextColor(_color);
}

void SSD1306Display::setCursor(int x, int y) {
  display.setCursor(x, y);
}

#ifdef OLED_MISC_FIXED_FONT
uint8_t SSD1306Display::glyphXAdvance(uint32_t cp) {
  return miscFixedXAdvance(cp, _text_sz);
}
#endif

void SSD1306Display::print(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  miscFixedPrint(display, str, _text_sz, _color);
#else
  display.print(str);
#endif
}

void SSD1306Display::fillRect(int x, int y, int w, int h) {
  display.fillRect(x, y, w, h, _color);
}

void SSD1306Display::drawRect(int x, int y, int w, int h) {
  display.drawRect(x, y, w, h, _color);
}

void SSD1306Display::drawXbm(int x, int y, const uint8_t* bits, int w, int h) {
  display.drawBitmap(x, y, bits, w, h, SSD1306_WHITE);
}

uint16_t SSD1306Display::getTextWidth(const char* str) {
#ifdef OLED_MISC_FIXED_FONT
  return miscFixedTextWidth(str, _text_sz);
#else
  int16_t x1, y1;
  uint16_t w, h;
  display.getTextBounds(str, 0, 0, &x1, &y1, &w, &h);
  return w;
#endif
}

void SSD1306Paged::sendBlock(uint8_t page, uint8_t col, uint8_t len) {
  const uint8_t cmd[] = { SSD1306_PAGEADDR, page, page, SSD1306_COLUMNADDR, col, (uint8_t)(col + len - 1) };
  const uint8_t* ptr = buffer + (uint16_t)page * WIDTH + col;
  ssd1306_commandList(cmd, sizeof(cmd));
  while (len) {   // 32 data bytes a transfer fit every core's Wire buffer
    uint8_t n = len < 32 ? len : 32;
    wire->beginTransmission(i2caddr);
    wire->write((uint8_t)0x40);
    wire->write(ptr, n);
    wire->endTransmission();
    ptr += n; len -= n;
  }
}

void SSD1306Display::endFrame() {
  if (_inv_want != _inv_shown) {   // ahead of the frame-skip: an inversion alone sends no frame
    _inv_shown = _inv_want;
    display.invertDisplay(_inv_shown);
  }
  const uint8_t* buf = display.getBuffer();
  const uint8_t per_page = 128 / BLOCK_W;
  bool any = false;
  bool dirty[BLOCKS];
  for (uint8_t i = 0; i < BLOCKS; i++) {
    const uint8_t* q = buf + (uint16_t)i * BLOCK_W;   // blocks run along each page in buffer order
    uint32_t h = 2166136261u;
    for (uint8_t j = 0; j < BLOCK_W; j++) { h ^= q[j]; h *= 16777619u; }
    dirty[i] = _force_redraw || h != _block_hash[i];
    _block_hash[i] = h;
    any |= dirty[i];
  }
  if (!any) return;
  if (_force_redraw || !display.canSendBlocks()) {
    _force_redraw = false;
    display.display();   // whole frame; also resets the panel's address window
    return;
  }
  display.blocksBegin();
  for (uint8_t page = 0; page < 64 / 8; page++) {
    // runs of neighbouring dirty blocks go out as one transfer
    for (uint8_t b = 0; b < per_page; ) {
      if (!dirty[page * per_page + b]) { b++; continue; }
      uint8_t e = b;
      while (e + 1 < per_page && dirty[page * per_page + e + 1]) e++;
      display.sendBlock(page, b * BLOCK_W, (e - b + 1) * BLOCK_W);
      b = e + 1;
    }
  }
  display.blocksEnd();
}
