#pragma once
// Included into UITask.cpp only (see the note on screen fragments there).
// Home: the clock, favourites, status, Bluetooth ... pages, and the lock screen.

// The time on a tall portrait panel (e-ink in portrait -- height > width): HH
// and MM stacked on two lines in the splash's slanted lettering, as the wide
// panels' big clock: as wide as the panel allows, the two lines no more than
// ~45% of its height (the date and the month go under them). Returns the y
// just below it.
static int drawClockTall(DisplayDriver& d, int top_y, const struct tm* ti, bool h12) {
  int hh = ti->tm_hour;
  const char* ap = nullptr;
  if (h12) { ap = (hh < 12) ? "AM" : "PM"; hh %= 12; if (hh == 0) hh = 12; }
  const int cx = d.width() / 2;
  char hbuf[4], mbuf[4];
  snprintf(hbuf, sizeof(hbuf), h12 ? "%d" : "%02d", hh);
  snprintf(mbuf, sizeof(mbuf), "%02d", ti->tm_min);

  int sc = (d.height() * 45 / 100) / (2 * lettering::LOGO_H);
  while (sc > 1 && info::letteringW("00", sc, sc + 1) > d.width() - 2 * sc) sc--;
  if (sc < 1) sc = 1;
  const int h = lettering::LOGO_H * sc, gap = sc + 1;
  int y = top_y;
  info::lettering(d, cx - info::letteringW(hbuf, sc, gap) / 2, y, hbuf, sc, gap);  y += h + 2 * sc;
  info::lettering(d, cx - info::letteringW(mbuf, sc, gap) / 2, y, mbuf, sc, gap);  y += h + sc;
  if (ap) {
    d.setTextSize(2);
    d.drawTextCentered(cx, y, ap);
    y += d.getLineHeight() + 1;
  }
  d.setTextSize(1);
  return y;
}

// The big clock on wide panels (Home clock page, lock screen): HH:MM in the
// splash's slanted lettering (ui-core/Lettering.h) at `sc`, centred on cx. A
// 12-hour clock drops the leading zero, closes the gaps up and sets AM / PM
// beside the digits on their baseline -- "12:59 PM" still fits 128 px.
struct BigClock { int sc, h; };
static BigClock bigClockSize(const DisplayDriver& d) {
  int sc = d.width() / 64;
  if (sc > d.height() / 32) sc = d.height() / 32;
  if (sc < 1) sc = 1;
  return { sc, lettering::LOGO_H * sc };
}
// The big clock's text, gaps and AM / PM, measured: its full width (w), the
// digits' (dw). draw() puts its left edge at x.
struct BigClockText {
  char t[8];
  const char* ap = nullptr;
  int sc, gap, dw, w;
  BigClockText(DisplayDriver& d, const struct tm* ti, bool h12, int scale) : sc(scale), gap(scale + 1) {
    if (h12) {
      int hh = ti->tm_hour % 12; if (hh == 0) hh = 12;
      snprintf(t, sizeof(t), "%d:%02d", hh, ti->tm_min);
      ap = ti->tm_hour < 12 ? "AM" : "PM";
      gap = sc / 2;
    } else {
      snprintf(t, sizeof(t), "%02d:%02d", ti->tm_hour, ti->tm_min);
    }
    dw = lettering::textW(t, 0) * sc + gap * ((int)strlen(t) - 1);
    d.setTextSize(1);
    w = dw + (ap ? (int)d.getTextWidth(ap) + 2 * sc : 0);
  }
  void draw(DisplayDriver& d, int x, int y) const {
    info::lettering(d, x, y, t, sc, gap);
    if (!ap) return;
    d.setCursor(x + dw + 2 * sc, y + lettering::LOGO_H * sc - d.getLineHeight() + 1);
    d.print(ap);
  }
};
static void drawBigClock(DisplayDriver& d, int cx, int y, const struct tm* ti, bool h12, int sc) {
  BigClockText bt(d, ti, h12, sc);
  bt.draw(d, cx - bt.w / 2, y);
}

static const char* const WDAY[] = {"Sun","Mon","Tue","Wed","Thu","Fri","Sat"};
static const char* const MON[]  = {"Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec"};

// ── HomeScreen ────────────────────────────────────────────────────────────────

// A dashboard field in this display's font (ui-core/Telemetry.h); `nouns`
// when it stands without its label (the lock screen). "" for none.
static void formatDashVal(uint8_t field, char* val, int val_len, uint16_t batt_mv, uint16_t low_batt_mv,
                          int unread, bool unread_overflow, bool imperial, CayenneLPP* lpp, bool nouns) {
  val[0] = '\0';
  if (field == telemetry::NONE || field >= telemetry::COUNT) return;
  telemetry::Style st = L1_TELEMETRY;
  st.nouns = nouns;
  if (nouns) st.gps_dp = 2;   // no label: room for the position is shorter
  telemetry::Inputs in;
  in.batt_mv = batt_mv;
  in.low_batt_mv = low_batt_mv;
  in.nodes = the_mesh.getNumContacts();
  in.unread = unread;
  in.unread_more = unread_overflow;
  in.imperial = imperial;
#if ENV_INCLUDE_GPS == 1
  in.loc = sensors.getLocationProvider();
  { NodePrefs* np = the_mesh.getNodePrefs(); in.gps_on = np && np->gps_enabled; }   // off: no stale satellite count
#endif
  if (telemetry::isSensor(field)) {
    if (!lpp) {
      static CayenneLPP s_lpp(200); s_lpp.reset();
      { CardKBBusLock bus_lock; queryLocalSensors(sensors, board.getMCUTemperature(), s_lpp); }
      lpp = &s_lpp;
    }
    in.lpp = lpp->getBuffer();
    in.lpp_len = lpp->getSize();
  }
  telemetry::text(field, in, st, val, val_len);
}


class HomeScreen : public UIScreen {
  enum HomePage {
    CLOCK,
    FAVOURITES,
    STATUS,      // radio, GPS, power, mesh at a glance (took over Radio / GPS / Sensors)
    BLUETOOTH,
    ADVERT,
    SETTINGS,
    MAP,
    TOOLS,
    QUICK_MSG,
    SHUTDOWN,
    Count,   // keep before LOCK — navigable-page count
    LOCK     // lock screen: full clock + dashboard view, not a navigable page
  };

  // Selected slot on the Favourites page (0..FAVOURITES_COUNT - 1).
  uint8_t _fav_sel = 0;

  // Slot payload (pubkey prefix, or channel index in byte 0 — see the slot's
  // kind), or nullptr when the slot is empty.
  const uint8_t* favSlotPrefix(int slot) const {
    NodePrefs* p = _task->getNodePrefs();
    if (!p || _task->isFavouriteSlotEmpty(slot)) return nullptr;
    return p->favourite_contacts[slot];
  }

  // Unpin/Replace menu for a filled tile. Choosing what to pin is the Messages
  // screen's job (see UITask::pickFavouriteTarget) -- the dial doesn't carry a
  // second browser for contacts, rooms and channels.
  PopupMenu _tile_menu;
  int       _pin_target_slot = -1;

  UITask* _task;
  mesh::RTCClock* _rtc;
  SensorManager* _sensors;
  NodePrefs* _node_prefs;
  uint8_t _page;
  uint8_t _prev_page;   // home page restored when the device unlocks
  bool _shutdown_init;

  // Sideways slide between pages (OLED; drivers that can't read their frame
  // back just cut over). _slide_dir is +1 (new page from the right), -1, or 0.
  static const unsigned long SLIDE_MS = 220;
  int8_t        _slide_dir = 0;
  uint8_t       _slide_from = 0;
  unsigned long _slide_t0 = 0;

  // First row under the header and the page-icon row (see render()).
  static int contentTop(DisplayDriver& d) {
    const int pg_half = (PAGE_ICON_PX * statusIconScale(d) + 1) / 2;
    return statusBarH(d) + 2 * pg_half + 4;
  }
  // 0..1 eased progress of the running slide, or -1 when none.
  float slideProgress() {
    if (!_slide_dir) return -1;
    unsigned long el = millis() - _slide_t0;
    if (el >= SLIDE_MS) { _slide_dir = 0; return -1; }
    float t = 1.0f - (float)el / SLIDE_MS;
    return 1.0f - t * t * t;           // ease-out: quick start, soft landing
  }
  void turnPage(int dir) {
    const uint8_t from = _page;
    _page = navPage(_page, dir);
    if (_page == from) return;
    _quick_sel = 0;   // a quick panel starts on its first row
    DisplayDriver* d = _task->getDisplay();
    // Pages with the header slide below it; the full-screen clock slides whole.
    const bool whole = from == CLOCK || _page == CLOCK;
    if (d && d->slideBegin(whole ? 0 : contentTop(*d))) {
      _slide_dir = (int8_t)dir; _slide_from = from; _slide_t0 = millis();
    }
  }

  unsigned long _advert_sent_ms = 0;   // last advert sent from this page (0: none yet)

  // ── Quick panels: an icon page as a few rows with margins round
  // them, the last one opening the full screen. Up/Down picks a row, Enter
  // acts on it. The rows are the Settings list's own (value, switch, bar).
  enum QKind : uint8_t { Q_VALUE, Q_SWITCH, Q_BAR, Q_BADGE, Q_MORE };
  enum QAct  : uint8_t { QA_NONE, QA_MORE, QA_BRIGHTNESS, QA_GPS, QA_CHAT, QA_BT,
                         QA_ADVERT, QA_AUTO_ADVERT, QA_TOOL, QA_LOCK, QA_HIBERNATE };
  struct QRow {
    const char* label;
    char    value[16];
    QKind   kind;
    QAct    act;
    uint8_t n;          // switch on, bar level, unread count, QA_TOOL's action
    bool    overflow;   // unread past what the badge can count
    bool    is_ch;      // QA_CHAT: a channel (ch) or a DM / room (prefix)
    uint8_t ch;
    uint8_t prefix[4];
    char    name[24];   // QA_CHAT: the label's storage
  };
  static const int QUICK_MAX = 8;
  // How many rows a quick panel has room for (at least the 3 a 128x64 OLED
  // fits): a taller screen lists more recent chats.
  int quickFit() const {
    DisplayDriver* d = _task->getDisplay();
    if (!d) return 3;
    const int n = (d->height() - contentTop(*d) - 5) / (d->lineStep() + 1);
    return n < 3 ? 3 : n > QUICK_MAX ? QUICK_MAX : n;
  }
  uint8_t _quick_sel = 0;
  bool    _quick_edit = false;   // the selected row is being changed with Left/Right
  bool    _quick_dirty = false;  // ...and has moved: saved once when it ends
  static bool isQuickPage(int p) {
    return p == SETTINGS || p == QUICK_MSG || p == BLUETOOTH || p == ADVERT || p == TOOLS || p == SHUTDOWN;
  }

  // What a tool is doing now, for its row on the Tools panel ("" when it has
  // nothing running worth telling).
  void toolState(ToolsScreen::Action a, char* buf, int n) {
    buf[0] = 0;
    switch (a) {
      case ToolsScreen::ACT_TRAIL: {
        TrailStore& tr = _task->trail();
        if (tr.isActive()) geo::fmtDist(buf, n, tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
        else strncpy(buf, "Off", n);
        break;
      }
      case ToolsScreen::ACT_LIVESHARE:
        strncpy(buf, _node_prefs && _node_prefs->loc_share_enabled ? "On" : "Off", n); break;
      case ToolsScreen::ACT_LOCATOR:
        strncpy(buf, _node_prefs && _node_prefs->locator_enabled ? "On" : "Off", n); break;
      case ToolsScreen::ACT_REPEATER:
        strncpy(buf, _node_prefs && _node_prefs->client_repeat ? "On" : "Off", n); break;
      case ToolsScreen::ACT_AUTOADVERT:
        strncpy(buf, AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::indexOf(
                       _node_prefs ? _node_prefs->advert_auto_interval_sec : 0)], n); break;
      case ToolsScreen::ACT_CLOCK:
        if (_task->isTimerRunning()) {
          const uint32_t s = (_task->timerRemainingMs() + 999) / 1000;
          if (s >= 3600) snprintf(buf, n, "%lu:%02lu:%02lu", (unsigned long)(s / 3600), (unsigned long)(s / 60 % 60), (unsigned long)(s % 60));
          else           snprintf(buf, n, "%lu:%02lu", (unsigned long)(s / 60), (unsigned long)(s % 60));
        }
        break;
      default: break;
    }
    buf[n - 1] = 0;
  }

  // The `max` newest conversations, DM / room or channel, newest first.
  int recentChats(QRow* out, int max) {
    MessageHistory& h = _task->core().history;
    struct Cand { bool is_ch; uint8_t ch; uint8_t prefix[4]; uint32_t ts; } c[2 * QUICK_MAX];
    if (max > QUICK_MAX) max = QUICK_MAX;
    int nc = 0;
    for (int j = 0; j < h.dmHistCount() && nc < max; j++) {
      const DmHistEntry& e = h.dmAtPos(h.dmHistPosNewest(j));
      bool seen = false;
      for (int k = 0; k < nc; k++) if (memcmp(c[k].prefix, e.prefix, 4) == 0) seen = true;
      if (seen) continue;
      c[nc].is_ch = false; c[nc].ch = 0; memcpy(c[nc].prefix, e.prefix, 4); c[nc].ts = e.timestamp; nc++;
    }
    const int nd = nc;
    for (int j = 0; j < h.chHistCount() && nc < nd + max; j++) {
      const ChHistEntry& e = h.chAtPos(h.chHistPosNewest(j));
      bool seen = false;
      for (int k = nd; k < nc; k++) if (c[k].ch == e.ch_idx) seen = true;
      if (seen) continue;
      c[nc].is_ch = true; c[nc].ch = e.ch_idx; c[nc].ts = e.timestamp; nc++;
    }
    for (int a = 0; a < nc; a++)            // newest first
      for (int b = a + 1; b < nc; b++)
        if (c[b].ts > c[a].ts) { Cand t = c[a]; c[a] = c[b]; c[b] = t; }
    const uint32_t now = rtc_clock.getCurrentTime();
    int n = 0;
    for (int k = 0; k < nc && n < max; k++) {
      QRow& r = out[n];
      memset(&r, 0, sizeof(r));
      r.act = QA_CHAT; r.is_ch = c[k].is_ch;
      if (r.is_ch) {
        ChannelDetails cd;
        if (!the_mesh.getChannel(c[k].ch, cd) || !cd.name[0]) continue;
        r.ch = c[k].ch;
        snprintf(r.name, sizeof(r.name), "#%s", chanctl::bareName(cd.name));
        r.n = _task->getChannelUnread(r.ch);
        r.overflow = r.n > 0 && _task->getChannelUnreadOverflow(r.ch);
      } else {
        ContactInfo ci;
        if (!MessageHistory::contactByPrefix(c[k].prefix, ci)) continue;
        memcpy(r.prefix, c[k].prefix, 4);
        snprintf(r.name, sizeof(r.name), "%s", ci.name);
        r.n = _task->getDMUnread(ci.id.pub_key);
        r.overflow = r.n > 0 && _task->getDMUnreadOverflow(ci.id.pub_key);
      }
      r.label = r.name;
      if (r.n > 0) r.kind = Q_BADGE;
      else { r.kind = Q_VALUE; geo::fmtAgeShort(r.value, sizeof(r.value), now, c[k].ts); }
      n++;
    }
    return n;
  }

  int buildQuick(int page, QRow* rows) {
    int n = 0;
    auto add = [&](const char* label, QKind k, QAct a, uint8_t v) {
      QRow& r = rows[n++];
      memset(&r, 0, sizeof(r));
      r.label = label; r.kind = k; r.act = a; r.n = v;
    };
    // A panel with a whole screen behind it lists the way in first, its
    // shortcuts after.
    if (page == SETTINGS) {
      add("All settings", Q_MORE, QA_MORE, 0);
#if FEAT_BRIGHTNESS_SETTING
      add("Brightness", Q_BAR, QA_BRIGHTNESS, _node_prefs ? _node_prefs->display_brightness + 1 : 0);
#endif
#if ENV_INCLUDE_GPS == 1
      if (sensors.getLocationProvider()) add("GPS", Q_SWITCH, QA_GPS, _task->getGPSState() ? 1 : 0);
#endif
    } else if (page == QUICK_MSG) {
      add("All messages", Q_MORE, QA_MORE, 0);
      n += recentChats(rows + n, quickFit() - 1);
    } else if (page == BLUETOOTH) {
      const bool on = _task->isBluetoothEnabled();
      add("Bluetooth", Q_SWITCH, QA_BT, on ? 1 : 0);
      add("Phone", Q_VALUE, QA_NONE, 0);
      char* v = rows[n - 1].value;
      if (!on)                            strcpy(v, "-");
      else if (_task->isBLEConnected())   strcpy(v, "Connected");
      // The pairing PIN is BLE-specific: show it while BLE is on but not yet
      // bonded. (Not a plain isConnected(): a USB port makes that true.)
      else if (the_mesh.getBLEPin() != 0) snprintf(v, sizeof(rows[0].value), "PIN %d", (int)the_mesh.getBLEPin());
      else                                strcpy(v, "Waiting");
    } else if (page == ADVERT) {
      add("Send now", Q_VALUE, QA_ADVERT, 0);
      if (_advert_sent_ms) {   // when this page last sent one
        const unsigned long ago = (millis() - _advert_sent_ms) / 60000UL;
        char* v = rows[n - 1].value;
        if (ago == 0)      strcpy(v, "sent");
        else if (ago < 60) snprintf(v, sizeof(rows[0].value), "%lum ago", ago);
        else               snprintf(v, sizeof(rows[0].value), "%luh ago", ago / 60);
      }
      add("Auto", Q_VALUE, QA_AUTO_ADVERT, 0);
      strcpy(rows[n - 1].value, AutoAdvertScreen::OPT_LABELS[AutoAdvertScreen::indexOf(
                                  _node_prefs ? _node_prefs->advert_auto_interval_sec : 0)]);
    } else if (page == TOOLS) {
      add("All tools", Q_MORE, QA_MORE, 0);
      for (int i = 0; i < ToolsScreen::RECENT_MAX; i++) {
        const ToolsScreen::Action a = ToolsScreen::s_recent[i];
        add(ToolsScreen::labelOf(a), Q_VALUE, QA_TOOL, (uint8_t)a);
        toolState(a, rows[n - 1].value, sizeof(rows[0].value));
      }
    } else if (page == SHUTDOWN) {
      add("Lock screen", Q_VALUE, QA_LOCK, 0);
#ifndef UI_NO_HIBERNATE   // a board with a power switch (the L1): a pocket can't hibernate it by mistake
      add("Hibernate", Q_VALUE, QA_HIBERNATE, 0);
      // The charge left, which is what you weigh before switching off.
      const int mv = _task->getBattMilliVolts();
      snprintf(rows[n - 1].value, sizeof(rows[0].value), "%d%%%s",
               battery::percent(mv, _node_prefs ? (int)_node_prefs->low_batt_mv : 0),
               board.isExternalPowered() ? " USB" : "");
#endif
    }
    return n;
  }

  // Small arrows either side of x0..x0+w while Left/Right change a row.
  static void drawEditArrows(DisplayDriver& d, int x0, int w, int cy, int s) {
    for (int i = 0; i < 3; i++) {
      d.fillRect(x0 - 6 * s + i * s, cy - i * s, s, (2 * i + 1) * s);       // ◂
      d.fillRect(x0 + w + 5 * s - i * s, cy - i * s, s, (2 * i + 1) * s);   // ▸
    }
  }

  // The rows, from a gap below the page icons and inset from both sides.
  int drawQuickPanel(DisplayDriver& d, int top, QRow* rows, int n) {
    const int W = d.width(), m = W / 16, lh = d.getLineHeight(), step = d.lineStep() + 1;
    const int reserve = m - 2;                       // so a row's right edge is W - m
    if (_quick_sel >= n) _quick_sel = n - 1;
    int y = top + 5, mq = 0;
    for (int i = 0; i < n; i++, y += step) {
      const QRow& r = rows[i];
      const bool sel = i == _quick_sel;
      d.setColor(UIColor::primary_txt);
      if (sel) { d.fillSoftRect(m - 3, y - 1, W - 2 * m + 6, lh + 1); d.setColor(UIColor::window_bkg); }
      switch (r.kind) {
        case Q_SWITCH: info::switchRow(d, y, r.label, r.n != 0, sel, reserve, m); break;
        case Q_BAR: {
          d.setCursor(m, y); d.print(r.label);
          const int box = lh - 3, gap = 2, s = miniIconScale(d), bars = 5 * box + 4 * gap;
          const bool editing = sel && _quick_edit;
          const int x0 = W - m - bars - (editing ? 6 * s : 0);
          if (editing) drawEditArrows(d, x0, bars, y + 1 + box / 2, s);
          for (int k = 0; k < 5; k++) {
            const int bx = x0 + k * (box + gap);
            if (k < r.n) d.fillRect(bx, y + 1, box, box); else d.drawRect(bx, y + 1, box, box);
          }
          break;
        }
        case Q_BADGE: {
          const int bw = d.unreadBadgeWidth(r.n, r.overflow);
          d.drawTextEllipsized(m, y, W - 2 * m - bw - 4, r.label);
          d.drawUnreadBadge(W - m, y, r.n, sel, r.overflow);
          break;
        }
        case Q_MORE:
          d.setCursor(m, y); d.print(r.label);
          d.drawTextRightAlign(W - m, y, ">");
          break;
        default:
          if (sel && _quick_edit) {   // a value being changed: arrows round it
            const int s = miniIconScale(d), vw = d.getTextWidth(r.value), x0 = W - m - vw - 6 * s;
            d.setCursor(m, y); d.print(r.label);
            d.setCursor(x0, y); d.print(r.value);
            drawEditArrows(d, x0, vw, y + 1 + (lh - 3) / 2, s);
          } else {
            int q = info::valueRow(d, y, r.label, r.value, sel, reserve, m);
            if (q > 0) mq = q;
          }
      }
      d.setColor(UIColor::primary_txt);
    }
    return mq;
  }

  void quickAct(int page, const QRow& r) {
    switch (r.act) {
      case QA_NONE: break;
      case QA_MORE:
        if (page == SETTINGS)    _task->gotoSettingsScreen();
        else if (page == TOOLS)  _task->gotoToolsScreen();
        else                     _task->gotoMessagesScreen();
        break;
      case QA_BRIGHTNESS:
      case QA_AUTO_ADVERT: _quick_edit = true; break;   // Left/Right change it, Enter / Back keep it
      case QA_BT:
        if (_task->isBluetoothEnabled()) _task->disableBluetooth(); else _task->enableBluetooth();
        break;
      case QA_ADVERT:
        _task->notify(UIEventType::ack);
        if (the_mesh.advert()) _advert_sent_ms = millis();   // the row says so
        else _task->showAlert("Advert failed", 1000);
        break;
      case QA_TOOL: ToolsScreen::run(_task, (ToolsScreen::Action)r.n, true); break;
      case QA_LOCK: _task->lockScreen(); break;
      case QA_HIBERNATE: _shutdown_init = true; break;   // waits for the button to be released
      case QA_GPS: _task->toggleGPS(false); break;
      case QA_CHAT:
        if (r.is_ch) { _task->openChannelHistory(r.ch); break; }
        {
          ContactInfo ci;
          if (!MessageHistory::contactByPrefix(r.prefix, ci)) break;
          if (ci.type == ADV_TYPE_ROOM) _task->openRoomServer(ci); else _task->openContactDM(ci);
        }
        break;
    }
  }

  int pageBit(int page) const {
    if (page == CLOCK)      return NodePrefs::HPB_CLOCK;
    if (page == FAVOURITES) return NodePrefs::HPB_FAVOURITES;
    if (page == STATUS)    return NodePrefs::HPB_RADIO;   // the old Radio slot
    if (page == BLUETOOTH) return NodePrefs::HPB_BLUETOOTH;
    if (page == ADVERT)    return NodePrefs::HPB_ADVERT;
    if (page == TOOLS)     return NodePrefs::HPB_TOOLS;
    if (page == SHUTDOWN)  return NodePrefs::HPB_SHUTDOWN;
    if (page == MAP)       return NodePrefs::HPB_MAP;
    return -1;  // SETTINGS, QUICK_MSG always visible (no mask bit)
  }

  // Maps page_order bit-index back to the HomePage enum value for this build.
  // Returns -1 if the page is not compiled in.
  int bitToPage(int bit) const {
    switch (bit) {
      case NodePrefs::HPB_CLOCK:      return CLOCK;
      case NodePrefs::HPB_FAVOURITES: return FAVOURITES;
      case NodePrefs::HPB_RADIO:     return STATUS;
      case NodePrefs::HPB_BLUETOOTH: return BLUETOOTH;
      case NodePrefs::HPB_ADVERT:    return ADVERT;
      case NodePrefs::HPB_TOOLS:     return TOOLS;
      case NodePrefs::HPB_SHUTDOWN:  return SHUTDOWN;
      case NodePrefs::HPB_SETTINGS:  return SETTINGS;
      case NodePrefs::HPB_QUICK_MSG: return QUICK_MSG;
      case NodePrefs::HPB_MAP:       return MAP;
      default: return -1;
    }
  }

  bool isPageVisible(int page) const {
    int bit = pageBit(page);
    if (bit < 0) return true;
    uint16_t mask = (_node_prefs && _node_prefs->home_pages_mask) ? _node_prefs->home_pages_mask : NodePrefs::HP_ALL;
    // Status stands in for the Radio, GPS and Sensors pages: shown if any was.
    if (page == STATUS) return (mask & (NodePrefs::HP_RADIO | NodePrefs::HP_GPS | NodePrefs::HP_SENSORS)) != 0;
    return (mask >> bit) & 1;
  }

  // Build ordered list of all visible pages, respecting page_order when set.
  // Returns count; out[] receives HomePage enum values.
  int buildVisibleOrder(int* out) const {
    int n = 0;
    bool custom = _node_prefs && _node_prefs->page_order_set == NodePrefs::PAGE_ORDER_MAGIC;
    if (custom) {
      for (int i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++) {
        uint8_t v = _node_prefs->page_order[i];
        if (v < 1 || v > NodePrefs::HPB_COUNT) break;
        int pg = bitToPage(v - 1);
        if (pg >= 0 && pg < (int)Count && isPageVisible(pg)) out[n++] = pg;
      }
      // Append any visible page missing from page_order (handles corrupted/migrated prefs)
      for (int pg = 0; pg < (int)Count; pg++) {
        if (!isPageVisible(pg)) continue;
        bool found = false;
        for (int i = 0; i < n; i++) if (out[i] == pg) { found = true; break; }
        if (!found) out[n++] = pg;
      }
    } else {
      for (int pg = 0; pg < (int)Count; pg++)
        if (isPageVisible(pg)) out[n++] = pg;
    }
    return n;
  }

  int navPage(int from, int dir) const {
    int order[(int)Count]; int n = buildVisibleOrder(order);
    if (n == 0) return from;
    int cur = 0;
    for (int i = 0; i < n; i++) if (order[i] == from) { cur = i; break; }
    return order[((cur + dir) % n + n) % n];
  }

  // reserve_left: how much width from x=0 must stay clear of status icons --
  // the time / node name on the pages (name_min below); the lock page passes
  // 0, its clock sits under the bar. -1 = use the normal name reserve.
  int renderBatteryIndicator(DisplayDriver& display, uint16_t batteryMilliVolts, int reserve_left = -1) {
    int low_mv = _node_prefs ? (int)_node_prefs->low_batt_mv : 0;
    int pct = battery::percent((int)batteryMilliVolts, low_mv);

    uint8_t mode = battery::mode(_node_prefs ? _node_prefs->batt_display_mode : 0);

    display.setTextSize(1);
    display.setColor(UIColor::primary_txt);

    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int sis     = statusIconScale(display);   // STATUS_ICON_SCALE, or the font's
    const int bar_h   = statusBarH(display);
    const int ind     = STATUS_ICONS_SCALED ? 7 * sis + 2 : cw + 2;    // indicator slot width
    const int ind_h   = (bar_h > lh) ? bar_h - 2 : (display.isSingleFont() ? lh - 2 : lh);
    const int text_y  = (bar_h > lh) ? (bar_h - lh) / 2 : 0;   // % / V text centred in a tall bar
    const int ind_gap = display.pixelScale() > 1 ? 3 : 1;  // gap between indicator boxes

    int battLeftX;
    if (mode == battery::PERCENT) {
      char buf[6];
      snprintf(buf, sizeof(buf),"%d%%", pct);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, text_y);
      display.print(buf);
    } else if (mode == battery::VOLTAGE) {
      char buf[8];
      snprintf(buf, sizeof(buf),"%u.%02uV", batteryMilliVolts / 1000, (batteryMilliVolts % 1000) / 10);
      battLeftX = display.width() - display.getTextWidth(buf) - 1;
      display.setCursor(battLeftX, text_y);
      display.print(buf);
    } else {  // icon — scales with lh, same box height as the status icons beside it (ind_h)
      const int iconH = ind_h;
      const int iconW = (bar_h > lh) ? ind_h * 2 : lh * 2;
      const int t  = display.sepH();   // outline: 1px, 2px on a doubled panel
      const int bm = 2 * t;            // inner margin: the outline plus as much gap
      battLeftX = display.width() - iconW - 3;
      // The outline with its corner pixels left out, softly rounded.
      display.drawSoftRect(battLeftX, 0, iconW, iconH);
      // Nub height/2, vertically centred by remaining-space/2 rather than a flat
      // iconH/4 margin — the flat form only centres when iconH is a multiple of
      // 4 (true for the old built-in font's lh=8, false for misc-fixed's 7/9),
      // so it visibly drifted off-centre once the box height changed.
      const int nub_h = iconH / 2;
      display.fillRect(battLeftX + iconW, (iconH - nub_h) / 2, 2 * t, nub_h);
      int fillW = (pct * (iconW - 2 * bm)) / 100;
      display.fillRect(battLeftX + bm, bm, fillW, iconH - 2 * bm);
    }

    // Secondary status icons, laid out right→left in PRIORITY order so a crowded
    // bar sheds its least-important cues instead of crushing the time. Once an
    // icon won't fit above the reserved area, every lower-priority icon after
    // it is dropped too (the list is ordered high→low).
    //
    // Priority: power > BT > GPS fix > alarm > mute > auto-advert > trail > live-share >
    // repeater. Battery (drawn above) is always rightmost. The background modes
    // (advert / trail / live-share / repeater) stay outside any BT gate — they
    // keep running with Bluetooth off, so their cue must not vanish with it.
    LocationProvider* loc = _sensors ? _sensors->getLocationProvider() : nullptr;
    bool gps_on  = loc && _node_prefs && _node_prefs->gps_enabled;
    bool mute_on = false;
#ifdef PIN_BUZZER
    mute_on = _task->isBuzzerQuiet();
#endif
    // Plain glyphs, no blinking: each one shows a state by its shape.
    // Bluetooth only once a phone is connected (it's on nearly always; the
    // Bluetooth page tells the rest), GPS as a broken reticle until a fix.
    const bool gps_fix = gps_on && loc->isValid();
    struct Sicon { bool active; const MiniIcon* icon; };
    const Sicon icons[] = {
      { board.isExternalPowered(),                                 &ICON_CHARGE },   // USB power: next to the battery
      { _task->isBluetoothEnabled() && _task->isBLEConnected(),       &ICON_BLUETOOTH },
      { gps_on,                                                    gps_fix ? &ICON_GPS : &ICON_GPS_SEARCH },
      { _node_prefs && _node_prefs->alarm_on,                      &ICON_ALARM },
      { mute_on,                                                   &ICON_MUTE },
      { _node_prefs && _node_prefs->advert_auto_interval_sec > 0,  &ICON_ADVERT },
      { _task->trail().isActive(),                                 &ICON_TRAIL },
      { _node_prefs && _node_prefs->loc_share_enabled,             &ICON_MAP_CONTACT },
      { _node_prefs && _node_prefs->client_repeat,                 &ICON_REPEATER },
    };

    int x = battLeftX;
    const int name_min = (reserve_left >= 0) ? reserve_left : display.getCharWidth() * 5;
    // Settings > Message alert Compact: the envelope and its count, first by the
    // battery; its place is kept through the blink so nothing beside it jumps.
    if (const int n = _task->msgBadgeCount()) {
      const int ix = x - mailCountWidth(display, n) - ind_gap - 2 * miniIconScale(display);   // the icons' boxes pad theirs
      if (ix >= name_min) {
        if (_task->msgBadgeLit()) drawMailCount(display, ix, 0, ind_h, n);
        _task->msgBadgeInBar();
        x = ix;
      }
    }
    for (const Sicon& s : icons) {
      if (!s.active) continue;
      int ix = x - ind - ind_gap;
      if (ix < name_min) break;                        // out of room — drop this + all lower priority
      if (STATUS_ICONS_SCALED) drawSlotIconScaled(display, ix, ind, ind_h, *s.icon, sis);
      else                       drawSlotIcon(display, ix, ind, ind_h, *s.icon);
      x = ix;
    }
    return x;
  }

  CayenneLPP sensors_lpp;
  int sensors_nb = 0;
  unsigned long next_sensors_refresh = 0;   // 0: read on the next call

  void refresh_sensors() {
    if (!next_sensors_refresh || (int32_t)(millis() - next_sensors_refresh) >= 0) {
      sensors_lpp.reset();
      sensors_nb = 0;
      sensors_lpp.addVoltage(TELEM_CHANNEL_SELF, (float)board.getBattMilliVolts() / 1000.0f);
      { CardKBBusLock bus_lock; queryLocalSensors(sensors, board.getMCUTemperature(), sensors_lpp); }
      LPPReader reader (sensors_lpp.getBuffer(), sensors_lpp.getSize());
      uint8_t channel, type;
      while(reader.readHeader(channel, type)) {
        reader.skipData(type);
        sensors_nb ++;
      }
#if AUTO_OFF_MILLIS > 0
      next_sensors_refresh = (millis() + 5000) | 1; // refresh sensor values every 5 sec
#else
      next_sensors_refresh = (millis() + 60000) | 1; // refresh sensor values every 1 min
#endif
    }
  }

public:
  HomeScreen(UITask* task, mesh::RTCClock* rtc, SensorManager* sensors, NodePrefs* node_prefs)
     : _task(task), _rtc(rtc), _sensors(sensors), _node_prefs(node_prefs), _page(0),
       _prev_page(0), _shutdown_init(false), sensors_lpp(200) {  }

  void showBluetoothPage() {
    _page = BLUETOOTH;
    _quick_sel = 0;
  }

  void showGpsPage() {
    _page = STATUS;
    _quick_sel = 0;
  }

  // Flips home screen between LOCK page and the page that was showing before locking
  void setLocked(bool locked) {
    if (locked) {
      if (_page != LOCK) _prev_page = _page;
      _page = LOCK;
    } else {
      _page = _prev_page;
    }
  }

  void poll() override {
    if (_shutdown_init && !_task->isButtonPressed()) {  // must wait for USR button to be released
      // _shutdown_init is never cleared elsewhere -- on real hardware
      // that's harmless because _board->powerOff() halts the MCU, so
      // there is no next poll() tick to matter. In the sim, powerOff()
      // is a deliberate no-op (no real hardware to power off, see
      // SimMainBoard.h), so without this the instance keeps running and
      // this branch re-fires shutdown() -> _display->turnOff() on every
      // single tick forever, repeatedly blacking out its canvas -- which
      // fights with a freshly reset instance's own boot splash trying to
      // render onto that same (simInstanceTag-keyed) canvas element.
      _shutdown_init = false;
      _task->shutdown();
    }
  }

  // Compact map preview for the Home "Map" page: own position, the GPS trail,
  // and live-tracked contacts (◆) folded into one auto-scaled box. A simplified
  // cousin of TrailScreen's map (no grid/labels, no break markers) so the home
  // carousel stays light. Returns false (and draws nothing) when there's
  // nothing to show.
  bool drawMapPreview(DisplayDriver& display, int ax, int ay, int aw, int ah) {
    if (aw < 8 || ah < 8) return false;
    bool init = false;
    int32_t mnla = 0, mxla = 0, mnlo = 0, mxlo = 0;
    auto fold = [&](int32_t la, int32_t lo) {
      if (!init) { mnla = mxla = la; mnlo = mxlo = lo; init = true; }
      else { if (la < mnla) mnla = la; if (la > mxla) mxla = la;
             if (lo < mnlo) mnlo = lo; if (lo > mxlo) mxlo = lo; }
    };
    TrailStore& tr = _task->trail();
    if (!tr.empty()) { int32_t a, b, c, d; tr.boundingBox(a, b, c, d); fold(a, b); fold(c, d); }
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++)
      if (lt.isActive(i, now)) fold(lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6);
    int32_t mla, mlo;
    bool have_gps = _task->currentLocation(mla, mlo);
    if (have_gps) fold(mla, mlo);
    int32_t tla, tlo;
    bool have_tgt = _task->activeTargetPos(tla, tlo);   // active Locator/Nav target
    if (have_tgt) fold(tla, tlo);
    if (!init) return false;

    // North marker — top-right, the same mini-icon as the full Trail map.
    display.setColor(UIColor::primary_txt);
    {
      const int ns = miniIconScale(display);
      miniIconDrawTop(display, ax + aw - ICON_MAP_NORTH.w * ns - 1, ay + 1, ICON_MAP_NORTH);
    }

    int cx = ax + aw / 2, cy = ay + ah / 2;
    // Degenerate: one coincident point — just centre the markers.
    if (mnla == mxla && mnlo == mxlo) {
      for (int i = 0; i < LiveTrackStore::CAPACITY; i++)
        if (lt.isActive(i, now)) { miniIconDrawCentered(display, cx, cy, ICON_MAP_CONTACT); break; }
      if (have_gps || !tr.empty()) miniIconDrawCentered(display, cx, cy, ICON_MAP_CURRENT);
      if (have_tgt) miniIconDrawCentered(display, cx, cy, ICON_MAP_TARGET);   // highlight on top
      return true;
    }
    float avg_lat_rad = ((mnla + mxla) / 2.0e6f) * (float)M_PI / 180.0f;
    float lon_scale = cosf(avg_lat_rad); if (lon_scale < 0.05f) lon_scale = 0.05f;
    float lat_span = (float)(mxla - mnla);
    float lon_span = (float)(mxlo - mnlo) * lon_scale;
    float slat = (float)ah / (lat_span > 0 ? lat_span : 1.0f);
    float slon = (float)aw / (lon_span > 0 ? lon_span : 1.0f);
    float scale = (slat < slon) ? slat : slon;
    int off_x = ax + (aw - (int)(lon_span * scale)) / 2;
    int off_y = ay + (ah - (int)(lat_span * scale)) / 2;
    auto project = [&](int32_t la, int32_t lo, int& px, int& py) {
      px = off_x + (int)((float)(lo - mnlo) * lon_scale * scale);
      py = off_y + (int)((float)(mxla - la) * scale);
    };
    // Trail as a connected line, matching the full Trail map (shared helper —
    // see gfx::drawTrail); no break marker here, just a silent gap.
    gfx::drawTrail(display, tr, project, [](int, int, int, int) {});
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
      if (!lt.isActive(i, now)) continue;
      int px, py; project(lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6, px, py);
      miniIconDrawCentered(display, px, py, ICON_MAP_CONTACT);
    }
    if (have_gps) { int px, py; project(mla, mlo, px, py); miniIconDrawCentered(display, px, py, ICON_MAP_CURRENT); }
    // Active target flag drawn last so it stays legible even atop a contact/own dot.
    if (have_tgt) { int px, py; project(tla, tlo, px, py); miniIconDrawCentered(display, px, py, ICON_MAP_TARGET); }

    // Bottom-left scale reference, always shown — distance to the active
    // target (or else the nearest live-tracked contact) now lives on the
    // status line below instead (see statusDistanceKm() / render()), so this
    // corner is free for it.
    {
      display.setColor(UIColor::primary_txt);
      int ty = ay + ah - display.getLineHeight();
      static const float M_PER_1E6 = 0.11132f;            // metres per 1e-6° lat
      float ppm = scale / M_PER_1E6;                       // pixels per metre
      if (ppm > 0.0f) {
        bool imp = _task->useImperial();
        static const float MET_M[] = { 5,10,25,50,100,250,500,1000,2000,5000,10000,25000,50000 };
        static const char* MET_L[] = { "5m","10m","25m","50m","100m","250m","500m","1km","2km","5km","10km","25km","50km" };
        static const float IMP_M[] = { 4.572f,15.24f,30.48f,76.2f,152.4f,402.34f,804.67f,1609.34f,4828.0f,16093.4f,80467.2f };
        static const char* IMP_L[] = { "15ft","50ft","100ft","250ft","500ft","1/4mi","1/2mi","1mi","3mi","10mi","50mi" };
        const float* M = imp ? IMP_M : MET_M;
        const char* const* L = imp ? IMP_L : MET_L;
        int N = imp ? (int)(sizeof(IMP_M) / sizeof(IMP_M[0])) : (int)(sizeof(MET_M) / sizeof(MET_M[0]));
        float target = 8.0f / ppm;                         // short reference tick, not 1/3 of the width
        int sel = 0;
        for (int i = N - 1; i >= 0; i--) if (M[i] <= target) { sel = i; break; }
        int barpx = (int)(M[sel] * ppm + 0.5f);
        if (barpx < 5)        barpx = 5;
        if (barpx > aw / 4)   barpx = aw / 4;
        int bx = ax + 1, mid = ty + display.getLineHeight() / 2;
        display.fillRect(bx, mid, barpx, display.sepH());   // single tick, on the text baseline
        display.setCursor(bx + barpx + 2, ty);
        display.print(L[sel]);
      }
    }
    return true;
  }

  // Distance shown on the MAP status line. The active Locator/Nav target
  // takes priority — that's what the flag on the mini-map is pointing at,
  // and it's the only way a waypoint target ever gets a distance readout
  // here (a waypoint isn't a live-tracked contact). Falls back to the
  // nearest live-tracked ([LOC]-sharing) contact when no target is set.
  // -1 when we don't have a fix or nothing to measure against.
  float statusDistanceKm() {
    int32_t mla, mlo;
    if (!_task->currentLocation(mla, mlo)) return -1.0f;
    int32_t tla, tlo;
    if (_task->activeTargetPos(tla, tlo)) return geo::haversineKm(mla, mlo, tla, tlo);
    LiveTrackStore& lt = _task->liveTrack();
    uint32_t now = rtc_clock.getCurrentTime();
    float nearest_km = -1.0f;
    for (int i = 0; i < LiveTrackStore::CAPACITY; i++) {
      if (!lt.isActive(i, now)) continue;
      float d = geo::haversineKm(mla, mlo, lt.slotAt(i).lat_1e6, lt.slotAt(i).lon_1e6);
      if (nearest_km < 0.0f || d < nearest_km) nearest_km = d;
    }
    return nearest_km;
  }

  // The Map page: the mini-map in a soft frame on the left, what it shows
  // spelled out in a column on the right (GPS, the trail, live contacts or
  // the target). Without a position the column still says what's missing.
  // A tall screen (portrait e-ink) stacks them: the map, the four rows under it.
  void drawMapPage(DisplayDriver& d, int top) {
    const int W = d.width(), H = d.height(), lh = d.getLineHeight();
    const int s = miniIconScale(d);
    const bool tall = H - top > W;
    // A screen 40 characters wide (the 4.2"): a square map and a column of
    // labelled rows beside it, one line apart.
    const bool wide = !tall && W >= 40 * d.getCharWidth();
    const int rows_h = tall ? 4 * (lh + lh / 2) : 0;
    const int ph = H - top - rows_h;
    const int pw = tall ? W : wide ? (ph < W * 3 / 5 ? ph : W * 3 / 5) : W / 2;   // map panel
    d.setColor(UIColor::primary_txt);
    d.drawSoftRect(0, top, pw, ph);
    if (!drawMapPreview(d, 2, top + 2, pw - 4, ph - 4)) {
      // An empty sheet: a sparse dot grid with the reason over it.
      for (int gy = top + 4; gy < top + ph - 2; gy += 6 * s)
        for (int gx = 4; gx < pw - 2; gx += 6 * s) d.fillRect(gx, gy, s, s);
      const char* why = "No fix";
      const int tw = d.getTextWidth(why), tx = (pw - tw) / 2, ty = top + (ph - lh) / 2;
      d.setColor(UIColor::window_bkg);
      d.fillRect(tx - 2, ty - 1, tw + 4, lh + 2);
      d.setColor(UIColor::primary_txt);
      d.setCursor(tx, ty);
      d.print(why);
    }

    if (wide) { drawMapColumn(d, pw + 6, top, W - 2); return; }

    // The column: four rows, always, like the Status tiles.
    const int cx = tall ? 2 : pw + 3, cw = W - cx;
    const int row_h = tall ? rows_h / 4 : ph / 4;
    int y = (tall ? top + ph : top) + (row_h - lh) / 2 + 1;
    auto line = [&](const MiniIcon& ic, const char* text) {
      miniIconDraw(d, cx, y, ic);
      const int tx = cx + ic.w * s + 2 * s;
      d.drawTextEllipsized(tx, y, cx + cw - tx, text);
      y += row_h;
    };
    char buf[20];
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    const bool on = _task->getGPSState();
    if (!loc)                 strcpy(buf, "No GPS");
    else if (!on)             strcpy(buf, "Off");
    else if (!loc->isValid()) strcpy(buf, "No fix");
    else                      snprintf(buf, sizeof(buf), "%ld sats", loc->satellitesCount());
#else
    strcpy(buf, "No GPS");
#endif
    line(ICON_GPS, buf);
    TrailStore& tr = _task->trail();
    if (tr.empty() && !tr.isActive()) strcpy(buf, "No trail");
    else geo::fmtDist(buf, sizeof(buf), tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
    line(ICON_TRAIL, buf);
    const float km = statusDistanceKm();
    int32_t tla, tlo;
    const bool target = _task->activeTargetPos(tla, tlo);
    const int live = _task->liveTrack().active(rtc_clock.getCurrentTime());
    if (live > 0 && km >= 0 && !target) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
    else                                snprintf(buf, sizeof(buf), "%d live", live);
    line(ICON_MAP_CONTACT, buf);
    if (target) {
      if (km >= 0) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      else         strcpy(buf, "-");
      line(ICON_MAP_TARGET, buf);
    } else {
      snprintf(buf, sizeof(buf), "%d wpts", _task->waypoints().count());
      line(ICON_MAP_WAYPOINT, buf);
    }
  }

  // The wide Map page's column, x0..x1 from top: GPS and the position, the
  // trail, then live contacts, the target and waypoints, each an icon, a
  // label and the value at the right, the groups split by a dotted rule.
  void drawMapColumn(DisplayDriver& d, int x0, int top, int x1) {
    const int lh = d.getLineHeight(), s = miniIconScale(d), step = d.lineStep() + 2;
    const int lx = x0 + PAGE_ICON_PX * s + 2 * s;   // labels clear of the widest icon
    int y = top + 1;
    auto row = [&](const MiniIcon* ic, const char* label, const char* val) {
      if (ic) miniIconDraw(d, x0, y + (lh - ic->h * s) / 2 - s, *ic);
      d.setCursor(lx, y); d.print(label);
      d.drawTextRightAlign(x1, y, val);
      y += step;
    };
    auto rule = [&]() { info::rule(d, x0, y + lh / 2 - 2, x1 - x0); y += lh / 2 + 2; };
    char buf[24];
#if ENV_INCLUDE_GPS == 1
    LocationProvider* loc = sensors.getLocationProvider();
    const bool on = _task->getGPSState();
    if (!loc)                 strcpy(buf, "none");
    else if (!on)             strcpy(buf, "off");
    else if (!loc->isValid()) strcpy(buf, "no fix");
    else                      snprintf(buf, sizeof(buf), "%ld sats", loc->satellitesCount());
    row(&ICON_GPS, "GPS", buf);
    int32_t mla, mlo;
    if (_task->currentLocation(mla, mlo)) {
      snprintf(buf, sizeof(buf), "%.5f", mla / 1e6); row(nullptr, "Lat", buf);
      snprintf(buf, sizeof(buf), "%.5f", mlo / 1e6); row(nullptr, "Lon", buf);
    }
#else
    row(&ICON_GPS, "GPS", "none");
#endif
    rule();
    TrailStore& tr = _task->trail();
    if (tr.empty() && !tr.isActive()) strcpy(buf, "-");
    else geo::fmtDist(buf, sizeof(buf), tr.totalDistanceMeters() / 1000.0f, _task->useImperial(), true);
    row(&ICON_TRAIL, "Trail", buf);
    if (!tr.empty() || tr.isActive()) {
      const uint32_t t = tr.elapsedSeconds();
      snprintf(buf, sizeof(buf), "%luh %02lum", (unsigned long)(t / 3600), (unsigned long)(t / 60 % 60));
      row(nullptr, tr.isActive() ? (tr.isPaused() ? "Paused" : "Rec") : "Time", buf);
      snprintf(buf, sizeof(buf), "%d", tr.count());
      row(nullptr, "Points", buf);
    }
    rule();
    const float km = statusDistanceKm();
    int32_t tla, tlo;
    const bool target = _task->activeTargetPos(tla, tlo);
    const int live = _task->liveTrack().active(rtc_clock.getCurrentTime());
    snprintf(buf, sizeof(buf), "%d", live);
    row(&ICON_MAP_CONTACT, "Live", buf);
    if (live > 0 && km >= 0 && !target) {
      geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      row(nullptr, "Nearest", buf);
    }
    if (target) {
      if (km >= 0) geo::fmtDist(buf, sizeof(buf), km, _task->useImperial(), true);
      else         strcpy(buf, "-");
      row(&ICON_MAP_TARGET, "Target", buf);
    }
    snprintf(buf, sizeof(buf), "%d", _task->waypoints().count());
    row(&ICON_MAP_WAYPOINT, "Waypoints", buf);
  }

  // The Favourites grid: landscape 3x2, portrait 2x3.
  static int favCols(DisplayDriver& d) { return d.isLandscape() ? 3 : 2; }

  // The clock fields set in Settings (dashboard_fields), in order; their count.
  int clockFields(uint8_t* out) {
    int n = 0;
    if (_node_prefs)
      for (int k = 0; k < 3; k++)
        if (_node_prefs->dashboard_fields[k] != telemetry::NONE && _node_prefs->dashboard_fields[k] < telemetry::COUNT)
          out[n++] = _node_prefs->dashboard_fields[k];
    return n;
  }
  // One field's value without its noun (the label beside it names it); "-" when there is none.
  void clockFieldValue(uint8_t f, char* val, int n) {
    formatDashVal(f, val, n, _task->getBattMilliVolts(), _node_prefs->low_batt_mv,
                  _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount(),
                  _task->getAnyUnreadOverflow(), _node_prefs->units_imperial, &sensors_lpp, false);
    if (!val[0]) snprintf(val, n, "-");
  }

  // ── Clock page: the big clock, a seconds bar under it, the date, and the
  // clock fields (Settings) as columns along the bottom, name over value.
  // Without fields the clock and date sit in the middle.
  // The days left this month the alarm goes off on, as calendar::draw()'s
  // marks: the repeat days, or a one-shot's next (today or tomorrow).
  uint32_t alarmDays(const struct tm& t) const {
    if (!_node_prefs) return 0;
    return calmath::alarmDays(t, _node_prefs->alarm_on, _node_prefs->alarm_repeat_mask,
                              _node_prefs->alarm_hour, _node_prefs->alarm_min);
  }

  void drawClockPage(DisplayDriver& d, uint32_t unix_ts) {
    struct tm ti;
    localTm(unix_ts, tzHoursAt(_node_prefs, unix_ts), ti);
    const bool h12 = _node_prefs && _node_prefs->clock_12h;
    const bool show_sec = !Features::IS_EINK && (!_node_prefs || !_node_prefs->clock_hide_seconds);
    const int W = d.width(), H = d.height();
    d.setColor(UIColor::primary_txt);
    d.setTextSize(1);
    const int lh = d.getLineHeight();

    uint8_t fields[3];
    const int nf = clockFields(fields);

    // The month under the date, where it fits: a tall screen (portrait
    // e-ink), or the 4.2" landscape one with the clock moved to the top.
    const int fields_h = nf ? 2 * lh + 1 + lh / 2 : 0;
    const int cal_h = calendar::height(d, ti);
    bool cal = false;
    int date_y;
    if (d.height() > d.width()) {   // portrait e-ink: stacked digits
      date_y = drawClockTall(d, lh / 2, &ti, h12);
      cal = true;
    } else {
      const BigClock bc = bigClockSize(d);
      const int bar = 3 * bc.sc / 2;              // the seconds bar and its gaps
      const int block = bc.h + bar + 1 + lh;
      cal = bc.sc + block + lh / 2 + 2 + cal_h <= H - fields_h;
      const int y = nf || cal ? bc.sc : (H - block) / 2;
      drawBigClock(d, W / 2, y, &ti, h12, bc.sc);
      const int bar_y = y + bc.h + bar / 2 + 1;
      if (show_sec) {   // fills over the minute: dotted track, solid part
        const int x0 = W / 8, bw = W - 2 * x0, fill = bw * ti.tm_sec / 60;
        const int t = d.sepH();
        d.fillRect(x0, bar_y, fill, t);
        for (int x = x0 + fill + (fill & 1) * t; x < x0 + bw; x += 2 * t) d.fillRect(x, bar_y, t, t);
      }
      // Alarm armed: the bell at the bar's left end (the status bar, which
      // shows it everywhere else, is hidden on this page).
      if (_node_prefs && _node_prefs->alarm_on) miniIconDrawTop(d, W / 32, bar_y - 2, ICON_ALARM);
      date_y = y + bc.h + bar + 1;
    }
    char buf[24];
    snprintf(buf, sizeof(buf), "%s %d %s %d", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon], 1900 + ti.tm_year);
    d.drawTextCentered(W / 2, date_y, buf);

    const int cal_top = date_y + lh + lh / 2 + 2;
    if (cal && cal_h <= H - fields_h - cal_top) {   // no wider than four characters a day
      const int cw = W < 28 * d.getCharWidth() ? W : 28 * d.getCharWidth();
      calendar::draw(d, (W - cw) / 2, cal_top, cw, ti, alarmDays(ti));
    }

    if (nf == 0) return;
    refresh_sensors();
    const int val_y = H - lh, lab_y = val_y - lh - 1, cw = W / nf;
    for (int k = 0; k < nf; k++) {
      char val[20];
      clockFieldValue(fields[k], val, sizeof(val));
      const int x0 = k * cw;
      auto cell = [&](int yy, const char* t) {
        if ((int)d.getTextWidth(t) <= cw - 2) d.drawTextCentered(x0 + cw / 2, yy, t);
        else d.drawTextEllipsized(x0 + 1, yy, cw - 2, t);
      };
      cell(lab_y, telemetry::LABEL[fields[k]]);
      cell(val_y, val);
    }
  }

  // ── Lock screen: the status bar (drawn by render()), then one of two
  // looks (Settings > Display > Lock clock):
  //   big     -- the big clock, the date, unread messages in a pill;
  //   compact -- a small clock with the date beside it, the clock fields as
  //              rows below (plus unread messages when no field shows them).
  // A key press on the lit screen (not the one that wakes it, so a glance
  // shows the data) puts the unlock hint in a pill at the bottom for a few
  // seconds (UITask::lockHintShown); e-ink keeps it up, it has no glance.
  void drawLockPage(DisplayDriver& d) {
    const int W = d.width(), H = d.height();
    d.setColor(UIColor::primary_txt);
    d.setTextSize(1);
    const int lh = d.getLineHeight(), top = statusBarH(d) + 3;
    const bool hint = Features::IS_EINK || _task->lockHintShown();
    const int pill_y = H - lh - 3;   // the bottom pill, clear of what's above
    const int unread = _task->getDMUnreadTotal() + _task->getChannelUnreadCount() + _task->getRoomUnreadCount();
    const uint32_t unix_ts = _rtc->getCurrentTime();
    const bool synced = unix_ts >= 1000000000UL;
    struct tm ti;
    if (synced) localTm(unix_ts, tzHoursAt(_node_prefs, unix_ts), ti);
    const bool h12 = _node_prefs && _node_prefs->clock_12h;
    const bool compact = _node_prefs && _node_prefs->lock_compact && W > H;
    // The month and the rows under the big clock: on a tall screen (portrait
    // e-ink), and on one 40 characters wide (the 4.2").
    const bool extras = H > W || W >= 40 * d.getCharWidth();

    char pill[24] = "";
    if (compact) {
      const int m = W / 16, x0 = W / 32;
      int sc = bigClockSize(d).sc / 2;
      if (sc < 1) sc = 1;
      const int ch = lettering::LOGO_H * sc;
      if (!synced) {
        d.setCursor(x0, top + (ch - lh + 1) / 2);
        d.print("No time sync");
      } else {   // the date beside the clock, without the weekday if it won't fit
        BigClockText bt(d, &ti, h12, sc);
        bt.draw(d, x0, top);
        char date[16];
        snprintf(date, sizeof(date), "%s %d %s", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
        if (x0 + bt.w + lh / 2 + (int)d.getTextWidth(date) > W - x0)
          snprintf(date, sizeof(date), "%d %s", ti.tm_mday, MON[ti.tm_mon]);
        d.drawTextRightAlign(W - x0, top + (ch - lh + 1) / 2, date);
      }
      uint8_t rows[4];
      int nr = clockFields(rows);
      bool has_msgs = false;
      for (int k = 0; k < nr; k++) if (rows[k] == telemetry::MSGS) has_msgs = true;
      if (!has_msgs && unread > 0) rows[nr++] = telemetry::MSGS;
      if (nr) refresh_sensors();
      const int step = d.lineStep();
      for (int k = 0, y = top + ch + 6; k < nr && y + lh <= H; k++, y += step) {
        if (hint && y + lh >= pill_y) break;   // the hint pill takes the bottom
        char val[20];
        clockFieldValue(rows[k], val, sizeof(val));
        info::valueRow(d, y, telemetry::LABEL[rows[k]], val, false, m - 2, m);
      }
    } else {
      if (!synced) {
        d.drawTextCentered(W / 2, H / 2 - lh, "No time sync");
      } else {
        int date_y;
        if (H > W) {
          date_y = drawClockTall(d, top, &ti, h12);
        } else {
          const BigClock bc = bigClockSize(d);
          drawBigClock(d, W / 2, top, &ti, h12, bc.sc);
          date_y = top + bc.h + 3;
        }
        char buf[16];
        snprintf(buf, sizeof(buf), "%s %d %s", WDAY[ti.tm_wday], ti.tm_mday, MON[ti.tm_mon]);
        d.drawTextCentered(W / 2, date_y, buf);
        if (extras) drawLockTallExtras(d, date_y + lh + lh / 2 + 2, hint ? pill_y - 3 : H, ti, unread);
      }
      if (!hint && unread > 0 && !(synced && extras)) snprintf(pill, sizeof(pill), "%d%s new", unread, _task->getAnyUnreadOverflow() ? "+" : "");
    }

    if (hint) {
      const int n = _task->lockSeqCount();
#if defined(CARDKB_I2C)
      const char* first = _task->hasCardKB() ? "Back+3xEnter/Fn+Esc" : "Hold Back + 3xEnter";
#else
      const char* first = "Hold Back + 3xEnter";
#endif
      snprintf(pill, sizeof(pill), "%s", n == 0 ? first : n == 1 ? "Enter x2 more..." : "Enter x1 more...");
    }
    if (!pill[0]) return;
    const int pw = d.getTextWidth(pill) + 6;
    d.fillSoftRect((W - pw) / 2, pill_y, pw, lh + 3);
    d.setColor(UIColor::window_bkg);
    d.drawTextCentered(W / 2, pill_y + 2, pill);
    d.setColor(UIColor::primary_txt);
  }

  // Under the lock clock on a tall or a 4.2" screen, between y and bottom: the month,
  // then a row each for unread messages, the armed alarm and the clock fields
  // -- the rows first, the calendar only if there's room for it as well.
  void drawLockTallExtras(DisplayDriver& d, int y, int bottom, const struct tm& ti, int unread) {
    const int W = d.width(), m = W / 16, step = d.lineStep(), s = miniIconScale(d);
    struct Row { const MiniIcon* ic; const char* label; char value[20]; } rows[6];
    int n = 0;
    auto add = [&](const MiniIcon* ic, const char* label) -> char* {
      rows[n].ic = ic; rows[n].label = label; rows[n].value[0] = 0;
      return rows[n++].value;
    };
    if (unread > 0)
      snprintf(add(&ICON_PG_MSG, "Messages"), sizeof(rows[0].value), "%d%s new", unread, _task->getAnyUnreadOverflow() ? "+" : "");
    if (_node_prefs && _node_prefs->alarm_on)
      snprintf(add(&ICON_ALARM, "Alarm"), sizeof(rows[0].value), "%02d:%02d", _node_prefs->alarm_hour, _node_prefs->alarm_min);
    uint8_t fields[3];
    const int nf = clockFields(fields);
    if (nf) refresh_sensors();
    for (int k = 0; k < nf && n < 6; k++) {
      if (fields[k] == telemetry::MSGS && unread > 0) continue;   // already a row
      clockFieldValue(fields[k], add(nullptr, telemetry::LABEL[fields[k]]), sizeof(rows[0].value));
    }
    const int rows_h = n * step;
    if (calendar::height(d, ti) + d.getLineHeight() / 2 + rows_h <= bottom - y) {
      const int cw = W < 28 * d.getCharWidth() ? W : 28 * d.getCharWidth();   // as on the Clock page
      calendar::draw(d, (W - cw) / 2, y, cw, ti, alarmDays(ti));
      y += calendar::height(d, ti) + d.getLineHeight() / 2;
    }
    for (int k = 0; k < n && y + d.getLineHeight() <= bottom; k++, y += step) {
      int x = m;
      if (rows[k].ic) { miniIconDraw(d, x, y, *rows[k].ic); x += rows[k].ic->w * s + 2 * s; }
      info::valueRow(d, y, rows[k].label, rows[k].value, false, m - 2, x);
    }
  }

  // The Status page: four tiles split by dotted rules, each one subject's
  // headline (icon + main value) over a detail line. A tile with the room
  // (the 4.2" panel) adds a line and a chart of its history under it. Enter
  // opens the Status screen with all of it.
  void drawStatusTiles(DisplayDriver& d, int top) {
    const int W = d.width(), H = d.height() - top;
    const int lh = d.getLineHeight(), s = miniIconScale(d);
    const int mid_x = W / 2, mid_y = top + H / 2;
    const bool roomy = H / 2 >= 7 * lh;   // three lines and a chart a tile
    StatusScreen* st = (StatusScreen*)_task->statusScreen();
    d.setColor(UIColor::primary_txt);
    info::vrule(d, mid_x, top, H);
    info::rule(d, 0, mid_y, W);
    // The tile's box, inside the rules.
    auto box = [&](int col, int row, int& x, int& y, int& w, int& h) {
      x = col ? mid_x + 3 : 1; y = row ? mid_y + 2 : top + 1;
      w = col ? W - x : mid_x - 2 - x;
      h = (row ? top + H : mid_y - 1) - y;
    };
    auto tile = [&](int col, int row, const MiniIcon* ic, float batt, const char* head, const char* detail,
                    const char* more = "") {
      int x, y, w, h;
      box(col, row, x, y, w, h);
      const int iy = y + (lh - info::batteryH(d)) / 2 - s;
      int tx = x;
      if (ic)            { miniIconDraw(d, x, iy, *ic); tx = x + ic->w * s + 2 * s; }
      else if (batt >= 0) { info::battery(d, x, iy, batt); tx = x + info::batteryW(d) + 2 * s; }
      d.drawTextEllipsized(tx, y, x + w - tx, head);
      d.drawTextEllipsized(x, y + lh + 2, w, detail);
      if (roomy && more[0]) d.drawTextEllipsized(x, y + 2 * (lh + 2), w, more);
    };
    // Under a roomy tile's three lines: a history.
    auto tileChart = [&](int col, int row, const info::History<StatusScreen::HIST_N>& hs, int min_span, bool bars) {
      int x, y, w, h;
      box(col, row, x, y, w, h);
      const int cy = y + 3 * (lh + 2) + 2, ch = y + h - cy - 3;
      if (ch >= 2 * lh) info::chart(d, x, cy, w - 4, ch, hs, min_span, bars, nullptr, nullptr);
    };
    char h[20], t[20], m[28];

    snprintf(h, sizeof(h), "%.3f", _node_prefs->freq);
    snprintf(t, sizeof(t), "SF%u %gk", (unsigned)_node_prefs->sf, _node_prefs->bw);
    const int nf = (int)radio_driver.getNoiseFloor();
    if (nf) snprintf(m, sizeof(m), "Noise %d dBm", nf); else m[0] = 0;
    tile(0, 0, &ICON_PG_RADIO, -1, h, t, m);
    if (roomy && st) tileChart(0, 0, st->noiseHistory(), 6, false);

#if ENV_INCLUDE_GPS == 1
    {
      LocationProvider* loc = sensors.getLocationProvider();
      const bool on = _task->getGPSState();
      t[0] = 0;
      if (!loc)               strcpy(h, "No GPS");
      else if (!on)           strcpy(h, "GPS off");
      else if (!loc->isValid()) strcpy(h, "No fix");
      else {
#ifdef STATUS_HAS_SKY
        GpsSky* g = skyview::sky();
        strcpy(h, g && g->fix_mode == 2 ? "2D fix" : "3D fix");
#else
        strcpy(h, "Fix");
#endif
      }
      if (loc && on) snprintf(t, sizeof(t), "%ld sats", loc->satellitesCount());
      int32_t la, lo;
      m[0] = 0;
      if (loc && on && loc->isValid() && _task->currentLocation(la, lo)) snprintf(m, sizeof(m), "%.4f %.4f", la / 1e6, lo / 1e6);
      tile(1, 0, &ICON_GPS, -1, h, t, m);
    }
#else
    tile(1, 0, &ICON_GPS, -1, "No GPS", "");
#endif

    const int mv = _task->getBattMilliVolts();
    const int pct = battery::percent(mv, _node_prefs ? (int)_node_prefs->low_batt_mv : 0);
    snprintf(h, sizeof(h), "%d%%", pct);
    snprintf(t, sizeof(t), "%d.%02d V%s", mv / 1000, (mv % 1000) / 10, board.isExternalPowered() ? " USB" : "");
    tile(0, 1, nullptr, pct / 100.0f, h, t);
    if (st && roomy) tileChart(0, 1, st->battHistory(), 20, false);
    else if (st) {   // the battery's trend beside its %
      const int x0 = 1 + info::batteryW(d) + 2 * s + d.getTextWidth(h) + 3 * s, x1 = mid_x - 3;
      if (x1 - x0 >= 12) info::spark(d, x0, mid_y + 2, x1 - x0, lh - 1, st->battHistory(), 20);
    }

    const int nc = the_mesh.getNumContacts();
    snprintf(h, sizeof(h), "%d node%s", nc, nc == 1 ? "" : "s");
    snprintf(t, sizeof(t), "%d in 1 h", StatusScreen::heardLastHour());
    snprintf(m, sizeof(m), "RX %lu  TX %lu", (unsigned long)StatusScreen::totalRx(), (unsigned long)StatusScreen::totalTx());
    tile(1, 1, &ICON_MAP_CONTACT, -1, h, t, m);
    if (roomy && st) tileChart(1, 1, st->trafficHistory(), 4, true);
  }

  // Small 5x5 glyph shown in the page-indicator row for each HomePage.
  static const MiniIcon* pageIcon(int page) {
    switch (page) {
      case CLOCK:      return &ICON_PG_CLOCK;
      case FAVOURITES: return &ICON_PG_STAR;
      case STATUS:     return &ICON_CHART;
      case BLUETOOTH:  return &ICON_PG_BT;
      case ADVERT:     return &ICON_PG_ADVERT;
      case SETTINGS:   return &ICON_PG_SETTINGS;
      case MAP:        return &ICON_PG_MAP;
      case TOOLS:      return &ICON_PG_TOOLS;
      case QUICK_MSG:  return &ICON_PG_MSG;
      case SHUTDOWN:   return &ICON_PG_POWER;
    }
    return nullptr;
  }

  int render(DisplayDriver& display) override {
    char tmp[80];
    int mq_delay = 0;   // >0 while a selected row's name is marquee-scrolling
    int anim_ms = 0;    // >0 while something animates: a page turn
    display.setTextSize(1);
    const int lh      = display.getLineHeight();  // line height at sz1
    const int step    = display.lineStep();        // lh + 2
    // Page-indicator row: small page icons (PAGE_ICON_PX) replace the old dots. Centre and
    // gap scale with the font so the band clears the header above and content
    // below (identical to the old lh+4 / +6 dots layout at 1x).
    const int pg_half   = (PAGE_ICON_PX * statusIconScale(display) + 1) / 2;   // page icons follow STATUS_ICON_SCALE
    const int dots_y    = statusBarH(display) + pg_half + 1;   // icon-row centre, below the header
    const int content_y = contentTop(display);    // first content row, below the icons
    const float slide   = slideProgress();        // -1, or how far a page turn has got

    // Title bar displaying node name (except on lock screen), status icons and battery.
    // Hidden on fullscreen pages (CLOCK).
    if (_page != CLOCK) {
      display.setColor(UIColor::primary_txt);
      // The lock page has nothing on the left of the bar (its clock sits
      // below it), so the status icons may use the whole row.
      const int lock_reserve = _page == LOCK ? 0 : -1;
      int rightEdge = renderBatteryIndicator(display, _task->getBattMilliVolts(), lock_reserve);
      display.setColor(UIColor::primary_txt);

      if (_page != LOCK) {
        const int bar_ty = (statusBarH(display) - lh) / 2;   // text centred in a tall bar (STATUS_ICON_SCALE)
        // The time, once the clock is set (the node name before that): the
        // name is what others see, the time is what you glance at here.
        char filtered_name[sizeof(_node_prefs->node_name)];
        uint32_t now_ts = _rtc->getCurrentTime();
        if (now_ts >= 1000000000UL) {
          struct tm lt;
          localTm(now_ts, tzHoursAt(_node_prefs, now_ts), lt);
          int hh = lt.tm_hour;
          if (_node_prefs->clock_12h) { hh %= 12; if (hh == 0) hh = 12; }
          snprintf(filtered_name, sizeof(filtered_name), "%d:%02d", hh, lt.tm_min);
          if (display.width() >= 40 * display.getCharWidth())   // the 4.2": the date beside it
            snprintf(filtered_name + strlen(filtered_name), sizeof(filtered_name) - strlen(filtered_name),
                     "  %s %d %s", WDAY[lt.tm_wday], lt.tm_mday, MON[lt.tm_mon]);
        } else {
          display.translateUTF8ToBlocks(filtered_name, _node_prefs->node_name, sizeof(filtered_name));
        }

        // Only show the live-power readout when APC is actually controlling power —
        // not merely when the pref is set. While repeating APC is suppressed and
        // power is pinned to the ceiling, so apcActive() is false and the name bar
        // drops the readout (matching the "--" lock in Settings).
        if (the_mesh.apcActive()) {
          char pwr_buf[8];
          snprintf(pwr_buf, sizeof(pwr_buf), "%ddB", (int)radio_driver.getTxPower());
          int pwr_w = display.getTextWidth(pwr_buf);
          display.drawTextEllipsized(0, bar_ty, rightEdge - 2 - pwr_w - 2, filtered_name);
          display.drawTextRightAlign(rightEdge - 2, bar_ty, pwr_buf);
        } else {
          display.drawTextEllipsized(0, bar_ty, rightEdge - 2, filtered_name);
        }
      }
    }

    // ensure current page is visible (e.g. after settings change)
    if (!isPageVisible(_page)) _page = navPage(_page, +1);

    // curr page indicator — a row of small page icons, one per visible page, with
    // the current page underlined. Hidden on CLOCK and LOCK (full screen used for
    // the clock/dashboard).
    if (_page != CLOCK && _page != LOCK) {
      int order[(int)Count]; int n = buildVisibleOrder(order);
      int curr_vis = 0;
      for (int i = 0; i < n; i++) if (order[i] == _page) { curr_vis = i; break; }
      const int s        = statusIconScale(display);   // STATUS_ICON_SCALE, or the font's
      const int icon_w   = PAGE_ICON_PX * s;
      int pitch = icon_w + 5 * s;                       // comfortable spacing
      if (n > 1) {                                      // shrink to fit if many pages
        int fit = (display.width() - icon_w) / (n - 1);
        if (fit < pitch) pitch = fit;
      }
      int x = display.width() / 2 - pitch * (n - 1) / 2;
      // The current page: its icon knocked out of a soft pill. During a page
      // turn the pill glides over from the page being left.
      const int pad = display.sepH();                  // the pill's margin round the icon
      int pw = icon_w + 4 * pad;
      if (pw > pitch - 1) pw = pitch - 1;
      int pill_x = x + pitch * curr_vis;
      if (slide >= 0) {
        for (int i = 0; i < n; i++)
          if (order[i] == _slide_from) { pill_x = x + pitch * i + (int)((pill_x - x - pitch * i) * slide + 0.5f); break; }
      }
      display.setColor(UIColor::primary_txt);
      display.fillSoftRect(pill_x - pw / 2, dots_y - pg_half - pad, pw, pg_half * 2 + 1 + 2 * pad);
      for (int i = 0; i < n; i++) {
        const MiniIcon* ic = pageIcon(order[i]);
        const int d2 = 2 * (x - pill_x < 0 ? pill_x - x : x - pill_x);
        if (d2 <= pw - icon_w)       display.setColor(UIColor::window_bkg);   // inside the pill
        else if (d2 < pw + icon_w) { x += pitch; continue; }                   // half under the gliding pill
        else                         display.setColor(UIColor::primary_txt);
        if (ic) miniIconDrawCenteredScaled(display, x, dots_y, *ic, s);
        x += pitch;
      }
      display.setColor(UIColor::primary_txt);
    }

    if (_page == HomePage::CLOCK) {
      uint32_t unix_ts = _rtc->getCurrentTime();
      if (unix_ts < 1000000000UL) {
        display.setColor(UIColor::primary_txt);
        display.setTextSize(1);
        int mid_y = display.height() / 2 - step;
        display.drawTextCentered(display.width() / 2, mid_y, "! No time sync");
        display.drawTextCentered(display.width() / 2, mid_y + step, "Enable GPS or");
        display.drawTextCentered(display.width() / 2, mid_y + step * 2, "connect app");
      } else {
        drawClockPage(display, unix_ts);
      }
    } else if (_page == HomePage::LOCK) {
      drawLockPage(display);
    } else if (_page == HomePage::STATUS) {
      drawStatusTiles(display, content_y);
    } else if (isQuickPage(_page) && !(_page == HomePage::SHUTDOWN && _shutdown_init)) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      QRow rows[QUICK_MAX];
      const int n = buildQuick(_page, rows);
      int q = drawQuickPanel(display, content_y, rows, n);
      if (_page == HomePage::TOOLS && _task->isTimerRunning()) q = 1000;   // the timer's countdown
      if (q > 0 && (mq_delay <= 0 || q < mq_delay)) mq_delay = q;
    } else if (_page == HomePage::MAP) {
      drawMapPage(display, content_y + 4);   // the gap under the page icons
    } else if (_page == HomePage::FAVOURITES) {
      // Grid of pinned contacts. Layout transposes to current orientation:
      // landscape → 3×2, portrait → 2×3. The selected card is filled.
      // No title — node name + battery (top bar) and the page-dots indicator above
      // serve as the page identity.
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);

      const int cols    = favCols(display);
      const int rows    = NodePrefs::FAVOURITES_COUNT / cols;
      const int grid_y  = content_y + 4;            // the gap under the page icons, as on every page
      const int grid_h  = display.height() - grid_y;
      const int cell_w  = display.width() / cols;
      const int cell_h  = grid_h / rows;
      const int line_h  = display.getLineHeight();

      if (_fav_sel >= NodePrefs::FAVOURITES_COUNT) _fav_sel = 0;

      bool fav_changed = false;   // a stale (gone) slot was pruned this pass → persist once after the loop
      for (uint8_t i = 0; i < NodePrefs::FAVOURITES_COUNT; i++) {
        int row = i / cols;
        int col = i % cols;
        int cx  = col * cell_w;
        int cy  = grid_y + row * cell_h;
        bool sel = (i == _fav_sel);
        const uint8_t* prefix = favSlotPrefix(i);
        // Each slot a card: a soft frame round a pinned one, a dotted outline
        // round a free one, filled while selected.
        const int tx = cx + 1, ty = cy, tw = cell_w - 2, th = cell_h - 2;
        display.setColor(UIColor::primary_txt);
        if (sel)         { display.fillSoftRect(tx, ty, tw, th); display.setColor(UIColor::window_bkg); }
        else if (prefix)   display.drawSoftRect(tx, ty, tw, th);
        else {
          const int t = display.sepH();   // dot size: 2px on a doubled panel
          for (int x = tx + 2 * t; x < tx + tw - t; x += 2 * t) { display.fillRect(x, ty, t, t); display.fillRect(x, ty + th - t, t, t); }
          for (int y = ty + 2 * t; y < ty + th - t; y += 2 * t) { display.fillRect(tx, y, t, t); display.fillRect(tx + tw - t, y, t, t); }
        }

        char    name[26];
        uint8_t unread   = 0;
        bool    overflow = false;
        bool    resolved = false;
        uint32_t last_ts = 0;                 // heard from / last post, for a tall card
        int32_t  plat = 0, plon = 0;          // a contact's advertised position
        const char* last_text = nullptr;      // the newest message, for a big card
        bool     last_out = false;

        if (prefix && _task->favouriteSlotKind(i) == NodePrefs::FAV_KIND_CHANNEL) {
          uint8_t ch_idx = prefix[0];
          ChannelDetails ch;
          if (the_mesh.getChannel(ch_idx, ch) && ch.name[0]) {
            // '#' marks a channel apart from a contact tile — the two share the
            // grid and Enter does something different on each.
            name[0] = '#';
            display.translateUTF8ToBlocks(name + 1, chanctl::bareName(ch.name), sizeof(name) - 1);
            unread   = _task->getChannelUnread(ch_idx);
            overflow = unread > 0 && _task->getChannelUnreadOverflow(ch_idx);
            resolved = true;
            MessageHistory& h = _task->core().history;
            for (int j = 0; j < h.chHistCount(); j++) {
              const ChHistEntry& e = h.chAtPos(h.chHistPosNewest(j));
              if (e.ch_idx == ch_idx) { last_ts = e.timestamp; last_text = e.text; break; }
            }
          }
        } else if (prefix) {
          for (int idx = MAX_ANON_CONTACTS; ; idx++) {
            ContactInfo c;
            if (!the_mesh.getContactByIdx(idx, c)) break;
            if (memcmp(c.id.pub_key, prefix, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
              display.translateUTF8ToBlocks(name, c.name, sizeof(name));
              unread   = _task->getDMUnread(c.id.pub_key);
              overflow = unread > 0 && _task->getDMUnreadOverflow(c.id.pub_key);
              resolved = true;
              last_ts  = c.lastmod;
              plat = c.gps_lat; plon = c.gps_lon;
              MessageHistory& h = _task->core().history;
              const int rp = h.dmHistEntryForContact(c.id.pub_key, 0);
              if (rp >= 0) { last_text = h.dmAtPos(rp).text; last_out = h.dmAtPos(rp).outgoing; }
              break;
            }
          }
        }

        if (prefix && !resolved) {
          // The pinned target is gone — prefs outlived the contact/channel list
          // (e.g. a wiped /contacts3; onContactRemoved/onChannelRemoved only
          // catch a live delete). Clear the stale slot so it renders as an empty
          // "+" tile instead of a blank one. Persisted once after the loop.
          _task->clearFavouriteSlot(i);
          fav_changed = true;
        }

        if (resolved) {
          // Reserve space for the unread badge so the name's ellipsis lands
          // before it instead of underneath. Badge and name share one baseline.
          int  bw = unread > 0 ? display.unreadBadgeWidth(unread, overflow) + 3 : 0;  // badge + 3 px gap
          // A tall card (e-ink) has the name at the top and lines of detail
          // under it: when it was last heard (a channel: its last post) and,
          // for a contact with a position, how far away it is.
          const bool tall = th >= 3 * line_h + 6;
          int name_y     = tall ? ty + 3 : ty + (th - line_h) / 2;
          int name_max_w = tw - 6 - bw;
          if (name_max_w < 6) name_max_w = 6;
          int r = display.drawTextEllipsized(tx + 3, name_y, name_max_w, name, sel);
          if (sel && r > 0) mq_delay = r;
          if (unread > 0)
            display.drawUnreadBadge(tx + tw - 3, name_y, unread, sel, overflow);
          // A card six lines tall on a screen 40 characters wide (the 4.2"):
          // the newest message under the name, how long ago and how far along
          // the bottom.
          if (th >= 6 * line_h && display.width() >= 40 * display.getCharWidth()) {
            display.setColor(sel ? UIColor::window_bkg : UIColor::primary_txt);
            const int by = ty + th - line_h - 3;   // the bottom line
            const uint32_t now = rtc_clock.getCurrentTime();
            char buf[16];
            if (last_ts) {
              geo::fmtAgeShort(buf, sizeof(buf), now, last_ts);
              display.setCursor(tx + 3, by); display.print(buf);
            }
            int32_t mla, mlo;
            if ((plat || plon) && _task->currentLocation(mla, mlo)) {
              geo::fmtDist(buf, sizeof(buf), geo::haversineKm(mla, mlo, plat, plon), _task->useImperial(), true);
              display.drawTextRightAlign(tx + tw - 3, by, buf);
            }
            const int my = name_y + line_h + line_h / 3;   // the message, wrapped
            const int nl_max = (by - 2 - my) / line_h;
            if (nl_max > 0) {
              char pv[MSG_TEXT_BUF + 4], tr_pv[MSG_TEXT_BUF + 4];
              if (last_text) snprintf(pv, sizeof(pv), "%s%s", last_out ? "Me: " : "", last_text);
              else           snprintf(pv, sizeof(pv), "No messages");
              display.translateUTF8ToBlocks(tr_pv, pv, sizeof(tr_pv));
              char lines[5][FS_CHARS_MAX];
              const int cap = nl_max < 4 ? nl_max : 4;
              int n = FullscreenMsgView::wrapLines(display, tr_pv, tw - 6, lines, cap + 1);
              if (n > cap) {   // more than fits: the last line shown ends in an ellipsis
                n = cap;
                const size_t l = strlen(lines[n - 1]);
                if (l + 4 < sizeof(lines[0])) strcat(lines[n - 1], " ...");
              }
              for (int k = 0; k < n; k++) display.drawTextEllipsized(tx + 3, my + k * line_h, tw - 6, lines[k]);
            }
          } else if (tall) {
            display.setColor(sel ? UIColor::window_bkg : UIColor::primary_txt);   // the badge leaves its own
            const int s = miniIconScale(display), gap = line_h / 3;
            int y = name_y + line_h + gap;
            auto detail = [&](const MiniIcon& ic, const char* text) {
              if (y + line_h > ty + th - 2) return;
              miniIconDraw(display, tx + 3, y, ic);
              const int x = tx + 3 + ic.w * s + 2 * s;
              display.drawTextEllipsized(x, y, tx + tw - 3 - x, text);
              y += line_h + gap;
            };
            char buf[16];
            const uint32_t now = rtc_clock.getCurrentTime();
            if (last_ts) {
              geo::fmtAgeShort(buf, sizeof(buf), now, last_ts);
              detail(prefix && _task->favouriteSlotKind(i) == NodePrefs::FAV_KIND_CHANNEL ? ICON_PG_MSG : ICON_PG_CLOCK, buf);
            }
            int32_t mla, mlo;
            if ((plat || plon) && _task->currentLocation(mla, mlo)) {
              // The distance after an arrow towards it (north up).
              geo::fmtDist(buf, sizeof(buf), geo::haversineKm(mla, mlo, plat, plon), _task->useImperial(), true);
              detail(*ICON_ARROWS[((geo::bearingDeg(mla, mlo, plat, plon) + 22) % 360) / 45], buf);
            }
          }
        } else {
          display.drawTextCentered(tx + tw / 2, ty + (th - line_h) / 2, "+");
        }
        if (sel) display.setColor(UIColor::primary_txt);
      }
      // Persist any pruned slots once, outside the loop — a render pass can clear
      // several gone tiles but only one flash write is needed. Self-healing: once
      // cleared, the slot is empty next frame so this can't re-fire per frame.
      if (fav_changed) the_mesh.savePrefs();
      if (_tile_menu.active) _tile_menu.render(display);
    } else if (_page == HomePage::SHUTDOWN) {
      display.setColor(UIColor::primary_txt);
      display.setTextSize(1);
      display.drawTextCentered(display.width() / 2, content_y + step, "hibernating...");   // the panel's Hibernate row
    }
    if (slide >= 0) {   // page turn: the page left behind slides out, this one in
      display.slideCompose(_slide_dir * (int)(display.width() * slide + 0.5f));
      anim_ms = 15;
    }
    if (Features::IS_EINK) {
      // slow display: poll every 30 s; inbound msgs force immediate refresh via notify()
      return (mq_delay > 0 && mq_delay < Features::HOME_REFRESH_MS) ? mq_delay : Features::HOME_REFRESH_MS;
    }
    if (_page == HomePage::CLOCK) {
      bool show_sec = !_node_prefs || !_node_prefs->clock_hide_seconds;
      int ret = show_sec ? 1000 : 60000;
      if (anim_ms > 0 && anim_ms < ret) ret = anim_ms;
      return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
    }
    int ret = 5000;
    if (anim_ms > 0 && anim_ms < ret) ret = anim_ms;
    return (mq_delay > 0 && mq_delay < ret) ? mq_delay : ret;
  }

  bool handleInput(char c) override {
    // Favourites grid claims joystick UP/DOWN and inner LEFT/RIGHT; LEFT at the
    // left column and RIGHT at the right column fall through to page nav so the
    // user can still leave the page sideways.
    if (_page == HomePage::FAVOURITES) {
      // The tile menu consumes all input while open.
      if (_tile_menu.active) {
        auto res = _tile_menu.handleInput(c);
        if (res == PopupMenu::SELECTED && _pin_target_slot >= 0) {
          if (_tile_menu.selectedIndex() == 1) {          // Replace
            _task->pickFavouriteTarget(_pin_target_slot);
            _pin_target_slot = -1;
            return true;
          }
          _task->clearFavouriteSlot(_pin_target_slot);
          the_mesh.savePrefs();
        }
        if (res != PopupMenu::NONE) _pin_target_slot = -1;
        return true;
      }
      DisplayDriver* d = _task->getDisplay();
      const int cols = d ? favCols(*d) : 2;
      const int rows = NodePrefs::FAVOURITES_COUNT / cols;
      int col = _fav_sel % cols;
      int row = _fav_sel / cols;
      if ((c == KEY_LEFT  || c == KEY_PREV) && col > 0)        { _fav_sel--;        return true; }
      if ((c == KEY_RIGHT || c == KEY_NEXT) && col < cols - 1) { _fav_sel++;        return true; }
      if (c == KEY_UP    && row > 0)                            { _fav_sel -= cols; return true; }
      if (c == KEY_DOWN  && row < rows - 1)                     { _fav_sel += cols; return true; }
      if (c == KEY_CONTEXT_MENU) {
        // Filled tile → Unpin / Replace. An empty tile has nothing to offer:
        // its Enter already opens the picker.
        if (favSlotPrefix(_fav_sel)) {
          _pin_target_slot = _fav_sel;
          _tile_menu.begin("Slot options", 2);
          _tile_menu.addItem("Unpin");
          _tile_menu.addItem("Replace");
        }
        return true;
      }
      if (c == KEY_ENTER) {
        // Filled slot → open its conversation, empty slot → in-place pin picker.
        const uint8_t* pfx = favSlotPrefix(_fav_sel);
        if (!pfx) { _task->pickFavouriteTarget(_fav_sel); return true; }
        if (_task->favouriteSlotKind(_fav_sel) == NodePrefs::FAV_KIND_CHANNEL) {
          _task->openChannelHistory(pfx[0]);
          return true;
        }
        for (int idx = MAX_ANON_CONTACTS; ; idx++) {
          ContactInfo c2;
          if (!the_mesh.getContactByIdx(idx, c2)) break;
          if (memcmp(c2.id.pub_key, pfx, NodePrefs::FAVOURITE_PREFIX_LEN) == 0) {
            // A room opens through its own entry point: posting to one needs a
            // login handshake that a plain DM view would skip.
            if (c2.type == ADV_TYPE_ROOM) _task->openRoomServer(c2);
            else                          _task->openContactDM(c2);
            return true;
          }
        }
        _task->showAlert("Contact not found", 800);
        return true;
      }
      // Edge LEFT/RIGHT and unhandled keys fall through to page nav below.
    }

    if (isQuickPage(_page) && _quick_edit) {
      // Changing a bar: shown live, saved once on the way out.
      if ((c == KEY_LEFT || c == KEY_RIGHT) && _node_prefs) {
        QRow rows[QUICK_MAX];
        const int n = buildQuick(_page, rows);
        const QAct act = n > 0 ? rows[_quick_sel < n ? _quick_sel : n - 1].act : QA_NONE;
        const int d = c == KEY_RIGHT ? 1 : -1;
        if (act == QA_BRIGHTNESS) {
          uint8_t& b = _node_prefs->display_brightness;
          const uint8_t nb = (uint8_t)constrain((int)b + d, 0, 4);
          if (nb != b) { b = nb; _quick_dirty = true; _task->applyDisplayPrefs(); }
        } else if (act == QA_AUTO_ADVERT) {
          const int i = constrain(AutoAdvertScreen::indexOf(_node_prefs->advert_auto_interval_sec) + d,
                                  0, AutoAdvertScreen::OPT_COUNT - 1);
          if (_node_prefs->advert_auto_interval_sec != AutoAdvertScreen::OPTS[i]) {
            _node_prefs->advert_auto_interval_sec = AutoAdvertScreen::OPTS[i];
            _quick_dirty = true;
          }
        }
        return true;
      }
      if (c == KEY_ENTER || c == KEY_CANCEL) {
        _quick_edit = false;
        _task->savePrefsIfDirty(_quick_dirty);
        _quick_dirty = false;
        return true;
      }
      return true;   // nothing else while editing
    }
    if (isQuickPage(_page)) {
      QRow rows[QUICK_MAX];
      const int n = buildQuick(_page, rows);
      if (c == KEY_UP)   { if (_quick_sel > 0) _quick_sel--; return true; }
      if (c == KEY_DOWN) { if (_quick_sel + 1 < n) _quick_sel++; return true; }
      if (c == KEY_ENTER && n > 0) { quickAct(_page, rows[_quick_sel < n ? _quick_sel : n - 1]); return true; }
    }
    if (c == KEY_LEFT || c == KEY_PREV) {
      turnPage(-1);
      return true;
    }
    if (c == KEY_NEXT || c == KEY_RIGHT) {
      turnPage(+1);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::STATUS) {
      _task->gotoStatusScreen(StatusScreen::TAB_RADIO);
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::MAP) {
      _task->gotoMapScreen();
      return true;
    }
    if (c == KEY_ENTER && _page == HomePage::CLOCK) {
      _task->gotoClockTools();   // Alarm / Timer / Stopwatch
      return true;
    }
    if (c == KEY_CONTEXT_MENU && _page == HomePage::CLOCK) {
      _task->gotoDashboardConfig();
      return true;
    }
    if (c == KEY_CONTEXT_MENU && _page == HomePage::MAP) {
      _task->quickShareMyLocation();
      return true;
    }
    return false;
  }
};
