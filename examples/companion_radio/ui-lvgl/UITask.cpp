#include "UITask.h"
#include <helpers/TxtDataHelpers.h>
#include "../MyMesh.h"
#include "target.h"
#if defined(ESP32)
  #include <esp_heap_caps.h>
  // The UI runs on Arduino's loop task: LVGL's renderer, FAT on the card and
  // the map's decoders all nest on its stack, and the stock 8 KB left a few
  // hundred bytes at the deepest point (Diagnostics > Stack free, the lowest
  // ever) -- a screenshot's file write went past it and reset the device.
  SET_LOOP_TASK_STACK_SIZE(16 * 1024);
#endif
#include <new>
#include <src/core/lv_obj_event_private.h>   // lv_cover_check_info_t (coversOwnArea)
#include <src/widgets/label/lv_label_private.h>   // lv_label_t (labelShows)
#include <src/misc/lv_area_private.h>   // lv_area_intersect (clockview::wheelDraw)
#include <stdarg.h>
#include <sys/stat.h>

// Flags: LVGL draws text one codepoint at a time, so a flag (a pair of regional
// indicator letters) would show as two letter tiles. Every label and span of
// this UI (all of it is this one file and the headers it includes) goes through
// these, which swap each pair for the codepoint fonts/ui_emoji.c keeps the
// flag's image under. Display only: what's stored and sent stays as it was.
// A label given the text it already shows is left alone: the status bar, the
// clocks and the live values are set every refresh, and each set is a redraw.
extern "C" char* ui_emoji_flags(const char* s);
static void ui_label_set_text(lv_obj_t* o, const char* t) {
  char* f = t ? ui_emoji_flags(t) : nullptr;   // t == NULL: LVGL's "redraw the current text"
  const char* s = f ? f : t;
  const char* cur = lv_label_get_text(o);
  if (!s || !cur || strcmp(cur, s) != 0) lv_label_set_text(o, s);
  lv_free(f);
}
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) __attribute__((format(printf, 2, 3)));
static void ui_label_set_text_fmt(lv_obj_t* o, const char* fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  char buf[160];
  int n = vsnprintf(buf, sizeof(buf), fmt, ap);
  va_end(ap);
  if (n < (int)sizeof(buf)) { ui_label_set_text(o, buf); return; }
  char* big = (char*)lv_malloc(n + 1);
  if (!big) return;
  va_start(ap, fmt);
  vsnprintf(big, n + 1, fmt, ap);
  va_end(ap);
  ui_label_set_text(o, big);
  lv_free(big);
}
static void ui_span_set_text(lv_span_t* s, const char* t) {
  char* f = ui_emoji_flags(t);
  lv_span_set_text(s, f ? f : t);
  lv_free(f);
}
#define lv_label_set_text ui_label_set_text
#define lv_label_set_text_fmt ui_label_set_text_fmt
#define lv_span_set_text ui_span_set_text

// A zeroed buffer of `n` T in PSRAM when the board has it: internal RAM is what
// BLE, WiFi and TLS need. Allocated once (at startup or first use), never freed.
template <class T> static T* psramBuf(size_t n) {
#if defined(ESP32)
  if (void* p = heap_caps_calloc(n, sizeof(T), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)) return (T*)p;
#endif
  return (T*)calloc(n, sizeof(T));
}

static bool s_chats_stale = false;   // Messages: a refresh waits for a held row to be let go

#include "../ui-core/UiCore.h"   // shared UI Core (header-only, this TU)
#include "../ui-core/NearbyModel.h"
#include "../ui-core/EtaTracker.h"
#include "../ui-core/SettingsSchema.h"
#include "../ui-core/GpsAverager.h"
#include "../ui-core/TrackBack.h"
#include "../ui-core/TrailProfile.h"
#include "../ui-core/StatusHistory.h"
#include "../ui-core/CalendarMath.h"
#include "../ui-core/RadioControl.h"
#include "../ui-core/RepeaterControl.h"
#include "../ui-core/Diagnostics.h"
#include "../ui-core/Battery.h"
#include "../ui-core/Telemetry.h"
#include "../ui-core/ChannelControl.h"
#include "../ui-core/BotConfig.h"
#include "../ui-core/SoundControl.h"
#include "../ui-core/SoundNotifier.h"
#include "../ui-core/MessageText.h"
#include "Theme.h"
#include "Anim.h"
#include "LvglPort.h"
#include "lv_mem_psram.h"
#include "HistoryStore.h"
#include "map/LiveCache.h"
static histstore::SdArchive s_archive;   // message history on the SD card
namespace storeview { static void stopWalk(); }   // StorageScreen.h
#include "../ui-core/KeyboardData.h"
#include "Keyboard.h"

static const char* waypointsFull() {
  static char t[24];
  snprintf(t, sizeof(t), "Waypoints full (%d)", WaypointStore::CAPACITY);
  return t;
}

// ui-lvgl skeleton (docs/solo/developer/ui-core.md, step 5): status bar, home,
// conversation list, contact picker, conversation view with compose. Every
// piece of state it shows comes from the UI Core; this file only draws it.

static UITask* s_ui = nullptr;   // for LVGL's C callbacks

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
#include <emscripten.h>
// The board's one button, pressed from the simulator page (web/lvgl.html).
static bool s_sim_btn_click = false, s_sim_btn_hold = false, s_sim_wake = false;
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button() { s_sim_btn_click = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_lcd_button_hold() { s_sim_btn_hold = true; }
extern "C" EMSCRIPTEN_KEEPALIVE void sim_wake_button() { s_sim_wake = true; }
static bool s_sim_shot = false;   // the side + top button combo
extern "C" EMSCRIPTEN_KEEPALIVE void sim_screenshot() { s_sim_shot = true; }
// The speaker, polled every frame by the page's Web Audio oscillator.
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_is_playing() { return s_ui && s_ui->isBuzzerPlaying() ? 1 : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_freq_hz() { return s_ui ? (int)s_ui->buzzerFreqHz() : 0; }
extern "C" EMSCRIPTEN_KEEPALIVE int sim_buzzer_get_volume() { return s_ui ? (int)s_ui->buzzerVolume() : 0; }
#endif

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
    if (top) {   // the status bar, on the top layer too, stays dimmed
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
  lv_obj_set_style_border_color(panel, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(panel, 1, 0);
  lv_obj_set_style_radius(panel, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(panel, theme::PAD, 0);
  lv_obj_set_style_pad_row(panel, 4, 0);
  lv_obj_set_flex_flow(panel, LV_FLEX_FLOW_COLUMN);
  if (fit == POP_BOTTOM) {   // over a live screen, nothing dimmed
    lv_obj_set_style_bg_opa(overlay, LV_OPA_TRANSP, 0);
    anim::rise(panel);
  } else {
    lv_obj_add_event_cb(overlay, freeze::ownerDeleted, LV_EVENT_DELETE, NULL);
    if (freeze::early(overlay, LV_OPA_60)) anim::rise(panel);
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

// ... and every label given a width in pixels, which labelFit() then measures
// at it (the screen titles, a list row's cut-off name ...).
static void rowsFit(lv_obj_t* root) {
  lv_obj_tree_walk(root, [](lv_obj_t* o, void*) {
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_USER_2)) {
      lv_obj_remove_flag(o, LV_OBJ_FLAG_USER_2);
      rowFit(o);
    } else if (lv_obj_check_type(o, &lv_label_class)) {
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
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, theme::PAD, hint ? 5 : 13);
  lv_obj_set_user_data(row, t);
  if (hint) {
    lv_obj_t* h = label(row, hint, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(h, LV_LABEL_LONG_DOT);
    lv_obj_set_size(h, hint_w, 15);   // fixed height: cut, don't wrap onto the label
    lv_obj_align(h, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
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
    lv_obj_set_style_radius(c, theme::RADIUS_SM, 0);
    lv_obj_set_style_pad_hor(c, 10, 0);
    lv_obj_set_style_pad_ver(c, 8, 0);
  }
  Choice* ch = (Choice*)lv_malloc(sizeof(Choice));
  memset(ch, 0, sizeof(*ch));
  snprintf(ch->title, sizeof(ch->title), "%s", title ? title : "");
  ch->value = label(c, "", THEME_FONT_BODY, theme::ACCENT);
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
  label(c, text, THEME_FONT_BODY, theme::ACCENT);
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
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::ACCENT_DIM), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::TEXT), LV_PART_ITEMS);
  lv_obj_set_style_text_font(m, THEME_FONT_SMALL, LV_PART_ITEMS);
  lv_obj_set_style_shadow_width(m, 0, LV_PART_ITEMS);
  lv_obj_set_style_radius(m, theme::RADIUS_SM, LV_PART_ITEMS);
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

// The primary action of a screen or popup: full amber, dark text (Theme.h);
// call after its label is made.
static void stylePrimary(lv_obj_t* b) {
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT), 0);
  for (uint32_t i = 0; i < lv_obj_get_child_count(b); i++)
    lv_obj_set_style_text_color(lv_obj_get_child(b, i), lv_color_hex(theme::BG), 0);
}

// A row of equal buttons at the foot of a popup or screen: buttonBar(), then
// barButton() for each -- `accent` for the main action or one that's on
// (barButtonOn() flips it later). Returns the button; its label is child 0.
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
  lv_obj_set_style_bg_color(b, lv_color_hex(on ? theme::ACCENT_DIM : theme::SURFACE), 0);
}
static lv_obj_t* barButton(lv_obj_t* bar, const char* text, lv_event_cb_t cb, uintptr_t user, bool accent = false) {
  lv_obj_t* b = lv_button_create(bar);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_pad_hor(b, 4, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  barButtonOn(b, accent);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)user);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT));
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
  lv_obj_t* l = label(b, "", THEME_FONT_SMALL, theme::BG);
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

// Local time (NodePrefs::tz_offset_hours), now or at `utc`; false before the clock is set.
static bool localTime(const NodePrefs* p, struct tm& out, uint32_t utc = rtc_clock.getCurrentTime()) {
  return localTm(utc, p ? p->tz_offset_hours : 0, out);
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

// A big clock (Home, the lock screen): the digits and, on a 12-hour clock,
// AM / PM beside them in `small`, on the digits' baseline. clockFaceSet()
// once a second; "--:--" until the time is known.
static lv_obj_t* clockFace(lv_obj_t* parent, const lv_font_t* big, const lv_font_t* small) {
  lv_obj_t* f = flexBox(parent, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(f, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_END);
  lv_obj_set_style_pad_column(f, 4, 0);
  label(f, "--:--", big, theme::TEXT);
  lv_obj_t* ap = label(f, "", small, theme::TEXT_MUTED);
  lv_obj_set_style_pad_bottom(ap, big->base_line - small->base_line, 0);
  lv_obj_add_flag(ap, LV_OBJ_FLAG_HIDDEN);
  return f;
}
static void clockFaceSet(lv_obj_t* f, const struct tm* ti, const NodePrefs* p) {
  lv_obj_t* ap = lv_obj_get_child(f, 1);
  char clk[12] = "--:--";
  if (ti) fmtClock(clk, sizeof(clk), *ti, p, false, true);
  setText(lv_obj_get_child(f, 0), clk);
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

// ── Lifecycle ─────────────────────────────────────────────────────────────────

void UITask::begin(DisplayDriver* display_drv, SensorManager* sensors, NodePrefs* node_prefs) {
  s_ui = this;
  _ui_started_ms = millis();
  _display = display_drv;
  _sensors = sensors;
  _prefs = node_prefs;
  s_prefs = node_prefs;
  if (node_prefs) battery::useCurve(node_prefs->batt_curve_mv);   // Settings > Battery curve

  _core = new UiCore();
  _core->begin(node_prefs, sensors, this);
  {  // history kept on the SD card: back into the ring, then every new entry to it
    int hk = lvport::loadHistKeep();
    s_archive.setKeep(histstore::KEEP[hk >= 0 && hk < histstore::KEEP_COUNT ? hk : histstore::KEEP_DEFAULT]);
    int lc = lvport::loadLiveCap();
    mapview::s_live_cache.setLimit((uint64_t)mapview::LIVE_CAP_MB[lc >= 0 && lc < mapview::LIVE_CAP_COUNT ? lc : mapview::LIVE_CAP_DEFAULT] << 20);
    s_archive.restore(_core->history);
    _core->history.setArchive(&s_archive);
  }
  trailJournalRestore();   // a trail cut off by a restart or a flat battery
  _tap_wake = lvport::loadTapWake();
  msgtext::seedQuick(node_prefs);   // "OK" in quick message 1 on first boot
  _nearby = new NearbyModel();
  _nearby->bindModel(_core, node_prefs);
  _scan = new NearbyModel();
  _scan->bindModel(_core, node_prefs);
  _scan->setSource(NearbyModel::SRC_SCAN);

#ifdef PIN_USER_BTN
  user_btn.begin();
#endif
#ifdef PIN_BUZZER
  soundctl::applyMode(_prefs, _buzzer, isClientConnected(), rtc_clock.getCurrentTime());
  _buzzer.setVolume(_prefs ? _prefs->buzzer_volume : 4);
  _buzzer.begin();   // plays the startup sound unless muted
#endif

  lv_init();
  lv_tick_set_cb([]() -> uint32_t { return (uint32_t)millis(); });
  if (!lvport::begin()) {
    Serial.println("ui-lvgl: no memory for display buffers");
    return;
  }
  theme::setAccent(lvport::loadAccent());
  theme::install(lv_display_get_default());
  lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t*) { rowsFitPending(); }, LV_EVENT_REFR_START, NULL);

  buildStatusBar();
  applyDisplayPrefs();   // a slider percentage overrides the level main.cpp set
  { char c[24]; lvport::crashSummary(c, sizeof(c)); }   // the dump in flash read now (~45 ms), not on the first Diagnostics
  showHome();
  if (pinSet()) lockScreen();   // a reboot doesn't get round the PIN
  showSplash();                // over both; fades out by itself
}

MyMesh::Listener* UITask::meshListener() { return _core; }

// The USER button's hold: sound on / off (leaves Auto, as the Sound page's
// On / Off would).
void UITask::toggleMute() {
#ifdef PIN_BUZZER
  if (!_prefs) return;
  bool on = _buzzer.isQuiet();   // muted now, however (mode, Auto, quiet hours): sound on
  soundctl::setMode(_prefs, _buzzer, on ? soundctl::MODE_ON : soundctl::MODE_OFF, isClientConnected());
  prefsSave();
  if (on) _buzzer.playForced(soundctl::MEL_VOLUME);
  if (!_asleep) { showToast(on ? "Sound on" : "Sound off", 1200); refreshStatusBar(); }
#endif
}

static constexpr uint32_t LOCK_OFF_MS = 30000;   // the lock screen's own auto-off cap

#if defined(UI_PERF_TEST)
// Perf tools (-D UI_PERF_TEST, output lines start "PERF"): each settings page
// and most screens built and drawn once, draw profiles, a popup test, then a
// walk through the screens. -D PERF_LISTS times the long lists (opened until
// all is in, scrolled, refreshed); -D PERF_LIST_MUL fills New message's
// with the few contacts there are, repeated up to 256.
// -D PERF_KEEP_USB leaves the USB popup up;
// -D PERF_WRAP_TEXT plus -Wl,--wrap= of lv_text_get_size_attributes,
// lv_layout_apply, lv_obj_get_style_prop_internal and lv_obj_send_event adds
// what layout costs per screen (text measured, flex runs, style reads, events).
//
// A full redraw of the active screen (and the top layer), timed per
// object -- its own drawing, not its children's -- the costliest listed with
// what they are, and totals by kind.
static uint32_t s_dp_own[160], s_dp_t0;
static lv_obj_t* s_dp_obj[160];
static int s_dp_n;
static void dpCb(lv_event_t* e) {
  lv_event_code_t c = lv_event_get_code(e);
  int k = (int)(intptr_t)lv_event_get_user_data(e);
  if (c == LV_EVENT_DRAW_MAIN_BEGIN || c == LV_EVENT_DRAW_POST_BEGIN) s_dp_t0 = micros();
  else s_dp_own[k] += micros() - s_dp_t0;
}
static const char* dpKind(lv_obj_t* o) {
  const lv_obj_class_t* k = lv_obj_get_class(o);
  return k == &lv_label_class ? "label" : k == &lv_button_class ? "button" : k == &lv_switch_class ? "switch"
       : k == &lv_image_class ? "image" : k == &lv_slider_class ? "slider" : k == &lv_obj_class ? "obj" : "other";
}
static void perfDrawProfile(const char* tag) {
  {   // the screen's entrance first: its frames timed, then done
    uint32_t n = 0, us = 0, t0 = millis();
    while (lv_anim_count_running() && millis() - t0 < 1000) {
      uint32_t u = micros();
      lv_timer_handler();
      u = micros() - u;
      if (u > 5000) { n++; us += u; }   // a pass that drew
      delay(1);
    }
    if (n) Serial.printf("PERF draw %-8s entrance %lu frames, avg %5.1f ms\n", tag, (unsigned long)n, us / 1000.0f / n);
  }
  s_dp_n = 0;
  memset(s_dp_own, 0, sizeof(s_dp_own));
  auto hook = [](lv_obj_t* o, void*) {
    if (s_dp_n >= 160) return LV_OBJ_TREE_WALK_END;
    s_dp_obj[s_dp_n] = o;
    for (lv_event_code_t c : { LV_EVENT_DRAW_MAIN_BEGIN, LV_EVENT_DRAW_MAIN_END, LV_EVENT_DRAW_POST_BEGIN, LV_EVENT_DRAW_POST_END })
      lv_obj_add_event_cb(o, dpCb, c, (void*)(intptr_t)s_dp_n);
    s_dp_n++;
    return LV_OBJ_TREE_WALK_NEXT;
  };
  lv_obj_tree_walk(lv_screen_active(), hook, nullptr);
  lv_obj_tree_walk(lv_layer_top(), hook, nullptr);
  const int N = 5;
  uint32_t fl0 = lvport::s_flush_us, t = micros();
  for (int i = 0; i < N; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
  t = micros() - t;
  uint32_t fl = lvport::s_flush_us - fl0, sum = 0;
  for (int i = 0; i < s_dp_n; i++) sum += s_dp_own[i];
  Serial.printf("PERF draw %-8s full %5.1f ms = flush %4.1f + objects %4.1f + the rest %4.1f  (%d objects)\n", tag,
                t / 1000.0f / N, fl / 1000.0f / N, sum / 1000.0f / N, (t - fl - sum) / 1000.0f / N, s_dp_n);
  struct Tot { const char* k; uint32_t us; int n; } tot[8] = {};
  for (int i = 0; i < s_dp_n; i++) {
    const char* k = dpKind(s_dp_obj[i]);
    for (Tot& x : tot) { if (!x.k) x.k = k; if (x.k == k) { x.us += s_dp_own[i]; x.n++; break; } }
  }
  for (Tot& x : tot) if (x.k) Serial.printf("PERF   by kind %-7s %3d objects %5.1f ms\n", x.k, x.n, x.us / 1000.0f / N);
  for (int r = 0; r < 12; r++) {   // the costliest, one by one
    int best = -1;
    for (int i = 0; i < s_dp_n; i++) if (s_dp_own[i] && (best < 0 || s_dp_own[i] > s_dp_own[best])) best = i;
    if (best < 0 || s_dp_own[best] < 300 * N) break;
    lv_obj_t* o = s_dp_obj[best];
    Serial.printf("PERF   %5.2f ms %-6s %3ldx%-3ld radius %2ld bg_opa %3d border %ld shadow %ld clip_corner %d %.30s\n",
                  s_dp_own[best] / 1000.0f / N, dpKind(o), (long)lv_obj_get_width(o), (long)lv_obj_get_height(o),
                  (long)lv_obj_get_style_radius(o, LV_PART_MAIN), (int)lv_obj_get_style_bg_opa(o, LV_PART_MAIN),
                  (long)lv_obj_get_style_border_width(o, LV_PART_MAIN), (long)lv_obj_get_style_shadow_width(o, LV_PART_MAIN),
                  (int)lv_obj_get_style_clip_corner(o, LV_PART_MAIN),
                  lv_obj_check_type(o, &lv_label_class) ? lv_label_get_text(o) : "");
    s_dp_own[best] = 0;
  }
  for (int i = 0; i < s_dp_n; i++) lv_obj_remove_event_cb(s_dp_obj[i], dpCb);
}

// Every switch, slider, segmented row and choice of the active screen used
// as a tap would (and put back): how long its handler took, then the frames
// until its animation settled -- count, average, longest.
struct PerfFrames { uint32_t n = 0, sum = 0, max = 0; };
static void perfSettle(PerfFrames& f, uint32_t min_ms, uint32_t max_ms = 800) {
  uint32_t t0 = millis();
  while (millis() - t0 < max_ms && (millis() - t0 < min_ms || lv_anim_count_running())) {
    uint32_t u = micros();
    lv_timer_handler();
    u = micros() - u;
    if (u > 3000) { f.n++; f.sum += u; if (u > f.max) f.max = u; }   // a pass that drew
    delay(1);
  }
}
static const char* perfCtlName(lv_obj_t* ctl) {   // the row's first text
  struct W { lv_obj_t* skip; const char* t; } w = { ctl, "" };
  lv_obj_t* row = lv_obj_get_parent(ctl);
  if (lv_obj_check_type(ctl, &lv_buttonmatrix_class) && lv_obj_get_child_count(row) < 2) return "(segmented)";
  lv_obj_tree_walk(row, [](lv_obj_t* o, void* u) {
    W* w = (W*)u;
    if (o == w->skip) return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    if (lv_obj_check_type(o, &lv_label_class) && *lv_label_get_text(o)) { w->t = lv_label_get_text(o); return LV_OBJ_TREE_WALK_END; }
    return LV_OBJ_TREE_WALK_NEXT;
  }, &w);
  return w.t;
}
static bool perfIsChoice(lv_obj_t* o) {
  return lv_obj_get_child_count(o) == 2 && lv_obj_get_user_data(o) && lv_obj_check_type(lv_obj_get_child(o, 1), &lv_label_class)
      && strcmp(lv_label_get_text(lv_obj_get_child(o, 1)), LV_SYMBOL_DOWN) == 0;
}
static int perfCollect(lv_obj_t** out, int max) {
  struct C { lv_obj_t** out; int n, max; } c = { out, 0, max };
  lv_obj_tree_walk(lv_screen_active(), [](lv_obj_t* o, void* u) {
    C* c = (C*)u;
    if (c->n >= c->max) return LV_OBJ_TREE_WALK_END;
    if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    if (lv_obj_check_type(o, &lv_switch_class) || lv_obj_check_type(o, &lv_slider_class)
        || lv_obj_check_type(o, &lv_buttonmatrix_class) || perfIsChoice(o)) {
      c->out[c->n++] = o;
      return LV_OBJ_TREE_WALK_SKIP_CHILDREN;
    }
    return LV_OBJ_TREE_WALK_NEXT;
  }, &c);
  return c.n;
}
static void perfControls(const char* tag, void (*fill_flush)()) {
  static lv_obj_t* ctl[48];
  lv_obj_t* scr = lv_screen_active();
  int n = perfCollect(ctl, 48);
  for (int k = 0; k < n; k++) {
    lv_obj_t* o = ctl[k];
    if (!lv_obj_is_valid(o)) continue;
    char name[28];
    snprintf(name, sizeof(name), "%s", perfCtlName(o));
    lv_obj_scroll_to_view_recursive(o, LV_ANIM_OFF);
    PerfFrames idle; perfSettle(idle, 60);
    PerfFrames f, back;
    uint32_t cb = 0;
    const char* kind = "";
    bool rb = false;   // the screen was rebuilt: put back on the new one's control
    auto rebuilt = [&]() { return lv_screen_active() != scr || !lv_obj_is_valid(o); };
    auto refind = [&]() {   // the controls found again, in the same order
      scr = lv_screen_active();
      fill_flush();
      layoutNow(scr);
      n = perfCollect(ctl, 48);
      o = k < n ? ctl[k] : nullptr;
    };
    if (lv_obj_check_type(o, &lv_switch_class)) {
      kind = "switch";
      auto tap = [&](PerfFrames& fr) {
        lv_obj_send_event(o, LV_EVENT_PRESSED, NULL);
        perfSettle(fr, 80);
        uint32_t t = micros();
        lv_obj_send_event(o, LV_EVENT_RELEASED, NULL);   // toggles, VALUE_CHANGED
        t = micros() - t;
        perfSettle(fr, 250);   // past a rebuildSoon()
        return t;
      };
      cb = tap(f);
      if (rebuilt()) { refind(); rb = true; }
      if (o && lv_obj_check_type(o, &lv_switch_class)) tap(back);
    } else if (lv_obj_check_type(o, &lv_slider_class)) {
      kind = "slider";
      int32_t v0 = lv_slider_get_value(o), lo = lv_slider_get_min_value(o), hi = lv_slider_get_max_value(o);
      lv_obj_send_event(o, LV_EVENT_PRESSED, NULL);
      for (int s = 0; s <= 12; s++) {   // dragged end to end, a step a frame
        lv_slider_set_value(o, lo + (hi - lo) * s / 12, LV_ANIM_OFF);
        uint32_t t = micros();
        lv_obj_send_event(o, LV_EVENT_VALUE_CHANGED, NULL);
        t = micros() - t;
        if (t > cb) cb = t;
        perfSettle(f, 16, 16);
      }
      uint32_t t = micros();
      lv_obj_send_event(o, LV_EVENT_RELEASED, NULL);
      t = micros() - t;
      if (t > cb) cb = t;
      perfSettle(f, 60);
      lv_slider_set_value(o, v0, LV_ANIM_OFF);
      lv_obj_send_event(o, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_send_event(o, LV_EVENT_RELEASED, NULL);
      perfSettle(back, 60);
    } else if (lv_obj_check_type(o, &lv_buttonmatrix_class)) {
      kind = "segment";
      uint32_t cur = UINT32_MAX, other = UINT32_MAX, cnt = 0;
      for (const char* const* m = lv_buttonmatrix_get_map(o); **m; m++) if (strcmp(*m, "\n")) cnt++;
      for (uint32_t i = 0; i < cnt; i++) {
        if (lv_buttonmatrix_has_button_ctrl(o, i, LV_BUTTONMATRIX_CTRL_CHECKED)) cur = i;
        else if (other == UINT32_MAX) other = i;
      }
      auto pick = [&](uint32_t i, PerfFrames& fr) {
        lv_buttonmatrix_set_selected_button(o, i);
        lv_buttonmatrix_set_button_ctrl(o, i, LV_BUTTONMATRIX_CTRL_CHECKED);
        uint32_t t = micros();
        lv_obj_send_event(o, LV_EVENT_VALUE_CHANGED, &i);
        t = micros() - t;
        perfSettle(fr, 250);
        return t;
      };
      if (other == UINT32_MAX) continue;
      cb = pick(other, f);
      if (rebuilt()) { refind(); rb = true; }
      if (cur != UINT32_MAX && o && lv_obj_check_type(o, &lv_buttonmatrix_class)) pick(cur, back);
    } else {
      kind = "choice";   // the options popup: opened, then closed unchanged
      uint32_t t = micros();
      lv_obj_send_event(o, LV_EVENT_CLICKED, NULL);
      cb = micros() - t;
      perfSettle(f, 120);
      pickerClose();
      perfSettle(back, 120);
    }
    Serial.printf("PERF ctl %-7s %-7s %-24s handler %5.1f ms | frames %2lu avg %4.1f max %5.1f | back %2lu avg %4.1f max %5.1f%s\n",
                  tag, kind, name, cb / 1000.0f, (unsigned long)f.n, f.n ? f.sum / 1000.0f / f.n : 0.0f, f.max / 1000.0f,
                  (unsigned long)back.n, back.n ? back.sum / 1000.0f / back.n : 0.0f, back.max / 1000.0f,
                  rb ? "  (screen rebuilt)" : "");
    if (o && rebuilt()) refind();
  }
}
#endif

#ifdef PERF_WRAP_TEXT
#include <src/misc/lv_text_private.h>
// (-Wl,--wrap=lv_text_get_size_attributes) How often text is measured,
// and how often with the same text, font and width as a recent call.
static uint32_t s_tm_n, s_tm_us, s_tm_chars, s_tm_same;
extern "C" void __real_lv_text_get_size_attributes(lv_point_t*, const char*, const lv_font_t*, lv_text_attributes_t*);
extern "C" void __wrap_lv_text_get_size_attributes(lv_point_t* s, const char* t, const lv_font_t* f, lv_text_attributes_t* a) {
  struct K { const char* t; const lv_font_t* f; int32_t w; };
  static K ring[64]; static int ri = 0;
  for (const K& k : ring) if (k.t == t && k.f == f && k.w == a->max_width) { s_tm_same++; break; }
  ring[ri++ & 63] = { t, f, a->max_width };
  uint32_t u = micros();
  __real_lv_text_get_size_attributes(s, t, f, a);
  s_tm_us += micros() - u; s_tm_n++; s_tm_chars += strlen(t);
}
// ... and flex runs (-Wl,--wrap=lv_layout_apply), style reads and events.
static uint32_t s_la_n, s_la_us, s_sp_n, s_ev_n;
extern "C" void __real_lv_layout_apply(lv_obj_t*);
extern "C" void __wrap_lv_layout_apply(lv_obj_t* o) {
  static int depth = 0;
  uint32_t u = micros(); depth++;
  __real_lv_layout_apply(o);
  if (--depth == 0) s_la_us += micros() - u;
  s_la_n++;
}
extern "C" lv_style_value_t __real_lv_obj_get_style_prop_internal(const lv_obj_t*, lv_part_t, lv_style_prop_t);
extern "C" lv_style_value_t __wrap_lv_obj_get_style_prop_internal(const lv_obj_t* o, lv_part_t p, lv_style_prop_t s) {
  s_sp_n++;
  return __real_lv_obj_get_style_prop_internal(o, p, s);
}
extern "C" lv_result_t __real_lv_obj_send_event(lv_obj_t*, lv_event_code_t, void*);
extern "C" lv_result_t __wrap_lv_obj_send_event(lv_obj_t* o, lv_event_code_t c, void* p) {
  s_ev_n++;
  return __real_lv_obj_send_event(o, c, p);
}
#define TM_RESET() (s_tm_n = s_tm_us = s_tm_chars = s_tm_same = s_la_n = s_la_us = s_sp_n = s_ev_n = 0)
#define TM_PRINT(tag) Serial.printf("PERF   text %-8s %4lu measured (%4lu same as recent), %6lu chars, %5.1f ms;" \
    " flex runs %4lu (%5.1f ms), style reads %6lu, events %5lu\n", tag, \
    (unsigned long)s_tm_n, (unsigned long)s_tm_same, (unsigned long)s_tm_chars, s_tm_us / 1000.0f, \
    (unsigned long)s_la_n, s_la_us / 1000.0f, (unsigned long)s_sp_n, (unsigned long)s_ev_n)
#else
#define TM_RESET()
#define TM_PRINT(tag)
#endif

void UITask::loop() {
  pollConnection();
  drainCoreEvents();
  sampleHistory();

  // USER (BOOT, side) button: Home's clock page; held and let go, mutes / unmutes, also with the
  // screen off, which it doesn't wake. WAKE (top) button: screen off / on.
  // Either one silences a ringing alarm first. Held in the first 8 s (once the
  // screen is up -- held at power-on it's the ESP32's download mode): CLI rescue.
  bool btn_click = false, btn_hold = false, wake_press = false;
#ifdef PIN_USER_BTN
  int ev = user_btn.check();
  btn_click = ev == BUTTON_EVENT_CLICK;
  btn_hold = ev == BUTTON_EVENT_LONG_PRESS;
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  btn_click = s_sim_btn_click;
  btn_hold = s_sim_btn_hold;
  wake_press = s_sim_wake;
  s_sim_btn_click = s_sim_btn_hold = s_sim_wake = false;
#endif
#if defined(SEEED_WIO_TRACKER_L2)
  if ((int32_t)(millis() - _next_wake_poll_ms) >= 0) {   // an I2C read on the expander
    _next_wake_poll_ms = millis() + 50;
    bool down = board.readWakeButton();
    wake_press = down && !_wake_down;
    _wake_down = down;
  }
#endif
  // Side button held + top button: a screenshot (and neither does its own thing).
  // A long hold only arms the mute, which happens on letting go -- so holding
  // the side button a bit long before the top one doesn't mute as well.
  static bool mute_armed = false;
  bool shot = false, mute = false;
#ifdef PIN_USER_BTN
  bool side_down = user_btn.isPressed();
  if (wake_press && side_down) { user_btn.cancelClick(); shot = true; mute_armed = false; }
  if (mute_armed && !side_down) { mute_armed = false; mute = true; }
#elif defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
  shot = s_sim_shot;
  s_sim_shot = false;
  if (mute_armed) { mute_armed = false; mute = true; }   // the page sends the hold on release
#endif
  if (shot) { if (!_asleep) takeScreenshot(); }
  else if ((btn_click || btn_hold || wake_press) && _core->clock.isRinging()) dismissRing();
  else if (wake_press) { if (_asleep) wake(); else sleep(); }
  else if (btn_hold && millis() - _ui_started_ms < 8000) {   // held in the first 8 s: CLI rescue, as on the L1
    the_mesh.enterCLIRescue();
    showToast("CLI rescue on the USB serial", 4000);
  }
  else if (btn_hold) mute_armed = true;
  else if (mute) toggleMute();
  else if (btn_click) {   // the side button doesn't wake: pockets
    if (quickPanelOpen()) quickPanelClose(true);
    else if (!_asleep && !locked()) goHome();
  }

  if (_asleep) {
    // Tap to wake (off: the top button only). An I2C read on the touch panel.
    if (_tap_wake && (int32_t)(millis() - _next_touch_poll_ms) >= 0) {
      _next_touch_poll_ms = millis() + 50;
      if (lvport::touched()) { lvport::swallowTouch(); wake(); }
    }
  } else {
    // Locked, nobody's using it: the lock screen goes dark after LOCK_OFF_MS
    // even with auto-off at Never (a woken-up pocket, a boot PIN, a message).
    uint32_t aoff = autoOffMillis();
    if (locked() && (aoff == 0 || aoff > LOCK_OFF_MS)) aoff = LOCK_OFF_MS;
    if (aoff > 0 && lv_display_get_inactive_time(NULL) > aoff && !_core->clock.isRinging() && !otaBusy()) sleep();
  }

  _core->loop();
  drainCoreEvents();
  prefsFlush();
  usbPoll();
#if defined(UI_HEAP_REPORT) && defined(ESP32)
  // -D UI_HEAP_REPORT: internal / PSRAM heap once, 20 s after boot.
  static bool heap_done = false;
  if (!heap_done && millis() > 20000) {
    heap_done = true;
    Serial.printf("HEAP internal free %u largest %u min %u | psram free %u | idf %s\n",
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL),
                  (unsigned)heap_caps_get_free_size(MALLOC_CAP_SPIRAM), esp_get_idf_version());
  }
#endif
#if defined(UI_PERF_TEST) && defined(ESP32)
  // -D UI_PERF_TEST: walks through screens by itself and prints how long each
  // display refresh took during the transition that followed (render+flush).
  {
    static uint32_t next = 15000, sum_us = 0, max_us = 0, frames = 0, t0 = 0;
    static uint32_t last_pass_us = 0, max_gap_us = 0;   // the longest the loop was away (a blocked UI)
    uint32_t now_us = micros();
    if (last_pass_us && now_us - last_pass_us > max_gap_us) max_gap_us = now_us - last_pass_us;
    last_pass_us = now_us;
    static int step = 0;
    static const char* name = "";
    static bool hooked = false;
    if (!hooked) {
      hooked = true;
      lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t* e) {
        if (lv_event_get_code(e) == LV_EVENT_REFR_START) t0 = micros();
        else { uint32_t d = micros() - t0; sum_us += d; if (d > max_us) max_us = d; frames++; }
      }, LV_EVENT_REFR_START, NULL);
      lv_display_add_event_cb(lv_display_get_default(), [](lv_event_t* e) {
        uint32_t d = micros() - t0; sum_us += d; if (d > max_us) max_us = d; frames++; (void)e;
      }, LV_EVENT_REFR_READY, NULL);
    }
    if (millis() >= next) {
      if (*name) Serial.printf("PERF %-10s frames %2lu avg %5.1f ms max %5.1f ms\n", name, (unsigned long)frames,
                               frames ? sum_us / 1000.0f / frames : 0.0f, max_us / 1000.0f);
      if (*name) Serial.printf("PERF   flush %5.1f ms per frame, longest loop pass %5.1f ms\n",
                               frames ? lvport::s_flush_us / 1000.0f / frames : 0.0f, max_gap_us / 1000.0f);
      sum_us = max_us = frames = 0;
      lvport::s_flush_us = 0;
      max_gap_us = 0;
      lv_display_trigger_activity(NULL);   // no sleeping mid-test
      next = millis() + 1200;
      if (step == 0) unlockScreen();   // a PIN lock would be drawn over everything
#ifndef PERF_KEEP_USB
      usbTap(false);   // nor the USB drive question ("Keep card")
#endif
      if (step == 0) lv_timer_handler();   // the popup's delete is async: gone before the sweep below
      if (step == 0) Serial.printf("PERF reset reason %d (4 panic, 5 int wdt, 6 task wdt, 7 wdt, 3 sw)\n", (int)esp_reset_reason());
#if defined(ESP32)
      if (step == 0) {
        esp_core_dump_summary_t sum;
        if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&sum) == ESP_OK) {
          Serial.printf("PERF crash task %s pc %08lx cause %lu vaddr %08lx bt:", sum.exc_task, (unsigned long)sum.exc_pc,
                        (unsigned long)sum.ex_info.exc_cause, (unsigned long)sum.ex_info.exc_vaddr);
          for (uint32_t i = 0; i < sum.exc_bt_info.depth; i++) Serial.printf(" %08lx", (unsigned long)sum.exc_bt_info.bt[i]);
          Serial.println();
        }
      }
#endif
      if (step == 0) {   // once: every settings page and most screens, built and drawn once
        auto one = [this](const char* what, int page, void (UITask::*fn)()) {
          TM_RESET();
          uint32_t t = micros();
          if (fn) (this->*fn)(); else showSchemaSettings(page);
          uint32_t b = micros() - t;
          TM_PRINT("build"); TM_RESET();
          uint32_t tl = micros();
          layoutNow(lv_screen_active());   // layout apart from drawing
          tl = micros() - tl;
          TM_PRINT("layout"); TM_RESET();
          uint32_t fl0 = lvport::s_flush_us, td = micros();
          lv_refr_now(NULL);
          td = micros() - td;
          TM_PRINT("draw"); TM_RESET();
          uint32_t fl1 = lvport::s_flush_us - fl0;
          uint32_t total = micros() - t;
          uint32_t tr = micros();   // the same screen drawn again: what a first frame costs over a steady one
          lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL);
          tr = micros() - tr;
          uint32_t n = 0;
          lv_obj_tree_walk(lv_screen_active(), [](lv_obj_t*, void* u) { ++*(uint32_t*)u; return LV_OBJ_TREE_WALK_NEXT; }, &n);
          uint32_t nf = 0;
          lv_obj_tree_walk(lv_screen_active(), [](lv_obj_t* o, void* u) {
            if (lv_obj_get_style_layout(o, LV_PART_MAIN) == LV_LAYOUT_FLEX) ++*(uint32_t*)u;
            return LV_OBJ_TREE_WALK_NEXT; }, &nf);
          Serial.printf("PERF   flex containers %lu\n", (unsigned long)nf);
          if ((!fn && (page == 7 || page == 1)) || fn == &UITask::showContacts || fn == &UITask::showNearby) {
            char tag[12]; snprintf(tag, sizeof(tag), "%s%d", fn ? "list" : "page", page);
            fillFlush();   // the whole page, not only its first section
            perfDrawProfile(tag);
          }
          Serial.printf("PERF screen %-12s %2d  build %5.1f ms, with first frame %5.1f ms, %3lu objects"
                        " | layout %5.1f, first draw %5.1f (flush %4.1f), redraw %5.1f\n", what, page,
                        b / 1000.0f, total / 1000.0f, (unsigned long)n, tl / 1000.0f, td / 1000.0f, fl1 / 1000.0f, tr / 1000.0f);
        };
        for (int pg = 0; pg < PG_ALL; pg++) one("page", pg, nullptr);
        one("radio", 0, &UITask::showRadio);
        one("diag", 0, &UITask::showDiag);
        one("clock", 0, &UITask::showClock);
        one("compass", 0, &UITask::showCompass);
        one("scopes", 0, &UITask::showScopes);
        one("repeater", 0, &UITask::showRepeater);
        one("bot", 0, &UITask::showBot);
        one("quick", 0, &UITask::showQuickMsgs);
        one("admin", 0, &UITask::showAdminPick);
        one("contacts", 0, &UITask::showContacts);
        one("chats", 0, &UITask::showChats);
        one("nearby", 0, &UITask::showNearby);
#ifdef PERF_LISTS
        {   // -D PERF_LISTS: the long lists -- opened until all is built, scrolled, refreshed
          auto list = [this](const char* tag, void (UITask::*fn)(), void (UITask::*again)()) {
            showHome(); lv_refr_now(NULL);
            size_t h0 = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
            uint32_t t = micros();
            (this->*fn)();
            uint32_t b = micros() - t;
            PerfFrames f;
            uint32_t t0 = millis();
            while (millis() - t0 < 3000 && (fillPending() || lv_anim_count_running() || millis() - t0 < 100)) {
              uint32_t u = micros();
              lv_timer_handler();
              fillTick();
              u = micros() - u;
              if (u > 3000) { f.n++; f.sum += u; if (u > f.max) f.max = u; }
              delay(1);
            }
            uint32_t ready = micros() - t;
            uint32_t n = 0;
            lv_obj_tree_walk(lv_screen_active(), [](lv_obj_t*, void* u) { ++*(uint32_t*)u; return LV_OBJ_TREE_WALK_NEXT; }, &n);
            long kb = ((long)h0 - (long)heap_caps_get_free_size(MALLOC_CAP_SPIRAM)) / 1024;
            struct S { lv_obj_t* o; int h; } s = { nullptr, 10 };   // the list: the most to scroll
            lv_obj_tree_walk(lv_screen_active(), [](lv_obj_t* o, void* u) {
              S* s = (S*)u;
              int v = lv_obj_get_scroll_bottom(o) + lv_obj_get_scroll_y(o);
              if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && v > s->h) { s->h = v; s->o = o; }
              return LV_OBJ_TREE_WALK_NEXT;
            }, &s);
            PerfFrames sc;
            if (s.o) {
              for (int i = 0; i < 60 && lv_obj_get_scroll_bottom(s.o) > 0; i++) {   // a steady fling, 18 px a frame
                lv_obj_scroll_by(s.o, 0, -18, LV_ANIM_OFF);
                uint32_t u = micros();
                lv_refr_now(NULL);
                u = micros() - u;
                sc.n++; sc.sum += u; if (u > sc.max) sc.max = u;
              }
              lv_obj_scroll_to_y(s.o, 0, LV_ANIM_OFF);
            }
            uint32_t tr = 0;
            if (again) {   // what a new message / a changed node costs
              lv_refr_now(NULL);
              tr = micros();
              (this->*again)();
              lv_refr_now(NULL);
              tr = micros() - tr;
            }
            Serial.printf("PERF list %-9s open %5.1f ms, all built %6.1f ms (%2lu frames avg %4.1f max %5.1f) | %4lu objects, %4ld KB"
                          " | scroll %2lu frames avg %4.1f max %5.1f (list %d px) | refresh %5.1f ms\n",
                          tag, b / 1000.0f, ready / 1000.0f, (unsigned long)f.n, f.n ? f.sum / 1000.0f / f.n : 0.0f, f.max / 1000.0f,
                          (unsigned long)n, kb, (unsigned long)sc.n, sc.n ? sc.sum / 1000.0f / sc.n : 0.0f, sc.max / 1000.0f,
                          s.h, tr / 1000.0f);
          };
          bool all = _prefs->dm_show_all;
          list("fav", &UITask::showContacts, nullptr);
          _prefs->dm_show_all = true;   // not saved
          list("contacts", &UITask::showContacts, nullptr);
          _prefs->dm_show_all = all;
          list("chats", &UITask::showChats, &UITask::buildChats);
          list("chats-new", &UITask::showChats, &UITask::refreshChats);   // a new message, in place
          list("nearby", &UITask::showNearby, &UITask::perfNearbyAgain);
          list("admin", &UITask::showAdminPick, nullptr);
        }
#endif
#ifdef PERF_CONTROLS
        {   // -D PERF_CONTROLS: every control on the settings screens used once (perfControls)
          auto ctls = [this](const char* tag, int page, void (UITask::*fn)()) {
            if (fn) (this->*fn)(); else if (page >= 0) showSchemaSettings(page);   // -1: shown already
            fillFlush();
            layoutNow(lv_screen_active());
            lv_refr_now(NULL);
            perfControls(tag, []() { s_ui->fillFlush(); });
          };
          auto prefs = [this](const char* when) {
            Serial.printf("PERF prefs %s: imperial %d repeat %d use_profile %d auto_power %d\n", when, _prefs->units_imperial,
                          _prefs->client_repeat, _prefs->repeater_use_profile, _prefs->tx_apc);
          };
          prefs("before");
          char tag[8];
          for (int pg = 0; pg < PG_ALL; pg++) { snprintf(tag, sizeof(tag), "page%d", pg); ctls(tag, pg, nullptr); }
          ctls("radio", 0, &UITask::showRadio);
          ctls("rptr", 0, &UITask::showRepeater);
          ctls("bot", 0, &UITask::showBot);
          ctls("clock", 0, &UITask::showClock);
          ctls("quick", 0, &UITask::showQuickMsgs);
          showChannelEdit(0); ctls("channel", -1, nullptr);
          prefs("after");
        }
#endif
        showHome();
        lv_refr_now(NULL);
        perfDrawProfile("home");
        {   // a popup over Home, the screen under it frozen as it opened
          lv_obj_t* ov = nullptr;
          uint32_t t = micros();
          lv_obj_t* p = popupOpen(lv_layer_top(), POP_FIT, ov);
          float took = (micros() - t) / 1000.0f;
          for (int i = 0; i < 6; i++) label(p, "A popup row", THEME_FONT_BODY, theme::TEXT);
          lv_anim_delete(p, NULL);
          lv_obj_set_style_opa(p, LV_OPA_COVER, 0);
          lv_obj_set_style_translate_y(p, 0, 0);
          lv_refr_now(NULL);
          uint32_t tf = micros();
          for (int i = 0; i < 5; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
          float frozen = (micros() - tf) / 5000.0f;
          perfDrawProfile("frozen");
          Serial.printf("PERF popup over home: full frame %.1f ms (opening with the freeze took %.1f ms, %s)\n",
                        frozen, took, freeze::s_img ? "frozen" : "NOT frozen");
          lv_obj_delete(ov);
          Serial.printf("PERF popup closed: image %s\n", freeze::s_img ? "LEFT" : "gone");
          lv_refr_now(NULL);
        }
      }
      if (step % 10 == 0 && step > 0) {   // a static full-screen redraw of Home, for the baseline
        uint32_t fl0 = lvport::s_flush_us, t = micros();
        for (int i = 0; i < 10; i++) { lv_obj_invalidate(lv_screen_active()); lv_refr_now(NULL); }
        Serial.printf("PERF full redraw (Home) %5.1f ms, flush %5.1f ms\n", (micros() - t) / 10000.0f,
                      (lvport::s_flush_us - fl0) / 10000.0f);
      }
      if (step % 10 == 9) perfMap();   // the map: tiles decoded while it opened, panning, a full redraw
      uint32_t tb = micros();
      switch (step++ % 10) {
        case 8: name = "map"; perfMap(true); openMap(true); next = millis() + 8000; break;
        case 9: name = "back-home"; back(); break;
        case 0: name = "settings"; showSettings(); break;
        case 1: name = "back-home"; back(); break;
        case 2: name = "messages"; showChats(); break;
        case 3: name = "back-home"; back(); break;
        case 4: name = "nearby"; showNearby(); break;
        case 5: name = "back-home"; back(); break;
        case 6: name = "page-2"; setHomePage(1); break;
        case 7: name = "page-1"; setHomePage(0); break;
      }
      Serial.printf("PERF build %-10s %5.1f ms\n", name, (micros() - tb) / 1000.0f);
    }
  }
#endif
#ifdef PIN_BUZZER
  _buzzer.loop();
  if (soundctl::tick(_prefs, _buzzer, isClientConnected(), rtc_clock.getCurrentTime()) && !_asleep) refreshStatusBar();
  // The alarm melody repeats until dismissed or the ring window ends.
  if (_core->clock.isRinging() && !_buzzer.isPlaying()) playMelody(soundctl::MEL_ALARM);
#endif
  checkLowBattery();
  mapDownloadTick();   // a map download keeps running on any screen, and asleep
  otaTick();           // so does a firmware update
  trailJournalTick();   // the live trail's copy on the card, once a minute
  if ((int32_t)(millis() - _next_trackback_ms) >= 0) {   // walking the trail back, on any screen
    _next_trackback_ms = millis() + 1000;
    navPollTrackBack();
  }

  uint32_t lv_next = 0;
  if (!_asleep) {
    if ((int32_t)(millis() - _next_status_ms) >= 0) {
      _next_status_ms = millis() + 1000;
      refreshStatusBar();
      refreshLock();
      if (_screen == SCR_HOME) refreshHome();
      if (s_chats_stale && !_nav_overlay) refreshChats();   // a row was held when it came
      refreshDiag();
      refreshCompass();
      refreshGps();
      refreshBattCurve();
      if (quickPanelOpen()) quickPanelRefresh();
    }
    if (locked()) lockPoll();
    else if (_screen == SCR_HOME) { homeSwipePoll(); homeMapLoop(); }
    if ((_screen == SCR_NEARBY || _screen == SCR_NODE) && (int32_t)(millis() - _next_nearby_ms) >= 0) {
      // Scan results trickle in over a few seconds: poll fast while scanning.
      _next_nearby_ms = millis() + (_scanning ? 250 : 2000);
      if (_scanning && (int32_t)(millis() - _scan_until_ms) >= 0) _scanning = false;
      if (_screen == SCR_NEARBY) {
        refreshNearbyList();
        if (_scan_overlay) refreshScanPopup();
      } else {
        refreshNode();
      }
    }
    fillTick();
    if (_screen == SCR_MAP) mapLoop();
    if (_screen == SCR_WIFI) pollWifiScan();
    if (_screen == SCR_STORAGE) pollStorage();
    if (_screen == SCR_ADMIN) adminPoll();
    roomPoll();
    if (_screen == SCR_CLOCK && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 100;   // stopwatch tenths
      refreshClock();
    }
    if (_screen == SCR_MELODY && (int32_t)(millis() - _next_clock_ms) >= 0) {
      _next_clock_ms = millis() + 30;    // the playing note's highlight keeps up
      refreshMelody();
    }
    if (_screen == SCR_THREAD && (int32_t)(millis() - _next_thread_check_ms) >= 0) {
      _next_thread_check_ms = millis() + 500;
      uint32_t sig = threadSignature();
      if (_thread_dirty || sig != _thread_sig) refreshThread();
      else if ((int32_t)(millis() - _next_thread_ages_ms) >= 0) refreshThreadAges();
      messagePopupTick();
    }
    lvport::cpuSlow(!cpuNeeded());
    lv_next = lv_timer_handler();
  }
  lvport::idle(idleMillis(lv_next));
}

// How long loop() may sleep: nothing is due before then. Not at all while the
// mesh has packets queued; briefly while a melody plays (its notes are timed
// here) or frames to or from the app wait (read one a pass, sent spaced out).
// A frame arriving from the app over BLE wakes the loop (serialFrameArrived).
uint32_t UITask::idleMillis(uint32_t lv_next) {
  if (the_mesh.hasPendingWork()) return 0;
#ifdef PIN_BUZZER
  if (_buzzer.isPlaying()) return 1;
#endif
  if (isClientConnected() && _serial->hasPendingFrames()) return 2;
  if (_asleep) return 50;   // as often as the buttons and tap to wake are polled
  return lv_next < 10 ? lv_next : 10;   // LVGL's next timer: a refresh, an animation, input
}

void UITask::shutdown(bool restart) {
  s_prefs_dirty_ms = s_prefs_save_at = 0;
  the_mesh.savePrefs();
  the_mesh.saveRTCTime();
  the_mesh.flushDirtyContacts();
  if (!trailJournalTick(true)) _core->trail.onShutdown();   // no card: the internal slot, if enabled
#ifdef PIN_BUZZER
  _buzzer.shutdown();   // the goodbye sound, unless muted
#ifndef SIM_PLATFORM   // the sim's single thread would freeze the page
  for (uint32_t t0 = millis(); _buzzer.isPlaying() && millis() - t0 < 2500; ) { _buzzer.loop(); delay(10); }
#endif
#endif
  if (restart) {
    _board->reboot();
  } else {
    if (_display) _display->turnOff();
    radio_driver.powerOff();
    if (_sensors) {
      LocationProvider* loc = _sensors->getLocationProvider();
      if (loc) loc->stop();
    }
    _board->powerOff();
  }
}

// Settings > Power > Battery shutdown (NodePrefs::low_batt_mv), as
// ui-new does it: a smoothed reading every 8 s, never while on USB power.
void UITask::checkLowBattery() {
  if ((int32_t)(millis() - _next_batt_ms) < 0) return;
  _next_batt_ms = millis() + 8000;
  uint16_t raw = getBattMilliVolts();
  if (raw > 0) _batt_mv = _batt_mv == 0 ? raw : (uint16_t)((_batt_mv * 4u + raw) / 5u);   // EMA, alpha 0.2
  uint16_t low = _prefs ? _prefs->low_batt_mv : 0;
  if (low == 0 || _batt_mv == 0 || _batt_mv >= low || _board->isExternalPowered()) return;
  wake();
  showToast("Low battery - shutting down", 3000);
  lv_timer_handler();
  delay(2000);
  shutdown();
}

uint32_t UITask::autoOffMillis() const {
  if (!_prefs || _prefs->auto_off_secs == 0) return 0;
  return (uint32_t)_prefs->auto_off_secs * 1000UL;
}

void UITask::sleep() {
  if (_asleep) return;
  _asleep = true;
  quickPanelClose(false);
  if (_display) _display->turnOff();
  prefsSaveSoon(0);   // nothing to stall now
  lvport::powerSave(true, _tap_wake);
  if ((_prefs && _prefs->auto_lock) || pinSet()) lockScreen();   // Lock screen, or a screen PIN
}

void UITask::wake() {
  lv_display_trigger_activity(NULL);
  if (!_asleep) return;
  _asleep = false;
  lvport::powerSave(false, _tap_wake);
  if (_display) _display->turnOn();   // dark until the frame below is on the panel
  refreshStatusBar();
  if (_screen == SCR_HOME) refreshHome();
  if (_screen == SCR_THREAD) refreshThread();
  lv_obj_invalidate(lv_screen_active());
  lv_refr_now(NULL);
  lvport::backlightFadeIn();
}

// ── Core events ───────────────────────────────────────────────────────────────

void UITask::drainCoreEvents() {
  UiEvent ev;
  while (_core->events.pop(ev)) {
    switch (ev.type) {
    case UiEventType::MessageArrived:
      onMessageArrived(ev);
      break;
    case UiEventType::ClockAlert:
      showRing(ev.text);
      playMelody(soundctl::MEL_ALARM);
      break;
    case UiEventType::ClockRingEnded:
      stopMelody();
      hideRing();
      break;
    case UiEventType::LiveShareEnded:
      showToast("Live share ended");
      break;
    case UiEventType::LocatorCrossed:
      showToast(ev.text, 3000);
#ifdef PIN_BUZZER
      if (!_buzzer.isQuiet()) playMelody(ev.flag ? soundctl::MEL_ARRIVE : soundctl::MEL_LEAVE);
#endif
      break;
    case UiEventType::LocatorBeep:   // Settings > Map > Proximity beeper
      playMelody(soundctl::MEL_TICK);
      break;
    case UiEventType::AdvertHeard:
      notify(ev.flag ? UIEventType::advertReceivedFlood : UIEventType::advertReceivedZeroHop);
      break;
    default:
      break;
    }
  }
}

// ── Message banner ──
// An incoming message slides down from under the status bar: an icon, the
// sender (or channel) and the start of the text. A tap opens the conversation
// (not while the screen is locked), a swipe up puts it away, and it goes by
// itself after BANNER_MS. The toast at the bottom stays for everything else.
static lv_obj_t*   s_banner = nullptr;
static lv_obj_t*   s_banner_icon = nullptr;
static lv_obj_t*   s_banner_title = nullptr;
static lv_obj_t*   s_banner_text = nullptr;
static lv_timer_t* s_banner_timer = nullptr;
static UIEventType s_banner_kind = UIEventType::none;
static int16_t     s_banner_ch = -1;
static uint8_t     s_banner_key[4];
static const uint32_t BANNER_MS = 4000;

static void bannerHidden(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }
static void bannerHide() {
  if (!s_banner || lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) return;
  if (s_banner_timer) lv_timer_pause(s_banner_timer);
  lv_anim_delete(s_banner, NULL);
  int32_t off = -(lv_obj_get_height(s_banner) + theme::STATUS_H);
  anim::run(s_banner, anim::setTy, lv_obj_get_style_translate_y(s_banner, LV_PART_MAIN), off, anim::OUT_MS, bannerHidden);
}
static void bannerTimerCb(lv_timer_t* t) { (void)t; bannerHide(); }
static void onBanner(lv_event_t* e) {
  if (lv_event_get_code(e) == LV_EVENT_GESTURE) {
    if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) bannerHide();
    return;
  }
  bannerHide();
  s_ui->bannerOpen();
}

// Built once, behind the status bar (made just after it on the top layer).
static void bannerBuild() {
  const int32_t w = lv_display_get_horizontal_resolution(NULL);
  s_banner = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_set_size(s_banner, w - 12, LV_SIZE_CONTENT);
  lv_obj_align(s_banner, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 2);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_bg_color(s_banner, lv_color_hex(theme::SURFACE), LV_STATE_PRESSED);
  lv_obj_set_style_border_color(s_banner, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(s_banner, 1, 0);
  lv_obj_set_style_radius(s_banner, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(s_banner, 12, 0);
  lv_obj_set_style_shadow_color(s_banner, lv_color_hex(0x000000), 0);
  lv_obj_set_style_shadow_opa(s_banner, LV_OPA_60, 0);
  lv_obj_set_style_pad_all(s_banner, 8, 0);
  lv_obj_set_style_pad_column(s_banner, 10, 0);
  lv_obj_set_flex_flow(s_banner, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(s_banner, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_add_event_cb(s_banner, onBanner, LV_EVENT_CLICKED, NULL);
  lv_obj_add_event_cb(s_banner, onBanner, LV_EVENT_GESTURE, NULL);
  s_banner_icon = label(s_banner, LV_SYMBOL_ENVELOPE, THEME_FONT_TITLE, theme::ACCENT);
  lv_obj_set_width(s_banner_icon, 20);   // the text lines up whichever icon
  lv_obj_set_style_text_align(s_banner_icon, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_t* col = flexBox(s_banner, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_width(col, 1);
  lv_obj_set_flex_grow(col, 1);
  lv_obj_set_style_pad_row(col, 1, 0);
  s_banner_title = label(col, "", THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(s_banner_title, LV_LABEL_LONG_DOT);
  lv_obj_set_width(s_banner_title, LV_PCT(100));
  s_banner_text = label(col, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_label_set_long_mode(s_banner_text, LV_LABEL_LONG_DOT);   // (LONG_DOT cuts at the max height)
  lv_obj_set_width(s_banner_text, LV_PCT(100));
  lv_obj_set_style_max_height(s_banner_text, lv_font_get_line_height(THEME_FONT_SMALL) * 2, 0);   // two lines, then "..."
  lv_obj_add_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
}

static void bannerShow(const char* icon, const char* title, const char* text) {
  if (!s_banner) return;
  lv_label_set_text(s_banner_icon, icon);
  lv_label_set_text(s_banner_title, title);
  lv_label_set_text(s_banner_text, text);
  bool shown = !lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(s_banner, NULL);
  lv_obj_remove_flag(s_banner, LV_OBJ_FLAG_HIDDEN);
  if (!shown) {   // a new one while it's up just changes the text
    layoutNow(s_banner);
    anim::run(s_banner, anim::setTy, -(lv_obj_get_height(s_banner) + theme::STATUS_H), 0, anim::SCREEN_MS);
  } else {
    lv_obj_set_style_translate_y(s_banner, 0, 0);
  }
  if (!s_banner_timer) s_banner_timer = lv_timer_create(bannerTimerCb, BANNER_MS, NULL);
  lv_timer_set_period(s_banner_timer, BANNER_MS);
  lv_timer_reset(s_banner_timer);
  lv_timer_resume(s_banner_timer);
}

void UITask::bannerOpen() {
  if (locked()) return;   // the lock screen first
  if (s_banner_kind == UIEventType::channelMessage && s_banner_ch >= 0) openChannel((uint8_t)s_banner_ch);
  else if (s_banner_kind == UIEventType::contactMessage) openDM(s_banner_key);
  else showChats();   // a room post: the list
}

void UITask::onMessageArrived(const UiEvent& ev) {
  if (ev.kind == UIEventType::contactMessage && ev.flag) {   // the sender's alert / melody overrides
    memcpy(_notif_dm_prefix, ev.key, 4);
    _notif_dm_valid = true;
  }
  if (ev.kind == UIEventType::channelMessage) _notif_ch_idx = ev.idx;
  notify(ev.kind);
  // The banner: the name from the event, the text from the newest history entry.
  const char* text = "";
  const char* icon = LV_SYMBOL_ENVELOPE;
  s_banner_kind = ev.kind;
  s_banner_ch = ev.idx;
  memcpy(s_banner_key, ev.key, 4);
  if (ev.kind == UIEventType::channelMessage) {
    icon = "#";
    int pos = ev.idx >= 0 ? _core->history.histEntryForChannel(ev.idx, 0) : -1;
    if (pos >= 0) text = _core->history.chAtPos(pos).text;
  } else if (ev.kind == UIEventType::contactMessage) {
    int pos = _core->history.dmHistEntryForContact(ev.key, 0);
    if (pos >= 0) text = _core->history.dmAtPos(pos).text;
  } else {
    icon = LV_SYMBOL_LIST;   // a room
  }
  // Wake for the message unless an app is already showing it, or the user
  // turned message-wake off.
  bool wake_disabled = _prefs && (!_prefs->msg_wake || inQuietHours(*_prefs, rtc_clock.getCurrentTime()));   // quiet hours: dark too
  if (_asleep && !wake_disabled && !isClientConnected()) wake();
  else if (!_asleep) lv_display_trigger_activity(NULL);
  bool open_here = _screen == SCR_THREAD &&
      (ev.kind == UIEventType::channelMessage ? _thread_is_channel && ev.idx == _thread_channel
                                              : !_thread_is_channel && memcmp(_thread_key, ev.key, 4) == 0);
  if (!open_here) bannerShow(icon, ev.text, text);   // not over the very conversation it's in
  if (_screen == SCR_CHATS && !_nav_overlay) refreshChats();   // new unread counts (not under an open popup)
}

bool UITask::isViewingChannel(uint8_t channel_idx) {
  return _screen == SCR_THREAD && _thread_is_channel && _thread_channel == channel_idx;
}

bool UITask::isViewingDM(const uint8_t* pub_key) {
  return _screen == SCR_THREAD && !_thread_is_channel && memcmp(_thread_key, pub_key, 4) == 0;
}

// ── Status bar + toast (top layer, over every screen) ─────────────────────────

static void onStatusBarTouch(lv_event_t* e);   // QuickPanel.h

void UITask::buildStatusBar() {
  // The banner and the toast first: the status bar, made after them, covers
  // them as they slide in and out from under it.
  bannerBuild();
  _toast = lv_obj_create(lv_layer_top());
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_toast, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_bg_color(_toast, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_color(_toast, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(_toast, 1, 0);
  lv_obj_set_style_radius(_toast, theme::RADIUS, 0);
  lv_obj_set_style_pad_hor(_toast, 12, 0);
  lv_obj_set_style_pad_ver(_toast, 6, 0);
  lv_obj_align(_toast, LV_ALIGN_TOP_MID, 0, theme::STATUS_H + 4);   // slides down from under the status bar
  lv_obj_t* tl = label(_toast, "", THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(tl, LV_LABEL_LONG_WRAP);   // long texts wrap instead of running off the screen
  lv_obj_set_style_max_width(tl, lv_display_get_horizontal_resolution(NULL) - 40, 0);
  lv_obj_set_style_text_align(tl, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_add_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_obj_t* bar = lv_obj_create(lv_layer_top());
  s_status_bar = bar;
  styleOpaque(bar, theme::BG);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(bar, LV_OBJ_FLAG_PRESS_LOCK);   // pulled down past it: still its touch (the quick panel)
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_GESTURE_BUBBLE);
  lv_obj_add_event_cb(bar, onStatusBarTouch, LV_EVENT_ALL, NULL);
  lv_obj_set_size(bar, LV_PCT(100), theme::STATUS_H);
  lv_obj_align(bar, LV_ALIGN_TOP_MID, 0, 0);
  lv_obj_set_style_pad_hor(bar, theme::PAD, 0);

  _status_time = label(bar, "--:--", THEME_FONT_SMALL, theme::TEXT);
  lv_obj_align(_status_time, LV_ALIGN_LEFT_MID, 0, 0);
  _status_batt = label(bar, "", THEME_FONT_ICONS, theme::TEXT_MUTED);
  lv_obj_align(_status_batt, LV_ALIGN_RIGHT_MID, 0, 0);
  _status_chg = label(bar, LV_SYMBOL_CHARGE, THEME_FONT_ICONS, theme::OK);
  lv_obj_add_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);
  _status_icons = lv_obj_create(bar);
  lv_obj_remove_style_all(_status_icons);
  lv_obj_remove_flag(_status_icons, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(_status_icons, 200, LV_PCT(100));   // fixed: packed to its right edge
  lv_obj_set_flex_flow(_status_icons, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(_status_icons, LV_FLEX_ALIGN_END, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(_status_icons, 2, 0);


  refreshStatusBar();
}

// lv_obj_align_to(o, base, LV_ALIGN_OUT_LEFT_MID, dx, 0), `base` a sibling,
// for what's refreshed every second: LVGL restyles the box on every call, so
// it redraws moved or not -- here it's only moved.
static void placeLeftOf(lv_obj_t* o, lv_obj_t* base, int32_t dx) {
  layoutNow(o);
  int32_t x = lv_obj_get_x(base) - lv_obj_get_width(o) + dx;
  int32_t y = lv_obj_get_y(base) + (lv_obj_get_height(base) - lv_obj_get_height(o)) / 2;
  if (lv_obj_get_style_align(o, LV_PART_MAIN) != LV_ALIGN_TOP_LEFT || lv_obj_get_x(o) != x || lv_obj_get_y(o) != y)
    lv_obj_align_to(o, base, LV_ALIGN_OUT_LEFT_MID, dx, 0);
}

void UITask::refreshStatusBar() {
  if (!_status_time) return;
  struct tm ti;
  if (localTime(_prefs, ti)) {
    char clk[12];
    fmtClock(clk, sizeof(clk), ti, _prefs, true);
    setText(_status_time, clk);
  } else {
    setText(_status_time, "--:--");
  }

  // Battery: the icon, then % or volts per Settings > Power > Battery display. The
  // smoothed reading (checkLowBattery): an ADC read blocks the loop for 10 ms.
  uint16_t mv = _batt_mv ? _batt_mv : getBattMilliVolts();
  int pct = battery::percent(mv, _prefs ? _prefs->low_batt_mv : 0);
  const char* batt = pct > 80 ? LV_SYMBOL_BATTERY_FULL : pct > 55 ? LV_SYMBOL_BATTERY_3
                   : pct > 30 ? LV_SYMBOL_BATTERY_2 : pct > 10 ? LV_SYMBOL_BATTERY_1 : LV_SYMBOL_BATTERY_EMPTY;
  char level[12] = "";
  switch (battery::mode(_prefs ? _prefs->batt_display_mode : 0)) {
    case battery::PERCENT: snprintf(level, sizeof(level), " %d%%", pct); break;
    case battery::VOLTAGE: snprintf(level, sizeof(level), " %u.%02u V", mv / 1000, (mv % 1000) / 10); break;
    default: break;
  }
  setTextFmt(_status_batt, "%s%s", batt, level);
  lv_obj_t* left_of = _status_batt;   // the icons pack up to this
  if (_board->isExternalPowered()) {
    lv_obj_remove_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);
    placeLeftOf(_status_chg, _status_batt, -2);
    left_of = _status_chg;
  } else lv_obj_add_flag(_status_chg, LV_OBJ_FLAG_HIDDEN);

  // Status icons, as the original's status bar (ui-new): Bluetooth (accent
  // when the app is connected), WiFi (when switched on, accent connected), GPS (green with a fix), the alarm, mute, then
  // the modes that keep running in the background -- auto-advert, trail, live
  // share, repeater, arrival alert -- in the accent colour. Right to left in
  // that order, next to the battery.
  struct Icon { const char* sym; uint32_t col; };
  Icon icons[11];
  int n = 0;
  if (isSerialEnabled()) icons[n++] = { LV_SYMBOL_BLUETOOTH, hasConnection() ? theme::ACCENT : theme::TEXT_MUTED };
  if (lvport::wifiAllowed())   // WiFi switched on: accent while connected (map tiles, update)
    icons[n++] = { LV_SYMBOL_WIFI, lvport::netRadio() == lvport::NET_UP ? theme::ACCENT : theme::TEXT_MUTED };
  int32_t lat, lon;
  if (_core->gpsEnabled()) {   // switched off, the module still reports its last fix as valid
    bool fix = _core->course.currentLocation(lat, lon);
    icons[n++] = { LV_SYMBOL_GPS, fix ? theme::OK : theme::TEXT_MUTED };
  }
  if (_prefs && _prefs->alarm_on) icons[n++] = { LV_SYMBOL_BELL, theme::TEXT_MUTED };
#ifdef PIN_BUZZER
  if (_buzzer.isQuiet()) icons[n++] = { UI_SYMBOL_MUTE, theme::TEXT_MUTED };
#endif
  if (_prefs && _prefs->advert_auto_interval_sec > 0) icons[n++] = { UI_SYMBOL_RADIO, theme::ACCENT };
  if (_core->trail.isActive()) icons[n++] = { UI_SYMBOL_ROUTE, theme::ACCENT };
  if (_prefs && _prefs->loc_share_enabled) icons[n++] = { UI_SYMBOL_PIN, theme::ACCENT };
  if (_prefs && _prefs->client_repeat) icons[n++] = { LV_SYMBOL_LOOP, theme::ACCENT };
  if (_prefs && _prefs->locator_enabled && _prefs->locator_has_target) icons[n++] = { UI_SYMBOL_FLAG, theme::ACCENT };
  char sig[sizeof(_status_sig)];
  int o = 0;
  for (int i = 0; i < n && o < (int)sizeof(sig) - 12; i++) o += snprintf(sig + o, sizeof(sig) - o, "%s%lx", icons[i].sym, (unsigned long)icons[i].col);
  sig[o] = '\0';
  if (strcmp(sig, _status_sig) != 0) {   // rebuilt only when something changed
    strcpy(_status_sig, sig);
    lv_obj_clean(_status_icons);
    for (int i = n - 1; i >= 0; i--)   // the icon font gives each the same size and cell
      label(_status_icons, icons[i].sym, THEME_FONT_ICONS, icons[i].col);
  }
  placeLeftOf(_status_icons, left_of, -5);
}

void UITask::setGps(bool on) {
  if (!_core->setGpsEnabled(on, false)) { showToast("No GPS on this device"); return; }
  prefsSave();   // written on leaving the screen, as the other switches
  refreshStatusBar();
}

void UITask::botSetGPS(bool on) { if (_core->setGpsEnabled(on)) refreshStatusBar(); }

bool UITask::ensureGps() {
  int32_t lat, lon;
  if (_core->gpsAvailable() && !_core->gpsEnabled()) { setGps(true); return false; }   // off, its last fix still reads valid
  if (_core->course.currentLocation(lat, lon)) return true;
  showToast("Waiting for a GPS fix");
  return false;
}

// One persistent timer, paused between toasts: hides the toast when it fires.
static void toastHidden(lv_anim_t* a) { lv_obj_add_flag((lv_obj_t*)a->var, LV_OBJ_FLAG_HIDDEN); }
static void toastTimerCb(lv_timer_t* t) {   // back up under the status bar
  lv_obj_t* o = (lv_obj_t*)lv_timer_get_user_data(t);
  anim::run(o, anim::setTy, 0, -(lv_obj_get_y(o) + lv_obj_get_height(o)), anim::OUT_MS, toastHidden);
  lv_timer_pause(t);
}

// A deliberate restart (the card formatted, the USB drive given back): a full
// screen saying so -- drawn at once, the loop stops here -- unsaved settings
// written, then the restart. Without it the screen just froze.
static void restartScreen(const char* why) {
  lv_obj_t* o = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(o);
  lv_obj_set_size(o, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(o, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(o, LV_OPA_COVER, 0);
  lv_obj_set_flex_flow(o, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(o, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_row(o, 8, 0);
  label(o, LV_SYMBOL_REFRESH, THEME_FONT_LARGE, theme::ACCENT);
  label(o, "Restarting...", THEME_FONT_LARGE, theme::TEXT);
  lv_obj_t* w = label(o, why, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_text_align(w, LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_invalidate(o);
  lv_refr_now(NULL);
  prefsSaveSoon(0);
  prefsFlush();
  delay(1200);   // long enough to read
  lvport::restart();
  lv_obj_delete(o);   // the sim carries on
}

// The screen as it is, toasts and popups included, re-rendered once into a
// buffer the flush callback fills (lvport::shotCopy).
uint16_t* UITask::captureFrame(int& w, int& h) {
  lv_display_t* d = lv_display_get_default();
  w = lv_display_get_horizontal_resolution(d);
  h = lv_display_get_vertical_resolution(d);
#ifdef ESP32
  uint16_t* px = (uint16_t*)heap_caps_malloc(w * h * 2, MALLOC_CAP_SPIRAM);
#else
  uint16_t* px = (uint16_t*)malloc(w * h * 2);
#endif
  if (!px) return nullptr;
  lvport::s_shot = px;
  lv_obj_invalidate(lv_screen_active());   // the whole display, every layer
  lv_refr_now(d);
  lvport::s_shot = nullptr;
  return px;
}

// The screen, written to the card as a 24-bit BMP: /sdcard/screenshots/scr_NNNN.bmp.
void UITask::takeScreenshot() {
  if (!lvport::mountStorage()) { showToast("No SD card"); return; }
  mkdir("/sdcard", 0777);   // the sim's MEMFS may not have it yet (the card: fails, harmless)
  mkdir("/sdcard/screenshots", 0777);
  char path[48];
  struct stat st;
  int n = 1;
  for (; n <= 9999; n++) {
    snprintf(path, sizeof(path), "/sdcard/screenshots/scr_%04d.bmp", n);
    if (stat(path, &st) != 0) break;
  }
  if (n > 9999) { showToast("Screenshots folder full"); return; }
  int W, H;
  uint16_t* px = captureFrame(W, H);
  if (!px) { showToast("Out of memory"); return; }

  bool ok = false;
  if (FILE* f = fopen(path, "wb")) {
    const uint32_t row = (uint32_t)W * 3, size = 54 + row * H;   // W*3 is a multiple of 4 at 320
    uint8_t hdr[54] = {'B', 'M'};
    auto put32 = [&](int at, uint32_t v) { for (int i = 0; i < 4; i++) hdr[at + i] = (uint8_t)(v >> (8 * i)); };
    put32(2, size); put32(10, 54); put32(14, 40); put32(18, W); put32(22, H);
    hdr[26] = 1; hdr[28] = 24; put32(34, row * H);
    ok = fwrite(hdr, 1, 54, f) == 54;
    static uint8_t line[320 * 3];   // not on the loop task's stack
    for (int32_t y = H - 1; ok && y >= 0; y--) {   // bottom-up, BGR
      const uint16_t* s = px + y * W;
      for (int32_t x = 0; x < W && x < 320; x++) {
        uint16_t c = s[x];
        uint8_t r = (c >> 11) & 0x1F, g = (c >> 5) & 0x3F, b = c & 0x1F;
        line[x * 3 + 0] = (uint8_t)((b << 3) | (b >> 2));
        line[x * 3 + 1] = (uint8_t)((g << 2) | (g >> 4));
        line[x * 3 + 2] = (uint8_t)((r << 3) | (r >> 2));
      }
      ok = fwrite(line, 1, row, f) == row;
    }
    ok = (fclose(f) == 0) && ok;
  }
  free(px);
  if (!ok) { remove(path); showToast("Couldn't save the screenshot"); return; }

  // A white frame flashing round the edges, then where it went. Four thin
  // strips, not a full-screen layer: each frame of the fade redraws only
  // them (a whole 320x240 frame per step tore and stuttered on the panel).
  const int32_t T = 6;
  const int32_t strip[4][4] = { { 0, 0, W, T }, { 0, H - T, W, T }, { 0, T, T, H - 2 * T }, { W - T, T, T, H - 2 * T } };
  for (const auto& r : strip) {
    lv_obj_t* f = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(f);
    lv_obj_set_pos(f, r[0], r[1]);
    lv_obj_set_size(f, r[2], r[3]);
    lv_obj_set_style_bg_color(f, lv_color_white(), 0);
    lv_obj_remove_flag(f, LV_OBJ_FLAG_CLICKABLE);
    anim::run(f, anim::setBgOpa, LV_OPA_COVER, LV_OPA_TRANSP, 400, anim::coverDone);
  }
  char msg[40];
  snprintf(msg, sizeof(msg), "Saved scr_%04d.bmp", n);
  showToast(msg);
}

void UITask::showToast(const char* text, uint32_t ms) {
  if (!_toast) return;
  lv_label_set_text(lv_obj_get_child(_toast, 0), text);
  bool shown = !lv_obj_has_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  lv_anim_delete(_toast, NULL);   // on its way out
  lv_obj_remove_flag(_toast, LV_OBJ_FLAG_HIDDEN);
  // Under the message banner while that's up, else right under the status bar.
  int32_t y = theme::STATUS_H + 4;
  if (s_banner && !lv_obj_has_flag(s_banner, LV_OBJ_FLAG_HIDDEN)) y = lv_obj_get_y(s_banner) + lv_obj_get_height(s_banner) + 4;
  lv_obj_align(_toast, LV_ALIGN_TOP_MID, 0, y);
  layoutNow(_toast);
  if (!shown) anim::run(_toast, anim::setTy, -(y + lv_obj_get_height(_toast)), 0, anim::POP_MS);   // a replaced text just changes
  else lv_obj_set_style_translate_y(_toast, 0, 0);
  if (!_toast_timer) _toast_timer = lv_timer_create(toastTimerCb, ms, _toast);
  lv_timer_set_period(_toast_timer, ms);
  lv_timer_reset(_toast_timer);
  lv_timer_resume(_toast_timer);
}

// ── Screens ───────────────────────────────────────────────────────────────────

static void onBack(lv_event_t* e) { (void)e; s_ui->back(); }

// A fresh screen below the status bar, loaded in place of the current one;
// returns its content area (flex column). The old screen is deleted
// asynchronously -- this usually runs from a click on one of its own widgets.
static uint32_t s_wifi_test_ms = 0;   // Settings > WiFi checking a just-saved network (WifiScreen.h)

namespace home { static void leave(); }   // HomeScreen.h

static int s_th_rows_n = 0;   // conversation bubbles built (refreshThread)

// A screen builds in pieces over several loop passes: what shows first at
// once, the rest as time allows. A widget costs ~1 ms on the L2 (they live in
// PSRAM), so 48 nodes, a few hundred contacts or a long settings page built
// in one go held the screen for up to a second. The pieces queue in order:
// fill(fn, from, to) makes fn(from) .. fn(to - 1), each from the data as it
// is then; then(fn, arg) makes its piece at once unless others still wait,
// keeping its place after them. A new screen drops what is left.
struct FillJob { void (UITask::*fn)(int); int next, end; };
static const int FILL_JOBS = 12;
static FillJob s_fill[FILL_JOBS];
static int s_fill_n = 0;
static const uint32_t FILL_BUDGET_US = 25000;   // pieces made per loop pass: about a frame's worth

void UITask::fill(void (UITask::*fn)(int), int from, int to) {
  if (from >= to) return;
  if (s_fill_n == FILL_JOBS) { while (from < to) (this->*fn)(from++); return; }   // no room: now
  s_fill[s_fill_n++] = { fn, from, to };
}

void UITask::then(void (UITask::*fn)(int), int arg) {
  if (s_fill_n) fill(fn, arg, arg + 1);
  else (this->*fn)(arg);
}

// Rows 0 .. n-1 of a list: the first `first` at once, the rest queued.
void UITask::fillStart(int n, int first, void (UITask::*row)(int)) {
  int now = first < n ? first : n;
  for (int i = 0; i < now; i++) (this->*row)(i);
  fill(row, now, n);
}

void UITask::fillStep() {
  FillJob& j = s_fill[0];
  (this->*j.fn)(j.next++);   // may queue more, behind
  if (j.next >= j.end) memmove(s_fill, s_fill + 1, --s_fill_n * sizeof(FillJob));
}

void UITask::fillTick() {
  // Not while the screen moves in: its frames came a budget apart (Nodes,
  // 57 rows: 33 ms a frame, up to 65). What it shows first is built already.
  if (!s_fill_n || (_body && lv_anim_get(_body, NULL)) || lv_display_get_screen_prev(NULL)) return;
  uint32_t t0 = micros();
  while (s_fill_n) {
    fillStep();
    if (micros() - t0 >= FILL_BUDGET_US) break;
  }
}

// Everything still queued, now: before scrolling to where the screen was.
void UITask::fillFlush() { while (s_fill_n) fillStep(); }

int UITask::fillPending() const {
  int n = 0;
  for (int k = 0; k < s_fill_n; k++) n += s_fill[k].end - s_fill[k].next;
  return n;
}

#include "NodeViz.h"   // the node detail's rose and path (newScreen() drops its card)

lv_obj_t* UITask::newScreen(const char* title, bool with_back) {
  quickPanelClose(false);
  s_fill_n = 0;   // what was still to be built goes with the screen
  _home_clock = _home_date = _home_unread = nullptr;
  _thread_list = _compose_ta = _keyboard = nullptr;
  s_th_rows_n = 0;   // the bubbles went with the screen
  if (_shown_screen == SCR_SETTINGS && _body) s_settings_y = lv_obj_get_scroll_y(_body);
  pickerClose();
  _header = _body = nullptr;
  _nearby_list = _nearby_status = _nearby_sort_lbl = _nearby_chips = nullptr;
  _node_info = _node_ping = _node_delete_lbl = nullptr;
  nodeviz::s_card = nullptr;
  _scan_overlay = _scan_list = _scan_status = nullptr;   // the popup went with the old screen
  _map_area = _map_marks = _map_me = _map_zoom_lbl = _map_hint = _map_dl_pill = _map_center_btn = nullptr;
  _dl_overlay = _dl_info = _dl_sub = _dl_start_lbl = _dl_job_row = _dl_job_lbl = nullptr;
  _nav_bar = _nav_title = _nav_info = _nav_clear = nullptr;
  _nav_overlay = _nav_ta = _nav_kb = _nav_del_lbl = _nav_rec = _nav_avg_pill = nullptr;
  _nav_trail_lbl = _nav_trail_btn = _nav_reset_lbl = _nav_share_lbl = _nav_share_btn = _nav_tb_btn = nullptr;
  _wifi_ssid = _wifi_pass = _wifi_kb = _wifi_list = _wifi_status = nullptr;
  if ((_wifi_scanning || s_wifi_test_ms) && !wifiInUse()) lvport::netEnd();   // left mid-scan / check: the radio goes off
  _wifi_scanning = false;
  s_wifi_test_ms = 0;
  _ota_status = _ota_bar = _ota_btn = _ota_btn_lbl = nullptr;
  for (lv_obj_t*& t : _map_tiles) t = nullptr;
  lv_obj_t* prev = _scr;
  lv_obj_t* scr = lv_obj_create(NULL);
  s_fit_screen = scr;   // rowsFit()
  s_fit_pending = true;
  styleOpaque(scr, theme::BG);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  int top = theme::STATUS_H;
  if (title) {
    lv_obj_t* hdr = lv_obj_create(scr);
    styleSurface(hdr, theme::BG);
    lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(hdr, LV_PCT(100), 38);
    lv_obj_set_pos(hdr, 0, top);
    _header = hdr;
    if (with_back) {
      lv_obj_t* b = lv_button_create(hdr);
      lv_obj_set_size(b, 40, 28);
      lv_obj_align(b, LV_ALIGN_LEFT_MID, 4, 0);
      lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
      lv_obj_set_style_shadow_width(b, 0, 0);
      lv_obj_add_event_cb(b, onBack, LV_EVENT_CLICKED, NULL);
      lv_obj_center(label(b, LV_SYMBOL_LEFT, THEME_FONT_BODY, theme::TEXT));
    }
    lv_obj_t* t = label(hdr, title, THEME_FONT_TITLE, theme::TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
    lv_obj_set_width(t, 230);
    lv_obj_align(t, LV_ALIGN_LEFT_MID, with_back ? 52 : theme::PAD, 0);
    top += 38;
  }

  lv_obj_t* body = lv_obj_create(scr);
  styleSurface(body, theme::BG);
  lv_obj_set_size(body, lv_display_get_horizontal_resolution(NULL), lv_display_get_vertical_resolution(NULL) - top);
  lv_obj_set_pos(body, 0, top);
  lv_obj_set_flex_flow(body, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_all(body, theme::PAD, 0);
  lv_obj_set_style_pad_row(body, theme::GAP, 0);
  _body = body;
  // The new screen emerges from the middle (Anim.h); a screen rebuilt in
  // place (same screen, same title) swaps without motion.
  bool same = _screen == _shown_screen && strncmp(title ? title : "", _shown_title, sizeof(_shown_title) - 1) == 0;
  bool backward = _nav_back || screenDepth(_screen) < screenDepth(_shown_screen);
  _nav_back = false;
  if (!same) prefsSaveSoon(anim::FADE_MS + 60);   // left the screen they were changed on
  if (_shown_screen == SCR_HOME && _screen != SCR_HOME) home::leave();   // arranging the apps ends, saved
  _shown_screen = _screen;
  snprintf(_shown_title, sizeof(_shown_title), "%s", title ? title : "");
  _scr = scr;
  if (_fade_next && prev && !_asleep && prev == lv_screen_active()) {
    // A cross-fade: the new screen dissolves in over the old one, deleted after.
    lv_screen_load_anim(scr, LV_SCR_LOAD_ANIM_FADE_IN, anim::FADE_MS, 0, true);
  } else {
    lv_screen_load(scr);
    if (prev) lv_obj_delete_async(prev);   // this usually runs from one of its own widgets
    if (prev && !same && !_asleep) anim::screenIn(body, backward);
  }
  _fade_next = false;
  return body;
}

// How deep a screen sits below Home, for the slide direction.
int UITask::screenDepth(Screen s) {
  switch (s) {
    case SCR_HOME: return 0;
    case SCR_CHATS: case SCR_NEARBY: case SCR_MAP: case SCR_SETTINGS:
    case SCR_COMPASS: case SCR_CLOCK: case SCR_BOT: case SCR_REPEATER: case SCR_ADMIN_PICK:
    case SCR_DIAG: case SCR_GPS: return 1;
    case SCR_THREAD: case SCR_CONTACTS: case SCR_CHANNEL_EDIT: case SCR_NODE:
    case SCR_SETTINGS_NAV: case SCR_ADMIN: return 2;
    default: return 3;   // pages under a settings page
  }
}

void UITask::back() {
  if (quickPanelOpen()) { quickPanelClose(true); return; }
  _nav_back = true;   // the screen shown from here slides back
  switch (_screen) {
    case SCR_THREAD:   if (_nav_overlay) navClosePopup(); else showChats(); break;
    case SCR_CONTACTS: showChats(); break;
    case SCR_CHANNEL_EDIT: showChats(); break;
    case SCR_BOT:      if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_DIAG:     if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_ADMIN_PICK: showHome(); break;
    case SCR_OTA:      otaLeave(); break;
    case SCR_COMPASS:  showHome(); break;
    case SCR_BATT:     showSchemaSettings(PG_POWER); break;
    case SCR_GPS:      if (_gps_from_settings) showSettings(); else showHome(); break;
    case SCR_ADMIN:    if (_nav_overlay) navClosePopup(); else adminLeave(); break;
    case SCR_SETTINGS: if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_SETTINGS_NAV:   // the map's options go back to the map
      if (_settings_page == settings::PG_NAV && s_opts_from_map) {
        openMap(true);
        if (s_nav_section >= 0) navToolsPopup();   // one section's options: back into Map tools
      } else {
        showSettings();
      }
      break;
    case SCR_QUICK:    // back to Messages & contacts' bottom, where the row is
      if (_nav_overlay) { navClosePopup(); break; }
      showSchemaSettings(settings::PG_MESSAGES);
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_MELODY:   // back to the Sound page's bottom, where the melodies are
      melodySave();
      stopMelody();
      showSchemaSettings(settings::PG_SOUND);
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CLOCK:    showHome(); break;
    case SCR_RADIO:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showSettings();
      break;
    case SCR_REPEATER:
      if (radioPopupOpen()) radioCloseFreq();
      else if (_nav_overlay) navClosePopup();
      else showHome();
      break;
    case SCR_SCOPES:   // back to the Radio screen's bottom, where Scopes is
      if (_nav_overlay) { navClosePopup(); break; }
      showRadio();
      if (_body) { layoutNow(_body); lv_obj_scroll_by(_body, 0, -lv_obj_get_scroll_bottom(_body), LV_ANIM_OFF); }
      break;
    case SCR_CHATS:    if (_nav_overlay) navClosePopup(); else showHome(); break;
    case SCR_NEARBY:
      if (_nav_overlay) navClosePopup();   // the advert popup
      else if (_scan_overlay) closeScanPopup();
      else showHome();
      break;
    case SCR_NODE:     // back to where the node was picked: map, the list, or the scan popup over it
      if (_node_from_map) { openMap(false); break; }
      _screen = SCR_NEARBY;
      buildNearby();
      if (_node_from_scan) showScanPopup();
      break;
    case SCR_MAP:
      if (_nav_overlay) navClosePopup();
      else if (areaSelecting()) areaSelectEnd();
      else if (_dl_overlay) mapDownloadClose();
      else if (_map_nodes || !_map_nav) showNearby();   // opened from Nodes
      else showHome();
      break;
    case SCR_WIFI:     showSettings(); break;
    case SCR_STORAGE:  storeview::stopWalk(); showSettings(); break;
    default:           break;
  }
  _nav_back = false;   // only closed a popup
}

// ── Home ──────────────────────────────────────────────────────────────────────

static void onOpenChats(lv_event_t* e) { (void)e; s_ui->showChats(); }
static void onOpenSettings(lv_event_t* e) { (void)e; s_ui->showSettings(); }

static void onOpenNearby(lv_event_t* e) { (void)e; s_ui->showNearby(); }
static void onOpenNodesMap(lv_event_t* e) { (void)e; s_ui->openMap(false); }
static void onOpenClock(lv_event_t* e);   // ClockScreen.h
static void onOpenBot(lv_event_t* e);     // BotScreen.h
static void onOpenCompass(lv_event_t* e);   // CompassScreen.h
static void onOpenDiag(lv_event_t* e);      // DiagScreen.h
static void onOpenGps(lv_event_t* e);       // GpsScreen.h
static void onOpenGpsFromSettings(lv_event_t* e);
static void onOpenRepeater(lv_event_t* e);  // RepeaterScreen.h
static void onOpenAdminPick(lv_event_t* e); // AdminScreen.h
static void onNodeName(lv_event_t* e);
static void onPowerRow(lv_event_t* e);

// Home itself (pages, clock, minimap, apps) is HomeScreen.h.

// ── Virtual list ──────────────────────────────────────────────────────────────
// A long list (contacts, nodes, conversations) as only the rows it shows: a
// row scrolled out of sight takes the next item coming in. The items are
// kinds with a fixed height -- a row made once (make), its texts set for an
// item (bind) -- or fixed objects of their own (a section title, a note).
// The scroll extent comes from the items' heights, so the scrollbar, a throw
// and coming back to where it was work as with every row built. Built in the
// same time whatever the count; 256 contacts: 522 objects, 950 ms filling
// in and 28 ms a scrolled frame before, 19 objects and 13 ms now.
namespace vlist {
struct Kind {
  int16_t h;
  lv_obj_t* (*make)(lv_obj_t* list);      // a row, its event callbacks on (they ask arg())
  void (*bind)(lv_obj_t* row, intptr_t arg);
};
struct Item { const Kind* k; intptr_t arg; int32_t top; int16_t h; };   // k null: arg is its own object
struct Slot { lv_obj_t* o; const Kind* k; int item; int prev; };
static const int SLOTS = 32;   // a screenful of the shortest kind, a few kinds
struct List {
  lv_obj_t* obj;
  Item* items;
  int n, cap;
  int32_t total;
  bool placed;   // the items' tops are known (not while a new set comes in)
  Slot slots[SLOTS];
  int nslots;
};
static List* s_lists[8];   // a screen's and a popup's, two screens while one slides in

static List* of(lv_obj_t* obj) {
  for (List* l : s_lists) if (l && l->obj == obj) return l;
  return nullptr;
}

static void place(lv_obj_t* o, int32_t y) {
  if (lv_obj_get_style_y(o, LV_PART_MAIN) != y) lv_obj_set_y(o, y);
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN)) lv_obj_remove_flag(o, LV_OBJ_FLAG_HIDDEN);
}

// The rows for what's in view: kept where they show the same item, the rest
// handed over (bound) to the items coming in, made only when none is free.
// all: every one bound again (the items changed).
static void update(List* l, bool all) {
  if (!l->placed) return;
  int32_t sy = lv_obj_get_scroll_y(l->obj), vh = lv_obj_get_content_height(l->obj);
  if (vh <= 0) vh = LV_VER_RES;   // not laid out yet: a screenful
  int first = 0, hi = l->n;
  while (first < hi) {   // the first item reaching into view
    int m = (first + hi) / 2;
    if (l->items[m].top + l->items[m].h <= sy) first = m + 1; else hi = m;
  }
  int last = first;
  while (last < l->n && l->items[last].top < sy + vh) last++;
  for (int s = 0; s < l->nslots; s++) {
    Slot& sl = l->slots[s];
    if (sl.item < 0) continue;
    if (all || sl.item < first || sl.item >= last) { sl.prev = sl.item; sl.item = -1; }
  }
  for (int i = first; i < last; i++) {
    Item& it = l->items[i];
    if (!it.k) continue;
    int got = -1;
    for (int s = 0; s < l->nslots && got < 0; s++) if (l->slots[s].item == i) got = s;
    if (got >= 0) continue;   // shown already
    for (int s = 0; s < l->nslots; s++) {   // a free one of its kind; the one that showed it, if any
      Slot& sl = l->slots[s];
      if (sl.item >= 0 || sl.k != it.k) continue;
      if (got < 0 || sl.prev == i) got = s;
      if (sl.prev == i) break;
    }
    if (got < 0) {
      if (l->nslots == SLOTS) continue;
      got = l->nslots++;
      l->slots[got] = { it.k->make(l->obj), it.k, -1, -1 };
    }
    Slot& sl = l->slots[got];
    sl.item = i;
    it.k->bind(sl.o, it.arg);
    place(sl.o, it.top);
  }
  for (int s = 0; s < l->nslots; s++) {
    Slot& sl = l->slots[s];
    if (sl.item < 0 && !lv_obj_has_flag(sl.o, LV_OBJ_FLAG_HIDDEN)) lv_obj_add_flag(sl.o, LV_OBJ_FLAG_HIDDEN);
  }
}

static void onEvent(lv_event_t* e) {
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
  List* l = of(obj);
  if (!l) return;
  switch (lv_event_get_code(e)) {
    case LV_EVENT_SCROLL: case LV_EVENT_SIZE_CHANGED: update(l, false); break;
    case LV_EVENT_GET_SELF_SIZE: {
      lv_point_t* p = (lv_point_t*)lv_event_get_param(e);
      if (p->y < l->total) p->y = l->total;
      break;
    }
    case LV_EVENT_DELETE:
      for (List*& x : s_lists) if (x == l) x = nullptr;
      lv_free(l->items);
      lv_free(l);
      break;
    default: break;
  }
}

// `obj` (a scrolling column: a screen body, a scrollList) becomes a virtual
// list; its children so far are taken away.
static void attach(lv_obj_t* obj) {
  List* l = nullptr;
  for (List*& x : s_lists) if (!x) { x = l = (List*)lv_malloc_zeroed(sizeof(List)); break; }
  if (!l) return;   // can't be: then a plain empty column
  l->obj = obj;
  lv_obj_clean(obj);
  lv_obj_set_layout(obj, LV_LAYOUT_NONE);
  for (lv_event_code_t c : { LV_EVENT_SCROLL, LV_EVENT_SIZE_CHANGED, LV_EVENT_GET_SELF_SIZE, LV_EVENT_DELETE })
    lv_obj_add_event_cb(obj, onEvent, c, NULL);
}

// A new set of items: add() / own() them, then end(). The rows made stay
// (rebound); objects of their own given to own() before are deleted.
static void begin(lv_obj_t* obj) {
  List* l = of(obj);
  if (!l) return;
  for (int i = 0; i < l->n; i++) if (!l->items[i].k) lv_obj_delete((lv_obj_t*)l->items[i].arg);
  l->n = 0;
  l->placed = false;
}
static void push(List* l, const Kind* k, intptr_t arg, int16_t h) {
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 64;
    l->items = (Item*)lv_realloc(l->items, l->cap * sizeof(Item));
  }
  l->items[l->n++] = { k, arg, 0, h };
}
static void add(lv_obj_t* obj, const Kind* k, intptr_t arg) { if (List* l = of(obj)) push(l, k, arg, k->h); }
// An object of its own (made on the list, its height known after a layout).
static void own(lv_obj_t* obj, lv_obj_t* o) { if (List* l = of(obj)) push(l, nullptr, (intptr_t)o, 0); }

static void end(lv_obj_t* obj) {
  List* l = of(obj);
  if (!l) return;
  bool mine = false;
  for (int i = 0; i < l->n && !mine; i++) mine = !l->items[i].k;
  if (mine) lv_obj_update_layout(obj);   // their heights
  int32_t gap = lv_obj_get_style_pad_row(obj, LV_PART_MAIN), y = 0;
  for (int i = 0; i < l->n; i++) {
    Item& it = l->items[i];
    if (!it.k) {
      lv_obj_t* o = (lv_obj_t*)it.arg;
      it.h = (int16_t)lv_obj_get_height(o);
      place(o, y);
    }
    it.top = y;
    y += it.h + gap;
  }
  l->total = l->n ? y - gap : 0;
  l->placed = true;
  lv_obj_refresh_self_size(obj);
  update(l, true);
  lv_obj_readjust_scroll(obj, LV_ANIM_OFF);   // fewer items: not scrolled past the end
}

// The item a row's event is for (its arg).
static intptr_t arg(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  List* l = of(lv_obj_get_parent(o));
  if (l)
    for (int s = 0; s < l->nslots; s++)
      if (l->slots[s].o == o && l->slots[s].item >= 0) return l->items[l->slots[s].item].arg;
  return -1;
}

// A row held down: what it shows stays until it's let go.
static bool pressed(lv_obj_t* obj) {
  List* l = of(obj);
  for (int s = 0; l && s < l->nslots; s++)
    if (lv_obj_has_state(l->slots[s].o, LV_STATE_PRESSED)) return true;
  return false;
}
}  // namespace vlist

// ── Conversation list ─────────────────────────────────────────────────────────

static void onOpenChannel(lv_event_t* e) {
  intptr_t i = vlist::arg(e);
  if (i >= 0) s_ui->openChannel((uint8_t)i);
}

// DM rows carry a 4-byte prefix; kept in a static table the rows point into.
static uint8_t s_dm_rows[MessageHistory::DM_HIST_MAX][4];
static const int CONTACT_ROWS_MAX = 256;   // "All" with a full contact table stays usable
static uint8_t (*s_contact_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(CONTACT_ROWS_MAX);
static uint16_t* s_contact_raw = psramBuf<uint16_t>(CONTACT_ROWS_MAX);   // their raw table index, for the name

static uint16_t s_dm_pos[MessageHistory::DM_HIST_MAX];   // its newest message in the DM ring
static bool s_dm_known[MessageHistory::DM_HIST_MAX];      // a contact (has options on hold)
static void onOpenDMRow(lv_event_t* e) {
  intptr_t r = vlist::arg(e);
  if (r >= 0) s_ui->openDM(s_dm_rows[r]);
}
static void onOpenContactRow(lv_event_t* e) {
  intptr_t i = vlist::arg(e);
  if (i >= 0) s_ui->openDM(s_contact_rows[i]);
}
static void onNewChat(lv_event_t* e) { (void)e; s_ui->showContacts(); }
static void onChanRowHold(lv_event_t* e);      // ChannelScreen.h
static void onChanAdd(lv_event_t* e);
static void onChanThreadMenu(lv_event_t* e);
static const int ROOM_ROWS_MAX = 32;
static uint8_t (*s_room_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(ROOM_ROWS_MAX);
static uint16_t s_room_raw[ROOM_ROWS_MAX];   // their raw table index, for the row
static bool s_chats_read_all = false;        // Messages built with "Read all" in its header
static void onDMRowHold(lv_event_t* e);        // ConversationScreen.h
static void onRoomRow(lv_event_t* e);
static void onRoomRowHold(lv_event_t* e);
static void onConvThreadMenu(lv_event_t* e);
static void onMsgHold(lv_event_t* e);
static void onChatFilter(lv_event_t* e);
enum : uint8_t { CF_CHANNELS, CF_ROOMS, CF_CONTACTS };   // favourites-only filters

// One tappable list row: title, optional muted subtitle, optional badge.
// In a group(): a flat row, the subtitle wrapping, a chevron on the right.
static lv_obj_t* listRow(lv_obj_t* parent, const char* title, const char* sub,
                         lv_event_cb_t cb, void* user) {
  if (isGroup(parent)) {
    lv_obj_t* row = groupLine(parent, true);
    groupText(row, title, sub);
    label(row, LV_SYMBOL_RIGHT, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
    return row;
  }
  lv_obj_t* row = lv_button_create(parent);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
  lv_obj_t* t = label(row, title, THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 230);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, theme::PAD, sub ? 5 : 13);
  lv_obj_set_user_data(row, t);
  if (sub) {
    lv_obj_t* s = label(row, sub, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s, 230, 15);   // fixed height: LONG_DOT cuts instead of wrapping over the title
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -5);
  }
  return row;
}

// A listRow() for a vlist: the subtitle always there (shown or not), the
// right side free for a value or a badge; texts set by rowSet().
static lv_obj_t* vRow(lv_obj_t* list, lv_event_cb_t cb, lv_event_cb_t hold) {
  lv_obj_t* row = listRow(list, "", "", cb, NULL);
  if (hold) lv_obj_add_event_cb(row, hold, LV_EVENT_LONG_PRESSED, NULL);
  return row;
}
static void rowSet(lv_obj_t* row, const char* title, const char* sub, uint32_t title_col = theme::TEXT) {
  lv_obj_t* t = rowTitle(row);
  lv_obj_t* s = lv_obj_get_child(row, 1);
  setText(t, title);
  setTextColor(t, title_col);
  int32_t y = sub ? 5 : 13;   // as listRow() puts it
  if (lv_obj_get_style_y(t, LV_PART_MAIN) != y) lv_obj_set_y(t, y);
  if (sub) setText(s, sub);
  if (!sub != lv_obj_has_flag(s, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(s, LV_OBJ_FLAG_HIDDEN, !sub);
}

// The same with a value on the right (a distance, a signal), set by rowValue().
static lv_obj_t* vRowValue(lv_obj_t* list, lv_event_cb_t cb) {
  lv_obj_t* row = vRow(list, cb, NULL);
  lv_obj_align(label(row, "", THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  return row;
}
static void rowValue(lv_obj_t* row, const char* text, uint32_t col) {
  lv_obj_t* r = lv_obj_get_child(row, 2);
  setText(r, text);
  setTextColor(r, col);
}

static lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_pad_top(l, 4, 0);
  return l;
}

// Messages section title: a chevron and the title fold the section (tap),
// an "All" / "★ Fav" pill on the right (filter >= 0) flips its
// favourites-only filter. Folded, the title carries the unread count.
enum : uint8_t { FOLD_CHANNELS, FOLD_DIRECT, FOLD_ROOMS };
static int s_chat_fold = -1;   // bit per FOLD_*; -1: not read from NVS yet
static void onChatFold(lv_event_t* e) { s_ui->chatFold((int)(uintptr_t)lv_event_get_user_data(e)); }
static bool chatFolded(int which) {
  if (s_chat_fold < 0) s_chat_fold = lvport::loadChatFold();
  return s_chat_fold & (1 << which);
}

static void chatSection(lv_obj_t* parent, const char* text, int fold, int unread, int filter, bool fav_only) {
  bool folded = chatFolded(fold);
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), 30);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(row, LV_OPA_60, LV_STATE_PRESSED);
  lv_obj_add_event_cb(row, onChatFold, LV_EVENT_CLICKED, (void*)(uintptr_t)fold);
  lv_obj_t* ch = label(row, folded ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(ch, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_t* t = label(row, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 22, 0);
  if (folded && unread > 0) {   // what's waiting inside
    lv_obj_t* n = label(row, "", THEME_FONT_SMALL, theme::ACCENT);
    lv_label_set_text_fmt(n, "%d new", unread);
    lv_obj_align_to(n, t, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  }
  if (filter < 0 || folded) return;
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 24);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(fav_only ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(b, onChatFilter, LV_EVENT_CLICKED, (void*)(uintptr_t)filter);
  lv_obj_center(label(b, fav_only ? UI_SYMBOL_STAR " Fav" : "All", THEME_FONT_SMALL, theme::TEXT));
}

// Section title with an "All" / "★ Fav" pill on the right that flips the
// section's favourites-only filter.
static void sectionWithFilter(lv_obj_t* parent, const char* text, bool fav_only, uint8_t which) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), 26);
  lv_obj_align(label(row, text, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_BOTTOM_LEFT, 0, -2);
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 24);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(fav_only ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -2, 0);
  lv_obj_add_event_cb(b, onChatFilter, LV_EVENT_CLICKED, (void*)(uintptr_t)which);
  lv_obj_center(label(b, fav_only ? UI_SYMBOL_STAR " Fav" : "All", THEME_FONT_SMALL, theme::TEXT));
}

void UITask::showChats() {
  _screen = SCR_CHATS;
  buildChats();
}

static void onMarkAllRead(lv_event_t* e) { (void)e; s_ui->markAllRead(); }
static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out);

void UITask::buildChats() {
  lv_obj_t* body = newScreen("Messages", true);
  s_chats_read_all = unreadTotal() > 0;
  if (_header) {   // a new message first, whatever the lists below hold
    lv_obj_t* nb = headerButton(_header, LV_SYMBOL_EDIT " New", onNewChat, 4, NULL);
    stylePrimary(nb);
    if (s_chats_read_all) {
      lv_obj_t* rb = headerButton(_header, LV_SYMBOL_OK " Read all", onMarkAllRead, 0, NULL);
      layoutNow(_header);
      lv_obj_align_to(rb, nb, LV_ALIGN_OUT_LEFT_MID, -6, 0);
    }
  }
  vlist::attach(body);
  fillChats();
}

// Messages again where it is (a new message, a section folded, back from
// the lock): the rows in view rewritten, the rest as they scroll in. Built
// anew only when "Read all" comes or goes.
void UITask::refreshChats() {
  if (_screen != SCR_CHATS) { s_chats_stale = false; return; }
  if (!_body || !vlist::of(_body) || (unreadTotal() > 0) != s_chats_read_all) {
    int y = _body ? lv_obj_get_scroll_y(_body) : 0;
    buildChats();
    layoutNow(_body);
    lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF);
    return;
  }
  s_chats_stale = vlist::pressed(_body);   // held: once it's let go
  if (!s_chats_stale) fillChats();
}

void UITask::fillChats() {
  lv_obj_t* body = _body;
  vlist::begin(body);
  auto own = [body]() { vlist::own(body, lv_obj_get_child(body, -1)); };

  // Channels: favourites first (unless turned off), hold a row for its options
  static const vlist::Kind CHANNEL = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenChannel, onChanRowHold); },
    [](lv_obj_t* row, intptr_t i) {
      UITask* ui = s_ui;
      ChannelDetails ch;
      if (!the_mesh.getChannel((int)i, ch)) ch.name[0] = '\0';
      char title[48], sub[64] = "";
      snprintf(title, sizeof(title), "%s%s%s", chanctl::favourite(ui->_prefs, (int)i) ? UI_SYMBOL_STAR " " : "", ch.name,
               chanctl::notif(ui->_prefs, (int)i) == chanctl::NOTIF_MUTED ? "  " UI_SYMBOL_MUTE : "");
      if (ui->_core->history.histCountForChannel((int)i) > 0) {
        const ChHistEntry& e = ui->_core->history.chAtPos(ui->_core->history.histEntryForChannel((int)i, 0));
        snprintf(sub, sizeof(sub), "%s", e.text);
        plainMentions(sub);
      }
      rowSet(row, title, sub[0] ? sub : NULL);
      rowBadge(row, ui->_core->history.chUnread((int)i), ui->_core->history.chUnreadOverflow((int)i));
    },
  };
  bool ch_fav_only = _prefs && _prefs->ch_fav_only;
  chatSection(body, "CHANNELS", FOLD_CHANNELS, _core->history.getTotalChannelUnread(), CF_CHANNELS, ch_fav_only);
  own();
  bool fav_first = !_prefs || _prefs->fav_sort;
  int ch_rows = 0;
  for (int pass = fav_first ? 0 : 1; pass < 2 && !chatFolded(FOLD_CHANNELS); pass++) {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      bool fav = chanctl::favourite(_prefs, i);
      if (ch_fav_only && !fav) continue;
      if (fav_first && fav != (pass == 0)) continue;
      ch_rows++;
      vlist::add(body, &CHANNEL, i);
    }
  }
  if (!chatFolded(FOLD_CHANNELS)) {
    if (ch_rows == 0 && ch_fav_only) { label(body, "No favourite channels", THEME_FONT_SMALL, theme::TEXT_MUTED); own(); }
    lv_obj_t* add_ch = lv_button_create(body);
    lv_obj_set_size(add_ch, LV_PCT(100), 32);
    lv_obj_set_style_bg_color(add_ch, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_color(add_ch, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_set_style_border_width(add_ch, 1, 0);
    lv_obj_set_style_radius(add_ch, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(add_ch, 0, 0);
    lv_obj_add_event_cb(add_ch, onChanAdd, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(add_ch, LV_SYMBOL_PLUS "  Add channel", THEME_FONT_SMALL, theme::TEXT_MUTED));
    vlist::own(body, add_ch);
  }

  // Recent direct conversations (DM ring, newest first, one row per contact)
  static const vlist::Kind DM = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenDMRow, onDMRowHold); },
    [](lv_obj_t* row, intptr_t r) {
      UITask* ui = s_ui;
      const DmHistEntry& e = ui->_core->history.dmAtPos(s_dm_pos[r]);
      ContactInfo c;
      bool known = s_dm_known[r] && MessageHistory::contactByPrefix(s_dm_rows[r], c);
      char name[48];
      contactName(s_dm_rows[r], name, sizeof(name));
      if (known && contactctl::favourite(c)) { char t[48]; snprintf(t, sizeof(t), UI_SYMBOL_STAR " %s", name); strcpy(name, t); }
      if (known && contactctl::notif(ui->_prefs, c.id.pub_key) == contactctl::NOTIF_MUTED) strncat(name, "  " UI_SYMBOL_MUTE, sizeof(name) - strlen(name) - 1);
      char sub[64];
      snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
      plainMentions(sub);
      rowSet(row, name, sub);
      rowBadge(row, ui->_core->dmUnread(s_dm_rows[r]), ui->_core->dmUnreadOverflow(s_dm_rows[r]));
    },
  };
  chatSection(body, "DIRECT", FOLD_DIRECT, _core->dmUnreadTotal(), -1, false);
  own();
  int rows = 0;
  for (int j = 0; j < _core->history.dmHistCount() && rows < MessageHistory::DM_HIST_MAX && !chatFolded(FOLD_DIRECT); j++) {
    int pos = _core->history.dmHistPosNewest(j);
    const DmHistEntry& e = _core->history.dmAtPos(pos);
    bool seen = false;
    for (int r = 0; r < rows; r++) if (memcmp(s_dm_rows[r], e.prefix, 4) == 0) { seen = true; break; }
    if (seen) continue;
    ContactInfo c;
    bool known = MessageHistory::contactByPrefix(e.prefix, c);
    if (known && c.type == ADV_TYPE_ROOM) continue;   // rooms have their own section
    memcpy(s_dm_rows[rows], e.prefix, 4);
    s_dm_pos[rows] = (uint16_t)pos;
    s_dm_known[rows] = known;
    vlist::add(body, &DM, rows);
    rows++;
  }
  if (rows == 0 && !chatFolded(FOLD_DIRECT)) { label(body, "No conversations yet - New, top right", THEME_FONT_SMALL, theme::TEXT_MUTED); own(); }

  // Room servers: tap logs in (saved password or ask) and opens; hold: options
  static const vlist::Kind ROOM = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onRoomRow, onRoomRowHold); },
    [](lv_obj_t* row, intptr_t r) {
      UITask* ui = s_ui;
      ContactInfo c;
      if (!the_mesh.getContactByIdx(s_room_raw[r], c) || memcmp(c.id.pub_key, s_room_rows[r], PUB_KEY_SIZE) != 0) {
        rowSet(row, "?", NULL);   // deleted meanwhile
        return;
      }
      char title[48], sub[64];
      snprintf(title, sizeof(title), "%s%s", contactctl::favourite(c) ? UI_SYMBOL_STAR " " : "", c.name);
      if (ui->_core->history.dmHistCountForContact(c.id.pub_key) > 0) {
        const DmHistEntry& e = ui->_core->history.dmAtPos(ui->_core->history.dmHistEntryForContact(c.id.pub_key, 0));
        snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
        plainMentions(sub);
      } else {
        snprintf(sub, sizeof(sub), "%s", ui->_core->rooms.isLoggedIn(c.id.pub_key) ? "Logged in" : "Tap to log in");
      }
      rowSet(row, title, sub);
    },
  };
  bool room_fav_only = _prefs && _prefs->room_fav_only;
  chatSection(body, "ROOMS", FOLD_ROOMS, _core->roomUnread(), CF_ROOMS, room_fav_only);
  own();
  int nrooms = 0, total = the_mesh.getNumContacts();
  for (int pass = fav_first ? 0 : 1; pass < 2 && !chatFolded(FOLD_ROOMS); pass++) {
    for (int i = 0; i < total && nrooms < ROOM_ROWS_MAX; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_ROOM) continue;
      bool fav = contactctl::favourite(c);
      if (room_fav_only && !fav) continue;
      if (fav_first && fav != (pass == 0)) continue;
      memcpy(s_room_rows[nrooms], c.id.pub_key, PUB_KEY_SIZE);
      s_room_raw[nrooms] = (uint16_t)(MAX_ANON_CONTACTS + i);
      vlist::add(body, &ROOM, nrooms);
      nrooms++;
    }
  }
  if (nrooms == 0 && !chatFolded(FOLD_ROOMS)) {
    label(body, room_fav_only ? "No favourite rooms" : "No room servers known", THEME_FONT_SMALL, theme::TEXT_MUTED);
    own();
  }
  vlist::end(body);
}

// A section title tapped: folded / unfolded (kept in NVS), the list redrawn
// where it was.
void UITask::chatFold(int which) {
  chatFolded(which);   // loaded
  s_chat_fold ^= 1 << which;
  lvport::saveChatFold(s_chat_fold);
  refreshChats();
}

// ── Contact picker (start a DM) ───────────────────────────────────────────────

void UITask::showContacts() {
  _screen = SCR_CONTACTS;
  buildContacts();
}

void UITask::buildContacts() {
  lv_obj_t* body = newScreen("New message", true);
  bool fav_only = _prefs && !_prefs->dm_show_all;   // ui-new's default: favourites only
  vlist::attach(body);
  vlist::begin(body);
  sectionWithFilter(body, "CONTACTS", fav_only, CF_CONTACTS);
  vlist::own(body, lv_obj_get_child(body, -1));
  int total = the_mesh.getNumContacts();
  int rows = 0;
  // +MAX_ANON_CONTACTS: getContactByIdx() takes the raw table index (see
  // MessageHistory::contactByPrefix()).
  for (int i = 0; i < total && rows < CONTACT_ROWS_MAX; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT) continue;
    if (fav_only && !contactctl::favourite(c)) continue;
    memcpy(s_contact_rows[rows], c.id.pub_key, PUB_KEY_SIZE);
    s_contact_raw[rows] = (uint16_t)(MAX_ANON_CONTACTS + i);
    rows++;
  }
#ifdef PERF_LIST_MUL
  for (int k = 0, real = rows; real && rows < CONTACT_ROWS_MAX; k++, rows++) {   // a long list from the few there are
    memcpy(s_contact_rows[rows], s_contact_rows[k % real], PUB_KEY_SIZE);
    s_contact_raw[rows] = s_contact_raw[k % real];
  }
#endif
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenContactRow, NULL); },
    [](lv_obj_t* row, intptr_t i) {
      ContactInfo c;
      bool same = the_mesh.getContactByIdx(s_contact_raw[i], c) && memcmp(c.id.pub_key, s_contact_rows[i], PUB_KEY_SIZE) == 0;
      rowSet(row, same ? c.name : "?", NULL);   // "?": deleted meanwhile
    },
  };
  for (int i = 0; i < rows; i++) vlist::add(body, &ROW, i);
  if (rows == 0) vlist::own(body, label(body, fav_only ? "No favourites - tap All" : "No contacts yet", THEME_FONT_BODY, theme::TEXT_MUTED));
  vlist::end(body);
}

// ── Nearby ────────────────────────────────────────────────────────────────────
// Contacts / live shares / heard adverts from NearbyModel, filtered by type
// chips, sorted by distance or recency. Tap a row for detail. Scan (nodes that
// answer a discover request right now) is a popup over the list with its own
// model, since it is a different set: who is in range, not who is known.

static NearbyModel::Entry s_node;   // the node open in SCR_NODE (a copy: the list re-sorts)
// Its info card's values (UITask::_node_info is the card).
static lv_obj_t *s_nd_type, *s_nd_status, *s_nd_dist, *s_nd_pos, *s_nd_heard, *s_nd_signal, *s_nd_id;
enum : uint8_t { NODE_MSG, NODE_PING, NODE_FAV, NODE_ADD, NODE_DELETE, NODE_NAV, NODE_ADMIN, NODE_WAYPOINT, NODE_PIN };

static void onNearbyChip(lv_event_t* e) { s_ui->setNearbyFilter((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onNearbySort(lv_event_t* e) { (void)e; s_ui->toggleNearbySort(); }
static void onNearbyScan(lv_event_t* e) { (void)e; s_ui->startNearbyScan(); }
static void onNearbyRow(lv_event_t* e)  { s_ui->openNode((int)vlist::arg(e)); }
static void onScanRow(lv_event_t* e)    { s_ui->openScanNode((int)vlist::arg(e)); }
static void onScanClose(lv_event_t* e)  { (void)e; s_ui->closeScanPopup(); }
static void onNodeAction(lv_event_t* e) { s_ui->nodeAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out) {
  lv_obj_t* b = lv_button_create(hdr);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_align(b, LV_ALIGN_RIGHT_MID, -right, 0);
  lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, NULL);
  lv_obj_t* l = label(b, text, THEME_FONT_SMALL, theme::TEXT);
  lv_obj_center(l);
  if (label_out) *label_out = l;
  return b;
}

void UITask::showNearby() {
  _screen = SCR_NEARBY;
  _scanning = false;
  buildNearby();   // filter / sort persist
}

void UITask::buildNearby() {
  lv_obj_t* body = newScreen("Nodes", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  if (_header) {
    headerButton(_header, LV_SYMBOL_REFRESH, onNearbyScan, 4, NULL);   // scan
    lv_obj_set_width(headerButton(_header, "", onNearbySort, 46, &_nearby_sort_lbl), 62);   // fits "Recent" / "Dist"
    headerButton(_header, UI_SYMBOL_MAP, onOpenNodesMap, 114, NULL);   // the Nodes map (the advert: the quick panel)
  }

  // Type filter chips
  _nearby_chips = lv_obj_create(body);
  styleSurface(_nearby_chips, theme::BG);
  lv_obj_remove_flag(_nearby_chips, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(_nearby_chips, LV_PCT(100), 26);
  lv_obj_set_flex_flow(_nearby_chips, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(_nearby_chips, 4, 0);
  for (uint8_t f = 0; f < NearbyModel::F_COUNT; f++) {
    lv_obj_t* c = lv_button_create(_nearby_chips);
    lv_obj_set_height(c, 26);
    lv_obj_set_flex_grow(c, 1);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_shadow_width(c, 0, 0);
    bool on = f == _nearby->filter();
    lv_obj_set_style_bg_color(c, lv_color_hex(on ? theme::ACCENT_DIM : theme::SURFACE), 0);   // selected (Theme.h)
    lv_obj_add_event_cb(c, onNearbyChip, LV_EVENT_CLICKED, (void*)(uintptr_t)f);
    lv_obj_center(label(c, NearbyModel::filterLabel(f), THEME_FONT_SMALL, theme::TEXT));
  }

  _nearby_status = label(body, "", THEME_FONT_SMALL, theme::TEXT_MUTED);

  _nearby_list = scrollList(body);
  vlist::attach(_nearby_list);

  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::setNearbyFilter(uint8_t f) {
  _nearby->setFilter(f);
  buildNearby();
}

void UITask::toggleNearbySort() {
  _nearby->setSortMode(_nearby->sortMode() == NearbyModel::SORT_DIST ? NearbyModel::SORT_TIME
                                                                     : NearbyModel::SORT_DIST);
  _nearby_sig = 0;
  refreshNearbyList();
}

void UITask::startNearbyScan() {
  _scanning = true;
  _scan_until_ms = millis() + 8000;
  _next_nearby_ms = millis() + 250;
  the_mesh.sendNodeDiscoverReq();
  if (!_scan_overlay) showScanPopup();
  else { _scan_sig = 0; refreshScanPopup(); }
}

// Dimmed full-screen overlay (swallows taps) holding a panel with the results.
// A child of the current screen, so it goes away with it.
void UITask::showScanPopup() {
  lv_obj_t* panel = popupOpen(screen(), POP_FULL, _scan_overlay);

  lv_obj_t* hdr = lv_obj_create(panel);
  styleSurface(hdr, theme::BG);
  lv_obj_remove_flag(hdr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(hdr, LV_PCT(100), 28);
  lv_obj_align(label(hdr, "In range now", THEME_FONT_TITLE, theme::TEXT), LV_ALIGN_LEFT_MID, 0, 0);
  headerButton(hdr, LV_SYMBOL_CLOSE, onScanClose, 0, NULL);
  headerButton(hdr, LV_SYMBOL_REFRESH " Again", onNearbyScan, 44, NULL);

  _scan_status = label(panel, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  _scan_list = scrollList(panel);
  vlist::attach(_scan_list);

  _scan_sig = 0;
  refreshScanPopup();
}

void UITask::closeScanPopup() {
  if (_scan_overlay) lv_obj_delete_async(_scan_overlay);   // may be closing from its own button
  _scan_overlay = _scan_list = _scan_status = nullptr;
  _scanning = false;
}

void UITask::refreshScanPopup() {
  if (!_scan_list) return;
  _scan->refreshScan();
  int n = _scan->count();
  if (_scanning) setTextFmt(_scan_status, LV_SYMBOL_REFRESH "  Listening for replies... %d", n);
  else setTextFmt(_scan_status, "%d node%s answered the discover request", n, n == 1 ? "" : "s");

  uint32_t sig = (uint32_t)n + (_scanning ? 0x10000u : 0);
  for (int i = 0; i < n; i++) {
    const NearbyModel::Entry& e = _scan->at(i);
    sig = sig * 31 + e.rssi * 7 + e.snr_x4 + e.is_known;
    for (int k = 0; k < 4; k++) sig = sig * 31 + e.pub_key[k];
  }
  if (sig == _scan_sig || vlist::pressed(_scan_list)) return;   // held: once it's let go
  _scan_sig = sig;

  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRowValue(list, onScanRow); },
    [](lv_obj_t* row, intptr_t i) {
      const NearbyModel::Entry& e = s_ui->_scan->at((int)i);
      char title[40], sub[48], right[12];
      if (e.name[0]) snprintf(title, sizeof(title), "%s", e.name);
      else snprintf(title, sizeof(title), "%s %02X%02X%02X%02X", NearbyModel::typeName(e.type),
                    e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
      snprintf(sub, sizeof(sub), "%s  -  SNR %.1f / %.1f%s", NearbyModel::typeName(e.type),
               e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f, e.is_known ? "" : "  -  new");
      snprintf(right, sizeof(right), "%d dBm", e.rssi);
      rowSet(row, title, sub);
      rowValue(row, right, theme::TEXT_MUTED);
    },
  };
  vlist::begin(_scan_list);
  for (int i = 0; i < n; i++) vlist::add(_scan_list, &ROW, i);
  if (n == 0) {
    lv_obj_t* l = noteLabel(_scan_list, _scanning ? "Repeaters and rooms in range will answer."
                            : "Nobody answered. Try again later or move.",
                            THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(l, 8, 0);
    vlist::own(_scan_list, l);
  }
  vlist::end(_scan_list);
}

void UITask::openScanNode(int row) {
  if (row < 0 || row >= _scan->count()) return;
  s_node = _scan->at(row);
  _node_from_scan = true;
  _node_from_map = false;
  _scanning = false;
  _pinging = false;
  _screen = SCR_NODE;
  buildNode();
}

// Changes whenever a rebuild would show something different.
uint32_t UITask::nearbySignature() const {
  uint32_t sig = (uint32_t)_nearby->count() * 2654435761u + _nearby->sortMode() + (_scanning ? 7 : 0);
  for (int i = 0; i < _nearby->count(); i++) {
    const NearbyModel::Entry& e = _nearby->at(i);
    sig = sig * 31 + e.contact_idx + (uint32_t)e.lastmod + (uint32_t)(e.dist_km * 100) + e.rssi + e.fav;
    for (const char* p = e.name; *p; p++) sig = sig * 31 + (uint8_t)*p;
  }
  // Ages are shown in minutes, so let the list re-render once a minute anyway.
  return sig + rtc_clock.getCurrentTime() / 60;
}

// A Nearby row's texts: the name (a star on a favourite), what it is and how
// long ago it was heard, and on the right its distance (or the age).
struct NearbyText { char title[48], sub[48], right[16]; uint32_t right_col; };
static void nearbyText(const NearbyModel::Entry& e, uint32_t now, bool imperial, NearbyText& t) {
  const char* name = e.name[0] ? e.name : "(unknown)";
  snprintf(t.title, sizeof(t.title), "%s%s", e.fav ? UI_SYMBOL_STAR " " : "", name);
  char age[8];
  geo::fmtAgeShort(age, sizeof(age), now, e.lastmod);
  t.right[0] = 0;
  if (e.dist_km >= 0.0f) geo::fmtDist(t.right, sizeof(t.right), e.dist_km, imperial);
  else if (age[0]) snprintf(t.right, sizeof(t.right), "%s", age);
  const char* kind = e.contact_idx >= 0 ? NearbyModel::typeName(e.type) : "not a contact";
  snprintf(t.sub, sizeof(t.sub), "%s%s%s%s", kind, e.is_live ? "  -  live" : "",
           (e.dist_km >= 0.0f && age[0]) ? "  -  " : "", (e.dist_km >= 0.0f && age[0]) ? age : "");
  t.right_col = e.is_live ? theme::OK : theme::TEXT_MUTED;
}

// A small arrow towards the node, drawn in the value label's left padding:
// its user data is the bearing + 1 (0: none).
static const int ROW_ARROW_W = 14;
static void onRowArrowDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  const int b = (int)(intptr_t)lv_obj_get_user_data(o) - 1;
  if (b < 0) return;
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const float cx = a.x1 + ROW_ARROW_W / 2 - 1, cy = (a.y1 + a.y2) / 2.0f;
  const float ang = b * (float)M_PI / 180.0f, sx = sinf(ang), sy = -cosf(ang);
  lv_draw_triangle_dsc_t tri;
  lv_draw_triangle_dsc_init(&tri);
  tri.color = lv_obj_get_style_text_color(o, LV_PART_MAIN);
  tri.opa = LV_OPA_COVER;
  tri.p[0].x = cx + sx * 6;           tri.p[0].y = cy + sy * 6;                 // the tip
  tri.p[1].x = cx - sx * 5 + sy * 4;  tri.p[1].y = cy - sy * 5 - sx * 4;
  tri.p[2].x = cx - sx * 5 - sy * 4;  tri.p[2].y = cy - sy * 5 + sx * 4;
  lv_draw_triangle(lv_event_get_layer(e), &tri);
}

void UITask::refreshNearbyList() {
  if (!_nearby_list) return;
  _nearby->refreshModel();
  uint32_t sig = nearbySignature();
  int n = _nearby->count();

  if (_nearby_sort_lbl)
    setText(_nearby_sort_lbl, _nearby->sortMode() == NearbyModel::SORT_TIME ? "Recent" : "Dist");
  int32_t lat, lon;
  bool gps = _nearby->ownPosition(lat, lon);
  setTextFmt(_nearby_status, "%d node%s%s", n, n == 1 ? "" : "s",
                        gps ? "" : "  -  no GPS fix, distances unknown");
  if (sig == _nearby_sig || vlist::pressed(_nearby_list)) return;   // held: once it's let go
  _nearby_sig = sig;

  // The rows in view rewritten from the model (ages, distances, a new
  // order); the list stays where it was scrolled.
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) {
      lv_obj_t* row = vRowValue(list, onNearbyRow);
      lv_obj_add_event_cb(lv_obj_get_child(row, 2), onRowArrowDraw, LV_EVENT_DRAW_MAIN_END, NULL);
      return row;
    },
    [](lv_obj_t* row, intptr_t i) {
      const NearbyModel::Entry& e = s_ui->_nearby->at((int)i);
      NearbyText t;
      nearbyText(e, rtc_clock.getCurrentTime(), s_ui->_prefs && s_ui->_prefs->units_imperial, t);
      rowSet(row, t.title, t.sub, e.fav ? theme::ACCENT : theme::TEXT);
      rowValue(row, t.right, t.right_col);
      // By the distance, an arrow towards it
      int b = -1;
      int32_t lat, lon;
      if (e.dist_km >= 0.0f && s_ui->_nearby->ownPosition(lat, lon)) b = geo::bearingDeg(lat, lon, e.lat_e6, e.lon_e6);
      lv_obj_t* v = lv_obj_get_child(row, 2);
      if ((intptr_t)lv_obj_get_user_data(v) != b + 1) {
        lv_obj_set_user_data(v, (void*)(intptr_t)(b + 1));
        lv_obj_set_style_pad_left(v, b >= 0 ? ROW_ARROW_W : 0, 0);
        lv_obj_invalidate(v);
      }
    },
  };
  vlist::begin(_nearby_list);
  for (int i = 0; i < n; i++) vlist::add(_nearby_list, &ROW, i);
  if (n == 0) {
    lv_obj_t* l = label(_nearby_list, "Nobody here yet. Tap Scan to look around.",
                        THEME_FONT_BODY, theme::TEXT_MUTED);
    lv_obj_set_style_pad_top(l, 12, 0);
    vlist::own(_nearby_list, l);
  }
  vlist::end(_nearby_list);
}

// ── Node detail ───────────────────────────────────────────────────────────────

void UITask::openNode(int row) {
  if (row < 0 || row >= _nearby->count()) return;
  s_node = _nearby->at(row);
  _node_from_scan = false;
  _node_from_map = false;
  _pinging = false;
  _screen = SCR_NODE;
  buildNode();
}

static lv_obj_t* s_nd_fav = nullptr;
static void nodeFavText() {
  if (s_nd_fav) lv_label_set_text(s_nd_fav, s_node.fav ? UI_SYMBOL_STAR "  Remove from favourites" : UI_SYMBOL_STAR "  Add to favourites");
}

void UITask::buildNode() {
  const NearbyModel::Entry& e = s_node;
  lv_obj_t* body = newScreen(e.name[0] ? e.name : "(unknown)", true);

  lv_obj_t* info = scrollList(body);   // the info, scrolling above the actions at the bottom
  s_nd_fav = nullptr;
  nodeVizBuild(info);
  nodeviz::s_bearing = nodeviz::s_hops = -2;   // nothing drawn yet: the first set fills both
  _node_info = infoCard(info);
  s_nd_type = infoRow(_node_info, "Type", "");
  s_nd_status = infoRow(_node_info, "Status", "");
  s_nd_dist = infoRow(_node_info, "Distance", "");
  s_nd_pos = infoRow(_node_info, "Position", "");
  s_nd_heard = infoRow(_node_info, "Heard", "");
  s_nd_signal = infoRow(_node_info, "Signal", "");
  s_nd_id = infoRow(_node_info, "ID", "");
  _node_ping = label(info, "", THEME_FONT_BODY, theme::ACCENT);

  // The bar: what the node is for -- a message, its admin, adding it -- a
  // ping and the way there. The rest, less often, in a group under the info.
  lv_obj_t* acts = buttonBar(body);
  bool contact = e.contact_idx >= 0;
  bool admin = contact && (e.type == ADV_TYPE_REPEATER || e.type == ADV_TYPE_ROOM);
  bool pos = e.lat_e6 != 0 || e.lon_e6 != 0;
  if (contact && e.type == ADV_TYPE_CHAT) barButton(acts, LV_SYMBOL_ENVELOPE " Message", onNodeAction, NODE_MSG, true);
  if (admin) barButton(acts, LV_SYMBOL_SETTINGS " Admin", onNodeAction, NODE_ADMIN, true);
  if (!contact && e.has_key && !e.is_known) barButton(acts, LV_SYMBOL_PLUS " Add", onNodeAction, NODE_ADD, true);
  if (e.has_key) barButton(acts, LV_SYMBOL_LOOP " Ping", onNodeAction, NODE_PING);
  if (pos) barButton(acts, UI_SYMBOL_COMPASS " Navigate", onNodeAction, NODE_NAV);

  if (pos || contact) {
    lv_obj_t* g = group(info, "More");
    if (pos) actionRow(g, UI_SYMBOL_FLAG "  Save as waypoint", onNodeAction, (void*)(uintptr_t)NODE_WAYPOINT);
    if (contact) s_nd_fav = actionRow(g, "", onNodeAction, (void*)(uintptr_t)NODE_FAV);
    nodeFavText();
    if (contact && e.has_key) actionRow(g, UI_SYMBOL_TACK "  Pin to Home", onNodeAction, (void*)(uintptr_t)NODE_PIN);
    if (contact) _node_delete_lbl = actionRow(g, LV_SYMBOL_TRASH "  Delete contact", onNodeAction,
                                              (void*)(uintptr_t)NODE_DELETE, theme::FAIL);
  }

  refreshNode();
}

void UITask::refreshNode() {
  if (!_node_info) return;
  const NearbyModel::Entry& e = s_node;
  // Refresh position / age / signal from its model while the node is still listed.
  NearbyModel* model = _node_from_scan ? _scan : _nearby;
  model->refreshModel();
  for (int i = 0; i < model->count(); i++) {
    const NearbyModel::Entry& m = model->at(i);
    bool same = e.has_key ? (m.has_key && memcmp(m.pub_key, e.pub_key, PUB_KEY_SIZE) == 0)
              : (e.contact_idx >= 0) ? m.contact_idx == e.contact_idx
              : strncmp(m.name, e.name, sizeof(m.name)) == 0;
    if (same) { s_node = m; break; }
  }

  // A scan row carries no contact index; is_known says whether it's in the contacts.
  bool known = e.contact_idx >= 0 || (_node_from_scan && e.is_known);
  char status[64] = "", dist[40] = "", pos[32] = "", heard[16] = "", sig[40] = "", id[12] = "";
  int o = 0;
  auto tag = [&](const char* t) { o += snprintf(status + o, sizeof(status) - o, "%s%s", o ? ", " : "", t); };
  if (!known) tag("not a contact");
  if (e.is_live) tag(e.live_verified ? "live position" : "live position (channel)");
  if (e.fav) tag("favourite");
  if (status[0] >= 'a' && status[0] <= 'z') status[0] -= 'a' - 'A';
  int32_t lat, lon;
  int bearing = -1;
  if (e.lat_e6 != 0 || e.lon_e6 != 0) {
    if (_nearby->ownPosition(lat, lon) && e.dist_km >= 0.0f) {
      char d[16];
      geo::fmtDist(d, sizeof(d), e.dist_km, _prefs && _prefs->units_imperial);
      int az = geo::bearingDeg(lat, lon, e.lat_e6, e.lon_e6);
      bearing = az;
      snprintf(dist, sizeof(dist), "%s  %d\xC2\xB0 %s", d, az, geo::bearingCardinal(az));
    }
    snprintf(pos, sizeof(pos), "%.5f, %.5f", e.lat_e6 / 1e6, e.lon_e6 / 1e6);
  } else if (!_node_from_scan) {
    snprintf(pos, sizeof(pos), "Not shared");
  }
  char age[8];
  geo::fmtAgeShort(age, sizeof(age), rtc_clock.getCurrentTime(), e.lastmod);
  if (age[0]) snprintf(heard, sizeof(heard), "%s ago", age);
  if (_node_from_scan)
    snprintf(sig, sizeof(sig), "%d dBm, SNR %.1f / %.1f", e.rssi, e.snr_x4 / 4.0f, e.remote_snr_x4 / 4.0f);
  if (e.has_prefix) snprintf(id, sizeof(id), "%02X%02X%02X%02X", e.pub_key[0], e.pub_key[1], e.pub_key[2], e.pub_key[3]);
  infoSet(s_nd_type, NearbyModel::typeName(e.type));
  infoSet(s_nd_status, status);
  infoSet(s_nd_dist, dist);
  infoSet(s_nd_pos, pos);
  infoSet(s_nd_heard, heard);
  infoSet(s_nd_signal, sig);
  infoSet(s_nd_id, id);
  ContactInfo ci;
  int hops = -1;
  if (!_node_from_scan && e.contact_idx >= 0 && the_mesh.getContactByIdx(e.contact_idx, ci))
    hops = ci.out_path_len == 0xFF ? 0xFF : (ci.out_path_len & 63);
  nodeVizSet(bearing, hops);

  // Ping result (PingEngine releases its slot on reply; the view owns the timeout)
  if (_pinging) {
    int16_t out = 0, back = 0; uint32_t rtt = 0;
    _core->ping.getResult(out, back, rtt);
    if (!_core->ping.isActive() && (out || back || rtt)) {
      setTextFmt(_node_ping, "Ping %lu ms  -  SNR out %.1f  back %.1f", (unsigned long)rtt,
                            out / 4.0f, back / 4.0f);
      _pinging = false;
    } else if (millis() - _ping_started_ms > 3000) {
      _core->ping.clear();
      setText(_node_ping, "Ping: no reply");
      _pinging = false;
    }
  }
}

void UITask::nodeAction(uint8_t action) {
  NearbyModel::Entry& e = s_node;
  switch (action) {
    case NODE_MSG:
      openDM(e.pub_key);
      break;
    case NODE_PING: {
      if (_pinging) break;
      PingEngine::StartResult r = _core->ping.start(e.pub_key);
      if (r == PingEngine::STARTED) {
        _pinging = true;
        _ping_started_ms = millis();
        _next_nearby_ms = millis() + 250;
        lv_label_set_text(_node_ping, "Pinging...");
      } else {
        lv_label_set_text(_node_ping, r == PingEngine::UNSUPPORTED ? "Ping needs 1-2 byte path hashes"
                                                                   : "Ping failed");
      }
      break;
    }
    case NODE_FAV:
      if (contactctl::setFavourite(e.pub_key, !e.fav)) {   // in place: the list stays where it was scrolled
        e.fav = !e.fav;
        nodeFavText();
        refreshNode();
      }
      break;
    case NODE_ADD:
      if (the_mesh.addDiscoveredContact(e.pub_key, e.name, e.type)) {
        _screen = SCR_NEARBY;
        buildNearby();
      } else {
        showToast("Contacts full");
      }
      break;
    case NODE_NAV:
      // Followed by key when there is one (the Locator resolves live share,
      // then advert position); otherwise a fixed point where it was seen.
      if (e.has_prefix) navToNode(e.pub_key, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
      else {
        _core->locator.setTarget(0, nullptr, e.lat_e6, e.lon_e6, e.name[0] ? e.name : "Node");
        prefsSave();
        openMap(true);
        navFrameTarget();
      }
      break;
    case NODE_ADMIN:
      openAdmin(e.pub_key);
      break;
    case NODE_WAYPOINT: {
      if (_core->waypoints.full()) { showToast(waypointsFull()); break; }
      char t[48];
      if (_core->waypoints.add(e.lat_e6, e.lon_e6, rtc_clock.getCurrentTime(), e.name[0] ? e.name : "Node")) {
        snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(_core->waypoints.count() - 1).label);
        showToast(t);
      }
      break;
    }
    case NODE_PIN:   // to the favourites dial (Home), as from a chat's options
      pinPopup(false, 0, e.pub_key);
      break;
    case NODE_DELETE:
      if (!tapConfirmed(_node_delete_lbl, LV_SYMBOL_TRASH "  Tap again to delete")) break;
      if (the_mesh.deleteContactByKey(e.pub_key)) {
        _screen = SCR_NEARBY;
        buildNearby();
      }
      break;
  }
}

static lv_obj_t* switchRow(lv_obj_t* parent, const char* text, const char* sub, uint8_t* pref);   // below
#include "MapScreen.h"
#include "ProfileView.h"
#include "NavMap.h"
#include "MapAreas.h"
#include "MapRegions.h"
#include "HomeScreen.h"
#include "ClockScreen.h"
#include "RadioScreen.h"
#include "WifiScreen.h"
#include "ChannelScreen.h"
#include "AdminScreen.h"
#include "BotScreen.h"

// ── Conversation ──────────────────────────────────────────────────────────────

static void onKeyboard(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) s_ui->sendFromCompose();
  else if (code == LV_EVENT_CANCEL) s_ui->setKeyboardVisible(false);
}

// Compose limit is in UTF-8 bytes (the over-the-air limit), not characters:
// Cyrillic / Greek / accented letters take two bytes each. Leaves room for the
// "Name: " prefix a channel send adds.
static const size_t COMPOSE_MAX_BYTES = MAX_TEXT_LEN - 40;

static void onComposeInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  if (ins && strlen(lv_textarea_get_text(ta)) + strlen(ins) > COMPOSE_MAX_BYTES)
    lv_textarea_set_insert_replace(ta, "");   // reject: would exceed the byte limit
}

static void onComposeClicked(lv_event_t* e) { (void)e; s_ui->setKeyboardVisible(true); }
static void onComposeMore(lv_event_t* e) { (void)e; s_ui->quickPopup(); }

// Keyboard up: the screen header goes away and the body takes its 32 px, so
// a line or two of the conversation stays visible above the compose field
// (240 px can't fit header + list + field + keys). The field is FOCUSED
// while the keyboard is up -- that is what makes LVGL draw its cursor.
void UITask::setKeyboardVisible(bool show) {
  if (!_keyboard || !_compose_ta) return;
  if (show == !lv_obj_has_flag(_keyboard, LV_OBJ_FLAG_HIDDEN)) return;
  if (show) {
    lv_obj_remove_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    fieldFocus(_compose_ta, true);
  } else {
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    fieldFocus(_compose_ta, false);
  }
  if (_header && _body) {
    int top = theme::STATUS_H + (show ? 0 : lv_obj_get_height(_header));
    if (show) lv_obj_add_flag(_header, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_remove_flag(_header, LV_OBJ_FLAG_HIDDEN);
    lv_obj_set_pos(_body, 0, top);
    lv_obj_set_height(_body, lv_display_get_vertical_resolution(NULL) - top);
  }
  if (_thread_list) {   // keep the newest message in view
    layoutNow(_thread_list);
    lv_obj_scroll_to_y(_thread_list, LV_COORD_MAX, LV_ANIM_OFF);
  }
}

void UITask::openChannel(uint8_t channel_idx) {
  _thread_is_channel = true;
  _thread_channel = channel_idx;
  _thread_skip = 0;
  _core->history.setChUnread(channel_idx, 0);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::openDM(const uint8_t* pub_key) {
  _thread_is_channel = false;
  _thread_skip = 0;
  memset(_thread_key, 0, sizeof(_thread_key));
  ContactInfo c;
  if (MessageHistory::contactByPrefix(pub_key, c)) memcpy(_thread_key, c.id.pub_key, PUB_KEY_SIZE);
  else memcpy(_thread_key, pub_key, 4);
  _core->clearDMUnread(_thread_key);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::buildThread() {
  char title[40];
  bool can_send = true;
  if (_thread_is_channel) {
    ChannelDetails ch;
    if (the_mesh.getChannel(_thread_channel, ch)) snprintf(title, sizeof(title), "%s", ch.name);   // as named: "#" marks a hashtag channel
    else snprintf(title, sizeof(title), "Channel %d", _thread_channel);
  } else {
    contactName(_thread_key, title, sizeof(title));
    ContactInfo c;
    can_send = MessageHistory::contactByPrefix(_thread_key, c) && (c.type == ADV_TYPE_CHAT || c.type == ADV_TYPE_ROOM);
  }
  lv_obj_t* body = newScreen(title, true);
  lv_obj_set_style_pad_all(body, 0, 0);
  lv_obj_set_style_pad_row(body, 0, 0);
  if (_header && (_thread_is_channel || can_send)) {   // channel / conversation options
    lv_obj_set_width(lv_obj_get_child(_header, 1), 200);   // title, clear of the button
    headerButton(_header, LV_SYMBOL_SETTINGS, _thread_is_channel ? onChanThreadMenu : onConvThreadMenu, 4, NULL);
  }

  _thread_list = scrollList(body);
  s_th_rows_n = 0;
  lv_obj_set_style_pad_all(_thread_list, theme::PAD, 0);

  _compose_ta = nullptr;
  _keyboard = nullptr;
  if (can_send) {
    lv_obj_t* bar = lv_obj_create(body);
    styleSurface(bar, theme::BG);
    lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(bar, LV_PCT(100), 42);
    lv_obj_set_style_pad_all(bar, 4, 0);
    lv_obj_t* more = lv_button_create(bar);   // quick messages, placeholders
    lv_obj_set_size(more, 34, 34);
    lv_obj_align(more, LV_ALIGN_LEFT_MID, 0, 0);
    lv_obj_set_style_pad_all(more, 0, 0);
    lv_obj_set_style_radius(more, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(more, 0, 0);
    lv_obj_set_style_bg_color(more, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(more, onComposeMore, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(more, LV_SYMBOL_PLUS, THEME_FONT_BODY, theme::TEXT));
    _compose_ta = textField(bar, "Message");   // FOCUSED while the keyboard is up
    lv_obj_add_event_cb(_compose_ta, onComposeInsert, LV_EVENT_INSERT, NULL);
    lv_obj_set_width(_compose_ta, lv_display_get_horizontal_resolution(NULL) - 8 - 34 - 4);   // beside "+"
    lv_obj_align(_compose_ta, LV_ALIGN_RIGHT_MID, 0, 0);

    // In the body's flex column below the compose bar: showing it shrinks the
    // message list, so the text field stays visible just above the keys.
    _keyboard = kb::create(body, _prefs);   // phone-style, scripts from prefs, hold for accents (Keyboard.h)
    lv_obj_set_size(_keyboard, LV_PCT(100), 124);
    lv_keyboard_set_textarea(_keyboard, _compose_ta);
    fieldFocus(_compose_ta, false);   // until the keyboard is up
    lv_obj_add_event_cb(_keyboard, onKeyboard, LV_EVENT_READY, _keyboard);
    lv_obj_add_event_cb(_keyboard, onKeyboard, LV_EVENT_CANCEL, _keyboard);
    lv_obj_add_flag(_keyboard, LV_OBJ_FLAG_HIDDEN);
    lv_obj_add_event_cb(_compose_ta, onComposeClicked, LV_EVENT_CLICKED, _keyboard);
  }
  refreshThread();
  if (_compose_ta && _share_text[0]) {   // shareToMessage(): the text waits in the field
    lv_textarea_set_text(_compose_ta, _share_text);
    _share_text[0] = '\0';
    layoutNow(screen());
    setKeyboardVisible(true);
  }
}

// Changes whenever the open conversation's content or delivery markers change.
uint32_t UITask::threadSignature() const {
  const MessageHistory& h = _core->history;
  uint32_t sig = 0;
  if (_thread_is_channel) {
    int n = h.histCountForChannel(_thread_channel);
    sig = n;
    for (int j = 0; j < n && j < 8; j++) {
      const ChHistEntry& e = h.chAtPos(h.histEntryForChannel(_thread_channel, j));
      sig = sig * 31 + e.timestamp + e.relay_status + e.path_len;   // path_len: repeaters heard
    }
  } else {
    int n = h.dmHistCountForContact(_thread_key);
    sig = n;
    for (int j = 0; j < n && j < 8; j++) {
      const DmHistEntry& e = h.dmAtPos(h.dmHistEntryForContact(_thread_key, j));
      sig = sig * 31 + e.timestamp + h.dmEffectiveStatus(e);
    }
  }
  return sig;
}

// Positions found in the shown messages ([WAY] / [LOC] / plain "lat,lon"),
// for the Go / Save buttons under such a bubble.
struct MsgLoc { int32_t lat, lon; char label[WAYPOINT_LABEL_LEN * 2]; };
static const int THREAD_MAX_SHOWN = 50;   // newest bubbles built per conversation
static MsgLoc* s_msg_locs = psramBuf<MsgLoc>(THREAD_MAX_SHOWN);
static int    s_msg_loc_n = 0;

// The messages shown, oldest first: copies from the SD card's history (which
// holds far more than the RAM ring) or from the ring, one page at a time.
static ChHistEntry* s_th_ch = psramBuf<ChHistEntry>(THREAD_MAX_SHOWN);
static DmHistEntry* s_th_dm = psramBuf<DmHistEntry>(THREAD_MAX_SHOWN);

// What a held bubble is about: its entry (index into s_th_ch / s_th_dm),
// sender, position slot. One per shown message, at its index.
struct MsgMeta { int pos; bool channel; bool own; int loc; char from[32]; };
static MsgMeta* s_msg_meta = psramBuf<MsgMeta>(THREAD_MAX_SHOWN);
static int     s_msg_meta_n = 0;

// The bubbles built, one per shown message: which message (key), its
// delivery state, its row and age label. A refresh that only adds messages
// or changes a delivery mark touches just those bubbles -- rebuilding all 50
// on every repeater echo made the conversation stutter.
struct ThreadRow { uint32_t key, state; lv_obj_t* row; lv_obj_t* age; };
static ThreadRow* s_th_rows = psramBuf<ThreadRow>(THREAD_MAX_SHOWN);
static lv_obj_t* s_older_lbl = nullptr;   // "Older messages (n)" on the page's top
static bool      s_th_rows_newest = false;   // built for the newest page (the one patched)

// The shown message a bubble (or a button in it) belongs to, or -1.
static int threadRowOf(lv_obj_t* o) {
  for (; o; o = lv_obj_get_parent(o))
    for (int r = 0; r < s_th_rows_n; r++) if (s_th_rows[r].row == o) return r;
  return -1;
}

static void onMsgLoc(lv_event_t* e) {
  int r = threadRowOf((lv_obj_t*)lv_event_get_current_target(e));
  if (r >= 0) s_ui->messageLocationAction(s_msg_meta[r].loc, (uintptr_t)lv_event_get_user_data(e) != 0);
}

static void msgLocButton(lv_obj_t* parent, const char* text, bool save, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT : theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onMsgLoc, LV_EVENT_CLICKED, (void*)(uintptr_t)(save ? 1 : 0));
  lv_obj_center(label(b, text, THEME_FONT_SMALL, accent ? theme::BG : theme::TEXT));
}

// A message's text, wrapped at `max_w` and shrunk to fit. "@[nick]" mentions
// (how a reply names who it answers) show as "@nick" in the accent -- in the
// text colour on our own amber bubbles -- and underlined when the nick is
// ours (*mentions_me set). Plain text stays a label.
static lv_obj_t* msgText(lv_obj_t* parent, const char* text, bool own, int max_w, bool* mentions_me) {
  *mentions_me = false;
  if (!strstr(text, "@[")) {
    lv_obj_t* t = label(parent, text, THEME_FONT_BODY, theme::TEXT);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(t, max_w, 0);
    lv_obj_set_width(t, LV_SIZE_CONTENT);
    return t;
  }
  lv_obj_t* sg = lv_spangroup_create(parent);
  lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE);   // a hold still reaches the bubble (its menu)
  lv_obj_set_style_text_font(sg, THEME_FONT_BODY, 0);
  lv_obj_set_style_text_color(sg, lv_color_hex(theme::TEXT), 0);
  lv_spangroup_set_mode(sg, LV_SPAN_MODE_BREAK);
  const char* me = the_mesh.getNodeName();
  char part[MAX_TEXT_LEN + 1];
  const char* p = text;
  while (*p) {
    const char* at = strstr(p, "@[");
    const char* close = at ? strchr(at + 2, ']') : nullptr;
    if (at && (!close || close - at > 34)) close = nullptr;   // not a mention: a nick is <= 31 chars
    const char* end = at && close ? at : p + strlen(p);
    if (at && !close) end = at + 2;   // "@[" alone: plain, carry on after it
    if (end > p) {
      size_t n = end - p < (ptrdiff_t)sizeof(part) ? end - p : sizeof(part) - 1;
      memcpy(part, p, n);
      part[n] = '\0';
      lv_span_set_text(lv_spangroup_new_span(sg), part);
    }
    if (!at || !close) { p = end; continue; }
    size_t n = close - (at + 2);
    part[0] = '@';
    memcpy(part + 1, at + 2, n);
    part[n + 1] = '\0';
    bool mine = strlen(me) == n && strncasecmp(me, at + 2, n) == 0;
    lv_span_t* sp = lv_spangroup_new_span(sg);
    lv_span_set_text(sp, part);
    lv_style_t* st = lv_span_get_style(sp);
    lv_style_set_text_color(st, lv_color_hex(own ? theme::TEXT : theme::ACCENT));
    if (mine) { lv_style_set_text_decor(st, LV_TEXT_DECOR_UNDERLINE); *mentions_me = true; }
    p = close + 1;
  }
  lv_spangroup_refr_mode(sg);
  uint32_t w = lv_spangroup_get_expand_width(sg, 0);
  lv_obj_set_width(sg, w > (uint32_t)max_w ? max_w : (int32_t)w + 1);
  lv_obj_set_height(sg, LV_SIZE_CONTENT);
  return sg;
}

// One message bubble. Own messages right-aligned in amber, others left.
// loc: the text carries a position (Go / Save buttons); hold: a long press
// opens the message menu. Returns its row; *age_out = the age label.
static lv_obj_t* bubble(lv_obj_t* list, const char* from, const char* text, bool own,
                        uint32_t ts, const char* status, uint32_t status_col, bool loc, bool hold,
                        int relays, lv_obj_t** age_out) {
  // Full-width row that pushes the bubble to its side.
  lv_obj_t* row = lv_obj_create(list);
  styleSurface(row, theme::BG);
  lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(row, own ? LV_FLEX_ALIGN_END : LV_FLEX_ALIGN_START,
                        LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);

  lv_obj_t* b = lv_obj_create(row);
  lv_obj_remove_flag(b, LV_OBJ_FLAG_SCROLLABLE);
  if (hold) {   // hold: reply / path / target (ConversationScreen.h)
    lv_obj_add_event_cb(b, onMsgHold, LV_EVENT_LONG_PRESSED, nullptr);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  } else {
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(b, 250, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(own ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_set_style_border_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_pad_all(b, 6, 0);
  lv_obj_set_style_pad_row(b, 2, 0);
  lv_obj_set_flex_flow(b, LV_FLEX_FLOW_COLUMN);

  char meta[32];
  uint32_t now = rtc_clock.getCurrentTime();
  fmtMsgAge(meta, sizeof(meta), now, ts, s_prefs);   // "12s" / "5m" / "3h" / "2d"

  // Channel messages: "Sender  5m" on one line above the text, which keeps
  // the bubble two lines tall -- matters with the keyboard up.
  bool meta_in_header = from && from[0] && !status;
  if (from && from[0]) {
    lv_obj_t* hdr = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(hdr, 8, 0);
    label(hdr, from, THEME_FONT_SMALL, theme::ACCENT);   // names are <= 31 chars: fits the bubble
    if (meta_in_header) *age_out = label(hdr, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
  }
  bool mentions_me;
  msgText(b, text, own, 238, &mentions_me);
  if (mentions_me && !own) {   // someone answering us: the bubble outlined
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(theme::ACCENT), 0);
  }

  if (loc) {   // position: navigate there / keep it as a waypoint
    lv_obj_t* acts = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(acts, 6, 0);
    lv_obj_set_style_pad_top(acts, 2, 0);
    msgLocButton(acts, UI_SYMBOL_COMPASS " Go", false, true);
    msgLocButton(acts, UI_SYMBOL_FLAG " Save", true, false);
  }

  if (!meta_in_header) {   // the age, then the delivery mark in its colour (as L1)
    lv_obj_t* line = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 6, 0);
    *age_out = label(line, meta, THEME_FONT_SMALL, theme::TEXT_MUTED);
    if (relays > 0) {   // as L1: the count alone says it got out, no check beside it
      lv_obj_t* c = lv_obj_create(line);
      lv_obj_remove_style_all(c);
      lv_obj_remove_flag(c, LV_OBJ_FLAG_CLICKABLE);
      lv_obj_set_size(c, LV_SIZE_CONTENT, 15);
      lv_obj_set_style_min_width(c, 15, 0);
      lv_obj_set_style_pad_hor(c, 4, 0);
      lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
      lv_obj_set_style_bg_color(c, lv_color_hex(theme::OK), 0);
      lv_obj_set_style_radius(c, LV_RADIUS_CIRCLE, 0);
      lv_obj_t* n = label(c, "", THEME_FONT_SMALL, theme::BG);
      lv_label_set_text_fmt(n, "%d", relays);
      lv_obj_center(n);
    } else if (status && status[0]) {
      label(line, status, THEME_FONT_SMALL, status_col);
    }
  }
  return row;
}

// Remember the position in `text` (if any) for its bubble's buttons, in
// slot r; label from a [WAY] tag, else the sender. Returns r or -1.
static int noteMsgLocation(int r, const char* text, const char* sender) {
  MsgLoc& m = s_msg_locs[r];
  if (!geo::parseLatLon(text, m.lat, m.lon, m.label, sizeof(m.label))) return -1;
  if (!m.label[0]) snprintf(m.label, sizeof(m.label), "%s", sender && sender[0] ? sender : "Msg loc");
  return r;
}

void UITask::messageLocationAction(int idx, bool save) {
  if (idx < 0 || idx >= s_msg_loc_n) return;
  const MsgLoc& m = s_msg_locs[idx];
  if (save) {
    if (_core->waypoints.full()) { showToast(waypointsFull()); return; }
    if (_core->waypoints.add(m.lat, m.lon, rtc_clock.getCurrentTime(), m.label)) {
      char t[48];
      snprintf(t, sizeof(t), "Saved %s", _core->waypoints.at(_core->waypoints.count() - 1).label);
      showToast(t);
    }
    return;
  }
  // A place snapshotted from the text (kind 0): nothing to keep re-resolving.
  navSetTarget(0, nullptr, m.lat, m.lon, m.label);
  openMap(true);
  navFrameTarget();
  refreshNavBar();
}

// The page of the open conversation into s_th_ch / s_th_dm (oldest first):
// THREAD_MAX_SHOWN messages ending _thread_skip before the newest. From the
// SD card's copy when it has at least what the ring has, else from the ring.
// `total` = messages in that source.
int UITask::loadThreadPage(int& total) {
  const MessageHistory& h = _core->history;
  char path[64];
  if (_thread_is_channel) {
    int ring = h.histCountForChannel(_thread_channel);
    int arc = 0;
    int got = s_archive.ready() && s_archive.chPath(_thread_channel, path, sizeof(path))
            ? s_archive.window(path, _thread_skip, THREAD_MAX_SHOWN, s_th_ch, &arc) : 0;
    if (arc > 0 && arc >= ring) {
      total = arc;
      return got;
    }
    total = ring;
    int m = ring - _thread_skip;
    if (m > THREAD_MAX_SHOWN) m = THREAD_MAX_SHOWN;
    for (int i = 0; i < m; i++) s_th_ch[i] = h.chAtPos(h.histEntryForChannel(_thread_channel, _thread_skip + m - 1 - i));
    return m > 0 ? m : 0;
  }
  int ring = h.dmHistCountForContact(_thread_key);
  int arc = 0, got = 0;
  if (s_archive.ready()) {
    histstore::SdArchive::dmPath(_thread_key, path, sizeof(path));
    got = s_archive.window(path, _thread_skip, THREAD_MAX_SHOWN, s_th_dm, &arc);
  }
  if (arc > 0 && arc >= ring) {
    total = arc;
    return got;
  }
  total = ring;
  int m = ring - _thread_skip;
  if (m > THREAD_MAX_SHOWN) m = THREAD_MAX_SHOWN;
  for (int i = 0; i < m; i++) s_th_dm[i] = h.dmAtPos(h.dmHistEntryForContact(_thread_key, _thread_skip + m - 1 - i));
  return m > 0 ? m : 0;
}

static void onThreadPage(lv_event_t* e) { s_ui->threadPage((int)(intptr_t)lv_event_get_user_data(e)); }

// "Older messages" / "Newer messages" at the ends of a page.
static void pageButton(lv_obj_t* list, const char* text, int dir) {
  lv_obj_t* b = lv_button_create(list);
  lv_obj_set_size(b, LV_PCT(100), 32);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE), 0);
  lv_obj_add_event_cb(b, onThreadPage, LV_EVENT_CLICKED, (void*)(intptr_t)dir);
  lv_obj_center(label(b, text, THEME_FONT_SMALL, theme::TEXT_MUTED));
}

void UITask::threadPage(int dir) {
  _thread_skip += dir > 0 ? THREAD_MAX_SHOWN : -THREAD_MAX_SHOWN;
  if (_thread_skip < 0) _thread_skip = 0;
  _thread_scroll_top = dir < 0;   // newer: read on from the top of that page
  refreshThread();
}

// Every 2 s on a conversation, as ui-new: the bubbles' ages ("12s", "14s").
void UITask::refreshThreadAges() {
  _next_thread_ages_ms = millis() + 2000;
  uint32_t now = rtc_clock.getCurrentTime();
  for (int r = 0; r < s_th_rows_n; r++) {
    if (!s_th_rows[r].age) continue;
    char meta[32];
    fmtMsgAge(meta, sizeof(meta), now, _thread_is_channel ? s_th_ch[r].timestamp : s_th_dm[r].timestamp, _prefs);
    setText(s_th_rows[r].age, meta);
  }
}

void UITask::refreshThread() {
  if (!_thread_list) return;
  _thread_dirty = false;
  _thread_sig = threadSignature();
  _next_thread_ages_ms = millis() + 2000;   // the ages are written below
  const MessageHistory& h = _core->history;
  int total = 0;
  int n = loadThreadPage(total);
  if (n == 0 && _thread_skip > 0) {   // the page went away (history trimmed)
    _thread_skip = 0;
    n = loadThreadPage(total);
  }
  ContactInfo tc;
  bool room = !_thread_is_channel && MessageHistory::contactByPrefix(_thread_key, tc) && tc.type == ADV_TYPE_ROOM;

  // Which message entry i is, and the delivery state its bubble shows.
  auto keyOf = [&](int i) {
    uint32_t ts = _thread_is_channel ? s_th_ch[i].timestamp : s_th_dm[i].timestamp;
    const char* t = _thread_is_channel ? s_th_ch[i].text : s_th_dm[i].text;
    uint32_t k = 2166136261u ^ ts;
    for (; *t; t++) k = (k ^ (uint8_t)*t) * 16777619u;
    return k;
  };
  auto stateOf = [&](int i) -> uint32_t {
    if (_thread_is_channel) return s_th_ch[i].relay_status | (uint32_t)s_th_ch[i].path_len << 8;
    return s_th_dm[i].outgoing ? 0x100u | h.dmEffectiveStatus(s_th_dm[i]) : 0;
  };
  // Entry i's bubble (at the list's end) and its menu / position slots.
  auto build = [&](int i) {
    MsgMeta& m = s_msg_meta[i];
    const char* st = NULL; uint32_t col = theme::TEXT_MUTED;
    int nrel = 0;
    lv_obj_t* age = nullptr;
    lv_obj_t* row;
    if (_thread_is_channel) {
      const ChHistEntry& e = s_th_ch[i];
      // Channel text is "Sender: body"; our own posts are filed as "Me: body".
      char from[40] = "";
      const char* body = e.text;
      const char* sep = strstr(e.text, ": ");
      if (sep && sep - e.text < (int)sizeof(from)) {
        memcpy(from, e.text, sep - e.text);
        from[sep - e.text] = '\0';
        body = sep + 2;
      }
      bool own = strcmp(from, "Me") == 0;
      // Own posts: once a repeater echoed it, how many distinct repeaters did
      // (markChannelRelayed), or a check if that's unknown; nothing before --
      // no echo is normal.
      if (own && e.relay_status == ACK_OK) {
        nrel = e.path_len & 63;
        st = LV_SYMBOL_OK; col = theme::OK;
      } else if (own) st = "";
      int loc = own ? -1 : noteMsgLocation(i, body, from);
      m = { i, true, own, loc, "" };
      if (!own) snprintf(m.from, sizeof(m.from), "%s", from);
      row = bubble(_thread_list, own ? NULL : from, body, own, e.timestamp, st, col, loc >= 0, true, nrel, &age);
    } else {
      const DmHistEntry& e = s_th_dm[i];
      if (e.outgoing) {
        switch (h.dmEffectiveStatus(e)) {
          case ACK_OK:      st = LV_SYMBOL_OK; col = theme::OK; break;
          case ACK_FAIL:    st = LV_SYMBOL_CLOSE; col = theme::FAIL; break;
          case ACK_PENDING: st = "..."; break;
          default:          st = ""; break;
        }
      }
      // A room post is filed "Author: text": shown under its author.
      char who[33] = "";
      const char* text = e.text;
      if (!e.outgoing && room) text = contactctl::splitRoomPost(e.text, who, sizeof(who));
      else if (!e.outgoing) contactName(_thread_key, who, sizeof(who));
      int loc = e.outgoing ? -1 : noteMsgLocation(i, text, who);
      m = { i, false, false, loc, "" };
      snprintf(m.from, sizeof(m.from), "%s", who);
      row = bubble(_thread_list, room && !e.outgoing ? who : NULL, text, e.outgoing, e.timestamp, st, col,
                   loc >= 0, !e.outgoing, 0, &age);   // nothing to show for our own DM
    }
    s_th_rows[i] = { keyOf(i), stateOf(i), row, age };
  };
  auto olderText = [&](char* t, int sz) { snprintf(t, sz, LV_SYMBOL_UP "  Older messages (%d)", total - _thread_skip - n); };

  // The same page as built, with messages added at the end (the oldest
  // dropping off the top) and delivery marks changed: patch it.
  int kept = -1;   // bubbles still shown, after `drop` gone from the top
  int drop = 0;
  if (s_th_rows_newest && s_th_rows_n > 0 && n > 0 && _thread_skip == 0 && (total > n) == (s_older_lbl != nullptr)) {
    for (drop = 0; drop < s_th_rows_n && kept < 0; drop++) {
      int m = s_th_rows_n - drop;
      if (m > n) continue;
      bool same = true;
      for (int r = 0; r < m && same; r++) same = s_th_rows[drop + r].key == keyOf(r);
      if (same) kept = m;
    }
    drop--;
  }
  if (kept > 0) {
    for (int r = 0; r < drop; r++) lv_obj_delete(s_th_rows[r].row);
    memmove(s_th_rows, s_th_rows + drop, kept * sizeof(ThreadRow));
    memmove(s_msg_meta, s_msg_meta + drop, kept * sizeof(MsgMeta));
    for (int r = 0; r < kept; r++) {
      if (s_th_rows[r].state == stateOf(r)) {   // unchanged: its slots, and a fresh age
        const ChHistEntry* ce = _thread_is_channel ? &s_th_ch[r] : nullptr;
        char meta[32];
        fmtMsgAge(meta, sizeof(meta), rtc_clock.getCurrentTime(), ce ? ce->timestamp : s_th_dm[r].timestamp, _prefs);
        if (s_th_rows[r].age) setText(s_th_rows[r].age, meta);
        s_msg_meta[r].pos = r;
        if (s_msg_meta[r].loc >= 0 && drop > 0) { s_msg_locs[r] = s_msg_locs[r + drop]; s_msg_meta[r].loc = r; }
        continue;
      }
      lv_obj_t* old = s_th_rows[r].row;   // a delivery mark changed: that bubble anew, in its place
      build(r);
      lv_obj_move_to_index(s_th_rows[r].row, lv_obj_get_index(old));
      lv_obj_delete(old);
    }
    for (int r = kept; r < n; r++) build(r);
    if (s_older_lbl) { char t[40]; olderText(t, sizeof(t)); setText(s_older_lbl, t); }
    s_th_rows_n = s_msg_meta_n = s_msg_loc_n = n;
    if (n > kept) {   // new messages: down to them
      layoutNow(_thread_list);
      lv_obj_scroll_to_y(_thread_list, LV_COORD_MAX, LV_ANIM_OFF);
    }
    return;
  }

  lv_obj_clean(_thread_list);
  s_th_rows_n = 0;
  s_older_lbl = nullptr;
  if (total > _thread_skip + n) {
    char t[40];
    olderText(t, sizeof(t));
    pageButton(_thread_list, t, 1);
    s_older_lbl = lv_obj_get_child(lv_obj_get_child(_thread_list, -1), 0);
  }
  for (int i = 0; i < n; i++) build(i);   // oldest first
  s_th_rows_n = s_msg_meta_n = s_msg_loc_n = n;
  s_th_rows_newest = _thread_skip == 0;   // a page back is built whole each time
  if (_thread_skip > 0) pageButton(_thread_list, LV_SYMBOL_DOWN "  Newer messages", -1);
  if (n == 0) label(_thread_list, "No messages yet", THEME_FONT_BODY, theme::TEXT_MUTED);
  layoutNow(_thread_list);
  lv_obj_scroll_to_y(_thread_list, _thread_scroll_top ? 0 : LV_COORD_MAX, LV_ANIM_OFF);
  _thread_scroll_top = false;
}

// To the open conversation (channel or DM). The caller refreshes the thread.
bool UITask::sendThreadText(const char* text) {
  bool ok;
  if (_thread_is_channel) {
    ok = _core->sendChannelText(_thread_channel, text);
  } else {
    ContactInfo c;
    ok = MessageHistory::contactByPrefix(_thread_key, c) && _core->sendDirectText(c, text);
  }
  if (!ok) showToast("Send failed");
  return ok;
}

void UITask::sendFromCompose() {
  if (!_compose_ta) return;
  const char* typed = lv_textarea_get_text(_compose_ta);
  if (!typed || !typed[0]) return;
  char text[MSG_TEXT_BUF];
  msgtext::expandOutgoing(typed, text, sizeof(text), _prefs);   // {loc}, {time}, ... (MessageText.h)
  if (!sendThreadText(text)) return;
  lv_textarea_set_text(_compose_ta, "");
  setKeyboardVisible(false);
  refreshThread();
}

// ── Settings (first rows; the declarative schema replaces this later) ────────

static lv_obj_t* s_kb_main_dd = nullptr;
static lv_obj_t* s_kb_alt_dd = nullptr;

static void onKeyboardAlphabet(lv_event_t* e) {
  (void)e;
  s_ui->setKeyboardAlphabets(choiceSelected(s_kb_main_dd), choiceSelected(s_kb_alt_dd));
}

// A preference toggle (0/1 byte in NodePrefs), saved on change.
static void onPrefSwitch(lv_event_t* e) {
  uint8_t* pref = (uint8_t*)lv_event_get_user_data(e);
  *pref = lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0;
  prefsSave();
}

static void onGpsSwitch(lv_event_t* e) {
  s_ui->setGps(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

// pref == nullptr: the caller wires the switch itself.
static lv_obj_t* switchRow(lv_obj_t* parent, const char* text, const char* sub, uint8_t* pref) {
  lv_obj_t* row = settingRow(parent, text, sub, 236);   // the hint clear of the switch
  lv_obj_t* sw = lv_switch_create(row);
  lv_obj_set_size(sw, 46, 24);
  lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  if (pref) {
    if (*pref) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onPrefSwitch, LV_EVENT_VALUE_CHANGED, pref);
  }
  return sw;
}

// ── Schema-driven settings (ui-core/SettingsSchema.h) ─────────────────────────

static void onSchemaSwitch(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onSchemaDropdown(lv_event_t* e) {
  s_ui->setSchemaValue((int)(uintptr_t)lv_event_get_user_data(e),
                       choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onOpenSchemaPage(lv_event_t* e) {
  s_nav_section = -1;   // from Settings: all of a page
  s_ui->showSchemaSettings((int)(uintptr_t)lv_event_get_user_data(e));
}
static void onHomeApps(lv_event_t* e) { (void)e; s_ui->homeEdit(true); }
static void onPruneContacts(lv_event_t* e) { (void)e; s_ui->pruneContacts(); }
static void onOpenQuickMsgs(lv_event_t* e);
static void onPinSetup(lv_event_t* e);   // DeviceScreen.h
static void onBattCurve(lv_event_t* e) { (void)e; s_ui->showBattCurve(); }
static void onOpenOta(lv_event_t* e);    // OtaScreen.h
static void bluetoothRow(lv_obj_t* body);
static void onVolumeSlider(lv_event_t* e) {
  s_ui->setSoundVolume((int)lv_slider_get_value((lv_obj_t*)lv_event_get_target(e)));
}


// Map tools > Trail / Share / Alert options: that section of the Map options.
void UITask::showMapOptions(uint8_t section) {
  s_nav_section = section;
  showSchemaSettings(settings::PG_NAV);
}

void UITask::showSchemaSettings(int page) {
  if (page == settings::PG_NAV && !_nav_back) s_opts_from_map = _screen == SCR_MAP;
  _screen = SCR_SETTINGS_NAV;
  _settings_page = (uint8_t)(page < PG_ALL ? page : 0);
  buildSchemaSettings();
}

// Contacts > prune now: first tap shows how many, the second (within 3 s) removes them.
void UITask::pruneContacts() {
  int n = the_mesh.countStaleContacts();
  if (n == 0) {
    showToast(_prefs && _prefs->contact_expiry_idx == 0 ? "Contact expiry is off" : "No inactive contacts");
    return;
  }
  char ask[40];
  snprintf(ask, sizeof(ask), LV_SYMBOL_TRASH "  Remove %d contact%s?", n, n == 1 ? "" : "s");
  if (!tapConfirmed(_prune_lbl, ask)) return;
  int removed = the_mesh.pruneStaleContacts();
  char t[40];
  snprintf(t, sizeof(t), "Removed %d contact%s", removed, removed == 1 ? "" : "s");
  showToast(t);
}

void UITask::applyDisplayPrefs() {
  if (!_prefs) return;
  if (_prefs->display_brightness_pct) lvport::setBacklightPct(_prefs->display_brightness_pct);
  else if (_display) _display->setBrightness(_prefs->display_brightness);
}

// Brightness slider: live while dragging, saved on release.
static void onBrightnessSlider(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  s_ui->setBrightnessPct((uint8_t)lv_slider_get_value(sl), lv_event_get_code(e) == LV_EVENT_RELEASED);
}

void UITask::setBrightnessPct(uint8_t pct, bool save) {
  if (!_prefs) return;
  _prefs->display_brightness_pct = pct;
  _prefs->display_brightness = (uint8_t)((pct + 12) / 25 > 4 ? 4 : (pct + 12) / 25);   // nearest level, for anything reading it
  applyDisplayPrefs();
  if (save) prefsSave();
}

static void onTapWake(lv_event_t* e) {
  s_ui->setTapWake(lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}

void UITask::setTapWake(bool on) {
  _tap_wake = on;
  lvport::saveTapWake(on);
}

void UITask::buildSchemaSettings() {
  static const char* const OWN[] = { "Keyboard", "About", "Display", "Power", "Time" };
  const char* title = _settings_page >= PG_KEYBOARD ? OWN[_settings_page - PG_KEYBOARD] : settings::pageTitle(_settings_page);
  if (_settings_page == settings::PG_NAV && s_nav_section >= 0)
    title = s_nav_section == settings::SEC_TRAIL ? "Trail options"
          : s_nav_section == settings::SEC_LIVE_SHARE ? "Share options" : "Alert options";
  lv_obj_t* body = newScreen(title, true);
  _prune_lbl = nullptr;
  switch (_settings_page) {
    case PG_KEYBOARD: buildKeyboardPage(body); return;
    case PG_ABOUT:    buildAboutPage(body); return;
    case PG_DISPLAY: {
      lv_obj_t* g = group(body, "SCREEN");
      schemaRow(g, SETTING(display_brightness));
      schemaRow(g, SETTING(auto_off_secs));
      schemaRow(g, SETTING(msg_wake));
      lv_obj_t* tw = switchRow(g, "Tap to wake", "Off: only the top button wakes it", nullptr);   // in NVS
      if (_tap_wake) lv_obj_add_state(tw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(tw, onTapWake, LV_EVENT_VALUE_CHANGED, NULL);
      g = group(body, "LOCK");
      schemaRow(g, SETTING(auto_lock));
      listRow(g, "Screen PIN", pinSet() ? "On  -  asked when the screen wakes" : "Off", onPinSetup, NULL);
      accentRow(group(body, "LOOK"));   // DeviceScreen.h
      return;
    }
    case PG_POWER: {
      lv_obj_t* g = group(body, "BATTERY");
      schemaRow(g, SETTING(batt_display_mode));
      schemaRow(g, SETTING(low_batt_mv));
      listRow(g, "Battery curve", _prefs && battery::validCurve(_prefs->batt_curve_mv) ? "Your own" : "LiPo (built in)",
              onBattCurve, NULL);
      g = group(body, "GPS");
      schemaRow(g, SETTING(gps_interval));
      return;
    }
    case PG_TIME: {
      lv_obj_t* g = group(body, nullptr);
      for (int i = 0; i < settings::COUNT; i++) if (settings::ALL[i].section == settings::SEC_TIME) schemaRow(g, i);
      return;
    }
  }
  schemaRows(body, _settings_page);
  if (_settings_page == settings::PG_SOUND) then(&UITask::schemaExtras, settings::PG_SOUND);   // after its groups
  if (_settings_page == settings::PG_MESSAGES) then(&UITask::schemaExtras, settings::PG_MESSAGES);
}

// What a page adds to its schema groups, once they are there.
void UITask::schemaExtras(int page) {
  if (page == settings::PG_SOUND) buildSoundRows(_body, false);   // the melodies
  if (page == settings::PG_MESSAGES) {
    lv_obj_t* c = s_sec_card[settings::SEC_CONTACTS];   // the action that goes with "Contact expiry"
    if (c) _prune_lbl = actionRow(c, LV_SYMBOL_TRASH "  Remove inactive contacts now", onPruneContacts, NULL, theme::FAIL);
    char sub[48];
    snprintf(sub, sizeof(sub), "%d of %d set  -  sent with one tap", msgtext::quickUsed(_prefs), msgtext::QUICK_COUNT);
    if (lv_obj_t* m = s_sec_card[settings::SEC_MESSAGES]) listRow(m, "Quick messages", sub, onOpenQuickMsgs, NULL);
  }
}

// Settings > Keyboard: the two scripts the keyboard switches between.
void UITask::buildKeyboardPage(lv_obj_t* body) {
  uint8_t main_a = _prefs ? _prefs->keyboard_main_alphabet : 0;
  uint8_t alt_a  = _prefs ? _prefs->keyboard_alt_alphabet : 0;
  if (main_a >= NodePrefs::KB_ALPHABET_COUNT) main_a = 0;
  if (alt_a >= NodePrefs::KB_ALPHABET_COUNT) alt_a = main_a;
  lv_obj_t* g = group(body, "SCRIPTS");
  // Order matches NodePrefs::KB_ALPHABET_* (Latin, Cyrillic, Greek).
  s_kb_main_dd = choiceRow(g, "Main", nullptr, "Latin\nCyrillic\nGreek", main_a, onKeyboardAlphabet);
  // "None" = no second script (stored as alt == main, as ui-new does).
  s_kb_alt_dd  = choiceRow(g, "Additional", "The globe key switches to it", "None\nLatin\nCyrillic\nGreek",
                           alt_a == main_a ? 0 : alt_a + 1, onKeyboardAlphabet);
  groupNote(body, "Hold a letter for accents and other variants.");
}

// Settings > About: this node, the firmware, credits.
void UITask::buildAboutPage(lv_obj_t* body) {
  lv_obj_t* about = infoCard(body);
  infoRow(about, "Node", the_mesh.getNodeName());
  infoRow(about, "Firmware", FIRMWARE_VERSION);
  if (!strstr(FIRMWARE_VERSION, FIRMWARE_BUILD_DATE)) infoRow(about, "Built", FIRMWARE_BUILD_DATE);
  sectionTitle(body, "CREDITS");
  lv_obj_t* cr = infoCard(body);
  infoNote(cr, "Map data", creditText((lvport::mountStorage() && mapview::s_provider->available())
                                          ? mapview::s_provider->attribution() : "\xC2\xA9 OpenStreetMap contributors"));
  infoNote(cr, "Emoji", creditText("Twemoji \xC2\xA9 Twitter, Inc. and contributors (CC-BY\xC2\xA0" "4.0)"));
}

// A page's schema rows, one group per section (Settings pages and the tools'
// options). Each section's card is kept in s_sec_card for rows added after.

// On a screen's body the first group shows at once and the others follow
// (fill); a popup's list gets them all now.
static lv_obj_t* s_schema_body = nullptr;
static bool s_schema_one = false;   // one section: its own page, no heading

void UITask::schemaRows(lv_obj_t* body, uint8_t page) {
  for (lv_obj_t*& c : s_sec_card) c = nullptr;
  s_schema_body = body;
  s_schema_one = page == settings::PG_NAV && s_nav_section >= 0;
  uint8_t sec = 0xFF;
  bool first = true;
  for (int i = 0; i < settings::COUNT; i++) {
    const settings::Setting& st = settings::ALL[i];
    if (settings::sectionPage(st.section) != page || st.section == sec) continue;
    if (s_schema_one && st.section != s_nav_section) continue;
    sec = st.section;
    if (first || body != _body) schemaSection(i);
    else fill(&UITask::schemaSection, i, i + 1);
    first = false;
  }
}

// The group of the section setting i starts, with its rows.
void UITask::schemaSection(int i) {
  uint8_t sec = settings::ALL[i].section;
  lv_obj_t* card = s_sec_card[sec] = group(s_schema_body, s_schema_one ? nullptr : settings::sectionTitle(sec));
  if (sec == settings::SEC_SOUND) buildSoundRows(card, true);   // On / Off / Auto
  for (int k = i; k < settings::COUNT && settings::ALL[k].section == sec; k++) {
    if (settings::ALL[k].offset == offsetof(NodePrefs, lock_compact)) continue;   // ui-new's lock look; the slide card has one
    schemaRow(card, k);
  }
}

// One schema setting as a row of `card`: a switch, a choice, or (brightness,
// volume) a slider.
void UITask::schemaRow(lv_obj_t* card, int i) {
  if (i < 0 || i >= settings::COUNT) return;
  const settings::Setting& st = settings::ALL[i];
  uint8_t v = settings::get(*_prefs, st);
  if (st.offset == offsetof(NodePrefs, buzzer_volume)) {   // a five-step slider, heard on release
    lv_obj_t* sl = rowSlider(settingRow(card, st.label, NULL), 0, 4, v);
    lv_obj_add_event_cb(sl, onVolumeSlider, LV_EVENT_RELEASED, NULL);
    return;
  }
  if (st.offset == offsetof(NodePrefs, display_brightness)) {   // a slider here instead of five steps
    uint8_t pct = _prefs->display_brightness_pct ? _prefs->display_brightness_pct
                                                 : (uint8_t)(_prefs->display_brightness * 25 > 5 ? _prefs->display_brightness * 25 : 5);
    lv_obj_t* sl = rowSlider(settingRow(card, st.label, NULL), 5, 100, pct);
    lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_add_event_cb(sl, onBrightnessSlider, LV_EVENT_RELEASED, NULL);
    return;
  }
  if (!st.option) {
    lv_obj_t* sw = switchRow(card, st.label, st.hint, nullptr);
    if (v) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onSchemaSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
    return;
  }
  char opts[160];
  int o = 0;
  for (uint8_t k = 0; k < st.count && o < (int)sizeof(opts) - 16; k++) {
    if (k) opts[o++] = '\n';
    st.option(k, opts + o, sizeof(opts) - o, *_prefs);
    o += strlen(opts + o);
  }
  opts[o] = '\0';
  choiceRow(card, st.label, st.hint, opts, v, onSchemaDropdown, (void*)(uintptr_t)i);
}

void UITask::setSchemaValue(int idx, int v) {
  if (!_prefs || idx < 0 || idx >= settings::COUNT) return;
  const settings::Setting& st = settings::ALL[idx];
  settings::set(*_prefs, st, (uint8_t)v);
  if (st.changed) st.changed(*_core);
  prefsSave();
  if (st.offset == offsetof(NodePrefs, units_imperial) && _screen == SCR_SETTINGS_NAV)   // other labels depend on it
    rebuildSoon(&UITask::rebuildSchemaSettings);
}

void UITask::rebuildSchemaSettings() {
  int32_t y = _body ? lv_obj_get_scroll_y(_body) : 0;
  buildSchemaSettings();
  fillFlush();
  if (_body) { layoutNow(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
}

// A screen rebuilt for what a switch or segmented row changed (hints, rows
// that come and go), once its knob has moved: rebuilt at once, the 80-130 ms
// cut the animation short and the knob jumped. Not if the screen's gone by then.
static lv_timer_t* s_rebuild_t = nullptr;
void UITask::rebuildSoon(void (UITask::*fn)()) {
  static void (UITask::*s_fn)() = nullptr;
  static uint8_t s_scr = 0;
  s_fn = fn;
  s_scr = (uint8_t)_screen;
  if (s_rebuild_t) lv_timer_delete(s_rebuild_t);
  s_rebuild_t = lv_timer_create([](lv_timer_t*) {
    s_rebuild_t = nullptr;
    if ((uint8_t)s_ui->_screen == s_scr && !lv_display_get_screen_prev(NULL)) (s_ui->*s_fn)();
  }, 150, nullptr);
  lv_timer_set_repeat_count(s_rebuild_t, 1);
}

void UITask::showSettings() {
  _screen = SCR_SETTINGS;
  buildSettings();
}

static void onOpenRadio(lv_event_t* e);   // RadioScreen.h
static void onOpenStorage(lv_event_t* e); // StorageScreen.h

// Settings: groups of rows, each opening a page (chevron) or switching
// something on the spot. What the app is for first, the system last.
void UITask::buildSettings() {
  bool restore = _nav_back;   // back from one of its pages: where it was
  lv_obj_t* body = newScreen("Settings", true);
  // A group at a time (fillStart): the first shows at once. Coming back to
  // where it was scrolled needs them all first.
  fillStart(SETTINGS_GROUPS, 1, &UITask::settingsGroup);
  if (restore) { fillFlush(); layoutNow(body); lv_obj_scroll_to_y(body, s_settings_y, LV_ANIM_OFF); }
}

void UITask::settingsGroup(int i) {
  lv_obj_t* body = _body;
  char sub[48];
  lv_obj_t* g;
  char v1[24], v2[24];
  switch (i) {
  case 0:
    if (!_prefs) break;
    g = group(body, "DEVICE");
    listRow(g, LV_SYMBOL_EYE_OPEN "  Display", "Brightness, screen off, lock, colour",
            onOpenSchemaPage, (void*)(uintptr_t)PG_DISPLAY);
    snprintf(sub, sizeof(sub), "Order, hide  -  %d of %d shown", home::shownCount(), home::APP_COUNT);
    listRow(g, LV_SYMBOL_HOME "  Home apps", sub, onHomeApps, NULL);
    snprintf(sub, sizeof(sub), "Shutdown %s  -  GPS: %s", settingText(*_prefs, SETTING(low_batt_mv), v1, sizeof(v1)),
             settingText(*_prefs, SETTING(gps_interval), v2, sizeof(v2)));
    listRow(g, LV_SYMBOL_BATTERY_FULL "  Power", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_POWER);
    {
      char vol[12];
      settings::optVolume(_prefs->buzzer_volume, vol, sizeof(vol), *_prefs);
      snprintf(sub, sizeof(sub), "%s, %s", soundctl::modeLabel(soundctl::mode(_prefs)), vol);
    }
    listRow(g, LV_SYMBOL_VOLUME_MAX "  Sound", sub, onOpenSchemaPage, (void*)(uintptr_t)settings::PG_SOUND);
    {
      static const char* const SCRIPT[] = { "Latin", "Cyrillic", "Greek" };
      uint8_t ma = _prefs->keyboard_main_alphabet % NodePrefs::KB_ALPHABET_COUNT;
      uint8_t aa = _prefs->keyboard_alt_alphabet % NodePrefs::KB_ALPHABET_COUNT;
      snprintf(sub, sizeof(sub), aa == ma ? "%s" : "%s + %s", SCRIPT[ma], SCRIPT[aa]);
    }
    listRow(g, LV_SYMBOL_KEYBOARD "  Keyboard", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_KEYBOARD);
    listRow(g, LV_SYMBOL_ENVELOPE "  Messages & contacts", "Resend, expiry, quick messages",
            onOpenSchemaPage, (void*)(uintptr_t)settings::PG_MESSAGES);
    break;

  case 1:
    g = group(body, "CONNECTIONS");
    if (_prefs) {
      int pi = radioctl::currentPreset(_prefs);
      const char* pn = "Custom"; float f, b; uint8_t sf, cr;
      if (pi >= 0) radioctl::presetAt(_prefs, pi, pn, f, b, sf, cr);
      snprintf(sub, sizeof(sub), "%s  -  %.3f MHz, %d dBm", pn, _prefs->freq, _prefs->tx_power_dbm);
      listRow(g, UI_SYMBOL_RADIO "  Radio", sub, onOpenRadio, NULL);
    }
    bluetoothRow(g);
    wifiRow(g);
    if (_core->gpsAvailable()) {   // as WiFi: the switch turns it on / off, the row opens its details
      lv_obj_t* sw = switchRow(g, LV_SYMBOL_GPS "  GPS", "Tap for satellites and signal", nullptr);
      if (_core->gpsEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, onGpsSwitch, LV_EVENT_VALUE_CHANGED, NULL);
      lv_obj_t* row = lv_obj_get_parent(sw);
      rowPressable(row);
      lv_obj_add_event_cb(row, [](lv_event_t* e) {
        if (lv_event_get_target(e) == lv_event_get_current_target(e)) onOpenGpsFromSettings(e);   // not the switch
      }, LV_EVENT_CLICKED, NULL);
    }
    break;

  case 2:
    g = group(body, "MAP & DATA");
    if (_prefs) listRow(g, UI_SYMBOL_MAP "  Map", "Trail, live sharing, arrival alert",
                        onOpenSchemaPage, (void*)(uintptr_t)settings::PG_NAV);
    listRow(g, LV_SYMBOL_SD_CARD "  Storage", "SD card, message history", onOpenStorage, NULL);
    break;

  case 3:
    g = group(body, "SYSTEM");
    listRow(g, LV_SYMBOL_EDIT "  Name", the_mesh.getNodeName(), onNodeName, NULL);
    if (_prefs) {
      snprintf(sub, sizeof(sub), "%s, %s", settingText(*_prefs, SETTING(tz_offset_hours), v1, sizeof(v1)),
               _prefs->clock_12h ? "12 h" : "24 h");
      listRow(g, UI_SYMBOL_CLOCK "  Time", sub, onOpenSchemaPage, (void*)(uintptr_t)PG_TIME);
      schemaRow(g, SETTING(units_imperial));
    }
    listRow(g, LV_SYMBOL_DOWNLOAD "  Firmware update", FIRMWARE_VERSION, onOpenOta, NULL);
    listRow(g, LV_SYMBOL_LIST "  About", "Node, firmware, credits", onOpenSchemaPage, (void*)(uintptr_t)PG_ABOUT);
    break;

  case 4:
    g = group(body, nullptr);
    actionRow(g, LV_SYMBOL_REFRESH "  Reboot", onPowerRow, (void*)(uintptr_t)1);
    actionRow(g, LV_SYMBOL_POWER "  Power off", onPowerRow, (void*)(uintptr_t)0, theme::FAIL);
    break;
  }
}

void UITask::setKeyboardAlphabets(int main_idx, int alt_sel) {
  if (!_prefs) return;
  _prefs->keyboard_main_alphabet = (uint8_t)main_idx;
  _prefs->keyboard_alt_alphabet  = (uint8_t)(alt_sel == 0 ? main_idx : alt_sel - 1);
  prefsSave();
}

#include "ConversationScreen.h"
#include "DeviceScreen.h"
#include "DiagScreen.h"
#include "CompassScreen.h"
#include "BatteryScreen.h"
#include "GpsScreen.h"
#include "RadioExtras.h"
#include "RepeaterScreen.h"
#include "SoundScreen.h"
#include "QuickScreen.h"
#include "QuickPanel.h"
#include "OtaScreen.h"
#include "StorageScreen.h"
#include "Splash.h"

// The screen on, the full CPU clock while it's worth having: touched in the
// last CPU_IDLE_MS, something moving, map tiles to decode, a download or an
// update on WiFi. Otherwise 80 MHz (lvport::cpuSlow) -- a clock ticking over,
// a message being read.
static constexpr uint32_t CPU_IDLE_MS = 2000;
bool UITask::cpuNeeded() {
#ifdef UI_PERF_TEST
  return true;   // measured at the full clock
#endif
  if (lv_display_get_inactive_time(NULL) < CPU_IDLE_MS || lv_anim_count_running() > 0) return true;
  if (wifiInUse() || mapview::s_dl.liveQueued() > 0) return true;
  if (_screen == SCR_MAP && _map_pending) return true;
  if (_screen == SCR_HOME && home::mini::s_area && home::mini::s_pending) return true;
  return false;
}

#if defined(SIM_PLATFORM) && defined(__EMSCRIPTEN__)
// Sim tests: straight to a screen by name.
extern "C" EMSCRIPTEN_KEEPALIVE void sim_open(const char* name) {
  if (!s_ui) return;
  struct { const char* n; void (UITask::*fn)(); } const SCREENS[] = {
    { "home", &UITask::showHome }, { "chats", &UITask::showChats }, { "settings", &UITask::showSettings },
    { "nodes", &UITask::showNearby }, { "ota", &UITask::showOta }, { "wifi", &UITask::showWifi },
    { "clock", &UITask::showClock }, { "radio", &UITask::showRadio }, { "scopes", &UITask::showScopes },
    { "repeater", &UITask::showRepeater }, { "bot", &UITask::showBot }, { "storage", &UITask::showStorage },
    { "diag", &UITask::showDiag }, { "compass", &UITask::showCompass }, { "admin", &UITask::showAdminPick },
    { "quick", &UITask::showQuickMsgs }, { "contacts", &UITask::showContacts },
  };
  for (auto& s : SCREENS) if (!strcmp(s.n, name)) { (s_ui->*s.fn)(); return; }
  if (!strncmp(name, "page", 4)) { s_ui->showSchemaSettings(atoi(name + 4)); return; }
  if (!strcmp(name, "map")) { s_ui->openMap(true); return; }
  if (!strcmp(name, "nodesmap")) { s_ui->openMap(false); return; }   // as from Nodes
  if (!strcmp(name, "battcurve")) { s_ui->showBattCurve(); return; }
  if (!strcmp(name, "homeedit")) { s_ui->showHome(); s_ui->homeEdit(true); return; }   // arranging the apps
  if (!strncmp(name, "accent@", 7)) { theme::setAccent(atoi(name + 7)); return; }   // "accent@4": Cyan
  if (!strncmp(name, "map@", 4)) { s_ui->simMapAt(name + 4); return; }   // "map@lat,lon,z"
  if (!strcmp(name, "vector")) { s_ui->setVectorMap(true); return; }
  if (!strcmp(name, "maptools")) { s_ui->navToolsPopup(); return; }
  if (!strcmp(name, "areasel")) { s_ui->areaSelectBegin(); return; }
  if (!strcmp(name, "areas")) { s_ui->mapAreasPopup(); return; }
  if (!strcmp(name, "regions")) { s_ui->mapRegionsPopup(); return; }
  if (!strcmp(name, "region0")) { s_ui->mapRegionPopup(0); return; }
  if (!strcmp(name, "advert")) { s_ui->advertPopup(); return; }
  if (!strcmp(name, "trailsave")) { s_ui->navToolAction(navmap::TL_TRAIL_SAVE); return; }   // Map tools > Save
  if (!strcmp(name, "savedtrail0")) { navmap::scanTrails(); s_ui->savedTrailPopup(0); return; }
  if (!strncmp(name, "trail@", 6)) {   // "trail@name.trl" from /sdcard/trails: Load, as the saved-trail popup does
    navmap::scanTrails();
    for (int i = 0; i < navmap::s_st_n; i++)
      if (!strcmp(navmap::s_st_names[i], name + 6)) { navmap::s_st_sel = i; s_ui->savedTrailAction(navmap::TL_ST_LOAD); }
    return;
  }
}
// Scrolls the screen's (or a popup's) main list by dy; returns what was left to scroll.
static void dbgScrollable(lv_obj_t* o, lv_obj_t*& best, int& h) {
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_HIDDEN) || lv_obj_check_type(o, &lv_roller_class) || lv_obj_check_type(o, &lv_dropdownlist_class)) return;
  int v = lv_obj_get_scroll_bottom(o) + lv_obj_get_scroll_y(o);
  if (lv_obj_has_flag(o, LV_OBJ_FLAG_SCROLLABLE) && v > h) { h = v; best = o; }
  for (uint32_t i = 0; i < lv_obj_get_child_count(o); i++) dbgScrollable(lv_obj_get_child(o, i), best, h);
}
extern "C" EMSCRIPTEN_KEEPALIVE int sim_scroll(int dy) {
  lv_obj_t* o = nullptr; int h = 10;
  dbgScrollable(lv_layer_top(), o, h);
  if (!o) dbgScrollable(lv_screen_active(), o, h);
  if (!o) return -1;
  int left = lv_obj_get_scroll_bottom(o);
  lv_obj_scroll_by(o, 0, -(dy < left ? dy : left), LV_ANIM_OFF);
  return left;
}
#endif
