// MECP (Mesh Emergency Communication Protocol) 1.4.0 decoder: severity and codes only.
//
// Freetext after the codes is neither checked nor parsed.
#pragma once

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

static constexpr size_t kMaxTextLen = 160;                      // MeshCore's MAX_TEXT_LEN
static constexpr size_t kHeaderLen = sizeof("MECP/0/") - 1;
static constexpr size_t kCodeLen = sizeof("B01") - 1;
// Most codes that fit after the header. Each is followed by a space except the last, hence the + 1.
static constexpr size_t kMaxCodes = (kMaxTextLen - kHeaderLen + 1) / (kCodeLen + 1);

struct Message {
  Severity severity = Severity::Mayday;
  Code codes[kMaxCodes];  // only the first code_count are set
  uint8_t code_count = 0;

  // s must be null terminated, and have the channel prefix ("name: ") stripped.
  // Returns true if decoding is successful.
  // The contents of `out` are unspecified if false is returned.
  static bool decode(const char *s, Message &out);

  bool hasCode(Code code) const;
  Beacon beacon() const;
};

}  // namespace mecp
