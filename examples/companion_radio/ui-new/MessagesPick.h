#pragma once
// MessagesScreen's pickers before a conversation: DMs / channels / rooms,
// the contact list (with its menu: pin, notifications, room login) and the
// channel list (with its menu: scope, notifications, edit) -- drawing and
// keys. Included at the end of MessagesScreen.h.

inline int MessagesScreen::renderModeSelect(DisplayDriver& display) {
  int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
  const int item_h = display.lineStep();
  const int start_y = display.listStart();
  // Say which dial slot is being filled -- entered from the Favourites page,
  // a bare "Messages" gives no sign that this browse is a pick.
  char hdr[16];
  if (_pick_fav_slot >= 0) snprintf(hdr, sizeof(hdr), "Pin slot %d", _pick_fav_slot + 1);
  else                     snprintf(hdr, sizeof(hdr), "Messages");
  drawScreenHeader(display, hdr, -1, 0, true, _ctx_menu.active);
  const char* opts[] = { "Direct message", "Channels", "Room Servers" };
  int badges[3] = {
    getDMUnreadTotal(),
    _task->getChannelUnreadCount(),
    _task->getRoomUnreadCount()
  };
  // Rooms have no local ring to overflow the same way (see UITask::getAnyUnreadOverflow).
  bool overflow[3] = { _task->getAnyDMUnreadOverflow(), anyChannelUnreadOverflow(), false };
  for (int i = 0; i < 3; i++) {
    int y = start_y + i * item_h;
    bool sel = (i == _mode_sel);
    display.drawSelectionRow(0, y - 1, display.width(), item_h - 1, sel);
    display.setCursor(2, y);
    display.print(opts[i]);
    if (badges[i] > 0)
      display.drawUnreadBadge(display.width() - 1, y, badges[i], sel, overflow[i]);
  }
  display.setColor(UIColor::primary_txt);
  // Fold the popup's own suggested redraw delay into mq_delay -- otherwise
  // a marquee inside it (see PopupMenu::render()) only ever advances at
  // whatever slower cadence this phase's own return statement below picks,
  // since that return previously ignored this call's result entirely.
  if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
  return marqueeDelay(mq_delay, 2000);
}

inline int MessagesScreen::renderContactPick(DisplayDriver& display) {
  int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
  drawScreenHeader(display, _room_mode ? "Rooms" : "Contacts", -1, 0, true, _ctx_menu.active);

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
      bool dm_overflow = dm_unread > 0 && _task->getDMUnreadOverflow(c.id.pub_key);
      int bw = dm_unread > 0 ? display.unreadBadgeWidth(dm_unread, dm_overflow) + 2 : 0;
      int sw = contactctl::favourite(c) ? favStarWidth(display) : 0;
      // See the channel/DM history bodies' identical comment: suppress this
      // row's own marquee while a context menu covers it, so the two don't
      // fight over DisplayDriver's single shared marquee slot.
      bool name_marquee = sel && !_ctx_menu.active;
      int r = display.drawTextEllipsized(2, y, display.width() - 2 - bw - sw - reserve, filtered, name_marquee);
      if (name_marquee && r > 0) mq_delay = r;
      if (sw) drawFavStar(display, display.width() - reserve - bw - sw + 1, y);
      if (dm_unread > 0)
        display.drawUnreadBadge(display.width() - reserve, y, dm_unread, sel, dm_overflow);
    }
  });

  // Context menu overlay
  // Fold the popup's own suggested redraw delay into mq_delay -- otherwise
  // a marquee inside it (see PopupMenu::render()) only ever advances at
  // whatever slower cadence this phase's own return statement below picks,
  // since that return previously ignored this call's result entirely.
  if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
  return marqueeDelay(mq_delay, 2000);
}

inline int MessagesScreen::renderChannelPick(DisplayDriver& display) {
  int mq_delay = 0;   // >0 while a selected row's text is marquee-scrolling
  drawScreenHeader(display, "Channels", -1, 0, true, _ctx_menu.active);

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
      display.setCursor(2, y);
      // Settings > Channels = Fav hides all but the favourites; say so when that leaves nothing.
      const NodePrefs* np = _task->getNodePrefs();
      display.print(_num_channels == 0 && np && np->ch_fav_only ? "+ Add (Fav only)" : "+ Add channel");
      display.setColor(UIColor::primary_txt);
      return;
    }
    ChannelDetails ch;
    if (the_mesh.getChannel(_channel_indices[list_idx], ch)) {
      uint8_t unread = _history.chUnread(_channel_indices[list_idx]);
      bool ch_overflow = unread > 0 && _history.chUnreadOverflow(_channel_indices[list_idx]);
      int bw = unread > 0 ? display.unreadBadgeWidth(unread, ch_overflow) + 2 : 0;
      int sw = chIsFav(_channel_indices[list_idx]) ? favStarWidth(display) : 0;
      // See the channel/DM history bodies' identical comment: suppress this
      // row's own marquee while a context menu covers it, so the two don't
      // fight over DisplayDriver's single shared marquee slot.
      bool name_marquee = sel && !_ctx_menu.active;
      int r = display.drawTextEllipsized(2, y, display.width() - 4 - bw - sw - reserve, ch.name, name_marquee);
      if (name_marquee && r > 0) mq_delay = r;
      if (sw) drawFavStar(display, display.width() - reserve - bw - sw + 1, y);
      if (unread > 0)
        display.drawUnreadBadge(display.width() - reserve, y, unread, sel, ch_overflow);
    }
  });

  // Context menu overlay
  // Fold the popup's own suggested redraw delay into mq_delay -- otherwise
  // a marquee inside it (see PopupMenu::render()) only ever advances at
  // whatever slower cadence this phase's own return statement below picks,
  // since that return previously ignored this call's result entirely.
  if (_ctx_menu.active) { int r = _ctx_menu.render(display); if (mq_delay <= 0 || r < mq_delay) mq_delay = r; }
  return marqueeDelay(mq_delay, 2000);
}

inline bool MessagesScreen::inputModeSelect(char c) {
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
      enterChannelList(_channel_sel < _num_channels ? _channel_indices[_channel_sel] : -1);
    } else {
      _room_mode = (_mode_sel == 2);
      if (_room_mode) _task->clearRoomUnread();
      buildContactList();
      _contact_sel = _contact_scroll = 0;
      _phase = CONTACT_PICK;
    }
    return true;
  }
  return false;
}

inline bool MessagesScreen::inputContactPick(char c) {
  // Context menu consumes all input while open
  if (_ctx_menu.active) {
    if (_pin_picker_active) {
      // Slot picker sub-menu: index 0..FAVOURITES_COUNT-1 maps directly to slot.
      auto res = _ctx_menu.handleInput(c);
      if (res == PopupMenu::SELECTED && _num_contacts > 0) {
        ContactInfo ci;
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci))
          pinContactToSlot(ci, _ctx_menu.selectedIndex());
      }
      if (res != PopupMenu::NONE) {
        _pin_picker_active = false;
        _task->savePrefsIfDirty(_ctx_dirty);   // Notif/Melody edits made before Pin was picked
      }
      return true;
    }
    if (_room_mode) {
      // LEFT/RIGHT -- and Enter, via VALUE_NEXT -- toggle Fav in place; the
      // menu stays open and only Back closes it.
      if (keyIsPrev(c) || keyIsNext(c)) {
        cycleRoomCtxValue(_ctx_menu.selectedIndex());
        return true;
      }
      auto res = _ctx_menu.handleInput(c);
      if (res == PopupMenu::VALUE_NEXT) {
        cycleRoomCtxValue(_ctx_menu.selectedIndex());
        return true;   // still open -- the list rebuild below waits for close
      }
      if (res == PopupMenu::SELECTED && _num_contacts > 0) {
        if (the_mesh.getContactByIdx(_sorted[_contact_sel], _sel_contact)) {
          int sel = _ctx_menu.selectedIndex();
          if (sel == 0) {
            _login_mode = true;
            _kb->begin("", 15); // room/repeater password: max 15 chars
            _kb->prompt = "Password";
            _kb->clearPlaceholders();   // {loc}/{time} are for messages, not a password
            _phase = KEYBOARD;
          } else if (sel == _ctx_pin_idx) {
            pinContactAction(_sel_contact);
            if (_pin_picker_active) return true;   // rebuild below would close the submenu
          } else if (sel != _ctx_fav_idx) {
            // Logout: only reachable when isRoomLoggedIn() added this item.
            _task->core().rooms.logout(_sel_contact.id.pub_key);
          }
          // Fav is a value row -- Enter never selects it (see cycleRoomCtxValue).
        }
      }
      if (res != PopupMenu::NONE && _phase == CONTACT_PICK) {
        // A room un-favourited under a fav-only filter drops off the list.
        buildContactList();
        if (_contact_sel >= _num_contacts) _contact_sel = _num_contacts > 0 ? _num_contacts - 1 : 0;
      }
      return true;
    }
    // LEFT/RIGHT -- and Enter, via VALUE_NEXT -- cycle Notif/Melody/Fav in
    // place; the menu stays open and only Back closes it.
    if (keyIsPrev(c) || keyIsNext(c)) {
      cycleContactCtxValue(_ctx_menu.selectedIndex(), keyIsNext(c) ? 1 : -1);
      return true;
    }
    auto res = _ctx_menu.handleInput(c);
    if (res == PopupMenu::VALUE_NEXT) {
      cycleContactCtxValue(_ctx_menu.selectedIndex(), 1);
      return true;   // still open -- the save/rebuild below waits for close
    }
    if (res == PopupMenu::SELECTED && _num_contacts > 0) {
      ContactInfo ci;
      if (the_mesh.getContactByIdx(_sorted[_contact_sel], ci)) {
        int sel = _ctx_menu.selectedIndex();
        if (sel == 0) {
          int cleared = (int)_task->getDMUnread(ci.id.pub_key);
          _task->clearDMUnread(ci.id.pub_key);
          markReadAlert(cleared);
        } else if (sel == 4) {
          pinContactAction(ci);
        }
        // sel 1 (Notif), 2 (Melody), 3 (Fav) are value rows -- Enter never
        // selects them (see cycleContactCtxValue).
      }
    }
    if (res != PopupMenu::NONE) {
      _task->savePrefsIfDirty(_ctx_dirty);
      if (!_pin_picker_active) {
        // A contact un-favourited under a fav-only filter drops off the list.
        buildContactList();
        if (_contact_sel >= _num_contacts) _contact_sel = _num_contacts > 0 ? _num_contacts - 1 : 0;
      }
    }
    return true;
  }
  if (c == KEY_CANCEL) {
    if (_pick_bot_room) { _pick_bot_room = false; _room_mode = false; _task->gotoBotScreen(); return true; }
    if (_pick_sos_slot >= 0) { _pick_sos_slot = -1; _task->gotoSosScreen(); return true; }
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
      if (_pick_fav_slot >= 0) { commitPickFavContact(_sel_contact); return true; }
      if (_pick_sos_slot >= 0) { commitPickSos(_sel_contact); return true; }
      if (_pick_bot_room) {
        if (!isRoomLoggedIn(_sel_contact.id.pub_key)) {
          // Known password, just not re-established this boot: the Core
          // retries silently in the background; the bot target is set
          // below regardless of this attempt's outcome (self-heals like
          // any other saved room password would on the next real open).
          RoomSessions::Open o = _task->core().rooms.open(_sel_contact);
          if (o == RoomSessions::LOGGING_IN || o == RoomSessions::SEND_FAILED) {
            loginAlert(o == RoomSessions::LOGGING_IN);
          } else if (o == RoomSessions::NEED_PASSWORD) {
            // Never logged in — the bot could never post here without a
            // password, so prompt for one now instead of picking an
            // unusable target. Commits once submitted (see the KEYBOARD/
            // _login_mode handler), whether or not login itself succeeds.
            _login_mode = true;
            _kb->begin("", 15); // room/repeater password: max 15 chars
            _kb->prompt = "Password";
            _kb->clearPlaceholders();
            _phase = KEYBOARD;
            return true;
          }
        }
        commitPickBotRoom(_sel_contact);
        return true;
      }
      if (_room_mode && !isRoomLoggedIn(_sel_contact.id.pub_key)) {
        requireRoomLogin();
        return true;
      }
      openDmHistory();
      if (_share_mode) beginShareCompose(false);
    }
    return true;
  }
  if (c == KEY_CONTEXT_MENU && _num_contacts > 0 && _room_mode) {
    ContactInfo ci;
    bool have = the_mesh.getContactByIdx(_sorted[_contact_sel], ci);
    bool logged_in = have && isRoomLoggedIn(ci.id.pub_key);
    snprintf(_ctx_fav_item, sizeof(_ctx_fav_item),
             (have && contactctl::favourite(ci)) ? "Fav: ON" : "Fav: OFF");
    { int pinned_slot = have ? _task->findFavouriteSlot(ci.id.pub_key) : -1;
      if (pinned_slot >= 0) snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Unpin (slot %d)", pinned_slot + 1);
      else                  snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Pin to dial"); }
    _ctx_menu.begin("Room options", logged_in ? 2 : 1);
    _ctx_menu.addItem("Login...");
    if (logged_in) _ctx_menu.addItem("Logout");
    _ctx_fav_idx = logged_in ? 2 : 1;
    _ctx_menu.addValueItem(_ctx_fav_item);
    _ctx_pin_idx = _ctx_fav_idx + 1;
    _ctx_menu.addItem(_ctx_pin_item);
    return true;
  }
  if (c == KEY_CONTEXT_MENU && _num_contacts > 0 && !_room_mode) {
    ContactInfo ci;
    the_mesh.getContactByIdx(_sorted[_contact_sel], ci);
    snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s",
             NOTIF_LABELS[dmNotifState(ci.id.pub_key)]);
    snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s",
             MELODY_LABELS[dmMelodySlot(ci.id.pub_key)]);
    int pinned_slot = _task->findFavouriteSlot(ci.id.pub_key);
    if (pinned_slot >= 0) snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Unpin (slot %d)", pinned_slot + 1);
    else                  snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Pin to dial");
    snprintf(_ctx_fav_item, sizeof(_ctx_fav_item),
             contactctl::favourite(ci) ? "Fav: ON" : "Fav: OFF");
    _ctx_menu.begin("Contact options", 3);
    _ctx_menu.addItem("Mark as read");
    _ctx_menu.addValueItem(_ctx_notif_item);
    _ctx_menu.addValueItem(_ctx_melody_item);
    _ctx_fav_idx = 3;
    _ctx_menu.addValueItem(_ctx_fav_item);
    _ctx_menu.addItem(_ctx_pin_item);
    _ctx_dirty = false;
    return true;
  }
  return false;
}

inline bool MessagesScreen::inputChannelPick(char c) {
  // Context menu consumes all input while open
  if (_ctx_menu.active) {
    // LEFT/RIGHT -- and Enter, via VALUE_NEXT below -- cycle Notif/Melody/Fav
    // in place; the menu stays open and only Back closes it.
    if (!_pin_picker_active && !_ch_delete_confirm_active && !_scope_pick_active && (keyIsPrev(c) || keyIsNext(c))) {
      cycleChannelCtxValue(_ctx_menu.selectedIndex(), keyIsNext(c) ? 1 : -1);
      return true;
    }
    auto res = _ctx_menu.handleInput(c);
    if (_scope_pick_active) {
      // Scope sub-menu: Default first, then the list ("*" first).
      if (res == PopupMenu::SELECTED) {
        the_mesh.setChannelScope(_ctx_ch_idx, chanctl::scopeFromRow(_ctx_menu.selectedIndex()));
        the_mesh.savePrefs();
      }
      if (res != PopupMenu::NONE) _scope_pick_active = false;
      return true;
    }
    if (_pin_picker_active) {
      // Slot picker sub-menu: index 0..FAVOURITES_COUNT-1 maps directly to slot.
      if (res == PopupMenu::SELECTED && _pin_slot_ch_idx >= 0) {
        pinChannelToSlot((uint8_t)_pin_slot_ch_idx, _ctx_menu.selectedIndex());
      }
      if (res != PopupMenu::NONE) {
        _pin_picker_active = false;
        _pin_slot_ch_idx = -1;
        _task->savePrefsIfDirty(_ctx_dirty);   // Notif/Melody/Fav edits made before Pin was picked
      }
      return true;
    }
    if (_ch_delete_confirm_active) {
      // Delete/Cancel sub-menu, defaults to Cancel (see where it's opened).
      if (res == PopupMenu::SELECTED && _ctx_menu.selectedIndex() == 0) {   // "Delete"
        chanctl::remove(_ctx_ch_idx);
      }
      if (res != PopupMenu::NONE) {
        _ch_delete_confirm_active = false;
        _task->savePrefsIfDirty(_ctx_dirty);
        buildChannelList();
        if (_channel_sel >= _num_channels) _channel_sel = _num_channels > 0 ? _num_channels - 1 : 0;
      }
      return true;
    }
    if (res == PopupMenu::VALUE_NEXT) {
      cycleChannelCtxValue(_ctx_menu.selectedIndex(), 1);
      return true;   // still open -- the save/rebuild below waits for close
    }
    if (res == PopupMenu::SELECTED && _num_channels > 0) {
      uint8_t ch_idx = _ctx_ch_idx;   // frozen at menu open — see declaration
      int sel = _ctx_menu.selectedIndex();
      if (_ctx_save_idx >= 0 && sel > _ctx_save_idx) sel--;   // the rows below count without it
      if (sel == 0) {
        int cleared = (int)_history.chUnread(ch_idx);
        _history.setChUnread(ch_idx, 0);
        markReadAlert(cleared);
      } else if (sel == 4) {              // Scope
        const ScopeList& sl = the_mesh.scopeList();
        snprintf(_ctx_scope_def, sizeof(_ctx_scope_def), "Default (%s)", sl.name(sl.default_idx));
        _ctx_menu.begin("Scope", 4);
        _ctx_menu.addItem(_ctx_scope_def);
        for (uint8_t i = 0; i <= sl.count; i++) _ctx_menu.addItem(sl.name(i));
        _ctx_menu.setSelected(chanctl::scope(_task->getNodePrefs(), ch_idx));
        _scope_pick_active = true;
        return true;   // list rebuild below would close the submenu
      } else if (sel == 5) {              // Pin / Unpin
        int pinned_slot = _task->findFavouriteChannelSlot(ch_idx);
        if (pinned_slot >= 0) {
          _task->clearFavouriteSlot(pinned_slot);
          the_mesh.savePrefs();
          char alert[24];
          snprintf(alert, sizeof(alert), "Unpinned (slot %d)", pinned_slot + 1);
          _task->showAlert(alert, 800);
        } else {
          buildSlotLabels();
          _ctx_menu.begin("Pick slot", 3);
          for (int s = 0; s < NodePrefs::FAVOURITES_COUNT; s++) _ctx_menu.addItem(_pin_slot_labels[s]);
          _pin_slot_ch_idx = ch_idx;
          _pin_picker_active = true;
          return true;   // list rebuild below would close the submenu
        }
      } else if (sel == 6) {              // Edit
        ChannelDetails ch;
        if (the_mesh.getChannel(ch_idx, ch)) _ch_view.openEdit(ch_idx, ch.name);
      } else if (sel == 7) {              // Delete -- confirm first (destructive)
        _ctx_menu.beginConfirm("Delete channel?", "Delete");
        _ch_delete_confirm_active = true;
        return true;   // list rebuild below would close the submenu
      }
      // sel 1/2/3 are value rows -- Enter never selects them
      // (see cycleChannelCtxValue).
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
    if (_pick_fav_slot >= 0) { commitPickFavChannel(_sel_channel_idx); return true; }
    if (_pick_bot_channel) { commitPickBotChannel(_sel_channel_idx); return true; }
    beginChannelView(_sel_channel_idx);
    _phase = CHANNEL_HIST;
    // Not updateChannelUnread() here: _hist_visible is still whatever this
    // channel's first render() hasn't computed yet (stale, shared with
    // DM_HIST) — calling it now could mark more seen than is about to be
    // shown. render() calls it once that's fresh.
    if (_share_mode) beginShareCompose(true);
    return true;
  }
  if (c == KEY_CONTEXT_MENU && _num_channels > 0 && _channel_sel < _num_channels) {
    uint8_t ch_idx = _channel_indices[_channel_sel];
    _ctx_ch_idx = ch_idx;   // freeze the menu's target channel
    snprintf(_ctx_notif_item, sizeof(_ctx_notif_item), "Notif: %s",
             NOTIF_LABELS[chNotifState(ch_idx)]);
    snprintf(_ctx_melody_item, sizeof(_ctx_melody_item), "Melody: %s",
             MELODY_LABELS[chNotifMelody(ch_idx)]);
    { NodePrefs* p2 = _task->getNodePrefs();
      bool is_fav = p2 && (p2->ch_fav_bitmask & (1ULL << ch_idx));
      snprintf(_ctx_fav_item, sizeof(_ctx_fav_item), is_fav ? "Fav: ON" : "Fav: OFF"); }
    { int pinned_slot = _task->findFavouriteChannelSlot(ch_idx);
      if (pinned_slot >= 0) snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Unpin (slot %d)", pinned_slot + 1);
      else                  snprintf(_ctx_pin_item, sizeof(_ctx_pin_item), "Pin to dial"); }
    { const uint8_t row = chanctl::scope(_task->getNodePrefs(), ch_idx);
      snprintf(_ctx_scope_item, sizeof(_ctx_scope_item), "Scope: %s",
               row ? the_mesh.scopeList().name(row - 1) : "Default"); }
    _ctx_menu.begin("Channel options", 6);
    _ctx_menu.addItem("Mark all read");
    _ctx_menu.addValueItem(_ctx_notif_item);
    _ctx_menu.addValueItem(_ctx_melody_item);
    _ctx_fav_idx = 3;
    _ctx_menu.addValueItem(_ctx_fav_item);
    _ctx_save_idx = -1;
#ifdef HIST_FLASH
    if (_task->getNodePrefs() && _task->getNodePrefs()->msg_keep) {   // only with the log on
      snprintf(_ctx_save_item, sizeof(_ctx_save_item),
               chanctl::saved(_task->getNodePrefs(), ch_idx) ? "Save: ON" : "Save: OFF");
      _ctx_save_idx = 4;
      _ctx_menu.addValueItem(_ctx_save_item);
    }
#endif
    _ctx_menu.addItem(_ctx_scope_item);
    _ctx_menu.addItem(_ctx_pin_item);
    _ctx_menu.addItem("Edit");
    _ctx_menu.addItem("Delete");
    _ctx_dirty = false;
    return true;
  }
  return false;
}
