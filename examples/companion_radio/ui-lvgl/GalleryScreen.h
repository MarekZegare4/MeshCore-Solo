#pragma once
// Home > Gallery. Two tabs:
//   Screenshots -- the card's screenshots/ (takeScreenshot(): side + top
//                  button), newest first, as a grid of thumbnails PAGE at a
//                  time; built one per loop pass while nothing is touched,
//                  so the grid shows at once and fills in. A tap shows one on the whole screen: swipe
//                  for the next / previous, tap for its name, Delete and close.
//   Trails      -- the saved trails, the list Map tools > Load shows; a tap
//                  opens the map with that trail's Load / GPX / Delete.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h.

namespace gallery {

static const char* const SHOTS_DIR = "/sdcard/screenshots";
static const int SHOTS_MAX = 300;          // names kept, newest first
static const int PAGE = 30;                // thumbnails on the grid at a time
static const int TW = 96, TH = 72;         // a thumbnail: 3 to a row
static const int SW = 320, SH = 240;       // the full view (what takeScreenshot() writes)
static const int BMP_MAX_W = 640;          // wider files aren't ours: not shown

enum : uint8_t { T_SHOTS, T_TRAILS };
enum : uint8_t { V_DELETE, V_CLOSE };

static PSRAM_BSS char s_names[SHOTS_MAX][16];
static int s_n = 0;
static int s_page = 0;                      // the grid's page, from the newest
static uint8_t s_tab = T_SHOTS;

// The grid's thumbnails: one buffer per grid, freed with it (a rebuild makes
// the new grid before the old one goes, so each frees only its own).
static uint16_t* s_thumbs = nullptr;
static lv_image_dsc_t s_tdsc[PAGE];
static lv_obj_t* s_cell[PAGE];
static int s_cells = 0, s_next = 0;         // cells on the grid / the next to build

// The full view, over the whole screen.
static uint16_t* s_full = nullptr;
static lv_image_dsc_t s_fdsc;
static lv_obj_t* s_view = nullptr;
static lv_obj_t* s_view_img = nullptr;
static lv_obj_t* s_view_bar = nullptr;
static lv_obj_t* s_view_title = nullptr;
static lv_obj_t* s_del_lbl = nullptr;
static int s_cur = 0;
static bool s_changed = false;              // a delete from the view: the grid is rebuilt on closing it
static lv_point_t s_press;
static const int SWIPE_PX = 40;

static void scan() {
  s_n = 0;
  DIR* d = opendir(SHOTS_DIR);
  if (!d) return;
  while (struct dirent* de = readdir(d)) {
    size_t l = strlen(de->d_name);
    if (de->d_name[0] == '.' || l < 5 || l >= sizeof(s_names[0]) || strcasecmp(de->d_name + l - 4, ".bmp") != 0) continue;
    if (s_n < SHOTS_MAX) snprintf(s_names[s_n++], sizeof(s_names[0]), "%s", de->d_name);
  }
  closedir(d);
  // Newest first: scr_NNNN numbers count up.
  qsort(s_names, s_n, sizeof(s_names[0]), [](const void* a, const void* b) {
    return -strcmp((const char*)a, (const char*)b);
  });
}

static void path(int i, char* out, size_t n) { snprintf(out, n, "%s/%s", SHOTS_DIR, s_names[i]); }

// In the display's byte order, as the map tiles are (UI_FLUSH_DMA: big-endian).
static inline uint16_t px565(uint32_t r, uint32_t g, uint32_t b) {
  uint16_t c = (uint16_t)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
#ifdef UI_FLUSH_DMA
  return __builtin_bswap16(c);
#else
  return c;
#endif
}
static void dscInit(lv_image_dsc_t& d, uint16_t* px, int w, int h) {
  memset(&d, 0, sizeof(d));
  d.header.magic = LV_IMAGE_HEADER_MAGIC;
#ifdef UI_FLUSH_DMA
  d.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
#else
  d.header.cf = LV_COLOR_FORMAT_RGB565;
#endif
  d.header.w = w;
  d.header.h = h;
  d.header.stride = w * 2;
  d.data_size = w * h * 2;
  d.data = (const uint8_t*)px;
}

// An uncompressed 24-bit BMP, read a row at a time (top row = 0).
struct Bmp {
  FILE* f = nullptr;
  int32_t w = 0, h = 0;
  uint32_t off = 0, stride = 0;
  bool bottom_up = true;
  bool open(const char* p) {
    f = fopen(p, "rb");
    uint8_t hdr[54];
    if (!f || fread(hdr, 1, 54, f) != 54 || hdr[0] != 'B' || hdr[1] != 'M') return false;
    auto get32 = [&](int at) { return (uint32_t)hdr[at] | (uint32_t)hdr[at + 1] << 8 | (uint32_t)hdr[at + 2] << 16 | (uint32_t)hdr[at + 3] << 24; };
    off = get32(10);
    w = (int32_t)get32(18);
    h = (int32_t)get32(22);
    bottom_up = h > 0;
    if (h < 0) h = -h;
    const uint16_t bpp = hdr[28] | hdr[29] << 8;
    if (bpp != 24 || get32(30) != 0 || w <= 0 || w > BMP_MAX_W || h <= 0 || h > BMP_MAX_W) return false;
    stride = ((uint32_t)w * 3 + 3) & ~3u;
    return true;
  }
  bool row(int y, uint8_t* out) {   // BGR
    if (fseek(f, (long)(off + (uint32_t)(bottom_up ? h - 1 - y : y) * stride), SEEK_SET) != 0) return false;
    return fread(out, 1, (size_t)w * 3, f) == (size_t)w * 3;
  }
  ~Bmp() { if (f) fclose(f); }
};
static PSRAM_BSS uint8_t s_row[2][BMP_MAX_W * 3];

// File `i` scaled into w x h: each pixel the mean of the 2x2 under its
// spot (a thumbnail), or the one pixel there (the full view).
static bool decode(int i, uint16_t* out, int w, int h, bool smooth) {
  char p[48];
  path(i, p, sizeof(p));
  Bmp b;
  if (!b.open(p)) return false;
  for (int ty = 0; ty < h; ty++) {
    const int sy = ty * b.h / h, sy2 = smooth && sy + 1 < b.h ? sy + 1 : sy;
    if (!b.row(sy, s_row[0]) || (sy2 != sy && !b.row(sy2, s_row[1]))) return false;
    const uint8_t* r1 = s_row[0];
    const uint8_t* r2 = sy2 != sy ? s_row[1] : s_row[0];
    for (int tx = 0; tx < w; tx++) {
      const int sx = tx * b.w / w, sx2 = smooth && sx + 1 < b.w ? sx + 1 : sx;
      const uint8_t* a = r1 + sx * 3; const uint8_t* c = r1 + sx2 * 3;
      const uint8_t* d = r2 + sx * 3; const uint8_t* e = r2 + sx2 * 3;
      out[ty * w + tx] = px565((a[2] + c[2] + d[2] + e[2]) / 4, (a[1] + c[1] + d[1] + e[1]) / 4,
                               (a[0] + c[0] + d[0] + e[0]) / 4);
    }
  }
  return true;
}

static void onGridDeleted(lv_event_t* e) {
  uint16_t* px = (uint16_t*)lv_event_get_user_data(e);
  if (px != s_thumbs) { free(px); return; }   // an older grid's
  for (int i = 0; i < s_cells; i++) lv_image_cache_drop(&s_tdsc[i]);
  free(s_thumbs);
  s_thumbs = nullptr;
  s_cells = s_next = 0;
}
static void onViewDeleted(lv_event_t* e) {
  (void)e;
  s_view = s_view_img = s_view_bar = s_view_title = s_del_lbl = nullptr;
  lv_image_cache_drop(&s_fdsc);
  free(s_full);
  s_full = nullptr;
}

}  // namespace gallery

static void onOpenGallery(lv_event_t* e) { (void)e; s_ui->showGallery(); }
static void onGalleryTab(lv_event_t* e) {
  s_ui->galleryTab((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onGalleryShot(lv_event_t* e) { s_ui->galleryOpen((int)(intptr_t)lv_event_get_user_data(e)); }
static void onGalleryPage(lv_event_t* e) { s_ui->galleryPage((int)(intptr_t)lv_event_get_user_data(e)); }
static void onGalleryTrail(lv_event_t* e) { s_ui->galleryTrail((int)(intptr_t)lv_event_get_user_data(e)); }
static void onGalleryViewBtn(lv_event_t* e) { s_ui->galleryViewAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
// The full view: a swipe steps, a tap shows / hides the bar.
static void onGalleryViewTouch(lv_event_t* e) {
  using namespace gallery;
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (lv_event_get_code(e) == LV_EVENT_PRESSED) { s_press = p; return; }
  const int dx = p.x - s_press.x, dy = p.y - s_press.y;
  if (abs(dx) > SWIPE_PX && abs(dx) > abs(dy)) s_ui->galleryStep(dx < 0 ? 1 : -1);
  else if (abs(dx) < 10 && abs(dy) < 10 && s_view_bar) {
    if (lv_obj_has_flag(s_view_bar, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(s_view_bar, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(s_view_bar, LV_OBJ_FLAG_HIDDEN);
  }
}

void UITask::showGallery() {
  _screen = SCR_GALLERY;
  gallery::s_page = 0;
  buildGallery();
}

void UITask::galleryTab(int tab) {
  if (tab != gallery::T_SHOTS && tab != gallery::T_TRAILS) return;
  gallery::s_tab = (uint8_t)tab;
  buildGallery();
}

void UITask::galleryPage(int page) {
  gallery::s_page = page;
  buildGallery();
}

void UITask::buildGallery() {
  using namespace gallery;
  lv_obj_t* body = newScreen("Gallery", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  static const char* TABS[] = { "Screenshots", "Trails", "" };
  lv_obj_t* tabs = segmented(body, TABS, s_tab, lv_pct(100), theme::TAB_H);
  lv_obj_set_style_bg_color(tabs, lv_color_hex(theme::SURFACE), LV_PART_ITEMS);
  lv_obj_add_event_cb(tabs, onGalleryTab, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_t* list = scrollList(body);
  const bool card = lvport::mountStorage();

  if (s_tab == T_TRAILS) {
    if (card) {
      navmap::scanTrails();
      for (int i = 0; i < navmap::s_st_n; i++) {
        char title[32], sub[48], p[64];
        navmap::trailTitle(navmap::s_st_names[i], title, sizeof(title));
        snprintf(p, sizeof(p), "%s/%s", navmap::TRAILS_DIR, navmap::s_st_names[i]);
        struct stat st;
        long pts = stat(p, &st) == 0 ? ((long)st.st_size - navmap::TRAIL_HDR) / (long)sizeof(TrailPoint) : 0;
        snprintf(sub, sizeof(sub), "%ld points", pts > 0 ? pts : 0);
        listRow(list, title, sub, onGalleryTrail, (void*)(intptr_t)i);
      }
    }
    if (TrailEngine::savedExists())
      listRow(list, "Saved on the device", "Without a card, or on low battery", onGalleryTrail, (void*)(intptr_t)-1);
    if (lv_obj_get_child_count(list) == 0)
      noteLabel(list, "No saved trails yet - Map tools > Save keeps the one on the map.", THEME_FONT_BODY);
    return;
  }

  if (!card) { noteLabel(list, "No SD card - screenshots are saved on it.", THEME_FONT_BODY); return; }
  scan();
  if (s_n == 0) {
    noteLabel(list, "No screenshots yet. Hold the side button and press the top one to take one.", THEME_FONT_BODY);
    return;
  }
  if (_header) {
    lv_obj_t* c = label(_header, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_text_fmt(c, "%d", s_n);
    lv_obj_align(c, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  }
  const int pages = (s_n + PAGE - 1) / PAGE;
  if (s_page >= pages) s_page = pages - 1;
  if (s_page < 0) s_page = 0;
  const int first = s_page * PAGE;
  const int cells = s_n - first < PAGE ? s_n - first : PAGE;

  uint16_t* px = (uint16_t*)psramBuf<uint16_t>((size_t)PAGE * TW * TH);
  if (!px) { noteLabel(list, "Out of memory", THEME_FONT_BODY); return; }
  s_thumbs = px;
  s_cells = cells;
  s_next = 0;
  lv_obj_t* grid = lv_obj_create(list);
  lv_obj_remove_style_all(grid);
  fillWidth(grid);
  lv_obj_set_height(grid, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(grid, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_flex_align(grid, LV_FLEX_ALIGN_SPACE_EVENLY, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_set_style_pad_row(grid, 6, 0);
  lv_obj_add_event_cb(grid, gallery::onGridDeleted, LV_EVENT_DELETE, px);
  for (int i = 0; i < cells; i++) {
    lv_obj_t* c = lv_obj_create(grid);
    styleSurface(c, theme::SURFACE);
    lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(c, TW, TH);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_add_event_cb(c, onGalleryShot, LV_EVENT_CLICKED, (void*)(intptr_t)(first + i));
    s_cell[i] = c;
  }
  if (pages > 1) {
    lv_obj_t* bar = buttonBar(list);
    lv_obj_set_flex_align(bar, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    if (s_page > 0) barButton(bar, LV_SYMBOL_LEFT " Newer", onGalleryPage, s_page - 1);
    lv_obj_t* n = label(bar, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_text_fmt(n, "%d-%d of %d", first + 1, first + cells, s_n);
    if (s_page + 1 < pages) barButton(bar, "Older " LV_SYMBOL_RIGHT, onGalleryPage, s_page + 1);
  }
}

// From loop() on the gallery: the next thumbnail, only with the screen let
// go and still. A decode holds the loop up, and LVGL, missing the finger's
// moves meanwhile, took a scroll for a tap on the picture under it.
void UITask::galleryTick() {
  using namespace gallery;
  if (!s_thumbs || s_next >= s_cells || s_view) return;
  lv_indev_t* in = lv_indev_get_next(NULL);
  if (in && lv_indev_get_state(in) == LV_INDEV_STATE_PRESSED) return;
  if (lv_display_get_inactive_time(NULL) < 300 || lv_anim_count_running() > 0) return;   // a scroll coasting
  const int i = s_next++;
  uint16_t* px = s_thumbs + (size_t)i * TW * TH;
  if (!decode(s_page * PAGE + i, px, TW, TH, true)) {
    lv_obj_center(label(s_cell[i], "?", THEME_FONT_LARGE, theme::TEXT_MUTED));
    return;
  }
  dscInit(s_tdsc[i], px, TW, TH);
  lv_obj_t* img = lv_image_create(s_cell[i]);
  lv_obj_remove_flag(img, LV_OBJ_FLAG_CLICKABLE);
  lv_image_set_src(img, &s_tdsc[i]);
  lv_obj_center(img);
}

void UITask::galleryTrail(int idx) {
  openMap(true);
  savedTrailPopup(idx);
}

// Screenshot `i` on the whole screen.
void UITask::galleryOpen(int i) {
  using namespace gallery;
  if (i < 0 || i >= s_n) return;
  if (!s_full) s_full = (uint16_t*)psramBuf<uint16_t>((size_t)SW * SH);
  if (!s_full) { showToast("Out of memory"); return; }
  if (s_view_img) lv_image_cache_drop(&s_fdsc);
  if (!decode(i, s_full, SW, SH, false)) { showToast("Can't read this file"); return; }
  s_cur = i;
  dscInit(s_fdsc, s_full, SW, SH);
  if (!s_view) {
    s_changed = false;
    s_view = lv_obj_create(_scr);
    lv_obj_remove_style_all(s_view);
    lv_obj_add_flag(s_view, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_set_pos(s_view, 0, 0);
    lv_obj_set_size(s_view, SW, SH);
    lv_obj_set_style_bg_color(s_view, lv_color_black(), 0);
    lv_obj_set_style_bg_opa(s_view, LV_OPA_COVER, 0);
    lv_obj_remove_flag(s_view, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_view, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_view, onGalleryViewTouch, LV_EVENT_PRESSED, NULL);
    lv_obj_add_event_cb(s_view, onGalleryViewTouch, LV_EVENT_RELEASED, NULL);
    lv_obj_add_event_cb(s_view, gallery::onViewDeleted, LV_EVENT_DELETE, NULL);
    s_view_img = lv_image_create(s_view);
    lv_obj_remove_flag(s_view_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_view_img, 0, 0);

    s_view_bar = lv_obj_create(s_view);
    styleSurface(s_view_bar, theme::SURFACE);
    lv_obj_set_style_bg_opa(s_view_bar, LV_OPA_90, 0);
    lv_obj_remove_flag(s_view_bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(s_view_bar, SW, 40);
    lv_obj_set_pos(s_view_bar, 0, 0);
    lv_obj_set_style_pad_hor(s_view_bar, theme::PAD, 0);
    lv_obj_set_flex_flow(s_view_bar, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(s_view_bar, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(s_view_bar, 6, 0);
    s_view_title = label(s_view_bar, "", THEME_FONT_BODY, theme::TEXT);
    lv_obj_set_flex_grow(s_view_title, 1);
    lv_obj_t* del = pillButton(s_view_bar, LV_SYMBOL_TRASH " Delete", theme::SURFACE_2, onGalleryViewBtn, V_DELETE);
    lv_obj_set_height(del, 32);
    s_del_lbl = lv_obj_get_child(del, 0);
    lv_obj_set_height(pillButton(s_view_bar, LV_SYMBOL_CLOSE, theme::SURFACE_2, onGalleryViewBtn, V_CLOSE), 32);
    lv_obj_add_flag(s_view_bar, LV_OBJ_FLAG_HIDDEN);   // a tap brings it
  }
  lv_image_set_src(s_view_img, &s_fdsc);
  lv_obj_invalidate(s_view_img);
  lv_label_set_text_fmt(s_view_title, "%.*s  %d / %d", (int)(strlen(s_names[i]) - 4), s_names[i], i + 1, s_n);
}

void UITask::galleryStep(int d) {
  const int i = gallery::s_cur + d;
  if (i >= 0 && i < gallery::s_n) galleryOpen(i);
}

void UITask::galleryCloseView() {
  using namespace gallery;
  if (!s_view) return;
  lv_obj_delete(s_view);   // onViewDeleted frees the picture
  // Back on the page the one looked at last is on.
  if (s_changed || s_cur / PAGE != s_page) { s_page = s_cur / PAGE; buildGallery(); }
}

void UITask::galleryBack() {
  if (gallery::s_view) galleryCloseView();
  else showHome();
}

void UITask::galleryViewAction(uint8_t act) {
  using namespace gallery;
  if (act == V_CLOSE) { galleryCloseView(); return; }
  if (!tapConfirmed(s_del_lbl, "Delete?")) return;
  char p[48];
  path(s_cur, p, sizeof(p));
  if (remove(p) != 0) { showToast("Delete failed"); return; }
  s_changed = true;
  scan();
  if (s_n == 0) { s_cur = 0; galleryCloseView(); return; }
  galleryOpen(s_cur < s_n ? s_cur : s_n - 1);
}
