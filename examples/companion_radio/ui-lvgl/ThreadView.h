#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).
// A conversation's thread: bubbles, the compose row, message metadata.

#include "../ui-core/TextFit.h"   // a sender's name cut on a whole character

// ── Conversation ──────────────────────────────────────────────────────────────

static void onKeyboard(lv_event_t* e) {
  lv_event_code_t code = lv_event_get_code(e);
  if (code == LV_EVENT_READY) s_ui->sendFromCompose();
  else if (code == LV_EVENT_CANCEL) s_ui->setKeyboardVisible(false);
}

// What a message may take is counted in UTF-8 bytes (the over-the-air limit),
// not characters: Cyrillic / Greek / accented letters take two bytes each. The
// limit is what the send leaves of it -- a channel post carries this node's
// name -- and a placeholder counts as what it expands to (MessageText.h).
int UITask::composeLimit() const { return msgtext::limit(_thread_is_channel, _prefs); }
int UITask::composeLen(const char* typed) const { return msgtext::sentLen(typed, _prefs); }

static lv_obj_t* s_compose_left = nullptr;   // the bytes left, a tag over the field's right end

// Only once the end is near (40 bytes, as on the other screens): a tag sitting
// on the field's top edge, so the field keeps its width and its text.
static void composeLeftRefresh(lv_obj_t* ta) {
  if (!s_compose_left || !ta) return;
  const int left = s_ui->composeLimit() - s_ui->composeLen(lv_textarea_get_text(ta));
  if (left > 40) { lv_obj_add_flag(s_compose_left, LV_OBJ_FLAG_HIDDEN); return; }
  char t[8];
  snprintf(t, sizeof(t), "%d", left);
  setText(s_compose_left, t);
  setTextColor(s_compose_left, left < 0 ? theme::FAIL : left <= 20 ? theme::ACCENT : theme::TEXT_MUTED);
  lv_obj_remove_flag(s_compose_left, LV_OBJ_FLAG_HIDDEN);
  lv_obj_update_layout(s_compose_left);
  lv_obj_align_to(s_compose_left, ta, LV_ALIGN_OUT_TOP_RIGHT, -8, lv_obj_get_height(s_compose_left) / 2);
}

static void onComposeInsert(lv_event_t* e) {
  lv_obj_t* ta = (lv_obj_t*)lv_event_get_target(e);
  const char* ins = (const char*)lv_event_get_param(e);
  if (!ins) return;
  char joined[2 * MAX_TEXT_LEN + 1];   // the text with the insert (at the end: the length is the same)
  snprintf(joined, sizeof(joined), "%s%s", lv_textarea_get_text(ta), ins);
  if (s_ui->composeLen(joined) > s_ui->composeLimit())
    lv_textarea_set_insert_replace(ta, "");   // reject: would exceed the byte limit
}

static void onComposeChanged(lv_event_t* e) { composeLeftRefresh((lv_obj_t*)lv_event_get_target(e)); }

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
    int top = show ? theme::STATUS_H : theme::APPBAR_H;   // typing: the slimmer status bar in its place
    if (show) lv_obj_add_flag(_header, LV_OBJ_FLAG_HIDDEN);
    else      lv_obj_remove_flag(_header, LV_OBJ_FLAG_HIDDEN);
    statusBarSync();
    lv_obj_set_pos(_body, 0, top);
    lv_obj_set_height(_body, lv_display_get_vertical_resolution(NULL) - top);
  }
  if (_thread_list) {   // keep the newest message in view
    layoutNow(_thread_list);
    lv_obj_scroll_to_y(_thread_list, LV_COORD_MAX, LV_ANIM_OFF);
  }
}

static const int THREAD_MAX_SHOWN = 50;   // newest bubbles built per conversation

// Opens on the first unread (newest-first index `first`, -1: none), "New"
// above it: the page that holds it, a few read ones above.
void UITask::threadOpenAt(int first) {
  _thread_new = first;
  _thread_new_on = false;
  _thread_skip = first < THREAD_MAX_SHOWN ? 0 : first - (THREAD_MAX_SHOWN - 4);
}

// Back on over a conversation: what came in while asleep opens it again on
// its first unread (the typed text kept, the keyboard down); else just the
// fresh content.
void UITask::reopenThreadIfUnread() {
  bool unread = _thread_is_channel ? _core->history.chUnread(_thread_channel) > 0 : _core->dmUnread(_thread_key) > 0;
  if (!unread || _nav_overlay) { refreshThread(); return; }
  char typed[MSG_TEXT_BUF] = "";
  if (_compose_ta) snprintf(typed, sizeof(typed), "%s", lv_textarea_get_text(_compose_ta));
  uint8_t key[PUB_KEY_SIZE];   // openDM() clears _thread_key before reading its argument
  memcpy(key, _thread_key, sizeof(key));
  if (_thread_is_channel) openChannel(_thread_channel);
  else openDM(key);
  if (_compose_ta && typed[0]) { lv_textarea_set_text(_compose_ta, typed); composeLeftRefresh(_compose_ta); }
}

void UITask::openChannel(uint8_t channel_idx) {
  _thread_is_channel = true;
  _thread_channel = channel_idx;
  MessageHistory& h = _core->history;
  threadOpenAt(h.firstUnreadChannel(channel_idx, h.chUnread(channel_idx)));
  h.setChUnread(channel_idx, 0);
  _screen = SCR_THREAD;
  buildThread();
}

void UITask::openDM(const uint8_t* pub_key) {
  _thread_is_channel = false;
  memset(_thread_key, 0, sizeof(_thread_key));
  ContactInfo c;
  if (MessageHistory::contactByPrefix(pub_key, c)) memcpy(_thread_key, c.id.pub_key, PUB_KEY_SIZE);
  else memcpy(_thread_key, pub_key, 4);
  threadOpenAt(_core->history.firstUnreadDm(_thread_key, _core->dmUnread(_thread_key)));
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
    headerButton(_header, LV_SYMBOL_SETTINGS, _thread_is_channel ? onChanThreadMenu : onConvThreadMenu, 4, NULL);
  }

  _thread_list = scrollList(body);
  s_th_rows_n = 0;
  lv_obj_set_style_pad_all(_thread_list, theme::PAD, 0);

  _compose_ta = nullptr;
  _keyboard = nullptr;
  s_compose_left = nullptr;
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
    lv_obj_add_event_cb(_compose_ta, onComposeChanged, LV_EVENT_VALUE_CHANGED, NULL);
    lv_obj_set_width(_compose_ta, lv_display_get_horizontal_resolution(NULL) - 8 - 34 - 4);   // beside "+"
    lv_obj_align(_compose_ta, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_add_flag(bar, LV_OBJ_FLAG_OVERFLOW_VISIBLE);   // the count's tag rises above the bar
    s_compose_left = label(bar, "", THEME_FONT_SMALL, theme::TEXT_MUTED);   // bytes left
    lv_obj_set_style_bg_color(s_compose_left, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_bg_opa(s_compose_left, LV_OPA_COVER, 0);
    lv_obj_set_style_pad_hor(s_compose_left, 4, 0);
    lv_obj_set_style_radius(s_compose_left, theme::RADIUS, 0);
    lv_obj_add_flag(s_compose_left, LV_OBJ_FLAG_HIDDEN);
    composeLeftRefresh(_compose_ta);

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
      sig = sig * 31 + e.timestamp + h.dmEffectiveStatus(e) + e.attempt * 7;   // attempt: a dot per resend
    }
  }
  return sig;
}

// Positions found in the shown messages ([WAY] / [LOC] / plain "lat,lon"),
// for the Go / Save buttons under such a bubble.
struct MsgLoc { int32_t lat, lon; char label[WAYPOINT_LABEL_LEN * 2]; };
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
static lv_obj_t* s_new_div = nullptr;   // "New" above the first unread

// The line with "New" over the first message unread when the thread opened.
static lv_obj_t* newDivider(lv_obj_t* list) {
  lv_obj_t* d = lv_obj_create(list);
  lv_obj_remove_style_all(d);
  lv_obj_set_size(d, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_flex_flow(d, LV_FLEX_FLOW_ROW);
  lv_obj_set_flex_align(d, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  lv_obj_set_style_pad_column(d, 6, 0);
  lv_obj_remove_flag(d, LV_OBJ_FLAG_CLICKABLE);
  for (int i = 0; i < 3; i++) {
    if (i == 1) { label(d, "New", THEME_FONT_SMALL, theme::ACCENT); continue; }
    lv_obj_t* l = lv_obj_create(d);
    lv_obj_remove_style_all(l);
    lv_obj_set_height(l, 1);
    lv_obj_set_flex_grow(l, 1);
    lv_obj_set_style_bg_color(l, lv_color_hex(theme::ACCENT), 0);
    lv_obj_set_style_bg_opa(l, LV_OPA_COVER, 0);
  }
  return d;
}

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

static void msgLocButton(lv_obj_t* parent, const char* text, bool save, bool accent, bool own) {
  lv_obj_t* b = lv_button_create(parent);
  lv_obj_set_size(b, LV_SIZE_CONTENT, 28);
  lv_obj_set_style_pad_hor(b, 10, 0);
  lv_obj_set_style_pad_ver(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(b, 0, 0);
  // on our own amber bubble the main one goes dark instead
  lv_obj_set_style_bg_color(b, lv_color_hex(accent ? (own ? theme::ON_ACCENT : theme::ACCENT) : theme::SURFACE_2), 0);
  lv_obj_add_event_cb(b, onMsgLoc, LV_EVENT_CLICKED, (void*)(uintptr_t)(save ? 1 : 0));
  lv_obj_center(label(b, text, THEME_FONT_SMALL, accent ? (own ? theme::ACCENT : theme::ON_ACCENT) : theme::TEXT));
}

// A message's text, wrapped at `max_w` and shrunk to fit. "@[nick]" mentions
// (how a reply names who it answers) show as "@nick" in the accent -- dark,
// as the rest, on our own amber bubbles -- and underlined when the nick is
// ours (*mentions_me set). Plain text stays a label.
static lv_obj_t* msgText(lv_obj_t* parent, const char* text, bool own, int max_w, bool* mentions_me) {
  *mentions_me = false;
  const uint32_t col = own ? theme::ON_ACCENT : theme::TEXT;
  if (!strstr(text, "@[")) {
    lv_obj_t* t = label(parent, text, THEME_FONT_BODY, col);
    lv_label_set_long_mode(t, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_max_width(t, max_w, 0);
    lv_obj_set_width(t, LV_SIZE_CONTENT);
    return t;
  }
  lv_obj_t* sg = lv_spangroup_create(parent);
  lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE);   // a hold still reaches the bubble (its menu)
  lv_obj_set_style_text_font(sg, THEME_FONT_BODY, 0);
  lv_obj_set_style_text_color(sg, lv_color_hex(col), 0);
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
    lv_style_set_text_color(st, lv_color_hex(own ? theme::ON_ACCENT : theme::ACCENT));
    if (mine) { lv_style_set_text_decor(st, LV_TEXT_DECOR_UNDERLINE); *mentions_me = true; }
    p = close + 1;
  }
  lv_spangroup_refr_mode(sg);
  uint32_t w = lv_spangroup_get_expand_width(sg, 0);
  lv_obj_set_width(sg, w > (uint32_t)max_w ? max_w : (int32_t)w + 1);
  lv_obj_set_height(sg, LV_SIZE_CONTENT);
  return sg;
}

// One message bubble. Own messages right-aligned in amber with dark text
// (the delivery mark too: green or red on amber can't be read), others left.
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
    lv_obj_set_style_bg_color(b, lv_color_hex(own ? theme::mix(theme::ACCENT, theme::BG, 75) : theme::SURFACE_2), LV_STATE_PRESSED);
  } else {
    lv_obj_remove_flag(b, LV_OBJ_FLAG_CLICKABLE);
  }
  lv_obj_set_size(b, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
  lv_obj_set_style_max_width(b, 250, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(own ? theme::ACCENT : theme::SURFACE), 0);
  const uint32_t muted = own ? theme::mix(theme::ON_ACCENT, theme::ACCENT, 60) : theme::TEXT_MUTED;
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
    label(hdr, from, THEME_FONT_SMALL, own ? theme::ON_ACCENT : theme::ACCENT);   // names are <= 31 chars: fits the bubble
    if (meta_in_header) *age_out = label(hdr, meta, THEME_FONT_SMALL, muted);
  }
  bool mentions_me;
  mecp::Message mm;   // MECP: in words, a MAYDAY / URGENT outlined red
  const bool alarm = !own && mecp::Message::decode(text, mm) && mm.severity <= mecp::Severity::Urgent;
  msgText(b, mecp::shown(text), own, 238, &mentions_me);
  if (alarm) {
    lv_obj_set_style_border_width(b, 2, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(theme::FAIL), 0);
  } else if (mentions_me && !own) {   // someone answering us: the bubble outlined
    lv_obj_set_style_border_width(b, 1, 0);
    lv_obj_set_style_border_color(b, lv_color_hex(theme::ACCENT), 0);
  }

  if (loc) {   // position: navigate there / keep it as a waypoint
    lv_obj_t* acts = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_style_pad_column(acts, 6, 0);
    lv_obj_set_style_pad_top(acts, 2, 0);
    msgLocButton(acts, UI_SYMBOL_COMPASS " Go", false, true, own);
    msgLocButton(acts, UI_SYMBOL_FLAG " Save", true, false, own);
  }

  if (!meta_in_header) {   // the age, then the delivery mark in its colour (as L1)
    lv_obj_t* line = flexBox(b, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(line, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
    lv_obj_set_style_pad_column(line, 6, 0);
    *age_out = label(line, meta, THEME_FONT_SMALL, muted);
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
      lv_obj_t* n = label(c, "", THEME_FONT_SMALL, theme::ON_ACCENT);
      lv_label_set_text_fmt(n, "%d", relays);
      lv_obj_center(n);
    } else if (status && status[0]) {
      label(line, status, THEME_FONT_SMALL, own ? theme::ON_ACCENT : status_col);
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
    return s_th_dm[i].outgoing ? 0x100u | h.dmEffectiveStatus(s_th_dm[i]) | (uint32_t)s_th_dm[i].attempt << 16 : 0;
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
      if (!own) textfit::copy(m.from, sizeof(m.from), from);
      row = bubble(_thread_list, own ? NULL : from, body, own, e.timestamp, st, col, loc >= 0, true, nrel, &age);
    } else {
      const DmHistEntry& e = s_th_dm[i];
      if (e.outgoing) {
        switch (h.dmEffectiveStatus(e)) {
          case ACK_OK:      st = LV_SYMBOL_OK; col = theme::OK; break;
          case ACK_FAIL:    st = LV_SYMBOL_CLOSE; col = theme::FAIL; break;
          case ACK_PENDING: {   // a dot per send, as ui-new: grows with each auto-resend
            static char dots[9];
            int k = e.attempt + 1 < 8 ? e.attempt + 1 : 8;
            memset(dots, '.', k); dots[k] = '\0';
            st = dots;
            break;
          }
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
      textfit::copy(m.from, sizeof(m.from), who);
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
    bool at_end = lv_obj_get_scroll_bottom(_thread_list) <= theme::PAD;   // not when reading further up
    for (int r = 0; r < drop; r++) {
      if (s_new_div && s_th_rows[r].key == _thread_new_key) { lv_obj_delete(s_new_div); s_new_div = nullptr; }
      lv_obj_delete(s_th_rows[r].row);
    }
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
    if (n > kept && at_end) {   // new messages: down to them
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
  s_new_div = nullptr;
  for (int i = 0; i < n; i++) {   // oldest first
    if (_thread_new >= 0 && _thread_skip + n - 1 - i == _thread_new) { _thread_new_key = keyOf(i); _thread_new_on = true; }
    if (_thread_new_on && !s_new_div && keyOf(i) == _thread_new_key && (i > 0 || s_older_lbl)) s_new_div = newDivider(_thread_list);
    build(i);
  }
  _thread_new = -1;
  s_th_rows_n = s_msg_meta_n = s_msg_loc_n = n;
  s_th_rows_newest = _thread_skip == 0;   // a page back is built whole each time
  if (_thread_skip > 0) pageButton(_thread_list, LV_SYMBOL_DOWN "  Newer messages", -1);
  if (n == 0) label(_thread_list, "No messages yet", THEME_FONT_BODY, theme::TEXT_MUTED);
  layoutNow(_thread_list);
  lv_obj_scroll_to_y(_thread_list, _thread_scroll_top ? 0 : LV_COORD_MAX, LV_ANIM_OFF);
  if (s_new_div && !_thread_scroll_top) lv_obj_scroll_to_view(s_new_div, LV_ANIM_OFF);   // from the bottom: "New" on top
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
  if (composeLen(typed) > composeLimit()) { showToast("Too long"); return; }   // the count beside the field says by how much
  if (!sendThreadText(text)) return;
  lv_textarea_set_text(_compose_ta, "");
  setKeyboardVisible(false);
  refreshThread();
}
