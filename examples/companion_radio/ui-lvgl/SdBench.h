#pragma once
// Test builds (SOLO_TEST_HOOKS), Wio Tracker L2: `sdbench`, how fast the SD
// card reads -- the raw card through FatFs' disk layer in transfers of 1 to
// 64 sectors, the same into PSRAM (which the SD host can't DMA to), then the
// current map layer's tiles as the map reads them. Tells the card, the driver
// and the file system apart. Blocks the loop for a few seconds.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp (SettingsPages.h's CLI).

#if defined(SOLO_TEST_HOOKS) && defined(SEEED_WIO_TRACKER_L2)
#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include "diskio.h"

namespace sdbench {

// The FatFs drive the card is on: the one whose size is the card's.
static int cardDrive() {
  const uint64_t want = SD_MMC.cardSize() / 512;
  for (BYTE d = 0; d < FF_VOLUMES; d++) {
    LBA_t n = 0;
    if (ff_disk_ioctl(d, GET_SECTOR_COUNT, &n) == RES_OK && n && (uint64_t)n >= want - want / 64 && (uint64_t)n <= want + want / 64)
      return d;
  }
  return -1;
}

// MB/s reading `total` bytes from sector `start` in transfers of `secs` sectors into `buf`.
static float raw(int drv, uint8_t* buf, uint32_t start, int secs, uint32_t total) {
  uint32_t t = micros(), done = 0;
  for (uint32_t s = start; done < total; s += secs, done += secs * 512)
    if (ff_disk_read(drv, buf, s, secs) != RES_OK) return -1;
  t = micros() - t;
  return t ? done / (float)t : 0;
}

// The first folder of tiles under the map layer's root (deepest zoom first).
static bool tileDir(const char* root, char* out, size_t n) {
  for (int z = 18; z >= 3; z--) {
    char zp[64];
    snprintf(zp, sizeof(zp), "%s/%d", root, z);
    DIR* d = opendir(zp);
    if (!d) continue;
    struct dirent* e;
    while ((e = readdir(d))) {
      if (e->d_name[0] == '.' || e->d_type != DT_DIR) continue;
      snprintf(out, n, "%s/%s", zp, e->d_name);
      closedir(d);
      return true;
    }
    closedir(d);
  }
  return false;
}

static void run(char* reply, int n) {
  if (!lvport::mountStorage()) { snprintf(reply, n, "Error: no card"); return; }
  const int drv = cardDrive();
  uint8_t* dma = (uint8_t*)heap_caps_malloc(32 * 1024, MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL);
  uint8_t* ps = (uint8_t*)heap_caps_malloc(32 * 1024, MALLOC_CAP_SPIRAM);
  if (drv < 0 || !dma || !ps) {
    heap_caps_free(dma); heap_caps_free(ps);
    snprintf(reply, n, "Error: drive %d, buffers %d/%d", drv, dma != nullptr, ps != nullptr);
    return;
  }
  // Raw: 512 KB a size, each from a fresh place on the card (no cache helps).
  const uint32_t TOTAL = 512 * 1024;
  uint32_t at = 1 << 20;   // 512 MB in
  float r1 = raw(drv, dma, at, 1, 128 * 1024); at += 4096;
  float r8 = raw(drv, dma, at, 8, TOTAL); at += 4096;
  float r32 = raw(drv, dma, at, 32, TOTAL); at += 4096;
  float r64 = raw(drv, dma, at, 64, TOTAL); at += 4096;
  float p32 = raw(drv, ps, at, 32, TOTAL);
  // Files: the tiles of one folder, opened and read whole as the map does.
  char dir[80];
  uint32_t files = 0, bytes = 0, t_open = 0, t_read = 0;
  if (tileDir(mapview::LAYERS[mapview::s_layer].root, dir, sizeof(dir))) {
    DIR* d = opendir(dir);
    struct dirent* e;
    while (d && files < 30 && (e = readdir(d))) {
      if (e->d_type != DT_REG) continue;
      char path[160];
      snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
      uint32_t t0 = micros();
      FILE* f = fopen(path, "rb");
      uint32_t t1 = micros();
      t_open += t1 - t0;
      if (!f) continue;
      size_t got, all = 0;
      while ((got = fread(dma, 1, 32 * 1024, f)) > 0) all += got;
      fclose(f);
      t_read += micros() - t1;
      files++; bytes += all;
    }
    if (d) closedir(d);
  }
  // The same files again through read() on the descriptor: no stdio buffer.
  uint32_t files2 = 0, bytes2 = 0, t_read2 = 0, t_open2 = 0;
  {
    DIR* d = files ? opendir(dir) : nullptr;
    struct dirent* e;
    while (d && files2 < 30 && (e = readdir(d))) {
      if (e->d_type != DT_REG) continue;
      char path[160];
      snprintf(path, sizeof(path), "%s/%s", dir, e->d_name);
      uint32_t t0 = micros();
      int fd = open(path, O_RDONLY);
      uint32_t t1 = micros();
      t_open2 += t1 - t0;
      if (fd < 0) continue;
      int got, all = 0;
      while ((got = read(fd, dma, 32 * 1024)) > 0) all += got;
      close(fd);
      t_read2 += micros() - t1;
      files2++; bytes2 += all;
    }
    if (d) closedir(d);
  }
  heap_caps_free(dma); heap_caps_free(ps);
  snprintf(reply, n, "> read(): open %.1f ms, read %.1f ms, %.2f MB/s | raw MB/s 1:%.2f 8:%.2f 32:%.2f 64:%.2f psram32:%.2f | %lu tiles %lu B avg, open %.1f ms, read %.1f ms, %.2f MB/s",
           files2 ? t_open2 / 1000.0f / files2 : 0, files2 ? t_read2 / 1000.0f / files2 : 0, t_read2 ? bytes2 / (float)t_read2 : 0, r1, r8, r32, r64, p32, (unsigned long)files, (unsigned long)(files ? bytes / files : 0),
           files ? t_open / 1000.0f / files : 0, files ? t_read / 1000.0f / files : 0, t_read ? bytes / (float)t_read : 0);
}

}  // namespace sdbench
#endif
