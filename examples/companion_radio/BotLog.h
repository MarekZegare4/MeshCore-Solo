#pragma once
// The bot's recent replies -- who asked what, what it answered, when --
// newest first, for the Bot screen's Log tab. ESP32 only (BOT_LOG): the
// nRF52's heap is short -- and the L2's simulator (BOT_LOG_SIM), which
// stands for one. The ring goes in PSRAM where the board has it, allocated on
// the first reply.

#include <string.h>
#include <stdlib.h>
#if defined(ESP32) || defined(BOT_LOG_SIM)
  #define BOT_LOG 1
#endif
#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif

struct BotLogEntry {
  uint32_t ts;          // RTC time of the reply
  uint8_t  where;       // BotLog::DM / CHANNEL / ROOM
  char     who[24];     // who asked
  char     req[48];     // what they wrote (cut)
  char     reply[80];   // what the bot sent (cut)
};

class BotLog {
public:
  enum : uint8_t { DM, CHANNEL, ROOM };
#ifdef BOT_LOG
  static const int SIZE = 32;

  void add(uint32_t ts, uint8_t where, const char* who, const char* req, const char* reply) {
    if (!_ring) {
#if defined(ESP32)
      _ring = (BotLogEntry*)heap_caps_calloc(SIZE, sizeof(BotLogEntry), MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#endif
      if (!_ring) _ring = (BotLogEntry*)calloc(SIZE, sizeof(BotLogEntry));
      if (!_ring) return;
    }
    BotLogEntry& e = _ring[_head];
    e.ts = ts;
    e.where = where;
    copy(e.who, sizeof(e.who), who);
    copy(e.req, sizeof(e.req), req);
    copy(e.reply, sizeof(e.reply), reply);
    _head = (_head + 1) % SIZE;
    if (_n < SIZE) _n++;
  }
  int count() const { return _n; }
  const BotLogEntry& at(int i) const { return _ring[(_head + SIZE - 1 - i) % SIZE]; }   // 0: the newest

private:
  static void copy(char* out, size_t n, const char* s) {
    strncpy(out, s ? s : "", n - 1);
    out[n - 1] = '\0';
  }
  BotLogEntry* _ring = nullptr;
  int _head = 0, _n = 0;
#else
  void add(uint32_t, uint8_t, const char*, const char*, const char*) {}
  int count() const { return 0; }
  const BotLogEntry& at(int) const { static BotLogEntry e = {}; return e; }
#endif
};
