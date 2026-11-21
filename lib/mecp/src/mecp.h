// MECP (Mesh Emergency Communication Protocol) 1.4.0 encoder and decoder: severity, codes, and the
// GPS coordinates convention in the freetext.
//
// The freetext's other conventions are neither encoded nor parsed.
#pragma once

#include <math.h>
#include <stddef.h>
#include <stdint.h>

#include "mecp_code.h"

namespace mecp {

enum class Severity : uint8_t {
  Mayday = 0,
  Urgent = 1,
  Safety = 2,
  Routine = 3,
};

// MECP defines only these meanings, not behavior
// MECP ADR 0005
enum class Beacon : uint8_t {
  None,
  Active,
  Ack,
  Cancel,
};

// A position from the freetext's GPS coordinates convention, such as "48.6520,20.1305".
// NaN/NaN when the message has none.
struct Gps {
  double lat = NAN;  // -90 to 90
  double lon = NAN;  // -180 to 180
};

static constexpr size_t kMaxTextLen = 160;                      // MeshCore's MAX_TEXT_LEN
static constexpr size_t kHeaderLen = sizeof("MECP/0/") - 1;
static constexpr size_t kCodeLen = sizeof("B01") - 1;
// Most codes that fit after the header. Each is followed by a space except the last, hence the + 1.
static constexpr size_t kMaxCodes = (kMaxTextLen - kHeaderLen + 1) / (kCodeLen + 1);

struct Message {
  Severity severity = Severity::Mayday;
  Code codes[kMaxCodes];  // only the first code_count are set
  uint8_t code_count = 0;
  Gps gps;

  // s must be null terminated, and have the channel prefix ("name: ") stripped.
  // Returns true if decoding is successful.
  // The contents of `out` are unspecified if false is returned.
  static bool decode(const char *s, Message &out);

  // Returns its length, or 0 if the message is invalid or does not fit
  size_t encode(char *out, size_t n) const;

  bool hasCode(Code code) const;
  // NaN is the only value that is not equal to itself. std::isnan() is not constexpr before C++23.
  constexpr bool hasGPS() const { return gps.lat == gps.lat && gps.lon == gps.lon; }
  Beacon beacon() const;
};

}  // namespace mecp
