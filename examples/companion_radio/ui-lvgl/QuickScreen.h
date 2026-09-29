// Quick messages and placeholders (the compose bar's "+", Settings > Messages
// & contacts > Quick messages), Settings > Send advert and > Bluetooth, and
// Messages' "Read all". The text side is ui-core/MessageText.h.
// Included by UITask.cpp (single translation unit).

namespace qview {

static const int PH_MAX = 12;
static const char* s_ph[PH_MAX];   // placeholders on offer (static strings)
static int s_ph_n = 0;
static int s_edit = -1;            // quick message being edited

static void collect() {
  s_ph_n = 0;
  msgtext::placeholders(&sensors, [](const char* ph, void*) { if (s_ph_n < PH_MAX) s_ph[s_ph_n++] = ph; }, nullptr);
}

// The placeholders as small buttons; `cb` gets the index as user data.
static lv_obj_t* chips(lv_obj_t* parent, lv_event_cb_t cb, bool wrap) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_set_size(row, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(row, wrap ? LV_FLEX_FLOW_ROW_WRAP : LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 4, 0);
  lv_obj_set_style_pad_row(row, 4, 0);
  if (wrap) lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  else { lv_obj_set_scroll_dir(row, LV_DIR_HOR); lv_obj_set_scrollbar_mode(row, LV_SCROLLBAR_MODE_OFF); }
  for (int i = 0; i < s_ph_n; i++) {
    lv_obj_t* b = lv_button_create(row);
    lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
    lv_obj_set_style_pad_hor(b, 8, 0);
    lv_obj_set_style_pad_ver(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS_SM, 0);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_add_event_cb(b, cb, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
    lv_obj_center(label(b, s_ph[i], THEME_FONT_SMALL, theme::TEXT));
  }
  return row;
}

static void onQuickSend(lv_event_t* e)    { s_ui->quickSend((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onQuickInsert(lv_event_t* e)  { s_ui->quickInsert((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onQuickSlot(lv_event_t* e)    { s_ui->quickEdit((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onQuickEditPh(lv_event_t* e)  { s_ui->quickEditInsert((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onQuickEditKb(lv_event_t* e)  { s_ui->quickEditDone(lv_event_get_code(e) == LV_EVENT_READY); }
static void onQuickList(lv_event_t* e)    { (void)e; s_ui->showQuickMsgs(); }
static void onAdvertGo(lv_event_t* e)     { s_ui->advertSend((uintptr_t)lv_event_get_user_data(e) != 0); }
static void bluetoothHint(char* sub, size_t n) {
  if (!s_ui->isSerialEnabled()) snprintf(sub, n, "Off");
  else if (s_ui->hasConnection()) snprintf(sub, n, "App connected");
  else if (the_mesh.getBLEPin()) snprintf(sub, n, "PIN %06lu", (unsigned long)the_mesh.getBLEPin());
  else snprintf(sub, n, "Waiting for the app");
}
static void onBtSwitch(lv_event_t* e) {
  lv_obj_t* sw = (lv_obj_t*)lv_event_get_target(e);
  s_ui->setBluetooth(lv_obj_has_state(sw, LV_STATE_CHECKED));
  char sub[48];
  bluetoothHint(sub, sizeof(sub));
  rowHintSet(lv_obj_get_parent(sw), sub);
}

}  // namespace qview

static void onAdvertRow(lv_event_t* e)     { (void)e; s_ui->advertPopup(); }
static void onOpenQuickMsgs(lv_event_t* e) { (void)e; s_ui->showQuickMsgs(); }

// ── The compose bar's "+" ─────────────────────────────────────────────────────

void UITask::quickPopup() {
  using namespace qview;
  if (!_compose_ta) return;
  collect();
  lv_obj_t* panel = navPopupPanel("Quick", true);
  lv_obj_add_flag(panel, LV_OBJ_FLAG_SCROLLABLE);
  label(panel, "Insert, filled in when sent:", THEME_FONT_SMALL, theme::TEXT_MUTED);
  chips(panel, onQuickInsert, true);
  label(panel, "Send now:", THEME_FONT_SMALL, theme::TEXT_MUTED);
  int shown = 0;
  for (int i = 0; i < msgtext::QUICK_COUNT; i++) {
    const char* t = msgtext::quick(_prefs, i);
    if (!t[0]) continue;
    char sent[MSG_TEXT_BUF];
    msgtext::expand(t, sent, sizeof(sent), _prefs);
    listRow(panel, t, strcmp(sent, t) ? sent : NULL, onQuickSend, (void*)(uintptr_t)i);   // what goes out
    shown++;
  }
  if (!shown) label(panel, "No quick messages yet.", THEME_FONT_SMALL, theme::TEXT_MUTED);
  listRow(panel, LV_SYMBOL_EDIT "  Edit quick messages", NULL, onQuickList, NULL);
}

void UITask::quickSend(int slot) {
  const char* t = msgtext::quick(_prefs, slot);
  if (!t[0]) return;
  char text[MSG_TEXT_BUF];
  msgtext::expand(t, text, sizeof(text), _prefs);
  navClosePopup();
  if (sendThreadText(text)) refreshThread();
}

void UITask::quickInsert(int ph) {
  using namespace qview;
  if (ph < 0 || ph >= s_ph_n || !_compose_ta) return;
  navClosePopup();
  lv_textarea_add_text(_compose_ta, s_ph[ph]);   // the byte cap still applies (onComposeInsert)
  setKeyboardVisible(true);
}

// ── Settings > Messages & contacts > Quick messages ───────────────────────────

void UITask::showQuickMsgs() {
  navClosePopup();
  _screen = SCR_QUICK;
  buildQuickMsgs();
}

void UITask::buildQuickMsgs() {
  using namespace qview;
  lv_obj_t* body = newScreen("Quick messages", true);
  lv_obj_t* g = group(body, nullptr);
  for (int i = 0; i < msgtext::QUICK_COUNT; i++) {
    const char* q = msgtext::quick(_prefs, i);
    char title[msgtext::QUICK_LEN + 8];
    snprintf(title, sizeof(title), "%d   %s", i + 1, q[0] ? q : "(empty)");
    lv_obj_t* row = listRow(g, title, NULL, onQuickSlot, (void*)(uintptr_t)i);
    if (!q[0]) lv_obj_set_style_text_color(rowTitle(row), lv_color_hex(theme::TEXT_MUTED), 0);
  }
  groupNote(body, "Sent from a chat's \"+\"; placeholders are filled in.");
}

// A field and the placeholders over the keyboard (a titled popup doesn't fit
// above 124 px of keys); the field's placeholder says which slot it is.
void UITask::quickEdit(int slot) {
  using namespace qview;
  if (!_prefs || slot < 0 || slot >= msgtext::QUICK_COUNT) return;
  collect();
  s_edit = slot;
  navClosePopup();
  _nav_overlay = lv_obj_create(screen());
  lv_obj_remove_style_all(_nav_overlay);
  lv_obj_set_size(_nav_overlay, LV_PCT(100), LV_PCT(100));
  lv_obj_set_style_bg_color(_nav_overlay, lv_color_hex(0x000000), 0);
  lv_obj_set_style_bg_opa(_nav_overlay, LV_OPA_80, 0);
  lv_obj_add_flag(_nav_overlay, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_remove_flag(_nav_overlay, LV_OBJ_FLAG_SCROLLABLE);
  int w = lv_display_get_horizontal_resolution(NULL) - 16;

  char ph[40];
  snprintf(ph, sizeof(ph), "Quick message %d (empty clears it)", slot + 1);
  _nav_ta = wifiField(_nav_overlay, ph, false);
  lv_obj_remove_event_cb(_nav_ta, onWifiField);
  lv_obj_set_width(_nav_ta, w);
  lv_obj_set_pos(_nav_ta, 8, theme::STATUS_H + 4);
  lv_textarea_set_text(_nav_ta, msgtext::quick(_prefs, slot));
  lv_obj_add_event_cb(_nav_ta, devview::onByteCapInsert, LV_EVENT_INSERT, (void*)(uintptr_t)(msgtext::QUICK_LEN - 1));
  lv_obj_add_state(_nav_ta, LV_STATE_FOCUSED);   // draws the cursor

  lv_obj_t* row = chips(_nav_overlay, onQuickEditPh, false);
  lv_obj_set_width(row, w);
  lv_obj_set_pos(row, 8, theme::STATUS_H + 4 + 34 + 6);

  _nav_kb = kb::create(_nav_overlay, _prefs);
  lv_obj_set_size(_nav_kb, LV_PCT(100), 124);
  lv_obj_align(_nav_kb, LV_ALIGN_BOTTOM_MID, 0, 0);
  lv_keyboard_set_textarea(_nav_kb, _nav_ta);
  lv_obj_add_event_cb(_nav_kb, onQuickEditKb, LV_EVENT_READY, NULL);
  lv_obj_add_event_cb(_nav_kb, onQuickEditKb, LV_EVENT_CANCEL, NULL);
}

void UITask::quickEditInsert(int ph) {
  using namespace qview;
  if (ph >= 0 && ph < s_ph_n && _nav_ta) lv_textarea_add_text(_nav_ta, s_ph[ph]);
}

void UITask::quickEditDone(bool ok) {
  using namespace qview;
  if (ok && _nav_ta && _prefs && s_edit >= 0) {
    msgtext::setQuick(_prefs, s_edit, lv_textarea_get_text(_nav_ta));
    prefsSave();
    showToast(msgtext::quick(_prefs, s_edit)[0] ? "Quick message saved" : "Quick message cleared", 1200);
  }
  navClosePopup();
  if (ok && _screen == SCR_QUICK) {
    lv_obj_t* body = _body;
    int32_t y = body ? lv_obj_get_scroll_y(body) : 0;
    buildQuickMsgs();
    if (_body) { lv_obj_update_layout(_body); lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF); }
  }
}

// ── Nearby > advert ───────────────────────────────────────────────────────────
// Shows you (and your position, if shared) to other nodes: now, or on a timer
// (the schema's PG_ADVERT page, ui-new's Tools > Auto-Advert).

void UITask::advertPopup() {
  using namespace qview;
  lv_obj_t* panel = navPopupPanel("Advert", true);
  lv_obj_t* list = lv_obj_create(panel);
  styleSurface(list, theme::BG);
  lv_obj_set_width(list, LV_PCT(100));
  lv_obj_set_flex_grow(list, 1);
  lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(list, theme::GAP, 0);
  lv_obj_t* g = group(list, "SEND NOW");
  listRow(g, "Zero hop", "Direct range only", onAdvertGo, (void*)(uintptr_t)0);
  listRow(g, "Flood", "Across the mesh", onAdvertGo, (void*)(uintptr_t)1);
  schemaRows(list, settings::PG_ADVERT);
}

void UITask::advertSend(bool flood) {
  navClosePopup();
  bool ok = flood ? the_mesh.advertFlood() : the_mesh.advert();
  if (ok) notify(UIEventType::ack);
  showToast(!ok ? "Advert failed" : flood ? "Flood advert sent" : "Zero-hop advert sent");
}

// ── Settings > Bluetooth ──────────────────────────────────────────────────────
// BLE only: the USB link to a computer stays on. Not kept over a reboot
// (Bluetooth starts on, as on L1).

static void bluetoothRow(lv_obj_t* body) {
  char sub[48];
  qview::bluetoothHint(sub, sizeof(sub));
  lv_obj_t* sw = switchRow(body, LV_SYMBOL_BLUETOOTH "  Bluetooth", sub, nullptr);
  if (s_ui->isSerialEnabled()) lv_obj_add_state(sw, LV_STATE_CHECKED);
  lv_obj_add_event_cb(sw, qview::onBtSwitch, LV_EVENT_VALUE_CHANGED, NULL);
}

void UITask::setBluetooth(bool on) {
  if (on) enableSerial(); else disableSerial();
  if (!on) showToast("Bluetooth off - USB still works", 1500);
  refreshStatusBar();
}

// ── Messages > Read all ───────────────────────────────────────────────────────

void UITask::markAllRead() {
  _core->markAllRead();
  showToast("All marked read", 1200);
  if (_screen == SCR_CHATS) buildChats();
}
