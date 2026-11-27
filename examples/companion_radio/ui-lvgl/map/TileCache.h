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
    // The stack in PSRAM: internal RAM is what WiFi starts on (it fails to
    // with ~25 KB left), and the task only reads the SD card, never the flash.
    if (!_task && xTaskCreatePinnedToCoreWithCaps(jobTask, "tiles", 12288, this, tskIDLE_PRIORITY + 1, &_task, 0,
                                                  MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT) != pdPASS) {
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
#if defined(ESP32)
    __sync_synchronize();   // pairs with runJob()'s: the state before the results it covers
#endif
    done = { _job.z, _job.x, _job.y, _job.src, _job.overlay_missed };
    bool keep = !_job_stale;
    if (keep && !_job.present) {   // remember the miss without holding a buffer slot
      Missing& m = _missing[_miss_next++ % MISSING];
      m.z = _job.z; m.x = _job.x; m.y = _job.y;
    } else if (keep) {
      Slot* s = victim();
      lv_image_cache_drop(&s->dsc);   // same descriptor, new pixels
      uint16_t* old = s->px;
      s->px = _spare;
      _spare = old;                   // nullptr while slots are still unallocated: the task makes one
      s->z = _job.z; s->x = _job.x; s->y = _job.y;
      s->used = ++_tick;
      s->present = true;
      memset(&s->dsc, 0, sizeof(s->dsc));
      s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
#ifdef UI_FLUSH_DMA
      s->dsc.header.cf = LV_COLOR_FORMAT_RGB565_SWAPPED;
#else
      s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
#endif
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

  // A tile is being decoded on the other core (into _spare, the task's then).
  bool decoding() const { return _job_state == JOB_RUNNING; }

  // The decoded tiles' buffers (3 MB once the map has been panned round) back
  // to the heap, for a screen needing a large one: the map decodes them again
  // when it's next shown. Not while decoding(); a tile finished meanwhile is
  // thrown away. Only with the map off the screen: its images point at slots.
  bool release() {
    if (_job_state == JOB_RUNNING) return false;
    _job_state = JOB_IDLE;
    for (Slot& s : _slots) {
      if (s.px) {
        lv_image_cache_drop(&s.dsc);
        free(s.px);
        s.px = nullptr;
      }
      s.z = -1; s.used = 0; s.present = false;
      s.dsc.data = nullptr; s.dsc.data_size = 0;
    }
    free(_spare);
    _spare = nullptr;
    return true;
  }

  // The view about to be laid out: a slot looked up from here on (the tiles
  // on screen, their coarser stand-ins, the read-ahead ring) is not given up
  // for another tile while an older one can be.
  void markView() { _mark = _tick; }

  // Read-ahead may decode one more: there is a slot to put it in without
  // pushing out a tile the view uses. False: the ring is as full as memory
  // allows -- decoding on would evict the screen's own tiles, which then come
  // back and push out the next (the top-left tile flickered, 2026-11-25).
  bool canTake() const {
    for (const Slot& c : _slots) {
      if (!c.px && roomForAnother()) return true;
      if (c.px && c.used <= _mark) return true;
    }
    return false;
  }

private:
  // Where a decoded tile goes: an unallocated slot while PSRAM allows another
  // buffer, else the least recently used one outside the view, else (the view
  // alone needs more) a new buffer down to FLOOR, and only then a view tile.
  Slot* victim() {
    Slot* empty = nullptr;
    Slot* old = nullptr;
    Slot* any = nullptr;
    for (Slot& c : _slots) {
      if (!c.px) { if (!empty || c.used < empty->used) empty = &c; continue; }
      if (c.used <= _mark && (!old || c.used < old->used)) old = &c;
      if (!any || c.used < any->used) any = &c;
    }
    if (empty && roomForAnother()) return empty;
    if (old) return old;
    if (empty && (!any || aboveFloor())) return empty;
    return any ? any : &_slots[0];
  }
  bool aboveFloor() const {
#if defined(ESP32)
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= FLOOR + TILE_PX * TILE_PX * 2;
#else
    return true;
#endif
  }
  static const size_t FLOOR = 256 * 1024;
  uint32_t _mark = 0;

  // Another slot's buffer only while PSRAM keeps RESERVE free after it --
  // what a screenshot, the live view's copy of the panel (150 KB at once),
  // the Gallery and LVGL itself need. Measured on the L2 (2026-11-21): all 24
  // slots + the vector data took PSRAM down to ~200 KB. MIN_SLOTS (the screen's
  // tiles and their neighbours) are allocated whatever is left.
  static const size_t RESERVE = 1024 * 1024;
  static const int MIN_SLOTS = 10;
  bool roomForAnother() const {
    int n = 0;
    for (const Slot& c : _slots) if (c.px) n++;
    if (n < MIN_SLOTS) return true;
#if defined(ESP32)
    return heap_caps_get_free_size(MALLOC_CAP_SPIRAM) >= RESERVE + TILE_PX * TILE_PX * 2;
#else
    return true;
#endif
  }
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
#ifdef UI_FLUSH_DMA   // in the display's byte order: drawn as a plain copy (LvglPort.h)
    if (_job.present) lv_draw_rgb565_swap(_spare, TILE_PX * TILE_PX);
#endif
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
