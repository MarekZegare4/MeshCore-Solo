#pragma once
// Light motion for ui-lvgl: screens emerge from the middle, popups and toasts
// rise and fade in, Home pages slide after a swipe (buttons giving under the
// finger is a Theme.h style). Short (under 200 ms) and eased, so nothing waits
// on them.

namespace anim {
  static const uint32_t SCREEN_MS = 180;   // screen change
  static const uint32_t POP_MS    = 160;   // popup / toast in
  static const uint32_t OUT_MS    = 120;   // toast out
  static const uint32_t PAGE_MS   = 160;   // Home page after a swipe
  static const uint32_t UNLOCK_MS = 260;   // lock screen away
  static const uint32_t SPRING_MS = 200;   // a let-go slider knob back home
  static const uint32_t FADE_MS   = 280;   // the side button's cross-fade to Home

  static void setTy(void* o, int32_t v)  { lv_obj_set_style_translate_y((lv_obj_t*)o, v, 0); }
  static void setTx(void* o, int32_t v)  { lv_obj_set_style_translate_x((lv_obj_t*)o, v, 0); }
  static void setOpa(void* o, int32_t v) { lv_obj_set_style_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
  static void setBgOpa(void* o, int32_t v) { lv_obj_set_style_bg_opa((lv_obj_t*)o, (lv_opa_t)v, 0); }
  static void setSlider(void* o, int32_t v) { lv_slider_set_value((lv_obj_t*)o, v, LV_ANIM_OFF); }

  static void run(lv_obj_t* o, lv_anim_exec_xcb_t cb, int32_t from, int32_t to, uint32_t ms,
                  lv_anim_completed_cb_t done = nullptr) {
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, o);
    lv_anim_set_exec_cb(&a, cb);
    lv_anim_set_values(&a, from, to);
    lv_anim_set_duration(&a, ms);
    lv_anim_set_path_cb(&a, lv_anim_path_ease_out);
    if (done) lv_anim_set_completed_cb(&a, done);
    lv_anim_start(&a);
    cb(o, from);   // no frame of the end state before the first tick
  }

  // Rise by dy and fade in (a popup's panel, a toast).
  static void rise(lv_obj_t* o, int32_t dy = 16, uint32_t ms = POP_MS) {
    run(o, setTy, dy, 0, ms);
    run(o, setOpa, LV_OPA_TRANSP, LV_OPA_COVER, ms);
  }

  // A popup: the dimmed backdrop fades up to `dim`, its first child (the
  // panel) rises; `done` runs once it's up.
  static void popup(lv_obj_t* overlay, lv_opa_t dim = LV_OPA_60, lv_anim_completed_cb_t done = nullptr) {
    run(overlay, setBgOpa, LV_OPA_TRANSP, dim, POP_MS, done);
    lv_obj_t* panel = lv_obj_get_child(overlay, 0);
    if (panel) rise(panel);
  }

  // A fading cover's end: it goes.
  static void coverDone(lv_anim_t* a) { lv_obj_delete((lv_obj_t*)a->var); }

  // A new screen emerges: its content drifts a few pixels into place (up
  // going deeper, down coming back). No fade: a cover fading over the screen
  // blended a full-screen fill into every frame of it (~7 ms each on the L2).
  static void screenIn(lv_obj_t* body, bool back) {
    if (body) run(body, setTy, back ? -6 : 6, 0, SCREEN_MS);
  }

  // Content that replaced other content in place: slides in from dx.
  static void slideIn(lv_obj_t* o, int32_t dx, uint32_t ms = PAGE_MS) {
    run(o, setTx, dx, 0, ms);
    run(o, setOpa, LV_OPA_40, LV_OPA_COVER, ms);
  }

  // Content replaced in place without a direction: fades up from nothing.
  static void fadeIn(lv_obj_t* o, uint32_t ms = FADE_MS) {
    lv_anim_delete(o, NULL);   // no slide left over from a swipe
    lv_obj_set_style_translate_x(o, 0, 0);
    run(o, setOpa, LV_OPA_TRANSP, LV_OPA_COVER, ms);
  }
}
