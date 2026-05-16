// Lemon font icon glyphs (Private Use Area), converted manually from lemon.bdf
// Add new icons by appending to the bitmap array, lemonIconCPs, and lemonIconGlyphs.
#pragma once
#include <Adafruit_GFX.h>

static const uint8_t lemonIconBitmaps[] PROGMEM = {
  0xEE, 0xBA, 0x2B, 0xA3, 0xB0,  // U+E03B mute: 6×6, yoff=0
};

static const uint32_t lemonIconCPs[] PROGMEM = {
  0xE03B,  // mute
};

static const GFXglyph lemonIconGlyphs[] PROGMEM = {
  //  off   w   h  adv  xo   yo
  {  0,   6,  6,   5,   0,  -6 },  // U+E03B mute
};

static const uint8_t lemonIconCount = sizeof(lemonIconCPs) / sizeof(lemonIconCPs[0]);
