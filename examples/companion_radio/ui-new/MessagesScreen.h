#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SettingsScreen.h is defined.

#include "NavView.h"   // navigate to a location shared inside a message
#include "icons.h"     // scalable mini-icons (delivery markers)
#include "ChannelsView.h"  // on-device channel add/edit form (Channels tab)

class MessagesScreen : public UIScreen {
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
  bool _login_mode; // true while KEYBOARD is collecting a room-login password

  // CHANNEL_PICK
  int _channel_sel, _channel_scroll;
  int _num_channels;
  uint8_t _channel_indices[MAX_GROUP_CHANNELS];
  int _sel_channel_idx;
  bool _sending_to_channel;

  // Carries the just-sent DM's ACK tag + deadline + send timestamp from
  // sendText() to afterSend().
  uint32_t _last_ack_tag = 0;
  uint32_t _last_ack_deadline_ms = 0;
  uint32_t _last_send_ts = 0;

  // MSG_PICK (shared)
  int _msg_sel, _msg_scroll;
  int _active_msgs[QUICK_MSGS_MAX];
  int _active_msg_count;

  // CHANNEL_HIST — selection + the unread "viewing session" bookkeeping. The
  // history ring itself and the per-channel unread counters live in _history.
  int _hist_sel, _hist_scroll;
  FullscreenMsgView _fs;
  int  _unread_at_entry;    // channel unread count when entering CHANNEL_HIST
  int  _viewing_max_seen;  // highest _hist_sel reached in current session

  // KEYBOARD
  KeyboardWidget* _kb;

  // Context menu (opened by KEY_CONTEXT_MENU in CHANNEL_PICK / CONTACT_PICK / histories)
  PopupMenu _ctx_menu;
  bool      _ctx_dirty;
  // Channel the open channel-context menu acts on, frozen at open time. The
  // Fav toggle can remove the highlighted channel from a fav-only list, so
  // re-reading _channel_indices[_channel_sel] mid-interaction could silently
  // retarget the menu at a different channel.
  uint8_t   _ctx_ch_idx = 0;
  char      _ctx_notif_item[22];
  char      _ctx_melody_item[20];
  char      _ctx_pin_item[28];   // "Pin to dial" or "Unpin (slot N)"
  char      _ctx_ch_fav_item[12]; // "Fav" or "Unfav"
  char      _pin_slot_labels[NodePrefs::FAVOURITES_COUNT][22];  // per-slot picker labels
  bool      _pin_picker_active;  // true while the slot-picker submenu is open
  bool      _dm_direct_entry;    // entered DM_HIST via Favourites shortcut; CANCEL returns home
  char      _reply_prefix[36];   // "@[nick] " built when reply is triggered
  bool      _reply_mode;         // true while composing a reply (prefix is prepended)

  // Fullscreen-message context menu actions. Built per-message: Reply (when
  // applicable) plus Navigate / Save waypoint when the message carries a
  // location (a {loc} string or a [WAY] share). _fs_act maps each visible row
  // back to an action so the index math survives the conditional layout.
  enum FsAct : uint8_t { FS_REPLY, FS_NAV, FS_SAVE };
  uint8_t   _fs_act[3];
  int       _fs_act_n = 0;
  // Inline navigate-to-location view layered over the fullscreen message.
  bool      _nav_active = false;
  int32_t   _nav_lat = 0, _nav_lon = 0;
  char      _nav_label[24] = "";

  // Share-compose mode: launched from elsewhere (e.g. a waypoint) with a
  // prepared message; the user picks a recipient and the text lands prefilled
  // in the keyboard to confirm/edit before sending.
  bool      _share_mode = false;
  char      _share_text[160] = "";
  // Live-share target picker: reuse the recipient chooser to set the persistent
  // auto-share target (channel / DM) instead of composing a message.
  bool      _pick_target = false;
  // Bot channel picker: same idea, but jumps straight to CHANNEL_PICK (the
  // bot only ever targets a channel, never a DM) to set bot_channel_idx.
  bool      _pick_bot_channel = false;
  // Bot room picker: jumps straight to CONTACT_PICK with room_mode on (the
  // bot's room target is always a room server) to set bot_room_prefix.
  bool      _pick_bot_room = false;

  // The message-history rings (channel + DM), their per-entry delivery state and
  // per-channel unread counters live in this store (see MessageHistory.h). The
  // phase machine below keeps only the view state — selection, scroll, the
  // fullscreen readers — and reaches entries through _history's accessors. The
  // shared types (AckState, ChHistEntry, DmHistEntry, MSG_TEXT_BUF) are file-
  // scope, so they're still referred to unqualified throughout this screen.
  MessageHistory _history;

  // DM_HIST view state (the ring itself is in _history).
  int _dm_hist_sel, _dm_hist_scroll;
  FullscreenMsgView _dm_fs;

  int _hist_visible = 2;  // updated in render(); for history list scroll clamping

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

  // Scrollbar metrics for a history list. The track is pinned to the full list
  // area (top_y..cby) so it never resizes with content; only the thumb sizes and
  // moves. `need` also drives the gutter reserve so wide message boxes don't
  // reflow as messages arrive.
  struct HistScroll { bool need; int reserve; long total_px, scroll_px; int view_px; };

  // `getBody(idx)` returns the body text for list item idx (the part that wraps),
  // or nullptr to fall back to a fixed 2-line box. Must return exactly what the
  // real per-item render pass wraps (sender split off, reply prefix stripped) --
  // this function doesn't reprocess it, so a mismatch here just means this sizing
  // pass and the real render disagree on line count.
  template <class GetBody>
  HistScroll computeHistScroll(DisplayDriver& display, bool portrait, int count, int scroll,
                               int hist_start_y, int cby, int lh, GetBody getBody) {
    HistScroll r{};
    const int fixed_bh = 2 * lh + 1;
    const int top_y    = hist_start_y + 1;
    r.view_px = cby - top_y;                  // fixed track height = full list area
    if (r.view_px < 1) r.view_px = 1;
    const int col = scrollIndicatorColWidth(display);

    if (!portrait) {                          // uniform 2-line boxes → exact pixel math
      const int box = fixed_bh + 1;
      r.total_px  = (long)count * box;
      r.scroll_px = (long)scroll * box;
      r.need      = r.total_px > r.view_px;
      r.reserve   = r.need ? col : 0;
      if (!r.need) r.scroll_px = 0;
      return r;
    }

    const int sp = 2;                         // portrait inter-box spacing
    auto boxH = [&](int idx, int rsv) -> int {
      const char* body = getBody(idx);
      if (!body) return fixed_bh;
      display.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
      int nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, display.width() - 6 - rsv, s_wrap_lines, 8);
      return (1 + (nl > 0 ? nl : 1)) * lh + 1;
    };
    // Scrollbar-needed test at the WIDEST layout (reserve 0 → fewest wrap lines →
    // shortest total). If even this overflows the list area the gutter is truly
    // needed, so it stays put and can't flicker in/out as messages arrive.
    int cur = hist_start_y, fit = 0;
    for (int i = 0; i < count; i++) { int bh = boxH(i, 0); if (cur + bh > cby) break; fit++; cur += bh + sp; }
    r.need    = fit < count;
    r.reserve = r.need ? col : 0;
    if (r.need) {                             // sum real box heights at the final width
      for (int i = 0; i < count; i++) {
        long ext = boxH(i, r.reserve) + sp;
        r.total_px += ext;
        if (i < scroll) r.scroll_px += ext;
      }
    }
    return r;
  }

  // Strip the "@[nick] " reply prefix for compact list display (body only).
  // Shares the one parser with the fullscreen view — see msgReplyBody().
  static const char* skipReplyPrefix(const char* text) { return msgReplyBody(text); }

  // Split a DM-history entry into the name to show as the author and the body to
  // show beneath it. Room servers carry many guests, so incoming room posts are
  // stored "Sender: text" (MyMesh::queueMessage); split that off so each line is
  // attributed to its guest. Outgoing → "Me"; plain DMs (or no separator) keep
  // the contact name and the text unchanged.
  //
  // Does NOT strip a leading "@[nick] " reply prefix — that's the caller's call,
  // and it must be made exactly once: FullscreenMsgView::render() parses it
  // itself (for the "To:" header), so callers feeding it must pass this
  // function's result straight through; the compact list view has no such
  // parsing of its own, so those callers must wrap the result in
  // skipReplyPrefix(). Stripping it in here unconditionally used to double-strip
  // the DM case (hiding FullscreenMsgView's "To:" header entirely) while never
  // stripping the room case (the split happens after this used to run), which is
  // why replies' addressee went undetected inconsistently between DM/room/list/
  // fullscreen.
  const char* dmDisplayParts(const DmHistEntry& e, bool is_room, const char* contact_name,
                             char* sender_buf, int sender_cap) const {
    const char* body = e.text;
    if (e.outgoing) {
      strncpy(sender_buf, "Me", sender_cap - 1);
    } else if (is_room) {
      const char* sep = strstr(body, ": ");
      if (sep) {
        int nl = (int)(sep - body);
        if (nl > sender_cap - 1) nl = sender_cap - 1;
        strncpy(sender_buf, body, nl);
        sender_buf[nl] = '\0';
        return sep + 2;   // body after "Sender: "
      }
      strncpy(sender_buf, contact_name, sender_cap - 1);
    } else {
      strncpy(sender_buf, contact_name, sender_cap - 1);
    }
    sender_buf[sender_cap - 1] = '\0';
    return body;
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

  // Build "@[nick] " into _reply_prefix for a reply to a DM/room post, raw
  // (UTF-8) so it goes out over the air intact — like buildChannelReplyPrefix,
  // and unlike the old per-site code which ran the name through the lossy
  // display transliterator. Addresses the same author the history shows
  // (dmDisplayParts): in a room every post is stored "Author: text", so the
  // addressee is that author, not the room server's own name (_sel_contact.name);
  // a plain 1:1 DM uses the contact name. Caller ensures the post is incoming.
  void buildDmReplyPrefix(const DmHistEntry& e) {
    char nick[32];
    dmDisplayParts(e, _sel_contact.type == ADV_TYPE_ROOM, _sel_contact.name, nick, sizeof(nick));
    snprintf(_reply_prefix, sizeof(_reply_prefix), "@[%.31s] ", nick);
  }

  void startReply(bool to_channel) {
    _sending_to_channel = to_channel;
    _reply_mode = true;
    setupMsgPick();
    _phase = MSG_PICK;
  }

  // Recipient chosen while sharing — open the keyboard with the prepared text.
  void beginShareCompose(bool channel) {
    _sending_to_channel = channel;
    _reply_mode = false;
    _kb->begin(_share_text);
    _phase = KEYBOARD;
  }

  // Build the fullscreen-message options popup: Reply (if allowed) plus
  // Navigate / Save waypoint when `body` carries a location. Opens _ctx_menu
  // only when there's at least one action. Parses the location once here and
  // stashes it for the action handler.
  void buildFsMenu(const char* body, bool reply_allowed) {
    bool has_loc = geo::parseLatLon(body, _nav_lat, _nav_lon, _nav_label, sizeof(_nav_label));
    int n = (reply_allowed ? 1 : 0) + (has_loc ? 2 : 0);
    if (n == 0) return;
    _fs_act_n = 0;
    _ctx_menu.begin("Options", n);
    if (reply_allowed) { _ctx_menu.addItem("Reply");         _fs_act[_fs_act_n++] = FS_REPLY; }
    if (has_loc)       { _ctx_menu.addItem("Navigate");      _fs_act[_fs_act_n++] = FS_NAV;
                         _ctx_menu.addItem("Save waypoint"); _fs_act[_fs_act_n++] = FS_SAVE; }
  }

  // Dispatch the selected fullscreen-options row. `channel` picks which
  // fullscreen view to close when starting a reply.
  void dispatchFsAction(bool channel) {
    int csel = _ctx_menu.selectedIndex();
    FsAct a = (FsAct)_fs_act[(csel >= 0 && csel < _fs_act_n) ? csel : 0];
    _ctx_menu.active = false;
    if (a == FS_REPLY) {
      (channel ? _fs : _dm_fs).active = false;
      startReply(channel);
    } else if (a == FS_NAV) {
      _nav_active = true;            // keep the message view active underneath
    } else {
      saveSharedWaypoint();
    }
  }

  // Save the location parsed from the open message as a waypoint. Uses the
  // [WAY] label when present, else auto-names it like a manually-marked point.
  void saveSharedWaypoint() {
    char label[WAYPOINT_LABEL_LEN];
    if (_nav_label[0]) {
      strncpy(label, _nav_label, sizeof(label) - 1);
      label[sizeof(label) - 1] = '\0';
    } else {
      snprintf(label, sizeof(label), "WP%d", _task->waypoints().count() + 1);
    }
    _task->addWaypoint(_nav_lat, _nav_lon, label);
  }

  void renderNav(DisplayDriver& display) {
    int32_t mylat, mylon; bool have = _task->currentLocation(mylat, mylon);
    int cog; bool cogv = _task->currentCourse(cog);
    NodePrefs* p = _task->getNodePrefs();
    navview::draw(display, have, mylat, mylon, _nav_lat, _nav_lon,
                  _nav_label[0] ? _nav_label : "Msg loc", cogv, cog, p && p->units_imperial);
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

  // Delivery marker, drawn with the current ink colour and auto-scaled to the
  // font (see icons.h). Pending = a row of dots, one per send (so it grows with
  // each auto-resend); delivered = ✓; failed = ✗; ACK_NONE = nothing.
  static void drawAckGlyph(DisplayDriver& d, int x, int top_y, AckState s, int sends = 1) {
    switch (s) {
      case ACK_PENDING: miniIconDotRow(d, x, top_y, sends);          break;
      case ACK_OK:      miniIconDraw(d, x, top_y, ICON_CHECK);       break;
      case ACK_FAIL:    miniIconDraw(d, x, top_y, ICON_CROSS);       break;
      default: break;                          // ACK_NONE → nothing
    }
  }

  // Selection frame for one history message box, shared by the DM/room and
  // channel history lists. Selected = solid fill; unselected = outline with a
  // filled header strip. Leaves the ink DARK (for the sender row drawn next).
  // Spans exactly [box_x, box_x+box_w) — the caller sizes/positions the bubble
  // (see computeBubbleBox below), this just draws whatever box it's given.
  static void drawHistRowFrame(DisplayDriver& d, int box_x, int box_w, int y, int bh, int lh, bool sel) {
    d.setColor(DisplayDriver::LIGHT);
    if (sel) {
      d.fillRect(box_x, y, box_w, bh);
    } else {
      d.drawRect(box_x, y, box_w, bh);
      d.fillRect(box_x + 1, y + 1, box_w - 2, lh);
    }
    d.setColor(DisplayDriver::DARK);
  }

  // Width of an ack/delivery glyph (see drawAckGlyph) — needed up front to size
  // an outgoing bubble before it's drawn.
  static int ackGlyphWidth(DisplayDriver& d, AckState s, int sends) {
    const int sc = miniIconScale(d);
    switch (s) {
      case ACK_PENDING: return sends * 3 * sc;   // dot+gap pitch (icons.h), slightly generous
      case ACK_OK:      return ICON_CHECK.w * sc;
      case ACK_FAIL:    return ICON_CROSS.w * sc;
      default:          return 0;
    }
  }

  // A chat bubble's horizontal extent: shrunk to fit its own content (the
  // sender+ack+age header line vs the wrapped/measured body, whichever is
  // wider), capped at the full available width, and anchored to the right for
  // outgoing ("Me") messages or the left for incoming ones — so which side a
  // bubble sits on shows who sent it, the same convention as a typical
  // messenger. Everything else in the row (sender, age, ack glyph, body) is
  // then drawn relative to this box's own x instead of the screen edge.
  struct BubbleBox { int x, w; };
  static BubbleBox computeBubbleBox(int full_avail, bool outgoing, int header_w, int body_w) {
    int w = header_w > body_w ? header_w : body_w;
    if (w > full_avail) w = full_avail;
    if (w < 1) w = 1;
    return { outgoing ? (full_avail - w) : 0, w };
  }

  // Bottom-right "[+ send]" compose button, shared by both history lists:
  // bordered when idle, inverted (filled) when selected. Right-aligned to
  // match the outgoing ("Me") bubbles anchored on that side, so composing
  // sits under your own messages rather than the other side's. Box is exactly
  // lh tall (no added border padding) — the text has no descenders ("[+
  // send]"), so padding sized against the font's full ascent+descent cell
  // read as a lopsided 1px-top/3px-bottom gap instead of an even border;
  // tight framing avoids that and gives the history list back the 2px. Always
  // reset to LIGHT first so it stays visible regardless of the ink the
  // message loop left.
  static void drawComposeButton(DisplayDriver& d, int cby, int lh, bool sel) {
    const char* ctxt = "[+ send]";
    int ctw = d.getTextWidth(ctxt);
    int bw = ctw + 4;
    int bx = d.width() - bw;
    d.setColor(DisplayDriver::LIGHT);
    if (sel) { d.fillRect(bx, cby, bw, lh); d.setColor(DisplayDriver::DARK); }
    else       d.drawRect(bx, cby, bw, lh);
    d.setCursor(bx + 2, cby);
    d.print(ctxt);
    d.setColor(DisplayDriver::LIGHT);
  }

  void afterSend(bool ok, const char* msg) {
    _reply_mode = false;
    _share_mode = false;
    if (ok && _sending_to_channel) {
      _hist_sel = 0;
      _hist_scroll = 0;
      _phase = CHANNEL_HIST;  // set before addChannelMsg so viewing=true, no unread bump
      char entry[sizeof(ChHistEntry::text)];
      snprintf(entry, sizeof(entry), "Me: %s", msg);
      int pos = addChannelMsg(_sel_channel_idx, entry);
      // Arm the "relayed into mesh" marker on this exact entry — MyMesh tracked
      // the flood it just originated and reports a heard repeater echo by seq.
      if (pos >= 0) _history.armChannelRelay(pos, the_mesh.lastChannelRelaySeq());
      // After inserting sent msg at index 0, the unread index range is stale.
      // User is active in this channel — treat as fully read.
      _history.setChUnread(_sel_channel_idx, 0);
      _unread_at_entry = 0;
      _viewing_max_seen = 0;
      _task->showAlert("Sent!", 600);
    } else if (ok) {
      NodePrefs* np = _task->getNodePrefs();
      uint8_t resends = np ? np->dm_resend_count : 0;
      _history.storeDMMsg(_sel_contact.id.pub_key, true, msg, _last_ack_tag,
                          _last_ack_deadline_ms, _last_send_ts, resends);
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
    _last_ack_tag = 0;
    _last_ack_deadline_ms = 0;
    _last_send_ts = 0;
    if (_sending_to_channel) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(_sel_channel_idx, ch)) return false;
      return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                       the_mesh.getNodeName(), msg, strlen(msg));
    } else {
      uint32_t send_ts = rtc_clock.getCurrentTime();
      uint32_t expected_ack = 0, est_timeout = 0;
      bool ok = the_mesh.sendMessage(_sel_contact, send_ts, 0,
                                     msg, expected_ack, est_timeout) > 0;
      if (ok && expected_ack) {
        _last_send_ts = send_ts;
        _last_ack_tag = expected_ack;
        // Generous margin over the base estimate so a slow multi-hop ACK isn't
        // prematurely shown as failed.
        _last_ack_deadline_ms = millis() + est_timeout + 4000;
      }
      return ok;
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
      // Build _sorted and counts[] in one pass — avoids a second getContactByIdx loop.
      // uint8_t, not int: values are bounded by MessageHistory::DM_HIST_MAX (32), and
      // this array is 1400 B at this build's MAX_CONTACTS=350 as an int[] -- a sizeable
      // slice of the 4 KB loop() task stack for one local array.
      uint8_t counts[MAX_CONTACTS];
      for (int i = 0; i < total; i++) {
        if (!the_mesh.getContactByIdx(i, c) || c.type != ADV_TYPE_CHAT) continue;
        if (!show_all && !(c.flags & 0x01)) continue;
        counts[_num_contacts] = _history.dmHistCountForContact(c.id.pub_key);
        _sorted[_num_contacts++] = i;
      }
      // Sort by message count descending; contacts with no messages keep original order.
      for (int i = 1; i < _num_contacts; i++) {
        if (counts[i] == 0) continue;
        uint16_t key = _sorted[i]; int kc = counts[i];
        int j = i;
        while (j > 0 && counts[j-1] < kc) {
          _sorted[j] = _sorted[j-1]; counts[j] = counts[j-1]; j--;
        }
        _sorted[j] = key; counts[j] = kc;
      }
    }
  }

  void buildChannelList() {
    NodePrefs* p = _task->getNodePrefs();
    bool fav_only = (p && p->ch_fav_only);
    _num_channels = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      if (fav_only && !(p->ch_fav_bitmask & (1ULL << i))) continue;
      _channel_indices[_num_channels++] = (uint8_t)i;
    }
  }

  // Returns per-channel notification state: 0=follow global, 1=muted, 2=force-on
  // Per-contact/channel notification + melody overrides share two storage
  // shapes, so the eight accessors below are thin wrappers over two primitives:
  //
  //  • Channels — a 3-state packed into a pair of channel-index bitmasks: a
  //    "presence" mask (is there an override at all) + a "variant" mask. The two
  //    uses disagree on which state the variant bit means, so the caller passes
  //    the state value that corresponds to variant-set (v_set).
  //  • DMs — a small {prefix[4], value} table: find by 4-byte prefix, update or
  //    clear (clear frees the slot), else insert into the first free slot, else
  //    overwrite slot 0. value 0 == "no override" == empty slot.

  static uint8_t maskPairGet(uint64_t presence, uint64_t variant, uint8_t idx,
                             uint8_t v_set, uint8_t v_clr) {
    uint64_t m = 1ULL << idx;
    if (!(presence & m)) return 0;
    return (variant & m) ? v_set : v_clr;
  }
  static void maskPairSet(uint64_t& presence, uint64_t& variant, uint8_t idx,
                          uint8_t state, uint8_t v_set) {
    uint64_t m = 1ULL << idx;
    if (state == 0) { presence &= ~m; variant &= ~m; return; }
    presence |= m;
    if (state == v_set) variant |= m; else variant &= ~m;
  }

  template <class Entry>
  static uint8_t prefTableGet(const Entry* tbl, int n, const uint8_t* pub_key,
                              uint8_t Entry::* val) {
    for (int i = 0; i < n; i++)
      if (tbl[i].*val && memcmp(tbl[i].prefix, pub_key, 4) == 0) return tbl[i].*val;
    return 0;
  }
  template <class Entry>
  static void prefTableSet(Entry* tbl, int n, const uint8_t* pub_key,
                           uint8_t Entry::* val, uint8_t v) {
    for (int i = 0; i < n; i++)
      if (tbl[i].*val && memcmp(tbl[i].prefix, pub_key, 4) == 0) {
        if (v == 0) memset(&tbl[i], 0, sizeof(tbl[i])); else tbl[i].*val = v;
        return;
      }
    if (v == 0) return;
    for (int i = 0; i < n; i++)
      if (tbl[i].*val == 0) { memcpy(tbl[i].prefix, pub_key, 4); tbl[i].*val = v; return; }
    memcpy(tbl[0].prefix, pub_key, 4); tbl[0].*val = v;   // table full — overwrite slot 0
  }

  // Channel notif: state 1 = muted (variant bit set), 2 = force-on (variant clear).
  uint8_t chNotifState(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    return p ? maskPairGet(p->ch_notif_override, p->ch_notif_muted, ch_idx, 1, 2) : 0;
  }
  void setChNotifState(uint8_t ch_idx, uint8_t state) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) maskPairSet(p->ch_notif_override, p->ch_notif_muted, ch_idx, state, 1);
  }

  uint8_t dmNotifState(const uint8_t* pub_key) const {
    NodePrefs* p = _task->getNodePrefs();
    return p ? prefTableGet(p->dm_notif, NodePrefs::DM_NOTIF_TABLE_MAX, pub_key,
                            &NodePrefs::DmNotifEntry::state) : 0;
  }
  void setDmNotifState(const uint8_t* pub_key, uint8_t state) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) prefTableSet(p->dm_notif, NodePrefs::DM_NOTIF_TABLE_MAX, pub_key,
                        &NodePrefs::DmNotifEntry::state, state);
  }

  // Channel melody: slot 1 = melody 1 (variant bit clear), 2 = melody 2 (set).
  uint8_t chNotifMelody(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    return p ? maskPairGet(p->ch_notif_melody_set, p->ch_notif_melody_2, ch_idx, 2, 1) : 0;
  }
  void setChNotifMelody(uint8_t ch_idx, uint8_t slot) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) maskPairSet(p->ch_notif_melody_set, p->ch_notif_melody_2, ch_idx, slot, 2);
  }

  uint8_t dmMelodySlot(const uint8_t* pub_key) const {
    NodePrefs* p = _task->getNodePrefs();
    return p ? prefTableGet(p->dm_melody, NodePrefs::DM_MELODY_TABLE_MAX, pub_key,
                            &NodePrefs::DmMelodyEntry::slot) : 0;
  }
  void setDmMelody(const uint8_t* pub_key, uint8_t slot) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) prefTableSet(p->dm_melody, NodePrefs::DM_MELODY_TABLE_MAX, pub_key,
                        &NodePrefs::DmMelodyEntry::slot, slot);
  }

  // On-device channel Add/Edit form (Channels tab) — owned by this screen and
  // delegated to while active(), the same relationship WaypointsView has with
  // TrailScreen.
  ChannelsView _ch_view;

public:
  MessagesScreen(UITask* task, KeyboardWidget* kb)
    : _task(task), _kb(kb), _phase(MODE_SELECT), _mode_sel(0),
      _contact_sel(0), _contact_scroll(0), _num_contacts(0), _room_mode(false), _login_mode(false),
      _channel_sel(0), _channel_scroll(0), _num_channels(0),
      _sel_channel_idx(0), _sending_to_channel(false),
      _msg_sel(0), _msg_scroll(0), _active_msg_count(0),
      _hist_sel(0), _hist_scroll(0),
      _unread_at_entry(0), _viewing_max_seen(0),
      _dm_hist_sel(-1), _dm_hist_scroll(0),
      _ctx_dirty(false), _pin_picker_active(false), _dm_direct_entry(false), _reply_mode(false),
      _ch_view(task) {
    // The history rings + per-channel unread counters init in MessageHistory.
  }

  // Forwarded to UITask's GPS duty-cycle hold — true while showing the
  // bearing/distance view to a location shared in a message.
  bool navActive() const { return _nav_active; }

  // First free channel slot (existing config or blank name), or -1 if full.
  int findFreeChannelSlot() const {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') return i;
    }
    return -1;
  }

  // CHANNEL_PICK row count including the synthetic "+ Add channel" row
  // (suppressed while picking a channel for the bot).
  int channelPickTotal() const { return _num_channels + (_pick_bot_channel ? 0 : 1); }

  // Public entry points (routed from MyMesh / the bot via UITask) — thin
  // forwarders to the history store. addChannelMsg computes the "viewing" flag
  // (a phase-machine fact the store can't see) and returns the ring position so
  // the outgoing path can attach a relay seq to that exact entry.
  int addChannelMsg(uint8_t ch_idx, const char* text, uint32_t timestamp = 0) {
    bool viewing = (_phase == CHANNEL_HIST && _sel_channel_idx == (int)ch_idx);
    int pos = _history.addChannelMsg(ch_idx, text, viewing, timestamp);
    // Ring entries are numbered newest-first (0 == newest), so a new insert
    // shifts every older message's index up by one. If the user has scrolled
    // up to an older message (_hist_sel > 0), re-point the selection at that
    // same message instead of silently relabeling a different one in under
    // them. At _hist_sel <= 0 (already at newest, or -1 == compose button
    // focused) there's nothing to preserve.
    if (viewing && _hist_sel > 0) { _hist_sel++; _hist_scroll++; }
    return pos;
  }
  void markChannelRelayed(uint32_t seq) { _history.markChannelRelayed(seq); }
  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text,
                uint32_t sender_timestamp = 0) {
    bool viewing = (_phase == DM_HIST && memcmp(_sel_contact.id.pub_key, pub_key, 4) == 0);
    _history.addDMMsg(pub_key, outgoing, text, sender_timestamp);
    if (viewing && _dm_hist_sel > 0) { _dm_hist_sel++; _dm_hist_scroll++; }   // see addChannelMsg
  }
  void markDmDelivered(uint32_t ack_crc) { _history.markDmDelivered(ack_crc); }

  // Rooms successfully logged in to this power-on session. RAM-only — the
  // server's ACL (see ClientACL) is the real permission store and survives
  // reboot on its own, but the device has no way to query it, so this is just
  // a local memo to skip re-prompting for a password already entered this
  // session when the same room is picked again.
  static const int ROOM_LOGIN_TABLE_SIZE = 8;
  uint8_t _room_login_prefix[ROOM_LOGIN_TABLE_SIZE][4];
  int _room_login_head = 0, _room_login_count = 0;

  bool isRoomLoggedIn(const uint8_t* pub_key) const {
    // Indices are relative to _room_login_head, same as forgetRoomLoggedIn() --
    // direct 0.._room_login_count indexing only happens to work before the
    // ring has wrapped once (head==0); after that it silently checks the wrong
    // slots.
    for (int i = 0; i < _room_login_count; i++) {
      int pos = (_room_login_head + i) % ROOM_LOGIN_TABLE_SIZE;
      if (memcmp(_room_login_prefix[pos], pub_key, 4) == 0) return true;
    }
    return false;
  }

  void markRoomLoggedIn(const uint8_t* pub_key) {
    if (isRoomLoggedIn(pub_key)) return;
    int pos;
    if (_room_login_count < ROOM_LOGIN_TABLE_SIZE) {
      pos = (_room_login_head + _room_login_count) % ROOM_LOGIN_TABLE_SIZE;
      _room_login_count++;
    } else {
      pos = _room_login_head;
      _room_login_head = (_room_login_head + 1) % ROOM_LOGIN_TABLE_SIZE;
    }
    memcpy(_room_login_prefix[pos], pub_key, 4);
  }

  // Reverses markRoomLoggedIn() on explicit Logout -- shifts the ring buffer
  // closed over the removed slot so isRoomLoggedIn() goes back to false and
  // the next room open prompts for a password instead of skipping it.
  void forgetRoomLoggedIn(const uint8_t* pub_key) {
    for (int i = 0; i < _room_login_count; i++) {
      int pos = (_room_login_head + i) % ROOM_LOGIN_TABLE_SIZE;
      if (memcmp(_room_login_prefix[pos], pub_key, 4) == 0) {
        for (int j = i; j < _room_login_count - 1; j++) {
          int from = (_room_login_head + j + 1) % ROOM_LOGIN_TABLE_SIZE;
          int to   = (_room_login_head + j) % ROOM_LOGIN_TABLE_SIZE;
          memcpy(_room_login_prefix[to], _room_login_prefix[from], 4);
        }
        _room_login_count--;
        return;
      }
    }
  }

  // Password of the room-login attempt currently in flight -- set right
  // before sendRoomLogin(), read back in onRoomLoginResult() so a successful
  // attempt can be persisted (see MyMesh::saveRoomPassword()).
  char _login_pw[16];

  void startRoomLogin(const char* password) {
    strncpy(_login_pw, password, sizeof(_login_pw) - 1);
    _login_pw[sizeof(_login_pw) - 1] = 0;
    uint32_t est_timeout = 0;   // this screen's login isn't a blocking wait (see
                                // onRoomLoginResult() below), so no deadline needed
    bool sent = the_mesh.sendRoomLogin(_sel_contact, password, est_timeout);
    _task->showAlert(sent ? "Logging in..." : "Login failed", sent ? 1000 : 1500);
  }

  // Result of an on-device sendRoomLogin() (MyMesh::onContactResponse(), routed
  // via AbstractUITask::onRoomLoginResult()). Surfaces as a transient alert.
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
    (void)permissions;
    if (success) {
      markRoomLoggedIn(pub_key);
      the_mesh.saveRoomPassword(pub_key, _login_pw);
      // Auto-enter the room's chat right after a successful login so the user
      // doesn't have to press Enter a second time. The login result is async,
      // so only do it if they're still sitting on this same room in the picker
      // (didn't navigate away, open a menu, or enter a share/pick sub-flow).
      if (_phase == CONTACT_PICK && _room_mode && !_ctx_menu.active
          && !_share_mode && !_pick_target && !_pick_bot_room
          && memcmp(_sel_contact.id.pub_key, pub_key, 4) == 0) {
        openDmHistory();
      }
    } else {
      // Saved password (if any) no longer works -- forget it so the next
      // ENTER on this room falls back to a manual prompt instead of
      // silently retrying the same bad password forever.
      the_mesh.forgetRoomPassword(pub_key);
    }
    _task->showAlert(success ? "Login OK" : "Login failed", 1200);
  }

  // Open the message history for the currently selected contact/room and reset
  // the scroll/selection view state. Shared by the Enter-on-contact path and the
  // post-login auto-enter.
  void openDmHistory() {
    _task->clearDMUnread(_sel_contact.id.pub_key);
    _dm_hist_sel = -1;
    _dm_hist_scroll = 0;
    _dm_fs.active = false;
    _phase = DM_HIST;
  }

  // Background tick (called every UI loop, regardless of the active screen) that
  // drives auto-resend of on-device DMs — forwarded to the history store.
  void tickDmResends() { _history.tickDmResends(); }

  int getDMUnreadTotal() const {
    return _task->getDMUnreadTotal();
  }

  int getTotalChannelUnread() const { return _history.getTotalChannelUnread(); }

  // How many DM ring entries this contact/room currently holds -- lets UITask
  // clamp its separate _dm_unread_table counters to what the shared 32-slot DM
  // ring actually still has, the same self-healing shape as the channel fix.
  int dmHistCountForContact(const uint8_t* pub_key) const { return _history.dmHistCountForContact(pub_key); }

  void markReadAlert(int n) {
    char msg[32];
    snprintf(msg, sizeof(msg), "%d marked read", n);
    _task->showAlert(msg, 800);
  }

  void clearAllChannelUnread() { _history.clearAllChannelUnread(); }

  // Update the viewing-session unread bookkeeping (UI state) and push the
  // resulting count down into the store. The ring lives in _history, but this
  // "what has the user seen on screen" logic is pure phase-machine state.
  void updateChannelUnread() {
    if (_sel_channel_idx < 0 || _sel_channel_idx >= MAX_GROUP_CHANNELS) return;
    // histEntryForChannel is newest-first: index 0 = newest (unread), higher = older.
    // Count everything actually rendered on screen as seen — not just the
    // highlighted row — so a taller screen that fits more boxes at once marks
    // more read up front, instead of requiring a press per row.
    int seen_to = _hist_scroll + _hist_visible - 1;
    if (_hist_sel > seen_to) seen_to = _hist_sel;
    if (seen_to > _viewing_max_seen) _viewing_max_seen = seen_to;
    // Each step down from 0 sees one more message; seen count = max_seen + 1.
    int remaining = _unread_at_entry - (_viewing_max_seen + 1);
    _history.setChUnread(_sel_channel_idx, (uint8_t)(remaining > 0 ? remaining : 0));
  }

  void reset() {
    _phase = MODE_SELECT;
    _mode_sel = 0;
    _contact_sel = _contact_scroll = 0;
    _msg_sel = _msg_scroll = 0;
    _channel_sel = _channel_scroll = 0;
    _sending_to_channel = false;

    _room_mode = false;
    _login_mode = false;
    buildContactList();
    buildChannelList();

    _ctx_menu.active = false;
    _ctx_dirty = false;
    _nav_active = false;
    _share_mode = false;
    _pick_target = false;
    _pick_bot_channel = false;
    _pick_bot_room = false;
    _pin_picker_active = false;
    _dm_direct_entry = false;
    _unread_at_entry = 0;
    _viewing_max_seen = 0;
    _ch_view.reset();
  }

  // Recent DM contacts, newest first, deduped (forwarded to the history store).
  int getRecentDMContacts(uint8_t out[][NodePrefs::FAVOURITE_PREFIX_LEN], int max) const {
    return _history.getRecentDMContacts(out, max);
  }

  // Jump straight into a contact's DM history (used by the Favourites dial).
  // Caller must have already reset() the screen. Marks the entry so KEY_CANCEL
  // from DM_HIST returns to the home screen instead of falling back through
  // CONTACT_PICK → MODE_SELECT.
  // Enter the screen pre-loaded to share `text` (e.g. a "[WAY]lat,lon label"
  // waypoint). The user picks Direct/Channel then a recipient; selecting one
  // jumps straight to the keyboard prefilled with the text (see beginShareCompose).
  void startShare(const char* text) {
    reset();
    _share_mode = true;
    strncpy(_share_text, text, sizeof(_share_text) - 1);
    _share_text[sizeof(_share_text) - 1] = '\0';
    _phase = MODE_SELECT;
  }

  // Open the recipient chooser to set the live-share target (channel/DM). On
  // selection the target is stored in NodePrefs and the Map screen is restored.
  void startPickTarget() {
    reset();
    _pick_target = true;
    _phase = MODE_SELECT;
  }

  void commitPickTargetChannel(int ch_idx) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      p->loc_share_target_type = 0;
      p->loc_share_channel_idx = (uint8_t)ch_idx;
      the_mesh.savePrefs();
    }
    _pick_target = false;
    _task->showAlert("Share target set", 1200);
    _task->gotoLiveShareScreen();
  }

  // Open the channel chooser to set the auto-reply bot's channel. Skips
  // MODE_SELECT (the bot only ever targets a channel) and lands straight on
  // CHANNEL_PICK, browsing real channel names instead of a bare index cycle.
  void startPickBotChannel() {
    reset();
    _pick_bot_channel = true;
    _phase = CHANNEL_PICK;
  }

  void commitPickBotChannel(int ch_idx) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      p->bot_channel_enabled = 1;
      p->bot_channel_idx = (uint8_t)ch_idx;
      the_mesh.savePrefs();
    }
    _pick_bot_channel = false;
    _task->showAlert("Bot channel set", 1200);
    _task->gotoBotScreen();
  }

  // Open the room chooser to set the auto-reply bot's room. Enter routes
  // through the same login handling the normal room-open flow uses (see the
  // CONTACT_PICK/room_mode Enter handler below) — the bot can't ever post to
  // a room it has no password for, so picking one is a good moment to prompt.
  void startPickBotRoom() {
    reset();
    _pick_bot_room = true;
    _room_mode = true;
    buildContactList();   // rebuild now that _room_mode is on (reset() built the DM list)
    _contact_sel = _contact_scroll = 0;
    _phase = CONTACT_PICK;
  }

  void commitPickBotRoom(const ContactInfo& ci) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      p->bot_room_enabled = 1;
      memcpy(p->bot_room_prefix, ci.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
      the_mesh.savePrefs();
    }
    _pick_bot_room = false;
    _room_mode = false;
    _task->showAlert("Bot room set", 1200);
    _task->gotoBotScreen();
  }

  void commitPickTargetDM(const ContactInfo& ci) {
    // A room server can't be a live-share target — a [LOC] DM to it is never
    // reposted to the room's members. Reject the pick and keep the chooser open.
    if (ci.type == ADV_TYPE_ROOM) {
      _task->showAlert("Rooms not supported", 1400);
      return;
    }
    NodePrefs* p = _task->getNodePrefs();
    if (p) {
      p->loc_share_target_type = 1;
      memcpy(p->loc_share_dm_prefix, ci.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);
      the_mesh.savePrefs();
    }
    _pick_target = false;
    _task->showAlert("Share target set", 1200);
    _task->gotoLiveShareScreen();
  }

  void enterDM(const ContactInfo& ci) {
    _sel_contact = ci;
    _task->clearDMUnread(ci.id.pub_key);
    _dm_hist_sel = -1;
    _dm_hist_scroll = 0;
    _dm_fs.active = false;
    _room_mode = false;
    _phase = DM_HIST;
    _dm_direct_entry = true;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    // Channel Add/Edit form owns the screen while active.
    if (_ch_view.active()) return _ch_view.render(display);

    // Navigate-to-location view sits over everything else while active.
    if (_nav_active) { renderNav(display); return 1000; }

    int lh      = display.getLineHeight();
    int item_h  = display.lineStep();
    int start_y = display.listStart();

    if (_phase == MODE_SELECT) {
      display.drawCenteredHeader("MESSAGE", true, _ctx_menu.active);
      const char* opts[] = { "Direct message", "Channels", "Room Servers" };
      int badges[3] = {
        getDMUnreadTotal(),
        _task->getChannelUnreadCount(),
        _task->getRoomUnreadCount()
      };
      for (int i = 0; i < 3; i++) {
        int y = start_y + i * item_h;
        bool sel = (i == _mode_sel);
        display.drawSelectionRow(0, y - 1, display.width(), item_h - 1, sel);
        display.setCursor(2, y);
        display.print(opts[i]);
        if (badges[i] > 0)
          display.drawUnreadBadge(display.width() - 1, y, badges[i], sel);
      }
      display.setColor(DisplayDriver::LIGHT);
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == CONTACT_PICK) {
      display.drawCenteredHeader(_room_mode ? "SELECT ROOM" : "SELECT CONTACT", true, _ctx_menu.active);

      if (_num_contacts == 0) {
        display.drawTextCentered(display.width()/2, display.height()/2, _room_mode ? "No room servers" : "No favourites");
        return 5000;
      }

      drawList(display, _num_contacts, _contact_sel, _contact_scroll, [&](int list_idx, int y, bool sel, int reserve) {
        int mesh_idx = _sorted[list_idx];

        drawRowSelection(display, y, sel, reserve);

        ContactInfo c;
        if (the_mesh.getContactByIdx(mesh_idx, c)) {
          char filtered[sizeof(c.name)];
          display.translateUTF8ToBlocks(filtered, c.name, sizeof(filtered));
          uint8_t dm_unread = _task->getDMUnread(c.id.pub_key);
          int bw = dm_unread > 0 ? display.unreadBadgeWidth(dm_unread) + 2 : 0;
          display.drawTextEllipsized(2, y, display.width() - 2 - bw - reserve, filtered);
          if (dm_unread > 0)
            display.drawUnreadBadge(display.width() - reserve, y, dm_unread, sel);
        }
      });

      // Context menu overlay
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == CHANNEL_PICK) {
      display.drawCenteredHeader("SELECT CHANNEL", true, _ctx_menu.active);

      // "+ Add channel" is a synthetic trailing row — suppressed while picking
      // a channel for the bot, so that picker's list stays unchanged.
      bool show_add = !_pick_bot_channel;
      int total = _num_channels + (show_add ? 1 : 0);

      if (total == 0) {
        display.drawTextCentered(display.width()/2, display.height()/2, "No channels");
        return 5000;
      }

      drawList(display, total, _channel_sel, _channel_scroll, [&](int list_idx, int y, bool sel, int reserve) {
        drawRowSelection(display, y, sel, reserve);
        if (list_idx == _num_channels) {                 // the synthetic "Add" row
          display.setCursor(2, y); display.print("+ Add channel");
          display.setColor(DisplayDriver::LIGHT);
          return;
        }
        ChannelDetails ch;
        if (the_mesh.getChannel(_channel_indices[list_idx], ch)) {
          uint8_t unread = _history.chUnread(_channel_indices[list_idx]);
          int bw = unread > 0 ? display.unreadBadgeWidth(unread) + 2 : 0;
          display.drawTextEllipsized(2, y, display.width() - 4 - bw - reserve, ch.name);
          if (unread > 0)
            display.drawUnreadBadge(display.width() - reserve, y, unread, sel);
        }
      });

      // Context menu overlay
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == DM_HIST) {
      display.setTextSize(1);
      char filtered_name[sizeof(_sel_contact.name)];
      display.translateUTF8ToBlocks(filtered_name, _sel_contact.name, sizeof(filtered_name));

      if (_dm_fs.active && _dm_hist_sel >= 0) {
        int ring_pos = _history.dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
        if (ring_pos >= 0) {
          const DmHistEntry& e = _history.dmAtPos(ring_pos);
          char sender_buf[33];
          // No skipReplyPrefix() here -- _dm_fs.render() parses "@[nick] " itself
          // (for the "To:" header); stripping it here first would hide it there.
          const char* body = dmDisplayParts(e, _sel_contact.type == ADV_TYPE_ROOM,
                                            filtered_name, sender_buf, sizeof(sender_buf));
          const char* sender = sender_buf;
          int dm_count = _history.dmHistCountForContact(_sel_contact.id.pub_key);
          int ret = _dm_fs.render(display, sender, body,
                                  _dm_hist_sel < dm_count - 1,
                                  _dm_hist_sel > 0);
          if (e.outgoing) {  // delivery marker in the (inverted) header bar
            display.setColor(DisplayDriver::DARK);
            drawAckGlyph(display, 2 + display.getTextWidth(sender) + 3, 1,
                         _history.dmEffectiveStatus(e), e.attempt + 1);
            display.setColor(DisplayDriver::LIGHT);
          }
          if (_ctx_menu.active) _ctx_menu.render(display);
          return ret;
        }
        return 500;
      }

      int hist_start_y = display.headerH();
      int cby = display.height() - lh;   // compose button is now exactly lh tall, no added border padding

      char title[24];
      snprintf(title, sizeof(title), "%.23s", filtered_name);
      display.drawCenteredHeader(title, true, _ctx_menu.active);

      int dm_count = _history.dmHistCountForContact(_sel_contact.id.pub_key);
      uint32_t now_ts = rtc_clock.getCurrentTime();
      bool is_room = (_sel_contact.type == ADV_TYPE_ROOM);

      // Portrait e-ink (height > width): variable-height boxes that show the full
      // wrapped message text. All other displays/orientations: compact 2-line boxes.
      bool portrait_expand = (display.height() > display.width());
      const int MAX_VIS_BOXES = 8;
      int box_ys[MAX_VIS_BOXES], box_hs[MAX_VIS_BOXES], n_vis = 0;
      // Fixed-track scrollbar metrics + a stable gutter reserve (decided by a
      // whole-list fit test, not last frame's visible count) so message boxes
      // don't reflow their width as messages arrive.
      HistScroll hs = computeHistScroll(display, portrait_expand, dm_count, _dm_hist_scroll,
          hist_start_y, cby, lh,
          [&](int idx) -> const char* {
            int rp = _history.dmHistEntryForContact(_sel_contact.id.pub_key, idx);
            if (rp < 0) return nullptr;
            // Must match the per-item body extraction below (dmDisplayParts +
            // skipReplyPrefix) exactly, or this sizing pass and the real render
            // pass disagree on wrapped line count for room posts -- previously
            // this returned the raw "Sender: text" unsplit, wrapping the sender
            // name in with the body and mis-sizing the box.
            char tmp_sender[33];
            return skipReplyPrefix(dmDisplayParts(_history.dmAtPos(rp), is_room, filtered_name,
                                                    tmp_sender, sizeof(tmp_sender)));
          });
      int reserve = hs.reserve;
      {
        // Stack boxes upward from just above the compose row, so item 0 (the
        // newest message) lands at the bottom of the list and older messages
        // sit progressively higher — same anchor convention as a typical
        // messenger, instead of newest-at-top. box_ys[i] still corresponds
        // to item (_dm_hist_scroll + i), same as before; only its y flips.
        const int fixed_bh = 2 * lh + 1;
        const int box_gap = portrait_expand ? 2 : 1;
        int cur_y = cby - box_gap;   // reserve the same gap against compose as between boxes
        for (int ii = 0; ii < MAX_VIS_BOXES && (_dm_hist_scroll + ii) < dm_count; ii++) {
          int bh = fixed_bh;
          if (portrait_expand) {
            int rp = _history.dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_scroll + ii);
            if (rp >= 0) {
              char hsb[33];
              const char* hbody = skipReplyPrefix(dmDisplayParts(_history.dmAtPos(rp), is_room, filtered_name, hsb, sizeof(hsb)));
              display.translateUTF8ToBlocks(s_wrap_trans, hbody, sizeof(s_wrap_trans));
              int nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, display.width() - 6 - reserve, s_wrap_lines, 8);
              bh = (1 + (nl > 0 ? nl : 1)) * lh + 1;
            }
          }
          int box_top = cur_y - bh;
          if (box_top < hist_start_y) break;
          box_ys[n_vis] = box_top; box_hs[n_vis] = bh; n_vis++;
          cur_y = box_top - box_gap;
        }
      }
      _hist_visible = n_vis;
      for (int i = 0; i < n_vis && (_dm_hist_scroll + i) < dm_count; i++) {
        int item = _dm_hist_scroll + i;
        bool sel = (item == _dm_hist_sel);
        int y = box_ys[i];
        int bh = box_hs[i];

        int ring_pos = _history.dmHistEntryForContact(_sel_contact.id.pub_key, item);
        if (ring_pos < 0) continue;

        const DmHistEntry& e = _history.dmAtPos(ring_pos);
        char sender_buf[33];
        const char* body = skipReplyPrefix(dmDisplayParts(e, is_room, filtered_name, sender_buf, sizeof(sender_buf)));
        const char* sender = sender_buf;

        char age[6]; geo::fmtAgeShort(age, sizeof(age), now_ts, e.timestamp);
        int age_w = age[0] ? display.getTextWidth(age) + 3 : 0;

        // Size the bubble to its own content before drawing anything (see
        // computeBubbleBox): the header (sender+ack+age) vs the body, measured
        // once here and reused below instead of re-wrapping.
        int full_avail = display.width() - reserve;
        int ack_w = e.outgoing ? (3 + ackGlyphWidth(display, _history.dmEffectiveStatus(e), e.attempt + 1)) : 0;
        int header_w = 3 + display.getTextWidth(sender) + ack_w + age_w + 3;
        int body_w, nl = 0;
        if (portrait_expand) {
          display.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
          nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, full_avail - 6, s_wrap_lines, 8);
          body_w = 0;
          for (int li = 0; li < nl; li++) { int w = display.getTextWidth(s_wrap_lines[li]); if (w > body_w) body_w = w; }
          body_w += 6;
        } else {
          int raw_w = display.getTextWidth(body);
          body_w = (raw_w > full_avail - 6 ? full_avail - 6 : raw_w) + 6;
        }
        BubbleBox box = computeBubbleBox(full_avail, e.outgoing, header_w, body_w);

        drawHistRowFrame(display, box.x, box.w, y, bh, lh, sel);
        display.drawTextEllipsized(box.x + 3, y + 1, box.w - 6 - age_w, sender);
        if (e.outgoing) {                       // delivery marker after "Me"
          int gx = box.x + 3 + display.getTextWidth(sender) + 3;
          drawAckGlyph(display, gx, y + 1, _history.dmEffectiveStatus(e), e.attempt + 1);
        }
        if (age[0]) { display.setCursor(box.x + box.w - age_w, y + 1); display.print(age); }
        if (!sel) display.setColor(DisplayDriver::LIGHT);
        if (portrait_expand) {
          for (int li = 0; li < nl; li++) { display.setCursor(box.x + 3, y + (li + 1) * lh + 1); display.print(s_wrap_lines[li]); }
        } else {
          display.drawTextEllipsized(box.x + 3, y + lh + 1, box.w - 6, body);
        }
      }

      if (dm_count == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width()/2, display.height()/2, "No messages yet");
      }

      // Scrollbar: track pinned to the full list area; thumb sized/positioned
      // from hs's pixel metrics (stable while scrolling the same list).
      // hs.scroll_px counts index-space distance from item 0 (newest); since
      // item 0 now renders at the bottom, invert it here so the thumb sits at
      // the bottom for the newest and rises as you scroll into older history.
      if (hs.need) {
        long span = hs.total_px - hs.view_px;
        long inv_scroll_px = span - hs.scroll_px;
        if (inv_scroll_px < 0) inv_scroll_px = 0;
        drawScrollIndicatorPx(display, hist_start_y + 1, hs.view_px,
                              hs.total_px, hs.view_px, inv_scroll_px);
      }

      drawComposeButton(display, cby, lh, _dm_hist_sel == -1);
      if (_ctx_menu.active) _ctx_menu.render(display);
      return dm_count > 0 ? 500 : 2000;

    } else if (_phase == CHANNEL_HIST) {
      if (_fs.active && _hist_sel >= 0) {
        int fs_hist_count = _history.histCountForChannel(_sel_channel_idx);
        int ring_pos = _history.histEntryForChannel(_sel_channel_idx, _hist_sel);
        if (ring_pos >= 0) {
          const char* ftext = _history.chAtPos(ring_pos).text;
          const char* fsep = strstr(ftext, ": ");
          char fsender[33], fmsg[MSG_TEXT_BUF];
          if (fsep) {
            int nl = fsep - ftext; if (nl > (int)sizeof(fsender) - 1) nl = sizeof(fsender) - 1;
            strncpy(fsender, ftext, nl); fsender[nl] = '\0';
            strncpy(fmsg, fsep + 2, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          } else {
            strcpy(fsender, "?");
            strncpy(fmsg, ftext, sizeof(fmsg) - 1); fmsg[sizeof(fmsg)-1] = '\0';
          }
          int ret = _fs.render(display, fsender, fmsg,
                               _hist_sel < fs_hist_count - 1,
                               _hist_sel > 0);
          // Channels: ✓ only once a repeater echo confirms relay (see list view).
          if (strcmp(fsender, "Me") == 0 && _history.chAtPos(ring_pos).relay_status == ACK_OK) {
            display.setColor(DisplayDriver::DARK);
            drawAckGlyph(display, 2 + display.getTextWidth(fsender) + 3, 1, ACK_OK);
            display.setColor(DisplayDriver::LIGHT);
          }
          if (_ctx_menu.active) _ctx_menu.render(display);
          return ret;
        }
        return 2000;
      }

      int hist_start_y = display.headerH();
      int cby = display.height() - lh;   // compose button is now exactly lh tall, no added border padding

      ChannelDetails ch;
      the_mesh.getChannel(_sel_channel_idx, ch);
      char title[24];
      snprintf(title, sizeof(title), "%.23s", ch.name);
      display.drawCenteredHeader(title, true, _ctx_menu.active);

      int ch_hist_count = _history.histCountForChannel(_sel_channel_idx);
      uint32_t now_ts = rtc_clock.getCurrentTime();

      // Portrait e-ink (height > width): variable-height boxes that show the full
      // wrapped message text. All other displays/orientations: compact 2-line boxes.
      bool portrait_expand = (display.height() > display.width());
      const int MAX_VIS_BOXES = 8;
      int box_ys[MAX_VIS_BOXES], box_hs[MAX_VIS_BOXES], n_vis = 0;
      // Fixed-track scrollbar metrics + stable gutter reserve (see DM history above).
      HistScroll hs = computeHistScroll(display, portrait_expand, ch_hist_count, _hist_scroll,
          hist_start_y, cby, lh,
          [&](int idx) -> const char* {
            int rp = _history.histEntryForChannel(_sel_channel_idx, idx);
            if (rp < 0) return nullptr;
            const char* t = _history.chAtPos(rp).text;
            const char* s = strstr(t, ": ");
            return skipReplyPrefix(s ? s + 2 : t);
          });
      int reserve = hs.reserve;
      {
        // Stack boxes upward from just above the compose row — see the DM
        // history block above for why (newest at the bottom, like a typical
        // messenger). box_ys[i] still corresponds to item (_hist_scroll + i).
        const int fixed_bh = 2 * lh + 1;
        const int box_gap = portrait_expand ? 2 : 1;
        int cur_y = cby - box_gap;   // reserve the same gap against compose as between boxes
        for (int ii = 0; ii < MAX_VIS_BOXES && (_hist_scroll + ii) < ch_hist_count; ii++) {
          int bh = fixed_bh;
          if (portrait_expand) {
            int rp = _history.histEntryForChannel(_sel_channel_idx, _hist_scroll + ii);
            if (rp >= 0) {
              const char* rtext = _history.chAtPos(rp).text;
              const char* rsep = strstr(rtext, ": ");
              const char* rbody = rsep ? rsep + 2 : rtext;
              display.translateUTF8ToBlocks(s_wrap_trans, skipReplyPrefix(rbody), sizeof(s_wrap_trans));
              int nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, display.width() - 6 - reserve, s_wrap_lines, 8);
              bh = (1 + (nl > 0 ? nl : 1)) * lh + 1;
            }
          }
          int box_top = cur_y - bh;
          if (box_top < hist_start_y) break;
          box_ys[n_vis] = box_top; box_hs[n_vis] = bh; n_vis++;
          cur_y = box_top - box_gap;
        }
      }
      _hist_visible = n_vis;
      updateChannelUnread();  // mark everything in the just-computed visible window as seen

      for (int i = 0; i < n_vis && (_hist_scroll + i) < ch_hist_count; i++) {
        int item = _hist_scroll + i;
        bool sel = (item == _hist_sel);
        int y = box_ys[i];
        int bh = box_hs[i];

        int ring_pos = _history.histEntryForChannel(_sel_channel_idx, item);
        if (ring_pos < 0) continue;

        const char* text = _history.chAtPos(ring_pos).text;
        const char* sep = strstr(text, ": ");
        char sender[33], msg_part[MSG_TEXT_BUF];
        if (sep) {
          int nl = sep - text; if (nl > (int)sizeof(sender) - 1) nl = sizeof(sender) - 1;
          strncpy(sender, text, nl); sender[nl] = '\0';
          strncpy(msg_part, sep + 2, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        } else {
          strcpy(sender, "?");
          strncpy(msg_part, text, sizeof(msg_part) - 1);
          msg_part[sizeof(msg_part) - 1] = '\0';
        }

        char age[6]; geo::fmtAgeShort(age, sizeof(age), now_ts, _history.chAtPos(ring_pos).timestamp);
        int age_w = age[0] ? display.getTextWidth(age) + 3 : 0;
        const char* body = skipReplyPrefix(msg_part);

        // Size the bubble to its own content before drawing anything (see
        // computeBubbleBox): the header (sender+ack+age) vs the body, measured
        // once here and reused below instead of re-wrapping. Channels have no
        // recipient ACK — only show ✓ once a repeater echo confirms the send
        // was relayed into the mesh; otherwise no marker (absence is normal).
        bool outgoing = strcmp(sender, "Me") == 0;
        bool show_ack = outgoing && _history.chAtPos(ring_pos).relay_status == ACK_OK;
        int full_avail = display.width() - reserve;
        int ack_w = show_ack ? (3 + ackGlyphWidth(display, ACK_OK, 1)) : 0;
        int header_w = 3 + display.getTextWidth(sender) + ack_w + age_w + 3;
        int body_w, nl = 0;
        if (portrait_expand) {
          display.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
          nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, full_avail - 6, s_wrap_lines, 8);
          body_w = 0;
          for (int li = 0; li < nl; li++) { int w = display.getTextWidth(s_wrap_lines[li]); if (w > body_w) body_w = w; }
          body_w += 6;
        } else {
          int raw_w = display.getTextWidth(body);
          body_w = (raw_w > full_avail - 6 ? full_avail - 6 : raw_w) + 6;
        }
        BubbleBox box = computeBubbleBox(full_avail, outgoing, header_w, body_w);

        drawHistRowFrame(display, box.x, box.w, y, bh, lh, sel);
        display.drawTextEllipsized(box.x + 3, y + 1, box.w - 6 - age_w, sender);
        if (show_ack) {
          int gx = box.x + 3 + display.getTextWidth(sender) + 3;
          drawAckGlyph(display, gx, y + 1, ACK_OK);
        }
        if (age[0]) { display.setCursor(box.x + box.w - age_w, y + 1); display.print(age); }
        if (!sel) display.setColor(DisplayDriver::LIGHT);
        if (portrait_expand) {
          for (int li = 0; li < nl; li++) { display.setCursor(box.x + 3, y + (li + 1) * lh + 1); display.print(s_wrap_lines[li]); }
        } else {
          display.drawTextEllipsized(box.x + 3, y + lh + 1, box.w - 6, body);
        }
      }

      if (ch_hist_count == 0) {
        display.setColor(DisplayDriver::LIGHT);
        display.drawTextCentered(display.width()/2, display.height()/2, "No messages yet");
      }

      // Scrollbar: track pinned to the full list area; thumb from hs metrics.
      // Inverted for the same reason as the DM history block above — item 0
      // (newest) renders at the bottom, so the thumb should start at the
      // bottom too and rise as you scroll into older history.
      if (hs.need) {
        long span = hs.total_px - hs.view_px;
        long inv_scroll_px = span - hs.scroll_px;
        if (inv_scroll_px < 0) inv_scroll_px = 0;
        drawScrollIndicatorPx(display, hist_start_y + 1, hs.view_px,
                              hs.total_px, hs.view_px, inv_scroll_px);
      }

      drawComposeButton(display, cby, lh, _hist_sel == -1);
      if (_ctx_menu.active) _ctx_menu.render(display);

    } else if (_phase == KEYBOARD) {
      return _kb->render(display);

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
      display.drawCenteredHeader(title);

      int total_msg_items = 1 + _active_msg_count;
      drawList(display, total_msg_items, _msg_sel, _msg_scroll, [&](int idx, int y, bool sel, int reserve) {
        drawRowSelection(display, y, sel, reserve);

        if (idx == 0) {
          display.setCursor(2, y);
          display.print("Custom message...");
        } else {
          NodePrefs* p = _task->getNodePrefs();
          int slot = _active_msgs[idx - 1];
          const char* tmpl = p ? p->custom_msgs[slot] : "";
          display.drawTextEllipsized(2, y, display.width() - 4 - reserve, tmpl);
        }
      });
    }
    return 2000;
  }

  bool handleInput(char c) override {
    // Channel Add/Edit form consumes all input while active.
    if (_ch_view.active()) return _ch_view.handleInput(c);

    // Navigate view: any back key returns to the message it was opened from.
    if (_nav_active) {
      if (c == KEY_CANCEL || c == KEY_ENTER || c == KEY_CONTEXT_MENU) _nav_active = false;
      return true;
    }
    if (_phase == MODE_SELECT) {
      // Context menu (Mark-all-read) takes precedence while active.
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          int cleared = 0;
          if (_mode_sel == 0) {
            cleared = getDMUnreadTotal();
            _task->clearAllDMUnread();
          } else if (_mode_sel == 1) {
            cleared = _task->getChannelUnreadCount();
            clearAllChannelUnread();
          } else {
            cleared = _task->getRoomUnreadCount();
            _task->clearRoomUnread();
          }
          markReadAlert(cleared);
        }
        return true;
      }
      if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
      if (c == KEY_UP)   { _mode_sel = (_mode_sel > 0) ? _mode_sel - 1 : 2; return true; }
      if (c == KEY_DOWN) { _mode_sel = (_mode_sel < 2) ? _mode_sel + 1 : 0; return true; }
      if (c == KEY_CONTEXT_MENU) {
        // PopupMenu stores the title pointer verbatim — use static strings.
        static const char* MODE_TITLES[] = { "DM options", "Channel options", "Room options" };
        _ctx_menu.begin(MODE_TITLES[_mode_sel < 3 ? _mode_sel : 0], 1);
        _ctx_menu.addItem("Mark all read");
        return true;
      }
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
        if (_room_mode) {
          auto res = _ctx_menu.handleInput(c);
          if (res == PopupMenu::SELECTED && _num_contacts > 0) {
            if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
              int sel = _ctx_menu.selectedIndex();
              if (sel == 0) {
                _login_mode = true;
                _kb->begin("", 15); // room/repeater password: max 15 chars
                _kb->clearPlaceholders();   // {loc}/{time} are for messages, not a password
                _phase = KEYBOARD;
              } else {
                // Logout: only reachable when isRoomLoggedIn() added this item.
                the_mesh.logoutRoom(_sel_contact.id.pub_key);
                forgetRoomLoggedIn(_sel_contact.id.pub_key);
                _task->showAlert("Logged out", 1000);
              }
            }
          }
          return true;
        }
        // LEFT/RIGHT cycle Notif/Melody in-place (menu stays open).
        if (!_pin_picker_active && _num_contacts > 0) {
          bool left  = keyIsPrev(c);
          bool right = keyIsNext(c);
          if (left || right) {
            static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
            static const char* ML[]           = { "global", "M1", "M2" };
            ContactInfo ci;
            if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) {
              int sel = _ctx_menu.selectedIndex();
              if (sel == 1) {
                uint8_t v = dmNotifState(ci.id.pub_key);
                v = right ? (v + 1) % 3 : (v + 2) % 3;
                setDmNotifState(ci.id.pub_key, v);
                snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s", NOTIF_LABELS[v]);
                _ctx_dirty = true;
              } else if (sel == 2) {
                uint8_t v = dmMelodySlot(ci.id.pub_key);
                v = right ? (v + 1) % 3 : (v + 2) % 3;
                setDmMelody(ci.id.pub_key, v);
                snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s", ML[v]);
                _ctx_dirty = true;
              }
            }
            return true;
          }
        }
        auto res = _ctx_menu.handleInput(c);
        if (_pin_picker_active) {
          // Slot picker sub-menu: index 0..FAVOURITES_COUNT-1 maps directly to slot.
          if (res == PopupMenu::SELECTED && _num_contacts > 0) {
            ContactInfo ci;
            if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) {
              int slot = _ctx_menu.selectedIndex();
              _task->setFavouriteSlot(slot, ci.id.pub_key);
              the_mesh.savePrefs();
              char alert[24];
              snprintf(alert, sizeof(alert), "Pinned to slot %d", slot + 1);
              _task->showAlert(alert, 800);
            }
          }
          if (res != PopupMenu::NONE) _pin_picker_active = false;
          return true;
        }
        if (res == PopupMenu::SELECTED && _num_contacts > 0) {
          ContactInfo ci;
          if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) {
            int sel = _ctx_menu.selectedIndex();
            if (sel == 0) {
              int cleared = (int)_task->getDMUnread(ci.id.pub_key);
              _task->clearDMUnread(ci.id.pub_key);
              markReadAlert(cleared);
            } else if (sel == 3) {
              // Pin / Unpin
              int pinned_slot = _task->findFavouriteSlot(ci.id.pub_key);
              if (pinned_slot >= 0) {
                _task->clearFavouriteSlot(pinned_slot);
                the_mesh.savePrefs();
                char alert[24];
                snprintf(alert, sizeof(alert), "Unpinned (slot %d)", pinned_slot + 1);
                _task->showAlert(alert, 800);
              } else {
                for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
                  if (_task->isFavouriteSlotEmpty(s)) {
                    snprintf(_pin_slot_labels[s], sizeof(_pin_slot_labels[s]), "Slot %d: empty", s + 1);
                  } else {
                    char nm[16] = "?";
                    NodePrefs* p = _task->getNodePrefs();
                    if (p) {
                      const uint8_t* pfx = p->favourite_contacts[s];
                      for (int idx = 0; ; idx++) {
                        ContactInfo c2;
                        if (!the_mesh.getContactByIdx(idx, c2)) break;
                        if (memcmp(c2.id.pub_key, pfx, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
                          DisplayDriver::translateUTF8Static(nm, c2.name, sizeof(nm));
                          break;
                        }
                      }
                    }
                    snprintf(_pin_slot_labels[s], sizeof(_pin_slot_labels[s]), "Slot %d: %s", s + 1, nm);
                  }
                }
                _ctx_menu.begin("Pick slot", 3);
                for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) _ctx_menu.addItem(_pin_slot_labels[s]);
                _pin_picker_active = true;
              }
            }
            // sel == 1 (Notif) and sel == 2 (Melody): already cycled via LEFT/RIGHT; ENTER just closes.
          }
        }
        if (res != PopupMenu::NONE) _task->savePrefsIfDirty(_ctx_dirty);
        return true;
      }
      if (c == KEY_CANCEL) {
        if (_pick_bot_room) { _pick_bot_room = false; _room_mode = false; _task->gotoBotScreen(); return true; }
        _room_mode = false;
        _phase = MODE_SELECT;
        return true;
      }
      // drawList() reclamps _contact_scroll from _contact_sel every render.
      if (c == KEY_UP   && _num_contacts > 0) { _contact_sel = (_contact_sel > 0) ? _contact_sel - 1 : _num_contacts - 1; return true; }
      if (c == KEY_DOWN && _num_contacts > 0) { _contact_sel = (_contact_sel < _num_contacts - 1) ? _contact_sel + 1 : 0; return true; }
      if (c == KEY_ENTER && _num_contacts > 0) {
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
          if (_pick_target) { commitPickTargetDM(_sel_contact); return true; }
          if (_pick_bot_room) {
            if (!isRoomLoggedIn(_sel_contact.id.pub_key)) {
              char saved_pw[sizeof(_login_pw)];
              if (the_mesh.getRoomPassword(_sel_contact.id.pub_key, saved_pw, sizeof(saved_pw))) {
                // Known password, just not re-established this boot — retry
                // silently in the background; the bot target is set below
                // regardless of this attempt's outcome (self-heals like any
                // other saved room password would on the next real open).
                startRoomLogin(saved_pw);
              } else {
                // Never logged in — the bot could never post here without a
                // password, so prompt for one now instead of picking an
                // unusable target. Commits once submitted (see the KEYBOARD/
                // _login_mode handler), whether or not login itself succeeds.
                _login_mode = true;
                _kb->begin("", 15); // room/repeater password: max 15 chars
                _kb->clearPlaceholders();
                _phase = KEYBOARD;
                return true;
              }
            }
            commitPickBotRoom(_sel_contact);
            return true;
          }
          if (_room_mode && !isRoomLoggedIn(_sel_contact.id.pub_key)) {
            // Posting to a room requires a login handshake first (even with a
            // blank password) — go straight to the password prompt instead of
            // a history view that would silently fail to send.
            char saved_pw[sizeof(_login_pw)];
            if (the_mesh.getRoomPassword(_sel_contact.id.pub_key, saved_pw, sizeof(saved_pw))) {
              // Logged in to this room before, on an earlier boot -- retry
              // with the remembered password instead of prompting again.
              startRoomLogin(saved_pw);
            } else {
              _login_mode = true;
              _kb->begin("", 15); // room/repeater password: max 15 chars
              _kb->clearPlaceholders();   // {loc}/{time} are for messages, not a password
              _phase = KEYBOARD;
            }
            return true;
          }
          openDmHistory();
          if (_share_mode) beginShareCompose(false);
        }
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _num_contacts > 0 && _room_mode) {
        ContactInfo ci;
        bool logged_in = the_mesh.getContactByIdx(_sorted[_contact_sel], ci) && isRoomLoggedIn(ci.id.pub_key);
        _ctx_menu.begin("Room options", logged_in ? 2 : 1);
        _ctx_menu.addItem("Login...");
        if (logged_in) _ctx_menu.addItem("Logout");
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
        int pinned_slot = _task->findFavouriteSlot(ci.id.pub_key);
        if (pinned_slot >= 0) snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Unpin (slot %d)", pinned_slot + 1);
        else                  snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Pin to dial");
        _ctx_menu.begin("Contact options", 3);
        _ctx_menu.addItem("Mark as read");
        _ctx_menu.addItem(_ctx_notif_item);
        _ctx_menu.addItem(_ctx_melody_item);
        _ctx_menu.addItem(_ctx_pin_item);
        _ctx_dirty = false;
        return true;
      }

    } else if (_phase == CHANNEL_PICK) {
      // Context menu consumes all input while open
      if (_ctx_menu.active) {
        // LEFT/RIGHT cycle Notif/Melody/Fav in-place (menu stays open).
        if (_num_channels > 0) {
          bool left  = keyIsPrev(c);
          bool right = keyIsNext(c);
          if (left || right) {
            static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
            static const char* ML[]           = { "global", "M1", "M2" };
            uint8_t ch_idx = _ctx_ch_idx;   // frozen at menu open — see declaration
            int sel = _ctx_menu.selectedIndex();
            if (sel == 1) {
              uint8_t v = chNotifState(ch_idx);
              v = right ? (v + 1) % 3 : (v + 2) % 3;
              setChNotifState(ch_idx, v);
              snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s", NOTIF_LABELS[v]);
              _ctx_dirty = true;
            } else if (sel == 2) {
              uint8_t v = chNotifMelody(ch_idx);
              v = right ? (v + 1) % 3 : (v + 2) % 3;
              setChNotifMelody(ch_idx, v);
              snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s", ML[v]);
              _ctx_dirty = true;
            } else if (sel == 3) {
              NodePrefs* p2 = _task->getNodePrefs();
              if (p2) {
                p2->ch_fav_bitmask ^= (1ULL << ch_idx);
                bool is_fav = (p2->ch_fav_bitmask & (1ULL << ch_idx));
                snprintf(_ctx_ch_fav_item, sizeof(_ctx_ch_fav_item), is_fav ? "Fav: yes" : "Fav: no");
                _ctx_dirty = true;
                // List rebuild is deferred to menu close: with the fav-only
                // filter on, un-favouriting this channel removes it from the
                // list, and rebuilding under the open menu would shift
                // _channel_sel onto a different channel mid-interaction.
              }
            }
            return true;
          }
        }
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _num_channels > 0) {
          uint8_t ch_idx = _ctx_ch_idx;   // frozen at menu open — see declaration
          int sel = _ctx_menu.selectedIndex();
          if (sel == 0) {
            int cleared = (int)_history.chUnread(ch_idx);
            _history.setChUnread(ch_idx, 0);
            markReadAlert(cleared);
          } else if (sel == 4) {              // Edit
            ChannelDetails ch;
            if (the_mesh.getChannel(ch_idx, ch)) _ch_view.openEdit(ch_idx, ch.name);
          } else if (sel == 5) {              // Delete
            ChannelDetails ch;
            memset(&ch, 0, sizeof(ch));
            the_mesh.setChannelLocal(ch_idx, ch);
            _task->showAlert("Channel deleted", 1000);
          }
          // sel 1/2/3 already handled by LEFT/RIGHT; ENTER just closes.
        }
        if (res != PopupMenu::NONE) {
          _task->savePrefsIfDirty(_ctx_dirty);
          // Apply any Fav/Edit/Delete change to the visible list now that the menu is done.
          buildChannelList();
          if (_channel_sel >= _num_channels) _channel_sel = _num_channels > 0 ? _num_channels - 1 : 0;
        }
        return true;
      }
      if (c == KEY_CANCEL) {
        if (_pick_bot_channel) { _pick_bot_channel = false; _task->gotoBotScreen(); return true; }
        _phase = MODE_SELECT;
        return true;
      }
      // drawList() reclamps _channel_scroll from _channel_sel every render.
      { int total = channelPickTotal();
        if (c == KEY_UP   && total > 0) { _channel_sel = (_channel_sel > 0) ? _channel_sel - 1 : total - 1; return true; }
        if (c == KEY_DOWN && total > 0) { _channel_sel = (_channel_sel < total - 1) ? _channel_sel + 1 : 0; return true; }
      }
      if (c == KEY_ENTER && _channel_sel == _num_channels && !_pick_bot_channel) {
        int idx = findFreeChannelSlot();
        if (idx < 0) _task->showAlert("Channels full", 1200);
        else         _ch_view.openAdd(idx);
        return true;
      }
      if (c == KEY_ENTER && _num_channels > 0 && _channel_sel < _num_channels) {
        _sel_channel_idx = _channel_indices[_channel_sel];
        if (_pick_target) { commitPickTargetChannel(_sel_channel_idx); return true; }
        if (_pick_bot_channel) { commitPickBotChannel(_sel_channel_idx); return true; }
        int hc = _history.histCountForChannel(_sel_channel_idx);
        _unread_at_entry = (int)_history.chUnread(_sel_channel_idx);
        _hist_scroll = 0;
        _hist_sel = hc > 0 ? 0 : -1;
        _viewing_max_seen = _hist_sel >= 0 ? _hist_sel : 0;
        _phase = CHANNEL_HIST;
        // Not updateChannelUnread() here: _hist_visible is still whatever this
        // channel's first render() hasn't computed yet (stale, shared with
        // DM_HIST) — calling it now could ratchet _viewing_max_seen past what's
        // actually about to be shown. render() calls it once that's fresh.
        if (_share_mode) beginShareCompose(true);
        return true;
      }
      if (c == KEY_CONTEXT_MENU && _num_channels > 0 && _channel_sel < _num_channels) {
        uint8_t ch_idx = _channel_indices[_channel_sel];
        _ctx_ch_idx = ch_idx;   // freeze the menu's target channel
        static const char* NOTIF_LABELS[] = { "default", "OFF", "ON" };
        snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s",
                 NOTIF_LABELS[chNotifState(ch_idx)]);
        { static const char* ML[] = { "global", "M1", "M2" };
          snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s",
                   ML[chNotifMelody(ch_idx)]); }
        { NodePrefs* p2 = _task->getNodePrefs();
          bool is_fav = p2 && (p2->ch_fav_bitmask & (1ULL << ch_idx));
          snprintf(_ctx_ch_fav_item, sizeof(_ctx_ch_fav_item), is_fav ? "Fav: yes" : "Fav: no"); }
        _ctx_menu.begin("Channel options", 6);
        _ctx_menu.addItem("Mark all read");
        _ctx_menu.addItem(_ctx_notif_item);
        _ctx_menu.addItem(_ctx_melody_item);
        _ctx_menu.addItem(_ctx_ch_fav_item);
        _ctx_menu.addItem("Edit");
        _ctx_menu.addItem("Delete");
        _ctx_dirty = false;
        return true;
      }

    } else if (_phase == DM_HIST) {
      int dm_count = _history.dmHistCountForContact(_sel_contact.id.pub_key);
      if (_dm_fs.active) {
        if (_ctx_menu.active) {
          auto res = _ctx_menu.handleInput(c);
          if (res == PopupMenu::SELECTED) {
            dispatchFsAction(false);
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
          int ring_pos = _history.dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
          if (ring_pos >= 0) {
            bool reply_ok = !_history.dmAtPos(ring_pos).outgoing;
            if (reply_ok) buildDmReplyPrefix(_history.dmAtPos(ring_pos));
            buildFsMenu(_history.dmAtPos(ring_pos).text, reply_ok);
          }
        }
        return true;
      }
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          dispatchFsAction(false);
        } else if (res != PopupMenu::NONE) {
          _ctx_menu.active = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) {
        if (_dm_direct_entry) {
          _dm_direct_entry = false;
          _task->gotoHomeScreen();
        } else {
          _phase = CONTACT_PICK;
        }
        return true;
      }
      // Newest (index 0) now renders at the bottom, oldest at the top (see the
      // render block above), so UP/DOWN swap which direction walks the index:
      // UP now climbs toward older (higher index, physically upward); DOWN
      // now walks toward newer (lower index, physically downward) and off the
      // bottom (index 0) reaches the compose row, which sits right below it.
      if (c == KEY_UP) {
        if (_dm_hist_sel == -1 && dm_count > 0) { _dm_hist_sel = 0; _dm_hist_scroll = 0; }
        else if (_dm_hist_sel >= 0 && _dm_hist_sel < dm_count - 1) {
          _dm_hist_sel++;
          if (_dm_hist_sel >= _dm_hist_scroll + _hist_visible)
            _dm_hist_scroll = _dm_hist_sel - _hist_visible + 1;
        }
        return true;
      }
      if (c == KEY_DOWN) {
        if (_dm_hist_sel > 0) {
          _dm_hist_sel--;
          if (_dm_hist_sel < _dm_hist_scroll) _dm_hist_scroll = _dm_hist_sel;
        } else if (_dm_hist_sel == 0) {
          _dm_hist_sel = -1;
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
        int ring_pos = _history.dmHistEntryForContact(_sel_contact.id.pub_key, _dm_hist_sel);
        if (ring_pos >= 0) {
          bool reply_ok = !_history.dmAtPos(ring_pos).outgoing;
          if (reply_ok) buildDmReplyPrefix(_history.dmAtPos(ring_pos));
          buildFsMenu(_history.dmAtPos(ring_pos).text, reply_ok);
        }
        return true;
      }

    } else if (_phase == CHANNEL_HIST) {
      int ch_hist_count = _history.histCountForChannel(_sel_channel_idx);
      if (_fs.active) {
        if (_ctx_menu.active) {
          auto res = _ctx_menu.handleInput(c);
          if (res == PopupMenu::SELECTED) {
            dispatchFsAction(true);
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
          int ring_pos = _history.histEntryForChannel(_sel_channel_idx, _hist_sel);
          if (ring_pos >= 0)
            buildFsMenu(_history.chAtPos(ring_pos).text, buildChannelReplyPrefix(_history.chAtPos(ring_pos).text));
        }
        return true;
      }
      if (_ctx_menu.active) {
        auto res = _ctx_menu.handleInput(c);
        if (res == PopupMenu::SELECTED) {
          dispatchFsAction(true);
        } else if (res != PopupMenu::NONE) {
          _ctx_menu.active = false;
        }
        return true;
      }
      if (c == KEY_CANCEL) { _phase = CHANNEL_PICK; return true; }
      // Newest (index 0) now renders at the bottom, oldest at the top (see the
      // render block above) — UP/DOWN swap direction accordingly, same as the
      // DM history handler above.
      if (c == KEY_UP) {
        if (_hist_sel == -1 && ch_hist_count > 0) { _hist_sel = 0; _hist_scroll = 0; }
        else if (_hist_sel >= 0 && _hist_sel < ch_hist_count - 1) {
          _hist_sel++;
          if (_hist_sel >= _hist_scroll + _hist_visible) _hist_scroll = _hist_sel - _hist_visible + 1;
        }
        updateChannelUnread();
        return true;
      }
      if (c == KEY_DOWN) {
        if (_hist_sel > 0) { _hist_sel--; if (_hist_sel < _hist_scroll) _hist_scroll = _hist_sel; }
        else if (_hist_sel == 0) _hist_sel = -1;
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
        int ring_pos = _history.histEntryForChannel(_sel_channel_idx, _hist_sel);
        if (ring_pos >= 0)
          buildFsMenu(_history.chAtPos(ring_pos).text, buildChannelReplyPrefix(_history.chAtPos(ring_pos).text));
        return true;
      }

    } else if (_phase == KEYBOARD) {
      auto res = _kb->handleInput(c);
      if (_login_mode) {
        if (res == KeyboardWidget::CANCELLED) {
          _login_mode = false;
          _phase = CONTACT_PICK;
        } else if (res == KeyboardWidget::DONE) {
          // Blank password is valid (guest/no-password rooms) — unlike normal
          // message text, an empty submit here is a deliberate "log in with no
          // password" attempt, so it isn't suppressed like an empty message is.
          _login_mode = false;
          startRoomLogin(_kb->buf);
          if (_pick_bot_room) { commitPickBotRoom(_sel_contact); return true; }
          _phase = CONTACT_PICK;
        }
        return true;
      }
      if (res == KeyboardWidget::CANCELLED) {
        if (_share_mode) { _share_mode = false; _task->gotoHomeScreen(); }
        else             { _phase = MSG_PICK; }
      } else if (res == KeyboardWidget::DONE) {
        int prefix_len = _reply_mode ? (int)strlen(_reply_prefix) : 0;
        if (_kb->len > prefix_len) {
          // Expand only the body — prefix "@[nick] " is preserved verbatim, so a nick
          // that happens to contain a placeholder token isn't substituted.
          char expanded[KB_MAX_LEN + 1];
          if (prefix_len > 0) {
            memcpy(expanded, _kb->buf, prefix_len);
            expandMsg(_kb->buf + prefix_len, expanded + prefix_len, sizeof(expanded) - prefix_len);
          } else {
            expandMsg(_kb->buf, expanded, sizeof(expanded));
          }
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
      // drawList() reclamps _msg_scroll from _msg_sel every render.
      if (c == KEY_UP)   { _msg_sel = (_msg_sel > 0) ? _msg_sel - 1 : total_msg_items - 1; return true; }
      if (c == KEY_DOWN) { _msg_sel = (_msg_sel < total_msg_items - 1) ? _msg_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if (_msg_sel == 0) {
          _kb->begin(_reply_mode ? _reply_prefix : "");
          kbAddSensorPlaceholders(*_kb, &sensors);
          _phase = KEYBOARD;
          return true;
        }
        NodePrefs* p = _task->getNodePrefs();
        int slot = _active_msgs[_msg_sel - 1];
        const char* tmpl = p ? p->custom_msgs[slot] : "OK";
        char msg[MSG_TEXT_BUF];
        if (_reply_mode) {
          char body[MSG_TEXT_BUF];
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
