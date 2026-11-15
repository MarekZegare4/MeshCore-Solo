#pragma once
// Text on the L2, drawn with less work per letter (linker wraps, set in
// solo/wio-tracker-l2/platformio.ini; elsewhere LVGL as it is).
//
// Glyphs: the fonts are 4 bits a pixel in flash, which LVGL unpacks to 8 bits
// into a scratch buffer every time a letter is drawn -- a quarter of a
// letter's cost. Here each glyph is kept unpacked in PSRAM after its first
// draw and handed over as an 8-bit static bitmap, which LVGL blends straight
// from (fonts/generate.sh marks the fonts static_bitmap). Bounded: past
// MAX_BYTES glyphs are unpacked as before.
//
// Line breaks: every draw of a label works out its lines again
// (lv_text_get_next_line, each letter's width looked up) though nothing
// changed -- a fifth of a label's drawing. The answers are kept, by the
// text's address, length and contents, the font, width and flags.
//
// Scratch buffer: each label drawn takes one for its letters, malloc'd and
// freed in PSRAM (~20 us a label) though cached glyphs never touch it. The
// last one is kept for the next.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(SEEED_WIO_TRACKER_L2)
#include <esp_heap_caps.h>
#include <src/misc/lv_text_private.h>
#include <src/core/lv_global.h>
#include <src/draw/lv_draw_buf_private.h>

namespace glyphcache {

struct Font { const lv_font_t* f; uint8_t** tab; uint32_t n; };
static Font s_fonts[8];
static int s_nfonts = 0;
static const uint32_t MAX_BYTES = 384 * 1024, CHUNK = 16 * 1024;
static uint8_t* s_chunk = nullptr;
static uint32_t s_chunk_left = 0, s_bytes = 0;

static Font* find(const lv_font_t* f) {
  for (int i = 0; i < s_nfonts; i++) if (s_fonts[i].f == f) return &s_fonts[i];
  return nullptr;
}
// A table of glyph pointers for `f`, as many as its character maps reach.
static Font* add(const lv_font_t* f) {
  if (s_nfonts >= (int)(sizeof(s_fonts) / sizeof(s_fonts[0]))) return nullptr;
  const lv_font_fmt_txt_dsc_t* d = (const lv_font_fmt_txt_dsc_t*)f->dsc;
  uint32_t n = 0;
  for (uint16_t i = 0; i < d->cmap_num; i++) {
    const lv_font_fmt_txt_cmap_t& c = d->cmaps[i];
    uint32_t end = c.glyph_id_start + (c.list_length > c.range_length ? c.list_length : c.range_length);
    if (end > n) n = end;
  }
  uint8_t** tab = (uint8_t**)heap_caps_calloc(n + 1, sizeof(uint8_t*), MALLOC_CAP_SPIRAM);
  if (!tab) return nullptr;
  s_fonts[s_nfonts] = { f, tab, n + 1 };
  return &s_fonts[s_nfonts++];
}
static uint8_t* cached(const lv_font_t* f, uint32_t gid) {
  Font* c = find(f);
  return c && gid < c->n ? c->tab[gid] : nullptr;
}
// Room for one unpacked glyph, from 16 KB pieces of PSRAM.
static uint8_t* take(uint32_t sz) {
  if (s_bytes + sz > MAX_BYTES || sz > CHUNK) return nullptr;
  if (sz > s_chunk_left) {
    s_chunk = (uint8_t*)heap_caps_malloc(CHUNK, MALLOC_CAP_SPIRAM);
    if (!s_chunk) { s_chunk_left = 0; return nullptr; }
    s_chunk_left = CHUNK;
    s_bytes += CHUNK;
  }
  uint8_t* p = s_chunk;
  s_chunk += sz;
  s_chunk_left -= sz;
  return p;
}

// Line breaks asked for lately.
struct Line {
  const char* txt; const lv_font_t* font; uint32_t len, hash, ret;
  int32_t max_w, used_w; int16_t letter_space; uint8_t flags, has_w;
};
static Line s_lines[64];
static uint32_t textHash(const char* t, uint32_t len) {
  if (len > 512) len = 512;   // a line ends well before (320 px)
  uint32_t h = 2166136261u;
  for (uint32_t i = 0; i < len && t[i]; i++) h = (h ^ (uint8_t)t[i]) * 16777619u;
  return h ^ len;
}

static lv_draw_buf_t* s_spare = nullptr;   // a font scratch buffer, kept
static const uint32_t SPARE_MAX = 8 * 1024;

}  // namespace glyphcache

extern "C" bool __real_lv_font_get_glyph_dsc_fmt_txt(const lv_font_t*, lv_font_glyph_dsc_t*, uint32_t, uint32_t);
extern "C" bool __wrap_lv_font_get_glyph_dsc_fmt_txt(const lv_font_t* f, lv_font_glyph_dsc_t* g, uint32_t letter, uint32_t next) {
  bool r = __real_lv_font_get_glyph_dsc_fmt_txt(f, g, letter, next);
  if (r && g->format == LV_FONT_GLYPH_FORMAT_A4 && f->static_bitmap && glyphcache::cached(f, g->gid.index)) {
    g->format = LV_FONT_GLYPH_FORMAT_A8;
    g->stride = g->box_w;
  }
  return r;
}

extern "C" const void* __real_lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t*, lv_draw_buf_t*);
extern "C" const void* __wrap_lv_font_get_bitmap_fmt_txt(lv_font_glyph_dsc_t* g, lv_draw_buf_t* buf) {
  const lv_font_t* f = g->resolved_font;
  const uint32_t gid = g->gid.index;
  if (g->req_raw_bitmap) {   // the static bitmap: ours when the glyph was made A8 above
    if (g->format == LV_FONT_GLYPH_FORMAT_A8)
      if (uint8_t* p = glyphcache::cached(f, gid)) return p;
    return __real_lv_font_get_bitmap_fmt_txt(g, buf);
  }
  const void* r = __real_lv_font_get_bitmap_fmt_txt(g, buf);
  if (!r || !buf || !f || !f->static_bitmap) return r;
  glyphcache::Font* c = glyphcache::find(f);
  if (!c) c = glyphcache::add(f);
  if (!c || gid >= c->n || c->tab[gid]) return r;
  const uint32_t w = g->box_w, h = g->box_h;
  uint8_t* m = glyphcache::take(w * h);
  if (!m) return r;
  const lv_draw_buf_t* d = (const lv_draw_buf_t*)r;   // A8, d->header.stride a row
  for (uint32_t y = 0; y < h; y++) memcpy(m + y * w, d->data + y * d->header.stride, w);
  c->tab[gid] = m;
  return r;
}

extern "C" uint32_t __real_lv_text_get_next_line(const char*, uint32_t, const lv_font_t*, int32_t*, lv_text_attributes_t*);
extern "C" uint32_t __wrap_lv_text_get_next_line(const char* txt, uint32_t len, const lv_font_t* font, int32_t* used_w,
                                                 lv_text_attributes_t* a) {
  using namespace glyphcache;
  if (!txt || !font || !a) return __real_lv_text_get_next_line(txt, len, font, used_w, a);
  const uint32_t h = textHash(txt, len);
  Line& e = s_lines[(h ^ ((uintptr_t)txt >> 2)) & 63];
  if (e.txt == txt && e.len == len && e.hash == h && e.font == font && e.max_w == a->max_width
      && e.letter_space == a->letter_space && e.flags == (uint8_t)a->text_flags && (e.has_w || !used_w)) {
    if (used_w) *used_w = e.used_w;
    return e.ret;
  }
  int32_t w = 0;
  uint32_t r = __real_lv_text_get_next_line(txt, len, font, used_w ? &w : nullptr, a);
  if (used_w) *used_w = w;
  e = { txt, font, len, h, r, a->max_width, w, (int16_t)a->letter_space, (uint8_t)a->text_flags, (uint8_t)(used_w != nullptr) };
  return r;
}

extern "C" lv_draw_buf_t* __real_lv_draw_buf_create_ex(const lv_draw_buf_handlers_t*, uint32_t, uint32_t, lv_color_format_t, uint32_t);
extern "C" lv_draw_buf_t* __wrap_lv_draw_buf_create_ex(const lv_draw_buf_handlers_t* h, uint32_t w, uint32_t hh, lv_color_format_t cf,
                                                     uint32_t stride) {
  using namespace glyphcache;
  if (s_spare && h == &LV_GLOBAL_DEFAULT()->font_draw_buf_handlers)
    if (lv_draw_buf_t* b = lv_draw_buf_reshape(s_spare, cf, w, hh, stride)) { s_spare = nullptr; return b; }
  return __real_lv_draw_buf_create_ex(h, w, hh, cf, stride);
}
extern "C" void __real_lv_draw_buf_destroy(lv_draw_buf_t*);
extern "C" void __wrap_lv_draw_buf_destroy(lv_draw_buf_t* b) {
  using namespace glyphcache;
  if (b && !s_spare && b->handlers == &LV_GLOBAL_DEFAULT()->font_draw_buf_handlers && b->data_size <= SPARE_MAX
      && lv_draw_buf_has_flag(b, LV_IMAGE_FLAGS_ALLOCATED)) {
    s_spare = b;
    return;
  }
  __real_lv_draw_buf_destroy(b);
}
#endif
