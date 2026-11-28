#pragma once

#include <stdint.h>
#ifdef ARDUINO
  #include <Arduino.h>   // PROGMEM, pgm_read_*
#endif
#ifndef PROGMEM
  #define PROGMEM
#endif
#ifndef pgm_read_word
  #define pgm_read_word(a) (*(const uint16_t*)(a))
#endif
#ifndef pgm_read_dword
  #define pgm_read_dword(a) (*(const uint32_t*)(a))
#endif

// A fixed-cell bitmap font (the misc-fixed sizes), as tools/bdf2mono.py writes
// it. Every glyph fills the whole cell, so the only thing that differs between
// codepoints is whether the font draws one at all: one bit each, instead of an
// Adafruit GFXglyph (8 bytes) per codepoint. Over U+0020-04FF that is 156
// bytes plus the rank table against ~10 KB of glyph records.
struct MonoFont {
  const uint8_t*  bitmap;   // the drawn glyphs in codepoint order, cellBytes() each, rows packed MSB first
  const uint32_t* defined;  // one bit per codepoint from `first` on, LSB first in each word
  const uint16_t* rank;     // glyphs drawn before each word of `defined`
  uint16_t first, last;
  uint8_t  w, h;            // the cell; h is also the row advance
  uint8_t  ascent;          // baseline below the top of the cell

  int cellBytes() const { return (w * h + 7) / 8; }
  bool covers(uint32_t cp) const { return cp >= first && cp <= last; }
  // cp's glyph in `bitmap`, or nullptr for a codepoint the font has no glyph for.
  const uint8_t* glyph(uint32_t cp) const {
    if (!covers(cp)) return nullptr;
    const uint32_t i = cp - first;
    const uint32_t word = pgm_read_dword(&defined[i >> 5]);
    const uint32_t bit = 1u << (i & 31);
    if (!(word & bit)) return nullptr;
    const uint32_t n = pgm_read_word(&rank[i >> 5]) + __builtin_popcount(word & (bit - 1));
    return bitmap + n * cellBytes();
  }
};
