#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SettingsScreen.h is defined.

class QuickMsgScreen : public UIScreen {
  UITask* _task;

  enum Phase { MODE_SELECT, CONTACT_PICK, DM_HIST, MSG_PICK, CHANNEL_PICK, CHANNEL_HIST, KEYBOARD };
  Phase _phase;

  // MODE_SELECT
  int _mode_sel;  // 0=Direct, 1=Channel, 2=Room Servers

  // CONTACT_PICK
  int _contact_sel, _contact_scroll;
  int _num_contacts;
  uint16_t _sorted[MAX_CONTACTS];
  ContactInfo _sel_contact;
  bool _room_mode;  // true = picking a room server, false = picking a DM contact

  // CHANNEL_PICK
  int _channel_sel, _channel_scroll;
  int _num_channels;
  uint8_t _channel_indices[MAX_GROUP_CHANNELS];
  uint8_t _ch_unread[MAX_GROUP_CHANNELS];
  int _sel_channel_idx;
  bool _sending_to_channel;

  // MSG_PICK (shared)
  int _msg_sel, _msg_scroll;
  int _active_msgs[QUICK_MSGS_MAX];
  int _active_msg_count;

  // CHANNEL_HIST
  int _hist_sel, _hist_scroll;
  FullscreenMsgView _fs;
  int  _unread_at_entry;    // _ch_unread value when entering CHANNEL_HIST
  int  _viewing_max_seen;  // highest _hist_sel reached in current session
  static const int CH_HIST_MAX = 32;

  // KEYBOARD
  KeyboardWidget _kb;

  // Context menu (opened by KEY_CONTEXT_MENU in CHANNEL_PICK / CONTACT_PICK / histories)
  PopupMenu _ctx_menu;
  bool      _ctx_dirty;
  char      _ctx_notif_item[22];
  char      _ctx_melody_item[20];
  char      _reply_prefix[36];   // "@[nick] " built when reply is triggered
  bool      _reply_mode;         // true while composing a reply (prefix is prepended)

  struct ChHistEntry { uint8_t ch_idx; char text[140]; };
  ChHistEntry _hist[CH_HIST_MAX];
  int _hist_head, _hist_count;

  // DM_HIST
  struct DmHistEntry { uint8_t prefix[4]; uint8_t outgoing; char text[80]; };
  static const int DM_HIST_MAX = 64;
  DmHistEntry _dm_hist[DM_HIST_MAX];
  int _dm_hist_head, _dm_hist_count;
  int _dm_hist_sel, _dm_hist_scroll;
  FullscreenMsgView _dm_fs;

  static const int VISIBLE      = 4;
  static const int ITEM_H       = 12;
  static const int START_Y      = 12;
  static const int HIST_VISIBLE = 2;   // 2-line boxed history entries
  static const int HIST_ITEM_H  = 21;  // box(19) + gap(2)
  static const int HIST_BOX_H   = 19;
  static const int HIST_START_Y = 11;

  void expandMsg(const char* tmpl, char* out, int out_len) const {
    double lat = 0, lon = 0;
    bool gps_valid = false;
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    if (loc && loc->isValid()) {
      lat = loc->getLatitude() / 1000000.0;
      lon = loc->getLongitude() / 1000000.0;
      gps_valid = true;
    }
#endif
    NodePrefs* np = _task->getNodePrefs();
    float batt = (float)board.getBattMilliVolts() / 1000.0f;
    ::expandMsg(tmpl, out, out_len, lat, lon, gps_valid,
                rtc_clock.getCurrentTime(),
                np ? np->tz_offset_hours : 0,
                &sensors, batt);
  }

  // Build "@[nick] " prefix from a channel message text ("nick: body") into _reply_prefix.
  // Returns false if sender is "Me" (own message — no reply prefix needed).
  bool buildChannelReplyPrefix(const char* text) {
    const char* sep = strstr(text, ": ");
    if (!sep) return false;
    int slen = (int)(sep - text);
    if (slen == 2 && strncmp(text, "Me", 2) == 0) return false;
    if (slen > 31) slen = 31;
    snprintf(_reply_prefix, sizeof(_reply_prefix), "@[%.*s] ", slen, text);
    return true;
  }

  void startReply(bool to_channel) {
    _sending_to_channel = to_channel;
    _reply_mode = true;
    setupMsgPick();
    _phase = MSG_PICK;
  }

  void setupMsgPick() {
    _msg_sel = _msg_scroll = 0;
    _active_msg_count = 0;
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      for (int i = 0; i < QUICK_MSGS_MAX; i++) {
        if (p->custom_msgs[i][0] != '\0')
          _active_msgs[_active_msg_count++] = i;
      }
    }
  }

  void renderScrollHints(DisplayDriver& display, int scroll, int count) {
    display.setColor(DisplayDriver::LIGHT);
    if (scroll > 0) {
      display.setCursor(display.width() - 6, START_Y);
      display.print("^");
    }
    if (scroll + VISIBLE < count) {
      display.setCursor(display.width() - 6, START_Y + (VISIBLE-1)*ITEM_H);
      display.print("v");
    }
  }

  // count history entries for a specific channel
  int histCountForChannel(int ch_idx) const {
    int n = 0;
    for (int i = 0; i < _hist_count; i++) {
      if (_hist[(_hist_head + i) % CH_HIST_MAX].ch_idx == (uint8_t)ch_idx) n++;
    }
    return n;
  }

  // get ring-buffer position of j-th history entry for channel (newest first)
  int histEntryForChannel(int ch_idx, int j) const {
    int n = 0;
    for (int i = _hist_count - 1; i >= 0; i--) {
      int pos = (_hist_head + i) % CH_HIST_MAX;
      if (_hist[pos].ch_idx == (uint8_t)ch_idx) {
        if (n == j) return pos;
        n++;
      }
    }
    return -1;
  }

  void storeDMMsg(const uint8_t* pub_key, bool outgoing, const char* text) {
    int pos;
    if (_dm_hist_count < DM_HIST_MAX) {
      pos = (_dm_hist_head + _dm_hist_count) % DM_HIST_MAX;
      _dm_hist_count++;
    } else {
      pos = _dm_hist_head;
      _dm_hist_head = (_dm_hist_head + 1) % DM_HIST_MAX;
    }
    memcpy(_dm_hist[pos].prefix, pub_key, 4);
    _dm_hist[pos].outgoing = outgoing ? 1 : 0;
    strncpy(_dm_hist[pos].text, text, sizeof(DmHistEntry::text) - 1);
    _dm_hist[pos].text[sizeof(DmHistEntry::text) - 1] = '\0';
  }

  int dmHistCountForContact(const uint8_t* prefix) const {
    int n = 0;
    for (int i = 0; i < _dm_hist_count; i++)
      if (memcmp(_dm_hist[(_dm_hist_head + i) % DM_HIST_MAX].prefix, prefix, 4) == 0) n++;
    return n;
  }

  int dmHistEntryForContact(const uint8_t* prefix, int j) const { // j=0 = newest
    int n = 0;
    for (int i = _dm_hist_count - 1; i >= 0; i--) {
      int pos = (_dm_hist_head + i) % DM_HIST_MAX;
      if (memcmp(_dm_hist[pos].prefix, prefix, 4) == 0) {
        if (n == j) return pos;
        n++;
      }
    }
    return -1;
  }

  void afterSend(bool ok, const char* msg) {
    _reply_mode = false;
    if (ok && _sending_to_channel) {
      _hist_sel = 0;
      _hist_scroll = 0;
      _phase = CHANNEL_HIST;  // set before addChannelMsg so viewing=true, no unread bump
      char entry[sizeof(ChHistEntry::text)];
      snprintf(entry, sizeof(entry), "Me: %s", msg);
      addChannelMsg(_sel_channel_idx, entry);
      // After inserting sent msg at index 0, the unread index range is stale.
      // User is active in this channel — treat as fully read.
      _ch_unread[_sel_channel_idx] = 0;
      _unread_at_entry = 0;
      _viewing_max_seen = 0;
      _task->showAlert("Sent!", 600);
    } else if (ok) {
      storeDMMsg(_sel_contact.id.pub_key, true, msg);
      _dm_hist_sel = 0;
      _dm_hist_scroll = 0;
      _phase = DM_HIST;
      _task->showAlert("Sent!", 600);
    } else {
      _task->showAlert("Send failed", 1500);
      _task->gotoHomeScreen();
    }
  }

  bool sendText(const char* msg) {
    if (_sending_to_channel) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(_sel_channel_idx, ch)) return false;
      return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                       the_mesh.getNodeName(), msg, strlen(msg));
    } else {
      uint32_t expected_ack = 0, est_timeout = 0;
      return the_mesh.sendMessage(_sel_contact, rtc_clock.getCurrentTime(), 0,
                                  msg, expected_ack, est_timeout) > 0;
    }
  }

  void buildContactList() {
    NodePrefs* p = _task->getNodePrefs();
    ContactInfo c;
    int total = the_mesh.getNumContacts();
    _num_contacts = 0;
    if (_room_mode) {
      bool fav_only = (p && p->room_fav_only);
      for (int i = 0; i < total; i++) {
        if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_ROOM) continue;
        if (fav_only && !(c.flags & 0x01)) continue;
        _sorted[_num_contacts++] = i;
      }
    } else {
      bool show_all = (p && p->dm_show_all);
      for (int i = 0; i < total; i++) {
        if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_CHAT) continue;
        if (!show_all && !(c.flags & 0x01)) continue;
        _sorted[_num_contacts++] = i;
      }
    }
  }

  void buildChannelList() {
    _num_channels = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0') {
        _channel_indices[_num_channels++] = (uint8_t)i;
      }
    }
  }

  // Returns per-channel notification state: 0=follow global, 1=muted, 2=force-on
  uint8_t chNotifState(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    uint64_t mask = 1ULL << ch_idx;
    if (!(p->ch_notif_override & mask)) return 0;
    return (p->ch_notif_muted & mask) ? 1 : 2;
  }

  void setChNotifState(uint8_t ch_idx, uint8_t state) {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return;
    uint64_t mask = 1ULL << ch_idx;
    if (state == 0) {
      p->ch_notif_override &= ~mask;
      p->ch_notif_muted    &= ~mask;
    } else if (state == 1) {
      p->ch_notif_override |= mask;
      p->ch_notif_muted    |= mask;
    } else {
      p->ch_notif_override |= mask;
      p->ch_notif_muted    &= ~mask;
    }
  }

  uint8_t dmNotifState(const uint8_t* pub_key) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++)
      if (p->dm_notif[i].state && memcmp(p->dm_notif[i].prefix, pub_key, 4) == 0)
        return p->dm_notif[i].state;
    return 0;
  }

  void setDmNotifState(const uint8_t* pub_key, uint8_t state) {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return;
    for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++) {
      if (p->dm_notif[i].state && memcmp(p->dm_notif[i].prefix, pub_key, 4) == 0) {
        if (state == 0) { memset(&p->dm_notif[i], 0, sizeof(p->dm_notif[i])); }
        else              p->dm_notif[i].state = state;
        return;
      }
    }
    if (state == 0) return;
    for (int i = 0; i < NodePrefs::DM_NOTIF_TABLE_MAX; i++) {
      if (p->dm_notif[i].state == 0) {
        memcpy(p->dm_notif[i].prefix, pub_key, 4);
        p->dm_notif[i].state = state;
        return;
      }
    }
    // table full — overwrite slot 0
    memcpy(p->dm_notif[0].prefix, pub_key, 4);
    p->dm_notif[0].state = state;
  }

  uint8_t chNotifMelody(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    uint64_t mask = 1ULL << ch_idx;
    if (!(p->ch_notif_melody_set & mask)) return 0;
    return (p->ch_notif_melody_2 & mask) ? 2 : 1;
  }

  void setChNotifMelody(uint8_t ch_idx, uint8_t slot) {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return;
    uint64_t mask = 1ULL << ch_idx;
    if (slot == 0) {
      p->ch_notif_melody_set &= ~mask;
      p->ch_notif_melody_2   &= ~mask;
    } else if (slot == 1) {
      p->ch_notif_melody_set |= mask;
      p->ch_notif_melody_2   &= ~mask;
    } else {
      p->ch_notif_melody_set |= mask;
      p->ch_notif_melody_2   |= mask;
    }
  }

  uint8_t dmMelodySlot(const uint8_t* pub_key) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return 0;
    for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++)
      if (p->dm_melody[i].slot && memcmp(p->dm_melody[i].prefix, pub_key, 4) == 0)
        return p->dm_melody[i].slot;
    return 0;
  }

  void setDmMelody(const uint8_t* pub_key, uint8_t slot) {
    NodePrefs* p = _task->getNodePrefs();
    if (!p) return;
    for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++) {
      if (p->dm_melody[i].slot && memcmp(p->dm_melody[i].prefix, pub_key, 4) == 0) {
        if (slot == 0) memset(&p->dm_melody[i], 0, sizeof(p->dm_melody[i]));
        else           p->dm_melody[i].slot = slot;
        return;
      }
    }
    if (slot == 0) return;
    for (int i = 0; i < NodePrefs::DM_MELODY_TABLE_MAX; i++) {
      if (p->dm_melody[i].slot == 0) {
        memcpy(p->dm_melody[i].prefix, pub_key, 4);
        p->dm_melody[i].slot = slot;
        return;
      }
    }
    memcpy(p->dm_melody[0].prefix, pub_key, 4);
    p->dm_melody[0].slot = slot;
  }

public:
  QuickMsgScreen(UITask* task)
    : _task(task), _phase(MODE_SELECT), _mode_sel(0),
      _contact_sel(0), _contact_scroll(0), _num_contacts(0), _room_mode(false),
      _channel_sel(0), _channel_scroll(0), _num_channels(0),
      _sel_channel_idx(0), _sending_to_channel(false),
      _msg_sel(0), _msg_scroll(0), _active_msg_count(0),
      _hist_sel(0), _hist_scroll(0),
      _unread_at_entry(0), _viewing_max_seen(0),
      _hist_head(0), _hist_count(0),
      _dm_hist_head(0), _dm_hist_count(0),
      _dm_hist_sel(-1), _dm_hist_scroll(0),
      _ctx_dirty(false), _reply_mode(false) {
    memset(_ch_unread, 0, sizeof(_ch_unread));
  }

  void addChannelMsg(uint8_t ch_idx, const char* text) {
    int pos;
    if (_hist_count < CH_HIST_MAX) {
      pos = (_hist_head + _hist_count) % CH_HIST_MAX;
      _hist_count++;
    } else {
      pos = _hist_head;
      _hist_head = (_hist_head + 1) % CH_HIST_MAX;
    }
    _hist[pos].ch_idx = ch_idx;
    strncpy(_hist[pos].text, text, sizeof(_hist[pos].text) - 1);
    _hist[pos].text[sizeof(_hist[pos].text) - 1] = '\0';

    if (ch_idx < MAX_GROUP_CHANNELS) {
      bool viewing = (_phase == CHANNEL_HIST && _sel_channel_idx == (int)ch_idx);
      if (!viewing && _ch_unread[ch_idx] < 99) _ch_unread[ch_idx]++;
    }
  }

  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text) {
    storeDMMsg(pub_key, outgoing, text);
  }

  int getDMUnreadTotal() const {
    return _task->getDMUnreadTotal();
  }

  int getTotalChannelUnread() const {
    int total = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) total += _ch_unread[i];
    return total;
  }

  void clearAllChannelUnread() {
    memset(_ch_unread, 0, sizeof(_ch_unread));
  }

  void updateChannelUnread() {
    if (_hist_sel < 0 || _sel_channel_idx < 0 || _sel_channel_idx >= MAX_GROUP_CHANNELS) return;
    if (_hist_sel > _viewing_max_seen) _viewing_max_seen = _hist_sel;
    // histEntryForChannel is newest-first: index 0 = newest (unread), higher = older.
    // Each step down from 0 sees one more message; seen count = max_seen + 1.
    int remaining = _unread_at_entry - (_viewing_max_seen + 1);
    _ch_unread[_sel_channel_idx] = (uint8_t)(remaining > 0 ? remaining : 0);
  }

  void reset() {
    _phase = MODE_SELECT;
    _mode_sel = 0;
    _contact_sel = _contact_scroll = 0;
    _msg_sel = _msg_scroll = 0;
    _channel_sel = _channel_scroll = 0;
    _sending_to_channel = false;

    _room_mode = false;
    buildContactList();
    buildChannelList();

    _ctx_menu.active = false;
    _ctx_dirty = false;
    _unread_at_entry = 0;
    _viewing_max_seen = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_phase == MODE_SELECT) {
      display.drawTextCentered(display.width()/2, 0, "MESSAGE");
      display.fillRect(0, 10, display.width(), 1);
      const char* opts[] = { "Direct message", "Channels", "Room Servers" };
      int badges[3] = {
        getDMUnreadTotal(),
        _task->getChannelUnreadCount(),
        _task->getRoomUnreadCount()
      };
      for (int i = 0; i < 3; i++) {
        int y = START_Y + i * ITEM_H;
        bool sel = (i == _mode_sel);
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");
        display.setCursor(8, y);
        display.print(opts[i]);
        if (badges[i] > 0) {
          char badge[5];
          snprintf(badge, sizeof(badge), "%d", badges[i]);
          int bw = display.getTextWidth(badge) + 2;
          display.setCursor(display.width() - bw, y);
          display.print(badge);
        }
      }
      display.setColor(DisplayDriver::LIGHT);

    } else if (_phase == CONTACT_PICK) {
      display.drawTextCentered(display.width()/2, 0, _room_mode ? "SELECT ROOM" : "SELECT CONTACT");
      display.fillRect(0, 10, display.width(), 1);

      if (_num_contacts == 0) {
        display.drawTextCentered(display.width()/2, 32, _room_mode ? "No room servers" : "No favourites");
        return 5000;
      }

      for (int i = 0; i < VISIBLE && (_contact_scroll+i) < _num_contacts; i++) {
        int list_idx = _contact_scroll + i;
        int mesh_idx = _sorted[list_idx];
        bool sel = (list_idx == _contact_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }

        ContactInfo c;
        if (the_mesh.getContactByIdx(mesh_idx, c)) {
          display.setCursor(0, y);
          display.print(sel ? ">" : " ");
          char filtered[sizeof(c.name)];
          display.translateUTF8ToBlocks(filtered, c.name, sizeof(filtered));
          uint8_t dm_unread = _task->getDMUnread(c.id.pub_key);
          char badge[5] = "";
          int bw = 0;
          if (dm_unread > 0) {
            snprintf(badge, sizeof(badge), "%d", (int)dm_unread);
            bw = display.getTextWidth(badge) + 2;
          }
          display.drawTextEllipsized(8, y, display.width() - 8 - bw - 1, filtered);
          if (dm_unread > 0) {
            display.setCursor(display.width() - bw + 1, y);
            display.print(badge);
          }
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _contact_scroll, _num_contacts);

      // Context menu overlay
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == CHANNEL_PICK) {
      display.drawTextCentered(display.width()/2, 0, "SELECT CHANNEL");
      display.fillRect(0, 10, display.width(), 1);

      if (_num_channels == 0) {
        display.drawTextCentered(display.width()/2, 32, "No channels");
        return 5000;
      }

      for (int i = 0; i < VISIBLE && (_channel_scroll+i) < _num_channels; i++) {
        int list_idx = _channel_scroll + i;
        bool sel = (list_idx == _channel_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");
        ChannelDetails ch;
        if (the_mesh.getChannel(_channel_indices[list_idx], ch)) {
          uint8_t unread = _ch_unread[_channel_indices[list_idx]];
          char badge[5] = "";
          int bw = 0;
          if (unread > 0) {
            snprintf(badge, sizeof(badge), "%d", (int)unread);
            bw = display.getTextWidth(badge) + 2;
          }
          display.drawTextEllipsized(8, y, display.width() - 10 - bw, ch.name);
          if (unread > 0) {
            display.setCursor(display.width() - bw, y);
            display.print(badge);
          }
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _channel_scroll, _num_channels);

      // Context menu overlay
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == DM_HIST) {
      display.setTextSize(1);
      char filtered_name[sizeof(_sel_contact.name)];
      display.translateUTF8ToBlocks(filtered_name, _sel_contact.name, sizeof(filtered_name));

      if (_dm_fs.active && _dm_hist_sel >= 0) {
        int ring_pos = dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
        if (ring_pos >= 0) {
          const DmHistEntry& e = _dm_hist[ring_pos];
          const char* sender = e.outgoing ? "Me" : filtered_name;
          int dm_count = dmHistCountForContact(_sel_contact.id.pub_key);
          int ret = _dm_fs.render(display, sender, e.text,
                                  _dm_hist_sel < dm_count - 1,
                                  _dm_hist_sel > 0);
          if (_ctx_menu.active) _ctx_menu.render(display);
          return ret;
        }
        return 500;
      }

      char title[24];
      display.setColor(DisplayDriver::LIGHT);
      snprintf(title, sizeof(title), "%.23s", filtered_name);
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 9, display.width(), 1);

      int dm_count = dmHistCountForContact(_sel_contact.id.pub_key);

      for (int i = 0; i < HIST_VISIBLE && (_dm_hist_scroll + i) < dm_count; i++) {
        int item = _dm_hist_scroll + i;
        bool sel = (item == _dm_hist_sel);
        int y = HIST_START_Y + i * HIST_ITEM_H;

        int ring_pos = dmHistEntryForContact(_sel_contact.id.pub_key, item);
        if (ring_pos < 0) continue;

        const DmHistEntry& e = _dm_hist[ring_pos];
        const char* sender = e.outgoing ? "Me" : filtered_name;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y, display.width(), HIST_BOX_H);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, e.text);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(0, y, display.width(), HIST_BOX_H);
          display.fillRect(1, y + 1, display.width() - 2, 8);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.setColor(DisplayDriver::LIGHT);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, e.text);
        }
      }

      if (dm_count == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width()/2, 32, "No messages yet");
      }

      display.setColor(DisplayDriver::LIGHT);
      if (_dm_hist_scroll > 0) {
        display.setCursor(display.width() - 6, HIST_START_Y + 1);
        display.print("^");
      }
      if (_dm_hist_scroll + HIST_VISIBLE < dm_count) {
        display.setCursor(display.width() - 6, HIST_START_Y + HIST_VISIBLE * HIST_ITEM_H - 10);
        display.print("v");
      }

      bool compose_sel = (_dm_hist_sel == -1);
      const char* ctxt = "[+ send]";
      int ctw = display.getTextWidth(ctxt);
      int cbx = 1, cby = 55;
      if (compose_sel) {
        display.fillRect(cbx, cby - 1, ctw + 4, 9);
        display.setColor(DisplayDriver::DARK);
      }
      display.setCursor(cbx + 2, cby);
      display.print(ctxt);
      display.setColor(DisplayDriver::LIGHT);
      if (_ctx_menu.active) _ctx_menu.render(display);
      return dm_count > 0 ? 500 : 2000;

    } else if (_phase == CHANNEL_HIST) {
      if (_fs.active && _hist_sel >= 0) {
        int fs_hist_count = histCountForChannel(_sel_channel_idx);
        int ring_pos = histEntryForChannel(_sel_channel_idx, _hist_sel);
        if (ring_pos >= 0) {
          const char* ftext = _hist[ring_pos].text;
          const char* fsep = strstr(ftext, ": ");
          char fsender[22], fmsg[140];
          if (fsep) {
            int nl = fsep - ftext; if (nl > 21) nl = 21;
            strncpy(fsender, ftext, nl); fsender[nl] = '\0';
            strncpy(fmsg, fsep + 2, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          } else {
            strncpy(fsender, "?", sizeof(fsender));
            strncpy(fmsg, ftext, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          }
          int ret = _fs.render(display, fsender, fmsg,
                               _hist_sel < fs_hist_count - 1,
                               _hist_sel > 0);
          if (_ctx_menu.active) _ctx_menu.render(display);
          return ret;
        }
        return 2000;
      }

      ChannelDetails ch;
      the_mesh.getChannel(_sel_channel_idx, ch);
      char title[24];
      snprintf(title, sizeof(title), "%.23s", ch.name);
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 9, display.width(), 1);

      int ch_hist_count = histCountForChannel(_sel_channel_idx);

      for (int i = 0; i < HIST_VISIBLE && (_hist_scroll + i) < ch_hist_count; i++) {
        int item = _hist_scroll + i;
        bool sel = (item == _hist_sel);
        int y = HIST_START_Y + i * HIST_ITEM_H;

        int ring_pos = histEntryForChannel(_sel_channel_idx, item);
        if (ring_pos < 0) continue;

        const char* text = _hist[ring_pos].text;
        const char* sep = strstr(text, ": ");
        char sender[22], msg_part[79];
        if (sep) {
          int nl = sep - text; if (nl > 21) nl = 21;
          strncpy(sender, text, nl); sender[nl] = '\0';
          strncpy(msg_part, sep + 2, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        } else {
          strncpy(sender, "?", sizeof(sender));
          strncpy(msg_part, text, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        }

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y, display.width(), HIST_BOX_H);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, msg_part);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(0, y, display.width(), HIST_BOX_H);
          display.fillRect(1, y + 1, display.width() - 2, 8);
          display.setColor(DisplayDriver::DARK);
          display.drawTextEllipsized(3, y + 1, display.width() - 6, sender);
          display.setColor(DisplayDriver::LIGHT);
          display.drawTextEllipsized(3, y + 10, display.width() - 6, msg_part);
        }
      }

      if (ch_hist_count == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width()/2, 32, "No messages yet");
      }

      // scroll hints
      display.setColor(DisplayDriver::LIGHT);
      if (_hist_scroll > 0) {
        display.setCursor(display.width() - 6, HIST_START_Y + 1);
        display.print("^");
      }
      if (_hist_scroll + HIST_VISIBLE < ch_hist_count) {
        display.setCursor(display.width() - 6, HIST_START_Y + HIST_VISIBLE * HIST_ITEM_H - 10);
        display.print("v");
      }

      // small compose button (bottom-left, always bordered, inverted when selected)
      bool compose_sel = (_hist_sel == -1);
      const char* ctxt = "[+ send]";
      int ctw = display.getTextWidth(ctxt);
      int cbx = 1, cby = 55;
      if (compose_sel) {
        display.setColor(DisplayDriver::LIGHT);
        display.fillRect(cbx - 1, cby - 1, ctw + 4, 10);
        display.setColor(DisplayDriver::DARK);
      } else {
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(cbx - 1, cby - 1, ctw + 4, 10);
      }
      display.setCursor(cbx + 1, cby);
      display.print(ctxt);
      display.setColor(DisplayDriver::LIGHT);
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == KEYBOARD) {
      return _kb.render(display);

    } else { // MSG_PICK
      char title[24];
      if (_reply_mode) {
        int rlen = (int)strlen(_reply_prefix) - 4; // exclude "@[" and "] "
        if (rlen < 0) rlen = 0;
        if (rlen > 20) rlen = 20;
        char nick_raw[32], nick_trans[32];
        snprintf(nick_raw, sizeof(nick_raw), "%.*s", rlen, _reply_prefix + 2);
        display.translateUTF8ToBlocks(nick_trans, nick_raw, sizeof(nick_trans));
        snprintf(title, sizeof(title), "RE:%s", nick_trans);
      } else if (_sending_to_channel) {
        ChannelDetails ch;
        the_mesh.getChannel(_sel_channel_idx, ch);
        snprintf(title, sizeof(title), "%.23s", ch.name);
      } else {
        snprintf(title, sizeof(title), "TO:%.14s", _sel_contact.name);
      }
      display.drawTextCentered(display.width()/2, 0, title);
      display.fillRect(0, 10, display.width(), 1);

      int total_msg_items = 1 + _active_msg_count;
      for (int i = 0; i < VISIBLE && (_msg_scroll+i) < total_msg_items; i++) {
        int idx = _msg_scroll + i;
        bool sel = (idx == _msg_sel);
        int y = START_Y + i * ITEM_H;

        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(0, y - 1, display.width(), ITEM_H - 1);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(0, y);
        display.print(sel ? ">" : " ");

        if (idx == 0) {
          display.setCursor(8, y);
          display.print("Custom message...");
        } else {
          NodePrefs* p = _task->getNodePrefs();
          int slot = _active_msgs[idx - 1];
          const char* tmpl = p ? p->custom_msgs[slot] : "";
          display.drawTextEllipsized(8, y, display.width() - 10, tmpl);
        }
      }
      display.setColor(DisplayDriver::LIGHT);
      renderScrollHints(display, _msg_scroll, total_msg_items);
    }
    return 2000;
  }

  bool handleInput(char c) override {
    if (_phase == MODE_SELECT) {
      if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
      if (c == KEY_UP && _mode_sel > 0) { _mode_sel--; return true; }
      if (c == KEY_DOWN && _mode_sel < 2) { _mode_sel++; return true; }
      if (c == KEY_ENTER) {
        if (_mode_sel == 1) {
          _phase = CHANNEL_PICK;
        } else {
          _room_mode = (_mode_sel == 2);
          if (_room_mode) _task->clearRoomUnread();
          buildContactList();
          _contact_sel = _contact_scroll = 0;
          _phase = CONTACT_PICK;
        }
        return true;
      }

    } else if (_phase == CONTACT_PICK) {
      // Context menu consumes all input while open
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _num_contacts > 0) {
          ContactInfo ci;
          if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) {
            if (_ctx_menu.selectedIndex() == 0) {
              _task->clearDMUnread(ci.id.pub_key);
            } else if (_ctx_menu.selectedIndex() == 1) {
              uint8_t nstate = dmNotifState(ci.id.pub_key);
              setDmNotifState(ci.id.pub_key, (nstate + 1) % 3);
              _ctx_dirty = true;
            } else {
              uint8_t slot = dmMelodySlot(ci.id.pub_key);
              setDmMelody(ci.id.pub_key, (slot + 1) % 3);
              _ctx_dirty = true;
            }
          }
        }
        if (res != PopupMenu::NONE && _ctx_dirty) {
          the_mesh.savePrefs(); _ctx_dirty = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _room_mode = false; _phase = MODE_SELECT; return true; }
      if (c == KEY_UP && _contact_sel > 0) {
        _contact_sel--;
        if (_contact_sel < _contact_scroll) _contact_scroll = _contact_sel;
        return true;
      }
      if (c == KEY_DOWN && _contact_sel < _num_contacts - 1) {
        _contact_sel++;
        if (_contact_sel >= _contact_scroll + VISIBLE) _contact_scroll = _contact_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER && _num_contacts > 0) {
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
          _task->clearDMUnread(_sel_contact.id.pub_key);
          _dm_hist_sel = -1;
          _dm_hist_scroll = 0;
          _dm_fs.active = false;
          _phase = DM_HIST;
        }
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _num_contacts > 0 && !_room_mode) {
        static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
        ContactInfo ci;
        the_mesh.getContactByIdx(_sorted[_contact_sel], ci);
        snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s",
                 NOTIF_LABELS[dmNotifState(ci.id.pub_key)]);
        { static const char* ML[] = { "global", "M1", "M2" };
          snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s",
                   ML[dmMelodySlot(ci.id.pub_key)]); }
        _ctx_menu.begin("Contact options", 3);
        _ctx_menu.addItem("Mark as read");
        _ctx_menu.addItem(_ctx_notif_item);
        _ctx_menu.addItem(_ctx_melody_item);
        _ctx_dirty = false;
        return true;
      }

    } else if (_phase == CHANNEL_PICK) {
      // Context menu consumes all input while open
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _num_channels > 0) {
          uint8_t ch_idx = _channel_indices[_channel_sel];
          if (_ctx_menu.selectedIndex() == 0) {
            _ch_unread[ch_idx] = 0;
          } else if (_ctx_menu.selectedIndex() == 1) {
            uint8_t nstate = chNotifState(ch_idx);
            setChNotifState(ch_idx, (nstate + 1) % 3);
            _ctx_dirty = true;
          } else {
            uint8_t slot = chNotifMelody(ch_idx);
            setChNotifMelody(ch_idx, (slot + 1) % 3);
            _ctx_dirty = true;
          }
        }
        if (res != PopupMenu::NONE && _ctx_dirty) {
          the_mesh.savePrefs(); _ctx_dirty = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = MODE_SELECT; return true; }
      if (c == KEY_UP && _channel_sel > 0) {
        _channel_sel--;
        if (_channel_sel < _channel_scroll) _channel_scroll = _channel_sel;
        return true;
      }
      if (c == KEY_DOWN && _channel_sel < _num_channels - 1) {
        _channel_sel++;
        if (_channel_sel >= _channel_scroll + VISIBLE) _channel_scroll = _channel_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER && _num_channels > 0) {
        _sel_channel_idx = _channel_indices[_channel_sel];
        int hc = histCountForChannel(_sel_channel_idx);
        _unread_at_entry = (int)_ch_unread[_sel_channel_idx];
        _hist_scroll = 0;
        _hist_sel = hc > 0 ? 0 : -1;
        _viewing_max_seen = _hist_sel >= 0 ? _hist_sel : 0;
        _phase = CHANNEL_HIST;
        updateChannelUnread();
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _num_channels > 0) {
        uint8_t ch_idx = _channel_indices[_channel_sel];
        static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
        snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s",
                 NOTIF_LABELS[chNotifState(ch_idx)]);
        { static const char* ML[] = { "global", "M1", "M2" };
          snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s",
                   ML[chNotifMelody(ch_idx)]); }
        _ctx_menu.begin("Channel options", 3);
        _ctx_menu.addItem("Mark all read");
        _ctx_menu.addItem(_ctx_notif_item);
        _ctx_menu.addItem(_ctx_melody_item);
        _ctx_dirty = false;
        return true;
      }

    } else if (_phase == DM_HIST) {
      int dm_count = dmHistCountForContact(_sel_contact.id.pub_key);
      if (_dm_fs.active) {
        if (_ctx_menu.active) {
          auto res = _ctx_menu.handleInput(c);
          if (res == PopupMenu::SELECTED) {
            _dm_fs.active = false;
            startReply(false);
          } else if (res != PopupMenu::NONE) {
            _ctx_menu.active = false;
          }
          return true;
        }
        auto res = _dm_fs.handleInput(c);
        if (res == FullscreenMsgView::PREV) {
          if (_dm_hist_sel < dm_count - 1) { _dm_hist_sel++; _dm_fs.scroll = 0; }
        } else if (res == FullscreenMsgView::NEXT) {
          if (_dm_hist_sel > 0) { _dm_hist_sel--; _dm_fs.scroll = 0; }
        } else if (res == FullscreenMsgView::CLOSE) {
          _dm_fs.active = false;
        } else if (res == FullscreenMsgView::REPLY) {
          int ring_pos = dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
          if (ring_pos >= 0 && !_dm_hist[ring_pos].outgoing) {
            snprintf(_reply_prefix, sizeof(_reply_prefix), "@[%.31s] ", _sel_contact.name);
            _ctx_menu.begin("Options", 1);
            _ctx_menu.addItem("Reply");
          }
        }
        return true;
      }
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          startReply(false);
        } else if (res != PopupMenu::NONE) {
          _ctx_menu.active = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = CONTACT_PICK; return true; }
      if (c == KEY_UP) {
        if (_dm_hist_sel > 0) {
          _dm_hist_sel--;
          if (_dm_hist_sel < _dm_hist_scroll) _dm_hist_scroll = _dm_hist_sel;
        } else if (_dm_hist_sel == 0) {
          _dm_hist_sel = -1;
        }
        return true;
      }
      if (c == KEY_DOWN) {
        if (_dm_hist_sel == -1 && dm_count > 0) { _dm_hist_sel = 0; _dm_hist_scroll = 0; }
        else if (_dm_hist_sel >= 0 && _dm_hist_sel < dm_count - 1) {
          _dm_hist_sel++;
          if (_dm_hist_sel >= _dm_hist_scroll + HIST_VISIBLE)
            _dm_hist_scroll = _dm_hist_sel - HIST_VISIBLE + 1;
        }
        return true;
      }
      if (c == KEY_ENTER) {
        if (_dm_hist_sel >= 0) {
          _dm_fs.begin();
        } else {
          _sending_to_channel = false;
          setupMsgPick();
          _phase = MSG_PICK;
        }
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _dm_hist_sel >= 0) {
        int ring_pos = dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
        if (ring_pos >= 0 && !_dm_hist[ring_pos].outgoing) {
          snprintf(_reply_prefix, sizeof(_reply_prefix), "@[%.31s] ", _sel_contact.name);
          _ctx_menu.begin("Options", 1);
          _ctx_menu.addItem("Reply");
        }
        return true;
      }

    } else if (_phase == CHANNEL_HIST) {
      int ch_hist_count = histCountForChannel(_sel_channel_idx);
      if (_fs.active) {
        if (_ctx_menu.active) {
          auto res = _ctx_menu.handleInput(c);
          if (res == PopupMenu::SELECTED) {
            _fs.active = false;
            startReply(true);
          } else if (res != PopupMenu::NONE) {
            _ctx_menu.active = false;
          }
          return true;
        }
        auto res = _fs.handleInput(c);
        if (res == FullscreenMsgView::PREV) {
          if (_hist_sel < ch_hist_count - 1) { _hist_sel++; _fs.scroll = 0; updateChannelUnread(); }
        } else if (res == FullscreenMsgView::NEXT) {
          if (_hist_sel > 0) { _hist_sel--; _fs.scroll = 0; updateChannelUnread(); }
        } else if (res == FullscreenMsgView::CLOSE) {
          _fs.active = false;
        } else if (res == FullscreenMsgView::REPLY) {
          int ring_pos = histEntryForChannel(_sel_channel_idx, _hist_sel);
          if (ring_pos >= 0 && buildChannelReplyPrefix(_hist[ring_pos].text)) {
            _ctx_menu.begin("Options", 1);
            _ctx_menu.addItem("Reply");
          }
        }
        return true;
      }
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          startReply(true);
        } else if (res != PopupMenu::NONE) {
          _ctx_menu.active = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = CHANNEL_PICK; return true; }
      if (c == KEY_UP) {
        if (_hist_sel > 0) { _hist_sel--; if (_hist_sel < _hist_scroll) _hist_scroll = _hist_sel; }
        else if (_hist_sel == 0) _hist_sel = -1;
        updateChannelUnread();
        return true;
      }
      if (c == KEY_DOWN) {
        if (_hist_sel == -1 && ch_hist_count > 0) { _hist_sel = 0; _hist_scroll = 0; }
        else if (_hist_sel >= 0 && _hist_sel < ch_hist_count - 1) {
          _hist_sel++;
          if (_hist_sel >= _hist_scroll + HIST_VISIBLE) _hist_scroll = _hist_sel - HIST_VISIBLE + 1;
        }
        updateChannelUnread();
        return true;
      }
      if (c == KEY_ENTER) {
        if (_hist_sel >= 0) {
          _fs.begin();
          updateChannelUnread();
        } else {
          _sending_to_channel = true;
          setupMsgPick();
          _phase = MSG_PICK;
        }
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _hist_sel >= 0) {
        int ring_pos = histEntryForChannel(_sel_channel_idx, _hist_sel);
        if (ring_pos >= 0 && buildChannelReplyPrefix(_hist[ring_pos].text)) {
          _ctx_menu.begin("Options", 1);
          _ctx_menu.addItem("Reply");
        }
        return true;
      }

    } else if (_phase == KEYBOARD) {
      auto res = _kb.handleInput(c);
      if (res == KeyboardWidget::CANCELLED) {
        _phase = MSG_PICK;
      } else if (res == KeyboardWidget::DONE) {
        int min_len = _reply_mode ? (int)strlen(_reply_prefix) : 0;
        if (_kb.len > min_len) {
          char expanded[KB_MAX_LEN + 1];
          expandMsg(_kb.buf, expanded, sizeof(expanded));
          bool ok = sendText(expanded);
          afterSend(ok, expanded);
        }
      }
      return true;

    } else { // MSG_PICK
      int total_msg_items = 1 + _active_msg_count;
      if (c == KEY_CANCEL) {
        _reply_mode = false;
        _phase = _sending_to_channel ? CHANNEL_HIST : DM_HIST;
        return true;
      }
      if (c == KEY_UP && _msg_sel > 0) {
        _msg_sel--;
        if (_msg_sel < _msg_scroll) _msg_scroll = _msg_sel;
        return true;
      }
      if (c == KEY_DOWN && _msg_sel < total_msg_items - 1) {
        _msg_sel++;
        if (_msg_sel >= _msg_scroll + VISIBLE) _msg_scroll = _msg_sel - VISIBLE + 1;
        return true;
      }
      if (c == KEY_ENTER) {
        if (_msg_sel == 0) {
          _kb.begin(_reply_mode ? _reply_prefix : "");
          kbAddSensorPlaceholders(_kb, &sensors);
          _phase = KEYBOARD;
          return true;
        }
        NodePrefs* p = _task->getNodePrefs();
        int slot = _active_msgs[_msg_sel - 1];
        const char* tmpl = p ? p->custom_msgs[slot] : "OK";
        char msg[140];
        if (_reply_mode) {
          char body[140];
          expandMsg(tmpl, body, sizeof(body));
          snprintf(msg, sizeof(msg), "%s%s", _reply_prefix, body);
        } else {
          expandMsg(tmpl, msg, sizeof(msg));
        }
        bool ok = sendText(msg);
        afterSend(ok, msg);
        return true;
      }
    }
    return false;
  }
};
