// The tally behind LvProf.h (-D PERF_PROF): a stack of open scopes and a
// table by name. Self time is a scope's cycles less its children's; total
// counts a function once while it's on the stack (layout recurses).
#if defined(PERF_PROF) && defined(ESP32)
#include <Arduino.h>
#include <esp_cpu.h>
#include <string.h>
#include "LvProf.h"

namespace {
struct Ent { const char* name; uint32_t calls, open; uint64_t self, total; };
struct Frame { int ent; uint32_t t0, child; };
const int N = 160, DEPTH = 96;
Ent s_ent[N];
Frame s_stack[DEPTH];
int s_depth = 0, s_used = 0;
int core() { return xPortGetCoreID(); }
const int UI_CORE = 1;   // Arduino's loop()

int find(const char* name) {
  static const char* last_name = nullptr;   // one-entry cache: scopes come in runs
  static int last = -1;
  if (name == last_name) return last;
  for (int i = 0; i < s_used; i++) if (s_ent[i].name == name) return last_name = name, last = i;   // literals: by address
  for (int i = 0; i < s_used; i++) if (!strcmp(s_ent[i].name, name)) return last_name = name, last = i;
  if (s_used == N) return -1;
  s_ent[s_used] = { name, 0, 0, 0, 0 };
  last_name = name; last = s_used;
  return s_used++;
}
}  // namespace

extern "C" void lvprof_begin(const char* name) {
  if (core() != UI_CORE || s_depth >= DEPTH) return;
  int e = find(name);
  if (e < 0) return;
  s_stack[s_depth++] = { e, (uint32_t)esp_cpu_get_cycle_count(), 0 };
}

extern "C" void lvprof_end(const char* name) {
  if (core() != UI_CORE || s_depth == 0) return;
  const uint32_t now = (uint32_t)esp_cpu_get_cycle_count();
  // close up to the matching scope (an early return may have skipped an END)
  while (s_depth > 0) {
    Frame f = s_stack[--s_depth];
    Ent& en = s_ent[f.ent];
    const uint32_t dt = now - f.t0;
    en.calls++;
    en.self += dt - f.child;
    bool outer = true;
    for (int i = 0; i < s_depth; i++) if (s_stack[i].ent == f.ent) { outer = false; break; }
    if (outer) en.total += dt;
    if (s_depth > 0) s_stack[s_depth - 1].child += dt;
    if (en.name == name) break;
  }
}

extern "C" void lvprof_end_cleanup(const char** tag) { lvprof_end(*tag); }

extern "C" void lvprof_reset(void) {
  for (int i = 0; i < s_used; i++) s_ent[i].calls = 0, s_ent[i].self = s_ent[i].total = 0;
}

extern "C" void lvprof_dump(const char* tag, int top) {
  const float mhz = (float)getCpuFrequencyMhz() * 1000.0f;   // cycles per ms
  uint64_t all = 0;
  for (int i = 0; i < s_used; i++) all += s_ent[i].self;
  Serial.printf("PROF %s: %.1f ms profiled\n", tag, all / mhz);
  bool done[N] = {};
  for (int k = 0; k < top; k++) {
    int b = -1;
    for (int i = 0; i < s_used; i++) if (!done[i] && s_ent[i].calls && (b < 0 || s_ent[i].self > s_ent[b].self)) b = i;
    if (b < 0) break;
    done[b] = true;
    Serial.printf("PROF   %-36s %6lu calls  self %6.2f ms  total %6.2f ms\n", s_ent[b].name,
                  (unsigned long)s_ent[b].calls, s_ent[b].self / mhz, s_ent[b].total / mhz);
  }
  lvprof_reset();
}
#endif
