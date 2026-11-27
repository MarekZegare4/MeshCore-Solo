#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after SettingsScreen.h is defined.

#include "NavView.h"   // navigate to a location shared inside a message
#include "icons.h"     // scalable mini-icons (delivery markers)
#include "ChannelsView.h"  // on-device channel add/edit form (Channels tab)
#include "../ui-core/BotConfig.h"   // which channel bot a channel pick is for
#include "../TextFit.h"   // names and replies cut on a whole character

class MessagesScreen : public UIScreen {
  UITask* _task;

  // Shared by every Notif/Melody value row -- contact, room and channel
  // context menus alike -- so the wording only needs to agree in one place.
  // Defined at file scope below (see NearbyScreen::FILTER_LABELS for the same
  // pattern).
  static const char* const NOTIF_LABELS[3];
  static const char* const MELODY_LABELS[3];

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

  // MSG_PICK (shared)
  int _msg_sel, _msg_scroll;
  int _active_msgs[QUICK_MSGS_MAX];
  int _active_msg_count;

  // CHANNEL_HIST — selection + the unread "viewing session" bookkeeping. The
  // history ring itself and the per-channel unread counters live in _history.
  int _hist_sel, _hist_scroll;
  FullscreenMsgView _fs;
  int  _unread_at_entry;    // channel unread count when entering CHANNEL_HIST
  int  _unread_base = 0;    // messages filed since: the unread are indices base.. base+count-1
  int  _seen_lo = 0, _seen_hi = -1;   // the indices shown so far this session

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
  char      _ctx_scope_item[30];   // "Scope: <name>"
  char      _ctx_scope_def[34];    // the picker's first row: "Default (<name>)"
  // Scope sub-picker: single-select from Default + the shared named-scope
  // list, reusing _ctx_menu itself as a popup -- same idiom as
  // _pin_picker_active's "Pick slot" submenu below (rows as chanctl::scope(),
  // no per-row value cycling needed since Enter just picks and closes).
  bool      _scope_pick_active = false;
  char      _ctx_pin_item[28];   // "Pin to dial" or "Unpin (slot N)"
  char      _ctx_fav_item[12]; // "Fav: ON" / "Fav: OFF" — shared by the channel,
                               // contact and room menus (never open at once)
  char      _ctx_save_item[12]; // "Save: ON" / "Save: OFF"
  int       _ctx_save_idx = -1; // its row in the channel menu, -1 with nothing to save
  int       _ctx_fav_idx = -1; // row the Fav toggle sits on, frozen at menu open
  int       _ctx_pin_idx = -1; // ...and the Pin row, in the room menu (it sits after Fav,
                               // whose own row depends on whether Logout is shown)
  char      _pin_slot_labels[NodePrefs::FAVOURITES_COUNT][22];  // per-slot picker labels
  bool      _pin_picker_active;  // true while the slot-picker submenu is open
  int       _pick_fav_slot = -1; // >=0 = browsing to fill that Favourites dial slot
  int       _pick_sos_slot = -1; // >=0 = choosing the contact for that SOS slot
  int       _pin_slot_ch_idx = -1; // >=0 = that submenu is pinning this channel, not a contact
  bool      _ch_delete_confirm_active = false; // true while the Delete/Cancel submenu is open
  bool      _direct_entry;      // entered a history view straight from the Favourites dial;
                                // CANCEL returns home instead of to the picker
  char      _reply_prefix[36];   // "@[nick] " built when reply is triggered
  bool      _reply_mode;         // true while composing a reply (prefix is prepended)

  // Fullscreen-message context menu actions. Built per-message: Reply (when
  // applicable) plus Navigate / Save waypoint when the message carries a
  // location (a {loc} string or a [WAY] share), plus Path/Relayed by when the
  // entry has hop data recorded. _fs_act maps each visible row back to an
  // action so the index math survives the conditional layout.
  enum FsAct : uint8_t { FS_REPLY, FS_NAV, FS_SAVE, FS_TARGET, FS_PATH, FS_ROUTE };
  uint8_t   _fs_act[6];   // Reply, Navigate / Save / Target, Path, Show route
  int       _fs_act_n = 0;
  // Which history entry the currently-open Options menu (and, after FS_PATH is
  // chosen, the path/relay detail popup) refers to -- set by buildFsMenu().
  int       _fs_menu_pos = -1;
  bool      _fs_menu_is_channel = false;
  // True while _ctx_menu is showing the path/relay detail list (opened from
  // FS_PATH) rather than the original Reply/Navigate/... Options menu -- the
  // detail list is purely informational, so any input just dismisses it
  // instead of being routed through dispatchFsAction()'s stale _fs_act mapping.
  bool      _showing_path_detail = false;
  // Resolved hop labels for the path/relay detail popup -- PopupMenu only
  // stores the pointers it's given (see PopupMenu.h), so these must outlive
  // the popup across renders, hence a member buffer, not a stack-local one.
  char      _path_detail_names[MAX_HIST_PATH_BYTES][24];
  // "Path (N hops)" / "Relayed by (N)" Options-row label -- same lifetime
  // requirement as _path_detail_names above (PopupMenu stores the pointer).
  char      _fs_path_item[24];
  // Inline navigate-to-location view layered over the fullscreen message.
  bool      _nav_active = false;
  navview::EtaTracker _nav_eta;   // closing speed / ETA, as in the other navigate views
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
  // per-channel unread counters live in the UI Core's store (ui-core/
  // MessageHistory.h); this screen binds to it by reference. The
  // phase machine below keeps only the view state — selection, scroll, the
  // fullscreen readers — and reaches entries through _history's accessors. The
  // shared types (AckState, ChHistEntry, DmHistEntry, MSG_TEXT_BUF) are file-
  // scope, so they're still referred to unqualified throughout this screen.
  MessageHistory& _history;

  // DM_HIST view state (the ring itself is in _history).
  int _dm_hist_sel, _dm_hist_scroll;
  bool _older = false;   // showing a page of the flash log, not the ring
  FullscreenMsgView _dm_fs;

  // "New" over the first message unread when the conversation opened (index
  // as _hist_sel / _dm_hist_sel, -1: none); the first render scrolls it up top.
  int  _new_idx = -1;
  bool _anchor_new = false;

  int _hist_visible = 2;  // updated in render(); for history list scroll clamping

  void expandMsg(const char* tmpl, char* out, int out_len) const {   // ui-core/MessageText.h
    msgtext::expand(tmpl, out, out_len, _task->getNodePrefs());
  }

  // Scrollbar metrics for a history list. The track is pinned to the full list
  // area (top_y..cby) so it never resizes with content; only the thumb sizes and
  // moves. `need` also drives the gutter reserve so wide message boxes don't
  // reflow as messages arrive.
  struct HistScroll { bool need; int reserve; long total_px, scroll_px; int view_px; };

  // Compact DM bubble text: -1 when it fits on one line beside the marker and
  // age (tail_w), else the byte offset its second line starts at -- the last
  // space that keeps line 1 within line_w, or a hard cut if there is none.
  static int dmSplit(DisplayDriver& d, const char* body, int line_w, int tail_w) {
    if ((int)d.getTextWidth(body) + tail_w <= line_w) return -1;
    const uint8_t* p = (const uint8_t*)body;
    int w = 0, last_sp = -1;
    while (*p) {
      const uint8_t* cp_start = p;
      uint32_t cp = DisplayDriver::decodeCodepoint(p);
      int off = (int)(cp_start - (const uint8_t*)body);
      if (cp == ' ' && w <= line_w) last_sp = off;
      w += d.getCodepointWidth(cp);
      if (w > line_w) return last_sp > 0 ? last_sp + 1 : (off > 0 ? off : (int)(p - (const uint8_t*)body));
    }
    return -1;
  }

  // Scrollbar metrics for boxes of varying height (h(idx, reserve) px each,
  // `gap` px apart): the same fit test as the expanded path below.
  template <class GetH>
  HistScroll computeHistScrollH(DisplayDriver& display, int count, int scroll, int hist_start_y, int cby,
                                int gap, GetH h) {
    HistScroll r{};
    r.view_px = cby - (hist_start_y + 1);
    if (r.view_px < 1) r.view_px = 1;
    const int col = scrollIndicatorColWidth(display);
    int cur = hist_start_y, fit = 0;
    for (int i = 0; i < count; i++) { int bh = h(i, 0); if (cur + bh > cby) break; fit++; cur += bh + gap; }
    r.need    = fit < count;
    r.reserve = r.need ? col : 0;
    if (r.need)
      for (int i = 0; i < count; i++) {
        long ext = h(i, r.reserve) + gap;
        r.total_px += ext;
        if (i < scroll) r.scroll_px += ext;
      }
    return r;
  }

  // `getBody(idx)` returns the body text for list item idx (the part that wraps),
  // or nullptr to fall back to a fixed 2-line box. Must return exactly what the
  // real per-item render pass wraps (sender split off, reply prefix stripped) --
  // this function doesn't reprocess it, so a mismatch here just means this sizing
  // pass and the real render disagree on line count.
  template <class GetBody>
  HistScroll computeHistScroll(DisplayDriver& display, bool expand, int count, int scroll,
                               int hist_start_y, int cby, int lh, GetBody getBody) {
    HistScroll r{};
    const int fixed_bh = 2 * lh + 1;
    const int top_y    = hist_start_y + 1;
    r.view_px = cby - top_y;                  // fixed track height = full list area
    if (r.view_px < 1) r.view_px = 1;
    const int col = scrollIndicatorColWidth(display);

    if (!expand) {                            // uniform boxes → exact pixel math
      const int box = fixed_bh + 1;
      r.total_px  = (long)count * box;
      r.scroll_px = (long)scroll * box;
      r.need      = r.total_px > r.view_px;
      r.reserve   = r.need ? col : 0;
      if (!r.need) r.scroll_px = 0;
      return r;
    }

    const int sp = 2;                         // expanded inter-box spacing
    auto boxH = [&](int idx, int rsv) -> int {
      const char* body = getBody(idx);
      return body ? expandedBoxH(display, body, display.width() - rsv) : fixed_bh;
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
  // An MECP message comes back in words (mecp::shown).
  static const char* skipReplyPrefix(const char* text) { return mecp::shown(msgReplyBody(text)); }

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
      textfit::copy(sender_buf, sender_cap, "Me");
      return body;
    }
    const char* sep = is_room ? strstr(body, ": ") : nullptr;
    if (sep) {
      textfit::copyN(sender_buf, sender_cap, body, (size_t)(sep - body));
      return sep + 2;   // body after "Sender: "
    }
    textfit::copy(sender_buf, sender_cap, contact_name);
    return body;
  }

  // A channel message's "Sender: text": the sender into `sender` (cut on a
  // whole character, "" when there's no separator), the text after it returned
  // (all of it when there's none).
  static const char* chSenderParts(const char* text, char* sender, size_t n) {
    const char* sep = strstr(text, ": ");
    if (!sep) { sender[0] = '\0'; return text; }
    textfit::copyN(sender, n, text, (size_t)(sep - text));
    return sep + 2;
  }

  // Build "@[nick] " prefix from a channel message text ("nick: body") into _reply_prefix.
  // Returns false if sender is "Me" (own message — no reply prefix needed).
  bool buildChannelReplyPrefix(const char* text) {
    if (!strstr(text, ": ")) return false;
    char nick[32];
    chSenderParts(text, nick, sizeof(nick));
    if (strcmp(nick, "Me") == 0) return false;
    snprintf(_reply_prefix, sizeof(_reply_prefix), "@[%s] ", nick);
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

  // The keyboard's field label while writing: who it goes to.
  char _kb_prompt[16];
  void setComposePrompt() {
    const char* to = _sel_contact.name;
    ChannelDetails ch;
    if (_sending_to_channel && the_mesh.getChannel(_sel_channel_idx, ch)) to = ch.name;
    snprintf(_kb_prompt, sizeof(_kb_prompt), "%.12s", to);
    _kb->prompt = _kb_prompt;
    // The room it really has: this node's name rides a channel post, and the
    // placeholders grow when sent.
    _kb->sent_limit = msgtext::limit(_sending_to_channel, _task->getNodePrefs());
    _kb->sent_len = [](const char* b, void* t) { return msgtext::sentLen(b, ((UITask*)t)->getNodePrefs()); };
    _kb->sent_ctx = _task;
  }

  // Recipient chosen while sharing — open the keyboard with the prepared text.
  void beginShareCompose(bool channel) {
    _sending_to_channel = channel;
    _reply_mode = false;
    _kb->begin(_share_text);
    setComposePrompt();
    _phase = KEYBOARD;
  }

  // Build the fullscreen-message options popup: Reply (if allowed), Navigate /
  // Save waypoint / Set as target when `body` carries a location, and Path /
  // Relayed by when the entry (ring_pos, in the DM ring or the channel ring
  // per is_channel) has hop data recorded. Opens _ctx_menu only when there's at
  // least one action. Parses the location once here and stashes it for the
  // action handler; stashes ring_pos/is_channel too, for showPathDetail().
  void buildFsMenu(const char* body, bool reply_allowed, int ring_pos, bool is_channel) {
    _fs_menu_pos = ring_pos;
    _fs_menu_is_channel = is_channel;
    _showing_path_detail = false;
    bool has_loc = geo::parseLatLon(body, _nav_lat, _nav_lon, _nav_label, sizeof(_nav_label));

    uint8_t hop_count = 0;
    bool path_is_relay = false;   // outgoing channel: path[] holds echoing-repeater hashes, not a received route
    if (ring_pos >= 0) {
      uint8_t path_len_packed = is_channel ? _history.chAtPos(ring_pos).path_len : _history.dmAtPos(ring_pos).path_len;
      hop_count = path_len_packed & 63;
      if (is_channel) path_is_relay = (_history.chAtPos(ring_pos).relay_seq != 0);
    }
    bool has_path_item = hop_count > 0;
    const bool has_route = HopRoute::has(body);   // a bot's {path} reply: the route on a map

    int n = (reply_allowed ? 1 : 0) + (has_loc ? 3 : 0) + (has_path_item ? 1 : 0) + (has_route ? 1 : 0);
    if (n == 0) return;
    _fs_act_n = 0;
    _ctx_menu.begin("Options", n);
    if (reply_allowed) { _ctx_menu.addItem("Reply");         _fs_act[_fs_act_n++] = FS_REPLY; }
    if (has_loc)       { _ctx_menu.addItem("Navigate");      _fs_act[_fs_act_n++] = FS_NAV;
                         _ctx_menu.addItem("Save waypoint"); _fs_act[_fs_act_n++] = FS_SAVE;
                         _ctx_menu.addItem("Set as target"); _fs_act[_fs_act_n++] = FS_TARGET; }
    if (has_path_item) {
      if (path_is_relay) snprintf(_fs_path_item, sizeof(_fs_path_item), "Relayed by (%u)", (unsigned)hop_count);
      else               snprintf(_fs_path_item, sizeof(_fs_path_item), "Path (%u hop%s)", (unsigned)hop_count, hop_count == 1 ? "" : "s");
      _ctx_menu.addItem(_fs_path_item);
      _fs_act[_fs_act_n++] = FS_PATH;
    }
    if (has_route) { _ctx_menu.addItem("Show route"); _fs_act[_fs_act_n++] = FS_ROUTE; }
  }

  // The route in the message stashed by buildFsMenu(), placed: the way our
  // message went to whoever wrote this, so from here to them.
  void showRoute() {
    if (_fs_menu_pos < 0) return;
    const char* text;
    const uint8_t* key = nullptr;
    char name[32] = "";
    if (_fs_menu_is_channel) {   // "Name: text"
      text = _history.chAtPos(_fs_menu_pos).text;
      chSenderParts(text, name, sizeof(name));
    } else {
      text = _history.dmAtPos(_fs_menu_pos).text;
      key = _history.dmAtPos(_fs_menu_pos).prefix;
    }
    int32_t lat, lon;
    const bool own = !_fs_menu_is_channel && (_task->currentLocation(lat, lon) || _task->core().fixedPosition(lat, lon));   // a channel's: someone else's way
    HopRoute& R = _task->core().hop_route;
    if (!R.show(text, own, lat, lon, key, name)) return;
    if (R.points() == 0) { R.close(); _task->showAlert("No hop has a position", 1200); }
  }

  // The route shown (showRoute()): the points fitted to the screen, joined;
  // you, each hop's number, the sender; under it how many hops are placed.
  void renderRoute(DisplayDriver& d) {
    const HopRoute& R = _task->core().hop_route;
    drawScreenHeader(d, "Route");
    const int W = d.width(), H = d.height(), lh = d.getLineHeight(), s = miniIconScale(d);
    const int top = d.listStart(), bar_y = H - lh - 1;
    const int ax = 1, ay = top, aw = W - 2, ah = bar_y - top - 2;
    d.setColor(UIColor::primary_txt);
    int32_t mnla = 0, mxla = 0, mnlo = 0, mxlo = 0;
    for (int i = 0; i < R.points(); i++) {
      const HopRoute::Pt& p = R.point(i);
      if (i == 0 || p.lat < mnla) mnla = p.lat;
      if (i == 0 || p.lat > mxla) mxla = p.lat;
      if (i == 0 || p.lon < mnlo) mnlo = p.lon;
      if (i == 0 || p.lon > mxlo) mxlo = p.lon;
    }
    float kx = cosf(((mnla + mxla) / 2.0e6f) * (float)M_PI / 180.0f);
    if (kx < 0.05f) kx = 0.05f;
    float span_la = (float)(mxla - mnla), span_lo = (float)(mxlo - mnlo) * kx;
    if (span_la < 2000.0f) span_la = 2000.0f;
    if (span_lo < 2000.0f) span_lo = 2000.0f;
    const int m = 4 * s + d.getCharWidth();   // the marks and a hop's number clear of the edge
    const float fw = (float)(aw - 2 * m), fh = (float)(ah - 2 * m);
    const float scale = fh / span_la < fw / span_lo ? fh / span_la : fw / span_lo;
    const float cla = (mnla + mxla) / 2.0f, clo = (mnlo + mxlo) / 2.0f;
    auto proj = [&](const HopRoute::Pt& p, float& x, float& y) {
      x = ax + aw / 2 + ((float)p.lon - clo) * kx * scale;
      y = ay + ah / 2 - ((float)p.lat - cla) * scale;
    };
    float x0 = 0, y0 = 0;
    for (int i = 0; i < R.points(); i++) {
      float x, y; proj(R.point(i), x, y);
      if (i > 0) gfx::drawLineClipped(d, x0, y0, x, y, ax, ay, aw, ah);
      x0 = x; y0 = y;
    }
    for (int i = 0; i < R.points(); i++) {
      const HopRoute::Pt& p = R.point(i);
      float fx, fy; proj(p, fx, fy);
      const int x = (int)lroundf(fx), y = (int)lroundf(fy);
      if (p.hop == -1)      miniIconDrawCentered(d, x, y, ICON_MAP_CURRENT);
      else if (p.hop == -2) miniIconDrawCentered(d, x, y, ICON_MAP_CONTACT);
      else {
        miniIconDrawCentered(d, x, y, ICON_MAP_DOT);
        char t[4];
        snprintf(t, sizeof(t), "%d", p.hop + 1);
        d.setCursor(x + 2 * s + 1, y - lh / 2);
        d.print(t);
      }
    }
    char t[40];
    R.summary(t, sizeof(t));
    d.drawTextCentered(W / 2, bar_y, t);
  }

  // Opens the path/relay detail popup for the entry stashed by buildFsMenu().
  // Incoming: hops listed oldest-hop-first (the order they were appended as the
  // message travelled). Outgoing channel: each distinct repeater heard echoing
  // the send, order not meaningful (they're independent, not a chain).
  void showPathDetail() {
    if (_fs_menu_pos < 0) return;
    uint8_t path_len_packed;
    const uint8_t* path;
    bool path_is_relay = false;
    if (_fs_menu_is_channel) {
      ChHistEntry& e = _history.chAtPos(_fs_menu_pos);
      path_len_packed = e.path_len; path = e.path; path_is_relay = (e.relay_seq != 0);
    } else {
      DmHistEntry& e = _history.dmAtPos(_fs_menu_pos);
      path_len_packed = e.path_len; path = e.path;
    }
    uint8_t hash_size = (path_len_packed >> 6) + 1;
    uint8_t hop_count = path_len_packed & 63;
    if (hop_count == 0) return;
    uint8_t max_hops = MAX_HIST_PATH_BYTES / hash_size;   // matches capturePath()/markChannelRelayed()'s write-side bound
    if (hop_count > max_hops) hop_count = max_hops;
    for (uint8_t i = 0; i < hop_count; i++) {
      contactctl::hopName(path_len_packed, path, i, _path_detail_names[i], sizeof(_path_detail_names[i]));
    }
    _ctx_menu.begin(path_is_relay ? "Relayed by" : "Path", hop_count);
    for (uint8_t i = 0; i < hop_count; i++) _ctx_menu.addItem(_path_detail_names[i]);
    _showing_path_detail = true;
  }

  // Dispatch the selected fullscreen-options row. `channel` picks which
  // fullscreen view to close when starting a reply.
  // The message menu open (a DM's or a channel post's): its actions, or the
  // path detail it shows closed by any key.
  bool msgMenuInput(char c, bool channel) {
    auto res = _ctx_menu.handleInput(c);
    if (_showing_path_detail) {
      if (res != PopupMenu::NONE) { _ctx_menu.active = false; _showing_path_detail = false; }
    } else if (res == PopupMenu::SELECTED) {
      dispatchFsAction(channel);
    } else if (res != PopupMenu::NONE) {
      _ctx_menu.active = false;
    }
    return true;
  }

  void dispatchFsAction(bool channel) {
    int csel = _ctx_menu.selectedIndex();
    FsAct a = (FsAct)_fs_act[(csel >= 0 && csel < _fs_act_n) ? csel : 0];
    _ctx_menu.active = false;
    if (a == FS_REPLY) {
      (channel ? _fs : _dm_fs).active = false;
      startReply(channel);
    } else if (a == FS_NAV) {
      _nav_active = true;            // keep the message view active underneath
      _nav_eta.reset();
    } else if (a == FS_SAVE) {
      saveSharedWaypoint();
    } else if (a == FS_TARGET) {
      // Kind 0 (a place): the coordinates were snapshotted out of the message
      // text, so there is no contact to keep re-resolving them from.
      _task->setTargetNow(0, nullptr, _nav_lat, _nav_lon,
                          _nav_label[0] ? _nav_label : "Msg loc");
    } else if (a == FS_PATH) {
      showPathDetail();
    } else if (a == FS_ROUTE) {
      showRoute();
    }
  }

  // Save the location parsed from the open message as a waypoint. Uses the
  // [WAY] label when present, else auto-names it like a manually-marked point.
  void saveSharedWaypoint() {
    char label[WAYPOINT_LABEL_LEN];
    if (_nav_label[0]) {
      textfit::copy(label, sizeof(label), _nav_label);
    } else {
      snprintf(label, sizeof(label), "WP%d", _task->waypoints().count() + 1);
    }
    _task->addWaypoint(_nav_lat, _nav_lon, label);
  }

  void renderNav(DisplayDriver& display) {
    int32_t mylat, mylon; bool have = _task->currentLocation(mylat, mylon);
    int cog; bool cogv = _task->currentCourse(cog);
    navview::draw(display, have, mylat, mylon, _nav_lat, _nav_lon,
                  _nav_label[0] ? _nav_label : "Msg loc", cogv, cog, _task->useImperial(), &_nav_eta);
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
  // relay_count (channel sends only): when > 0, replaces the ✓ with the
  // distinct-repeater echo count as tiny digit icons -- the count alone
  // already says "confirmed", so showing the checkmark too is redundant; a
  // DM's ✓ (no such count) leaves it 0 and keeps the plain checkmark.
  static void drawAckGlyph(DisplayDriver& d, int x, int top_y, AckState s, int sends = 1, int relay_count = 0) {
    switch (s) {
      case ACK_PENDING: miniIconDotRow(d, x, top_y, sends);          break;
      case ACK_OK:
        if (relay_count > 0) miniIconDrawNumber(d, x, top_y, relay_count);
        else                 miniIconDraw(d, x, top_y, ICON_CHECK);
        break;
      case ACK_FAIL:    miniIconDraw(d, x, top_y, ICON_CROSS);       break;
      default: break;                          // ACK_NONE → nothing
    }
  }

  // Selection frame for one history message box, shared by the DM/room and
  // channel history lists. Selected = solid fill, dark text (the same
  // drawSelectionRow() convention every other list in the UI uses). Unselected
  // = plain black box (like the rest of the UI) with a thin light frame
  // separating it from its neighbours -- no filled header strip, which used
  // to leave a patch of light background on every bubble whether selected or
  // not, unlike anywhere else in the UI. Leaves the ink colour set correctly
  // for the sender row drawn next (and everything after it, until something
  // else changes it): LIGHT for the whole unselected box, DARK for selected.
  // Spans exactly [box_x, box_x+box_w) — the caller sizes/positions the bubble
  // (see computeBubbleBox below), this just draws whatever box it's given.
  static void drawHistRowFrame(DisplayDriver& d, int box_x, int box_w, int y, int bh, int lh, bool sel) {
    d.setColor(UIColor::primary_txt);
    if (sel) {
      d.fillSoftRect(box_x, y, box_w, bh);
      d.setColor(UIColor::window_bkg);
    } else {
      d.drawSoftRect(box_x, y, box_w, bh);
    }
  }

  // Width of an ack/delivery glyph (see drawAckGlyph) — needed up front to size
  // an outgoing bubble before it's drawn.
  static int ackGlyphWidth(DisplayDriver& d, AckState s, int sends, int relay_count = 0) {
    const int sc = miniIconScale(d);
    switch (s) {
      case ACK_PENDING: return miniIconDotRowWidth(d, sends);
      case ACK_OK:      return relay_count > 0 ? miniIconNumberWidth(d, relay_count) : ICON_CHECK.w * sc;
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
  //
  // A bubble never fills the whole row: max_w leaves a gutter on the side the
  // sender's bubble is anchored away from (see bubbleMaxW), so even a long
  // message still shows which side it came from.
  struct BubbleBox { int x, w; };
  static int bubbleMaxW(DisplayDriver& d, int full_avail) {
    int gutter = d.getCharWidth() * 2;
    if (gutter < 8) gutter = 8;
    int w = full_avail - gutter;
    return w < 1 ? 1 : w;
  }
  // Whether the history shows each message whole (wrapped, a box as tall as
  // it needs) rather than as compact fixed boxes: on a screen of nine lines
  // or more (the e-ink panels either way up), not on a 7-line OLED.
  static bool expandHistory(DisplayDriver& d) { return d.height() / d.getLineHeight() >= 9; }
  // An expanded box's height: its header line, then `body` wrapped at the
  // width it's drawn in -- the bubble's own, inside `full_avail`.
  static int expandedBoxH(DisplayDriver& d, const char* body, int full_avail) {
    d.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
    int nl = FullscreenMsgView::wrapLines(d, s_wrap_trans, bubbleMaxW(d, full_avail) - 6, s_wrap_lines, 8);
    return (1 + (nl > 0 ? nl : 1)) * d.getLineHeight() + 3;   // 2 px under the descenders
  }
  static BubbleBox computeBubbleBox(int full_avail, int max_w, bool outgoing, int header_w, int body_w) {
    int w = header_w > body_w ? header_w : body_w;
    if (w > max_w) w = max_w;
    if (w < 1) w = 1;
    return { outgoing ? (full_avail - w) : 0, w };
  }

  // The marker in a bubble's header, after the sender: a delivery glyph (an
  // outgoing DM, or a channel post a repeater echoed), or the hop count an
  // incoming message took; none if neither applies.
  struct BubbleMark {
    enum Kind : uint8_t { NONE, ACK, HOPS } kind = NONE;
    AckState status = ACK_OK;
    int sends = 1, relay = 0, hops = 0;
    static BubbleMark ack(AckState s, int sends, int relay) { BubbleMark m; m.kind = ACK; m.status = s; m.sends = sends; m.relay = relay; return m; }
    static BubbleMark hop(int n) { BubbleMark m; if (n > 0) { m.kind = HOPS; m.hops = n; } return m; }
    int width(DisplayDriver& d) const {
      return kind == ACK ? 3 + ackGlyphWidth(d, status, sends, relay)
           : kind == HOPS ? 3 + miniIconNumberWidth(d, hops) : 0;
    }
  };

  // One history row as a bubble with a header (sender, marker, age) over its
  // body, sized to its own content -- shared by the DM/room and channel lists.
  // `expand` wraps the body over several lines; otherwise one line that the
  // selected row marquees. `mq_delay` takes the marquee's redraw request.
  void drawHeadedBubble(DisplayDriver& display, int y, int bh, int lh, bool sel, int reserve, bool outgoing,
                        const char* sender, const char* body, const char* age, const BubbleMark& mark,
                        bool expand, int& mq_delay) {
    const int age_w = age[0] ? display.getTextWidth(age) + 3 : 0;
    // Size the bubble to its own content before drawing anything (see
    // computeBubbleBox): the header (sender+marker+age) vs the body, measured
    // once here and reused below instead of re-wrapping.
    const int full_avail = display.width() - reserve;
    const int max_w = bubbleMaxW(display, full_avail);
    const int ack_w = mark.width(display);
    // A little air between a marker and the age, so "12" + "1s" doesn't read as "121s".
    const int mk_gap = (ack_w > 0 && age_w > 0) ? 2 : 0;
    // The marker after the name: a character's gap beside the larger icons,
    // whose digits stand as tall as the text and would read as part of it.
    const int nm_gap = MINI_ICONS_LARGE ? display.getCharWidth() : 3;
    const int header_w = 3 + (sender[0] ? display.getTextWidth(sender) + (ack_w ? nm_gap - 3 : 0) : -3) + ack_w + mk_gap + age_w + 3;
    int body_w, nl = 0;
    if (expand) {
      display.translateUTF8ToBlocks(s_wrap_trans, body, sizeof(s_wrap_trans));
      nl = FullscreenMsgView::wrapLines(display, s_wrap_trans, max_w - 6, s_wrap_lines, 8);
      body_w = 0;
      for (int li = 0; li < nl; li++) { int w = display.getTextWidth(s_wrap_lines[li]); if (w > body_w) body_w = w; }
      body_w += 6;
    } else {
      int raw_w = display.getTextWidth(body);
      body_w = (raw_w > max_w - 6 ? max_w - 6 : raw_w) + 6;
    }
    const BubbleBox box = computeBubbleBox(full_avail, max_w, outgoing, header_w, body_w);

    drawHistRowFrame(display, box.x, box.w, y, bh, lh, sel);
    // Only the body marquees, not the sender too: both share the single
    // marquee slot on DisplayDriver, and if two texts in the same row both
    // qualified they'd keep resetting each other's animation every frame.
    // The name gives way to the marker and the age (a long name is
    // ellipsized), and the marker sits right after whatever width the name
    // actually got -- not after its full width, which would run it into the age.
    int name_avail = box.w - 6 - age_w - ack_w - mk_gap - (ack_w ? nm_gap - 3 : 0);
    if (name_avail < display.getCharWidth()) name_avail = display.getCharWidth();
    int name_w = sender[0] ? display.getTextWidth(sender) : 0;
    if (name_w > name_avail) name_w = name_avail;
    if (sender[0]) display.drawTextEllipsized(box.x + 3, y + 1, name_avail, sender);
    int gx = box.x + 3 + (name_w ? name_w + nm_gap : 0);
    if (mark.kind == BubbleMark::ACK)       drawAckGlyph(display, gx, y + 1, mark.status, mark.sends, mark.relay);
    else if (mark.kind == BubbleMark::HOPS) miniIconDrawNumber(display, gx, y + 1, mark.hops);
    if (age[0]) { display.setCursor(box.x + box.w - age_w, y + 1); display.print(age); }
    // Ink is already LIGHT (unselected) or DARK (selected) from
    // drawHistRowFrame above, and nothing since has changed it.
    if (expand) {
      for (int li = 0; li < nl; li++) { display.setCursor(box.x + 3, y + (li + 1) * lh + 1); display.print(s_wrap_lines[li]); }
    } else {
      // Suppress this row's own marquee while _ctx_menu (Path/Relayed by,
      // etc.) sits on top of it -- both would otherwise fight over
      // DisplayDriver's single shared marquee slot every frame (this row
      // redraws every frame regardless of the popup), each is_new-resetting
      // the other's animation and producing a stuck-then-jumpy scroll.
      const bool body_marquee = sel && !_ctx_menu.active;
      const int r_body = display.drawTextEllipsized(box.x + 3, y + lh + 1, box.w - 6, body, body_marquee);
      if (body_marquee && r_body > 0) mq_delay = r_body;
    }
  }

  // Bottom-right "+ Send" compose button, shared by both history lists:
  // bordered when idle, inverted (filled) when selected. Right-aligned to
  // match the outgoing ("Me") bubbles anchored on that side, so composing
  // sits under your own messages rather than the other side's. Box is exactly
  // lh tall (no added border padding) — the text has no descenders ("+
  // Send"), so padding sized against the font's full ascent+descent cell
  // read as a lopsided 1px-top/3px-bottom gap instead of an even border;
  // tight framing avoids that and gives the history list back the 2px. Always
  // reset to LIGHT first so it stays visible regardless of the ink the
  // message loop left.
  static void drawComposeButton(DisplayDriver& d, int cby, int lh, bool sel) {
    (void)lh;
    const char* ctxt = "+ Send";
    drawButton(d, d.width() - buttonWidth(d, ctxt), cby, ctxt, sel);
  }

  // A failed send leaves the screen as it was -- the keyboard with the text
  // still in it, or the quick-message list -- so it can be sent again.
  // Alerts don't show over the keyboard, so there its field label says it.
  void afterSend(bool ok) {
    if (!ok) {
      if (_phase == KEYBOARD) _kb->prompt = "Not sent";
      else _task->showAlert("Send failed", 1500);
      return;
    }
    _reply_mode = false;
    _share_mode = false;
    dropOlder();                     // a reply from an older page: back to the newest
    if (_sending_to_channel) {
      _hist_sel = 0;
      _hist_scroll = 0;
      // Filed at index 0 by sendText(): the unread index range is stale, and
      // the user is active in this channel -- fully read.
      _unread_at_entry = 0;
    } else {
      _dm_hist_sel = 0;
      _dm_hist_scroll = 0;
      _phase = DM_HIST;
    }
  }

  // Sent through the Core, which files the message as L2's are: a DM with its
  // ACK tracking and resends, a channel post as "Me: ..." with the relay
  // marker armed and the channel read.
  bool sendText(const char* msg) {
    if (!_sending_to_channel) return _task->core().sendDirectText(_sel_contact, msg);
    Phase prev = _phase;
    _phase = CHANNEL_HIST;   // before filing, so the Core sees it viewed: no unread bump
    bool ok = _task->core().sendChannelText(_sel_channel_idx, msg);
    if (!ok) _phase = prev;
    return ok;
  }

  // Fill _pin_slot_labels for the "Pick slot" submenu: each dial slot with its
  // current occupant, contact or channel.
  void buildSlotLabels() {
    NodePrefs* p = _task->getNodePrefs();
    for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) {
      if (!p || _task->isFavouriteSlotEmpty(s)) {
        snprintf(_pin_slot_labels[s], sizeof(_pin_slot_labels[s]), "Slot %d: empty", s + 1);
        continue;
      }
      char nm[16] = "?";
      const uint8_t* pfx = p->favourite_contacts[s];
      if (_task->favouriteSlotKind(s) == NodePrefs::FAV_KIND_CHANNEL) {
        ChannelDetails ch;
        if (the_mesh.getChannel(pfx[0], ch) && ch.name[0]) {
          nm[0] = '#';
          DisplayDriver::translateUTF8Static(nm + 1, chanctl::bareName(ch.name), sizeof(nm) - 1);
        }
      } else {
        for (int idx = MAX_ANON_CONTACTS; ; idx++) {
          ContactInfo c2;
          if (!the_mesh.getContactByIdx(idx, c2)) break;
          if (memcmp(c2.id.pub_key, pfx, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
            DisplayDriver::translateUTF8Static(nm, c2.name, sizeof(nm));
            break;
          }
        }
      }
      textfit::fmt(_pin_slot_labels[s], sizeof(_pin_slot_labels[s]), "Slot %d: %s", s + 1, nm);
    }
  }

  void pinContactToSlot(const ContactInfo& ci, int slot) {
    if (slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return;
    favslots::pinContact(_task->getNodePrefs(), slot, ci.id.pub_key);
    the_mesh.savePrefs();
    char alert[24];
    snprintf(alert, sizeof(alert), "Pinned to slot %d", slot + 1);
    _task->showAlert(alert, 800);
  }

  // Pin/Unpin row picked in a contact or room menu: unpins if already pinned,
  // otherwise hands the menu over to the slot picker.
  void pinContactAction(const ContactInfo& ci) {
    int pinned_slot = _task->findFavouriteSlot(ci.id.pub_key);
    if (pinned_slot >= 0) {
      _task->clearFavouriteSlot(pinned_slot);
      the_mesh.savePrefs();
      char alert[24];
      snprintf(alert, sizeof(alert), "Unpinned (slot %d)", pinned_slot + 1);
      _task->showAlert(alert, 800);
      return;
    }
    buildSlotLabels();
    _ctx_menu.begin("Pick slot", 3);
    for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) _ctx_menu.addItem(_pin_slot_labels[s]);
    _pin_slot_ch_idx = -1;   // pinning a contact, not a channel
    _pin_picker_active = true;
  }

  void pinChannelToSlot(uint8_t ch_idx, int slot) {
    if (slot < 0 || slot >= NodePrefs::FAVOURITES_COUNT) return;
    favslots::pinChannel(_task->getNodePrefs(), slot, ch_idx);
    the_mesh.savePrefs();
    char alert[24];
    snprintf(alert, sizeof(alert), "Pinned to slot %d", slot + 1);
    _task->showAlert(alert, 800);
  }

  // Posting to a room requires a login handshake first (even with a blank
  // password), so get one under way instead of opening a history view that
  // would silently fail to send. Targets _sel_contact.
  void requireRoomLogin() {
    // Logged in to this room before, on an earlier boot: the Core retries with
    // the remembered password instead of prompting again.
    RoomSessions::Open o = _task->core().rooms.open(_sel_contact);
    if (o != RoomSessions::NEED_PASSWORD) {
      if (o != RoomSessions::OPEN_NOW) loginAlert(o == RoomSessions::LOGGING_IN);
      return;
    }
    _login_mode = true;
    _kb->begin("", 15); // room/repeater password: max 15 chars
    _kb->prompt = "Password";
    _kb->clearPlaceholders();   // {loc}/{time} are for messages, not a password
    _phase = KEYBOARD;
  }

  // Flip a contact's/room's favourite flag and refresh the menu label. The list
  // rebuild is deferred to menu close (see the channel Fav toggle for why).
  void toggleContactFav(const ContactInfo& ci) {
    bool now_fav = !contactctl::favourite(ci);
    if (!contactctl::setFavourite(ci.id.pub_key, now_fav)) return;
    snprintf(_ctx_fav_item, sizeof(_ctx_fav_item), now_fav ? "Fav: ON" : "Fav: OFF");
  }

  void buildContactList() {
    NodePrefs* p = _task->getNodePrefs();
    ContactInfo c;
    int total = the_mesh.getNumContacts();
    const bool rooms = _room_mode;
    _num_contacts = 0;
    // Sort key per row: favourites first, then most-messaged first (DMs only).
    // uint8_t, not int: DM history is capped at MessageHistory::DM_HIST_MAX (32),
    // well under the 128 the favourite bit adds, and this array is 1400 B at this
    // build's MAX_CONTACTS=350 as an int[] -- a sizeable slice of the 4 KB loop()
    // task stack for one local array.
    uint8_t keys[MAX_CONTACTS];
    bool fav_only = rooms ? (p && p->room_fav_only) : !(p && p->dm_show_all);
    // getContactByIdx() indexes the RAW contacts[] table, whose first
    // MAX_ANON_CONTACTS slots are reserved for anon requests (see
    // BaseChatMesh::resetContacts()/ContactsIterator); getNumContacts()
    // already excludes them from the count, so the real contacts start at
    // MAX_ANON_CONTACTS, not 0 -- same offset NearbyScreen.h's own contact
    // scan already applies. Without it, this loop only ever read the
    // reserved anon slots (empty name, type 0) for the first
    // MAX_ANON_CONTACTS iterations and, for any total at or under that count,
    // NEVER reached a real contact at all -- e.g. a device with exactly one
    // known contact would show "No favourites" with an empty list forever,
    // regardless of the dm_show_all/fav_only setting or that contact's own
    // favourite flag.
    for (int i = 0; i < total; i++) {
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
      if (c.type != (rooms ? ADV_TYPE_ROOM : ADV_TYPE_CHAT)) continue;
      bool fav = contactctl::favourite(c);
      if (fav_only && !fav) continue;
      uint8_t k = rooms ? 0 : _history.dmHistCountForContact(c.id.pub_key);
      if (k > 127) k = 127;
      if (fav && (!p || p->fav_sort)) k += 128;
      keys[_num_contacts] = k;
      // Store the RAW table index (not the bare loop counter) -- every other
      // call site in this file (render()'s mesh_idx, toggleFav(), the
      // context-menu handlers, ...) reads _sorted[...] straight into
      // getContactByIdx() with no offset of its own.
      _sorted[_num_contacts++] = MAX_ANON_CONTACTS + i;
    }
    // Descending insertion sort; rows with key 0 keep their contact-table order.
    for (int i = 1; i < _num_contacts; i++) {
      if (keys[i] == 0) continue;
      uint16_t idx = _sorted[i]; uint8_t k = keys[i];
      int j = i;
      while (j > 0 && keys[j-1] < k) {
        _sorted[j] = _sorted[j-1]; keys[j] = keys[j-1]; j--;
      }
      _sorted[j] = idx; keys[j] = k;
    }
  }

  void buildChannelList() {
    NodePrefs* p = _task->getNodePrefs();
    _num_channels = chanctl::order(p, _history, _channel_indices, p && p->ch_fav_only);
  }

  // Into the channel list, sorted afresh (a "Newest" order follows the last
  // message), the cursor on channel `keep_ch` (a slot) wherever it now is.
  void enterChannelList(int keep_ch) {
    buildChannelList();
    for (int i = 0; i < _num_channels; i++)
      if (_channel_indices[i] == keep_ch) { _channel_sel = i; break; }
    if (_channel_sel > _num_channels) _channel_sel = _num_channels;   // the Add row at most
    _phase = CHANNEL_PICK;
  }

  // Advance one of the contact menu's value rows. dir is +1 for RIGHT and for
  // Enter (which PopupMenu reports as VALUE_NEXT on a value row), -1 for LEFT.
  void cycleContactCtxValue(int sel, int dir) {
    ContactInfo ci;
    if (_num_contacts <= 0 || !the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) return;
    if (sel == 1) {
      uint8_t v = dmNotifState(ci.id.pub_key);
      v = (dir > 0) ? (v + 1) % 3 : (v + 2) % 3;
      setDmNotifState(ci.id.pub_key, v);
      snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s", NOTIF_LABELS[v]);
      _ctx_dirty = true;
    } else if (sel == 2) {
      uint8_t v = dmMelodySlot(ci.id.pub_key);
      v = (dir > 0) ? (v + 1) % 3 : (v + 2) % 3;
      setDmMelody(ci.id.pub_key, v);
      snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s", MELODY_LABELS[v]);
      _ctx_dirty = true;
    } else if (sel == _ctx_fav_idx) {
      toggleContactFav(ci);
    }
  }

  // Same, for the room menu -- its only value row is Fav.
  void cycleRoomCtxValue(int sel) {
    ContactInfo ci;
    if (sel != _ctx_fav_idx || _num_contacts <= 0) return;
    if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) toggleContactFav(ci);
  }

  // Same, for the channel menu. The list rebuild is deferred to menu close: with
  // the fav-only filter on, un-favouriting removes this channel from the list,
  // and rebuilding under the open menu would shift _channel_sel onto another one.
  void cycleChannelCtxValue(int sel, int dir) {
    if (_num_channels <= 0) return;
    uint8_t ch_idx = _ctx_ch_idx;   // frozen at menu open -- see declaration
    if (sel == 1) {
      uint8_t v = chNotifState(ch_idx);
      v = (dir > 0) ? (v + 1) % 3 : (v + 2) % 3;
      setChNotifState(ch_idx, v);
      snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s", NOTIF_LABELS[v]);
      _ctx_dirty = true;
    } else if (sel == 2) {
      uint8_t v = chNotifMelody(ch_idx);
      v = (dir > 0) ? (v + 1) % 3 : (v + 2) % 3;
      setChNotifMelody(ch_idx, v);
      snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s", MELODY_LABELS[v]);
      _ctx_dirty = true;
    } else if (sel == _ctx_fav_idx) {
      NodePrefs* p2 = _task->getNodePrefs();
      if (p2) {
        p2->ch_fav_bitmask ^= (1ULL << ch_idx);
        bool is_fav = (p2->ch_fav_bitmask & (1ULL << ch_idx));
        snprintf(_ctx_fav_item, sizeof(_ctx_fav_item), is_fav ? "Fav: ON" : "Fav: OFF");
        _ctx_dirty = true;
      }
    } else if (sel == _ctx_save_idx) {
      bool on = !chanctl::saved(_task->getNodePrefs(), ch_idx);
      chanctl::setSaved(_task->getNodePrefs(), ch_idx, on);
      snprintf(_ctx_save_item, sizeof(_ctx_save_item), on ? "Save: ON" : "Save: OFF");
      _ctx_dirty = true;
    }
  }

  bool chIsFav(uint8_t ch_idx) const {
    NodePrefs* p = _task->getNodePrefs();
    return p && (p->ch_fav_bitmask & (1ULL << ch_idx)) != 0;
  }

  // Channel notif (ui-core/ChannelControl.h): 0 = default, 1 = muted, 2 = force-on.
  uint8_t chNotifState(uint8_t ch_idx) const { return chanctl::notif(_task->getNodePrefs(), ch_idx); }
  void setChNotifState(uint8_t ch_idx, uint8_t state) { chanctl::setNotif(_task->getNodePrefs(), ch_idx, state); }

  // DM notif / melody overrides (ui-core/ContactControl.h).
  uint8_t dmNotifState(const uint8_t* pub_key) const { return contactctl::notif(_task->getNodePrefs(), pub_key); }
  void setDmNotifState(const uint8_t* pub_key, uint8_t state) { contactctl::setNotif(_task->getNodePrefs(), pub_key, state); }

  // Channel melody (ui-core/ChannelControl.h): 0 = global, 1 / 2 = melody 1 / 2.
  uint8_t chNotifMelody(uint8_t ch_idx) const { return chanctl::melody(_task->getNodePrefs(), ch_idx); }
  void setChNotifMelody(uint8_t ch_idx, uint8_t slot) { chanctl::setMelody(_task->getNodePrefs(), ch_idx, slot); }

  uint8_t dmMelodySlot(const uint8_t* pub_key) const { return contactctl::melody(_task->getNodePrefs(), pub_key); }
  void setDmMelody(const uint8_t* pub_key, uint8_t slot) { contactctl::setMelody(_task->getNodePrefs(), pub_key, slot); }

  // On-device channel Add/Edit form (Channels tab) — owned by this screen and
  // delegated to while active(), the same relationship WaypointsView has with
  // TrailScreen.
  ChannelsView _ch_view;
  bool _ch_view_pending_rebuild = false; // tracks redraw after channel create/delete

public:
#ifdef SIM_PLATFORM
  // Sim tests: channel 0's keyboard holding `text`.
  void simCompose(const char* text) {
    _sel_channel_idx = 0;
    snprintf(_share_text, sizeof(_share_text), "%s", text);
    beginShareCompose(true);
  }
#endif

  MessagesScreen(UITask* task, KeyboardWidget* kb)
    : _task(task), _phase(MODE_SELECT), _mode_sel(0),
      _contact_sel(0), _contact_scroll(0), _num_contacts(0), _room_mode(false), _login_mode(false),
      _channel_sel(0), _channel_scroll(0), _num_channels(0),
      _sel_channel_idx(0), _sending_to_channel(false),
      _msg_sel(0), _msg_scroll(0), _active_msg_count(0),
      _hist_sel(0), _hist_scroll(0),
      _unread_at_entry(0),
      _kb(kb),
      _ctx_dirty(false), _pin_picker_active(false), _direct_entry(false), _reply_mode(false),
      _history(task->core().history),
      _dm_hist_sel(-1), _dm_hist_scroll(0),
      _ch_view(task) {
    // The history rings + per-channel unread counters live in UiCore.
  }

  // Forwarded to UITask's GPS duty-cycle hold — true while showing the
  // bearing/distance view to a location shared in a message.
  bool navActive() const { return _nav_active; }

  // First free channel slot (existing config or blank name), or -1 if full.
  int findFreeChannelSlot() const { return chanctl::freeSlot(); }

  // CHANNEL_PICK row count including the synthetic "+ Add channel" row
  // (suppressed while picking a channel for the bot).
  int channelPickTotal() const { return _num_channels + (_pick_bot_channel ? 0 : 1); }

  // View state the UI Core consults while filing a message (via UiCoreHost):
  // a conversation open on screen doesn't count the message unread, and after
  // the insert the selection is kept on the same message. Ring entries are
  // numbered newest-first (0 == newest), so a new insert shifts every older
  // message's index up by one; if the user has scrolled up to an older message
  // (sel > 0), re-point the selection at that same message instead of silently
  // relabeling a different one in under them. At sel <= 0 (already at newest,
  // or -1 == compose button focused) there's nothing to preserve.
  bool isViewingChannel(uint8_t ch_idx) const {
    return _phase == CHANNEL_HIST && _sel_channel_idx == (int)ch_idx && !_older;
  }
  bool isViewingDM(const uint8_t* pub_key) const {
    return _phase == DM_HIST && memcmp(_sel_contact.id.pub_key, pub_key, 4) == 0 && !_older;
  }
  void onViewedHistoryGrew(bool channel) {
    if (channel) {
      if (_hist_sel > 0) { _hist_sel++; _hist_scroll++; }
      _unread_base++;
      if (_seen_hi >= 0) { _seen_lo++; _seen_hi++; }
    } else {
      if (_dm_hist_sel > 0) { _dm_hist_sel++; _dm_hist_scroll++; }
    }
    if (_new_idx >= 0) _new_idx++;
  }

  // Room logins (who is logged in this session, the saved passwords, the one
  // login in flight) are the UI Core's RoomSessions.
  bool isRoomLoggedIn(const uint8_t* pub_key) const { return _task->core().rooms.isLoggedIn(pub_key); }

  void loginAlert(bool sent) { _task->showAlert(sent ? "Logging in..." : "Login failed", sent ? 1000 : 1500); }

  void startRoomLogin(const char* password) { loginAlert(_task->core().rooms.login(_sel_contact, password)); }

  // Result of an on-device sendRoomLogin() (MyMesh::onContactResponse(), routed
  // via the UI Core and UITask::onRoomLoginResult()). Surfaces as a transient alert.
  void onRoomLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
    (void)permissions;
    if (success) {
      // Auto-enter the room's chat right after a successful login so the user
      // doesn't have to press Enter a second time. The login result is async,
      // so only do it if they're still sitting on this same room in the picker
      // (didn't navigate away, open a menu, or enter a share/pick sub-flow).
      if (_phase == CONTACT_PICK && _room_mode && !_ctx_menu.active
          && !_share_mode && !_pick_target && !_pick_bot_room
          && memcmp(_sel_contact.id.pub_key, pub_key, 4) == 0) {
        openDmHistory();
      }
    }
    _task->showAlert(success ? "Login OK" : "Login failed", 1200);
  }

  // Open the message history for the currently selected contact/room and reset
  // the scroll/selection view state. Shared by the Enter-on-contact path and the
  // post-login auto-enter.
  // The conversation being shown: the ring's entries for it, or a page of
  // older ones from the flash log. A list of a few either way, as a render
  // measures all of it. `_older` is that page.
  int chViewCount() const {
    return _older ? olderCount() : _history.histCountForChannel(_sel_channel_idx);
  }
  int chViewPos(int j) const {
    if (_older) return j < olderCount() ? MessageHistory::CH_HIST_MAX + j : -1;
    return _history.histEntryForChannel(_sel_channel_idx, j);
  }
  int dmViewCount() const {
    return _older ? olderCount() : _history.dmHistCountForContact(_sel_contact.id.pub_key);
  }
  int dmViewPos(int j) const {
    if (_older) return j < olderCount() ? MessageHistory::DM_HIST_MAX + j : -1;
    return _history.dmHistEntryForContact(_sel_contact.id.pub_key, j);
  }
#ifdef HIST_FLASH
  int  olderCount() const { return _task->core().history_log.pageCount(); }
  void dropOlder() {
    if (_older) _task->core().history_log.pageEnd();
    _older = false;
  }
  // Past the oldest entry: the next page back. True when that's handled
  // (at the very end nothing moves); false with nothing older, as a plain list.
  bool turnOlder(bool channel) {
    HistoryLog& log = _task->core().history_log;
    if (_older) {
      if (!log.pageOlder()) return true;
    } else {
      if (!(channel ? log.pageBeginCh(_sel_channel_idx) : log.pageBeginDm(_sel_contact.id.pub_key))) return false;
      _older = true;
      _new_idx = -1;
      _anchor_new = false;
    }
    (channel ? _hist_sel : _dm_hist_sel) = 0;
    (channel ? _hist_scroll : _dm_hist_scroll) = 0;
    return true;
  }
  // Past the newest of a page: the next one forward, then the ring again.
  bool turnNewer(bool channel) {
    if (!_older) return false;
    int count;
    if (_task->core().history_log.pageNewer()) {
      count = olderCount();
    } else {
      bool shown = (channel ? _fs : _dm_fs).active;
      leaveOlder(channel);
      (channel ? _fs : _dm_fs).active = shown;
      _new_idx = -1;
      count = channel ? chViewCount() : dmViewCount();
    }
    (channel ? _hist_sel : _dm_hist_sel) = count - 1;
    (channel ? _hist_scroll : _dm_hist_scroll) = count > _hist_visible ? count - _hist_visible : 0;
    return true;
  }
#else
  int  olderCount() const { return 0; }
  void dropOlder() {}
  bool turnOlder(bool) { return false; }
  bool turnNewer(bool) { return false; }
#endif
  void leaveOlder(bool channel) {
    dropOlder();
    if (channel) beginChannelView(_sel_channel_idx); else beginDmView();
  }

  void openDmHistory() {
    beginDmView();
    _phase = DM_HIST;
  }

  // Opening a conversation: on its first unread message ("New" above it), else
  // on the compose row under the newest.
  void beginDmView() {
    dropOlder();
    const uint8_t* key = _sel_contact.id.pub_key;
    _new_idx = _history.firstUnreadDm(key, _task->getDMUnread(key));
    _anchor_new = _new_idx >= 0;
    _task->clearDMUnread(key);
    _dm_hist_sel = _new_idx;
    _dm_hist_scroll = 0;
    _dm_fs.active = false;
  }
  void beginChannelView(uint8_t channel_idx) {
    dropOlder();
    _sel_channel_idx = channel_idx;
    _unread_at_entry = (int)_history.chUnread(channel_idx);
    _unread_base = 0;
    _seen_lo = 0; _seen_hi = -1;
    _new_idx = _history.firstUnreadChannel(channel_idx, _unread_at_entry);
    _anchor_new = _new_idx >= 0;
    _hist_scroll = 0;
    _hist_sel = _new_idx >= 0 ? _new_idx : _history.histCountForChannel(channel_idx) > 0 ? 0 : -1;
    _fs.active = false;
  }
  // The display came back on over a conversation: what was filed while it was
  // dark is unread -- read on from its first message, as on opening.
  void onDisplayOn() {
    if (_ctx_menu.active) return;
    if (_phase == CHANNEL_HIST && !_fs.active && _history.chUnread(_sel_channel_idx) > 0) beginChannelView(_sel_channel_idx);
    else if (_phase == DM_HIST && !_dm_fs.active && _task->getDMUnread(_sel_contact.id.pub_key) > 0) beginDmView();
  }

  // On the first unread (sel), opening: the scroll showing it highest with
  // "New" above it (stack(s) lays out from s, sets div_y when "New" fits).
  // A history list's boxes stacked upward from just above the compose row
  // (cby), so item s (the newest shown) sits at the bottom and older ones
  // higher, as in a messenger; "New" goes over item _new_idx, below the older
  // ones. ys/hs[i] are item (s + i)'s; boxH(item) its height. Returns how many
  // fit above `top`.
  template <class BoxH>
  int stackBoxes(int s, int count, int cby, int top, int lh, bool expand, int* ys, int* hs, int max,
                 int& div_y, BoxH boxH) const {
    const int gap = expand ? 2 : 1;
    int cur_y = cby - gap;   // the same gap against compose as between boxes
    int n = 0;
    div_y = -1;
    for (int ii = 0; ii < max && (s + ii) < count; ii++) {
      const int bh = boxH(s + ii);
      const int box_top = cur_y - bh;
      if (box_top < top) break;
      ys[n] = box_top; hs[n] = bh; n++;
      cur_y = box_top - gap;
      if (s + ii == _new_idx && _new_idx + 1 < count) {
        if (cur_y - lh < top) break;
        div_y = cur_y - lh;
        cur_y = div_y - gap;
      }
    }
    return n;
  }

  // Under a history list: the "New" divider, "No messages yet", the scrollbar
  // and the compose row, then the menu over it all. The scrollbar is inverted:
  // item 0 (newest) is at the bottom, so its thumb starts there and rises into
  // older history. The menu's own redraw delay (a marquee in it) is folded
  // into mq_delay.
  void drawHistFooter(DisplayDriver& display, const HistScroll& hs, int div_y, int count, int top, int cby,
                      int lh, bool compose_sel, int& mq_delay) {
    if (div_y >= 0) drawNewDivider(display, div_y, display.width() - hs.reserve, lh);
    if (count == 0) {
      display.setColor(UIColor::primary_txt);
      display.drawTextCentered(display.width()/2, display.height()/2, "No messages yet");
    }
    if (hs.need) {
      long inv_scroll_px = (hs.total_px - hs.view_px) - hs.scroll_px;
      if (inv_scroll_px < 0) inv_scroll_px = 0;
      drawScrollIndicatorPx(display, top + 1, hs.view_px, hs.total_px, hs.view_px, inv_scroll_px);
    }
    drawComposeButton(display, cby, lh, compose_sel);
    if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
  }

  template <class Stack> static int anchorScroll(Stack stack, const int& div_y, int sel, int count) {
    int pick = -1;
    for (int s = sel > 8 ? sel - 8 : 0; s <= sel; s++) {
      int n = stack(s);
      if (s + n - 1 < sel) continue;
      if (div_y >= 0 || sel + 1 >= count) return s;
      if (pick < 0) pick = s;
    }
    return pick >= 0 ? pick : sel;
  }

  // "New" between two lines, lh tall, over the first message unread on opening.
  static void drawNewDivider(DisplayDriver& d, int y, int w, int lh) {
    const char* t = "New";
    int tw = d.getTextWidth(t);
    int tx = (w - tw) / 2, mid = y + lh / 2;
    d.setColor(UIColor::primary_txt);
    d.fillRect(1, mid, tx - 4, d.sepH());
    d.fillRect(tx + tw + 3, mid, w - 1 - (tx + tw + 3), d.sepH());
    d.setCursor(tx, y + 1);
    d.print(t);
  }


  int getDMUnreadTotal() const {
    return _task->getDMUnreadTotal();
  }

  int getTotalChannelUnread() const { return _history.getTotalChannelUnread(); }
  bool anyChannelUnreadOverflow() const { return _history.anyChannelUnreadOverflow(); }

  // How many DM ring entries this contact/room currently holds -- lets UITask
  // clamp the Core's separate DM unread counters to what the shared 32-slot DM
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
    if (_sel_channel_idx < 0 || _sel_channel_idx >= MAX_GROUP_CHANNELS || _older) return;
    // histEntryForChannel is newest-first: index 0 = newest, higher = older;
    // the unread from opening are _unread_base onwards. Count everything
    // actually rendered on screen as seen — not just the highlighted row — so
    // a taller screen that fits more boxes at once marks more read up front,
    // instead of requiring a press per row. Reading starts at the first unread
    // and goes down to the newest, so what's been shown is one stretch.
    int lo = _hist_scroll, hi = _hist_scroll + _hist_visible - 1;
    if (_hist_sel >= 0 && _hist_sel < lo) lo = _hist_sel;
    if (_hist_sel > hi) hi = _hist_sel;
    if (_seen_hi < 0 || lo < _seen_lo) _seen_lo = lo;
    if (hi > _seen_hi) _seen_hi = hi;
    int a = _seen_lo > _unread_base ? _seen_lo : _unread_base;
    int b = _unread_base + _unread_at_entry - 1;
    if (_seen_hi < b) b = _seen_hi;
    int remaining = _unread_at_entry - (b >= a ? b - a + 1 : 0);
    _history.setChUnread(_sel_channel_idx, (uint8_t)(remaining > 0 ? remaining : 0));
  }

  void reset() {
    dropOlder();
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
    _task->core().hop_route.close();
    _share_mode = false;
    _pick_target = false;
    _pick_bot_channel = false;
    _pick_bot_room = false;
    _pin_picker_active = false;
    _pin_slot_ch_idx = -1;
    _scope_pick_active = false;
    _ch_delete_confirm_active = false;
    _pick_fav_slot = -1;
    _pick_sos_slot = -1;
    _direct_entry = false;
    _unread_at_entry = 0;
    _new_idx = -1;
    _anchor_new = false;
    _ch_view.reset();
    _ch_view_pending_rebuild = false;
  }

  // Recent DM contacts, newest first, deduped (forwarded to the history store).
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
    textfit::copy(_share_text, sizeof(_share_text), text);
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
    _task->gotoLiveShareScreen();
  }

  // Open the chooser to fill Favourites dial slot `slot`. Reuses this screen's
  // own Direct/Channels/Rooms browse rather than the dial carrying a second
  // picker of its own.
  void startPickFavourite(int slot) {
    reset();
    _pick_fav_slot = slot;
    _phase = MODE_SELECT;
  }

  void commitPickFavContact(const ContactInfo& ci) {
    pinContactToSlot(ci, _pick_fav_slot);
    _pick_fav_slot = -1;
    _room_mode = false;
    _task->gotoHomeScreen();
  }

  void commitPickFavChannel(int ch_idx) {
    pinChannelToSlot((uint8_t)ch_idx, _pick_fav_slot);
    _pick_fav_slot = -1;
    _task->gotoHomeScreen();
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
      NodePrefs::BotCh b = p->botCh(botcfg::s_ch_slot);   // the one the Bot screen shows
      *b.enabled = 1;
      *b.idx = (uint8_t)ch_idx;
      the_mesh.savePrefs();
    }
    _pick_bot_channel = false;
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
    _task->gotoBotScreen();
  }

  // Tools > SOS: a contact to call for help, straight to the Direct list.
  void startPickSos(int slot) {
    reset();
    _pick_sos_slot = slot;
    _contact_sel = _contact_scroll = 0;
    _phase = CONTACT_PICK;
  }

  void commitPickSos(const ContactInfo& ci) {
    NodePrefs* p = _task->getNodePrefs();
    if (p) memcpy(p->sos_dm[_pick_sos_slot], ci.id.pub_key, NodePrefs::FAVOURITE_PREFIX_LEN);   // saved on leaving SOS
    _pick_sos_slot = -1;
    _task->gotoSosScreen();
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
    _task->gotoLiveShareScreen();
  }

  void enterDM(const ContactInfo& ci) {
    _sel_contact = ci;
    beginDmView();
    _room_mode = false;
    _phase = DM_HIST;
    _direct_entry = true;
  }

  // Same as enterDM, but keeps room framing on (sender-prefixed lines, room
  // compose) and runs the login handshake a room needs before it can be posted
  // to. Caller must have already reset() the screen.
  void enterRoom(const ContactInfo& ci) {
    _sel_contact = ci;
    beginDmView();
    _room_mode = true;
    _phase = DM_HIST;
    _direct_entry = true;
    if (!isRoomLoggedIn(ci.id.pub_key)) requireRoomLogin();
  }

  // Open a channel's history directly (used by the Favourites dial). Caller
  // must have already reset() the screen.
  void enterChannel(uint8_t channel_idx) {
    beginChannelView(channel_idx);
    _phase = CHANNEL_HIST;
    _direct_entry = true;
  }

  uint8_t chUnread(uint8_t channel_idx) const { return _history.chUnread(channel_idx); }
  bool chUnreadOverflow(uint8_t channel_idx) const { return _history.chUnreadOverflow(channel_idx); }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);

    // Channel Add/Edit form owns the screen while active.
    if (_ch_view.active()) {
      _ch_view_pending_rebuild = true;
      return _ch_view.render(display);
    }

    // Rebuild channel list if one is pending, due to for example a channel create/delete
    if (_ch_view_pending_rebuild) {
      _ch_view_pending_rebuild = false;
      buildChannelList();
    }

    // Navigate-to-location view sits over everything else while active.
    if (_nav_active) { renderNav(display); return 1000; }
    if (_task->core().hop_route.active()) { renderRoute(display); return 5000; }

    switch (_phase) {
      case MODE_SELECT: return renderModeSelect(display);
      case CONTACT_PICK: return renderContactPick(display);
      case CHANNEL_PICK: return renderChannelPick(display);
      case DM_HIST: return renderDmHist(display);
      case CHANNEL_HIST: return renderChannelHist(display);
      case KEYBOARD: return _kb->render(display);
      case MSG_PICK: return renderMsgPick(display);
    }
    return 2000;
  }

  // One button: a message opened full screen is scrolled, then the next one.
  char stepKey() override {
    if (_ch_view.active() || _ctx_menu.active) return KEY_DOWN;
    if (_phase == DM_HIST && _dm_fs.active) return _dm_fs.stepKey();
    if (_phase == CHANNEL_HIST && _fs.active) return _fs.stepKey();
    return KEY_DOWN;
  }
  char stepBackKey() override {
    if (_ch_view.active() || _ctx_menu.active) return KEY_UP;
    if (_phase == DM_HIST && _dm_fs.active) return _dm_fs.stepBackKey();
    if (_phase == CHANNEL_HIST && _fs.active) return _fs.stepBackKey();
    return KEY_UP;
  }

  bool handleInput(char c) override {
    // Channel Add/Edit form consumes all input while active.
    if (_ch_view.active()) return _ch_view.handleInput(c);

    // Navigate view: Back returns to the message it was opened from.
    if (_nav_active) {
      if (c == KEY_CANCEL) _nav_active = false;
      return true;
    }
    // A message's route on the map: Back returns to the message.
    if (_task->core().hop_route.active()) {
      if (c == KEY_CANCEL) _task->core().hop_route.close();
      return true;
    }
    switch (_phase) {
      case MODE_SELECT: return inputModeSelect(c);
      case CONTACT_PICK: return inputContactPick(c);
      case CHANNEL_PICK: return inputChannelPick(c);
      case DM_HIST: return inputDmHist(c);
      case CHANNEL_HIST: return inputChannelHist(c);
      case KEYBOARD: return inputKeyboard(c);
      case MSG_PICK: return inputMsgPick(c);
    }
    return false;
  }

  // ── One method per phase: render, then input ──

  // A row's marquee asks for its next frame sooner than the screen's own cadence.
  static int marqueeDelay(int mq_delay, int cadence) { return (mq_delay > 0 && mq_delay < cadence) ? mq_delay : cadence; }

  int renderModeSelect(DisplayDriver& display);    // MessagesPick.h
  int renderContactPick(DisplayDriver& display);
  int renderChannelPick(DisplayDriver& display);
  int renderDmHist(DisplayDriver& display);        // MessagesThread.h
  int renderChannelHist(DisplayDriver& display);
  int renderMsgPick(DisplayDriver& display) {
    int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
    char title[24];
    if (_reply_mode) {
      int rlen = (int)strlen(_reply_prefix) - 4; // exclude "@[" and "] "
      if (rlen < 0) rlen = 0;
      if (rlen > 20) rlen = 20;
      char nick_raw[32], nick_trans[32];
      snprintf(nick_raw, sizeof(nick_raw), "%.*s", rlen, _reply_prefix + 2);
      display.translateUTF8ToBlocks(nick_trans, nick_raw, sizeof(nick_trans));
      snprintf(title, sizeof(title), "Re: %s", nick_trans);
    } else if (_sending_to_channel) {
      ChannelDetails ch;
      the_mesh.getChannel(_sel_channel_idx, ch);
      snprintf(title, sizeof(title), "%.23s", ch.name);
    } else {
      snprintf(title, sizeof(title), "To: %.14s", _sel_contact.name);
    }
    drawScreenHeader(display, title);

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
        int r = display.drawTextEllipsized(2, y, display.width() - 4 - reserve, tmpl, sel);
        if (sel && r > 0) mq_delay = r;
      }
    });
    return marqueeDelay(mq_delay, 2000);
  }

  bool inputModeSelect(char c);     // MessagesPick.h
  bool inputContactPick(char c);
  bool inputChannelPick(char c);
  bool inputDmHist(char c);         // MessagesThread.h
  bool inputChannelHist(char c);
  bool inputKeyboard(char c) {
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
        char expanded[2 * KB_MAX_LEN + 1];
        if (prefix_len > 0) {
          memcpy(expanded, _kb->buf, prefix_len);
          expandMsg(_kb->buf + prefix_len, expanded + prefix_len, sizeof(expanded) - prefix_len);
        } else {
          expandMsg(_kb->buf, expanded, sizeof(expanded));
        }
        // Too long for what it adds (the keyboard's "N over" says by how much): it
        // would be cut short, or refused, so it stays on the keyboard.
        if ((int)strlen(expanded) > msgtext::limit(_sending_to_channel, _task->getNodePrefs())) return true;
        bool ok = sendText(expanded);
        afterSend(ok);
      }
    }
    return true;
  }

  bool inputMsgPick(char c) {
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
        setComposePrompt();
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
        textfit::fmt(msg, sizeof(msg), "%s%s", _reply_prefix, body);   // too long: never half a letter sent
      } else {
        expandMsg(tmpl, msg, sizeof(msg));
      }
      bool ok = sendText(msg);
      afterSend(ok);
      return true;
    }
    return false;
  }

};

const char* const MessagesScreen::NOTIF_LABELS[3]  = { "Default", "Off", "On" };   // three states: text, not a switch
const char* const MessagesScreen::MELODY_LABELS[3] = { "Global", "M1", "M2" };

#include "MessagesPick.h"
#include "MessagesThread.h"
