#pragma once
// After a crash, a watchdog restart or a brown-out: a report on the SD card
// (meshcore/crash/), for the user to send. In it: why the device started
// again, the crashed task's backtrace from the core dump (decode it with the
// build's firmware.elf: xtensa-esp32s3-elf-addr2line -pfiaC -e firmware.elf
// <addresses>), and the last things done before it -- taps, screens, popups,
// map tools, WiFi, the screen going off -- each with the time and the free
// internal heap.
//
// Those last things are kept in RTC memory that a restart doesn't clear
// (RTC_NOINIT): nothing is written while the device runs, so it costs no
// card wear and no power; only after a restart that wasn't a normal one is
// the ring read back and written out. note() is for the loop task only.

#include <Arduino.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/stat.h>

#if defined(ESP32) && ESP_ARDUINO_VERSION_MAJOR >= 3
  #include <esp_attr.h>
  #include <esp_system.h>
  #include <esp_core_dump.h>
  #include <esp_heap_caps.h>
  #define CRASHLOG_ON 1
#endif

namespace crashlog {

static const uint32_t MAGIC = 0x534C4F47;   // "SLOG"
static const int N = 32;
struct Ev {
  uint32_t ms;        // since the start
  uint32_t t;         // UTC, 0 when the clock wasn't set
  uint16_t heap_kb;   // free internal heap
  char what[22];
};
struct Ring {
  uint32_t magic;
  uint32_t head;      // events noted so far (the ring holds the last N)
  uint32_t boot_t;    // UTC at the start, or 0
  Ev ev[N];
};

#ifdef CRASHLOG_ON
RTC_NOINIT_ATTR static Ring s_ring;
#else
static Ring s_ring;
#endif
static Ring s_prev;              // the run before, when it ended badly
static bool s_report = false;    // s_prev is to be written out
static int s_reason = 0;

static uint32_t clockNow() {
  const uint32_t t = rtc_clock.getCurrentTime();
  return t >= 1700000000UL ? t : 0;
}

static void note(const char* fmt, ...) {
  if (s_ring.magic != MAGIC) return;   // before begin()
  Ev& e = s_ring.ev[s_ring.head % N];
  e.ms = millis();
  e.t = clockNow();
#ifdef CRASHLOG_ON
  const size_t fr = heap_caps_get_free_size(MALLOC_CAP_INTERNAL) / 1024;
  e.heap_kb = (uint16_t)(fr > 0xFFFF ? 0xFFFF : fr);
#else
  e.heap_kb = 0;
#endif
  va_list ap;
  va_start(ap, fmt);
  vsnprintf(e.what, sizeof(e.what), fmt, ap);
  va_end(ap);
  s_ring.head++;
}

// First thing at the start: the run before kept if it ended badly, the ring
// begun afresh.
static void begin() {
#ifdef CRASHLOG_ON
  const esp_reset_reason_t r = esp_reset_reason();
  s_reason = (int)r;
  const bool bad = r == ESP_RST_PANIC || r == ESP_RST_TASK_WDT || r == ESP_RST_INT_WDT || r == ESP_RST_WDT ||
                   r == ESP_RST_BROWNOUT;
  if (bad) {
    s_report = true;
    if (s_ring.magic == MAGIC) {
      memcpy(&s_prev, &s_ring, sizeof(s_prev));
      for (int i = 0; i < N; i++) s_prev.ev[i].what[sizeof(s_prev.ev[i].what) - 1] = '\0';
    } else {
      memset(&s_prev, 0, sizeof(s_prev));   // the RTC memory lost too: the dump alone
    }
  }
#endif
  memset(&s_ring, 0, sizeof(s_ring));
  s_ring.magic = MAGIC;
  s_ring.boot_t = clockNow();
}

static void fmtUtc(char* b, size_t n, uint32_t t, bool date) {
  if (!t) { snprintf(b, n, "%s", date ? "clock not set" : "--:--:--"); return; }
  time_t tt = (time_t)t;
  struct tm* g = gmtime(&tt);
  if (date) snprintf(b, n, "%04d-%02d-%02d %02d:%02d:%02d UTC", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday, g->tm_hour, g->tm_min, g->tm_sec);
  else snprintf(b, n, "%02d:%02d:%02d", g->tm_hour, g->tm_min, g->tm_sec);
}

// Once the card is mounted (`card`): the report, if there's one to write.
// The file's name, for the toast, in `name` (empty when nothing was written).
static bool writeReport(bool card, const char* fw, const char* board, const char* reason, char* name, size_t nn) {
  if (nn) name[0] = '\0';
  if (!s_report || !card) return false;
  s_report = false;
  mkdir("/sdcard/meshcore", 0777);
  mkdir("/sdcard/meshcore/crash", 0777);
  const uint32_t now = clockNow();
  char fn[40];
  if (now) {
    time_t tt = (time_t)now;
    struct tm* g = gmtime(&tt);
    snprintf(fn, sizeof(fn), "%04d%02d%02d-%02d%02d%02d.txt", g->tm_year + 1900, g->tm_mon + 1, g->tm_mday, g->tm_hour, g->tm_min, g->tm_sec);
  } else {
#ifdef CRASHLOG_ON
    snprintf(fn, sizeof(fn), "nodate-%08lx.txt", (unsigned long)esp_random());
#else
    snprintf(fn, sizeof(fn), "nodate-%08lx.txt", (unsigned long)millis());
#endif
  }
  char path[72];
  snprintf(path, sizeof(path), "/sdcard/meshcore/crash/%s", fn);
  FILE* f = fopen(path, "w");
  if (!f) return false;
  char ts[32];
  fprintf(f, "Solo %s, %s\n", fw, board);
  fmtUtc(ts, sizeof(ts), now, true);
  fprintf(f, "Started again: %s (reset reason %d), %s\n", reason, s_reason, ts);
#ifdef CRASHLOG_ON
  esp_core_dump_summary_t sum;
  if (esp_core_dump_image_check() == ESP_OK && esp_core_dump_get_summary(&sum) == ESP_OK) {
    const bool fresh = s_reason != ESP_RST_BROWNOUT;   // a brown-out leaves no dump: what's there is older
    fprintf(f, "\nCore dump%s: task %.16s, pc 0x%08lx, firmware sha %.16s\n", fresh ? "" : " (an earlier crash's)",
            sum.exc_task, (unsigned long)sum.exc_pc, (const char*)sum.app_elf_sha256);
    fprintf(f, "Exception cause %lu, address 0x%08lx\n", (unsigned long)sum.ex_info.exc_cause, (unsigned long)sum.ex_info.exc_vaddr);
    fprintf(f, "Backtrace%s:", sum.exc_bt_info.corrupted ? " (corrupted)" : "");
    for (uint32_t i = 0; i < sum.exc_bt_info.depth && i < 16; i++) fprintf(f, " 0x%08lx", (unsigned long)sum.exc_bt_info.bt[i]);
    fprintf(f, "\n");
  } else {
    fprintf(f, "\nNo core dump.\n");
  }
#endif
  const uint32_t cnt = s_prev.head < (uint32_t)N ? s_prev.head : (uint32_t)N;
  fmtUtc(ts, sizeof(ts), s_prev.boot_t, true);
  fprintf(f, "\nThe run before: started %s; %lu things noted, the last %lu (seconds since its start, UTC, free heap):\n",
          ts, (unsigned long)s_prev.head, (unsigned long)cnt);
  for (uint32_t k = s_prev.head - cnt; k < s_prev.head; k++) {
    const Ev& e = s_prev.ev[k % N];
    fmtUtc(ts, sizeof(ts), e.t, false);
    fprintf(f, "  %8.1f s  %s  %3u KB  %s\n", e.ms / 1000.0f, ts, (unsigned)e.heap_kb, e.what);
  }
  const bool ok = fclose(f) == 0;
  if (ok && nn) snprintf(name, nn, "%s", fn);
  return ok;
}

}  // namespace crashlog
