#pragma once
// The quick panel, as a phone's: pulled down from the status bar, over any
// screen -- following the finger, open when let go a third of the way down
// or flicked down from well past the bar; a nudge of a few pixels never. Only a drag opens it: a tap that was the start of a drag
// opened it at once. Tiles that switch at a tap -- Bluetooth, WiFi, GPS,
// sound, the trail, live share -- the advert, the lock, all eight at once in
// two rows, and the brightness under them. Holding a tile opens its settings.
// Swiped up, a tap under it or on the status bar, the side button or another
// screen puts it away. Nothing under it
// takes a touch (a clear layer catches it). The screen under it isn't dimmed
// or frozen: a frozen, dimmed copy took a snapshot of the whole screen as the
// finger started (the panel stalled, then jumped) and sent all of it at once
// (it tore, top to bottom). While it's out the panel is the screen's last
// child, not on the top layer: LVGL only skips drawing what's covered within
// the screen, so over a screen from the top layer every frame of the slide
// drew the screen under it too (the map, Home's minimap) -- the slide
// stuttered, or came down in a few jumps. On the screen it covers its bands
// and only it is drawn. Back on the top layer, hidden, when it's up: a screen
// deleted under it doesn't take it along. Built at start, so the first pull
// doesn't stall either. ui-new has the same things
// as Home pages (Radio, Bluetooth, Advert, GPS).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after QuickScreen.h.

namespace qpanel {

enum : uint8_t { T_BT, T_WIFI, T_GPS, T_SOUND, T_ADVERT, T_TRAIL, T_SHARE, T_LOCK, T_COUNT };
static const int PER_PAGE = 4;

static lv_obj_t* s_panel = nullptr;
static lv_obj_t* s_catch = nullptr;   // under it, while it's out: a tap there closes it (clear)
static uint8_t s_look[T_COUNT];       // what tileSet() last drew (0: nothing yet)
static lv_obj_t* s_tile[T_COUNT];
static lv_obj_t* s_icon[T_COUNT];
static lv_obj_t* s_cap[T_COUNT];
static lv_obj_t* s_slider = nullptr;
static int32_t s_h = 1;               // how far it moves: up to its bottom edge
static bool s_open = false;           // down, or on its way down
static bool s_drag = false;           // following a finger from the status bar
static int32_t s_x0 = 0, s_y0 = 0;
// A flick's speed: the finger sampled every FLICK_MS; at the release it's
// measured from the sample before last (FLICK_MS..2x ago), never over a
// window of a few milliseconds.
static int32_t s_fy = 0, s_fy2 = 0;
static uint32_t s_ft = 0, s_ft2 = 0;
static const uint32_t FLICK_MS = 60;
static const int32_t DRAG_PX = 8;      // down this far, and more down than sideways: the pull starts
static const int32_t GRIP_UNDER = 6;   // the handle's middle above the panel's bottom (pad 4 + half its 4)
static const int32_t FLICK_PX = 40;    // a flick opens it only past this...
static const int32_t FLICK_PX_S = 300; // ...still moving down this fast (px a second)

// Its place: 0 down, -s_h away under the status bar.
static void place(int32_t ty) {
  if (ty > 0) ty = 0;
  if (ty < -s_h) ty = -s_h;
  lv_obj_set_style_translate_y(s_panel, ty, 0);
}
static void hidden(lv_anim_t* a) {
  (void)a;
  lv_obj_add_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
  lv_obj_set_parent(s_panel, lv_layer_top());   // off the screen (see above)
  if (s_catch) { lv_obj_delete(s_catch); s_catch = nullptr; }
  s_sb_panel = false;   // back under the title bar, if the screen has one
  s_ui->statusBarSync();
}
// The slide, timed by its own frames: a frame that took long (the map
// redrawing under it) moves it at most STEP_MS on, where an lv_anim, timed
// by the clock alone, was over by then and the panel just appeared.
static const uint32_t STEP_MS = 30;
static lv_timer_t* s_mover = nullptr;
static int32_t s_from = 0, s_to = 0;
static uint32_t s_t = 0, s_dur = 1, s_last = 0;
static bool s_away = false;
static void moverStop() {
  if (s_mover) { lv_timer_delete(s_mover); s_mover = nullptr; }
}
static void moverTick(lv_timer_t* t) {
  (void)t;
  uint32_t dt = lv_tick_elaps(s_last);
  s_last = lv_tick_get();
  s_t += dt < STEP_MS ? dt : STEP_MS;
  if (s_t >= s_dur) {
    moverStop();
    place(s_to);
    if (s_away) hidden(nullptr);
    return;
  }
  const int32_t k = 1024 - (int32_t)((s_dur - s_t) * 1024 / s_dur);   // 0..1024, then eased out
  const int32_t e = 1024 - (1024 - k) * (1024 - k) / 1024 * (1024 - k) / 1024;
  place(s_from + (s_to - s_from) * e / 1024);
}
static void slide(int32_t to, bool away) {
  moverStop();
  s_from = lv_obj_get_style_translate_y(s_panel, LV_PART_MAIN);
  s_to = to;
  s_away = away;
  s_t = 0;
  s_dur = away ? anim::OUT_MS : anim::POP_MS;
  s_last = lv_tick_get();
  s_mover = lv_timer_create(moverTick, LV_DEF_REFR_PERIOD / 2, NULL);
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
static void onBrightness(lv_event_t* e) { onBrightnessSlider(e); }   // SettingsPages.h

static const int TILE_H = 60;
static lv_obj_t* tile(lv_obj_t* parent, int t, int w) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, w, TILE_H);
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

// A tile's look: on in SEL_BG / SEL_FG (Theme.h), off on the surface;
// `dim`: nothing to switch on this device.
static void tileSet(int t, const char* icon, const char* cap, bool on, bool dim = false) {
  const uint32_t fg = on ? theme::SEL_FG : dim ? theme::TEXT_MUTED : theme::TEXT;
  const uint8_t look = 1 | (on ? 2 : 0);
  if (s_look[t] != look) {   // LVGL restyles, and redraws, on every set (the refresh is each second)
    s_look[t] = look;
    lv_obj_set_style_bg_color(s_tile[t], lv_color_hex(on ? theme::SEL_BG : theme::SURFACE), 0);
    lv_obj_set_style_bg_color(s_tile[t], lv_color_hex(on ? theme::SEL_PRESS : theme::SURFACE_2), LV_STATE_PRESSED);
  }
  setText(s_icon[t], icon);
  setText(s_cap[t], cap);
  setTextColor(s_icon[t], fg);
  setTextColor(s_cap[t], fg);
}

}  // namespace qpanel

// Status bar touches: a drag down pulls the panel after the finger; let go a
// third of the way down, or flicked down past FLICK_PX, it opens, else it
// goes back up. A tap only closes it (opening is the drag's alone).
static void onStatusBarTouch(lv_event_t* e) {
  using namespace qpanel;
  const lv_event_code_t c = lv_event_get_code(e);
  lv_point_t p;
  lv_indev_get_point(lv_indev_active(), &p);
  const uint32_t now = lv_tick_get();
  if (c == LV_EVENT_PRESSED) { s_x0 = p.x; s_y0 = s_fy = s_fy2 = p.y; s_ft = s_ft2 = now; s_drag = false; return; }
  const int32_t dy = p.y - s_y0, dx = p.x - s_x0;
  if (c == LV_EVENT_PRESSING) {
    if (!s_drag && dy > DRAG_PX && dy > (dx < 0 ? -dx : dx) && !s_open && s_ui->quickPanelBegin()) s_drag = true;
    if (s_drag) place(p.y + GRIP_UNDER - s_h);   // the handle under the finger
    if (now - s_ft >= FLICK_MS) { s_fy2 = s_fy; s_ft2 = s_ft; s_fy = p.y; s_ft = now; }
    return;
  }
  if (c != LV_EVENT_RELEASED && c != LV_EVENT_PRESS_LOST) return;
  if (s_drag) {
    s_drag = false;
    const int32_t pulled = p.y + GRIP_UNDER;   // how much of it is down
    const uint32_t dt = now - s_ft2;
    const bool flick = pulled > FLICK_PX && dt > 0 && (p.y - s_fy2) * 1000 / (int32_t)dt > FLICK_PX_S;   // well down, still fast
    if (pulled > s_h / 3 || flick) { s_open = true; slide(0, false); }
    else s_ui->quickPanelClose(true);
  } else if (c == LV_EVENT_RELEASED && dy < 6 && s_open) {
    s_ui->quickPanelClose(true);
  }
}

// Built at start (UITask::begin), hidden, on the top layer between the screen and
// the toast and status bar (which stay over it).
void UITask::quickPanelBuild() {
  using namespace qpanel;
  const int32_t w = lv_display_get_horizontal_resolution(NULL);
  s_panel = lv_obj_create(lv_layer_top());
  styleOpaque(s_panel, theme::BG);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(s_panel, w, LV_SIZE_CONTENT);
  // On a screen while it's out: not in its layout, not scrolled with it.
  lv_obj_add_flag(s_panel, (lv_obj_flag_t)(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING));
  lv_obj_add_event_cb(s_panel, [](lv_event_t*) {   // a screen deleted with it on (no newScreen()): built again
    qpanel::moverStop();
    qpanel::s_panel = nullptr;
    qpanel::s_open = qpanel::s_drag = false;
    s_sb_panel = false;
  }, LV_EVENT_DELETE, NULL);
  // Its top a radius up under the status bar: the rounded top corners are
  // hidden there, not cut out of the panel with the screen showing through.
  lv_obj_set_pos(s_panel, 0, theme::STATUS_H - theme::RADIUS);
  lv_obj_set_style_radius(s_panel, theme::RADIUS, 0);
  lv_obj_set_style_border_side(s_panel, LV_BORDER_SIDE_BOTTOM, 0);
  lv_obj_set_style_border_width(s_panel, 1, 0);
  lv_obj_set_style_border_color(s_panel, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_pad_hor(s_panel, theme::PAD, 0);
  lv_obj_set_style_pad_top(s_panel, theme::RADIUS + theme::GAP, 0);
  lv_obj_set_style_pad_bottom(s_panel, 4, 0);
  lv_obj_set_style_pad_row(s_panel, theme::GAP, 0);
  lv_obj_set_flex_flow(s_panel, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_flex_align(s_panel, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_GESTURE_BUBBLE);   // a swipe on it (or a tile) is its own
  lv_obj_add_event_cb(s_panel, onPanelGesture, LV_EVENT_GESTURE, NULL);

  // All the tiles at once, four to a row.
  const int32_t pw = w - 2 * theme::PAD;
  const int tw = (pw - (PER_PAGE - 1) * theme::GAP) / PER_PAGE;
  lv_obj_t* grid = flexBox(s_panel, LV_FLEX_FLOW_ROW_WRAP);
  lv_obj_set_width(grid, pw);
  lv_obj_set_style_pad_column(grid, theme::GAP, 0);
  lv_obj_set_style_pad_row(grid, theme::GAP, 0);
  for (int t = 0; t < T_COUNT; t++) {
    s_tile[t] = tile(grid, t, tw);
    s_look[t] = 0;
  }

  lv_obj_t* row = flexBox(s_panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_size(row, LV_PCT(100), 40);
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
  moverStop();
  // On the screen, over all of it (see the top of this file), the clear
  // layer under it: a tap or swipe there closes it.
  lv_obj_t* scr = screen();
  if (!s_catch) {
    s_catch = lv_obj_create(scr);
    lv_obj_remove_style_all(s_catch);
    lv_obj_add_flag(s_catch, (lv_obj_flag_t)(LV_OBJ_FLAG_IGNORE_LAYOUT | LV_OBJ_FLAG_FLOATING | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(s_catch, 0, 0);
    lv_obj_set_size(s_catch, LV_PCT(100), LV_PCT(100));
    lv_obj_remove_flag(s_catch, LV_OBJ_FLAG_GESTURE_BUBBLE);
    lv_obj_add_event_cb(s_catch, onCatch, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(s_catch, onPanelGesture, LV_EVENT_GESTURE, NULL);
    lv_obj_add_event_cb(s_catch, [](lv_event_t*) { qpanel::s_catch = nullptr; }, LV_EVENT_DELETE, NULL);
  }
  if (lv_obj_get_parent(s_panel) != scr) lv_obj_set_parent(s_panel, scr);
  lv_obj_move_foreground(s_panel);
  lv_obj_remove_flag(s_panel, LV_OBJ_FLAG_HIDDEN);
  s_sb_panel = true;   // the full status bar over it, a title bar or not
  statusBarSync();
  // Measured where it rests: lv_obj_get_y() counts the translation, and the
  // last close left it a whole height up -- measured there, the next pull
  // got a height of 0 and the panel came down in full at the first pixel.
  lv_obj_set_style_translate_y(s_panel, 0, 0);
  layoutNow(s_panel);
  s_h = lv_obj_get_y(s_panel) + lv_obj_get_height(s_panel);   // its bottom at the top of the screen
  place(-s_h);
  return true;
}

void UITask::quickPanelClose(bool animate) {
  using namespace qpanel;
  if (!s_panel || (!s_open && lv_obj_has_flag(s_panel, LV_OBJ_FLAG_HIDDEN))) return;
  s_open = s_drag = false;
  if (animate) { slide(-s_h, true); return; }
  moverStop();
  hidden(nullptr);
}

bool UITask::quickPanelOpen() const { return qpanel::s_open; }

// The tiles as things are now: on its way down, after a tap, and every second.
void UITask::quickPanelRefresh() {
  using namespace qpanel;
  if (!s_panel) return;
  tileSet(T_BT, LV_SYMBOL_BLUETOOTH, hasConnection() ? "Connected" : "Bluetooth", isBluetoothEnabled());
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
    case T_BT:    setBluetooth(!isBluetoothEnabled()); break;
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
    case T_SHARE: {
      if (!_prefs) break;
      const bool on = t == T_TRAIL ? _core->trail.isActive() : _prefs->loc_share_enabled;
      if (!on && _core->gpsAvailable() && !_core->gpsEnabled()) {   // "GPS is off", over the screen
        quickPanelClose(false);
        gpsAsked(t == T_TRAIL ? GA_TRAIL : GA_SHARE);
        return;
      }
      if (t == T_TRAIL) _core->trail.setActive(!on);
      else { _core->live_share.setEnabled(!on); prefsSave(); }
      break;
    }
  }
  refreshStatusBar();
  quickPanelRefresh();
}
