#pragma once
// Device diagnostics shared by ui-new's Tools > Diagnostics and ui-lvgl's
// Settings > Diagnostics: the live counters (uptime, packets by category,
// heap / stack, radio, queue, error flags), the static system card (firmware,
// board, radio parameters) and the font test card. Each frontend lays the rows
// out its own way.
//
// Packet counts come from Dispatcher's per-type counters, grouped into a few
// categories rather than all 16 raw PAYLOAD_TYPE_* values, so they stay
// readable on a 128x64 OLED.

#include <helpers/DeviceDiag.h>
#include <stdarg.h>

namespace diag {

struct Row { const char* label; char value[20]; };
static const int MAX_ROWS = 14;
static const int MAX_LINES = 15;
typedef char Line[40];

inline void addRow(Row* rows, int& n, const char* label, const char* fmt, ...) {
  if (n >= MAX_ROWS) return;
  rows[n].label = label;
  va_list ap; va_start(ap, fmt);
  vsnprintf(rows[n].value, sizeof(rows[n].value), fmt, ap);
  va_end(ap);
  n++;
}
inline void addLine(Line* lines, int& n, const char* fmt, ...) {
  if (n >= MAX_LINES) return;
  va_list ap; va_start(ap, fmt);
  vsnprintf(lines[n], sizeof(lines[n]), fmt, ap);
  va_end(ap);
  n++;
}

inline uint32_t sumByTypes(bool recv, const uint8_t* types, int n) {
  uint32_t total = 0;
  for (int i = 0; i < n; i++)
    total += recv ? the_mesh.getNumRecvByType(types[i]) : the_mesh.getNumSentByType(types[i]);
  return total;
}

// Live counters, label / value. Returns the row count.
inline int liveRows(Row* rows) {
  int n = 0;
  uint32_t up = millis() / 1000;
  unsigned long d = up / 86400, h = (up % 86400) / 3600, m = (up % 3600) / 60, s = up % 60;
  if (d > 0) addRow(rows, n, "Uptime", "%lud %02lu:%02lu:%02lu", d, h, m, s);
  else       addRow(rows, n, "Uptime", "%02lu:%02lu:%02lu", h, m, s);

  static const uint8_t MSG[]    = { PAYLOAD_TYPE_TXT_MSG, PAYLOAD_TYPE_GRP_TXT };
  static const uint8_t ADVERT[] = { PAYLOAD_TYPE_ADVERT };
  static const uint8_t ROUTE[]  = { PAYLOAD_TYPE_ACK, PAYLOAD_TYPE_PATH, PAYLOAD_TYPE_TRACE };
  static const uint8_t OTHER[]  = { PAYLOAD_TYPE_REQ, PAYLOAD_TYPE_RESPONSE, PAYLOAD_TYPE_ANON_REQ,
                                    PAYLOAD_TYPE_GRP_DATA, PAYLOAD_TYPE_MULTIPART, PAYLOAD_TYPE_CONTROL,
                                    PAYLOAD_TYPE_RAW_CUSTOM };
  unsigned long msg_rx = sumByTypes(true, MSG, 2),    msg_tx = sumByTypes(false, MSG, 2);
  unsigned long adv_rx = sumByTypes(true, ADVERT, 1), adv_tx = sumByTypes(false, ADVERT, 1);
  unsigned long rte_rx = sumByTypes(true, ROUTE, 3),  rte_tx = sumByTypes(false, ROUTE, 3);
  unsigned long oth_rx = sumByTypes(true, OTHER, 7),  oth_tx = sumByTypes(false, OTHER, 7);
  // "12345/12345" rather than "rx12345 tx12345": 5-digit counts would collide
  // with a long label on a 128 px OLED. "Total rx/tx" spells out the order once.
  addRow(rows, n, "Total rx/tx", "%lu/%lu", msg_rx + adv_rx + rte_rx + oth_rx, msg_tx + adv_tx + rte_tx + oth_tx);
  addRow(rows, n, "Msg", "%lu/%lu", msg_rx, msg_tx);
  addRow(rows, n, "Advert", "%lu/%lu", adv_rx, adv_tx);
  addRow(rows, n, "Ack/Path", "%lu/%lu", rte_rx, rte_tx);
  addRow(rows, n, "Other", "%lu/%lu", oth_rx, oth_tx);
  addRow(rows, n, "Forwarded", "%lu", (unsigned long)the_mesh.getNumForwarded());

  uint32_t heap_free, heap_total;
  DeviceDiag::getHeapStats(heap_free, heap_total);
  if (heap_total > 0) addRow(rows, n, "Heap free", "%lu/%luKB", (unsigned long)(heap_free / 1024), (unsigned long)(heap_total / 1024));
  else                addRow(rows, n, "Heap free", "N/A");
  addRow(rows, n, "Stack free", "%luB", (unsigned long)DeviceDiag::getStackFreeBytes());

  addRow(rows, n, "Noise floor", "%d dBm", (int)radio_driver.getNoiseFloor());
  addRow(rows, n, "RSSI/SNR", "%d/%.1f", (int)radio_driver.getLastRSSI(), radio_driver.getLastSNR());
  addRow(rows, n, "Pool free", "%d", the_mesh.getPoolFreeCount());
  addRow(rows, n, "Queue", "%d", the_mesh.getOutboundQueueLen());

  // Radio error flags since boot / reset (F = queue full, C = CAD timeout,
  // R = RX-start timeout), "OK" when none have fired.
  uint16_t err = the_mesh.getErrFlags();
  if (err == 0) addRow(rows, n, "Errors", "OK");
  else addRow(rows, n, "Errors", "%s%s%s", (err & ERR_EVENT_FULL) ? "F " : "",
              (err & ERR_EVENT_CAD_TIMEOUT) ? "C " : "", (err & ERR_EVENT_STARTRX_TIMEOUT) ? "R " : "");
  if (err) {   // drop the trailing space so right-alignment sits flush
    char* v = rows[n - 1].value;
    size_t len = strlen(v);
    if (len && v[len - 1] == ' ') v[len - 1] = '\0';
  }
#if FEAT_RX_POWERSAVE
  // RX duty-cycle watchdog recoveries since boot / reset (soft re-arm / hard
  // chip reset); 0/0 where duty-cycle power-save never arms.
  addRow(rows, n, "RXPS wd s/h", "%lu/%lu", (unsigned long)radio_driver.getRxPsWatchdogSoftCount(),
         (unsigned long)radio_driver.getRxPsWatchdogHardCount());
#endif
  return n;
}

// Zeroes the packet / forward counters, error flags and the radio's own counts.
inline void resetCounters() {
  the_mesh.resetStats();
  radio_driver.resetStats();
}

// Firmware version minus the commit-hash suffix build.sh appends as the last
// dash-segment (v1.16-solo.0-abcdef -> v1.16-solo.0; the last dash, since a
// tag like v1.21-rc1 has one of its own).
inline void shortVersion(char* out, size_t n) {
  const char* ver = FIRMWARE_VERSION;
  const char* dash = strrchr(ver, '-');
  size_t len = dash ? (size_t)(dash - ver) : strlen(ver);
  if (len >= n) len = n - 1;
  memcpy(out, ver, len);
  out[len] = '\0';
}

// One sample per script the UI font covers, to check coverage by eye. "Acc"
// takes one letter from each of the keyboard's accent-popup groups.
inline int fontLines(Line* lines) {
  int n = 0;
  addLine(lines, n, "Latin ABCabc xyz");
  addLine(lines, n, "Acc áçďéíłñóřśťúýź");
  addLine(lines, n, "Grk ΑΒΓ αβγξω");
  addLine(lines, n, "Cyr АБВ абвжя");
  addLine(lines, n, "Num 0123456789");
  addLine(lines, n, "Sym @#&*()[]{}/\\+=");
  return n;
}

}  // namespace diag
