#pragma once
// The L2's loop task is on the task watchdog (main.cpp: 30 s, then a panic
// and a restart). Work on the loop that's long by design -- the coverage
// files rewritten, a card formatted -- feeds it now and then (feed()) or, for
// one call that can't be broken up, holds it off (a Pause in scope). Off the
// loop task, and where the loop isn't watched, both do nothing.

#include <Arduino.h>

#if defined(ESP32) && defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
#include <esp_task_wdt.h>
extern TaskHandle_t loopTaskHandle;   // the Arduino core's (cores/esp32/main.cpp)
extern bool loopTaskWDTEnabled;

namespace loopwdt {
static inline bool watched() { return loopTaskWDTEnabled && xTaskGetCurrentTaskHandle() == loopTaskHandle; }
static inline void feed() { if (watched()) esp_task_wdt_reset(); }
struct Pause {
  bool was;
  Pause() : was(watched()) { if (was) disableLoopWDT(); }
  ~Pause() { if (was) enableLoopWDT(); }
};
}  // namespace loopwdt
#else
namespace loopwdt {
static inline void feed() {}
struct Pause { Pause() {} };
}  // namespace loopwdt
#endif
