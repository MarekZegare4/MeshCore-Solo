#pragma once
// LVGL's allocator on the ESP32-S3 (LV_STDLIB_CUSTOM in lv_conf.h): the heap
// in PSRAM, so widgets / text never compete with BLE and the radio for
// internal RAM, and -- unlike LVGL's own pool -- safe from two tasks at once:
// map tiles decode through LVGL's lodepng on the other core (TileCache).
// (LVGL's TLSF pool in PSRAM for the loop was measured: malloc+free ~3.5 us
// against ~8, yet screens built no faster -- widget cost is PSRAM and cache
// misses, not the allocator -- and it held 2 MB aside.)
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

#if defined(ESP32)
#include <esp_heap_caps.h>

extern "C" {
void lv_mem_init(void) {}
void lv_mem_deinit(void) {}
lv_mem_pool_t lv_mem_add_pool(void* mem, size_t bytes) { (void)mem; (void)bytes; return NULL; }
void lv_mem_remove_pool(lv_mem_pool_t pool) { (void)pool; }
void* lv_malloc_core(size_t size) { return heap_caps_malloc(size, MALLOC_CAP_SPIRAM); }
void* lv_realloc_core(void* p, size_t new_size) { return heap_caps_realloc(p, new_size, MALLOC_CAP_SPIRAM); }
void lv_free_core(void* p) { heap_caps_free(p); }
void lv_mem_monitor_core(lv_mem_monitor_t* mon) {
  multi_heap_info_t i;
  heap_caps_get_info(&i, MALLOC_CAP_SPIRAM);
  mon->total_size = i.total_free_bytes + i.total_allocated_bytes;
  mon->free_size = i.total_free_bytes;
  mon->free_biggest_size = i.largest_free_block;
  mon->free_cnt = i.free_blocks;
  mon->used_cnt = i.allocated_blocks;
  mon->max_used = i.total_free_bytes + i.total_allocated_bytes - i.minimum_free_bytes;
  mon->used_pct = mon->total_size ? (uint8_t)(100 * i.total_allocated_bytes / mon->total_size) : 0;
  mon->frag_pct = i.total_free_bytes ? (uint8_t)(100 - 100 * (uint64_t)i.largest_free_block / i.total_free_bytes) : 0;
}
lv_result_t lv_mem_test_core(void) { return LV_RESULT_OK; }
}
#endif
