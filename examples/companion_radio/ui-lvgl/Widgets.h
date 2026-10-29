#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).
// The widget kit every screen builds from: label and text setters that skip
// needless redraws, popups, info cards, settings rows and pickers, the big clock.

// ── Small helpers ─────────────────────────────────────────────────────────────

// A plain box in `bg`. One in the page colour is left unpainted: the screen
// under it (or the popup panel) already is that colour, and filling it again
// costs a full-area blend per frame. The screen and the status bar, which
// something may scroll or slide under, are made opaque with styleOpaque().
static void styleSurface(lv_obj_t* o, uint32_t bg) {
  lv_obj_set_style_bg_color(o, lv_color_hex(bg), 0);
  lv_obj_set_style_bg_opa(o, bg == theme::BG ? LV_OPA_TRANSP : LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_radius(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
}
static void styleOpaque(lv_obj_t* o, uint32_t bg) {
  styleSurface(o, bg);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
}

static bool growsWide(lv_obj_t* o) {   // a flex grow along a row: the width is the layout's
  lv_obj_t* p = lv_obj_get_parent(o);
  if (!p || !lv_obj_get_style_flex_grow(o, LV_PART_MAIN) || lv_obj_get_style_layout(p, LV_PART_MAIN) != LV_LAYOUT_FLEX) return false;
  lv_flex_flow_t f = lv_obj_get_style_flex_flow(p, LV_PART_MAIN);
  return !(f & LV_FLEX_COLUMN);
}
// The width of `o`'s content, worked out from the styles -- or -1 where it
// hangs on its content or a row's flex grow. A new box's own size is only known
// after a layout pass, and LVGL lays out children before their parent, so
// each box sized in % of the one above adds a whole pass over the screen.
static int32_t innerW(lv_obj_t* o) {
  if (!o || growsWide(o)) return -1;
  int32_t w = lv_obj_get_style_width(o, LV_PART_MAIN);
  if (LV_COORD_IS_PCT(w)) {
    int32_t pw = innerW(lv_obj_get_parent(o));
    if (pw < 0) return -1;
    w = pw * LV_COORD_GET_PCT(w) / 100;
  } else if (!LV_COORD_IS_PX(w)) {
    return -1;
  }
  w -= lv_obj_get_style_pad_left(o, LV_PART_MAIN) + lv_obj_get_style_pad_right(o, LV_PART_MAIN);
  lv_border_side_t side = lv_obj_get_style_border_side(o, LV_PART_MAIN);
  int32_t bw = lv_obj_get_style_border_width(o, LV_PART_MAIN);
  if (side & LV_BORDER_SIDE_LEFT) w -= bw;
  if (side & LV_BORDER_SIDE_RIGHT) w -= bw;
  return w;
}
extern "C" bool lv_obj_refr_size(lv_obj_t* obj);   // lv_obj_pos.c, in no header
// A wrapping label's size settled now, `w` wide. LVGL's first layout of a
// label works out its height at the width it had before -- 0 for a new one,
// a line per letter -- and only then measures it again at the new width.
static void labelFit(lv_obj_t* l, int32_t w) {
  l->coords.x2 = l->coords.x1 + w - 1;
  lv_obj_refr_size(l);
}
// A flex row whose one growing child gets its width before the first layout
// (rowsFit(), further down).
static bool s_fit_pending;
static lv_obj_t* s_fit_screen;   // newScreen()'s, maybe still fading in: not lv_screen_active() yet
static void fitLater(lv_obj_t* row) {
  lv_obj_add_flag(row, LV_OBJ_FLAG_USER_2);
  s_fit_pending = true;
}
// The parent's full width, in pixels when that's known now (see innerW()).
static void fillWidth(lv_obj_t* o) {
  int32_t w = innerW(lv_obj_get_parent(o));
  lv_obj_set_width(o, w >= 0 ? w : LV_PCT(100));
  if (w > 0 && lv_obj_check_type(o, &lv_label_class)) labelFit(o, w);
}

static lv_obj_t* label(lv_obj_t* parent, const char* text, const lv_font_t* font, uint32_t color) {
  lv_obj_t* l = lv_label_create(parent);
  lv_label_set_text(l, text);
  lv_obj_set_style_text_font(l, font, 0);
  lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
  return l;
}
// Whether label `l` holds `text`. A LV_LABEL_LONG_DOT one that's cut short
// has the "..." written into its own copy of the text, the bytes they cover
// kept aside (lv_label_set_dots): it's read back past them.
static bool labelShows(lv_obj_t* l, const char* text) {
  const lv_label_t* lb = (const lv_label_t*)l;
  const char* cur = lb->text;
  if (!cur) return false;
  uint32_t d = lb->dot_begin;
  if (d == 0xFFFFFFFF || lb->static_txt) return strcmp(cur, text) == 0;   // no dots (lv_label.c LV_LABEL_DOT_BEGIN_INV)
  if (strncmp(cur, text, d) != 0) return false;
  size_t k = strnlen(lb->dot, sizeof(lb->dot));   // the bytes under the dots
  if (strncmp(lb->dot, text + d, k) != 0) return false;
  return k < sizeof(lb->dot) ? text[d + k] == '\0' : strcmp(cur + d + k, text + d + k) == 0;
}
// A label's text, set only when it differs: LVGL redraws a label on every
// set_text, the same text or not, and the refreshes that run every second
// mostly write back what's already there.
static void setText(lv_obj_t* l, const char* text) {
  if (labelShows(l, text)) return;
  lv_label_set_text(l, text);
}
// The same for a text colour: LVGL restyles, and redraws, on every set.
static void setTextColor(lv_obj_t* l, uint32_t color) {
  lv_color_t c = lv_color_hex(color);
  if (!lv_color_eq(lv_obj_get_style_text_color(l, LV_PART_MAIN), c)) lv_obj_set_style_text_color(l, c, 0);
}
static void setTextFmt(lv_obj_t* l, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void setTextFmt(lv_obj_t* l, const char* fmt, ...) {
  char b[160];
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(b, sizeof(b), fmt, ap);
  va_end(ap);
  setText(l, b);
}
// A sentence across the parent's width, wrapping: a hint, a note, a status.
static lv_obj_t* noteLabel(lv_obj_t* parent, const char* text, const lv_font_t* font = THEME_FONT_SMALL,
                           uint32_t color = theme::TEXT_MUTED) {
  lv_obj_t* l = label(parent, text, font, color);
  lv_label_set_long_mode(l, LV_LABEL_LONG_WRAP);
  fillWidth(l);
  return l;
}

// A list filling the rest of a flex column, scrolling on its own (the
// scrollbar only while it moves).
static lv_obj_t* scrollList(lv_obj_t* parent) {
  lv_obj_t* l = lv_obj_create(parent);
  styleSurface(l, theme::BG);
  fillWidth(l);
  lv_obj_set_flex_grow(l, 1);
  lv_obj_set_flex_flow(l, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(l, theme::GAP, 0);
  lv_obj_set_scrollbar_mode(l, LV_SCROLLBAR_MODE_ACTIVE);
  return l;
}

// The dimmed full-screen layer under a popup; it swallows taps on what's below.
static lv_obj_t* dimOverlay(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(o, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_60, 0);
  lv_obj_add_flag(o, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

// LV_EVENT_COVER_CHECK: an opaque thing LVGL can't tell is (an image, a box
// drawn by its children) hides all under its own area, and LVGL then starts
// drawing from it.
static void coversOwnArea(lv_event_t* e) {
  lv_cover_check_info_t* info = (lv_cover_check_info_t*)lv_event_get_param(e);
  lv_area_t a;
  lv_obj_get_coords((lv_obj_t*)lv_event_get_current_target(e), &a);
  const lv_area_t* r = info->area;
  if (info->res != LV_COVER_RES_MASKED && r->x1 >= a.x1 && r->y1 >= a.y1 && r->x2 <= a.x2 && r->y2 <= a.y2)
    info->res = LV_COVER_RES_COVER;
}

static lv_obj_t* s_status_bar = nullptr;   // on the top layer, opaque
// Where the screen shows below the status bar (its top when the bar is hidden).
static int32_t belowStatusBar() {
  return s_status_bar && !lv_obj_has_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN) ? lv_obj_get_y2(s_status_bar) + 1 : 0;
}
// Where what drops in from the top (a toast, the message banner) goes: under
// the status bar, or under a screen's title bar, which takes its place.
static int32_t belowTopBar() {
  return s_status_bar && lv_obj_has_flag(s_status_bar, LV_OBJ_FLAG_HIDDEN) ? theme::APPBAR_H : theme::STATUS_H;
}
// Home's tiles: `rows` of them fill the page between the status bar and the
// page dots, less `reserve` px left under them (a hint line).
static const int HOME_DOTS_H = 26;
static int homeTileH(int rows, int reserve = 0) {
  const int page = lv_display_get_vertical_resolution(NULL) - theme::STATUS_H - theme::PAD - HOME_DOTS_H;
  return (page - reserve - (rows - 1) * theme::GAP) / rows;
}
// What keeps the status bar up over a title bar (UITask::statusBarSync).
static bool s_sb_panel = false, s_sb_lock = false;

// The screen under an open popup, frozen: once the dim is up, the screen is
// drawn once into an image, darkened as the dim layer darkens it, and shown
// opaque on top of the screen with the dim layer cleared -- LVGL then draws
// nothing below it. Live, the screen plus a full-screen blend under the popup
// cost ~28 ms a frame. One popup at a time; the image goes with its popup, and
// the popup dims live again if its screen goes first. Any dim level: the
// layer's own when it's frozen.
namespace freeze {
  static lv_obj_t* s_img = nullptr;      // the frozen screen, on that screen
  static lv_obj_t* s_owner = nullptr;    // the popup's dim layer
  static lv_obj_t* s_strip = nullptr;    // the status bar's dim (a popup on the top layer)
  static lv_timer_t* s_retry = nullptr;  // waiting for the screen to stop moving
  static lv_opa_t s_dim = LV_OPA_60;     // the dim layer's, frozen into the image

  static void imgDeleted(lv_event_t* e) {
    lv_draw_buf_destroy((lv_draw_buf_t*)lv_event_get_user_data(e));
    s_img = nullptr;
    if (s_owner) {   // the screen went, the popup stays
      if (s_strip) lv_obj_delete(s_strip);
      lv_obj_set_style_bg_opa(s_owner, s_dim, 0);
      s_owner = s_strip = nullptr;
    }
  }
  static void ownerDeleted(lv_event_t* e) {
    if (s_retry && lv_timer_get_user_data(s_retry) == lv_event_get_target(e)) { lv_timer_delete(s_retry); s_retry = nullptr; }
    if (s_owner != lv_event_get_target(e)) return;
    s_owner = s_strip = nullptr;
    if (s_img) lv_obj_delete(s_img);
  }
  static bool moving(lv_obj_t* scr) {   // a screen still sliding / fading in
    if (lv_display_get_screen_prev(NULL)) return true;
    for (uint32_t i = 0; i < lv_obj_get_child_count(scr); i++)
      if (lv_anim_get(lv_obj_get_child(scr, i), NULL)) return true;
    return false;
  }
  static void take(lv_obj_t* overlay);
  static void retry(lv_timer_t* t) {
    s_retry = nullptr;
    take((lv_obj_t*)lv_timer_get_user_data(t));
  }
  // Darkened into the display's own byte order (UI_FLUSH_DMA: big-endian;
  // lv_snapshot can't make that one itself): drawn then as a plain copy.
#ifdef UI_FLUSH_DMA
  static inline uint16_t out565(uint16_t c) { return __builtin_bswap16(c); }
#else
  static inline uint16_t out565(uint16_t c) { return c; }
#endif
  static void darken(lv_draw_buf_t* buf) {   // as the dim layer over it would
    const uint32_t keep = 256 - s_dim;   // what's left under the black
    for (uint32_t y = 0; y < buf->header.h; y++) {
      uint16_t* p = (uint16_t*)(buf->data + y * buf->header.stride);
      for (uint32_t x = 0; x < buf->header.w; x++) {
        uint32_t c = p[x];
        p[x] = out565((uint16_t)(((((c >> 11) * keep) >> 8) << 11) | (((((c >> 5) & 0x3F) * keep) >> 8) << 5) |
                                 (((c & 0x1F) * keep) >> 8)));
      }
    }
#ifdef UI_FLUSH_DMA
    buf->header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
#endif
  }
  static lv_draw_buf_t* snap(lv_obj_t* overlay, lv_obj_t* scr, bool top) {
    if (!top) lv_obj_add_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    lv_draw_buf_t* buf = lv_snapshot_take(scr, LV_COLOR_FORMAT_RGB565);
    if (!top) lv_obj_remove_flag(overlay, LV_OBJ_FLAG_HIDDEN);
    return buf;
  }
  static void show(lv_obj_t* overlay, lv_obj_t* scr, bool top, lv_draw_buf_t* buf) {
    s_img = lv_image_create(scr);
    lv_image_set_src(s_img, buf);
    lv_obj_add_flag(s_img, LV_OBJ_FLAG_IGNORE_LAYOUT);
    lv_obj_remove_flag(s_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_pos(s_img, 0, 0);
    lv_obj_add_event_cb(s_img, imgDeleted, LV_EVENT_DELETE, buf);
    lv_obj_add_event_cb(s_img, coversOwnArea, LV_EVENT_COVER_CHECK, NULL);   // no background: its own check says no
    if (!top) lv_obj_move_foreground(overlay);   // the image under the popup, over the rest
    s_owner = overlay;
  }
  static void dimmed(lv_obj_t* overlay, bool top) {   // the image holds the dim now
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    if (top && belowStatusBar()) {   // the status bar, on the top layer too, stays dimmed
      s_strip = lv_obj_create(overlay);
      lv_obj_remove_style_all(s_strip);
      lv_obj_remove_flag(s_strip, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_add_flag(s_strip, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_set_size(s_strip, LV_PCT(100), theme::STATUS_H);
      lv_obj_set_style_bg_color(s_strip, lv_color_hex(0x000000), 0);
      lv_obj_set_style_bg_opa(s_strip, s_dim, 0);
    }
  }
  static void take(lv_obj_t* overlay) {
    if (s_img) return;   // a popup over a popup: dimmed live
    lv_obj_t* scr = lv_obj_get_screen(overlay);
    bool top = scr == lv_layer_top();
    if (top) scr = lv_screen_active();
    if (moving(scr)) {
      s_retry = lv_timer_create(retry, 60, overlay);
      lv_timer_set_repeat_count(s_retry, 1);
      return;
    }
    s_dim = lv_obj_get_style_bg_opa(overlay, LV_PART_MAIN);
    lv_draw_buf_t* buf = snap(overlay, scr, top);
    if (!buf) return;   // no memory: dimmed live
    darken(buf);
    show(overlay, scr, top, buf);
    dimmed(overlay, top);
  }
  static void onDimUp(lv_anim_t* a) { take((lv_obj_t*)a->var); }
  // A popup opening over a still screen: frozen and dimmed at once, so the
  // panel's rise redraws only the panel -- a dim fading up over the live
  // screen redrew all of it every frame (~45 ms each). False when the screen
  // is still moving or there's no memory: the caller fades the dim up.
  static bool early(lv_obj_t* overlay, lv_opa_t dim) {
    if (s_img) return false;
    lv_obj_t* scr = lv_obj_get_screen(overlay);
    bool top = scr == lv_layer_top();
    if (top) scr = lv_screen_active();
    if (moving(scr)) return false;
    lv_draw_buf_t* buf = snap(overlay, scr, top);
    if (!buf) return false;
    s_dim = dim;
    darken(buf);
    show(overlay, scr, top, buf);
    dimmed(overlay, top);
    return true;
  }
  // A dim layer that's up at once, without the fade: frozen now.
  static void now(lv_obj_t* overlay) {
    lv_obj_add_event_cb(overlay, ownerDeleted, LV_EVENT_DELETE, NULL);
    take(overlay);
  }
}

// A popup: the dimmed layer (into `overlay`, to close it by) and its panel --
// the page colour, the accent hairline, a flex column -- rising into view.
// POP_FIT: as tall as its content, centred below the status bar, scrolling
// past that height. POP_FULL: all of that height; its own list scrolls.
// POP_TOP: as tall as its content, under the status bar, clear of a keyboard.
// POP_BOTTOM: as tall as its content, at the bottom, nothing dimmed -- for
// what's shown on the screen above it (a map area's preview).
enum PopFit : uint8_t { POP_FIT, POP_FULL, POP_TOP, POP_BOTTOM };
// The next popup is already up: back to it from a page it led to, it is
// there at once -- dimmed in full, no rise -- not opening again.
static bool s_pop_still = false;
static lv_obj_t* popupOpen(lv_obj_t* parent, PopFit fit, lv_obj_t*& overlay, int32_t inset = 8) {
  overlay = dimOverlay(parent);
  s_fit_pending = true;   // rowsFit()
  lv_obj_t* panel = lv_obj_create(overlay);
  int32_t w = lv_display_get_horizontal_resolution(NULL), h = lv_display_get_vertical_resolution(NULL);
  lv_obj_set_width(panel, w - 2 * inset);
  if (fit == POP_FULL) {
    lv_obj_set_height(panel, h - theme::STATUS_H - 12);
    lv_obj_set_pos(panel, inset, theme::STATUS_H + 6);
  } else if (fit == POP_TOP) {
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);
  } else if (fit == POP_BOTTOM) {
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_align(panel, LV_ALIGN_BOTTOM_MID, 0, -inset);
  } else {
    lv_obj_set_height(panel, LV_SIZE_CONTENT);
    lv_obj_set_style_max_height(panel, h - theme::STATUS_H - 12, 0);
    lv_obj_align(panel, LV_ALIGN_CENTER, 0, theme::STATUS_H / 2);
    lv_obj_set_scrollbar_mode(panel, LV_SCROLLBAR_MODE_ACTIVE);
  }
  if (fit != POP_FIT) lv_obj_remove_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(panel, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_border_color(panel, lv_color_hex(theme::EDGE), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(panel, theme::PAD, 0);
  lv_obj_set_style_pad_row(panel, 4, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  const bool still = s_pop_still;
  s_pop_still = false;
  if (fit == POP_BOTTOM) {   // over a live screen, nothing dimmed
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    anim::rise(panel);
  } else {
    lv_obj_add_event_cb(overlay, freeze::ownerDeleted, LV_EVENT_DELETE, NULL);
    if (still) { lv_obj_set_style_bg_opa(overlay, LV_OPA_60, 0); freeze::take(overlay); }   // frozen as it looks
    else if (freeze::early(overlay, LV_OPA_60)) anim::rise(panel);
    else anim::popup(overlay, LV_OPA_60, freeze::onDimUp);
  }
  return panel;
}

// ── Info cards ──
// Label / value rows on a card (Diagnostics, GPS, node detail, About): the
// label small and muted on the left, the value on the right -- wrapping,
// right-aligned, when it's long -- and a hairline between rows. infoRow()
// returns the value label to update in place; infoSet() also hides its row
// while there's nothing to show.
static lv_obj_t* infoCard(lv_obj_t* parent) {
  lv_obj_t* c = lv_obj_create(parent);
  styleSurface(c, theme::SURFACE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
  fillWidth(c);
  lv_obj_set_height(c, LV_SIZE_CONTENT);
  lv_obj_set_style_radius(c, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(c, theme::PAD, 0);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(c, 0, 0);   // the rows' own padding spaces them, around the hairline
  return c;
}

// A credit line that wraps cleanly: "(c) Name" and "CC-BY-SA" never split.
static const char* creditText(const char* in) {
  static char out[160];
  size_t o = 0;
  int paren = 0;
  for (const char* p = in; *p && o + 4 < sizeof(out); p++) {
    if (*p == '(') paren++;
    else if (*p == ')' && paren) paren--;
    if ((uint8_t)p[0] == 0xC2 && (uint8_t)p[1] == 0xA9 && p[2] == ' ') {   // "(c) " -> "(c)" + no-break space
      memcpy(out + o, "\xC2\xA9\xC2\xA0", 4); o += 4; p += 2;
    } else if (*p == '-' && paren) {   // licence names: a non-breaking hyphen
      memcpy(out + o, "\xE2\x80\x91", 3); o += 3;
    } else {
      out[o++] = *p;
    }
  }
  out[o] = '\0';
  return out;
}

static lv_obj_t* infoLine(lv_obj_t* card) {   // one row's box, the hairline above all but the first
  lv_obj_t* r = lv_obj_create(card);
  lv_obj_remove_style_all(r);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  fillWidth(r);
  lv_obj_set_height(r, LV_SIZE_CONTENT);
  lv_obj_set_style_pad_ver(r, 5, 0);
  if (lv_obj_has_flag(card, LV_OBJ_FLAG_USER_1)) lv_obj_set_style_pad_hor(r, theme::PAD, 0);   // in a group()
  if (lv_obj_get_index(r) > 0) {
    lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
    lv_obj_set_style_border_width(r, 1, 0);
    lv_obj_set_style_border_color(r, lv_color_hex(theme::SURFACE_2), 0);
  }
  return r;
}

static lv_obj_t* infoRow(lv_obj_t* card, const char* key, const char* value, uint32_t col = theme::TEXT,
                         const lv_font_t* font = THEME_FONT_BODY) {
  lv_obj_t* r = infoLine(card);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 8, 0);
  lv_obj_t* k = label(r, key, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(k, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(k, 1);
  lv_obj_set_flex_grow(k, 1);
  lv_obj_t* v = label(r, value, font, col);
  lv_label_set_long_mode(v, LV_LABEL_LONG_WRAP);
  lv_obj_set_width(v, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(v, LV_PCT(66), 0);
  lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
  fitLater(r);
  return v;
}

// A row whose text is a sentence (credits, notes): the label over it.
static lv_obj_t* infoNote(lv_obj_t* card, const char* key, const char* text) {
  lv_obj_t* r = infoLine(card);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(r, 2, 0);
  label(r, key, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_t* t = noteLabel(r, text, THEME_FONT_SMALL, theme::TEXT);
  return t;
}

static lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text);

static void infoSet(lv_obj_t* value, const char* text) {
  lv_obj_t* row = lv_obj_get_parent(value);
  if (!text || !text[0]) { lv_obj_add_flag(row, LV_OBJ_FLAG_HIDDEN); return; }
  lv_obj_remove_flag(row, LV_OBJ_FLAG_HIDDEN);
  setText(value, text);
}

// A bare flex row / column sized to its content, taps passing through.
static lv_obj_t* flexBox(lv_obj_t* parent, lv_flex_flow_t flow) {
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_style_all(b);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(b, flow);
  return b;
}

// A full-width row of centred items that grows with them (button rows in
// popups, the clock's tabs and actions).
static lv_obj_t* centerRow(lv_obj_t* parent) {
  lv_obj_t* r = lv_obj_create(parent);
  styleSurface(r, theme::BG);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, theme::GAP, 0);
  return r;
}

// A small pill button with a label, right-aligned in its parent row.
static lv_obj_t* pillButton(lv_obj_t* row, const char* text, uint32_t bg, lv_event_cb_t cb, uintptr_t user) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 24);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)user);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT));
  return b;
}

// ── Settings kit ──
// Every settings page is built from the same few pieces: group() -- a section
// title and the card its rows sit on, hairlines between them -- and rows on
// it: settingRow() (label, hint under it, a control on the right: switch,
// choice, slider, segmented), listRow() (opens something: a chevron),
// actionRow() (does something now) and groupNote() (one line under a card).
// Rows built outside a group keep the older look: a card of their own.

static inline bool isGroup(lv_obj_t* o) { return o && lv_obj_has_flag(o, LV_OBJ_FLAG_USER_1); }

static lv_obj_t* group(lv_obj_t* parent, const char* title) {
  if (title) sectionTitle(parent, title);
  lv_obj_t* c = infoCard(parent);
  lv_obj_set_style_pad_hor(c, 0, 0);   // rows pad themselves: a pressed row lights edge to edge
  lv_obj_add_flag(c, LV_OBJ_FLAG_USER_1);
  return c;
}

// A row that lights up under the finger. In a group() the card clips its
// corners while a row is held, so the first / last row's light is rounded
// with it -- only then: clipping a card's corners costs a few ms every frame.
static void groupPressClip(lv_event_t* e) {
  lv_obj_t* card = lv_obj_get_parent((lv_obj_t*)lv_event_get_current_target(e));
  if (isGroup(card)) lv_obj_set_style_clip_corner(card, lv_event_get_code(e) == LV_EVENT_PRESSED, 0);
}
static void rowPressable(lv_obj_t* row) {
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_bg_opa(row, LV_OPA_COVER, LV_STATE_PRESSED);
  lv_obj_add_event_cb(row, groupPressClip, LV_EVENT_PRESSED, NULL);
  lv_obj_add_event_cb(row, groupPressClip, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(row, groupPressClip, LV_EVENT_PRESS_LOST, NULL);
}

static lv_obj_t* groupNote(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = noteLabel(parent, text);
  lv_obj_set_style_pad_hor(l, theme::PAD, 0);
  return l;
}

// A group row's box: full width, at least a touch target tall, a flex row
// with the text column growing and whatever follows it on the right.
static lv_obj_t* groupLine(lv_obj_t* card, bool tappable) {
  lv_obj_t* r = infoLine(card);
  lv_obj_set_style_pad_hor(r, theme::PAD, 0);
  lv_obj_set_style_pad_ver(r, 6, 0);
  lv_obj_set_style_min_height(r, theme::ROW_H, 0);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, 8, 0);
  if (tappable) rowPressable(r);
  return r;
}

// The label (and the hint under it, wrapping) filling a group row's left side.
static lv_obj_t* groupText(lv_obj_t* row, const char* text, const char* hint, uint32_t col = theme::TEXT) {
  lv_obj_t* t = flexBox(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(t, 1);
  lv_obj_set_flex_grow(t, 1);
  lv_obj_set_style_pad_row(t, 1, 0);
  // The row's touch-target height lives here: a row's own min height comes
  // after its flex layout, which then sits everything at the top.
  lv_obj_set_style_min_height(t, theme::ROW_H - 12, 0);
  lv_obj_set_flex_align(t, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
  lv_obj_t* l = noteLabel(t, text, THEME_FONT_BODY, col);
  if (hint) {
    noteLabel(t, hint);
  }
  lv_obj_set_user_data(row, l);   // rowTitle()
  fitLater(row);
  return l;
}

// A group row's text column grows into what the controls on its right leave,
// and LVGL lays out children before their parent: its labels were first
// measured 1 px wide (a line per letter), then at the real width, and each
// height that changed took another pass up the screen -- four in all. Before
// a frame's layout, rowsFit() gives each new column the width the flex will:
// its row's minus the controls', worked out from their styles and text. The
// grow stays, so a control this can't size only costs the old passes.
static constexpr lv_obj_flag_t NOT_LAID_OUT =
    (lv_obj_flag_t)(LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_IGNORE_LAYOUT);

// `o`'s outer width as the layout will make it; -1 when that's unclear.
static int32_t layoutW(lv_obj_t* o) {
  int32_t w = lv_obj_get_style_width(o, LV_PART_MAIN);
  if (LV_COORD_IS_PX(w)) {
    // as it is
  } else if (w != LV_SIZE_CONTENT) {
    return -1;
  } else if (lv_obj_check_type(o, &lv_label_class)) {
    lv_obj_refr_size(o);   // the label's own measure, cached for the layout
    w = lv_obj_get_width(o);
  } else if (lv_obj_get_style_layout(o, LV_PART_MAIN) == LV_LAYOUT_FLEX &&
             lv_obj_get_style_flex_flow(o, LV_PART_MAIN) == LV_FLEX_FLOW_ROW) {
    int32_t gap = lv_obj_get_style_pad_column(o, LV_PART_MAIN), n = 0;
    w = lv_obj_get_style_pad_left(o, LV_PART_MAIN) + lv_obj_get_style_pad_right(o, LV_PART_MAIN);
    for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
      lv_obj_t* c = lv_obj_get_child(o, i);
      if (lv_obj_has_flag_any(c, NOT_LAID_OUT)) continue;
      int32_t cw = layoutW(c);
      if (cw < 0) return -1;
      w += cw + (n++ ? gap : 0);
    }
    w = LV_CLAMP(lv_obj_get_style_min_width(o, LV_PART_MAIN), w, lv_obj_get_style_max_width(o, LV_PART_MAIN));
  } else {
    return -1;
  }
  return w + lv_obj_get_style_margin_left(o, LV_PART_MAIN) + lv_obj_get_style_margin_right(o, LV_PART_MAIN);
}

static void rowFit(lv_obj_t* row) {
  lv_obj_refr_size(row);   // its own width now: a child's % size is of it
  int32_t w = innerW(row), gap = lv_obj_get_style_pad_column(row, LV_PART_MAIN);
  if (w < 0) return;
  lv_obj_t* grow = nullptr;
  for (uint32_t i = 0; i < lv_obj_get_child_count(row); i++) {
    lv_obj_t* c = lv_obj_get_child(row, i);
    if (lv_obj_has_flag_any(c, NOT_LAID_OUT)) continue;
    if (lv_obj_get_style_flex_grow(c, LV_PART_MAIN)) {
      if (grow) return;   // one grows, or the share isn't worth working out
      grow = c;
      continue;
    }
    int32_t cw = layoutW(c);
    if (cw < 0) return;
    w -= cw + gap;
  }
  if (!grow || w < 1) return;
  lv_obj_set_width(grow, w);
  if (lv_obj_check_type(grow, &lv_label_class)) { labelFit(grow, w); return; }
  lv_obj_refr_size(grow);
  for (uint32_t i = 0; i < lv_obj_get_child_count(grow); i++) {   // its labels, 100 % of it
    lv_obj_t* l = lv_obj_get_child(grow, i);
    if (lv_obj_check_type(l, &lv_label_class)) labelFit(l, w);
  }
}

// ... and every label given a width in pixels (not one that grows), which labelFit() then measures
// at it (the screen titles, a list row's cut-off name ...).
static void rowsFit(lv_obj_t* root) {
  lv_obj_tree_walk(root, [](lv_obj_t* o, void*) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_2)) {
      lv_obj_remove_flag(o, LV_OBJ_FLAG_USER_2);
      rowFit(o);
    } else if (lv_obj_check_type(o, &lv_label_class) && !lv_obj_get_style_flex_grow(o, LV_PART_MAIN)) {
      // (a growing one's px width is a stand-in: the flex's, or its row's rowFit())
      int32_t w = lv_obj_get_style_width(o, LV_PART_MAIN);
      if (LV_COORD_IS_PX(w) && w > 0 && lv_obj_get_width(o) != w) labelFit(o, w);
    }
    return LV_OBJ_TREE_WALK_NEXT;
  }, nullptr);
}
static void rowsFitPending() {
  if (!s_fit_pending) return;
  s_fit_pending = false;
  if (s_fit_screen && s_fit_screen != lv_screen_active()) rowsFit(s_fit_screen);
  rowsFit(lv_screen_active());
  rowsFit(lv_layer_top());
}
// lv_obj_update_layout() for a screen just built: its rows fitted first.
static void layoutNow(lv_obj_t* o) {
  rowsFitPending();
  lv_obj_update_layout(o);
}

// The title label of a settingRow() / listRow(), to recolour or rename it.
static lv_obj_t* rowTitle(lv_obj_t* row) { return (lv_obj_t*)lv_obj_get_user_data(row); }

// Rewrites the hint under a row's title in place (the label made right after
// it) -- a rebuild would cut short the animation of the switch just tapped.
static void rowHintSet(lv_obj_t* row, const char* hint) {
  lv_obj_t* h = lv_obj_get_sibling(rowTitle(row), 1);
  if (h && lv_obj_check_type(h, &lv_label_class)) lv_label_set_text(h, hint);
}

// A settings row: the label left (a muted hint under it, cut at `hint_w`),
// room on the right for a switch / dropdown / slider.
static lv_obj_t* settingRow(lv_obj_t* parent, const char* text, const char* hint, int hint_w = 150) {
  if (isGroup(parent)) {
    lv_obj_t* row = groupLine(parent, false);
    groupText(row, text, hint);
    return row;
  }
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::SURFACE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_t* t = label(row, text, THEME_FONT_BODY, theme::TEXT);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, theme::PAD, hint ? 4 : 13);
  lv_obj_set_user_data(row, t);
  if (hint) {
    lv_obj_t* h = label(row, hint, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_set_size(h, hint_w, 18);   // fixed height: cut, don't wrap onto the label -- the font's whole line, or the tails of y and g are cut
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -4);
  }
  return row;
}

// A slider on the right of a settingRow(). Its track is the theme's 20 %
// accent tint worked out over the card, opaque: blended, it cost ~3 ms a frame.
static lv_obj_t* rowSlider(lv_obj_t* row, int32_t min, int32_t max, int32_t v) {
  lv_obj_t* sl = lv_slider_create(row);
  lv_slider_set_range(sl, min, max);
  lv_slider_set_value(sl, v, LV_ANIM_OFF);
  lv_obj_set_size(sl, 150, 8);
  lv_obj_set_style_margin_right(sl, 8, 0);
  lv_obj_set_ext_click_area(sl, 14);
  lv_obj_set_style_bg_color(sl, lv_color_hex(theme::mix(theme::ACCENT, theme::SURFACE, 20)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(sl, LV_OPA_COVER, LV_PART_MAIN);
  return sl;
}

// Does something at once (Reboot, Delete ...): its text in `col`, no chevron.
// Returns the label, for a confirm-by-second-tap text.
static lv_obj_t* actionRow(lv_obj_t* card, const char* text, lv_event_cb_t cb, void* user,
                           uint32_t col = theme::TEXT) {
  lv_obj_t* r = groupLine(card, true);
  lv_obj_t* l = groupText(r, text, nullptr, col);
  lv_obj_add_event_cb(r, cb, LV_EVENT_CLICKED, user);
  return l;
}

// ── Choice: a value picked from a list ──
// In place of a dropdown: the value and a chevron; a tap opens the options
// over the screen, and a pick sends LV_EVENT_VALUE_CHANGED like a dropdown.
struct Choice {
  char* opts;          // "\n"-separated, owned
  uint16_t sel, count;
  char title[32];
  lv_obj_t* value;
};
static lv_obj_t* s_pick_overlay = nullptr;
static lv_obj_t* s_pick_target = nullptr;

static Choice* choiceOf(lv_obj_t* c) { return (Choice*)lv_obj_get_user_data(c); }

// Option `i` of a "\n"-list, into buf.
static const char* choiceItem(const Choice* ch, int i, char* buf, size_t n) {
  const char* p = ch->opts;
  for (int k = 0; k < i && p; k++) { p = strchr(p, '\n'); if (p) p++; }
  if (!p) { buf[0] = '\0'; return buf; }
  const char* e = strchr(p, '\n');
  size_t len = e ? (size_t)(e - p) : strlen(p);
  if (len >= n) len = n - 1;
  memcpy(buf, p, len);
  buf[len] = '\0';
  return buf;
}

static void choiceShow(lv_obj_t* c) {
  Choice* ch = choiceOf(c);
  char v[48];
  lv_label_set_text(ch->value, choiceItem(ch, ch->sel, v, sizeof(v)));
}

static void choiceSetOptions(lv_obj_t* c, const char* opts) {
  Choice* ch = choiceOf(c);
  lv_free(ch->opts);
  size_t n = strlen(opts);
  ch->opts = (char*)lv_malloc(n + 1);
  memcpy(ch->opts, opts, n + 1);
  ch->count = 1;
  for (const char* p = opts; *p; p++) if (*p == '\n') ch->count++;
  if (ch->sel >= ch->count) ch->sel = 0;
  choiceShow(c);
}

static int choiceSelected(lv_obj_t* c) { return choiceOf(c)->sel; }
static void choiceSetSelected(lv_obj_t* c, int i) {
  Choice* ch = choiceOf(c);
  if (i < 0 || i >= ch->count) return;
  ch->sel = (uint16_t)i;
  choiceShow(c);
}

static void pickerClose() {
  if (s_pick_overlay) lv_obj_delete_async(s_pick_overlay);
  s_pick_overlay = s_pick_target = nullptr;
}

static void onPickOption(lv_event_t* e) {
  lv_obj_t* c = s_pick_target;
  pickerClose();
  if (!c) return;
  choiceSetSelected(c, (int)(uintptr_t)lv_event_get_user_data(e));
  lv_obj_send_event(c, LV_EVENT_VALUE_CHANGED, NULL);   // may rebuild the screen: c is gone after this
}

static void pickerOpen(lv_obj_t* c) {
  Choice* ch = choiceOf(c);
  pickerClose();
  s_pick_target = c;
  lv_obj_t* panel = popupOpen(lv_layer_top(), POP_FIT, s_pick_overlay, 24);
  lv_obj_add_event_cb(s_pick_overlay, [](lv_event_t* e) {   // a tap beside the list: nothing changes
    if (lv_event_get_target(e) == lv_event_get_current_target(e)) pickerClose();
  }, LV_EVENT_CLICKED, NULL);
  lv_obj_set_style_pad_all(panel, 6, 0);
  lv_obj_t* t = label(panel, ch->title, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_pad_hor(t, 4, 0);
  lv_obj_t* card = group(panel, nullptr);
  lv_obj_t* sel_row = nullptr;
  // Each option one label, a fixed one-line row: a time zone list is ~30 of
  // them, and a group row's box + text column + label took ~3 ms each.
  const int32_t H = 38, lh = lv_font_get_line_height(THEME_FONT_BODY), w = innerW(card);
  for (int i = 0; i < ch->count; i++) {
    char v[48];
    bool cur = i == ch->sel;
    lv_obj_t* r = label(card, choiceItem(ch, i, v, sizeof(v)), THEME_FONT_BODY, cur ? theme::ACCENT : theme::TEXT);
    lv_label_set_long_mode(r, LV_LABEL_LONG_CLIP);
    lv_obj_set_size(r, w > 0 ? w : LV_PCT(100), H);
    lv_obj_set_style_pad_left(r, theme::PAD, 0);
    lv_obj_set_style_pad_right(r, theme::PAD + 20, 0);   // the tick's room
    lv_obj_set_style_pad_top(r, (H - lh) / 2, 0);
    if (i > 0) {
      lv_obj_set_style_border_side(r, LV_BORDER_SIDE_TOP, 0);
      lv_obj_set_style_border_width(r, 1, 0);
      lv_obj_set_style_border_color(r, lv_color_hex(theme::SURFACE_2), 0);
    }
    rowPressable(r);
    if (cur) {
      lv_obj_t* t = label(r, LV_SYMBOL_OK, THEME_FONT_BODY, theme::ACCENT);
      lv_obj_add_flag(t, LV_OBJ_FLAG_IGNORE_LAYOUT);
      lv_obj_align(t, LV_ALIGN_RIGHT_MID, 20, 0);   // into the right padding
      sel_row = r;
    }
    lv_obj_add_event_cb(r, onPickOption, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
  if (sel_row) { layoutNow(panel); lv_obj_scroll_to_view_recursive(sel_row, LV_ANIM_OFF); }
}

// A choice: in a group row the value is bare text (accent) and a chevron;
// elsewhere it sits on a small surface of its own.
static lv_obj_t* choiceCreate(lv_obj_t* parent, const char* opts, int sel, const char* title) {
  lv_obj_t* c = lv_obj_create(parent);
  lv_obj_remove_style_all(c);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(c, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(c, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(c, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);   // END + SIZE_CONTENT mis-sizes
  lv_obj_set_style_pad_column(c, 6, 0);
  lv_obj_set_ext_click_area(c, 10);
  lv_obj_set_style_opa(c, LV_OPA_60, LV_STATE_PRESSED);
  if (!isGroup(lv_obj_get_parent(parent))) {
    lv_obj_set_style_bg_color(c, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(c, theme::RADIUS, 0);
    lv_obj_set_style_pad_hor(c, 10, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
  }
  Choice* ch = (Choice*)lv_malloc(sizeof(Choice));
  memset(ch, 0, sizeof(*ch));
  snprintf(ch->title, sizeof(ch->title), "%s", title ? title : "");
  ch->value = label(c, "", THEME_FONT_BODY, theme::VALUE);
  lv_obj_set_style_max_width(ch->value, 140, 0);   // a long value wraps (LONG_DOT mis-sizes with SIZE_CONTENT)
  lv_obj_set_style_text_align(ch->value, LV_TEXT_ALIGN_RIGHT, 0);
  label(c, LV_SYMBOL_DOWN, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_user_data(c, ch);
  ch->sel = sel < 0 ? 0 : (uint16_t)sel;
  choiceSetOptions(c, opts);
  lv_obj_add_event_cb(c, [](lv_event_t* e) { pickerOpen((lv_obj_t*)lv_event_get_current_target(e)); },
                      LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(c, [](lv_event_t* e) {
    lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
    if (s_pick_target == o) pickerClose();
    Choice* ch = choiceOf(o);
    lv_free(ch->opts);
    lv_free(ch);
  }, LV_EVENT_DELETE, NULL);
  return c;
}

// A list's toolbar: one line over it, a muted text on the left (how many,
// what's missing) and its choices on the right (toolbarChoice). Returns the
// text, to update; its parent is the row.
static lv_obj_t* toolbarRow(lv_obj_t* parent) {
  lv_obj_t* row = flexBox(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(row, theme::GAP, 0);
  lv_obj_t* t = label(row, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 1);
  lv_obj_set_flex_grow(t, 1);
  return t;
}
// A choice in a toolbarRow(): the value and a chevron on a small surface,
// the options over the screen as any choice's.
static lv_obj_t* toolbarChoice(lv_obj_t* row, const char* opts, int sel, const char* title) {
  lv_obj_t* c = choiceCreate(row, opts, sel, title);
  lv_obj_set_style_pad_ver(c, 6, 0);
  lv_obj_set_style_pad_hor(c, 8, 0);
  lv_obj_set_style_text_font(choiceOf(c)->value, THEME_FONT_SMALL, 0);
  return c;
}

// One settings row: label left, a choice right.
static lv_obj_t* choiceRow(lv_obj_t* parent, const char* text, const char* hint, const char* options, int sel,
                           lv_event_cb_t cb, void* user = nullptr) {
  lv_obj_t* row = settingRow(parent, text, hint);
  lv_obj_t* c = choiceCreate(row, options, sel, text);
  lv_obj_align(c, LV_ALIGN_RIGHT_MID, -4, 0);
  lv_obj_add_event_cb(c, cb, LV_EVENT_VALUE_CHANGED, user);
  return c;
}

// A value typed in a popup (frequency, name ...): shown as a choice is, with
// a pencil; the tap is the caller's.
static lv_obj_t* rowValue(lv_obj_t* row, const char* text, lv_event_cb_t cb, void* user = nullptr) {
  lv_obj_t* c = flexBox(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(c, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(c, 6, 0);
  lv_obj_add_flag(c, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_ext_click_area(c, 10);
  lv_obj_set_style_opa(c, LV_OPA_60, LV_STATE_PRESSED);
  label(c, text, THEME_FONT_BODY, theme::VALUE);
  label(c, LV_SYMBOL_EDIT, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_add_event_cb(c, cb, LV_EVENT_CLICKED, user);
  return c;
}

// A tick on a listRow() (the one in use), left of its chevron.
static void rowCheck(lv_obj_t* row) {
  lv_obj_t* l = label(row, LV_SYMBOL_OK, THEME_FONT_BODY, theme::ACCENT);
  if (isGroup(lv_obj_get_parent(row))) lv_obj_move_to_index(l, (int32_t)lv_obj_get_child_count(row) - 2);
  else lv_obj_align(l, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
}

// The subtitle label of a listRow() / settingRow() with one, to update it.
static lv_obj_t* rowSub(lv_obj_t* row) {
  if (isGroup(lv_obj_get_parent(row))) return lv_obj_get_child(lv_obj_get_child(row, 0), 1);
  return lv_obj_get_child(row, 1);
}

// Settings pages: the schema's, then this UI's own. Display, Power and Time
// split the schema's "Display & power" page (shared with L1, left as it is)
// into what their titles say.
enum : uint8_t { PG_KEYBOARD = settings::PG_COUNT, PG_ABOUT, PG_DISPLAY, PG_POWER, PG_TIME, PG_ALL };

// A schema setting by its NodePrefs field; -1 if there's none.
#define SETTING(field) settings::indexOf(offsetof(NodePrefs, field))

// A setting's current value as its choice shows it ("UTC+2", "3.4 V" ...).
static const char* settingText(const NodePrefs& p, int idx, char* buf, int n) {
  buf[0] = '\0';
  return idx < 0 ? buf : settings::text(p, settings::ALL[idx], buf, n);
}
static lv_obj_t* s_sec_card[settings::SEC_COUNT];   // schemaRows()' groups, by section
static bool s_opts_from_map = false;                // Map options opened from the map: back returns there
static int s_nav_section = -1;                      // Map options: one section of them (from the map), -1 all
static int32_t s_settings_y = 0;                    // Settings' scroll, kept while a page of it is open

// A one-of-N choice: a row (or grid, with "\n" in the map) of checkable
// buttons, `sel` checked (-1: none). `item` is the unchecked button colour —
// SURFACE on a SURFACE_2 panel, SURFACE_2 on the page.
static lv_obj_t* segmented(lv_obj_t* parent, const char** map, int sel, int w, int h,
                           uint32_t item = theme::SURFACE_2) {
  lv_obj_t* m = lv_buttonmatrix_create(parent);
  lv_buttonmatrix_set_map(m, map);
  lv_buttonmatrix_set_button_ctrl_all(m, LV_BUTTONMATRIX_CTRL_CHECKABLE);
  lv_buttonmatrix_set_one_checked(m, true);
  if (sel >= 0) lv_buttonmatrix_set_button_ctrl(m, sel, LV_BUTTONMATRIX_CTRL_CHECKED);
  lv_obj_set_size(m, w, h);
  lv_obj_set_style_pad_all(m, 0, 0);
  lv_obj_set_style_pad_gap(m, 4, 0);
  lv_obj_set_style_bg_opa(m, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(m, 0, 0);
  lv_obj_set_style_bg_color(m, lv_color_hex(item), LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::SEL_BG), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::SEL_FG), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_font(m, THEME_FONT_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(m, theme::RADIUS, LV_PART_ITEMS);
  return m;
}

// A one-line text field, full width. The theme's padding leaves less than one
// line inside 34 px, which makes the field scroll vertically (text jumps as
// it's typed), so it's padded to fit exactly one body line: 34 = 2*1 border +
// 2*6 pad + 20. The theme draws the cursor only while FOCUSED.
static lv_obj_t* textField(lv_obj_t* parent, const char* placeholder = NULL) {
  lv_obj_t* ta = lv_textarea_create(parent);
  lv_textarea_set_one_line(ta, true);
  if (placeholder) lv_textarea_set_placeholder_text(ta, placeholder);
  lv_obj_set_size(ta, LV_PCT(100), 34);
  lv_obj_set_style_border_width(ta, 1, 0);
  lv_obj_set_style_pad_ver(ta, 6, 0);
  lv_obj_set_style_pad_hor(ta, 10, 0);
  lv_obj_set_scrollbar_mode(ta, LV_SCROLLBAR_MODE_OFF);
  lv_obj_set_style_border_color(ta, lv_color_hex(theme::ACCENT), LV_PART_CURSOR | LV_STATE_FOCUSED);
  lv_obj_set_style_border_width(ta, 2, LV_PART_CURSOR | LV_STATE_FOCUSED);
  return ta;
}
// A field's cursor shown (FOCUSED, blinking) or not. LVGL starts and stops
// the blink only as the cursor moves, so it's nudged here: left running on a
// field without focus, it redraws the field every 400 ms for nothing.
// (lv_keyboard_set_textarea() focuses the field it's given.)
static void fieldFocus(lv_obj_t* ta, bool on) {
  if (!ta) return;
  if (on) lv_obj_add_state(ta, LV_STATE_FOCUSED);
  else lv_obj_remove_state(ta, LV_STATE_FOCUSED);
  lv_textarea_set_cursor_pos(ta, lv_textarea_get_cursor_pos(ta));
}

// A field limited in UTF-8 bytes (user data: the cap) -- names and other text
// that is stored in a fixed buffer. LVGL's own limit counts characters.
static void onByteCapInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  size_t cap = (size_t)(uintptr_t)lv_event_get_user_data(e);
  if (ins && strlen(lv_textarea_get_text(ta)) + strlen(ins) > cap) lv_textarea_set_insert_replace(ta, "");
}

// The primary action of a screen or popup: full accent, ON_ACCENT text (Theme.h);
// call after its label is made.
static void stylePrimary(lv_obj_t* b) {
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(b); i++)
    lv_obj_set_style_text_color(lv_obj_get_child(b, i), lv_color_hex(theme::ON_ACCENT), 0);
}

// Something selected / on (a tab, a chip, a row): SEL_BG with SEL_FG text
// (Theme.h), or back to the surface with TEXT. Every label under it follows.
static void textUnder(lv_obj_t* o, uint32_t col) {
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) {
    lv_obj_t* c = lv_obj_get_child(o, i);
    if (lv_obj_check_type(c, &lv_label_class)) setTextColor(c, col);
    else textUnder(c, col);
  }
}
static void styleSelected(lv_obj_t* b, bool on) {
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::SEL_BG : theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::SEL_PRESS : theme::SURFACE_2), LV_STATE_PRESSED);
  textUnder(b, on ? theme::SEL_FG : theme::TEXT);
}

// A row of equal buttons at the foot of a popup or screen: buttonBar(), then
// barButton() for each -- `accent` for the main action, in the full accent
// (barButtonOn() flips it later; a button that is on, as Fav, takes
// styleSelected() instead). Returns the button; its label is child 0.
static lv_obj_t* buttonBar(lv_obj_t* parent) {
  lv_obj_t* bar = lv_obj_create(parent);
  styleSurface(bar, theme::BG);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(bar, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(bar, theme::GAP, 0);
  return bar;
}
static void barButtonOn(lv_obj_t* b, bool on) {
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::ACCENT : theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::mix(theme::ACCENT, theme::BG, 75) : theme::SURFACE_2), LV_STATE_PRESSED);
  textUnder(b, on ? theme::ON_ACCENT : theme::TEXT);
}
static lv_obj_t* barButton(lv_obj_t* bar, const char* text, lv_event_cb_t cb, uintptr_t user, bool accent = false) {
  lv_obj_t* b = lv_button_create(bar);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_pad_hor(b, 4, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)user);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT));
  barButtonOn(b, accent);
  return b;
}

// Asking before something that can't be undone: under navPopupPanel("...?"),
// what happens in a sentence and the red button that does it.
static lv_obj_t* confirmBody(lv_obj_t* panel, const char* text, const char* button, lv_event_cb_t cb, uintptr_t user) {
  noteLabel(panel, text);
  lv_obj_t* b = lv_button_create(panel);
  lv_obj_set_size(b, LV_PCT(100), 40);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::FAIL), 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)user);
  lv_obj_center(label(b, button, THEME_FONT_BODY, theme::TEXT));
  return b;
}

// A button that asks by itself before it destroys: the first tap puts `ask`
// on its label, a second tap within 3 s is the yes. Left alone, the label
// comes back and the next tap asks again. One button asks at a time.
static lv_obj_t* s_ask_lbl = nullptr;
static char s_ask_text[64];
static lv_timer_t* s_ask_timer = nullptr;
static void onAskDeleted(lv_event_t* e);
static void askReset() {
  if (s_ask_timer) { lv_timer_delete(s_ask_timer); s_ask_timer = nullptr; }
  if (!s_ask_lbl) return;
  lv_obj_remove_event_cb(s_ask_lbl, onAskDeleted);
  lv_label_set_text(s_ask_lbl, s_ask_text);
  s_ask_lbl = nullptr;
}
static void onAskDeleted(lv_event_t* e) { (void)e; s_ask_lbl = nullptr; askReset(); }
static bool tapConfirmed(lv_obj_t* lbl, const char* ask) {
  bool yes = lbl && lbl == s_ask_lbl;
  askReset();
  if (yes || !lbl) return yes;
  s_ask_lbl = lbl;
  snprintf(s_ask_text, sizeof(s_ask_text), "%s", lv_label_get_text(lbl));
  lv_label_set_text(lbl, ask);
  lv_obj_add_event_cb(lbl, onAskDeleted, LV_EVENT_DELETE, NULL);
  s_ask_timer = lv_timer_create([](lv_timer_t*) { s_ask_timer = nullptr; askReset(); }, 3000, NULL);
  lv_timer_set_repeat_count(s_ask_timer, 1);
  return false;
}

// "@[nick]" -> "@nick" in place, for one-line text (previews, quotes) where
// the bubble's highlight doesn't reach.
static void plainMentions(char* t) {
  char* w = t;
  for (const char* r = t; *r;) {
    const char* close = r[0] == '@' && r[1] == '[' ? strchr(r + 2, ']') : nullptr;
    if (close && close - r <= 34) {
      *w++ = '@';
      for (const char* q = r + 2; q < close;) *w++ = *q++;
      r = close + 1;
    } else {
      *w++ = *r++;
    }
  }
  *w = '\0';
}

// Unread badge: amber pill with a count, on the right edge of `parent`.
static void badge(lv_obj_t* parent, int n, bool overflow) {
  if (n <= 0) return;
  lv_obj_t* b = lv_obj_create(parent);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 20);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_pad_hor(b, 7, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  lv_obj_t* l = label(b, "", THEME_FONT_SMALL, theme::ON_ACCENT);
  lv_label_set_text_fmt(l, "%d%s", n, overflow ? "+" : "");
  lv_obj_center(l);
}

// A vlist row's badge (its third child): made the first time there's a
// count, then its count set, hidden at none.
static void rowBadge(lv_obj_t* row, int n, bool overflow) {
  lv_obj_t* b = lv_obj_get_child(row, 2);
  if (!b) { badge(row, n, overflow); return; }
  lv_obj_set_flag(b, LV_OBJ_FLAG_HIDDEN, n <= 0);
  if (n <= 0) return;
  char t[8];
  snprintf(t, sizeof(t), "%d%s", n, overflow ? "+" : "");
  setText(lv_obj_get_child(b, 0), t);
}

// The same on a Home tile: in its top-right corner, clear of the icon.
static void tileBadge(lv_obj_t* tile, int n) {
  if (n <= 0) return;
  badge(tile, n, false);
  lv_obj_align(lv_obj_get_child(tile, -1), LV_ALIGN_TOP_RIGHT, -2, 2);
}

// Local time (time zone + summer time), now or at `utc`; false before the clock is set.
static bool localTime(const NodePrefs* p, struct tm& out, uint32_t utc = rtc_clock.getCurrentTime()) {
  return localTm(utc, tzHoursAt(p, utc), out);
}
static const char* const MONTHS[] = { "Jan", "Feb", "Mar", "Apr", "May", "Jun",
                                      "Jul", "Aug", "Sep", "Oct", "Nov", "Dec" };
// "14:05", or "2:05" (+ " PM" with suffix) with Settings > 12-hour clock;
// with `seconds`, ":09" follows unless Settings > Time > Clock seconds is off.
static void fmtClock(char* b, size_t n, const struct tm& ti, const NodePrefs* p, bool suffix, bool seconds = false) {
  char sec[4] = "";
  if (seconds && p && !p->clock_hide_seconds) snprintf(sec, sizeof(sec), ":%02d", ti.tm_sec);
  if (p && p->clock_12h) {
    int h = ti.tm_hour % 12;
    snprintf(b, n, "%d:%02d%s%s", h ? h : 12, ti.tm_min, sec, suffix ? (ti.tm_hour < 12 ? " AM" : " PM") : "");
  } else {
    snprintf(b, n, "%02d:%02d%s", ti.tm_hour, ti.tm_min, sec);
  }
}
// "Thu 25 Sep 2026".
static void fmtDate(char* b, size_t n, const struct tm& ti) {
  static const char* DOW[] = { "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat" };
  snprintf(b, n, "%s %d %s %d", DOW[ti.tm_wday], ti.tm_mday, MONTHS[ti.tm_mon], ti.tm_year + 1900);
}

// Settings changed here reach flash once their screen is left (as on the L1:
// a run of switches is one write, and the flash wears less), just after the
// next screen has come in -- a save stalls the UI (0.1-0.6 s on the ESP32's
// SPIFFS, far less on LittleFS). Else when the screen goes off, or a minute on. NodePrefs
// itself changes at once; shutdown() saves whatever is pending.
static uint32_t s_prefs_dirty_ms = 0;   // when the first unsaved change came (0: none)
static uint32_t s_prefs_save_at = 0;    // the screen was left: save from then (0: not yet)
static void prefsSave() { if (!s_prefs_dirty_ms) s_prefs_dirty_ms = millis() | 1; }
static void prefsSaveSoon(uint32_t delay_ms) {
  if (s_prefs_dirty_ms && !s_prefs_save_at) s_prefs_save_at = (millis() + delay_ms) | 1;
}
static void prefsFlush() {
  if (!s_prefs_dirty_ms) return;
  bool due = s_prefs_save_at ? (int32_t)(millis() - s_prefs_save_at) >= 0 : millis() - s_prefs_dirty_ms >= 60000;
  if (!due) return;
  s_prefs_dirty_ms = s_prefs_save_at = 0;
  the_mesh.savePrefs();
}

// The big clock in the boot splash's lettering, as ui-new draws it on the
// OLED and e-ink (ui-core/Lettering.h: block digits in 3 px strokes): HH:MM
// at SC x, the seconds after them at SC_SEC x on the same baseline. The
// lettering leans a glyph row at a time, steps SC px tall at this size;
// here the lean is worked out per screen row, so it's a smooth slant.
#include "../ui-core/Lettering.h"
namespace bigclock {
static const int SC = 4, SC_SEC = 2;
struct Text { char hm[8]; char sec[4]; };

static int lean(int py, int h, int sc) { return (2 * sc * (h - 1 - py) + (h - 1) / 2) / (h - 1); }   // 2 glyph px at the top, 0 at the bottom
static int32_t width(const char* t, int sc) {
  const int w = lettering::textW(t, 0);
  return w <= 0 ? 0 : w * sc + ((int)strlen(t) - 1) * (sc + 1);
}
static void draw(lv_layer_t* layer, int32_t x, int32_t y, const char* t, int sc, lv_color_t col) {
  const int h = lettering::LOGO_H * sc;
  lv_draw_rect_dsc_t d;
  lv_draw_rect_dsc_init(&d);
  d.bg_color = col;
  for (const char* p = t; *p; p++) {
    const lettering::PixGlyph* g = lettering::pixGlyph(*p);
    if (!g) continue;
    const int gw = (int)strlen(g->rows[0]);
    for (int py = 0; py < h; ) {
      const int r = py / sc, off = lean(py, h, sc);
      int end = py + 1;   // the rows under it on the same glyph row, leaning as far
      while (end < h && end / sc == r && lean(end, h, sc) == off) end++;
      const char* row = g->rows[r];
      for (int c = 0; c < gw; c++) {   // runs of ink as one rect
        if (row[c] != '#') continue;
        int e = c;
        while (e + 1 < gw && row[e + 1] == '#') e++;
        lv_area_t a = { x + off + c * sc, y + py, x + off + (e + 1) * sc - 1, y + end - 1 };
        lv_draw_rect(layer, &d, &a);
        c = e;
      }
      py = end;
    }
    x += (gw + 2) * sc + sc + 1;
  }
}

static void onDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  const Text* t = (const Text*)lv_obj_get_user_data(o);
  if (!t) return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const lv_color_t col = lv_obj_get_style_text_color(o, LV_PART_MAIN);
  draw(lv_event_get_layer(e), a.x1, a.y1, t->hm, SC, col);
  if (t->sec[0])
    draw(lv_event_get_layer(e), a.x1 + width(t->hm, SC) + 3 * SC, a.y2 + 1 - lettering::LOGO_H * SC_SEC, t->sec, SC_SEC, col);
}
static void onDelete(lv_event_t* e) { lv_free(lv_obj_get_user_data((lv_obj_t*)lv_event_get_target(e))); }

static lv_obj_t* create(lv_obj_t* parent) {
  lv_obj_t* o = lv_obj_create(parent);
  lv_obj_remove_style_all(o);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_CLICKABLE);
  // Graphite on the light theme: that much black, that big, was heavy.
  lv_obj_set_style_text_color(o, lv_color_hex(theme::s_light ? theme::mix(theme::TEXT, theme::BG, 80) : theme::TEXT), 0);
  Text* t = (Text*)lv_malloc(sizeof(Text));
  if (t) { snprintf(t->hm, sizeof(t->hm), "--:--"); t->sec[0] = 0; }
  lv_obj_set_user_data(o, t);
  lv_obj_set_size(o, width("--:--", SC), lettering::LOGO_H * SC);
  lv_obj_add_event_cb(o, onDraw, LV_EVENT_DRAW_MAIN_END, NULL);
  lv_obj_add_event_cb(o, onDelete, LV_EVENT_DELETE, NULL);
  return o;
}
static void set(lv_obj_t* o, const char* hm, const char* sec) {
  Text* t = (Text*)lv_obj_get_user_data(o);
  if (!t || (!strcmp(t->hm, hm) && !strcmp(t->sec, sec))) return;
  snprintf(t->hm, sizeof(t->hm), "%s", hm);
  snprintf(t->sec, sizeof(t->sec), "%s", sec);
  const int32_t w = width(hm, SC) + (sec[0] ? 3 * SC + width(sec, SC_SEC) : 0);
  if (w != lv_obj_get_width(o)) lv_obj_set_width(o, w);
  lv_obj_invalidate(o);
}
}  // namespace bigclock

// A big clock (Home, the lock screen): the digits and, on a 12-hour clock,
// AM / PM beside them in `small`, on the digits' baseline. `big` null: the
// digits in the lettering (bigclock above). clockFaceSet() once a second;
// "--:--" until the time is known.
static lv_obj_t* clockFace(lv_obj_t* parent, const lv_font_t* big, const lv_font_t* small) {
  lv_obj_t* f = flexBox(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(f, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(f, big ? 4 : 3 * bigclock::SC, 0);
  if (big) label(f, "--:--", big, theme::TEXT);
  else { bigclock::create(f); lv_obj_set_style_pad_bottom(f, 6, 0); }   // clear of the line under it
  lv_obj_t* ap = label(f, "", small, theme::TEXT_MUTED);
  if (big) lv_obj_set_style_pad_bottom(ap, big->base_line - small->base_line, 0);
  else lv_obj_set_style_translate_y(ap, small->base_line, 0);   // its baseline on the digits' foot
  lv_obj_add_flag(ap, LV_OBJ_FLAG_HIDDEN);
  return f;
}
static void clockFaceSet(lv_obj_t* f, const struct tm* ti, const NodePrefs* p) {
  lv_obj_t* digits = lv_obj_get_child(f, 0);
  lv_obj_t* ap = lv_obj_get_child(f, 1);
  if (lv_obj_check_type(digits, &lv_label_class)) {
    char clk[12] = "--:--";
    if (ti) fmtClock(clk, sizeof(clk), *ti, p, false, true);
    setText(digits, clk);
  } else {
    char hm[8] = "--:--", sec[4] = "";
    if (ti) {
      if (p && p->clock_12h) snprintf(hm, sizeof(hm), "%d:%02d", ti->tm_hour % 12 ? ti->tm_hour % 12 : 12, ti->tm_min);
      else snprintf(hm, sizeof(hm), "%02d:%02d", ti->tm_hour, ti->tm_min);
      if (!p || !p->clock_hide_seconds) snprintf(sec, sizeof(sec), "%02d", ti->tm_sec);
    }
    bigclock::set(digits, hm, sec);
  }
  bool h12 = ti && p && p->clock_12h;
  if (h12) setText(ap, ti->tm_hour < 12 ? "AM" : "PM");
  if (h12 != !lv_obj_has_flag(ap, LV_OBJ_FLAG_HIDDEN)) {
    if (h12) lv_obj_remove_flag(ap, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(ap, LV_OBJ_FLAG_HIDDEN);
  }
}
// A message's age ("12s" / "5m" / "3h" / "2d"). After a restart the clock runs
// from the build date until GPS or the app sets it, so messages restored from
// the card look newer than "now": those show when they came ("25 Sep 14:05").
static const NodePrefs* s_prefs = nullptr;   // for the free helpers below; set in begin()
static bool clockBehind(uint32_t now, uint32_t ts) { return ts > 1000000000UL && ts > now + 120; }
static void fmtMsgAge(char* b, size_t n, uint32_t now, uint32_t ts, const NodePrefs* p) {
  if (!clockBehind(now, ts)) { geo::fmtAgeShort(b, (int)n, now, ts ? ts : now); return; }
  struct tm ti;
  localTime(p, ti, ts);   // set: clockBehind() says ts is
  char clk[12];
  fmtClock(clk, sizeof(clk), ti, p, true);
  snprintf(b, n, "%d %s %s", ti.tm_mday, MONTHS[ti.tm_mon], clk);
}

static void contactName(const uint8_t* prefix, char* out, size_t n) {
  ContactInfo c;
  if (MessageHistory::contactByPrefix(prefix, c) && c.name[0]) snprintf(out, n, "%s", c.name);
  else snprintf(out, n, "%02X%02X%02X%02X", prefix[0], prefix[1], prefix[2], prefix[3]);
}
