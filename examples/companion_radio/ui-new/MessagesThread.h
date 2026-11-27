#pragma once
// MessagesScreen's open conversation: the DM and channel histories (stacked
// bubbles, newest at the bottom, the full-screen message view, the message
// menu) -- drawing and keys. Included at the end of MessagesScreen.h.

inline int MessagesScreen::renderDmHist(DisplayDriver& display) {
  int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
  const int lh = display.getLineHeight();
  display.setTextSize(1);
  char filtered_name[sizeof(_sel_contact.name)];
  display.translateUTF8ToBlocks(filtered_name, _sel_contact.name, sizeof(filtered_name));

  if (_dm_fs.active && _dm_hist_sel >= 0) {
    int ring_pos = dmViewPos(_dm_hist_sel);
    if (ring_pos >= 0) {
      const DmHistEntry& e = _history.dmAtPos(ring_pos);
      char sender_buf[33];
      // No skipReplyPrefix() here -- _dm_fs.render() parses "@[nick] " itself
      // (for the "To:" header); stripping it here first would hide it there.
      const char* body = dmDisplayParts(e, _sel_contact.type == ADV_TYPE_ROOM,
                                        filtered_name, sender_buf, sizeof(sender_buf));
      const char* sender = sender_buf;
      int dm_count = dmViewCount();
      int ret = _dm_fs.render(display, sender, body,
                              _dm_hist_sel < dm_count - 1,
                              _dm_hist_sel > 0);
      if (e.outgoing) {  // delivery marker in the (inverted) header bar
        display.setColor(UIColor::window_bkg);
        drawAckGlyph(display, 2 + display.getTextWidth(sender) + 3, 1,
                     _history.dmEffectiveStatus(e), e.attempt + 1);
        display.setColor(UIColor::primary_txt);
      }
      // Fold the popup's own suggested redraw delay into mq_delay -- otherwise
  // a marquee inside it (see PopupMenu::render()) only ever advances at
  // whatever slower cadence this phase's own return statement below picks,
  // since that return previously ignored this call's result entirely.
  if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
      return ret;
    }
    return 500;
  }

  int hist_start_y = display.headerH();
  int cby = display.height() - lh;   // compose button is now exactly lh tall, no added border padding

  char title[24];
  snprintf(title, sizeof(title), "%.23s", filtered_name);
  drawScreenHeader(display, title, -1, 0, true, _ctx_menu.active);

  int dm_count = dmViewCount();
  uint32_t now_ts = rtc_clock.getCurrentTime();
  bool is_room = (_sel_contact.type == ADV_TYPE_ROOM);

  // On a tall enough screen (expandHistory): boxes as tall as their whole
  // wrapped text. Otherwise compact 2-line boxes.
  const bool expand = expandHistory(display);
  // A DM bubble on a compact screen carries no sender line: one line of
  // text with the delivery / hop marker and the age at its end, growing to
  // two lines (marker and age ending the second) when the text is longer.
  // Rooms keep the author line above the text.
  const bool compact_dm = !is_room && !expand;
  const int MAX_VIS_BOXES = 8;
  int box_ys[MAX_VIS_BOXES], box_hs[MAX_VIS_BOXES], n_vis = 0;

  // The marker + age strip at a compact bubble's end, and its width.
  struct Tail { int ack_w, age_w, mk_gap, hops, w; char age[6]; };
  auto tailOf = [&](const DmHistEntry& e) {
    Tail t;
    geo::fmtAgeShort(t.age, sizeof(t.age), now_ts, e.timestamp ? e.timestamp : now_ts);
    t.age_w = t.age[0] ? display.getTextWidth(t.age) + 3 : 0;
    t.hops = !e.outgoing ? (e.path_len & 63) : 0;
    t.ack_w = e.outgoing ? (3 + ackGlyphWidth(display, _history.dmEffectiveStatus(e), e.attempt + 1))
            : (t.hops > 0 ? (3 + miniIconNumberWidth(display, t.hops)) : 0);
    t.mk_gap = (t.ack_w > 0 && t.age_w > 0) ? 4 : 0;
    t.w = t.ack_w + t.mk_gap + t.age_w + (t.age_w ? 0 : 3);
    return t;
  };
  auto bodyAt = [&](int idx, char* sb, size_t sbn) -> const char* {
    int rp = dmViewPos(idx);
    if (rp < 0) return nullptr;
    return skipReplyPrefix(dmDisplayParts(_history.dmAtPos(rp), is_room, filtered_name, sb, sbn));
  };
  // Height of compact bubble idx at a given scrollbar reserve.
  auto compactH = [&](int idx, int rsv) {
    int rp = dmViewPos(idx);
    if (rp < 0) return lh + 1;
    char sb[33];
    const char* body = bodyAt(idx, sb, sizeof(sb));
    int line_w = bubbleMaxW(display, display.width() - rsv) - 6;
    return dmSplit(display, body ? body : "", line_w, tailOf(_history.dmAtPos(rp)).w) < 0 ? lh + 1 : 2 * lh + 1;
  };

  // Fixed-track scrollbar metrics + a stable gutter reserve (decided by a
  // whole-list fit test, not last frame's visible count) so message boxes
  // don't reflow their width as messages arrive.
  auto scrollMetrics = [&]() {
    return compact_dm
      ? computeHistScrollH(display, dm_count, _dm_hist_scroll, hist_start_y, cby, 1, compactH)
      : computeHistScroll(display, expand, dm_count, _dm_hist_scroll,
      hist_start_y, cby, lh,
      [&](int idx) -> const char* {
        // Must match the per-item body extraction below (dmDisplayParts +
        // skipReplyPrefix) exactly, or this sizing pass and the real render
        // pass disagree on wrapped line count for room posts.
        char tmp_sender[33];
        return bodyAt(idx, tmp_sender, sizeof(tmp_sender));
      });
  };
  HistScroll hs = scrollMetrics();
  int reserve = hs.reserve;
  int div_y = -1;   // "New", when shown
  // Stack boxes upward from just above the compose row, so item 0 (the
  // newest message) lands at the bottom of the list and older messages
  // sit progressively higher — same anchor convention as a typical
  // messenger, instead of newest-at-top. box_ys[i] still corresponds
  // to item (s + i), same as before; only its y flips.
  auto stack = [&](int s) {
    return n_vis = stackBoxes(s, dm_count, cby, hist_start_y, lh, expand, box_ys, box_hs, MAX_VIS_BOXES, div_y,
      [&](int item) {
        if (expand) {
          const int rp = dmViewPos(item);
          if (rp >= 0) {
            char hsb[33];
            const char* hbody = skipReplyPrefix(dmDisplayParts(_history.dmAtPos(rp), is_room, filtered_name, hsb, sizeof(hsb)));
            return expandedBoxH(display, hbody, display.width() - reserve);
          }
        }
        return compact_dm ? compactH(item, reserve) : 2 * lh + 1;
      });
  };
  if (_anchor_new) {
    _anchor_new = false;
    _dm_hist_scroll = anchorScroll(stack, div_y, _dm_hist_sel, dm_count);
    hs = scrollMetrics();
  }
  stack(_dm_hist_scroll);
  _hist_visible = n_vis;
  for (int i = 0; i < n_vis && (_dm_hist_scroll + i) < dm_count; i++) {
    int item = _dm_hist_scroll + i;
    bool sel = (item == _dm_hist_sel);
    int y = box_ys[i];
    int bh = box_hs[i];

    int ring_pos = dmViewPos(item);
    if (ring_pos < 0) continue;

    const DmHistEntry& e = _history.dmAtPos(ring_pos);
    char sender_buf[33];
    const char* body = skipReplyPrefix(dmDisplayParts(e, is_room, filtered_name, sender_buf, sizeof(sender_buf)));
    // A DM has two people: the header names the contact and the side a
    // bubble sits on says who wrote it, so bubbles carry only the delivery
    // or hop marker and the age. Rooms keep each post's author.
    const char* sender = is_room ? sender_buf : "";

    if (compact_dm) {
      Tail t = tailOf(e);
      int full_avail = display.width() - reserve;
      int max_w = bubbleMaxW(display, full_avail);
      int line_w = max_w - 6;
      int split = dmSplit(display, body, line_w, t.w);
      char l1[MSG_TEXT_BUF];
      const char* l2 = nullptr;
      int body_w;
      if (split < 0) {
        int raw = display.getTextWidth(body);
        body_w = (raw > line_w - t.w ? line_w - t.w : raw) + 6 + t.w;
      } else {
        int n = split;
        while (n > 0 && body[n - 1] == ' ') n--;
        if (n > (int)sizeof(l1) - 1) n = sizeof(l1) - 1;
        memcpy(l1, body, n); l1[n] = '\0';
        l2 = body + split;
        int w1 = display.getTextWidth(l1), w2 = display.getTextWidth(l2);
        if (w2 > line_w - t.w) w2 = line_w - t.w;
        body_w = (w1 > w2 + t.w ? w1 : w2 + t.w) + 6;
      }
      BubbleBox box = computeBubbleBox(full_avail, max_w, e.outgoing, 0, body_w);
      drawHistRowFrame(display, box.x, box.w, y, bh, lh, sel);
      // Marker and age end the last line of text.
      const int ty = y + 1 + (l2 ? lh : 0);
      const int mark_x = box.x + box.w - t.w + 3;
      if (e.outgoing)      drawAckGlyph(display, mark_x, ty, _history.dmEffectiveStatus(e), e.attempt + 1);
      else if (t.hops > 0) miniIconDrawNumber(display, mark_x, ty, t.hops);
      if (t.age[0]) { display.setCursor(box.x + box.w - t.age_w, ty); display.print(t.age); }
      // Only the last line marquees (one shared marquee slot), and not
      // while the context menu is over it.
      bool body_marquee = sel && !_ctx_menu.active;
      int r_body;
      if (!l2) {
        r_body = display.drawTextEllipsized(box.x + 3, y + 1, box.w - 6 - t.w, body, body_marquee);
      } else {
        display.drawTextEllipsized(box.x + 3, y + 1, box.w - 6, l1, false);
        r_body = display.drawTextEllipsized(box.x + 3, y + lh + 1, box.w - 6 - t.w, l2, body_marquee);
      }
      if (body_marquee && r_body > 0) mq_delay = r_body;
      continue;
    }

    // e.timestamp==0 shouldn't happen (storeDMMsg() falls back to receipt
    // time at write time), but if it ever does, show the receipt time
    // (now_ts) rather than leave the row with no age at all.
    char age[6]; geo::fmtAgeShort(age, sizeof(age), now_ts, e.timestamp ? e.timestamp : now_ts);
    // Outgoing: the delivery marker. Incoming: the hop count the DM
    // actually took to reach us, as the same tiny digit icon (no ack
    // glyph exists for incoming -- there's nothing to deliver).
    const BubbleMark mark = e.outgoing ? BubbleMark::ack(_history.dmEffectiveStatus(e), e.attempt + 1, 0)
                                       : BubbleMark::hop(e.path_len & 63);
    drawHeadedBubble(display, y, bh, lh, sel, reserve, e.outgoing, sender, body, age, mark, expand, mq_delay);
  }

  drawHistFooter(display, hs, div_y, dm_count, hist_start_y, cby, lh, _dm_hist_sel == -1, mq_delay);
  { int ret = dm_count > 0 ? 500 : 2000; return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret; }
  return marqueeDelay(mq_delay, 2000);
}

inline int MessagesScreen::renderChannelHist(DisplayDriver& display) {
  int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
  const int lh = display.getLineHeight();
  if (_fs.active && _hist_sel >= 0) {
    int fs_hist_count = chViewCount();
    int ring_pos = chViewPos(_hist_sel);
    if (ring_pos >= 0) {
      char fsender[33];
      const char* fmsg = chSenderParts(_history.chAtPos(ring_pos).text, fsender, sizeof(fsender));
      if (!fsender[0]) strcpy(fsender, "?");
      int ret = _fs.render(display, fsender, fmsg,
                           _hist_sel < fs_hist_count - 1,
                           _hist_sel > 0);
      // Channels: ✓ only once a repeater echo confirms relay (see list view),
      // plus the distinct-echoing-repeater count as tiny digits (path_len's
      // hop_count -- see markChannelRelayed/showPathDetail's "Relayed by").
      if (strcmp(fsender, "Me") == 0 && _history.chAtPos(ring_pos).relay_status == ACK_OK) {
        display.setColor(UIColor::window_bkg);
        int relay_count = _history.chAtPos(ring_pos).path_len & 63;
        drawAckGlyph(display, 2 + display.getTextWidth(fsender) + 3, 1, ACK_OK, 1, relay_count);
        display.setColor(UIColor::primary_txt);
      }
      // Fold the popup's own suggested redraw delay into mq_delay -- otherwise
  // a marquee inside it (see PopupMenu::render()) only ever advances at
  // whatever slower cadence this phase's own return statement below picks,
  // since that return previously ignored this call's result entirely.
  if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
      return ret;
    }
    return 2000;
  }

  int hist_start_y = display.headerH();
  int cby = display.height() - lh;   // compose button is now exactly lh tall, no added border padding

  ChannelDetails ch;
  the_mesh.getChannel(_sel_channel_idx, ch);
  char title[32];
  NodePrefs* p_hdr = _task->getNodePrefs();
  uint8_t hdr_sc_idx = p_hdr ? the_mesh.channelScope(_sel_channel_idx) : 0;
  if (hdr_sc_idx != 0) {
    // Non-wildcard scope set on this channel -- surface it in the title,
    // same as the app's own per-channel scope tag, so it's obvious at a
    // glance the channel isn't sending unscoped.
    snprintf(title, sizeof(title), "%.16s [%.8s]", ch.name, the_mesh.scopeList().name(hdr_sc_idx));
  } else {
    snprintf(title, sizeof(title), "%.23s", ch.name);
  }
  drawScreenHeader(display, title, -1, 0, true, _ctx_menu.active);

  int ch_hist_count = chViewCount();
  uint32_t now_ts = rtc_clock.getCurrentTime();

  // On a tall enough screen (expandHistory): boxes as tall as their whole
  // wrapped text. Otherwise compact 2-line boxes.
  const bool expand = expandHistory(display);
  const int MAX_VIS_BOXES = 8;
  int box_ys[MAX_VIS_BOXES], box_hs[MAX_VIS_BOXES], n_vis = 0;
  // Fixed-track scrollbar metrics + stable gutter reserve (see DM history above).
  auto scrollMetrics = [&]() {
    return computeHistScroll(display, expand, ch_hist_count, _hist_scroll,
      hist_start_y, cby, lh,
      [&](int idx) -> const char* {
        int rp = chViewPos(idx);
        if (rp < 0) return nullptr;
        const char* t = _history.chAtPos(rp).text;
        const char* s = strstr(t, ": ");
        return skipReplyPrefix(s ? s + 2 : t);
      });
  };
  HistScroll hs = scrollMetrics();
  int reserve = hs.reserve;
  int div_y = -1;   // "New", when shown
  // Stack boxes upward from just above the compose row — see the DM
  // history block above for why (newest at the bottom, like a typical
  // messenger). box_ys[i] still corresponds to item (s + i).
  auto stack = [&](int s) {
    return n_vis = stackBoxes(s, ch_hist_count, cby, hist_start_y, lh, expand, box_ys, box_hs, MAX_VIS_BOXES, div_y,
      [&](int item) {
        const int rp = expand ? chViewPos(item) : -1;
        if (rp < 0) return 2 * lh + 1;
        const char* rtext = _history.chAtPos(rp).text;
        const char* rsep = strstr(rtext, ": ");
        return expandedBoxH(display, skipReplyPrefix(rsep ? rsep + 2 : rtext), display.width() - reserve);
      });
  };
  if (_anchor_new) {
    _anchor_new = false;
    _hist_scroll = anchorScroll(stack, div_y, _hist_sel, ch_hist_count);
    hs = scrollMetrics();
  }
  stack(_hist_scroll);
  _hist_visible = n_vis;
  updateChannelUnread();  // mark everything in the just-computed visible window as seen

  for (int i = 0; i < n_vis && (_hist_scroll + i) < ch_hist_count; i++) {
    int item = _hist_scroll + i;
    bool sel = (item == _hist_sel);
    int y = box_ys[i];
    int bh = box_hs[i];

    int ring_pos = chViewPos(item);
    if (ring_pos < 0) continue;

    char sender[33];
    const char* msg_part = chSenderParts(_history.chAtPos(ring_pos).text, sender, sizeof(sender));
    if (!sender[0]) strcpy(sender, "?");

    // See the DM history block above: 0 shouldn't happen (addChannelMsg()
    // also falls back to receipt time at write time), but fall back to
    // now_ts here too rather than leave the row with no age at all.
    uint32_t ch_ts = _history.chAtPos(ring_pos).timestamp;
    char age[6]; geo::fmtAgeShort(age, sizeof(age), now_ts, ch_ts ? ch_ts : now_ts);
    const char* body = skipReplyPrefix(msg_part);

    // Channels have no recipient ACK -- only show the glyph once a
    // repeater echo confirms the send was relayed into the mesh; otherwise
    // no marker (absence is normal). Incoming: the hop count this post
    // actually took to reach us.
    const bool outgoing = strcmp(sender, "Me") == 0;
    const ChHistEntry& cm = _history.chAtPos(ring_pos);
    const BubbleMark mark = !outgoing ? BubbleMark::hop(cm.path_len & 63)
                          : cm.relay_status == ACK_OK ? BubbleMark::ack(ACK_OK, 1, cm.path_len & 63)
                          : BubbleMark();
    drawHeadedBubble(display, y, bh, lh, sel, reserve, outgoing, sender, body, age, mark, expand, mq_delay);
  }

  drawHistFooter(display, hs, div_y, ch_hist_count, hist_start_y, cby, lh, _hist_sel == -1, mq_delay);
  return marqueeDelay(mq_delay, 2000);
}

inline bool MessagesScreen::inputDmHist(char c) {
  int dm_count = dmViewCount();
  if (_ctx_menu.active) return msgMenuInput(c, false);   // over the list or the full-screen view
  if (_dm_fs.active) {
    auto res = _dm_fs.handleInput(c);
    if (res == FullscreenMsgView::PREV) {
      if (_dm_hist_sel < dm_count - 1) { _dm_hist_sel++; _dm_fs.scroll = 0; }
      else if (turnOlder(false)) _dm_fs.scroll = 0;
    } else if (res == FullscreenMsgView::NEXT) {
      if (_dm_hist_sel > 0) { _dm_hist_sel--; _dm_fs.scroll = 0; }
      else if (turnNewer(false)) _dm_fs.scroll = 0;
    } else if (res == FullscreenMsgView::CLOSE) {
      _dm_fs.active = false;
    } else if (res == FullscreenMsgView::REPLY) {
      int ring_pos = dmViewPos(_dm_hist_sel);
      if (ring_pos >= 0) {
        bool reply_ok = !_history.dmAtPos(ring_pos).outgoing;
        if (reply_ok) buildDmReplyPrefix(_history.dmAtPos(ring_pos));
        buildFsMenu(_history.dmAtPos(ring_pos).text, reply_ok, ring_pos, false);
      }
    }
    return true;
  }
  if (c == KEY_CANCEL && _older) { leaveOlder(false); return true; }
  if (c == KEY_CANCEL) {
    if (_direct_entry) {
      _direct_entry = false;
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
    if (dm_count == 0 && turnOlder(false)) return true;   // the ring has lost this conversation
    if (_dm_hist_sel == -1 && dm_count > 0) { _dm_hist_sel = 0; _dm_hist_scroll = 0; }
    else if (_dm_hist_sel >= 0 && _dm_hist_sel < dm_count - 1) {
      _dm_hist_sel++;
      if (_dm_hist_sel >= _dm_hist_scroll + _hist_visible)
        _dm_hist_scroll = _dm_hist_sel - _hist_visible + 1;
    } else if (_dm_hist_sel == dm_count - 1) {
      if (turnOlder(false)) return true;
      // Oldest (top of the list) -> wrap straight to the newest (bottom
      // of the list), same ring-wrap every other list in the UI does at
      // its ends -- the compose row sits below the list itself and is
      // not part of this wrap.
      _dm_hist_sel = 0;
      _dm_hist_scroll = 0;
    }
    return true;
  }
  if (c == KEY_DOWN) {
    if (_dm_hist_sel > 0) {
      _dm_hist_sel--;
      if (_dm_hist_sel < _dm_hist_scroll) _dm_hist_scroll = _dm_hist_sel;
    } else if (_dm_hist_sel == 0) {
      if (turnNewer(false)) return true;
      _dm_hist_sel = -1;
    } else if (_dm_hist_sel == -1 && dm_count > 0) {
      // Compose row -> wrap to the oldest message (top of the list).
      _dm_hist_sel = dm_count - 1;
      _dm_hist_scroll = (dm_count > _hist_visible) ? dm_count - _hist_visible : 0;
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
    int ring_pos = dmViewPos(_dm_hist_sel);
    if (ring_pos >= 0) {
      bool reply_ok = !_history.dmAtPos(ring_pos).outgoing;
      if (reply_ok) buildDmReplyPrefix(_history.dmAtPos(ring_pos));
      buildFsMenu(_history.dmAtPos(ring_pos).text, reply_ok, ring_pos, false);
    }
    return true;
  }
  return false;
}

inline bool MessagesScreen::inputChannelHist(char c) {
  int ch_hist_count = chViewCount();
  if (_ctx_menu.active) return msgMenuInput(c, true);   // over the list or the full-screen view
  if (_fs.active) {
    auto res = _fs.handleInput(c);
    if (res == FullscreenMsgView::PREV) {
      if (_hist_sel < ch_hist_count - 1) { _hist_sel++; _fs.scroll = 0; updateChannelUnread(); }
      else if (turnOlder(true)) _fs.scroll = 0;
    } else if (res == FullscreenMsgView::NEXT) {
      if (_hist_sel > 0) { _hist_sel--; _fs.scroll = 0; updateChannelUnread(); }
      else if (turnNewer(true)) _fs.scroll = 0;
    } else if (res == FullscreenMsgView::CLOSE) {
      _fs.active = false;
    } else if (res == FullscreenMsgView::REPLY) {
      int ring_pos = chViewPos(_hist_sel);
      if (ring_pos >= 0)
        buildFsMenu(_history.chAtPos(ring_pos).text, buildChannelReplyPrefix(_history.chAtPos(ring_pos).text), ring_pos, true);
    }
    return true;
  }
  if (c == KEY_CANCEL && _older) { leaveOlder(true); return true; }
  if (c == KEY_CANCEL) {
    if (_direct_entry) { _direct_entry = false; _task->gotoHomeScreen(); }
    else               { enterChannelList(_sel_channel_idx); }
    return true;
  }
  // Newest (index 0) now renders at the bottom, oldest at the top (see the
  // render block above) — UP/DOWN swap direction accordingly, same as the
  // DM history handler above.
  if (c == KEY_UP) {
    if (ch_hist_count == 0 && turnOlder(true)) return true;   // the ring has lost this channel
    if (_hist_sel == -1 && ch_hist_count > 0) { _hist_sel = 0; _hist_scroll = 0; }
    else if (_hist_sel >= 0 && _hist_sel < ch_hist_count - 1) {
      _hist_sel++;
      if (_hist_sel >= _hist_scroll + _hist_visible) _hist_scroll = _hist_sel - _hist_visible + 1;
    } else if (_hist_sel == ch_hist_count - 1) {
      if (turnOlder(true)) return true;
      // Oldest -> wrap straight to the newest, same as the DM history handler.
      _hist_sel = 0;
      _hist_scroll = 0;
    }
    updateChannelUnread();
    return true;
  }
  if (c == KEY_DOWN) {
    if (_hist_sel > 0) { _hist_sel--; if (_hist_sel < _hist_scroll) _hist_scroll = _hist_sel; }
    else if (_hist_sel == 0) { if (turnNewer(true)) return true; _hist_sel = -1; }
    else if (_hist_sel == -1 && ch_hist_count > 0) {
      // Compose row -> wrap to the oldest message.
      _hist_sel = ch_hist_count - 1;
      _hist_scroll = (ch_hist_count > _hist_visible) ? ch_hist_count - _hist_visible : 0;
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
    int ring_pos = chViewPos(_hist_sel);
    if (ring_pos >= 0)
      buildFsMenu(_history.chAtPos(ring_pos).text, buildChannelReplyPrefix(_history.chAtPos(ring_pos).text), ring_pos, true);
    return true;
  }
  return false;
}
