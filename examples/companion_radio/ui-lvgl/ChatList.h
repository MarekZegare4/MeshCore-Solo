#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).
// Chats: the conversation list and the contact picker (start a DM).

// ── Conversation list ─────────────────────────────────────────────────────────

static void onOpenChannel(lv_event_t* e) {
  intptr_t i = vlist::arg(e);
  if (i >= 0) s_ui->openChannel((uint8_t)i);
}

// DM rows carry a 4-byte prefix; kept in a static table the rows point into.
static PSRAM_BSS uint8_t s_dm_rows[MessageHistory::DM_HIST_MAX][4];
static const int CONTACT_ROWS_MAX = 256;   // "All" with a full contact table stays usable
static uint8_t (*s_contact_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(CONTACT_ROWS_MAX);
static uint16_t* s_contact_raw = psramBuf<uint16_t>(CONTACT_ROWS_MAX);   // their raw table index, for the name

static PSRAM_BSS uint16_t s_dm_pos[MessageHistory::DM_HIST_MAX];   // its newest message in the DM ring
static bool s_dm_known[MessageHistory::DM_HIST_MAX];      // a contact (has options on hold)
static void onOpenDMRow(lv_event_t* e) {
  intptr_t r = vlist::arg(e);
  if (r >= 0) s_ui->openDM(s_dm_rows[r]);
}
static void onOpenContactRow(lv_event_t* e) {
  intptr_t i = vlist::arg(e);
  if (i >= 0) s_ui->openDM(s_contact_rows[i]);
}
static void onNewChat(lv_event_t* e) { (void)e; s_ui->showContacts(); }
static void onChanRowHold(lv_event_t* e);      // ChannelScreen.h
static void onChanAdd(lv_event_t* e);
static void onChanThreadMenu(lv_event_t* e);
static const int ROOM_ROWS_MAX = 32;
static uint8_t (*s_room_rows)[PUB_KEY_SIZE] = psramBuf<uint8_t[PUB_KEY_SIZE]>(ROOM_ROWS_MAX);
static uint16_t s_room_raw[ROOM_ROWS_MAX];   // their raw table index, for the row
static bool s_chats_read_all = false;        // Messages built with "Read all" in its header
static void onDMRowHold(lv_event_t* e);        // ConversationScreen.h
static void onRoomRow(lv_event_t* e);
static void onRoomRowHold(lv_event_t* e);
static void onConvThreadMenu(lv_event_t* e);
static void onMsgHold(lv_event_t* e);
static void onChatFilter(lv_event_t* e);
enum : uint8_t { CF_CHANNELS, CF_ROOMS, CF_CONTACTS };   // favourites-only filters

// One tappable list row: title, optional muted subtitle, optional badge.
// In a group(): a flat row, the subtitle wrapping, a chevron on the right.
static lv_obj_t* listRow(lv_obj_t* parent, const char* title, const char* sub,
                         lv_event_cb_t cb, void* user) {
  if (isGroup(parent)) {
    lv_obj_t* row = groupLine(parent, true);
    groupText(row, title, sub);
    label(row, LV_SYMBOL_RIGHT, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
    return row;
  }
  lv_obj_t* row = lv_button_create(parent);
  lv_obj_set_size(row, LV_PCT(100), theme::ROW_H);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE), 0);
  lv_obj_set_style_bg_color(row, lv_color_hex(theme::SURFACE_2), LV_STATE_PRESSED);
  lv_obj_set_style_radius(row, theme::RADIUS, 0);
  lv_obj_set_style_shadow_width(row, 0, 0);
  lv_obj_set_style_pad_all(row, 0, 0);
  lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, user);
  lv_obj_t* t = label(row, title, THEME_FONT_BODY, theme::TEXT);
  lv_label_set_long_mode(t, LV_LABEL_LONG_DOT);
  lv_obj_set_width(t, 230);
  lv_obj_align(t, LV_ALIGN_TOP_LEFT, theme::PAD, sub ? 4 : 13);
  lv_obj_set_user_data(row, t);
  if (sub) {
    lv_obj_t* s = label(row, sub, THEME_FONT_SMALL, theme::TEXT_MUTED);
    lv_label_set_long_mode(s, LV_LABEL_LONG_DOT);
    lv_obj_set_size(s, 230, 18);   // fixed height: LONG_DOT cuts instead of wrapping over the title -- the font's whole line (18), or the tails of y and g and emoji are cut
    lv_obj_align(s, LV_ALIGN_BOTTOM_LEFT, theme::PAD, -4);
  }
  return row;
}

// A listRow() for a vlist: the subtitle always there (shown or not), the
// right side free for a value or a badge; texts set by rowSet().
static lv_obj_t* vRow(lv_obj_t* list, lv_event_cb_t cb, lv_event_cb_t hold) {
  lv_obj_t* row = listRow(list, "", "", cb, NULL);
  if (hold) lv_obj_add_event_cb(row, hold, LV_EVENT_LONG_PRESSED, NULL);
  return row;
}
static void rowSet(lv_obj_t* row, const char* title, const char* sub, uint32_t title_col = theme::TEXT) {
  lv_obj_t* t = rowTitle(row);
  lv_obj_t* s = lv_obj_get_child(row, 1);
  setText(t, title);
  setTextColor(t, title_col);
  int32_t y = sub ? 4 : 13;   // as listRow() puts it
  if (lv_obj_get_style_y(t, LV_PART_MAIN) != y) lv_obj_set_y(t, y);
  if (sub) setText(s, sub);
  if (!sub != lv_obj_has_flag(s, LV_OBJ_FLAG_HIDDEN)) lv_obj_set_flag(s, LV_OBJ_FLAG_HIDDEN, !sub);
}

// The same with a value on the right (a distance, a signal), set by rowValue().
static lv_obj_t* vRowValue(lv_obj_t* list, lv_event_cb_t cb) {
  lv_obj_t* row = vRow(list, cb, NULL);
  lv_obj_align(label(row, "", THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_RIGHT_MID, -theme::PAD, 0);
  return row;
}
static void rowValue(lv_obj_t* row, const char* text, uint32_t col) {
  lv_obj_t* r = lv_obj_get_child(row, 2);
  setText(r, text);
  setTextColor(r, col);
}

static lv_obj_t* sectionTitle(lv_obj_t* parent, const char* text) {
  lv_obj_t* l = label(parent, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_set_style_pad_top(l, 4, 0);
  return l;
}

// Messages section title: a chevron and the title fold the section (tap),
// an All / Favourites choice on the right (filter >= 0), as the Nodes list's
// toolbar has. Folded, the title carries the unread count.
enum : uint8_t { FOLD_CHANNELS, FOLD_DIRECT, FOLD_ROOMS };
static int s_chat_fold = -1;   // bit per FOLD_*; -1: not read from NVS yet
static void onChatFold(lv_event_t* e) { s_ui->chatFold((int)(uintptr_t)lv_event_get_user_data(e)); }
static bool chatFolded(int which) {
  if (s_chat_fold < 0) s_chat_fold = lvport::loadChatFold();
  return s_chat_fold & (1 << which);
}

// A section's favourites-only filter: user data = which | what it was << 8.
static void chatFilterChoice(lv_obj_t* row, int which, bool fav_only) {
  lv_obj_t* c = toolbarChoice(row, "All\nFavourites", fav_only ? 1 : 0, "Show");
  lv_obj_align(c, LV_ALIGN_RIGHT_MID, 0, 0);
  lv_obj_add_event_cb(c, onChatFilter, LV_EVENT_VALUE_CHANGED, (void*)(uintptr_t)(which | (fav_only ? 0x100 : 0)));
}

static void chatSection(lv_obj_t* parent, const char* text, int fold, int unread, int filter, bool fav_only) {
  bool folded = chatFolded(fold);
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), 30);
  lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_style_opa(row, LV_OPA_60, LV_STATE_PRESSED);
  lv_obj_add_event_cb(row, onChatFold, LV_EVENT_CLICKED, (void*)(uintptr_t)fold);
  lv_obj_t* ch = label(row, folded ? LV_SYMBOL_RIGHT : LV_SYMBOL_DOWN, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(ch, LV_ALIGN_LEFT_MID, 2, 0);
  lv_obj_t* t = label(row, text, THEME_FONT_SMALL, theme::TEXT_MUTED);
  lv_obj_align(t, LV_ALIGN_LEFT_MID, 22, 0);
  if (folded && unread > 0) {   // what's waiting inside
    lv_obj_t* n = label(row, "", THEME_FONT_SMALL, theme::ACCENT);
    lv_label_set_text_fmt(n, "%d new", unread);
    lv_obj_align_to(n, t, LV_ALIGN_OUT_RIGHT_MID, 8, 0);
  }
  if (filter < 0 || folded) return;
  chatFilterChoice(row, filter, fav_only);
}

// Section title with the All / Favourites choice on the right.
static void sectionWithFilter(lv_obj_t* parent, const char* text, bool fav_only, uint8_t which) {
  lv_obj_t* row = lv_obj_create(parent);
  styleSurface(row, theme::BG);
  lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(row, LV_PCT(100), 30);
  lv_obj_align(label(row, text, THEME_FONT_SMALL, theme::TEXT_MUTED), LV_ALIGN_LEFT_MID, 0, 0);
  chatFilterChoice(row, which, fav_only);
}

void UITask::showChats() {
  _screen = SCR_CHATS;
  buildChats();
}

static void onMarkAllRead(lv_event_t* e) { (void)e; s_ui->markAllRead(); }
static lv_obj_t* headerButton(lv_obj_t* hdr, const char* text, lv_event_cb_t cb, int right, lv_obj_t** label_out);

void UITask::buildChats() {
  lv_obj_t* body = newScreen("Messages", true);
  s_chats_read_all = unreadTotal() > 0;
  if (_header) {   // a new message last, at the edge, whatever the lists below hold
    if (s_chats_read_all) headerButton(_header, LV_SYMBOL_OK " Read all", onMarkAllRead, 0, NULL);
    stylePrimary(headerButton(_header, LV_SYMBOL_EDIT, onNewChat, 0, NULL));
  }
  vlist::attach(body);
  fillChats();
}

// Messages again where it is (a new message, a section folded, back from
// the lock): the rows in view rewritten, the rest as they scroll in. Built
// anew only when "Read all" comes or goes.
void UITask::refreshChats() {
  if (_screen != SCR_CHATS) { s_chats_stale = false; return; }
  if (!_body || !vlist::of(_body) || (unreadTotal() > 0) != s_chats_read_all) {
    int y = _body ? lv_obj_get_scroll_y(_body) : 0;
    buildChats();
    layoutNow(_body);
    lv_obj_scroll_to_y(_body, y, LV_ANIM_OFF);
    return;
  }
  s_chats_stale = vlist::pressed(_body);   // held: once it's let go
  if (!s_chats_stale) fillChats();
}

void UITask::fillChats() {
  lv_obj_t* body = _body;
  vlist::begin(body);
  auto own = [body]() { vlist::own(body, lv_obj_get_child(body, -1)); };

  // Channels: favourites first (unless turned off), hold a row for its options
  static const vlist::Kind CHANNEL = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenChannel, onChanRowHold); },
    [](lv_obj_t* row, intptr_t i) {
      UITask* ui = s_ui;
      ChannelDetails ch;
      if (!the_mesh.getChannel((int)i, ch)) ch.name[0] = '\0';
      char title[48], sub[64] = "";
      snprintf(title, sizeof(title), "%s%s%s", chanctl::favourite(ui->_prefs, (int)i) ? UI_SYMBOL_STAR " " : "", ch.name,
               chanctl::notif(ui->_prefs, (int)i) == chanctl::NOTIF_MUTED ? "  " UI_SYMBOL_MUTE : "");
      if (ui->_core->history.histCountForChannel((int)i) > 0) {
        const ChHistEntry& e = ui->_core->history.chAtPos(ui->_core->history.histEntryForChannel((int)i, 0));
        snprintf(sub, sizeof(sub), "%s", e.text);
        plainMentions(sub);
      }
      rowSet(row, title, sub[0] ? sub : NULL);
      rowBadge(row, ui->_core->history.chUnread((int)i), ui->_core->history.chUnreadOverflow((int)i));
    },
  };
  bool ch_fav_only = _prefs && _prefs->ch_fav_only;
  chatSection(body, "CHANNELS", FOLD_CHANNELS, _core->history.getTotalChannelUnread(), CF_CHANNELS, ch_fav_only);
  own();
  bool fav_first = !_prefs || _prefs->fav_sort;
  int ch_rows = 0;
  if (!chatFolded(FOLD_CHANNELS)) {
    uint8_t order[MAX_GROUP_CHANNELS];
    ch_rows = chanctl::order(_prefs, _core->history, order, ch_fav_only);
    for (int r = 0; r < ch_rows; r++) vlist::add(body, &CHANNEL, order[r]);
  }
  if (!chatFolded(FOLD_CHANNELS)) {
    if (ch_rows == 0 && ch_fav_only) { label(body, "No favourite channels", THEME_FONT_SMALL, theme::TEXT_MUTED); own(); }
    lv_obj_t* add_ch = lv_button_create(body);
    lv_obj_set_size(add_ch, LV_PCT(100), 32);
    lv_obj_set_style_bg_color(add_ch, lv_color_hex(theme::BG), 0);
    lv_obj_set_style_border_color(add_ch, lv_color_hex(theme::SURFACE_2), 0);
    lv_obj_set_style_border_width(add_ch, 1, 0);
    lv_obj_set_style_radius(add_ch, theme::RADIUS, 0);
    lv_obj_set_style_shadow_width(add_ch, 0, 0);
    lv_obj_add_event_cb(add_ch, onChanAdd, LV_EVENT_CLICKED, NULL);
    lv_obj_center(label(add_ch, LV_SYMBOL_PLUS "  Add channel", THEME_FONT_SMALL, theme::TEXT_MUTED));
    vlist::own(body, add_ch);
  }

  // Recent direct conversations (DM ring, newest first, one row per contact)
  static const vlist::Kind DM = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenDMRow, onDMRowHold); },
    [](lv_obj_t* row, intptr_t r) {
      UITask* ui = s_ui;
      const DmHistEntry& e = ui->_core->history.dmAtPos(s_dm_pos[r]);
      ContactInfo c;
      bool known = s_dm_known[r] && MessageHistory::contactByPrefix(s_dm_rows[r], c);
      char name[48];
      contactName(s_dm_rows[r], name, sizeof(name));
      if (known && contactctl::favourite(c)) { char t[48]; snprintf(t, sizeof(t), UI_SYMBOL_STAR " %s", name); strcpy(name, t); }
      if (known && contactctl::notif(ui->_prefs, c.id.pub_key) == contactctl::NOTIF_MUTED) strncat(name, "  " UI_SYMBOL_MUTE, sizeof(name) - strlen(name) - 1);
      char sub[64];
      snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
      plainMentions(sub);
      rowSet(row, name, sub);
      rowBadge(row, ui->_core->dmUnread(s_dm_rows[r]), ui->_core->dmUnreadOverflow(s_dm_rows[r]));
    },
  };
  chatSection(body, "DIRECT", FOLD_DIRECT, _core->dmUnreadTotal(), -1, false);
  own();
  int rows = 0;
  for (int j = 0; j < _core->history.dmHistCount() && rows < MessageHistory::DM_HIST_MAX && !chatFolded(FOLD_DIRECT); j++) {
    int pos = _core->history.dmHistPosNewest(j);
    const DmHistEntry& e = _core->history.dmAtPos(pos);
    bool seen = false;
    for (int r = 0; r < rows; r++) if (memcmp(s_dm_rows[r], e.prefix, 4) == 0) { seen = true; break; }
    if (seen) continue;
    ContactInfo c;
    bool known = MessageHistory::contactByPrefix(e.prefix, c);
    if (known && c.type == ADV_TYPE_ROOM) continue;   // rooms have their own section
    memcpy(s_dm_rows[rows], e.prefix, 4);
    s_dm_pos[rows] = (uint16_t)pos;
    s_dm_known[rows] = known;
    vlist::add(body, &DM, rows);
    rows++;
  }
  if (rows == 0 && !chatFolded(FOLD_DIRECT)) { label(body, "No conversations yet - the pencil, top right", THEME_FONT_SMALL, theme::TEXT_MUTED); own(); }

  // Room servers: tap logs in (saved password or ask) and opens; hold: options
  static const vlist::Kind ROOM = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onRoomRow, onRoomRowHold); },
    [](lv_obj_t* row, intptr_t r) {
      UITask* ui = s_ui;
      ContactInfo c;
      if (!the_mesh.getContactByIdx(s_room_raw[r], c) || memcmp(c.id.pub_key, s_room_rows[r], PUB_KEY_SIZE) != 0) {
        rowSet(row, "?", NULL);   // deleted meanwhile
        return;
      }
      char title[48], sub[64];
      snprintf(title, sizeof(title), "%s%s", contactctl::favourite(c) ? UI_SYMBOL_STAR " " : "", c.name);
      if (ui->_core->history.dmHistCountForContact(c.id.pub_key) > 0) {
        const DmHistEntry& e = ui->_core->history.dmAtPos(ui->_core->history.dmHistEntryForContact(c.id.pub_key, 0));
        snprintf(sub, sizeof(sub), "%s%s", e.outgoing ? "Me: " : "", e.text);
        plainMentions(sub);
      } else {
        snprintf(sub, sizeof(sub), "%s", ui->_core->rooms.isLoggedIn(c.id.pub_key) ? "Logged in" : "Tap to log in");
      }
      rowSet(row, title, sub);
    },
  };
  bool room_fav_only = _prefs && _prefs->room_fav_only;
  chatSection(body, "ROOMS", FOLD_ROOMS, _core->roomUnread(), CF_ROOMS, room_fav_only);
  own();
  int nrooms = 0, total = the_mesh.getNumContacts();
  for (int pass = fav_first ? 0 : 1; pass < 2 && !chatFolded(FOLD_ROOMS); pass++) {
    for (int i = 0; i < total && nrooms < ROOM_ROWS_MAX; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c) || c.type != ADV_TYPE_ROOM) continue;
      bool fav = contactctl::favourite(c);
      if (room_fav_only && !fav) continue;
      if (fav_first && fav != (pass == 0)) continue;
      memcpy(s_room_rows[nrooms], c.id.pub_key, PUB_KEY_SIZE);
      s_room_raw[nrooms] = (uint16_t)(MAX_ANON_CONTACTS + i);
      vlist::add(body, &ROOM, nrooms);
      nrooms++;
    }
  }
  if (nrooms == 0 && !chatFolded(FOLD_ROOMS)) {
    label(body, room_fav_only ? "No favourite rooms" : "No room servers known", THEME_FONT_SMALL, theme::TEXT_MUTED);
    own();
  }
  vlist::end(body);
}

// A section title tapped: folded / unfolded (kept in NVS), the list redrawn
// where it was.
void UITask::chatFold(int which) {
  chatFolded(which);   // loaded
  s_chat_fold ^= 1 << which;
  lvport::saveChatFold(s_chat_fold);
  refreshChats();
}

// ── Contact picker (start a DM) ───────────────────────────────────────────────

void UITask::showContacts() {
  _screen = SCR_CONTACTS;
  buildContacts();
}

void UITask::buildContacts() {
  lv_obj_t* body = newScreen("New message", true);
  bool fav_only = _prefs && !_prefs->dm_show_all;   // ui-new's default: favourites only
  vlist::attach(body);
  vlist::begin(body);
  sectionWithFilter(body, "CONTACTS", fav_only, CF_CONTACTS);
  vlist::own(body, lv_obj_get_child(body, -1));
  int total = the_mesh.getNumContacts();
  int rows = 0;
  // +MAX_ANON_CONTACTS: getContactByIdx() takes the raw table index (see
  // MessageHistory::contactByPrefix()).
  for (int i = 0; i < total && rows < CONTACT_ROWS_MAX; i++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
    if (c.type != ADV_TYPE_CHAT) continue;
    if (fav_only && !contactctl::favourite(c)) continue;
    memcpy(s_contact_rows[rows], c.id.pub_key, PUB_KEY_SIZE);
    s_contact_raw[rows] = (uint16_t)(MAX_ANON_CONTACTS + i);
    rows++;
  }
#ifdef PERF_LIST_MUL
  for (int k = 0, real = rows; real && rows < CONTACT_ROWS_MAX; k++, rows++) {   // a long list from the few there are
    memcpy(s_contact_rows[rows], s_contact_rows[k % real], PUB_KEY_SIZE);
    s_contact_raw[rows] = s_contact_raw[k % real];
  }
#endif
  static const vlist::Kind ROW = {
    theme::ROW_H,
    [](lv_obj_t* list) { return vRow(list, onOpenContactRow, NULL); },
    [](lv_obj_t* row, intptr_t i) {
      ContactInfo c;
      bool same = the_mesh.getContactByIdx(s_contact_raw[i], c) && memcmp(c.id.pub_key, s_contact_rows[i], PUB_KEY_SIZE) == 0;
      rowSet(row, same ? c.name : "?", NULL);   // "?": deleted meanwhile
    },
  };
  for (int i = 0; i < rows; i++) vlist::add(body, &ROW, i);
  if (rows == 0) vlist::own(body, label(body, fav_only ? "No favourites - tap All" : "No contacts yet", THEME_FONT_BODY, theme::TEXT_MUTED));
  vlist::end(body);
}
