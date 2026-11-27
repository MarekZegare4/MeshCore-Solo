#pragma once
// Home > Flashlight: the whole screen white at full backlight, over everything
// (the status bar too), kept on while it's up (no auto-off). A tap, the side
// button or the top button puts it away and the brightness back.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after CompassScreen.h.

namespace torch {
static lv_obj_t* s_obj = nullptr;
}  // namespace torch

static void onOpenFlashlight(lv_event_t* e) { (void)e; s_ui->showFlashlight(); }
static void onTorchTap(lv_event_t* e) { (void)e; s_ui->flashlightClose(); }

void UITask::showFlashlight() {
  using namespace torch;
  if (s_obj) return;
  quickPanelClose(false);
  s_obj = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_obj);
  lv_obj_set_size(s_obj, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(s_obj, lv_color_hex(0xFFFFFF), 0);
  lv_obj_set_style_bg_opa(s_obj, LV_OPA_COVER, 0);
  lv_obj_set_clickable(s_obj, true);
  lv_obj_set_gesture_bubble(s_obj, false);
  lv_obj_add_event_cb(s_obj, onTorchTap, LV_EVENT_CLICKED, NULL);
  lv_obj_move_foreground(s_obj);
  lvport::setBacklightPct(100);
}

void UITask::flashlightClose() {
  using namespace torch;
  if (!s_obj) return;
  lv_obj_delete(s_obj);
  s_obj = nullptr;
  applyDisplayPrefs();
}

bool UITask::flashlightOn() const { return torch::s_obj != nullptr; }
