#pragma once
// Auto-reply bot settings (Home > Bot) -- ui-new's Tools > Bot. The rows and
// their NodePrefs fields are ui-core/BotConfig.h; the bot runs in MyMesh.
// Tabs Channel / Room / Direct / Other; switches inline, trigger / reply in a
// popup with the keyboard (reply: placeholder buttons above it), channel /
// room from a list popup, quiet hours with - / +.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp after AdminScreen.h.

namespace botview {

static uint8_t s_tab = botcfg::TAB_CHANNEL;
static int s_row = -1;                 // row a popup is editing
static lv_obj_t* s_hour_lbl = nullptr;
static uint8_t s_rooms[32][NodePrefs::FAVOURITE_PREFIX_LEN];

enum : uint8_t { B_MINUS, B_PLUS, B_DONE };

}  // namespace botview

static void onBotTab(lv_event_t* e) {
  s_ui->botTab((int)lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)));
}
static void onBotRow(lv_event_t* e)    { s_ui->botRow((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onBotSwitch(lv_event_t* e) {
  s_ui->botToggle((int)(uintptr_t)lv_event_get_user_data(e),
                  lv_obj_has_state((lv_obj_t*)lv_event_get_target(e), LV_STATE_CHECKED));
}
static void onBotScope(lv_event_t* e) {
  s_ui->botToggle((int)(uintptr_t)lv_event_get_user_data(e),
                  lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e)) == 1);
}
static void onBotPick(lv_event_t* e)   { s_ui->botPick((int)(uintptr_t)lv_event_get_user_data(e)); }
static void onBotHour(lv_event_t* e)   { s_ui->botHour((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onBotTextKb(lv_event_t* e) { s_ui->botTextDone(lv_event_get_code(e) == LV_EVENT_READY); }
static lv_obj_t* s_chip_ta = nullptr;   // the reply field the chips type into
static void onBotPlaceholder(lv_event_t* e) {
  lv_obj_t* btn = (lv_obj_t*)lv_event_get_target(e);
  if (s_chip_ta) lv_textarea_add_text(s_chip_ta, lv_label_get_text(lv_obj_get_child(btn, 0)));
}
// A placeholder button on `parent` (the reply editor's chips row).
static void botChip(const char* ph, void* parent) {
  lv_obj_t* b = lv_button_create((lv_obj_t*)parent);
  lv_obj_set_height(b, 26);
  lv_obj_set_style_pad_hor(b, 7, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onBotPlaceholder, LV_EVENT_CLICKED, NULL);
  lv_obj_center(label(b, ph, THEME_FONT_SMALL, theme::TEXT));
}
static void onOpenBot(lv_event_t* e) { (void)e; s_ui->showBot(); }

static lv_obj_t* botHourButton(lv_obj_t* parent, const char* text, uint8_t act, bool accent) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_height(b, 40);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? theme::ACCENT_DIM : theme::SURFACE), 0);
  lv_obj_add_event_cb(b, onBotHour, LV_EVENT_CLICKED, (void*)(uintptr_t)act);
  lv_obj_center(label(b, text, act == botview::B_DONE ? THEME_FONT_BODY : THEME_FONT_LARGE, theme::TEXT));
  return b;
}

void UITask::showBot() {
  _screen = SCR_BOT;
  buildBot();
}

void UITask::buildBot() {
  using namespace botview;
  lv_obj_t* body = newScreen("Bot", true);
  lv_obj_set_style_pad_row(body, 4, 0);
  NodePrefs* p = _prefs;
  if (!p) return;
  uint16_t sent = the_mesh.botReplyCount();
  if (sent > 0 && _header) {   // replies since boot
    lv_obj_t* c = label(_header, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_text_fmt(c, "%u sent", (unsigned)sent);
    lv_obj_align(c, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  }

  static const char* TABS[botcfg::TAB_COUNT + 1];
  for (int i = 0; i < botcfg::TAB_COUNT; i++) TABS[i] = botcfg::TAB_LABELS[i];
  TABS[botcfg::TAB_COUNT] = "";
  lv_obj_t* tabs = segmented(body, TABS, s_tab, lv_pct(100), 34);
  lv_obj_set_style_bg_color(tabs, lv_color_hex(theme::SURFACE), LV_PART_ITEMS);
  lv_obj_add_event_cb(tabs, onBotTab, LV_EVENT_VALUE_CHANGED, NULL);

  lv_obj_t* list = scrollList(body);
  lv_obj_t* g = group(list, nullptr);

  for (int i = 0; i < botcfg::rowCount(s_tab); i++) {
    const botcfg::Row& r = botcfg::row(s_tab, i);
    uint8_t* fl = botcfg::flag(p, r.kind);
    lv_obj_t* row = settingRow(g, r.label, fl || botcfg::isHour(r.kind) ? r.hint : nullptr);
    if (fl && r.kind == botcfg::DM_SCOPE) {
      static const char* SCOPE[] = { "All", "Fav", "" };
      lv_obj_t* seg = segmented(row, SCOPE, *fl ? 1 : 0, 110, 32);
      lv_obj_align(seg, LV_ALIGN_RIGHT_MID, -4, 0);
      lv_obj_add_event_cb(seg, onBotScope, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
      continue;
    }
    if (fl) {
      lv_obj_t* sw = lv_switch_create(row);
      lv_obj_set_size(sw, 46, 24);
      lv_obj_align(sw, LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
      if (*fl) lv_obj_add_state(sw, LV_STATE_CHECKED);
      lv_obj_add_event_cb(sw, onBotSwitch, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)i);
      continue;
    }
    // Value on the right; tap the row to change it.
    char val[48];
    int cap;
    if (const char* t = botcfg::text(p, r.kind, cap)) snprintf(val, sizeof(val), "%s", botcfg::shownText(t, botcfg::isTrigger(r.kind)));
    else if (r.kind == botcfg::CHANNEL) { if (!botcfg::channelName(p, val, sizeof(val))) snprintf(val, sizeof(val), "(pick one)"); }
    else if (r.kind == botcfg::ROOM)    { if (!botcfg::roomName(p, val, sizeof(val))) snprintf(val, sizeof(val), "(pick one)"); }
    else if (botcfg::quietOff(p)) snprintf(val, sizeof(val), "Off");
    else snprintf(val, sizeof(val), "%02d:00", botcfg::hour(p, r.kind));
    lv_obj_t* v = label(row, val, THEME_FONT_BODY, theme::ACCENT);
    lv_label_set_long_mode(v, LV_LABEL_LONG_DOT);
    lv_obj_set_width(v, botcfg::isHour(r.kind) ? 60 : 150);   // fixed: LONG_DOT needs a width
    lv_obj_set_style_text_align(v, LV_TEXT_ALIGN_RIGHT, 0);
    label(row, LV_SYMBOL_RIGHT, THEME_FONT_SMALL, theme::TEXT_MUTED);
    rowPressable(row);
    lv_obj_add_event_cb(row, onBotRow, LV_EVENT_CLICKED, (void*)(uintptr_t)i);
  }
}

void UITask::botTab(int tab) {
  if (tab < 0 || tab >= botcfg::TAB_COUNT) return;
  botview::s_tab = (uint8_t)tab;
  buildBot();
}

void UITask::botToggle(int row, bool on) {
  if (!_prefs || row < 0 || row >= botcfg::rowCount(botview::s_tab)) return;
  uint8_t* fl = botcfg::flag(_prefs, botcfg::row(botview::s_tab, row).kind);
  if (!fl) return;
  *fl = on ? 1 : 0;
  prefsSave();
}

// Tap on a value row: its editor.
void UITask::botRow(int row) {
  using namespace botview;
  NodePrefs* p = _prefs;
  if (!p || row < 0 || row >= botcfg::rowCount(s_tab)) return;
  s_row = row;
  const botcfg::Row& r = botcfg::row(s_tab, row);
  int cap;
  if (char* t = botcfg::text(p, r.kind, cap)) {
    bool reply = !botcfg::isTrigger(r.kind);
    TextEntry te = { r.label, onBotTextKb };
    te.text = t;
    te.max_bytes = cap - 1;
    te.hint = botcfg::isTrigger(r.kind) ? "e.g. !hi, or * for any message" : "Reply text";
    lv_obj_t* panel = navTextEntry(te);
    if (reply) {
      // Placeholders the bot fills in, as buttons in place of the title (no
      // room for another row above the keyboard; the field says "Reply").
      lv_obj_t* hdr = lv_obj_get_child(panel, 0);
      lv_obj_add_flag(lv_obj_get_child(hdr, 0), LV_OBJ_FLAG_HIDDEN);
      lv_obj_t* chips = lv_obj_create(hdr);
      lv_obj_remove_style_all(chips);
      lv_obj_set_size(chips, 240, 28);
      lv_obj_align(chips, LV_ALIGN_LEFT_MID, 0, 0);
      lv_obj_set_flex_flow(chips, LV_FLEX_FLOW_ROW);
      lv_obj_set_flex_align(chips, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
      lv_obj_set_style_pad_gap(chips, 4, 0);
      lv_obj_set_scroll_dir(chips, LV_DIR_HOR);   // swipe for the rest
      lv_obj_set_scrollbar_mode(chips, LV_SCROLLBAR_MODE_OFF);
      s_chip_ta = _nav_ta;
      for (int i = 0; i < botcfg::REPLY_PLACEHOLDER_COUNT; i++) botChip(botcfg::REPLY_PLACEHOLDERS[i], chips);
      msgtext::sensorPlaceholders(&sensors, [](const char* ph, void* c) {
        if (strcmp(ph, "{batt}") != 0) botChip(ph, c);   // already there
      }, chips);
    }
    return;
  }

  if (r.kind == botcfg::CHANNEL || r.kind == botcfg::ROOM) {
    lv_obj_t* panel = navPopupPanel(r.kind == botcfg::CHANNEL ? "Answer on channel" : "Answer in room", true);
    lv_obj_t* list = lv_obj_create(panel);
    styleSurface(list, theme::BG);
    lv_obj_set_width(list, LV_PCT(100));
    lv_obj_set_flex_grow(list, 1);
    lv_obj_set_flex_flow(list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_style_pad_row(list, theme::GAP, 0);
    int n = 0;
    if (r.kind == botcfg::CHANNEL) {
      for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
        ChannelDetails ch;
        if (!the_mesh.getChannel(i, ch) || !ch.name[0]) continue;
        lv_obj_t* rw = listRow(list, ch.name, NULL, onBotPick, (void*)(uintptr_t)i);
        if (i == p->bot_channel_idx) lv_obj_set_style_bg_color(rw, lv_color_hex(theme::ACCENT_DIM), 0);
        n++;
      }
    } else {
      int total = the_mesh.getNumContacts();
      for (int i = 0; i < total && n < (int)(sizeof(s_rooms) / sizeof(s_rooms[0])); i++) {
        ContactInfo c;
        if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_ROOM) continue;
        memcpy(s_rooms[n], c.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
        lv_obj_t* rw = listRow(list, c.name, NULL, onBotPick, (void*)(uintptr_t)n);
        if (memcmp(c.id.pub_key, p->bot_room_prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0)
          lv_obj_set_style_bg_color(rw, lv_color_hex(theme::ACCENT_DIM), 0);
        n++;
      }
    }
    if (n == 0) label(list, r.kind == botcfg::CHANNEL ? "No channels." : "No room servers in your contacts.",
                      THEME_FONT_SMALL, theme::TEXT_MUTED);
    return;
  }

  if (botcfg::isHour(r.kind)) {
    lv_obj_t* panel = navPopupPanel(r.label, false);
    label(panel, "Same hour for both = no quiet hours.", THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_t* rw = centerRow(panel);
    botHourButton(rw, LV_SYMBOL_MINUS, B_MINUS, false);
    s_hour_lbl = label(rw, "", THEME_FONT_LARGE, theme::TEXT);
    lv_obj_set_width(s_hour_lbl, 80);
    lv_obj_set_style_text_align(s_hour_lbl, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_text_fmt(s_hour_lbl, "%02d:00", botcfg::hour(p, r.kind));
    botHourButton(rw, LV_SYMBOL_PLUS, B_PLUS, false);
    rw = centerRow(panel);
    botHourButton(rw, LV_SYMBOL_OK " Done", B_DONE, true);
  }
}

void UITask::botTextDone(bool ok) {
  using namespace botview;
  if (ok && _nav_ta && _prefs && s_row >= 0 && s_row < botcfg::rowCount(s_tab)) {
    int cap;
    char* t = botcfg::text(_prefs, botcfg::row(s_tab, s_row).kind, cap);
    if (t) {
      snprintf(t, cap, "%s", lv_textarea_get_text(_nav_ta));
      prefsSave();
    }
  }
  navClosePopup();
  if (ok) buildBot();
}

void UITask::botPick(int idx) {
  using namespace botview;
  if (!_prefs || s_row < 0) return;
  botcfg::Kind k = botcfg::row(s_tab, s_row).kind;
  if (k == botcfg::CHANNEL) _prefs->bot_channel_idx = (uint8_t)idx;
  else if (k == botcfg::ROOM && idx >= 0 && idx < (int)(sizeof(s_rooms) / sizeof(s_rooms[0]))) botcfg::setRoom(_prefs, s_rooms[idx]);
  prefsSave();
  navClosePopup();
  buildBot();
}

void UITask::botHour(uint8_t act) {
  using namespace botview;
  if (!_prefs || s_row < 0) return;
  botcfg::Kind k = botcfg::row(s_tab, s_row).kind;
  uint8_t& h = botcfg::hour(_prefs, k);
  if (act == B_DONE) {
    prefsSave();
    navClosePopup();
    buildBot();
    return;
  }
  h = (uint8_t)((h + (act == B_PLUS ? 1 : 23)) % 24);
  if (s_hour_lbl) lv_label_set_text_fmt(s_hour_lbl, "%02d:00", h);
}
