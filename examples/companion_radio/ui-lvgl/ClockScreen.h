#pragma once
// Clock tools (Home > Clock): alarm, countdown timer, stopwatch and this
// month's calendar -- ui-new's Tools > Clock on a touch screen (the calendar
// as ui-new's e-ink has it under the clock). The alarm (NodePrefs alarm_*) and the
// countdown run in the Core's ClockEngine, so they fire on any screen and while
// the display sleeps; the stopwatch is view state here, as in ui-new. Whatever
// fires brings up a full-screen card with Dismiss (silent: the L2 speaker has
// no driver yet).
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after NavMap.h.

namespace clockview {

enum : uint8_t { TAB_ALARM, TAB_TIMER, TAB_STOPWATCH, TAB_CALENDAR, TAB_COUNT };
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

static struct tm s_cal_tm;    // the day the calendar was drawn for (local)
static uint32_t s_cal_marks = 0;  // its alarm days, bit (day - 1)
static lv_obj_t* s_cal = nullptr;

// The month as a grid, weeks from Monday: the weekday names, today in an
// accent box, the alarm's days with an accent bar under them, the weekend
// dimmer.
static void onCalendarDraw(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_target(e);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  const struct tm& t = s_cal_tm;
  static const char* const NAMES[] = { "Mo", "Tu", "We", "Th", "Fr", "Sa", "Su" };
  const int W = a.x2 - a.x1 + 1, cw = W / 7, x0 = a.x1 + (W - 7 * cw) / 2;
  const int head = 16, rows = calmath::weeks(t), ch = (a.y2 - a.y1 + 1 - head) / rows;
  lv_draw_label_dsc_t td;
  lv_draw_label_dsc_init(&td);
  td.align = LV_TEXT_ALIGN_CENTER;
  td.font = THEME_FONT_SMALL;
  for (int c = 0; c < 7; c++) {
    td.color = lv_color_hex(theme::TEXT_MUTED);
    td.text = NAMES[c];
    lv_area_t la = { x0 + c * cw, a.y1, x0 + (c + 1) * cw - 1, a.y1 + head - 1 };
    lv_draw_label(layer, &td, &la);
  }
  td.font = THEME_FONT_BODY;
  const int fh = lv_font_get_line_height(td.font), col0 = calmath::firstCol(t), n = calmath::daysIn(t.tm_year, t.tm_mon);
  char buf[3];
  for (int day = 1; day <= n; day++) {
    const int k = col0 + day - 1, cx = x0 + (k % 7) * cw, cy = a.y1 + head + (k / 7) * ch;
    const bool today = day == t.tm_mday;
    if (today) {
      lv_draw_rect_dsc_t rd;
      lv_draw_rect_dsc_init(&rd);
      rd.radius = theme::RADIUS_SM;
      rd.bg_color = lv_color_hex(theme::ACCENT);
      const int bh = ch - 2 < fh + 2 ? ch - 2 : fh + 2, by = cy + (ch - bh) / 2;
      lv_area_t ra = { cx + 4, by, cx + cw - 5, by + bh - 1 };
      lv_draw_rect(layer, &rd, &ra);
    }
    snprintf(buf, sizeof(buf), "%d", day);
    td.text = buf;
    td.color = lv_color_hex(today ? theme::BG : k % 7 >= 5 ? theme::TEXT_MUTED : theme::TEXT);
    lv_area_t la = { cx, cy + (ch - fh) / 2, cx + cw - 1, cy + (ch + fh) / 2 };
    lv_draw_label(layer, &td, &la);
    if (s_cal_marks & (1UL << (day - 1))) {   // under the number
      lv_draw_rect_dsc_t md;
      lv_draw_rect_dsc_init(&md);
      md.radius = 1;
      md.bg_color = lv_color_hex(today ? theme::BG : theme::ACCENT);
      lv_area_t ma = { cx + cw / 2 - 6, cy + ch - 2, cx + cw / 2 + 6, cy + ch - 1 };
      lv_draw_rect(layer, &md, &ma);
    }
  }
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

// A number wheel, 0..n-1 in a loop with the middle row selected: an infinite
// lv_roller's look and touch, drawn by hand. lv_roller measures its whole
// option text (repeated three times) on every style change, layout pass and
// frame: some 40 of the ~100 ms this screen took to open went on that.
struct Wheel {
  uint8_t n;
  int32_t pos;     // the selected row, in 1/256 rows: dragging moves it between rows
  int32_t vel;     // the finger's recent speed, px per input read (smoothed)
  int32_t drag;    // px the finger has moved since it went down
};
static const int32_t WHEEL_LINE = 4;   // between rows
static char s_nums[60][3];             // "00".."59"

static int32_t wheelPitch() { return lv_font_get_line_height(THEME_FONT_TITLE) + WHEEL_LINE; }
static Wheel* wheelOf(lv_obj_t* o) { return (Wheel*)lv_obj_get_user_data(o); }
static int wheelValue(lv_obj_t* o) {
  const Wheel* w = wheelOf(o);
  int32_t r = (w->pos + 128) >> 8;
  return (int)(((r % w->n) + w->n) % w->n);
}

static void wheelAnim(void* o, int32_t v) {
  wheelOf((lv_obj_t*)o)->pos = v;
  lv_obj_invalidate((lv_obj_t*)o);
}
static void wheelAnimDone(lv_anim_t* a) {   // back to the first loop, so pos never grows
  Wheel* w = wheelOf((lv_obj_t*)a->var);
  w->pos = (int32_t)wheelValue((lv_obj_t*)a->var) << 8;
}
// Rolls to row `to` (whole rows from where it is now) and says so.
static void wheelGo(lv_obj_t* o, int32_t to) {
  Wheel* w = wheelOf(o);
  int32_t from = w->pos, end = to * 256;
  lv_anim_delete(o, wheelAnim);
  lv_anim_t a;
  lv_anim_init(&a);
  lv_anim_set_var(&a, o);
  lv_anim_set_exec_cb(&a, wheelAnim);
  lv_anim_set_values(&a, from, end);
  int32_t rows = LV_ABS(end - from) >> 8;
  lv_anim_set_duration(&a, LV_MIN(200 + rows * 20, 600));
  lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
  lv_anim_set_completed_cb(&a, wheelAnimDone);
  w->pos = end;   // the value is the target already: read by the event below
  lv_obj_send_event(o, LV_EVENT_VALUE_CHANGED, NULL);
  w->pos = from;
  lv_anim_start(&a);
}

static void wheelDraw(lv_event_t* e) {
  lv_obj_t* o = lv_event_get_current_target_obj(e);
  const Wheel* w = wheelOf(o);
  lv_layer_t* layer = lv_event_get_layer(e);
  lv_area_t a;
  lv_obj_get_coords(o, &a);
  int32_t pitch = wheelPitch(), mid = (a.y1 + a.y2 + 1) / 2;
  int32_t h_main = lv_font_get_line_height(THEME_FONT_TITLE), h_sel = lv_font_get_line_height(THEME_FONT_LARGE);
  int32_t band_h = (h_main + h_sel) / 2 + WHEEL_LINE;
  lv_area_t band = { a.x1, mid - band_h / 2, a.x2, mid - band_h / 2 + band_h - 1 };

  lv_draw_rect_dsc_t rd;
  lv_draw_rect_dsc_init(&rd);
  rd.bg_color = lv_color_hex(theme::SURFACE_2);
  lv_draw_rect(layer, &rd, &band);

  // Above the band, the band, below it: the band's rows in the larger font.
  const lv_area_t clips[3] = { { a.x1, a.y1, a.x2, band.y1 - 1 }, band, { a.x1, band.y2 + 1, a.x2, a.y2 } };
  const lv_area_t clip_ori = layer->_clip_area;
  int32_t base = w->pos >> 8, frac = w->pos & 255;
  int32_t span = lv_area_get_height(&a) / 2 / pitch + 2;
  for (int c = 0; c < 3; c++) {
    if (!lv_area_intersect(&layer->_clip_area, &clip_ori, &clips[c])) continue;
    bool sel = c == 1;
    lv_draw_label_dsc_t td;
    lv_draw_label_dsc_init(&td);
    td.font = sel ? THEME_FONT_LARGE : THEME_FONT_TITLE;
    td.color = lv_color_hex(sel ? theme::ACCENT : theme::TEXT_MUTED);
    td.align = LV_TEXT_ALIGN_CENTER;
    int32_t fh = sel ? h_sel : h_main;
    for (int32_t k = -span; k <= span; k++) {
      int32_t y = mid + ((k << 8) - frac) * pitch / 256;   // the row's middle
      if (y + fh < layer->_clip_area.y1 || y - fh > layer->_clip_area.y2) continue;
      td.text = s_nums[(((base + k) % w->n) + w->n) % w->n];
      lv_area_t ta = { a.x1, y - fh / 2, a.x2, y - fh / 2 + fh - 1 };
      lv_draw_label(layer, &td, &ta);
    }
  }
  layer->_clip_area = clip_ori;
}

static void wheelEvent(lv_event_t* e) {
  lv_obj_t* o = lv_event_get_current_target_obj(e);
  Wheel* w = wheelOf(o);
  lv_indev_t* indev = lv_indev_active();
  switch (lv_event_get_code(e)) {
    case LV_EVENT_PRESSED:
      lv_anim_delete(o, wheelAnim);
      w->vel = w->drag = 0;
      break;
    case LV_EVENT_PRESSING: {
      lv_point_t v = { 0, 0 };
      if (indev) lv_indev_get_vect(indev, &v);
      w->vel = (w->vel + v.y) / 2;
      w->drag += LV_ABS(v.y);
      if (v.y) {
        w->pos -= v.y * 256 / wheelPitch();
        lv_obj_invalidate(o);
      }
      break;
    }
    case LV_EVENT_RELEASED:
    case LV_EVENT_PRESS_LOST: {
      int32_t pitch = wheelPitch(), to;
      if (w->drag > 4) {   // a throw carries on for ~10 reads' worth (as lv_roller's), then the nearest row
        to = (w->pos - w->vel * 10 * 256 / pitch + 128) >> 8;
      } else {             // a tap: that row to the middle
        lv_point_t p = { 0, 0 };
        if (indev) lv_indev_get_point(indev, &p);
        lv_area_t a;
        lv_obj_get_coords(o, &a);
        int32_t dy = p.y - (a.y1 + a.y2 + 1) / 2;
        to = ((w->pos + 128) >> 8) + (dy + (dy < 0 ? -pitch / 2 : pitch / 2)) / pitch;
      }
      wheelGo(o, to);
      break;
    }
    case LV_EVENT_DELETE:
      lv_anim_delete(o, wheelAnim);
      lv_free(w);
      break;
    default: break;
  }
}

static lv_obj_t* wheel(lv_obj_t* parent, uint8_t n, int sel, lv_event_cb_t cb, uintptr_t which) {
  if (!s_nums[0][0]) for (int i = 0; i < 60; i++) snprintf(s_nums[i], sizeof(s_nums[i]), "%02d", i);
  Wheel* w = (Wheel*)lv_malloc(sizeof(Wheel));
  *w = { n, (int32_t)(sel % n) * 256, 0, 0 };
  lv_obj_t* o = lv_obj_create(parent);
  styleSurface(o, theme::SURFACE);
  lv_obj_set_style_radius(o, theme::RADIUS_SM, 0);
  lv_obj_remove_flag(o, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_SCROLL_CHAIN));
  lv_obj_set_size(o, 60, 84);   // three rows; leaves room for the button below
  lv_obj_set_user_data(o, w);
  lv_obj_add_event_cb(o, wheelDraw, LV_EVENT_DRAW_MAIN, NULL);
  lv_obj_add_event_cb(o, wheelEvent, LV_EVENT_ALL, NULL);
  lv_obj_add_event_cb(o, cb, LV_EVENT_VALUE_CHANGED, (void*)which);
  return o;
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

// Alarm: hour / minute wheels, on switch, repeat. Saved on every change.
static void onAlarmWheel(lv_event_t* e) {
  s_ui->setAlarm((int)(uintptr_t)lv_event_get_user_data(e), clockview::wheelValue((lv_obj_t*)lv_event_get_target(e)));
}
static void onAlarmSwitch(lv_event_t* e) {
  s_ui->setAlarm(2, lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED) ? 1 : 0);
}
static void onAlarmRepeat(lv_event_t* e) {
  s_ui->setAlarm(3, choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onTimerWheel(lv_event_t* e) {
  int v = clockview::wheelValue((lv_obj_t*)lv_event_get_target(e));
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
  lv_obj_t* body = newScreen("Clock", true);
  lv_obj_set_style_pad_row(body, 6, 0);
  lv_obj_remove_flag(body, LV_OBJ_FLAG_SCROLLABLE);
  s_big = s_go_lbl = s_alarm_sw = s_cal = nullptr;

  // Tabs
  lv_obj_t* tabs = row(body);
  static const char* const NAMES[TAB_COUNT] = { "Alarm", "Timer", "Stopwatch", "Calendar" };
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
    wheel(r, 24, _prefs->alarm_hour % 24, onAlarmWheel, 0);
    colon(r);
    wheel(r, 60, _prefs->alarm_min % 60, onAlarmWheel, 1);
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

  if (s_tab == TAB_CALENDAR) {
    struct tm t;
    bool synced = localTime(_prefs, t);
    if (!synced) {
      label(body, "The clock isn't set yet: no date to show.", THEME_FONT_BODY, theme::TEXT_MUTED);
      return;
    }
    static const char* const LONG_MONTHS[] = { "January", "February", "March", "April", "May", "June", "July",
                                               "August", "September", "October", "November", "December" };
    char title[24];   // the month on the header's right: the grid gets the height
    snprintf(title, sizeof(title), "%s %d", LONG_MONTHS[t.tm_mon], t.tm_year + 1900);
    if (_header) lv_obj_align(label(_header, title, THEME_FONT_TITLE, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
    s_cal_tm = t;
    s_cal_marks = calmath::alarmDays(t, _prefs->alarm_on, _prefs->alarm_repeat_mask, _prefs->alarm_hour, _prefs->alarm_min);
    s_cal = lv_obj_create(body);
    lv_obj_remove_style_all(s_cal);
    lv_obj_remove_flag(s_cal, LV_OBJ_FLAG_CLICKABLE);
    fillWidth(s_cal);
    lv_obj_set_flex_grow(s_cal, 1);
    lv_obj_add_event_cb(s_cal, onCalendarDraw, LV_EVENT_DRAW_MAIN_END, NULL);
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
      wheel(r, 24, s_timer_h, onTimerWheel, 0);
      colon(r);
      wheel(r, 60, s_timer_m, onTimerWheel, 1);
      colon(r);
      wheel(r, 60, s_timer_s, onTimerWheel, 2);
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
  if (s_tab == TAB_CALENDAR) {   // past midnight, or the alarm changed: a new day to box or mark
    struct tm t;
    if (s_cal && localTime(_prefs, t)
        && (t.tm_mday != s_cal_tm.tm_mday || t.tm_mon != s_cal_tm.tm_mon
            || calmath::alarmDays(t, _prefs->alarm_on, _prefs->alarm_repeat_mask, _prefs->alarm_hour, _prefs->alarm_min) != s_cal_marks))
      buildClock();
    return;
  }
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
