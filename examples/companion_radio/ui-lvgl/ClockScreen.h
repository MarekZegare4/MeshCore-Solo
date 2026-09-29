#pragma once
// Clock tools (Home > Clock): alarm, countdown timer and stopwatch -- ui-new's
// Tools > Clock on a touch screen. The alarm (NodePrefs alarm_*) and the
// countdown run in the Core's ClockEngine, so they fire on any screen and while
// the display sleeps; the stopwatch is view state here, as in ui-new. Whatever
// fires brings up a full-screen card with Dismiss (silent: the L2 speaker has
// no driver yet).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h.

namespace clockview {

enum : uint8_t { TAB_ALARM, TAB_TIMER, TAB_STOPWATCH, TAB_COUNT };
enum : uint8_t { ACT_START_STOP, ACT_RESET };

static uint8_t   s_tab = TAB_ALARM;
static uint8_t   s_timer_h = 0, s_timer_m = 5, s_timer_s = 0;   // countdown to start
static bool      s_sw_running = false;
static uint32_t  s_sw_start = 0, s_sw_accum = 0;
static bool      s_shown_running = false;   // timer state the screen was built for
static lv_obj_t* s_big = nullptr;           // timer remaining / stopwatch reading
static lv_obj_t* s_go_lbl = nullptr;        // Start / Stop
static lv_obj_t* s_ring = nullptr;          // "Timer done" card on the top layer
static lv_obj_t* s_ring_lbl = nullptr;
static lv_obj_t* s_alarm_sw = nullptr;     // follows a time change (setting it arms the alarm)

static char s_opts24[24 * 3], s_opts60[60 * 3];   // "00\n01\n..." for the rollers
static void buildOptions() {
  if (s_opts24[0]) return;
  int o = 0;
  for (int i = 0; i < 24; i++) o += snprintf(s_opts24 + o, sizeof(s_opts24) - o, i ? "\n%02d" : "%02d", i);
  o = 0;
  for (int i = 0; i < 60; i++) o += snprintf(s_opts60 + o, sizeof(s_opts60) - o, i ? "\n%02d" : "%02d", i);
}

static uint32_t swElapsed() { return s_sw_accum + (s_sw_running ? millis() - s_sw_start : 0); }

// Countdown: rounds up, so "00:01" shows until it actually fires.
static void fmtRemaining(char* b, int n, uint32_t ms) {
  uint32_t t = (ms + 999) / 1000;
  if (t >= 3600) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  else snprintf(b, n, "%02lu:%02lu", (unsigned long)(t / 60), (unsigned long)(t % 60));
}
static void fmtStopwatch(char* b, int n, uint32_t ms) {
  uint32_t t = ms / 1000;
  if (t >= 3600) snprintf(b, n, "%lu:%02lu:%02lu", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60), (unsigned long)(t % 60));
  else snprintf(b, n, "%02lu:%02lu.%lu", (unsigned long)(t / 60), (unsigned long)(t % 60), (unsigned long)(ms / 100 % 10));
}

static lv_obj_t* roller(lv_obj_t* parent, const char* opts, int sel, lv_event_cb_t cb, uintptr_t which) {
  lv_obj_t* r = lv_roller_create(parent);
  lv_roller_set_options(r, opts, LV_ROLLER_MODE_INFINITE);
  lv_roller_set_selected(r, sel, LV_ANIM_OFF);
  lv_obj_set_size(r, 60, 84);   // three rows; leaves room for the button below
  lv_obj_set_style_text_font(r, THEME_FONT_TITLE, 0);
  lv_obj_set_style_text_font(r, THEME_FONT_LARGE, LV_PART_SELECTED);
  lv_obj_set_style_text_line_space(r, 4, 0);
  lv_obj_set_style_text_color(r, lv_color_hex(theme::TEXT_MUTED), 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_border_width(r, 0, 0);
  lv_obj_set_style_bg_color(r, lv_color_hex(theme::SURFACE_2), LV_PART_SELECTED);
  lv_obj_set_style_text_color(r, lv_color_hex(theme::ACCENT), LV_PART_SELECTED);
  lv_obj_add_event_cb(r, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return r;
}

static lv_obj_t* colon(lv_obj_t* parent) { return label(parent, ":", THEME_FONT_LARGE, theme::TEXT_MUTED); }

static lv_obj_t* row(lv_obj_t* parent) {
  lv_obj_t* r = lv_obj_create(parent);
  styleSurface(r, theme::BG);
  lv_obj_remove_flag(r, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(r, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(r, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(r, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(r, theme::GAP, 0);
  return r;
}

}  // namespace clockview

static void onOpenClock(lv_event_t* e)   { (void)e; s_ui->showClock(); }
static void onClockTab(lv_event_t* e)    { s_ui->clockTab((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onClockAct(lv_event_t* e)    { s_ui->clockAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onRingDismiss(lv_event_t* e) { (void)e; s_ui->dismissRing(); }

// Alarm: hour / minute rollers, on switch, repeat. Saved on every change.
static void onAlarmRoller(lv_event_t* e) {
  s_ui->setAlarm((int)(uintptr_t)lv_event_get_user_data(e), (int)lv_roller_get_selected((lv_obj_t*)lv_event_get_target(e)));
}
static void onAlarmSwitch(lv_event_t* e) {
  s_ui->setAlarm(2, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onAlarmRepeat(lv_event_t* e) {
  s_ui->setAlarm(3, choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onTimerRoller(lv_event_t* e) {
  int v = (int)lv_roller_get_selected((lv_obj_t*)lv_event_get_target(e));
  switch ((uintptr_t)lv_event_get_user_data(e)) {
    case 0: clockview::s_timer_h = (uint8_t)v; break;
    case 1: clockview::s_timer_m = (uint8_t)v; break;
    default: clockview::s_timer_s = (uint8_t)v; break;
  }
}

void UITask::showClock() {
  _screen = SCR_CLOCK;
  buildClock();
}

void UITask::clockTab(int tab) {
  if (tab < 0 || tab >= clockview::TAB_COUNT) return;
  clockview::s_tab = (uint8_t)tab;
  buildClock();
}

void UITask::buildClock() {
  using namespace clockview;
  buildOptions();
  lv_obj_t* body = newScreen("Clock", true);
  lv_obj_set_style_pad_row(body, 6, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  s_big = s_go_lbl = s_alarm_sw = nullptr;

  // Tabs
  lv_obj_t* tabs = row(body);
  static const char* const NAMES[TAB_COUNT] = { LV_SYMBOL_BELL " Alarm", UI_SYMBOL_CLOCK " Timer",
                                                UI_SYMBOL_STOPWATCH " Stopwatch" };
  for (int t = 0; t < TAB_COUNT; t++) {
    lv_obj_t* b = lv_button_create(tabs);
    lv_obj_set_height(b, 32);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(t == s_tab ? theme::ACCENT_DIM : theme::SURFACE), 0);
    lv_obj_add_event_cb(b, onClockTab, LV_EVENT_CLICKED, (void*)(uintptr_t)t);
    lv_obj_center(label(b, NAMES[t], THEME_FONT_SMALL, theme::TEXT));
  }

  if (s_tab == TAB_ALARM) {
    lv_obj_t* r = row(body);
    roller(r, s_opts24, _prefs->alarm_hour % 24, onAlarmRoller, 0);
    colon(r);
    roller(r, s_opts60, _prefs->alarm_min % 60, onAlarmRoller, 1);
    lv_obj_t* col = lv_obj_create(r);
    styleSurface(col, theme::BG);
    lv_obj_remove_flag(col, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(col, 120, LV_SIZE_CONTENT);
    lv_obj_set_flex_flow(col, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(col, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_START);
    lv_obj_set_style_pad_row(col, 4, 0);
    lv_obj_set_style_pad_left(col, 8, 0);
    lv_obj_t* on = flexBox(col, LV_FLEX_FLOW_ROW);   // the switch and its label: one line
    lv_obj_set_flex_align(on, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(on, 8, 0);
    lv_obj_set_style_pad_bottom(on, 4, 0);
    lv_obj_t* sw = s_alarm_sw = lv_switch_create(on);
    label(on, "Alarm on", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_set_size(sw, 50, 26);
    if (_prefs->alarm_on) lv_obj_add_state(sw, LV_STATE_CHECKED);
    lv_obj_add_event_cb(sw, onAlarmSwitch, LV_EVENT_VALUE_CHANGED, NULL);
    label(col, "Repeat", THEME_FONT_SMALL, theme::TEXT_MUTED);
    char opts[48];
    int o = 0;
    for (uint8_t i = 0; i < NodePrefs::ALARM_REPEAT_COUNT; i++)
      o += snprintf(opts + o, sizeof(opts) - o, "%s%s", i ? "\n" : "", i ? NodePrefs::alarmRepeatLabel(i) : "Once");
    lv_obj_t* dd = choiceCreate(col, opts, NodePrefs::alarmRepeatIdxForMask(_prefs->alarm_repeat_mask), "Repeat");
    lv_obj_set_width(dd, 112);
    lv_obj_add_event_cb(dd, onAlarmRepeat, LV_EVENT_VALUE_CHANGED, NULL);
    bool synced = rtc_clock.getCurrentTime() > 1000000000UL;
    label(body, synced ? "Rings at this local time (Settings time zone)." : "The clock isn't set yet: the alarm waits for a time sync.",
          THEME_FONT_SMALL, theme::TEXT_MUTED);
    return;
  }

  if (s_tab == TAB_TIMER) {
    s_shown_running = _core->clock.isTimerRunning();
    if (s_shown_running) {
      s_big = label(body, "", THEME_FONT_CLOCK, theme::TEXT);
      lv_obj_set_width(s_big, LV_PCT(100));
      lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
      lv_obj_set_style_pad_ver(s_big, 18, 0);
    } else {
      lv_obj_t* r = row(body);
      roller(r, s_opts24, s_timer_h, onTimerRoller, 0);
      colon(r);
      roller(r, s_opts60, s_timer_m, onTimerRoller, 1);
      colon(r);
      roller(r, s_opts60, s_timer_s, onTimerRoller, 2);
    }
  } else {   // stopwatch
    s_big = label(body, "", THEME_FONT_CLOCK, theme::TEXT);
    lv_obj_set_width(s_big, LV_PCT(100));
    lv_obj_set_style_text_align(s_big, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_ver(s_big, 18, 0);
  }

  lv_obj_t* acts = row(body);
  lv_obj_t* go = lv_button_create(acts);
  lv_obj_set_size(go, 150, 40);
  lv_obj_set_style_shadow_width(go, 0, 0);
  lv_obj_set_style_radius(go, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(go, lv_color_hex(theme::ACCENT_DIM), 0);
  lv_obj_add_event_cb(go, onClockAct, LV_EVENT_CLICKED, (void*)(uintptr_t)ACT_START_STOP);
  s_go_lbl = label(go, "", THEME_FONT_BODY, theme::TEXT);
  lv_obj_center(s_go_lbl);
  stylePrimary(go);
  if (s_tab == TAB_STOPWATCH) {
    lv_obj_t* rs = lv_button_create(acts);
    lv_obj_set_size(rs, 100, 40);
    lv_obj_set_style_shadow_width(rs, 0, 0);
    lv_obj_set_style_radius(rs, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(rs, lv_color_hex(theme::SURFACE), 0);
    lv_obj_add_event_cb(rs, onClockAct, LV_EVENT_CLICKED, (void*)(uintptr_t)ACT_RESET);
    lv_obj_center(label(rs, LV_SYMBOL_REFRESH " Reset", THEME_FONT_BODY, theme::TEXT));
  }
  refreshClock();
}

// From loop() while SCR_CLOCK is shown: the running readouts.
void UITask::refreshClock() {
  using namespace clockview;
  if (s_tab == TAB_TIMER) {
    bool running = _core->clock.isTimerRunning();
    if (running != s_shown_running) { buildClock(); return; }   // started elsewhere / just fired
    if (s_big) {
      char b[16];
      fmtRemaining(b, sizeof(b), _core->clock.timerRemainingMs());
      setText(s_big, b);
    }
    if (s_go_lbl) setText(s_go_lbl, running ? LV_SYMBOL_STOP " Stop" : LV_SYMBOL_PLAY " Start");
  } else if (s_tab == TAB_STOPWATCH) {
    if (s_big) {
      char b[16];
      fmtStopwatch(b, sizeof(b), swElapsed());
      setText(s_big, b);
    }
    if (s_go_lbl) setText(s_go_lbl, s_sw_running ? LV_SYMBOL_PAUSE " Stop" : LV_SYMBOL_PLAY " Start");
  }
}

void UITask::clockAction(uint8_t act) {
  using namespace clockview;
  if (s_tab == TAB_TIMER) {
    if (_core->clock.isTimerRunning()) _core->clock.stopTimer();
    else {
      uint32_t ms = ((uint32_t)s_timer_h * 3600 + s_timer_m * 60 + s_timer_s) * 1000UL;
      if (ms == 0) { showToast("Set a time first"); return; }
      _core->clock.startTimer(ms);
    }
    buildClock();
  } else if (s_tab == TAB_STOPWATCH) {
    if (act == ACT_RESET) { s_sw_accum = 0; s_sw_start = millis(); }
    else if (s_sw_running) { s_sw_accum += millis() - s_sw_start; s_sw_running = false; }
    else { s_sw_start = millis(); s_sw_running = true; }
    refreshClock();
  }
}

// which: 0 hour, 1 minute, 2 on/off, 3 repeat preset.
void UITask::setAlarm(int which, int v) {
  if (!_prefs) return;
  switch (which) {
    case 0: _prefs->alarm_hour = (uint8_t)v; _prefs->alarm_on = 1; break;   // setting a time arms it
    case 1: _prefs->alarm_min = (uint8_t)v;  _prefs->alarm_on = 1; break;
    case 2: _prefs->alarm_on = (uint8_t)v; break;
    case 3: _prefs->alarm_repeat_mask = NodePrefs::alarmRepeatMaskForIdx((uint8_t)v); break;
  }
  _core->clock.onAlarmChanged();
  prefsSave();
  if (which < 2 && clockview::s_alarm_sw) lv_obj_add_state(clockview::s_alarm_sw, LV_STATE_CHECKED);
}

// ── Ring card (any screen) ────────────────────────────────────────────────────

void UITask::showRing(const char* text) {
  using namespace clockview;
  wake();
  if (!s_ring) {
    s_ring = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_ring);
    lv_obj_set_size(s_ring, LV_PCT(100), LV_PCT(100));
    lv_obj_set_style_bg_color(s_ring, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_bg_opa(s_ring, LV_OPA_COVER, 0);
    lv_obj_add_flag(s_ring, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_flex_flow(s_ring, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(s_ring, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_row(s_ring, 14, 0);
    label(s_ring, LV_SYMBOL_BELL, THEME_FONT_LARGE, theme::ACCENT);
    s_ring_lbl = label(s_ring, "", THEME_FONT_LARGE, theme::TEXT);
    lv_obj_t* b = lv_button_create(s_ring);
    lv_obj_set_size(b, 200, 56);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::ACCENT_DIM), 0);
    lv_obj_add_event_cb(b, onRingDismiss, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(b, "Dismiss", THEME_FONT_LARGE, theme::TEXT));
    stylePrimary(b);
  }
  lv_label_set_text(s_ring_lbl, text);
  lv_obj_remove_flag(s_ring, LV_OBJ_FLAG_HIDDEN);
  lv_obj_move_foreground(s_ring);   // over the lock screen too
}

void UITask::dismissRing() {
  stopMelody();
  _core->clock.dismissRing();
  hideRing();
}

void UITask::hideRing() {
  if (clockview::s_ring) lv_obj_add_flag(clockview::s_ring, LV_OBJ_FLAG_HIDDEN);
  if (_screen == SCR_CLOCK) buildClock();   // e.g. the timer's Start again
}
