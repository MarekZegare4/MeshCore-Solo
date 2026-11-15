// PNGdec for the map tiles (map/TileProvider.h): a row at a time straight
// into the RGB565 tile -- no 256 KB RGBA picture in between as with lodepng,
// and about a third faster. In a file of its own because PNGdec's zlib
// headers put names like DONE and EXTRA in the global namespace, which the
// single-TU UI would trip over. Its state (~37 KB, mostly the inflate window)
// sits in PSRAM: one copy, for the tile task only (TileCache).

#if defined(ESP32)
#include <PNGdec.h>
#include <esp_heap_caps.h>
#include <new>

static PNG* s_png = nullptr;

static bool openPng(const uint8_t* png, size_t len, int side, PNG_DRAW_CALLBACK* row) {
  if (!s_png) {
    void* m = heap_caps_malloc(sizeof(PNG), MALLOC_CAP_SPIRAM);
    if (!m) return false;
    s_png = new (m) PNG();
  }
  if (s_png->openRAM((uint8_t*)png, (int)len, row) != PNG_SUCCESS) return false;
  if (s_png->getWidth() == side && s_png->getHeight() == side) return true;
  s_png->close();
  return false;
}

// Indexed tiles (the usual OSM kind): the palette in RGB565 once a tile, at
// its first row (PNGdec reads PLTE as it decodes), then a pixel is one lookup.
// getLineAsRGB565() works each pixel out from the palette's RGB again -- half
// the decode time -- and its own fast palette is skipped when the palette has
// alpha (tRNS). The table gives what it gave, alpha (8-bit) included.
static uint16_t s_lut[256];
static bool s_lut_ready = false;

static int onRow(PNGDRAW* d) {
  uint16_t* o = (uint16_t*)d->pUser + d->y * d->iWidth;
  if (d->iPixelType != PNG_PIXEL_INDEXED) {
    s_png->getLineAsRGB565(d, o, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
    return 1;
  }
  const int bpp = d->iBpp;
  if (!s_lut_ready) {
    const uint8_t* p = d->pPalette;
    const bool alpha = d->iHasAlpha && bpp == 8;   // as getLineAsRGB565(): 8-bit only, colour times alpha
    for (int i = 0; i < (1 << bpp); i++, p += 3) {
      if (alpha) {
        const unsigned a = d->pPalette[768 + i];
        s_lut[i] = (uint16_t)((((p[0] * a) >> 11) << 11) | (((p[1] * a) >> 10) << 5) | ((p[2] * a) >> 11));
      } else {
        s_lut[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
      }
    }
    s_lut_ready = true;
  }
  const uint8_t* s = d->pPixels;
  const int w = d->iWidth;
  if (bpp == 8) {
    for (int x = 0; x < w; x++) o[x] = s_lut[s[x]];
  } else {
    const unsigned mask = (1u << bpp) - 1;
    for (int x = 0; x < w; x++) o[x] = s_lut[(s[(x * bpp) >> 3] >> (8 - bpp - ((x * bpp) & 7))) & mask];
  }
  return 1;
}

// `png` (len bytes) into out[side * side]; false if it isn't a side x side
// picture PNGdec can read (the caller then tries lodepng).
bool pngdecRgb565(const uint8_t* png, size_t len, uint16_t* out, int side) {
  if (!openPng(png, len, side, onRow)) return false;
  s_lut_ready = false;
  bool ok = s_png->decode(out, 0) == PNG_SUCCESS;
  s_png->close();
  return ok;
}

#if defined(SOLO_TEST_HOOKS)
// `tilebench` (SdBench.h): the same decode with rows thrown away -- inflate
// and unfilter alone, without the colour conversion.
static int onRowNone(PNGDRAW*) { return 1; }
// ... and the tile as getLineAsRGB565() makes it, to check the table against.
static int onRowRef(PNGDRAW* d) {
  s_png->getLineAsRGB565(d, (uint16_t*)d->pUser + d->y * d->iWidth, PNG_RGB565_LITTLE_ENDIAN, 0xffffffff);
  return 1;
}
bool pngdecRef(const uint8_t* png, size_t len, uint16_t* out, int side) {
  if (!openPng(png, len, side, onRowRef)) return false;
  bool ok = s_png->decode(out, 0) == PNG_SUCCESS;
  s_png->close();
  return ok;
}
bool pngdecBench(const uint8_t* png, size_t len, int side) {
  if (!openPng(png, len, side, onRowNone)) return false;
  bool ok = s_png->decode(nullptr, 0) == PNG_SUCCESS;
  s_png->close();
  return ok;
}
#endif

// One pixel of an overlay over the tile, by its alpha.
static inline void blend(uint16_t& o, unsigned r, unsigned g, unsigned b, unsigned a) {
  unsigned r0 = (o >> 8) & 0xF8, g0 = (o >> 3) & 0xFC, b0 = (o << 3) & 0xF8;
  r = (r * a + r0 * (255 - a)) / 255;
  g = (g * a + g0 * (255 - a)) / 255;
  b = (b * a + b0 * (255 - a)) / 255;
  o = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

static int onOverlayRow(PNGDRAW* d) {
  uint16_t* o = (uint16_t*)d->pUser + d->y * d->iWidth;
  const uint8_t* s = d->pPixels;
  if (d->iPixelType == PNG_PIXEL_TRUECOLOR_ALPHA) {
    for (int x = 0; x < d->iWidth; x++, s += 4)
      if (s[3]) blend(o[x], s[0], s[1], s[2], s[3]);
    return 1;
  }
  const uint8_t* pal = d->pPalette;   // indexed: RGB triplets, then the alpha of each entry at 768
  const int bpp = d->iBpp;
  for (int x = 0; x < d->iWidth; x++) {
    unsigned i = bpp == 8 ? s[x] : (s[(x * bpp) >> 3] >> (8 - bpp - ((x * bpp) & 7))) & ((1u << bpp) - 1);
    if (unsigned a = pal[768 + i]) blend(o[x], pal[i * 3], pal[i * 3 + 1], pal[i * 3 + 2], a);
  }
  return 1;
}

// A transparent overlay (`png`) drawn over out[side * side]. Indexed or 8-bit
// RGBA pictures only (hiking trails come as either); false otherwise, with
// `out` untouched.
bool pngdecOverlayRgb565(const uint8_t* png, size_t len, uint16_t* out, int side) {
  if (!openPng(png, len, side, onOverlayRow)) return false;
  int t = s_png->getPixelType();
  bool ok = (t == PNG_PIXEL_INDEXED || (t == PNG_PIXEL_TRUECOLOR_ALPHA && s_png->getBpp() == 8))
            && s_png->decode(out, 0) == PNG_SUCCESS;
  s_png->close();
  return ok;
}
#endif
