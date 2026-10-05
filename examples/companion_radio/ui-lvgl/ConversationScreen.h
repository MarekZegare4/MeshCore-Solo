#pragma once
// Conversations beyond plain sending -- ui-new's Messages extras:
//  - room servers: opening one logs in first (the saved password silently, else
//    a password popup), then the thread shows each post under its author;
//  - conversation options (hold a DM / room row, or the ⚙ in its thread):
//    alerts and favourite for a contact; favourite, log in again / log out
//    for a room;
//  - message actions (hold a bubble): reply ("@[name] " in the compose field),
//    the path it came by / the repeaters that relayed it, set as target;
//  - the favourites-only filters of the Chats sections and the contact picker.
// Logic in the UI Core: RoomSessions (logins), contactctl (prefs, reply
// prefix, room post author, path hops), shared with ui-new.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp (uses the
// thread's s_msg_meta).

namespace convview {

enum : uint8_t { A_FAV, A_READ, A_LOGIN, A_LOGOUT, A_PIN };
enum : uint8_t { M_REPLY, M_TARGET };

static uint8_t s_key[PUB_KEY_SIZE];     // the conversation the popup is about
static uint8_t s_login_key[PUB_KEY_SIZE];
static bool s_login_wait = false;       // open the room once this login answers
static lv_obj_t* s_fav_btn = nullptr;
static int s_msg = -1;                  // s_msg_meta index the message popup is about
// The message popup kept up to date (messagePopupTick): its overlay while
// open, the message (by time), its "when" line and what else it shows.
static lv_obj_t* s_msg_overlay = nullptr;
static lv_obj_t* s_msg_when = nullptr;
static uint32_t s_msg_ts = 0;
static uint32_t s_msg_state = 0;

// "14:05  -  12s ago  -  2 hops": when it came and, incoming, how far.
static void whenText(char* when, size_t n, uint32_t ts, uint8_t packed, bool own, const NodePrefs* prefs) {
  char age[16];
  uint32_t now = rtc_clock.getCurrentTime();
  geo::fmtAgeShort(age, sizeof(age), now, ts ? ts : now);
  struct tm ti;
  if (localTime(prefs, ti, ts)) {
    char clk[12];
    fmtClock(clk, sizeof(clk), ti, prefs, true);
    if (clockBehind(now, ts)) {   // the clock isn't set yet: the date instead of an age
      char date[32];
      fmtDate(date, sizeof(date), ti);
      snprintf(when, n, "%s  %s", date, clk);
    } else snprintf(when, n, "%s  -  %s ago", clk, age);
  } else {
    snprintf(when, n, "%s ago", age);
  }
  if (!own) {   // incoming: how far it came, on the same line
    uint8_t hops = contactctl::hopCount(packed);
    size_t o = strlen(when);
    if (hops > 0) snprintf(when + o, n - o, "  -  %u hop%s", hops, hops == 1 ? "" : "s");
    else snprintf(when + o, n - o, "  -  direct");
  }
}


}  // namespace convview

static void onConvAction(lv_event_t* e) { s_ui->conversationAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onConvMelody(lv_event_t* e) {
  s_ui->conversationMelody(choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onConvNotif(lv_event_t* e) {
  s_ui->conversationNotif(choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onMsgAction(lv_event_t* e) { s_ui->messageAction((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }
static void onRoomLoginKb(lv_event_t* e) { s_ui->roomLoginDone(lv_event_get_code(e) == LV_EVENT_READY); }

static void onDMRowHold(lv_event_t* e) {
  intptr_t r = vlist::arg(e);
  if (r < 0 || !s_dm_known[r]) return;   // no options for who isn't a contact
  lv_indev_wait_release(lv_indev_active());   // the hold isn't also a tap that opens it
  s_ui->conversationMenu(s_dm_rows[r]);
}
static void onRoomRow(lv_event_t* e) {
  intptr_t r = vlist::arg(e);
  if (r >= 0) s_ui->openRoom(s_room_rows[r]);
}
static void onRoomRowHold(lv_event_t* e) {
  intptr_t r = vlist::arg(e);
  if (r < 0) return;
  lv_indev_wait_release(lv_indev_active());
  s_ui->conversationMenu(s_room_rows[r]);
}
static void onConvThreadMenu(lv_event_t* e) { (void)e; s_ui->conversationMenu(nullptr); }
static void onMsgHold(lv_event_t* e) {
  lv_indev_wait_release(lv_indev_active());
  s_ui->messageMenu(threadRowOf((lv_obj_t*)lv_event_get_current_target(e)));
}
static void onChatFilter(lv_event_t* e) { s_ui->toggleChatFilter((uint8_t)(uintptr_t)lv_event_get_user_data(e)); }

// ── Rooms ─────────────────────────────────────────────────────────────────────

void UITask::openRoom(const uint8_t* pub_key) {
  using namespace convview;
  ContactInfo ci;
  if (!MessageHistory::contactByPrefix(pub_key, ci)) return;
  _core->clearRoomUnread();
  switch (_core->rooms.open(ci)) {
    case RoomSessions::OPEN_NOW:
      openDM(ci.id.pub_key);
      break;
    case RoomSessions::LOGGING_IN:   // saved password: opens when the room answers
      memcpy(s_login_key, ci.id.pub_key, PUB_KEY_SIZE);
      s_login_wait = true;
      showToast(LV_SYMBOL_REFRESH "  Logging in...", 8000);
      break;
    case RoomSessions::NEED_PASSWORD:
      roomLoginPopup(ci.id.pub_key);
      break;
    default:
      showToast("Login failed - not sent");
      break;
  }
}

void UITask::roomLoginPopup(const uint8_t* pub_key) {
  using namespace convview;
  ContactInfo ci;
  if (!MessageHistory::contactByPrefix(pub_key, ci)) return;
  memcpy(s_login_key, ci.id.pub_key, PUB_KEY_SIZE);
  TextEntry t = { ci.name, onRoomLoginKb };
  t.hint = "Room password (may be empty)";
  t.password = true;
  t.max_bytes = 15;
  navTextEntry(t);
}

void UITask::roomLoginDone(bool ok) {
  using namespace convview;
  ContactInfo ci;
  if (!ok || !_nav_ta || !MessageHistory::contactByPrefix(s_login_key, ci)) { navClosePopup(); return; }
  bool sent = _core->rooms.login(ci, lv_textarea_get_text(_nav_ta));
  navClosePopup();
  s_login_wait = sent;
  showToast(sent ? LV_SYMBOL_REFRESH "  Logging in..." : "Login failed - not sent", sent ? 8000 : 2500);
}

// From loop(), on any screen: a login answer (or its timeout).
void UITask::roomPoll() {
  using namespace convview;
  uint8_t key[4];
  RoomSessions::Outcome o = _core->rooms.take(key);
  if (o == RoomSessions::NONE) return;
  bool mine = s_login_wait && memcmp(key, s_login_key, 4) == 0;
  if (mine) s_login_wait = false;
  bool picking = (_screen == SCR_CHATS || _screen == SCR_HOME) && !_nav_overlay && !locked();   // where it was tapped
  if (o == RoomSessions::LOGGED_IN) {
    showToast("Logged in", 1200);
    if (mine && picking) openDM(s_login_key);
    else if (_screen == SCR_CHATS && !_nav_overlay) refreshChats();
  } else if (o == RoomSessions::LOGIN_FAILED) {
    showToast("Login failed - wrong password?", 3000);
    if (mine && picking) roomLoginPopup(s_login_key);
  } else {
    showToast("No answer from the room", 3000);
  }
}

// ── Conversation options ──────────────────────────────────────────────────────

void UITask::conversationMenu(const uint8_t* pub_key) {
  using namespace convview;
  ContactInfo ci;
  if (!MessageHistory::contactByPrefix(pub_key ? pub_key : _thread_key, ci)) return;
  memcpy(s_key, ci.id.pub_key, PUB_KEY_SIZE);
  lv_obj_t* panel = navPopupPanel(ci.name, false);
  bool room = ci.type == ADV_TYPE_ROOM;

  if (!room) {
    lv_obj_t* g = group(panel, nullptr);
    choiceRow(g, "Alerts", nullptr, "Default\nMuted\nAlways", contactctl::notif(_prefs, ci.id.pub_key), onConvNotif);
    choiceRow(g, "Sound", nullptr, "Default\nMelody 1\nMelody 2", contactctl::melody(_prefs, ci.id.pub_key), onConvMelody);
  } else {
    bool in = _core->rooms.isLoggedIn(ci.id.pub_key);
    lv_obj_t* st = label(panel, in ? LV_SYMBOL_OK "  Logged in: you can post"
                                   : "Not logged in this session", THEME_FONT_SMALL, in ? theme::OK : theme::TEXT_MUTED);
    lv_obj_set_width(st, LV_PCT(100));
  }

  lv_obj_t* acts = buttonBar(panel);
  s_fav_btn = barButton(acts, UI_SYMBOL_STAR " Fav", onConvAction, A_FAV, contactctl::favourite(ci));
  if (!room && _core->dmUnread(ci.id.pub_key) > 0) barButton(acts, LV_SYMBOL_OK " Read", onConvAction, A_READ, false);
  barButton(acts, UI_SYMBOL_TACK, onConvAction, A_PIN, favslots::findContact(_prefs, ci.id.pub_key) >= 0);
  if (room) {
    barButton(acts, LV_SYMBOL_EDIT " Login", onConvAction, A_LOGIN, false);
    if (_core->rooms.isLoggedIn(ci.id.pub_key)) barButton(acts, LV_SYMBOL_CLOSE " Logout", onConvAction, A_LOGOUT, false);
  }
}

void UITask::conversationNotif(int v) {
  if (!_prefs || v < 0 || v > 2) return;
  contactctl::setNotif(_prefs, convview::s_key, (uint8_t)v);
  prefsSave();
}

void UITask::conversationAction(uint8_t act) {
  using namespace convview;
  ContactInfo ci;
  if (!MessageHistory::contactByPrefix(s_key, ci)) { navClosePopup(); return; }
  switch (act) {
    case A_FAV: {
      bool on = !contactctl::favourite(ci);
      if (!contactctl::setFavourite(ci.id.pub_key, on)) break;
      if (s_fav_btn) barButtonOn(s_fav_btn, on);
      showToast(on ? "Added to favourites" : "Removed from favourites", 1200);
      if (_screen == SCR_CHATS) { buildChats(); conversationMenu(s_key); }   // star / filter
      break;
    }
    case A_READ:
      _core->clearDMUnread(ci.id.pub_key);
      navClosePopup();
      if (_screen == SCR_CHATS) buildChats();
      break;
    case A_LOGIN:
      roomLoginPopup(ci.id.pub_key);
      break;
    case A_PIN:
      pinPopup(false, 0, ci.id.pub_key);   // DeviceScreen.h
      break;
    case A_LOGOUT:
      _core->rooms.logout(ci.id.pub_key);
      navClosePopup();
      showToast("Logged out - password forgotten", 2000);
      if (_screen == SCR_THREAD) showChats();
      else if (_screen == SCR_CHATS) buildChats();
      break;
  }
}

// ── Message actions ───────────────────────────────────────────────────────────

// One stop on the path diagram: a dot on a vertical line (the line runs on to
// the next stop unless `last`), the name beside it, what it is on the right.
static void pathStop(lv_obj_t* parent, const char* name, const char* role, bool first, bool last,
                     uint32_t col, bool end) {
  lv_obj_t* row = lv_obj_create(parent);
  lv_obj_remove_style_all(row);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(row, LV_PCT(100), 22);
  if (!first || !last) {
    lv_obj_t* line = lv_obj_create(row);
    lv_obj_remove_style_all(line);
    lv_obj_set_style_bg_color(line, lv_color_hex(theme::TEXT_MUTED), 0);
    lv_obj_set_style_bg_opa(line, LV_OPA_50, 0);
    lv_obj_set_size(line, 2, first || last ? 11 : 22);
    lv_obj_set_pos(line, 6, first ? 11 : 0);
  }
  lv_obj_t* dot = lv_obj_create(row);
  lv_obj_remove_style_all(dot);
  lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
  lv_obj_set_style_bg_color(dot, lv_color_hex(end ? col : theme::SURFACE), 0);
  lv_obj_set_style_border_color(dot, lv_color_hex(col), 0);
  lv_obj_set_style_border_width(dot, end ? 0 : 2, 0);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_size(dot, end ? 10 : 8, end ? 10 : 8);
  lv_obj_align(dot, LV_ALIGN_LEFT_MID, end ? 2 : 3, 0);
  lv_obj_t* t = label(row, name, end ? THEME_FONT_BODY : THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 190);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 22, 0);
  if (role && role[0]) lv_obj_align(label(row, role, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, 0, 0);
}

// The stops of a path, in a box that scrolls once there are many (up to 64 hops).
static lv_obj_t* pathDiagram(lv_obj_t* panel) {
  lv_obj_t* diag = lv_obj_create(panel);
  lv_obj_remove_style_all(diag);
  lv_obj_set_size(diag, LV_PCT(100), LV_SIZE_CONTENT);
  lv_obj_set_style_max_height(diag, 5 * 22, 0);
  lv_obj_set_style_pad_right(diag, 6, 0);   // the roles clear this box's own scrollbar
  lv_obj_set_flex_flow(diag, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_scroll_dir(diag, LV_DIR_VER);
  return diag;
}

void UITask::messageMenu(int idx) {
  using namespace convview;
  if (idx < 0 || idx >= s_msg_meta_n) return;
  const MsgMeta& m = s_msg_meta[idx];
  s_msg = idx;
  lv_obj_t* panel = navPopupPanel(m.own ? "My message" : (m.from[0] ? m.from : "Message"), false);

  const MessageHistory& h = _core->history;
  uint8_t packed = 0; const uint8_t* path = nullptr; bool relay = false;
  uint32_t ts = 0; const char* body = "";
  AckState dm_st = ACK_NONE;
  if (m.pos >= 0) {
    if (m.channel) {
      const ChHistEntry& e = s_th_ch[m.pos];
      packed = e.path_len; path = e.path; relay = m.own; ts = e.timestamp;
      const char* sep = strstr(e.text, ": ");
      body = sep ? sep + 2 : e.text;
    } else {
      const DmHistEntry& e = s_th_dm[m.pos];
      packed = e.path_len; path = e.path; ts = e.timestamp; body = e.text;
      if (e.outgoing) dm_st = h.dmEffectiveStatus(e);
      else {
        ContactInfo tc;   // a room post is filed "Author: text"
        if (MessageHistory::contactByPrefix(_thread_key, tc) && tc.type == ADV_TYPE_ROOM) {
          const char* sep = strstr(e.text, ": ");
          if (sep) body = sep + 2;
        }
      }
    }
  }

  // The message itself, quoted short, and when.
  char quote[MAX_TEXT_LEN + 1];
  snprintf(quote, sizeof(quote), "%s", body);
  plainMentions(quote);
  lv_obj_t* q = label(panel, quote, THEME_FONT_SMALL, theme::TEXT);
  lv_label_set_long_mode(q, LV_LABEL_LONG_DOT);
  lv_obj_set_width(q, LV_PCT(100));
  lv_obj_set_style_max_height(q, 34, 0);   // two lines
  lv_obj_set_style_border_side(q, LV_BORDER_SIDE_LEFT, 0);
  lv_obj_set_style_border_width(q, 2, 0);
  lv_obj_set_style_border_color(q, lv_color_hex(m.own ? theme::ACCENT : theme::SURFACE_2), 0);
  lv_obj_set_style_pad_left(q, 8, 0);
  char when[64];
  whenText(when, sizeof(when), ts, packed, m.own, _prefs);
  uint8_t hops = contactctl::hopCount(packed);
  s_msg_when = label(panel, when, THEME_FONT_SMALL, theme::TEXT_MUTED);
  s_msg_overlay = _nav_overlay;
  s_msg_ts = ts;
  s_msg_state = packed | (uint32_t)dm_st << 8;

  if (m.own && !m.channel) {   // a DM we sent: its end-to-end delivery, in words
    const char* st = "Sent"; uint32_t col = theme::TEXT_MUTED;
    switch (dm_st) {
      case ACK_OK:      st = LV_SYMBOL_OK " Delivered"; col = theme::OK; break;
      case ACK_FAIL:    st = LV_SYMBOL_CLOSE " Not delivered"; col = theme::FAIL; break;
      case ACK_PENDING: st = "Sending..."; break;
      default: break;
    }
    label(panel, st, THEME_FONT_BODY, col);
  } else if (relay) {   // our channel post: the repeaters heard passing it on
    char hd[48];
    if (hops > 0) snprintf(hd, sizeof(hd), "Heard %u repeater%s pass it on", hops, hops == 1 ? "" : "s");
    else snprintf(hd, sizeof(hd), "No repeater heard passing it on yet");
    label(panel, hd, THEME_FONT_BODY, hops > 0 ? theme::OK : theme::TEXT_MUTED);
    if (hops > 0) {   // each one an echo of ours: a fan out from us, not a chain
      lv_obj_t* diag = pathDiagram(panel);
      pathStop(diag, the_mesh.getNodeName(), "you", true, false, theme::ACCENT, true);
      for (uint8_t i = 0; i < hops; i++) {
        char nm[33];
        contactctl::hopName(packed, path, i, nm, sizeof(nm));
        pathStop(diag, nm, UI_SYMBOL_RADIO, false, i == hops - 1, theme::OK, false);
      }
    }
  } else {   // incoming: sender, each hop, then us
    lv_obj_t* diag = pathDiagram(panel);
    pathStop(diag, m.from[0] ? m.from : "Sender", "sender", true, false, theme::ACCENT, true);
    for (uint8_t i = 0; i < hops; i++) {
      char nm[33], role[12];
      contactctl::hopName(packed, path, i, nm, sizeof(nm));
      snprintf(role, sizeof(role), "hop %u", (unsigned)(i + 1));
      pathStop(diag, nm, role, false, false, theme::TEXT_MUTED, false);
    }
    pathStop(diag, the_mesh.getNodeName(), "you", false, true, theme::OK, true);
  }

  bool can_reply = !m.own && m.from[0] && _compose_ta;
  if (!can_reply && m.loc < 0) return;
  lv_obj_t* acts = buttonBar(panel);
  if (can_reply) barButton(acts, LV_SYMBOL_EDIT " Reply", onMsgAction, M_REPLY, false);
  if (m.loc >= 0) barButton(acts, UI_SYMBOL_COMPASS " Set target", onMsgAction, M_TARGET, false);
}

// The open message popup, every half second on a conversation: the age in
// its "when" line, and opened anew when the repeaters heard passing it on or
// its delivery change. Found again by its time (the page may have moved).
void UITask::messagePopupTick() {
  using namespace convview;
  if (!_nav_overlay || _nav_overlay != s_msg_overlay) return;
  const MessageHistory& h = _core->history;
  for (int i = 0; i < s_msg_meta_n; i++) {
    const MsgMeta& m = s_msg_meta[i];
    if (m.pos < 0) continue;
    uint32_t ts, state;
    uint8_t packed;
    if (m.channel) {
      const ChHistEntry& e = s_th_ch[m.pos];
      ts = e.timestamp; packed = e.path_len; state = packed | (uint32_t)ACK_NONE << 8;
    } else {
      const DmHistEntry& e = s_th_dm[m.pos];
      ts = e.timestamp; packed = e.path_len;
      state = packed | (uint32_t)(e.outgoing ? h.dmEffectiveStatus(e) : ACK_NONE) << 8;
    }
    if (ts != s_msg_ts) continue;
    if (state != s_msg_state) { messageMenu(i); return; }
    char when[64];
    whenText(when, sizeof(when), ts, packed, m.own, _prefs);
    setText(s_msg_when, when);
    return;
  }
}

void UITask::messageAction(uint8_t act) {
  using namespace convview;
  if (s_msg < 0 || s_msg >= s_msg_meta_n) { navClosePopup(); return; }
  const MsgMeta& m = s_msg_meta[s_msg];
  navClosePopup();
  if (act == M_REPLY && _compose_ta) {
    char pre[40];
    contactctl::replyPrefix(m.from, pre, sizeof(pre));
    lv_textarea_set_text(_compose_ta, pre);
    setKeyboardVisible(true);
  } else if (act == M_TARGET && m.loc >= 0 && m.loc < s_msg_loc_n) {
    const MsgLoc& l = s_msg_locs[m.loc];
    navSetTarget(0, nullptr, l.lat, l.lon, l.label);   // a place snapshotted from the text
    char msg[48];
    snprintf(msg, sizeof(msg), "Target: %s", l.label);
    showToast(msg);
  }
}

// ── Filters ───────────────────────────────────────────────────────────────────

void UITask::toggleChatFilter(uint8_t which) {
  if (!_prefs) return;
  switch (which) {
    case CF_CHANNELS: _prefs->ch_fav_only ^= 1; break;
    case CF_ROOMS:    _prefs->room_fav_only ^= 1; break;
    case CF_CONTACTS: _prefs->dm_show_all ^= 1; break;
  }
  prefsSave();
  if (_screen == SCR_CONTACTS) buildContacts(); else refreshChats();
}
