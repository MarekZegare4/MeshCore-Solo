#pragma once
// Decoded map tiles (RGB565, 128 KB each) kept for reuse while panning / zooming
// back, least-recently-used evicted. Buffers live in PSRAM on the ESP32 and are
// allocated on first use, then kept for the session. Tiles found missing are
// remembered separately (no buffer), so looking up coarser zoom levels for the
// overzoom fallback doesn't push real tiles out.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(ESP32)
  #include <esp_heap_caps.h>
#endif

extern "C" void lv_image_cache_drop(const void* src);   // not exported through lvgl.h here

namespace mapview {

#ifdef UI_PERF_TEST
static uint32_t s_perf_tile_us = 0, s_perf_tile_max = 0, s_perf_tile_n = 0;   // renderTile() time
#endif

class TileCache {
public:
  static const int SLOTS = 24;   // 6 on screen at 320x218 + the 14-tile read-ahead ring + a few coarser (3 MB)

  struct Slot {
    int16_t z = -1;
    int32_t x = 0, y = 0;
    bool    present = false;     // false: looked up, there is no tile (don't retry)
    uint32_t used = 0;
    uint16_t* px = nullptr;
    lv_image_dsc_t dsc;
  };

  // The slot holding z/x/y if it was loaded (present, or known missing: a
  // shared slot with present == false), else nullptr.
  Slot* find(int z, int x, int y) {
    for (Slot& s : _slots)
      if (s.z == z && s.x == x && s.y == y) { s.used = ++_tick; return &s; }
    for (const Missing& m : _missing)
      if (m.z == z && m.x == x && m.y == y) return &_miss_slot;
    return nullptr;
  }

  // Decoding runs off the UI loop: request() hands z/x/y to a task on core 0
  // (ESP32; a tile takes ~130 ms to read and decode), which renders it into a
  // spare buffer; poll() swaps that into the least recently used slot, so a
  // slot on screen is never drawn half written. One tile at a time: false
  // while one is in flight. In the sim the job runs at once, the rest alike.
  bool busy() const { return _job_state != JOB_IDLE; }
  bool request(TileProvider& src, int z, int x, int y) {
    if (_job_state != JOB_IDLE) return false;
    _job.src = &src;
    _job.z = (int16_t)z; _job.x = x; _job.y = y;
    _job_stale = false;
    _job_state = JOB_RUNNING;
#if defined(ESP32)
    if (!_task && xTaskCreatePinnedToCore(jobTask, "tiles", 12288, this, tskIDLE_PRIORITY + 1, &_task, 0) != pdPASS) {
      _task = nullptr;
      runJob();   // no task: here, as before
      return true;
    }
    xTaskNotifyGive(_task);
#else
    runJob();
#endif
    return true;
  }

  // A finished tile into its slot. True when one was placed (or found
  // missing): the view has something new. `done` = which, and whether the
  // raster provider went without its trails overlay.
  struct Done { int z; int32_t x, y; TileProvider* src; bool overlay_missed; };
  bool poll(Done& done) {
    if (_job_state != JOB_DONE) return false;
    done = { _job.z, _job.x, _job.y, _job.src, _job.overlay_missed };
    bool keep = !_job_stale;
    if (keep && !_job.present) {   // remember the miss without holding a buffer slot
      Missing& m = _missing[_miss_next++ % MISSING];
      m.z = _job.z; m.x = _job.x; m.y = _job.y;
    } else if (keep) {
      Slot* s = &_slots[0];
      for (Slot& c : _slots) if (c.used < s->used) s = &c;
      lv_image_cache_drop(&s->dsc);   // same descriptor, new pixels
      uint16_t* old = s->px;
      s->px = _spare;
      _spare = old;                   // nullptr while slots are still unallocated: the task makes one
      s->z = _job.z; s->x = _job.x; s->y = _job.y;
      s->used = ++_tick;
      s->present = true;
      memset(&s->dsc, 0, sizeof(s->dsc));
      s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
      s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
      s->dsc.header.w = TILE_PX;
      s->dsc.header.h = TILE_PX;
      s->dsc.header.stride = TILE_PX * 2;
      s->dsc.data_size = TILE_PX * TILE_PX * 2;
      s->dsc.data = (const uint8_t*)s->px;
    }
    _job_state = JOB_IDLE;
    return keep;
  }

  // Forget the "no tile here" answers (new tiles may have been written).
  void forgetMissing() { for (Missing& m : _missing) m.z = -1; }
  // Forget one "no tile here" answer (that tile was just fetched).
  void forgetMissing(int z, int x, int y) {
    for (Missing& m : _missing) if (m.z == z && m.x == x && m.y == y) m.z = -1;
  }

  // Forget one tile, decoded or missing (it changed on the card: redrawn next time).
  void drop(int z, int x, int y) {
    for (Slot& s : _slots) if (s.z == z && s.x == x && s.y == y) { s.z = -1; s.used = 0; }
    forgetMissing(z, x, y);
    if (_job_state != JOB_IDLE && _job.z == z && _job.x == x && _job.y == y) _job_stale = true;
  }

  // Forget everything (e.g. the card was swapped); keeps the buffers.
  void invalidate() {
    for (Slot& s : _slots) { s.z = -1; s.used = 0; }
    forgetMissing();
    if (_job_state != JOB_IDLE) _job_stale = true;   // rendered from what's gone
  }

private:
  enum : uint8_t { JOB_IDLE, JOB_RUNNING, JOB_DONE };
  struct Job { TileProvider* src = nullptr; int16_t z = 0; int32_t x = 0, y = 0; bool present = false, overlay_missed = false; };
  Job _job;
  volatile uint8_t _job_state = JOB_IDLE;   // RUNNING: the task owns _job and _spare
  volatile bool _job_stale = false;         // dropped / invalidated meanwhile: thrown away
  uint16_t* _spare = nullptr;               // the buffer the next tile is rendered into
#if defined(ESP32)
  TaskHandle_t _task = nullptr;
  static void jobTask(void* arg) {
    for (;;) {
      ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
      static_cast<TileCache*>(arg)->runJob();
      if (TaskHandle_t t = lvport::s_loop_task) xTaskNotifyGive(t);   // the loop places it
    }
  }
#endif
  void runJob() {
    if (!_spare) {
#if defined(ESP32)
      _spare = (uint16_t*)heap_caps_malloc(TILE_PX * TILE_PX * 2, MALLOC_CAP_SPIRAM);
#else
      _spare = (uint16_t*)malloc(TILE_PX * TILE_PX * 2);
#endif
    }
#ifdef UI_PERF_TEST
    uint32_t t0 = micros();
#endif
    _job.present = _spare && _job.src->renderTile(_job.z, _job.x, _job.y, _spare);
#ifdef UI_PERF_TEST
    uint32_t dt = micros() - t0;
    if (_job.present) { s_perf_tile_us += dt; s_perf_tile_n++; if (dt > s_perf_tile_max) s_perf_tile_max = dt; }
#endif
    _job.overlay_missed = _job.src->overlayMissed();
#if defined(ESP32)
    __sync_synchronize();   // the results before the state the loop reads
#endif
    _job_state = JOB_DONE;
  }
  struct Missing { int16_t z = -1; int32_t x = 0, y = 0; };
  static const int MISSING = 48;
  Slot _slots[SLOTS];
  Missing _missing[MISSING];
  int _miss_next = 0;
  Slot _miss_slot;   // present == false
  uint32_t _tick = 0;
};

}  // namespace mapview
