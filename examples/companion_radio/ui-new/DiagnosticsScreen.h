#pragma once
#include <helpers/ui/DisplayDriver.h>
#include <helpers/ui/UIScreen.h>
#include <helpers/DeviceDiag.h>
#include <Arduino.h>
#include <stdarg.h>
#include "icons.h"
#include "PopupMenu.h"
#include "TabBar.h"
#include "../MyMesh.h"

extern MyMesh the_mesh;

// Device diagnostics, organised as a circular tab carousel (shared TabBar.h,
// same idiom as BotScreen/NearbyScreen). LEFT/RIGHT switches tabs; UP/DOWN
// scrolls within the active tab:
//   Live   — live counters: uptime, packet counts by category (RX/TX),
//            forwarded count, heap/stack headroom, radio signal, pool/queue
//            depth, error flags. Hold Enter resets the counters.
//   System — static device identity: firmware version + build date, device
//            model, node name, and the active radio parameters.
//   Font   — a rendering test card: one sample line per script the UI font
//            claims to cover (Latin, diacritics, Greek, Cyrillic, digits,
//            symbols), so the on-device font can be eyeballed for coverage.
// The packet counts are built from Dispatcher's per-type counters — grouped
// into a handful of categories rather than all 16 raw PAYLOAD_TYPE_* values,
// so they stay readable on a 128x64 OLED. Every tab falls back to scrolling on
// screens too small to show its rows at once.
class DiagnosticsScreen : public UIScreen {
  UITask* _task;
  int _scroll = 0;
  uint8_t _tab = 0;        // persists across visits (like BotScreen's _tab)
  PopupMenu _reset_menu;   // Live tab, Hold Enter → 1-item "Reset counters" action menu (Back dismisses)

  enum Tab : uint8_t { TAB_LIVE, TAB_SYSTEM, TAB_FONT, TAB_COUNT };
  static const char* const TAB_LABELS[TAB_COUNT];

  struct Row { const char* label; char value[20]; };
  static const int MAX_ROWS = 14;
  Row _rows[MAX_ROWS];
  int _row_count = 0;

  // Full-width text lines (System / Font tabs) — long values (device model,
  // node name) don't fit the Live tab's narrow right-aligned value column, so
  // these tabs draw one ellipsized line each instead of a label/value pair.
  static const int MAX_LINES = 15;
  char _lines[MAX_LINES][40];
  int _line_count = 0;

  void addRow(const char* label, const char* value) {
    if (_row_count >= MAX_ROWS) return;
    _rows[_row_count].label = label;
    strncpy(_rows[_row_count].value, value, sizeof(_rows[_row_count].value) - 1);
    _rows[_row_count].value[sizeof(_rows[_row_count].value) - 1] = 0;
    _row_count++;
  }
  // "12345/12345" rather than "rx12345 tx12345" — at 5-digit counts (a busy
  // repeater after weeks of uptime) the longer form collides with the value
  // column on a 128px OLED when paired with a long label like "Ack/Path".
  // The "Total rx/tx" label spells out the rx/tx order once for every row below it.
  void addRxTxRow(const char* label, uint32_t rx, uint32_t tx) {
    char buf[20];
    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)rx, (unsigned long)tx);
    addRow(label, buf);
  }

  void addLine(const char* fmt, ...) {
    if (_line_count >= MAX_LINES) return;
    va_list ap; va_start(ap, fmt);
    vsnprintf(_lines[_line_count], sizeof(_lines[_line_count]), fmt, ap);
    va_end(ap);
    _line_count++;
  }

  static uint32_t sumByTypes(bool recv, const uint8_t* types, int n) {
    uint32_t total = 0;
    for (int i = 0; i < n; i++)
      total += recv ? the_mesh.getNumRecvByType(types[i]) : the_mesh.getNumSentByType(types[i]);
    return total;
  }

  void buildLiveRows() {
    _row_count = 0;

    uint32_t up_secs = millis() / 1000;
    char buf[20];
    uint32_t d = up_secs / 86400, h = (up_secs % 86400) / 3600, m = (up_secs % 3600) / 60, s = up_secs % 60;
    if (d > 0) snprintf(buf, sizeof(buf), "%lud %02lu:%02lu:%02lu", (unsigned long)d, (unsigned long)h, (unsigned long)m, (unsigned long)s);
    else       snprintf(buf, sizeof(buf), "%02lu:%02lu:%02lu", (unsigned long)h, (unsigned long)m, (unsigned long)s);
    addRow("Uptime", buf);

    static const uint8_t MSG_TYPES[]   = { PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_GRP_TXT };
    static const uint8_t ADVERT_TYPES[] = { PAYLOAD_TYPE_ADVERT };
    static const uint8_t ROUTE_TYPES[] = { PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_PATH, PAYLOAD_TYPE_TRACE };
    static const uint8_t OTHER_TYPES[] = { PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_ANON_REQ,
                                            PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
                                            PAYLOAD_TYPE_RAW_CUSTOM };

    uint32_t msg_rx = sumByTypes(true, MSG_TYPES, 2), msg_tx = sumByTypes(false, MSG_TYPES, 2);
    uint32_t adv_rx = sumByTypes(true, ADVERT_TYPES, 1), adv_tx = sumByTypes(false, ADVERT_TYPES, 1);
    uint32_t rte_rx = sumByTypes(true, ROUTE_TYPES, 3), rte_tx = sumByTypes(false, ROUTE_TYPES, 3);
    uint32_t oth_rx = sumByTypes(true, OTHER_TYPES, 7), oth_tx = sumByTypes(false, OTHER_TYPES, 7);

    addRxTxRow("Total rx/tx", msg_rx + adv_rx + rte_rx + oth_rx, msg_tx + adv_tx + rte_tx + oth_tx);
    addRxTxRow("Msg",     msg_rx, msg_tx);
    addRxTxRow("Advert",  adv_rx, adv_tx);
    addRxTxRow("Ack/Path", rte_rx, rte_tx);
    addRxTxRow("Other",   oth_rx, oth_tx);

    snprintf(buf, sizeof(buf), "%lu", (unsigned long)the_mesh.getNumForwarded());
    addRow("Forwarded", buf);

    uint32_t heap_free, heap_total;
    DeviceDiag::getHeapStats(heap_free, heap_total);
    if (heap_total > 0) snprintf(buf, sizeof(buf), "%lu/%luKB", (unsigned long)(heap_free / 1024), (unsigned long)(heap_total / 1024));
    else                strcpy(buf, "N/A");
    addRow("Heap free", buf);

    uint32_t stack_free = DeviceDiag::getStackFreeBytes();
    snprintf(buf, sizeof(buf), "%luB", (unsigned long)stack_free);
    addRow("Stack free", buf);

    snprintf(buf, sizeof(buf), "%d dBm", (int)radio_driver.getNoiseFloor());
    addRow("Noise floor", buf);
    snprintf(buf, sizeof(buf), "%d/%.1f", (int)radio_driver.getLastRSSI(), radio_driver.getLastSNR());
    addRow("RSSI/SNR", buf);

    snprintf(buf, sizeof(buf), "%d", the_mesh.getPoolFreeCount());
    addRow("Pool free", buf);
    snprintf(buf, sizeof(buf), "%d", the_mesh.getOutboundQueueLen());
    addRow("Queue", buf);

    // Radio error flags since boot/reset, decoded to short tokens (F=queue full,
    // C=CAD timeout, R=RX-start timeout). "OK" when none have fired.
    uint16_t err = the_mesh.getErrFlags();
    if (err == 0) strcpy(buf, "OK");
    else {
      buf[0] = '\0';
      if (err & ERR_EVENT_FULL)            strcat(buf, "F ");
      if (err & ERR_EVENT_CAD_TIMEOUT)     strcat(buf, "C ");
      if (err & ERR_EVENT_STARTRX_TIMEOUT) strcat(buf, "R ");
      int len = strlen(buf);   // drop the trailing space so right-alignment sits flush
      if (len > 0 && buf[len - 1] == ' ') buf[len - 1] = '\0';
    }
    addRow("Errors", buf);

    // RX duty-cycle watchdog recovery counts since boot/reset (soft re-arm /
    // hard chip reset). Always 0/0 on radios or profiles that never arm
    // duty-cycle power-save (repeaters force it off — see applyPowerSave()).
    snprintf(buf, sizeof(buf), "%lu/%lu", (unsigned long)radio_driver.getRxPsWatchdogSoftCount(),
             (unsigned long)radio_driver.getRxPsWatchdogHardCount());
    addRow("RXPS wd s/h", buf);
  }

  void buildSystemLines() {
    _line_count = 0;

    // Firmware version, minus the commit-hash suffix build.sh appends as the
    // LAST dash-segment (v1.16-solo.0-abcdef -> v1.16-solo.0) — same strip the
    // boot screen does; the last dash, not the first, since a tag like
    // v1.21-rc1 has a dash of its own before the hash.
    const char* ver = FIRMWARE_VERSION;
    const char* dash = strrchr(ver, '-');
    int plen = dash ? (int)(dash - ver) : (int)strlen(ver);
    char sv[24];
    if (plen >= (int)sizeof(sv)) plen = sizeof(sv) - 1;
    memcpy(sv, ver, plen); sv[plen] = '\0';

    addLine("FW %s", sv);
    addLine("Built %s", FIRMWARE_BUILD_DATE);
    addLine("Dev %s", board.getManufacturerName());
    addLine("Node %s", the_mesh.getNodeName());

    NodePrefs* p = the_mesh.getNodePrefs();
    if (p) {
      addLine("Freq %.3f MHz", p->freq);
      addLine("SF%u BW%.0f CR%u", (unsigned)p->sf, p->bw, (unsigned)p->cr);
      addLine("TX %d dBm", (int)p->tx_power_dbm);
    }
  }

  // One sample line per script/keyboard alphabet the UI font claims to cover
  // (all inside the U+0020-04FF glyph range the OLED misc-fixed / e-ink Lemon
  // fonts carry), so the font's coverage can be checked by eye on real
  // hardware. "Acc" samples one variant from each KeyboardWidget.h
  // KB_ACCENT_VARIANTS group (the Hold-Enter accent popup that replaced the
  // old per-language alt-alphabet pages) so every accented letter actually
  // reachable in the UI still gets an eyeball check.
  void buildFontLines() {
    _line_count = 0;
    addLine("Latin ABCabc xyz");
    addLine("Acc áçďéíłñóřśťúýź");
    addLine("Grk ΑΒΓ αβγξω");          // ΑΒΓ αβγξω
    addLine("Cyr АБВ абвжя");          // АБВ абвжя
    addLine("Num 0123456789");
    addLine("Sym @#&*()[]{}/\\+=");
  }

  // Shared scrollable renderer for the label/value Live tab.
  void renderRows(DisplayDriver& display) {
    const int item_h  = display.lineStep();
    const int start_y = display.listStart();
    int visible = display.listVisible(item_h);
    if (visible < 1) visible = 1;
    clampScroll(_row_count, visible);

    const int reserve = scrollIndicatorReserve(display, _row_count, visible);
    for (int i = 0; i < visible && (_scroll + i) < _row_count; i++) {
      const Row& r = _rows[_scroll + i];
      int y = start_y + i * item_h;
      display.setCursor(2, y);
      display.print(r.label);
      display.drawTextRightAlign(display.width() - reserve - 2, y, r.value);
    }
    drawScrollIndicator(display, start_y, visible * item_h, _row_count, visible, _scroll);
  }

  // Shared scrollable renderer for the full-width System / Font tabs.
  void renderLines(DisplayDriver& display) {
    const int item_h  = display.lineStep();
    const int start_y = display.listStart();
    int visible = display.listVisible(item_h);
    if (visible < 1) visible = 1;
    clampScroll(_line_count, visible);

    const int reserve = scrollIndicatorReserve(display, _line_count, visible);
    for (int i = 0; i < visible && (_scroll + i) < _line_count; i++) {
      int y = start_y + i * item_h;
      display.drawTextEllipsized(2, y, display.width() - reserve - 4, _lines[_scroll + i]);
    }
    drawScrollIndicator(display, start_y, visible * item_h, _line_count, visible, _scroll);
  }

  void clampScroll(int total, int visible) {
    int max_scroll = total - visible;
    if (max_scroll < 0) max_scroll = 0;
    if (_scroll > max_scroll) _scroll = max_scroll;
    if (_scroll < 0) _scroll = 0;
  }

public:
  DiagnosticsScreen(UITask* task) : _task(task) {}

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    tabbar::draw(display, TAB_LABELS, TAB_COUNT, _tab);

    switch (_tab) {
      case TAB_SYSTEM: buildSystemLines(); renderLines(display); break;
      case TAB_FONT:   buildFontLines();   renderLines(display); break;
      default:         buildLiveRows();    renderRows(display);  break;
    }

    display.setColor(DisplayDriver::LIGHT);
    if (_reset_menu.active) _reset_menu.render(display);
    // Live counters refresh once a second; the static System/Font cards don't
    // change, so they can idle. The reset popup wants a snappier redraw.
    if (_reset_menu.active) return 50;
    return _tab == TAB_LIVE ? 1000 : 2000;
  }

  bool handleInput(char c) override {
    if (_reset_menu.active) {
      auto res = _reset_menu.handleInput(c);   // Back/Cancel dismisses; the only item is "Reset counters"
      if (res == PopupMenu::SELECTED) {
        the_mesh.resetStats();     // zeroes Dispatcher per-type counters + Mesh forward count + err flags
        radio_driver.resetStats(); // zeroes the radio's own counters, incl. RXPS watchdog soft/hard counts
        _task->showAlert("Counters reset", 800);
      }
      return true;
    }
    if (keyIsPrev(c)) { _tab = (_tab + TAB_COUNT - 1) % TAB_COUNT; _scroll = 0; return true; }
    if (keyIsNext(c)) { _tab = (_tab + 1) % TAB_COUNT;            _scroll = 0; return true; }
    if (c == KEY_UP)   { if (_scroll > 0) _scroll--; return true; }
    if (c == KEY_DOWN) { _scroll++; return true; }   // clamped in render()
    if (c == KEY_CONTEXT_MENU && _tab == TAB_LIVE) {   // Hold Enter — reset the live counters
      _reset_menu.begin("Diagnostics", 1);
      _reset_menu.addItem("Reset counters");
      return true;
    }
    if (c == KEY_CANCEL) { _task->gotoToolsScreen(); return true; }
    return false;
  }
};

const char* const DiagnosticsScreen::TAB_LABELS[DiagnosticsScreen::TAB_COUNT] = { "Live", "System", "Font" };
