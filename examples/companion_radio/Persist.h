#pragma once
// Shared on-disk header for the small fixed-table snapshots (waypoints, trail).
// Layout: magic(4) + version(1) + reserved(1) + count(2), little-endian, then
// table-specific payload (records, and for the trail an extra accumulated-ms
// field). Keeps the read/write boilerplate — and its short-read checks — in one
// place so both stores can't drift apart.
#include <Arduino.h>

namespace persist {

// Write magic(4) + version(1) + reserved(1) + count(2). Returns true on a full write.
template <typename F>
inline bool writeHeader(F& file, uint32_t magic, uint8_t version, uint16_t count) {
  uint8_t res = 0;
  return file.write((uint8_t*)&magic,   sizeof(magic)) == sizeof(magic)
      && file.write(&version, 1)                       == 1
      && file.write(&res, 1)                           == 1
      && file.write((uint8_t*)&count,   sizeof(count)) == sizeof(count);
}

// Read + validate the header. Returns true and sets count_out only when magic
// and version match. The caller decides how to treat count_out (clamp or
// reject against its own capacity) and reads any table-specific fields that
// follow the count.
template <typename F>
// `min_version` > 0 also takes the older versions from it up to expect_version,
// the one read going to *version_out.
inline bool readHeader(F& file, uint32_t expect_magic, uint8_t expect_version, uint16_t& count_out,
                       uint8_t min_version = 0, uint8_t* version_out = nullptr) {
  uint32_t magic = 0;
  if (file.read((uint8_t*)&magic, sizeof(magic)) != (int)sizeof(magic)) return false;
  if (magic != expect_magic) return false;
  uint8_t ver = 0, res = 0;
  uint16_t cnt = 0;
  if (file.read(&ver, 1)                       != 1)                return false;
  if (file.read(&res, 1)                       != 1)                return false;
  if (file.read((uint8_t*)&cnt, sizeof(cnt))   != (int)sizeof(cnt)) return false;
  if (ver > expect_version || ver < (min_version ? min_version : expect_version)) return false;
  if (version_out) *version_out = ver;
  count_out = cnt;
  return true;
}

}  // namespace persist
