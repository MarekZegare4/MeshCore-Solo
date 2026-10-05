#pragma once
// Perf tools for ui-lvgl (-D UI_PERF_TEST): draw profiles, per-control timing and
// screen walks. A single-TU fragment, included by UITask.cpp where it used to sit.

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

#if defined(UI_PERF_TEST) && defined(ESP32)
// From loop(): walks through the screens by itself and prints how long each
// display refresh took during the transition that followed (render+flush).
void UITask::perfWalk() {
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
}
#endif
