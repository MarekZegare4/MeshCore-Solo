#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <Arduino.h>
#include "icons.h"
#include "InfoKit.h"
#include "PopupMenu.h"
#include "TabBar.h"
#include "../MyMesh.h"
#include "../ui-core/Diagnostics.h"
#include "../ui-core/Battery.h"
#if ENV_INCLUDE_GPS == 1 && defined(GPS_SKYVIEW)
  #include "SkyView.h"
  #define STATUS_HAS_SKY 1
#elif ENV_INCLUDE_GPS == 1
  #include <helpers/sensors/GpsSky.h>   // the type only: no receiver data without GPS_SKYVIEW
#endif

extern MyMesh the_mesh;

// Home › Status (Enter on the Status page): what the device can tell about
// itself, one tab per subject. LEFT/RIGHT switches tabs, UP/DOWN scrolls a
// tab by whole blocks.
//   Radio  -- frequency, SF / BW / CR, TX power, the noise floor with its
//             last half hour, the last packet's signal.
//   GPS    -- on / off (Enter toggles), fix, satellites, position, altitude,
//             speed, precision, time to first fix.
//   Sky    -- the sky plot or the signal bars (SkyView.h); Enter swaps them.
//   Power  -- battery level, voltage, source, its trend, then any sensors.
//   Mesh   -- packet counts with a traffic line, nodes heard, contacts.
//             Hold Enter resets the counters.
//   System -- uptime, firmware, device, memory, queue, errors, a font card.
// Replaces Tools › Diagnostics and Tools › Satellites. Drawn with InfoKit.h.
class StatusScreen : public UIScreen {
public:
  enum Tab : uint8_t { TAB_RADIO, TAB_GPS, TAB_SKY, TAB_POWER, TAB_MESH, TAB_SYSTEM, TAB_ALL };

private:
  UITask* _task;
  uint8_t _tabs[TAB_ALL];          // the tabs this board has, in order
  const char* _labels[TAB_ALL];
  uint8_t _count = 0;
  uint8_t _cur = 0;                // index into _tabs
  int  _scroll = 0;                // blocks scrolled off the top
  bool _more = false;              // the last frame cut blocks off at the bottom
  bool _signal = false;            // Sky: the bars instead of the plot
  PopupMenu _reset_menu;           // Mesh, Hold Enter -> confirm
  CayenneLPP _lpp;
  unsigned long _lpp_at = 0;

  // History lines, one sample a minute (32 min).
  static const unsigned long SAMPLE_MS = 60000;
  unsigned long _next_sample = 0;
  uint32_t _last_rx = 0;
  info::History<32> _noise, _batt, _traffic;

  void addTab(uint8_t t, const char* label) { _tabs[_count] = t; _labels[_count] = label; _count++; }
  uint8_t tab() const { return _tabs[_cur]; }

  static uint32_t totalRx() {
    uint32_t n = 0;
    for (uint8_t t = 0; t < 16; t++) n += the_mesh.getNumRecvByType(t);
    return n;
  }
  static uint32_t totalTx() {
    uint32_t n = 0;
    for (uint8_t t = 0; t < 16; t++) n += the_mesh.getNumSentByType(t);
    return n;
  }

  int battMv() const { return _task->getBattMilliVolts(); }
  int battPct() const {
    NodePrefs* p = the_mesh.getNodePrefs();
    return battery::percent(battMv(), p ? (int)p->low_batt_mv : 0);
  }
  bool imperial() const {
    NodePrefs* p = the_mesh.getNodePrefs();
    return p && p->units_imperial;
  }

  // One label/value row as a block.
  // A long value takes the line under its label (no cursor here to scroll it).
  void row(DisplayDriver& d, info::Flow& f, int reserve, const char* label, const char* value) {
    if (f.place(info::rowH(d, label, value, reserve))) info::rowWrapped(d, f.at, label, value, reserve);
  }
  void section(DisplayDriver& d, info::Flow& f, int reserve, const char* label) {
    if (f.place(d.lineStep())) info::section(d, f.at, label, reserve);
  }

  // ── tabs ──────────────────────────────────────────────────────────────
  void tabRadio(DisplayDriver& d, info::Flow& f, int rsv) {
    NodePrefs* p = the_mesh.getNodePrefs();
    char a[24], b[24];
    const int lh = d.getLineHeight();
    if (f.place(info::bigH(d) + 2)) {
      snprintf(a, sizeof(a), "%.3f", p ? p->freq : 0.0f);
      info::big(d, 1, f.at, a, "MHz");
    }
    if (f.place(lh + 5)) {
      int x = 1;
      snprintf(a, sizeof(a), "SF%u", p ? (unsigned)p->sf : 0);      x = info::chip(d, x, f.at, a);
      snprintf(a, sizeof(a), "BW%g", p ? p->bw : 0.0f);              x = info::chip(d, x, f.at, a);
      snprintf(a, sizeof(a), "CR%u", p ? (unsigned)p->cr : 0);       info::chip(d, x, f.at, a);
    }
    snprintf(a, sizeof(a), "%d dBm", (int)radio_driver.getTxPower());
    row(d, f, rsv, "TX power", a);
    if (f.place(d.lineStep())) {   // label, the last half hour, the value now
      const int nf = (int)radio_driver.getNoiseFloor();
      if (nf) snprintf(a, sizeof(a), "%d dBm", nf); else strcpy(a, "-");
      info::row(d, f.at, "Noise", a, rsv);
      const int x0 = 1 + d.getTextWidth("Noise") + 6;
      const int x1 = d.width() - rsv - 2 - d.getTextWidth(a) - 6;
      if (x1 - x0 > 12) info::spark(d, x0, f.at, x1 - x0, lh - 1, _noise);
    }
    if (f.place(d.lineStep())) {
      const float snr = radio_driver.getLastSNR();
      snprintf(b, sizeof(b), "%d dBm", (int)radio_driver.getLastRSSI());
      info::row(d, f.at, "Last RX", b, rsv);
      drawSignalBars(d, d.width() - rsv - 2 - d.getTextWidth(b) - 5, f.at, (int)(snr * 4));
    }
    snprintf(b, sizeof(b), "%.1f dB", radio_driver.getLastSNR());
    row(d, f, rsv, "Last SNR", b);
  }

#if ENV_INCLUDE_GPS == 1
  void tabGps(DisplayDriver& d, info::Flow& f, int rsv) {
    char a[24];
    const int lh = d.getLineHeight();
    const bool on = _task->getGPSState();
    if (f.place(d.lineStep())) {
      info::row(d, f.at, "GPS", nullptr, rsv);
      info::toggle(d, d.width() - rsv - 2, f.at, on);
    }
    LocationProvider* loc = sensors.getLocationProvider();
#ifdef STATUS_HAS_SKY
    GpsSky* g = skyview::sky();
#else
    GpsSky* g = nullptr;
#endif
    if (!loc) { row(d, f, rsv, "Receiver", "none"); return; }
    const bool fix = loc->isValid();
    if (f.place(lh + 5)) {   // the fix as a chip, satellites used / in view
      const char* st = !on ? "OFF" : !fix ? "NO FIX" : (g && g->fix_mode == 2) ? "2D FIX" : "3D FIX";
      info::chip(d, 1, f.at, st, on && fix);
#ifdef STATUS_HAS_SKY
      if (g) snprintf(a, sizeof(a), "%d/%d sats", skyview::usedCount(*g), g->count);
      else
#endif
      snprintf(a, sizeof(a), "%ld sats", loc->satellitesCount());
      d.drawTextRightAlign(d.width() - rsv - 2, f.at + 1, a);
    }
    const long lat = loc->getLatitude(), lon = loc->getLongitude();
    // The driver reports 999 deg before a first fix (0,0 in the simulator).
    const bool known = labs(lat) <= 90000000L && (lat || lon);
    if (known) snprintf(a, sizeof(a), "%.5f\xc2\xb0%c", labs(lat) / 1e6, lat < 0 ? 'S' : 'N'); else strcpy(a, "-");
    row(d, f, rsv, fix ? "Lat" : "Lat (last)", a);
    if (known) snprintf(a, sizeof(a), "%.5f\xc2\xb0%c", labs(lon) / 1e6, lon < 0 ? 'W' : 'E'); else strcpy(a, "-");
    row(d, f, rsv, fix ? "Lon" : "Lon (last)", a);
    if (fix) {
      telemetry::altText(loc->getAltitude() / 1000.0f, imperial(), L1_INFO, a, sizeof(a));
      row(d, f, rsv, "Altitude", a);
    }
    if (g && fix) {
      if (imperial()) snprintf(a, sizeof(a), "%.0f mph", g->speed_kmh * 0.621371f);
      else            snprintf(a, sizeof(a), "%.0f km/h", g->speed_kmh);
      row(d, f, rsv, "Speed", a);
    }
    if (g) {
      if (g->hdop > 0 && g->hdop < 99) snprintf(a, sizeof(a), "%.1f", g->hdop); else strcpy(a, "-");
      row(d, f, rsv, "HDOP", a);
      if (g->ttff_ms)       snprintf(a, sizeof(a), "%lu s", (unsigned long)(g->ttff_ms / 1000));
      else if (g->start_ms) snprintf(a, sizeof(a), "%lu s", (unsigned long)((millis() - g->start_ms) / 1000));
      else                  strcpy(a, "-");
      row(d, f, rsv, g->ttff_ms ? "First fix" : "Searching", a);
    }
  }
#endif

  void tabPower(DisplayDriver& d, info::Flow& f, int rsv) {
    char a[24];
    const int mv = battMv(), pct = battPct();
    if (f.place(info::bigH(d) + 2)) {
      snprintf(a, sizeof(a), "%d", pct);
      const int w = info::big(d, 1, f.at, a, "%");
      const int mx = 1 + w + 8, mh = d.getLineHeight() + 2;
      info::meter(d, mx, f.at + info::bigH(d) - mh - 1, d.width() - rsv - 2 - mx, mh, pct / 100.0f);
    }
    snprintf(a, sizeof(a), "%d.%02d V", mv / 1000, (mv % 1000) / 10);
    row(d, f, rsv, "Battery", a);
    row(d, f, rsv, "Source", board.isExternalPowered() ? "USB" : "Battery");
    if (f.place(d.lineStep())) {
      d.setCursor(1, f.at);
      d.print("Trend");
      const int x0 = 1 + d.getTextWidth("Trend") + 6;
      info::spark(d, x0, f.at, d.width() - rsv - 2 - x0, d.getLineHeight() - 1, _batt, 20);
    }

    // Sensors: every reading but the battery (above) and the position (GPS).
    if (!_lpp_at || millis() - _lpp_at > 5000) {   // both passes (measure, draw) read one query
      _lpp.reset();
      sensors.querySensors(0xFF, _lpp);
      _lpp_at = millis() | 1;
    }
    static const struct { uint8_t type; const char* name; } NAMES[] = {
      { LPP_TEMPERATURE, "Temperature" }, { LPP_RELATIVE_HUMIDITY, "Humidity" },
      { LPP_BAROMETRIC_PRESSURE, "Pressure" }, { LPP_ALTITUDE, "Altitude" },
      { LPP_CURRENT, "Current" }, { LPP_POWER, "Power" }, { LPP_LUMINOSITY, "Light" },
      { LPP_PERCENTAGE, "Moisture" }, { LPP_DISTANCE, "Distance" }, { LPP_CONCENTRATION, "CO2" },
    };
    bool titled = false;
    LPPReader r(_lpp.getBuffer(), _lpp.getSize());
    uint8_t ch, type;
    while (r.readHeader(ch, type)) {
      const char* name = nullptr;
      for (auto& n : NAMES) if (n.type == type) { name = n.name; break; }
      if (!name) { r.skipData(type); continue; }
      telemetry::lppText(r, type, imperial(), L1_INFO, a, sizeof(a));
      if (!titled) { section(d, f, rsv, "Sensors"); titled = true; }
      row(d, f, rsv, name, a);
    }
  }

  void tabMesh(DisplayDriver& d, info::Flow& f, int rsv) {
    char a[24];
    const int lh = d.getLineHeight();
    if (f.place(d.lineStep())) {
      snprintf(a, sizeof(a), "RX %lu", (unsigned long)totalRx());
      d.setCursor(1, f.at); d.print(a);
      snprintf(a, sizeof(a), "TX %lu", (unsigned long)totalTx());
      d.drawTextRightAlign(d.width() - rsv - 2, f.at, a);
    }
    if (f.place(lh + 6)) info::spark(d, 1, f.at, d.width() - rsv - 3, lh + 3, _traffic, 4);

    static const uint8_t MSG[]    = { PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_GRP_TXT };
    static const uint8_t ADVERT[] = { PAYLOAD_TYPE_ADVERT };
    static const uint8_t ROUTE[]  = { PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_PATH, PAYLOAD_TYPE_TRACE };
    snprintf(a, sizeof(a), "%lu / %lu", (unsigned long)diag::sumByTypes(true, MSG, 2), (unsigned long)diag::sumByTypes(false, MSG, 2));
    row(d, f, rsv, "Messages", a);
    snprintf(a, sizeof(a), "%lu / %lu", (unsigned long)diag::sumByTypes(true, ADVERT, 1), (unsigned long)diag::sumByTypes(false, ADVERT, 1));
    row(d, f, rsv, "Adverts", a);
    snprintf(a, sizeof(a), "%lu / %lu", (unsigned long)diag::sumByTypes(true, ROUTE, 3), (unsigned long)diag::sumByTypes(false, ROUTE, 3));
    row(d, f, rsv, "Acks, paths", a);
    snprintf(a, sizeof(a), "%lu", (unsigned long)the_mesh.getNumForwarded());
    row(d, f, rsv, "Forwarded", a);
    snprintf(a, sizeof(a), "%d", heardLastHour());
    row(d, f, rsv, "Heard in 1 h", a);
    snprintf(a, sizeof(a), "%d", the_mesh.getNumContacts());
    row(d, f, rsv, "Contacts", a);
  }

  void tabSystem(DisplayDriver& d, info::Flow& f, int rsv) {
    char a[32];
    uint32_t up = millis() / 1000;
    unsigned long dd = up / 86400, h = (up % 86400) / 3600, m = (up % 3600) / 60;
    if (dd) snprintf(a, sizeof(a), "%lud %luh %lum", dd, h, m);
    else    snprintf(a, sizeof(a), "%luh %lum", h, m);
    row(d, f, rsv, "Uptime", a);
    diag::shortVersion(a, sizeof(a));
    row(d, f, rsv, "Firmware", a);
    row(d, f, rsv, "Built", FIRMWARE_BUILD_DATE);
    row(d, f, rsv, "Device", board.getManufacturerName());
    uint32_t heap_free, heap_total;
    DeviceDiag::getHeapStats(heap_free, heap_total);
    if (heap_total) snprintf(a, sizeof(a), "%lu of %lu KB", (unsigned long)(heap_free / 1024), (unsigned long)(heap_total / 1024));
    else            strcpy(a, "-");
    row(d, f, rsv, "Free heap", a);
    snprintf(a, sizeof(a), "%lu B", (unsigned long)DeviceDiag::getStackFreeBytes());
    row(d, f, rsv, "Free stack", a);
    snprintf(a, sizeof(a), "%d", the_mesh.getPoolFreeCount());
    row(d, f, rsv, "Packet pool", a);
    snprintf(a, sizeof(a), "%d", the_mesh.getOutboundQueueLen());
    row(d, f, rsv, "Send queue", a);
    const uint16_t err = the_mesh.getErrFlags();
    if (!err) strcpy(a, "none");
    else snprintf(a, sizeof(a), "%s%s%s", (err & ERR_EVENT_FULL) ? "full " : "",
                  (err & ERR_EVENT_CAD_TIMEOUT) ? "CAD " : "", (err & ERR_EVENT_STARTRX_TIMEOUT) ? "RX" : "");
    row(d, f, rsv, "Errors", a);
    section(d, f, rsv, "Font");
    diag::Line lines[diag::MAX_LINES];
    const int n = diag::fontLines(lines);
    for (int i = 0; i < n; i++)
      if (f.place(d.lineStep())) d.drawTextEllipsized(1, f.at, d.width() - rsv - 3, lines[i]);
  }

  // All of a tab's blocks, drawn (or, through a measuring Flow, only counted).
  void layout(DisplayDriver& d, info::Flow& f, int rsv) {
    switch (tab()) {
      case TAB_RADIO:  tabRadio(d, f, rsv);  break;
#if ENV_INCLUDE_GPS == 1
      case TAB_GPS:    tabGps(d, f, rsv);    break;
#endif
      case TAB_POWER:  tabPower(d, f, rsv);  break;
      case TAB_MESH:   tabMesh(d, f, rsv);   break;
      case TAB_SYSTEM: tabSystem(d, f, rsv); break;
      default: break;
    }
  }

public:
  StatusScreen(UITask* task) : _task(task), _lpp(160) {
    addTab(TAB_RADIO, "Radio");
#if ENV_INCLUDE_GPS == 1
    addTab(TAB_GPS, "GPS");
#endif
#ifdef STATUS_HAS_SKY
    addTab(TAB_SKY, "Sky");
#endif
    addTab(TAB_POWER, "Power");
    addTab(TAB_MESH, "Mesh");
    addTab(TAB_SYSTEM, "System");
  }

  // Open on a given tab (Status page: Radio; the GPS shortcuts: GPS / Sky).
  void showTab(uint8_t t) {
    for (uint8_t i = 0; i < _count; i++) if (_tabs[i] == t) { _cur = i; _scroll = 0; }
  }
  // GPS stays awake while its live tabs are on screen.
  bool wantsLiveGps() const { return tab() == TAB_GPS || tab() == TAB_SKY; }

  // Called from the UI loop on every pass: one sample a minute for the lines.
  void sample() {
    const unsigned long now = millis();
    if (_next_sample && (long)(now - _next_sample) < 0) return;
    _next_sample = now + SAMPLE_MS;
    const int nf = (int)radio_driver.getNoiseFloor();
    if (nf) _noise.push((int16_t)nf);
    _batt.push((int16_t)battMv());
    const uint32_t rx = totalRx();
    if (_last_rx || rx) _traffic.push((int16_t)(rx - _last_rx > 30000 ? 30000 : rx - _last_rx));
    _last_rx = rx;
  }

  // Contacts heard from in the last hour (cached for 10 s: it walks them all).
  static int heardLastHour() {
    static unsigned long at = 0;
    static int n = 0;
    if (at && millis() - at < 10000) return n;
    at = millis() | 1;
    n = 0;
    const uint32_t now = rtc_clock.getCurrentTime();
    const int nc = the_mesh.getNumContacts();
    for (int i = 0; i < nc; i++) {
      ContactInfo ci;
      if (!the_mesh.getContactByIdx(i + MAX_ANON_CONTACTS, ci)) continue;
      if (ci.lastmod && ci.lastmod + 3600 >= now) n++;
    }
    return n;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    tabbar::draw(display, _labels, _count, _cur);
    const int top = display.listStart(), bottom = display.height();
    int next = 1000;

#ifdef STATUS_HAS_SKY
    if (tab() == TAB_SKY) {
      next = skyview::render(display, top, _signal, _task->getGPSState());
    } else
#endif
    {
      // Measure first, so a tab taller than the screen gets its scroll column.
      info::Flow m = info::Flow::measure();
      layout(display, m, 0);
      const long view = bottom - top;
      const int rsv = m.total_px > view ? scrollIndicatorColWidth(display) : 0;
      info::Flow f(top, bottom, _scroll);
      layout(display, f, rsv);
      _more = f.more;
      if (rsv) drawScrollIndicatorPx(display, top, view, f.total_px, view, f.skipped_px);
    }

    display.setColor(DisplayDriver::LIGHT);
    if (_reset_menu.active) { _reset_menu.render(display); return 50; }
    return next;
  }

  bool handleInput(char c) override {
    if (_reset_menu.active) {
      auto res = _reset_menu.handleInput(c);
      if (res == PopupMenu::SELECTED && _reset_menu.selectedIndex() == 0) {
        diag::resetCounters();
        _last_rx = 0;
        _task->showAlert("Counters reset", 800);
      }
      return true;
    }
    if (keyIsPrev(c)) { _cur = (_cur + _count - 1) % _count; _scroll = 0; return true; }
    if (keyIsNext(c)) { _cur = (_cur + 1) % _count;          _scroll = 0; return true; }
    if (c == KEY_UP)   { if (_scroll > 0) _scroll--; return true; }
    if (c == KEY_DOWN) { if (_more) _scroll++; return true; }
#if ENV_INCLUDE_GPS == 1
    if (c == KEY_ENTER && tab() == TAB_GPS) { _task->toggleGPS(); return true; }
#endif
    if (c == KEY_ENTER && tab() == TAB_SKY) { _signal = !_signal; return true; }
    if (c == KEY_CONTEXT_MENU && tab() == TAB_MESH) {
      _reset_menu.beginConfirm("Reset counters?", "Reset");
      return true;
    }
    if (c == KEY_CANCEL) { _task->gotoHomeScreen(); return true; }
    return false;
  }
};
