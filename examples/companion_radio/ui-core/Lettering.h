#pragma once
// The MeshCore wordmark and its lettering, shared by both boot splashes
// (ui-new/UITask.cpp on the OLED/e-ink boards, ui-lvgl/Splash.h on the L2):
// the 128x13 wordmark bitmap, its own letters, the extra characters drawn in
// the same strokes (digits, '.', '-', v, l, d, space), and the Solo version /
// build date strings both splashes show. Platform-free: callers plot pixels.

#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <ctype.h>

namespace lettering {
static const int LOGO_W = 128, LOGO_H = 13;

// 'meshcore', 128x13 px, MSB first 
static const uint8_t LOGO[] = {
  0x3c, 0x01, 0xe3, 0xff, 0xc7, 0xff, 0x8f, 0x03, 0x87, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe, 0x1f, 0xfe,
  0x3c, 0x03, 0xe3, 0xff, 0xc7, 0xff, 0x8e, 0x03, 0x8f, 0xfe, 0x3f, 0xfe, 0x1f, 0xff, 0x1f, 0xfe,
  0x3e, 0x03, 0xc3, 0xff, 0x8f, 0xff, 0x0e, 0x07, 0x8f, 0xfe, 0x7f, 0xfe, 0x1f, 0xff, 0x1f, 0xfc,
  0x3e, 0x07, 0xc7, 0x80, 0x0e, 0x00, 0x0e, 0x07, 0x9e, 0x00, 0x78, 0x0e, 0x3c, 0x0f, 0x1c, 0x00,
  0x3e, 0x0f, 0xc7, 0x80, 0x1e, 0x00, 0x0e, 0x07, 0x1e, 0x00, 0x70, 0x0e, 0x38, 0x0f, 0x3c, 0x00,
  0x7f, 0x0f, 0xc7, 0xfe, 0x1f, 0xfc, 0x1f, 0xff, 0x1c, 0x00, 0x70, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
  0x7f, 0x1f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x0e, 0x38, 0x0e, 0x3f, 0xf8,
  0x7f, 0x3f, 0xc7, 0xfe, 0x0f, 0xff, 0x1f, 0xff, 0x1c, 0x00, 0xf0, 0x1e, 0x3f, 0xfe, 0x3f, 0xf0,
  0x77, 0x3b, 0x87, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xfc, 0x38, 0x00,
  0x77, 0xfb, 0x8f, 0x00, 0x00, 0x07, 0x1c, 0x0f, 0x3c, 0x00, 0xe0, 0x1c, 0x7f, 0xf8, 0x38, 0x00,
  0x73, 0xf3, 0x8f, 0xff, 0x0f, 0xff, 0x1c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x78, 0x7f, 0xf8,
  0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfe, 0x3c, 0x0e, 0x3f, 0xf8, 0xff, 0xfc, 0x70, 0x3c, 0x7f, 0xf8,
  0xe3, 0xe3, 0x8f, 0xff, 0x1f, 0xfc, 0x3c, 0x0e, 0x1f, 0xf8, 0xff, 0xf8, 0x70, 0x3c, 0x7f, 0xf8,
};

// The wordmark's own letters: columns of LOGO.
struct LogoLetter { char c; uint8_t x0, w; };
static const LogoLetter LOGO_LETTERS[] = {
  { 'm', 0, 18 }, { 'e', 19, 15 }, { 's', 35, 15 }, { 'h', 50, 16 }, { 'c', 66, 14 }, { 'o', 80, 16 }, { 'r', 96, 16 },
};
// The rest, drawn upright in the same strokes (3 px bars at the top, middle
// and bottom); slanted like the wordmark when drawn (SLANT).
struct PixGlyph { char c; const char* rows[LOGO_H]; };
#define F10 "##########"
#define L10 "###......."
#define R10 ".......###"
#define B10 "###....###"
static const PixGlyph GLYPHS[] = {
  { '0', { F10, F10, F10, B10, B10, B10, B10, B10, B10, B10, F10, F10, F10 } },
  { '1', { "....####..", "...#####..", "..######..", "....####..", "....####..", "....####..", "....####..",
           "....####..", "....####..", "....####..", "....####..", "....####..", "....####.." } },
  { '2', { F10, F10, F10, R10, R10, F10, F10, F10, L10, L10, F10, F10, F10 } },
  { '3', { F10, F10, F10, R10, R10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '4', { B10, B10, B10, B10, B10, F10, F10, F10, R10, R10, R10, R10, R10 } },
  { '5', { F10, F10, F10, L10, L10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '6', { F10, F10, F10, L10, L10, F10, F10, F10, B10, B10, F10, F10, F10 } },
  { '7', { F10, F10, F10, R10, R10, R10, R10, R10, R10, R10, R10, R10, R10 } },
  { '8', { F10, F10, F10, B10, B10, F10, F10, F10, B10, B10, F10, F10, F10 } },
  { '9', { F10, F10, F10, B10, B10, F10, F10, F10, R10, R10, F10, F10, F10 } },
  { '.', { "....", "....", "....", "....", "....", "....", "....", "....", "....", "....", "###.", "###.", "###." } },
  { '-', { ".......", ".......", ".......", ".......", ".......", "#######", "#######", "#######",
           ".......", ".......", ".......", ".......", "......." } },
  { 'v', { "###.....###", "###.....###", "###.....###", "###.....###", "###.....###", "###.....###", "###.....###",
           "###.....###", ".###...###.", ".###...###.", "..###.###..", "...#####...", "....###...." } },
  { 'l', { "###......", "###......", "###......", "###......", "###......", "###......", "###......",
           "###......", "###......", "###......", "#########", "#########", "#########" } },
  { 'd', { "#########.", F10, F10, B10, B10, B10, B10, B10, B10, B10, F10, F10, "#########." } },
  { ' ', { ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", ".....", "....." } },
};
#undef F10
#undef L10
#undef R10
#undef B10
static const uint8_t SLANT[LOGO_H] = { 2, 2, 2, 1, 1, 1, 1, 1, 1, 0, 0, 0, 0 };   // px right, per row

static inline bool logoBit(int x, int y) {
  return LOGO[y * (LOGO_W / 8) + x / 8] & (0x80 >> (x % 8));
}

// One character of the lettering: its width, and whether (x, y) is inked.
static inline const LogoLetter* logoLetter(char c) {
  for (const LogoLetter& l : LOGO_LETTERS) if (l.c == c) return &l;
  return nullptr;
}
static inline const PixGlyph* pixGlyph(char c) {
  for (const PixGlyph& g : GLYPHS) if (g.c == c) return &g;
  return nullptr;
}
static inline int charW(char c) {
  if (const LogoLetter* l = logoLetter(c)) return l->w;
  if (const PixGlyph* g = pixGlyph(c)) return (int)strlen(g->rows[0]) + 2;   // + the slant
  return -1;
}
static inline bool inked(char c, int x, int y) {
  if (const LogoLetter* l = logoLetter(c)) {
    return logoBit(l->x0 + x, y);
  }
  const PixGlyph* g = pixGlyph(c);
  int gx = x - SLANT[y];
  return g && gx >= 0 && gx < (int)strlen(g->rows[y]) && g->rows[y][gx] == '#';
}

// `text` in the lettering (lower case), -1 if it has a character that isn't drawn.
static inline int textW(const char* text, int gap) {
  int w = 0;
  for (const char* p = text; *p; p++) {
    int cw = charW((char)tolower((unsigned char)*p));
    if (cw < 0) return -1;
    w += cw + (p[1] ? gap : 0);
  }
  return w;
}

// "v1.28-solo.1-abcdef" -> "v1.28-solo.1": build.sh appends the commit as
// the last dash segment .
static inline void soloVersion(char* out, size_t n) {
  const char* ver = FIRMWARE_VERSION;
  const char* dash = strrchr(ver, '-');
  size_t len = dash ? (size_t)(dash - ver) : strlen(ver);
  if (len >= n) len = n - 1;
  memcpy(out, ver, len);
  out[len] = '\0';
}

// The build date as digits, "2026-09-26": build.sh gives "26-Sep-2026",
// __DATE__ "Sep 26 2026". Anything else is passed through.
static inline void buildDate(char* out, size_t n) {
  static const char MON[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
  const char* d = FIRMWARE_BUILD_DATE;
  int day = 0, year = 0, mon = 0;
  char m[4] = "";
  if (sscanf(d, "%d-%3s-%d", &day, m, &year) != 3 && sscanf(d, "%3s %d %d", m, &day, &year) != 3) {
    snprintf(out, n, "%s", d);
    return;
  }
  const char* f = strstr(MON, m);
  mon = f && m[0] ? (int)(f - MON) / 3 + 1 : 0;
  if (mon) snprintf(out, n, "%04d-%02d-%02d", year, mon, day);
  else snprintf(out, n, "%s", d);
}
}  // namespace lettering
