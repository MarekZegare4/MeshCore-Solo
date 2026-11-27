#pragma once
// Text cut to fit a buffer on a whole UTF-8 character. A plain snprintf stops
// at the byte, and a name such as "Łódź" cut inside a letter leaves the first
// half of it: a stray glyph on screen, a broken character in a sent message.
// For the places that fit text on purpose (a name in a row, a title) -- a
// buffer that has to hold the whole text wants to be big enough instead.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

namespace textfit {

// The length of buf[0..len) without a character cut off at its end: the last
// lead byte whose sequence runs past `len` is dropped with what follows it.
// Bytes that are not UTF-8 at all are kept as they are (nothing to cut).
inline size_t wholeChars(const char* buf, size_t len) {
  if (!len) return 0;
  size_t s = len;
  int cont = 0;   // continuation bytes walked back over
  while (s > 0 && cont < 3 && ((uint8_t)buf[s - 1] & 0xC0) == 0x80) { s--; cont++; }
  if (s == 0) return len;   // only continuation bytes: not UTF-8, keep
  const uint8_t lead = (uint8_t)buf[s - 1];
  const int need = lead >= 0xF0 ? 3 : lead >= 0xE0 ? 2 : lead >= 0xC0 ? 1 : 0;
  if (lead < 0xC0) return len;               // ASCII (or a stray byte) ends it
  return cont < need ? s - 1 : len;          // the last character incomplete: drop it
}

// snprintf, the result cut on a whole character when it doesn't fit.
// Returns the length written.
inline int fmt(char* buf, size_t n, const char* f, ...) __attribute__((format(printf, 3, 4)));
inline int fmt(char* buf, size_t n, const char* f, ...) {
  if (!n) return 0;
  va_list ap;
  va_start(ap, f);
  const int r = vsnprintf(buf, n, f, ap);
  va_end(ap);
  if (r < 0) { buf[0] = '\0'; return 0; }
  if ((size_t)r < n) return r;
  const size_t k = wholeChars(buf, n - 1);
  buf[k] = '\0';
  return (int)k;
}

// strcpy into n bytes, cut on a whole character.
inline int copy(char* dst, size_t n, const char* src) { return fmt(dst, n, "%s", src ? src : ""); }

}  // namespace textfit
