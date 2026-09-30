#pragma once
// Map tile sources. The map screen, its tile cache and the overlays only ever
// call renderTile(): 256x256 RGB565 pixels for Web-Mercator tile z/x/y. The
// raster provider decodes PNG files from the card; a vector renderer would be
// another TileProvider drawing into the same buffer, with nothing else changing.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#include <stdio.h>
#include <sys/stat.h>
// LVGL's copy of lodepng (LV_USE_LODEPNG), compiled as C. It is patched for
// LVGL: the decode "output" is an lv_draw_buf_t* (32-bit RGBA pixels in ->data),
// not a plain pixel array, and is freed with lv_draw_buf_destroy().
#define LODEPNG_NO_COMPILE_CPP
extern "C" {
#include "src/libs/lodepng/lodepng.h"
}

#if defined(ESP32)
bool pngdecRgb565(const uint8_t* png, size_t len, uint16_t* out, int side);          // ui-lvgl/TilePng.cpp
bool pngdecOverlayRgb565(const uint8_t* png, size_t len, uint16_t* out, int side);
#endif

namespace mapview {

// OpenTopoMap answers every tile past its last zoom level (17) with the same
// "no tile" picture. Earlier builds saved it; the provider drops such a file
// (so the map magnifies the parent instead) and the downloader never writes it.
static const uint32_t OTM_NOTILE_LEN = 4343, OTM_NOTILE_FNV = 0x0B80D772;
static bool isNoTilePicture(const uint8_t* d, size_t n) {
  if (n != OTM_NOTILE_LEN) return false;
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 16777619u;
  return h == OTM_NOTILE_FNV;
}

static const int TILE_PX = 256;

// Hiking trails: Waymarked Trails' transparent overlay (marked routes in their
// waymark colours), kept apart from the base map under TRAILS_ROOT in the same
// {z}/{x}/{y}.png layout, drawn over whatever base tile is shown while
// s_trails_on. A 0-byte file there means "fetched, no trail on this tile" --
// most tiles, and an empty file takes no cluster on a FAT card.
static const char* const TRAILS_ROOT = "/sdcard/maps-trails";
static const char* const TRAILS_URL = "https://tile.waymarkedtrails.org/hiking/{z}/{x}/{y}.png";
static const char* const TRAILS_ATTR = "Trails \xC2\xA9 waymarkedtrails.org (CC-BY-SA)";
static const int TRAILS_MAX_Z = 18;
static bool s_trails_on = false;   // Map tools > Hiking trails (NVS), set when the map opens

// fread into a PSRAM buffer: the SD card can't DMA there, so the driver
// falls back to one 512-byte sector per transfer. Read in chunks through
// internal (DMA-capable) memory instead -- several times faster.
static bool readFast(FILE* f, uint8_t* dst, size_t len) {
#if defined(ESP32)
  const size_t CHUNK = 16 * 1024;
  uint8_t* bounce = (uint8_t*)heap_caps_malloc(len < CHUNK ? len : CHUNK, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  if (bounce) {
    size_t done = 0;
    while (done < len) {
      size_t n = len - done < CHUNK ? len - done : CHUNK;
      if (fread(bounce, 1, n, f) != n) break;
      memcpy(dst + done, bounce, n);
      done += n;
    }
    heap_caps_free(bounce);
    return done == len;
  }
#endif
  return fread(dst, 1, len, f) == len;
}

// PNG -> RGBA (LVGL's lodepng: the result is an lv_draw_buf_t*, destroy it).
static lv_draw_buf_t* decodePng(const uint8_t* png, size_t len) {
  unsigned char* res = nullptr;
  unsigned w = 0, h = 0;
  unsigned err = lodepng_decode32(&res, &w, &h, png, len);
  lv_draw_buf_t* db = (lv_draw_buf_t*)res;
  if (err || !db || w != TILE_PX || h != TILE_PX) { if (db) lv_draw_buf_destroy(db); return nullptr; }
  return db;
}

// An overlay tile with anything drawn on it (not fully transparent).
static bool pngHasInk(const uint8_t* png, size_t len) {
  lv_draw_buf_t* db = decodePng(png, len);
  if (!db) return false;
  bool ink = false;
  for (int row = 0; row < TILE_PX && !ink; row++) {
    const uint8_t* p = db->data + row * db->header.stride + 3;
    for (int i = 0; i < TILE_PX; i++, p += 4) if (*p) { ink = true; break; }
  }
  lv_draw_buf_destroy(db);
  return ink;
}

class TileProvider {
public:
  virtual ~TileProvider() {}
  virtual bool available() = 0;                                   // anything to draw at all
  virtual bool renderTile(int z, int x, int y, uint16_t* out) = 0; // false: no tile there
  virtual const char* attribution() const = 0;
  virtual bool overlayMissed() const { return false; }   // the last renderTile() went without its trails overlay
};

// Raster tiles under <root>/{z}/{x}/{y}.png -- the Meshtastic MUI layout, so
// cards prepared for either work for both (tools/maps/fetch_tiles.py writes
// it) -- or packed per column in <root>/{z}/{x}.pak ('TPK1': y_min, y_max,
// offsets[n+1], PNG blobs; the format of upstream PR #3381's tile_packer.py),
// which copies onto a FAT card far faster than millions of loose files.
class RasterTileProvider : public TileProvider {
public:
  // `live`: a second folder in the same layout (live tiles, LiveCache.h), read
  // when <root> has no tile.
  explicit RasterTileProvider(const char* root, const char* live = nullptr) : _root(root), _live(live) {}

  // Also reads <root>/attribution.txt (written by tools/maps/fetch_tiles.py and
  // the on-device downloader): the tile set's credit line, shown on the map.
  bool available() override {
    struct stat st;
    if (stat(_root, &st) != 0 || !S_ISDIR(st.st_mode)) return false;
    char path[64];
    snprintf(path, sizeof(path), "%s/attribution.txt", _root);
    _attr[0] = '\0';
    if (FILE* f = fopen(path, "r")) {
      size_t n = fread(_attr, 1, sizeof(_attr) - 1, f);
      fclose(f);
      while (n > 0 && (_attr[n - 1] == '\n' || _attr[n - 1] == '\r')) n--;
      _attr[n] = '\0';
    }
    return true;
  }

#ifdef UI_PERF_TEST
  uint32_t perf_read_us = 0, perf_dec_us = 0, perf_bytes = 0, perf_ovl_us = 0;
#endif
  bool renderTile(int z, int x, int y, uint16_t* out) override {
    _overlay_missed = false;
#ifdef UI_PERF_TEST
    uint32_t t0 = micros();
#endif
    uint32_t len = 0;
    uint8_t* png = readLoose(_root, z, x, y, len);
    if (!png) png = readPacked(z, x, y, len);
    if (!png && _live) png = readLoose(_live, z, x, y, len);
    if (!png) return false;
#ifdef UI_PERF_TEST
    uint32_t t1 = micros();
    perf_read_us += t1 - t0; perf_bytes += len;
#endif

    bool ok = false;
#if defined(ESP32)
    ok = pngdecRgb565(png, len, out, TILE_PX);   // lodepng below if it can't
#endif
    if (!ok) {
      lv_draw_buf_t* db = decodePng(png, len);
      if (!db) { lv_free(png); return false; }
      for (int row = 0; row < TILE_PX; row++) {
        const uint8_t* p = db->data + row * db->header.stride;   // R, G, B, A
        uint16_t* o = out + row * TILE_PX;
        for (int i = 0; i < TILE_PX; i++, p += 4)
          o[i] = (uint16_t)(((p[0] & 0xF8) << 8) | ((p[1] & 0xFC) << 3) | (p[2] >> 3));
      }
      lv_draw_buf_destroy(db);
    }
    lv_free(png);
#ifdef UI_PERF_TEST
    perf_dec_us += micros() - t1;
#endif
#ifdef UI_PERF_TEST
    uint32_t t2 = micros();
#endif
    if (s_trails_on && z <= TRAILS_MAX_Z) drawOverlay(z, x, y, out);
#ifdef UI_PERF_TEST
    perf_ovl_us += micros() - t2;
#endif
    return true;
  }

  // The last renderTile() found no trails file for its tile (not fetched yet).
  bool overlayMissed() const override { return _overlay_missed; }

  const char* attribution() const override {
    const char* base = _attr[0] ? _attr : "\xC2\xA9 OpenStreetMap contributors";
    if (!s_trails_on) return base;
    snprintf(_attr_full, sizeof(_attr_full), "%s | %s", base, TRAILS_ATTR);
    return _attr_full;
  }

private:
  const char* _root;
  const char* _live;
  char _attr[96] = "";
  mutable char _attr_full[160] = "";
  bool _overlay_missed = false;

  // The trails tile over `out`, blended by its alpha.
  void drawOverlay(int z, int x, int y, uint16_t* out) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.png", TRAILS_ROOT, z, x, y);
    FILE* f = fopen(path, "rb");
    if (!f) { _overlay_missed = true; return; }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    uint8_t* png = sz > 0 ? readRange(f, 0, (uint32_t)sz) : nullptr;   // 0 bytes: no trail here
    fclose(f);
    if (!png) return;
#if defined(ESP32)
    if (pngdecOverlayRgb565(png, (size_t)sz, out, TILE_PX)) { lv_free(png); return; }   // else lodepng
#endif
    lv_draw_buf_t* db = decodePng(png, (size_t)sz);
    lv_free(png);
    if (!db) return;
    for (int row = 0; row < TILE_PX; row++) {
      const uint8_t* p = db->data + row * db->header.stride;
      uint16_t* o = out + row * TILE_PX;
      for (int i = 0; i < TILE_PX; i++, p += 4) {
        unsigned a = p[3];
        if (!a) continue;
        unsigned r = (o[i] >> 8) & 0xF8, g = (o[i] >> 3) & 0xFC, b = (o[i] << 3) & 0xF8;
        r = (p[0] * a + r * (255 - a)) / 255;
        g = (p[1] * a + g * (255 - a)) / 255;
        b = (p[2] * a + b * (255 - a)) / 255;
        o[i] = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
      }
    }
    lv_draw_buf_destroy(db);
  }

  static uint8_t* readRange(FILE* f, long off, uint32_t len) {
    if (len == 0 || len > 512 * 1024) return nullptr;
    uint8_t* buf = (uint8_t*)lv_malloc(len);
    if (!buf) return nullptr;
    if (fseek(f, off, SEEK_SET) != 0 || !readFast(f, buf, len)) { lv_free(buf); return nullptr; }
    return buf;
  }

  uint8_t* readLoose(const char* root, int z, int x, int y, uint32_t& len) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d/%d.png", root, z, x, y);
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    uint8_t* buf = sz > 0 ? readRange(f, 0, (uint32_t)sz) : nullptr;
    fclose(f);
    if (buf && isNoTilePicture(buf, (size_t)sz)) { lv_free(buf); remove(path); return nullptr; }
    if (buf) len = (uint32_t)sz;
    return buf;
  }

  uint8_t* readPacked(int z, int x, int y, uint32_t& len) {
    char path[64];
    snprintf(path, sizeof(path), "%s/%d/%d.pak", _root, z, x);
    FILE* f = fopen(path, "rb");
    if (!f) return nullptr;
    uint8_t hdr[12];
    uint32_t y0, y1, off[2];
    uint8_t* buf = nullptr;
    if (fread(hdr, 1, 12, f) == 12 && memcmp(hdr, "TPK1", 4) == 0) {
      memcpy(&y0, hdr + 4, 4);
      memcpy(&y1, hdr + 8, 4);
      if (y >= (int)y0 && y <= (int)y1 &&
          fseek(f, 12 + 4 * (y - (int)y0), SEEK_SET) == 0 && fread(off, 4, 2, f) == 2 && off[1] > off[0]) {
        len = off[1] - off[0];
        buf = readRange(f, off[0], len);
      }
    }
    fclose(f);
    return buf;
  }
};

}  // namespace mapview
