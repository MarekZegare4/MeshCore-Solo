#pragma once
// Text cut to fit a buffer on a whole UTF-8 character. A plain snprintf or
// strncpy stops at the byte, and a name such as "Łódź" cut inside a letter
// leaves the first half of it: a stray glyph on screen, a broken character in
// a sent message. For the places that fit text on purpose (a name in a row, a
// title, a label kept in a smaller field) -- a buffer that has to hold the
// whole text wants to be big enough instead.

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace textfit {

static inline bool isCont(char c) { return ((uint8_t)c & 0xC0) == 0x80; }

// Where to cut s[0..len) to at most `max` bytes: `max` moved back to the start
// of the character it lands inside, the byte after the cut seen. Bytes that
// are not UTF-8 (no lead within 3 bytes back) are cut where they are.
inline size_t cutAt(const char* s, size_t len, size_t max) {
  if (len <= max) return len;
  size_t k = max;
  for (int i = 0; i < 3 && k > 0 && isCont(s[k]); i++) k--;
  return isCont(s[k]) ? max : k;
}

// The length of buf[0..len) without a character cut off at its end, for text
// already cut with nothing after it to look at (a vsnprintf result): the last
// lead byte whose sequence runs past `len` is dropped with what follows it.
inline size_t wholeChars(const char* buf, size_t len) {
  if (!len) return 0;
  size_t s = len;
  int cont = 0;   // continuation bytes walked back over
  while (s > 0 && cont < 3 && isCont(buf[s - 1])) { s--; cont++; }
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

// The first `len` bytes of src (not NUL-terminated: a field in a frame, a name
// before its separator) into n bytes, cut on a whole character. Returns the
// length written.
inline int copyN(char* dst, size_t n, const char* src, size_t len) {
  if (!n) return 0;
  const size_t k = src ? cutAt(src, len, n - 1) : 0;
  if (k) memmove(dst, src, k);
  dst[k] = '\0';
  return (int)k;
}

// strcpy into n bytes, cut on a whole character.
inline int copy(char* dst, size_t n, const char* src) { return copyN(dst, n, src, src ? strlen(src) : 0); }

}  // namespace textfit
