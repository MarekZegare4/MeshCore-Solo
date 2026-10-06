#pragma once
// The quick panel, as a phone's: pulled down from the status bar (or a tap
// on it), over any screen. Tiles that switch at a tap -- Bluetooth, WiFi,
// GPS, sound, the trail, live share -- the advert, the lock, and the
// brightness under them. Holding a tile opens its settings. The tiles are two
// pages of four, swiped sideways. Swiped up, a tap under it or on the status
// bar, the side button or another screen puts it away. The screen under it
// is frozen and dimmed in one snapshot (freeze::early, as under a popup), so
// the slide redraws an image and the panel -- a live screen under it, or a
// dim blended over that, was redrawn every frame -- and nothing under it
// takes a touch. ui-new has the same things as Home pages (Radio, Bluetooth,
// Advert, GPS).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after QuickScreen.h.

namespace qpanel {

enum : uint8_t { T_BT, T_WIFI, T_GPS, T_SOUND, T_ADVERT, T_TRAIL, T_SHARE, T_LOCK, T_COUNT };
static const int PER_PAGE = 4;

static lv_obj_t* s_panel = nullptr;
static lv_obj_t* s_catch = nullptr;   // under it, while it's out: a tap there closes it
static lv_obj_t* s_pages = nullptr;
static lv_obj_t* s_dot[2];
static uint8_t s_look[T_COUNT];       // what tileSet() last drew (0: nothing yet)
static lv_obj_t* s_tile[T_COUNT];
static lv_obj_t* s_icon[T_COUNT];
static lv_obj_t* s_cap[T_COUNT];
static lv_obj_t* s_slider = nullptr;
static int32_t s_h = 1;               // how far it moves: its height and the status bar's
static bool s_open = false;           // down, or on its way down
static bool s_drag = false;           // following a finger from the status bar
static int32_t s_y0 = 0;

// Its place: 0 down, -s_h away under the status bar.
static void place(int32_t ty) {
  if (ty > 0) ty = 0;
  if (ty < -s_h) ty = -s_h;
  lv_obj_set_style_translate_y(s_panel, ty, 0);
}
static void placeCb(void* o, int32_t v) { (void)o; place(v); }
static void hidden(lv_anim_t* a) {
  (void)a;
  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
  if (s_catch) { lv_obj_delete(s_catch); s_catch = nullptr; }   // the frozen screen goes with it
}
static void slide(int32_t to, bool away) {
  lv_anim_delete(s_panel, NULL);
  anim::run(s_panel, placeCb, lv_obj_get_style_translate_y(s_panel, LV_PART_MAIN), to,
            away ? anim::OUT_MS : anim::POP_MS, away ? hidden : nullptr);
}

static void onTile(lv_event_t* e) {
  if (!s_open) return;   // a swipe up that began on it: the panel is going
  s_ui->quickPanelTile((int)(uintptr_t)lv_event_get_user_data(e), lv_event_get_code(e) == LV_EVENT_LONG_PRESSED);
}
static void onCatch(lv_event_t* e) { (void)e; s_ui->quickPanelClose(true); }
static void onPanelGesture(lv_event_t* e) {
  (void)e;
  if (lv_indev_get_gesture_dir(lv_indev_active()) == LV_DIR_TOP) s_ui->quickPanelClose(true);
}
static void onBrightness(lv_event_t* e) {
  lv_obj_t* sl = (lv_obj_t*)lv_event_get_target(e);
  static int32_t s_last = -1;   // the backlight follows the knob, not every event
  const int32_t v = lv_slider_get_value(sl);
  const bool done = lv_event_get_code(e) == LV_EVENT_RELEASED;
  if (v == s_last && !done) return;
  s_last = v;
  s_ui->setBrightnessPct((uint8_t)v, done);
}

static lv_obj_t* tile(lv_obj_t* parent, int t, int w) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, 56);
  lv_obj_set_style_pad_all(b, 2, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_add_event_cb(b, onTile, LV_EVENT_SHORT_CLICKED, (void*)(uintptr_t)t);
  lv_obj_add_event_cb(b, onTile, LV_EVENT_LONG_PRESSED, (void*)(uintptr_t)t);
  s_icon[t] = label(b, "", THEME_FONT_LARGE, theme::TEXT);
  lv_obj_align(s_icon[t], LV_ALIGN_TOP_MID, 0, 5);
  s_cap[t] = label(b, "", THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(s_cap[t], LV_LABEL_LONG_DOT);
  lv_obj_set_style_text_align(s_cap[t], LV_TEXT_ALIGN_CENTER, 0);
  lv_obj_set_style_anim_duration(s_cap[t], 0, 0);
  lv_obj_set_width(s_cap[t], w - 4);
  lv_obj_align(s_cap[t], LV_ALIGN_BOTTOM_MID, 0, -4);
  return b;
}

static void dotsSet(int page) {
  for (int i = 0; i < 2; i++)
    lv_obj_set_style_bg_color(s_dot[i], lv_color_hex(i == page ? theme::TEXT_MUTED : theme::SURFACE_2), 0);
}
static void onPagesScrolled(lv_event_t* e) {
  (void)e;
  dotsSet(lv_obj_get_scroll_x(s_pages) > lv_obj_get_width(s_pages) / 2);
}

// A tile's look: on in the accent with dark text, off on the surface;
// `dim`: nothing to switch on this device.
static void tileSet(int t, const char* icon, const char* cap, bool on, bool dim = false) {
  const uint32_t fg = on ? theme::BG : dim ? theme::TEXT_MUTED : theme::TEXT;
  const uint8_t look = 1 | (on ? 2 : 0);
  if (s_look[t] != look) {   // LVGL restyles, and redraws, on every set (the refresh is each second)
    s_look[t] = look;
    lv_obj_set_style_bg_color(s_tile[t], lv_color_hex(on ? theme::ACCENT : theme::SURFACE), 0);
    lv_obj_set_style_bg_color(s_tile[t], lv_color_hex(on ? theme::mix(theme::ACCENT, theme::BG, 75) : theme::SURFACE_2), LV_STATE_PRESSED);
  }
  setText(s_icon[t], icon);
  setText(s_cap[t], cap);
  setTextColor(s_icon[t], fg);
  setTextColor(s_cap[t], fg);
}

}  // namespace qpanel

// Status bar touches: a drag down pulls the panel after the finger, let go
// past a third it opens, else it goes back; a tap opens or closes it.
static void onStatusBarTouch(lv_event_t* e) {
  using namespace qpanel;
  const lv_event_code_t c = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  if (c == LV_EVENT_PRESSED) { s_y0 = p.y; s_drag = false; return; }
  const int32_t dy = p.y - s_y0;
  if (c == LV_EVENT_PRESSING) {
    if (!s_drag && dy > 6 && !s_open && s_ui->quickPanelBegin()) s_drag = true;
    if (s_drag) place(dy - s_h);
    return;
  }
  if (c != LV_EVENT_RELEASED && c != LV_EVENT_PRESS_LOST) return;
  if (s_drag) {
    s_drag = false;
    if (dy > s_h / 3) { s_open = true; slide(0, false); }
    else s_ui->quickPanelClose(true);
  } else if (c == LV_EVENT_RELEASED && dy < 6) {
    if (s_open) s_ui->quickPanelClose(true);
    else if (s_ui->quickPanelBegin()) { s_open = true; slide(0, false); }
  }
}

// Built the first time it's pulled, on the top layer between the screen and
// the toast and status bar (which stay over it).
void UITask::quickPanelBuild() {
  using namespace qpanel;
  const int32_t w = lv_display_get_horizontal_resolution(NULL);
  s_panel = lv_obj_create(lv_layer_top());
  styleOpaque(s_panel, theme::BG);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_panel, w, LV_SIZE_CONTENT);
  lv_obj_set_pos(s_panel, 0, theme::STATUS_H);
  lv_obj_set_style_radius(s_panel, theme::RADIUS, 0);   // the top corners are under the status bar
  lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(s_panel, 1, 0);
  lv_obj_set_style_border_color(s_panel, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_pad_hor(s_panel, theme::PAD, 0);
  lv_obj_set_style_pad_top(s_panel, theme::GAP, 0);
  lv_obj_set_style_pad_bottom(s_panel, 4, 0);
  lv_obj_set_style_pad_row(s_panel, theme::GAP, 0);
  lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a swipe on it (or a tile) is its own
  lv_obj_add_event_cb(s_panel, onPanelGesture, LV_EVENT_GESTURE, NULL);

  // Two pages of four tiles, a swipe sideways turning one (snapped, one at
  // a time), the dots under them saying which.
  const int32_t pw = w - 2 * theme::PAD;
  s_pages = flexBox(s_panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(s_pages, pw, 56);
  lv_obj_add_flag(s_pages, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(s_pages, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(s_pages, LV_SCROLL_SNAP_START);
  lv_obj_set_scrollbar_mode(s_pages, LV_SCROLLBAR_MODE_OFF);
  lv_obj_remove_flag(s_pages, LV_OBJ_FLAG_SCROLL_ELASTIC);
  lv_obj_add_flag(s_pages, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_set_style_pad_column(s_pages, theme::GAP, 0);
  lv_obj_add_event_cb(s_pages, onPagesScrolled, LV_EVENT_SCROLL_END, NULL);
  const int tw = (pw - (PER_PAGE - 1) * theme::GAP) / PER_PAGE;
  lv_obj_t* page = nullptr;
  for (int t = 0; t < T_COUNT; t++) {
    if (t % PER_PAGE == 0) {
      page = flexBox(s_pages, LV_FLEX_FLOW_ROW);
      lv_obj_set_size(page, pw, 56);
      lv_obj_add_flag(page, LV_OBJ_FLAG_SNAPPABLE);
      lv_obj_set_style_pad_column(page, theme::GAP, 0);
    }
    s_tile[t] = tile(page, t, tw);
    s_look[t] = 0;
  }
  lv_obj_t* dots = flexBox(s_panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(dots, 6, 0);
  for (int i = 0; i < 2; i++) {
    s_dot[i] = lv_obj_create(dots);
    lv_obj_remove_style_all(s_dot[i]);
    lv_obj_remove_flag(s_dot[i], LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(s_dot[i], 6, 6);
    lv_obj_set_style_radius(s_dot[i], LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_opa(s_dot[i], LV_OPA_COVER, 0);
  }
  dotsSet(0);

  lv_obj_t* row = flexBox(s_panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), 30);
  lv_obj_set_style_pad_hor(row, 6, 0);
  lv_obj_set_style_pad_column(row, 12, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(row, UI_SYMBOL_SUN, THEME_FONT_TITLE, theme::TEXT_MUTED);
  s_slider = lv_slider_create(row);
  lv_slider_set_range(s_slider, 5, 100);
  lv_obj_set_height(s_slider, 8);
  lv_obj_set_flex_grow(s_slider, 1);
  lv_obj_set_style_margin_right(s_slider, 8, 0);
  lv_obj_set_ext_click_area(s_slider, 14);
  lv_obj_set_style_anim_duration(s_slider, 0, 0);        // the knob is where the finger is, not on its way
  lv_obj_set_style_shadow_width(s_slider, 0, LV_PART_KNOB);
  lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_SCROLL_CHAIN);
  lv_obj_remove_flag(s_slider, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a drag along it isn't a swipe up
  lv_obj_set_style_bg_color(s_slider, lv_color_hex(theme::mix(theme::ACCENT, theme::SURFACE, 20)), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(s_slider, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_add_event_cb(s_slider, onBrightness, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(s_slider, onBrightness, LV_EVENT_RELEASED, NULL);

  lv_obj_t* grip = lv_obj_create(s_panel);   // the handle: it goes back up
  lv_obj_remove_style_all(grip);
  lv_obj_set_size(grip, 36, 4);
  lv_obj_set_style_radius(grip, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(grip, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_bg_opa(grip, LV_OPA_COVER, 0);

  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(_toast);
  lv_obj_move_foreground(s_status_bar);
}

// Ready to come down, up under the status bar: false when it can't (locked).
bool UITask::quickPanelBegin() {
  using namespace qpanel;
  if (_asleep || locked()) return false;
  if (!s_panel) quickPanelBuild();
  if (_nav_overlay) navClosePopup();
  pickerClose();
  quickPanelRefresh();
  lv_anim_delete(s_panel, NULL);
  if (!s_catch) {   // a still copy of the screen, dimmed, under it: a tap or swipe there closes it
    s_catch = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_catch);
    lv_obj_set_size(s_catch, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_catch, lv_color_hex(0x000000), 0);
    lv_obj_add_flag(s_catch, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_remove_flag(s_catch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_catch, onCatch, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_catch, onPanelGesture, LV_EVENT_GESTURE, NULL);
    lv_obj_move_to_index(s_catch, lv_obj_get_index(s_panel));
    lv_obj_add_event_cb(s_catch, freeze::ownerDeleted, LV_EVENT_DELETE, NULL);
    freeze::early(s_catch, LV_OPA_50);   // no memory, or a popup's still frozen: the screen stays as it is
  }
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_scroll_to_x(s_pages, 0, LV_ANIM_OFF);   // the first page each time
  dotsSet(0);
  layoutNow(s_panel);
  s_h = lv_obj_get_height(s_panel) + theme::STATUS_H;
  place(-s_h);
  return true;
}

void UITask::quickPanelClose(bool animate) {
  using namespace qpanel;
  if (!s_panel || (!s_open && lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))) return;
  s_open = s_drag = false;
  if (animate) { slide(-s_h, true); return; }
  lv_anim_delete(s_panel, NULL);
  hidden(nullptr);
}

bool UITask::quickPanelOpen() const { return qpanel::s_open; }

// The tiles as things are now: on its way down, after a tap, and every second.
void UITask::quickPanelRefresh() {
  using namespace qpanel;
  if (!s_panel) return;
  tileSet(T_BT, LV_SYMBOL_BLUETOOTH, hasConnection() ? "Connected" : "Bluetooth", isSerialEnabled());
  tileSet(T_WIFI, LV_SYMBOL_WIFI, lvport::netRadio() == lvport::NET_UP ? "Online" : "WiFi", lvport::wifiAllowed());
  int32_t lat, lon;
  const bool gps = _core->gpsEnabled();
  tileSet(T_GPS, LV_SYMBOL_GPS, !_core->gpsAvailable() ? "No GPS" : gps && _core->course.currentLocation(lat, lon) ? "GPS fix" : "GPS",
          gps, !_core->gpsAvailable());
#ifdef PIN_BUZZER
  const bool auto_mode = _prefs && soundctl::mode(_prefs) == soundctl::MODE_AUTO;
  tileSet(T_SOUND, _buzzer.isQuiet() ? UI_SYMBOL_MUTE : LV_SYMBOL_VOLUME_MAX,
          _buzzer.isQuiet() ? (auto_mode ? "Auto, silent" : "Muted") : (auto_mode ? "Auto" : "Sound"), !_buzzer.isQuiet());
#else
  tileSet(T_SOUND, UI_SYMBOL_MUTE, "No sound", false, true);
#endif
  tileSet(T_ADVERT, UI_SYMBOL_RADIO, "Advert", _prefs && _prefs->advert_auto_interval_sec > 0);
  tileSet(T_TRAIL, UI_SYMBOL_ROUTE, "Trail", _core->trail.isActive());
  tileSet(T_SHARE, UI_SYMBOL_PIN, "Live share", _prefs && _prefs->loc_share_enabled);
  tileSet(T_LOCK, UI_SYMBOL_LOCK, "Lock", false);
  if (_prefs) {
    const int pct = _prefs->display_brightness_pct ? _prefs->display_brightness_pct
                                                   : (_prefs->display_brightness * 25 > 5 ? _prefs->display_brightness * 25 : 5);
    if (!lv_obj_has_state(s_slider, LV_STATE_PRESSED)) lv_slider_set_value(s_slider, pct, LV_ANIM_OFF);
  }
}

// A tap switches; a hold opens the settings behind the tile (the panel goes).
void UITask::quickPanelTile(int t, bool hold) {
  using namespace qpanel;
  if (hold || t == T_ADVERT || t == T_LOCK) {
    quickPanelClose(false);
    switch (t) {
      case T_BT:     showSettings(); break;   // Connections, at the top
      case T_WIFI:   showWifi(); break;
      case T_GPS:    if (_core->gpsAvailable()) showGps(false); break;
      case T_SOUND:  showSchemaSettings(settings::PG_SOUND); break;
      case T_ADVERT: advertPopup(); break;
      case T_TRAIL:
      case T_SHARE:  openMap(true); navToolsPopup(); break;
      case T_LOCK:   lockScreen(); break;
    }
    return;
  }
  // The tile shows what changed: no toast saying it again.
  switch (t) {
    case T_BT:    setBluetooth(!isSerialEnabled()); break;
    case T_WIFI:  wifiSetAllowed(!lvport::wifiAllowed()); break;
    case T_GPS:   if (_core->gpsAvailable()) setGps(!_core->gpsEnabled()); break;
    case T_SOUND:
#ifdef PIN_BUZZER
      if (_prefs) {   // as the side button's hold: on / off, leaving Auto
        const bool on = _buzzer.isQuiet();
        soundctl::setMode(_prefs, _buzzer, on ? soundctl::MODE_ON : soundctl::MODE_OFF, isClientConnected());
        prefsSave();
        if (on) _buzzer.playForced(soundctl::MEL_VOLUME);
      }
#endif
      break;
    case T_TRAIL:
      _core->trail.setActive(!_core->trail.isActive());
      if (_core->trail.isActive()) ensureGps();   // turns GPS on, or says it's waiting
      break;
    case T_SHARE:
      if (!_prefs) break;
      _prefs->loc_share_enabled = !_prefs->loc_share_enabled;
      if (_prefs->loc_share_enabled) { _core->live_share.restartSession(); ensureGps(); }
      prefsSave();
      break;
  }
  refreshStatusBar();
  quickPanelRefresh();
}
