// MECP encoder and decoder. See mecp.h.

#include "mecp.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace mecp {

namespace {

// ---------- Freetext boundaries (MECP 1.4.0 Optional Freetext rule 4) ----------

// Code points that are a boundary B, as sorted inclusive ranges.
struct Range {
  uint32_t lo, hi;
};
constexpr Range kBoundary[] = {
  { 0x0009, 0x000A }, { 0x000C, 0x000D }, { 0x0020, 0x0022 }, { 0x0027, 0x0029 },
  { 0x002C, 0x002C }, { 0x002E, 0x002E }, { 0x003A, 0x003B }, { 0x003F, 0x003F },
  { 0x005B, 0x005B }, { 0x005D, 0x005D }, { 0x007B, 0x007B }, { 0x007D, 0x007D },
  { 0x00A0, 0x00A0 }, { 0x00AB, 0x00AB }, { 0x00BB, 0x00BB }, { 0x060C, 0x060C },
  { 0x061B, 0x061B }, { 0x061F, 0x061F }, { 0x06D4, 0x06D4 }, { 0x1680, 0x1680 },
  { 0x2000, 0x200A }, { 0x2013, 0x2014 }, { 0x2018, 0x201D }, { 0x2026, 0x2026 },
  { 0x202F, 0x202F }, { 0x205F, 0x205F }, { 0x3000, 0x3002 }, { 0x300C, 0x3011 },
  { 0xFF01, 0xFF01 }, { 0xFF08, 0xFF09 }, { 0xFF0C, 0xFF0C }, { 0xFF0E, 0xFF0E },
  { 0xFF1A, 0xFF1B }, { 0xFF1F, 0xFF1F },
};

// Decodes the UTF-8 code point at p, which must not be at the NUL. Returns its byte length; an
// invalid or truncated sequence is one byte of U+FFFD. A NUL fails the continuation check, so this
// never reads past the end of the string.
size_t utf8Next(const char *p, uint32_t &cp) {
  const unsigned char *u = reinterpret_cast<const unsigned char *>(p);
  size_t n;
  if (u[0] < 0x80) {
    cp = u[0];
    return 1;
  } else if ((u[0] & 0xE0) == 0xC0) {
    n = 2;
    cp = u[0] & 0x1F;
  } else if ((u[0] & 0xF0) == 0xE0) {
    n = 3;
    cp = u[0] & 0x0F;
  } else if ((u[0] & 0xF8) == 0xF0) {
    n = 4;
    cp = u[0] & 0x07;
  } else {
    cp = 0xFFFD;
    return 1;
  }
  for (size_t i = 1; i < n; i++) {
    if ((u[i] & 0xC0) != 0x80) {
      cp = 0xFFFD;
      return 1;
    }
    cp = (cp << 6) | (u[i] & 0x3F);
  }
  return n;
}

// Byte length of the boundary at p, or 0 if there is none.
size_t boundaryLen(const char *p) {
  if (*p == '\0') return 0;
  uint32_t cp;
  const size_t n = utf8Next(p, cp);
  for (const Range &r : kBoundary) {
    if (cp >= r.lo && cp <= r.hi) return n;
  }
  return 0;
}

// A token may end at p: a boundary or the end of the freetext, (?:B|$).
bool tokenEndsAt(const char *p) { return *p == '\0' || boundaryLen(p) > 0; }

bool isDigit(char c) { return c >= '0' && c <= '9'; }

const char *skipDigits(const char *p) {
  while (isDigit(*p)) p++;
  return p;
}

// ---------- GPS coordinates convention ----------

// [+-]?[0-9]+\.[0-9]+
// Returns its end, or nullptr.
const char *matchDecimal(const char *p) {
  if (*p == '+' || *p == '-') p++;
  const char *dot = skipDigits(p);
  if (dot == p || *dot != '.') return nullptr;
  const char *end = skipDigits(dot + 1);
  return end == dot + 1 ? nullptr : end;
}

// ([+-]?[0-9]+\.[0-9]+), ?([+-]?[0-9]+\.[0-9]+)(?:B|$) at p. Returns the end of the token, or nullptr.
// strtod() reads each group exactly: what follows one is a ',' or a boundary, never part of a number.
const char *matchGps(const char *p, Gps &out) {
  const char *lat_end = matchDecimal(p);
  if (!lat_end || *lat_end != ',') return nullptr;
  const char *lon = lat_end + 1;
  if (*lon == ' ') lon++;
  const char *lon_end = matchDecimal(lon);
  if (!lon_end || !tokenEndsAt(lon_end)) return nullptr;
  out.lat = strtod(p, nullptr);
  out.lon = strtod(lon, nullptr);
  return lon_end;
}

bool inRange(const Gps &gps) {
  return gps.lat >= -90 && gps.lat <= 90 && gps.lon >= -180 && gps.lon <= 180;
}

// The first match by position that passes Additional Validation (rules 5 and 6). After a match fails
// validation, the search resumes at its end, so the boundary after it can start the next match.
// Leaves `out` alone if nothing passes.
void findGps(const char *freetext, Gps &out) {
  Gps gps;
  const char *p = freetext;
  while (*p) {
    const char *end = p == freetext ? matchGps(p, gps) : nullptr;  // (?:^|B)
    const size_t boundary = boundaryLen(p);
    if (!end && boundary) end = matchGps(p + boundary, gps);
    if (end) {
      if (inRange(gps)) {
        out = gps;
        return;
      }
      p = end;
      continue;
    }
    uint32_t cp;
    p += utf8Next(p, cp);
  }
}

}  // namespace

bool Message::hasCode(Code code) const {
  for (size_t i = 0; i < code_count; i++) {
    if (codes[i] == code) return true;
  }
  return false;
}

Beacon Message::beacon() const {
  if (hasCode({ 'B', 1 })) return Beacon::Active;
  if (hasCode({ 'B', 2 })) return Beacon::Ack;
  if (hasCode({ 'B', 3 })) return Beacon::Cancel;
  return Beacon::None;
}

bool Message::decode(const char *s, Message &out) {
    if (!s) return false;

  const size_t len = strlen(s);
  if (len < kHeaderLen || strncmp(s, "MECP/", 5) != 0 || s[6] != '/')
    return false;

  out = Message();
  out.severity = static_cast<Severity>(s[5] - '0');
  if (out.severity > Severity::Routine)
    return false;

  for (size_t i = 0; i < kMaxCodes; i++) {
    const size_t code_start = kHeaderLen + i * (kCodeLen + 1);
    if (code_start + kCodeLen > len) break;

    out.codes[i] = Code(&s[code_start]);
    if (out.codes[i].category == '\0')
        break;
    out.code_count++;
  }
  if (out.code_count == 0) return false;

  // The freetext starts after the space that follows the last code.
  const size_t freetext = kHeaderLen + out.code_count * (kCodeLen + 1);
  if (freetext <= len) findGps(&s[freetext], out.gps);
  return true;
}

size_t Message::encode(char *out, size_t n) const {
  if (n == 0) return 0;
  out[0] = '\0';
  if (severity > Severity::Routine || code_count == 0 || code_count > kMaxCodes) return 0;
  if (hasGPS() && !inRange(gps)) return 0;
  for (size_t i = 0; i < code_count; i++) {
    if (codes[i].category < 'A' || codes[i].category > 'Z' || codes[i].number > 99) return 0;
  }

  // snprintf() returns the length it needs, so `len` keeps counting past `n` and the check below
  // catches it. None of these formats can fail, so the return is never negative.
  size_t len = 0;
  auto at = [&] { return len < n ? out + len : nullptr; };
  auto room = [&] { return len < n ? n - len : 0; };
  len += snprintf(at(), room(), "MECP/%d/", static_cast<int>(severity));
  for (size_t i = 0; i < code_count; i++) {
    len += snprintf(at(), room(), i ? " %c%02u" : "%c%02u", codes[i].category,
                    static_cast<unsigned>(codes[i].number));
  }
  if (hasGPS()) len += snprintf(at(), room(), " %.5f,%.5f", gps.lat, gps.lon);

  if (len >= n) {
    out[0] = '\0';
    return 0;
  }
  return len;
}

} // namespace mecp
