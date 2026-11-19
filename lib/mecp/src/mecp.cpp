// MECP decoder. See mecp.h.

#include "mecp.h"

#include <string.h>

namespace mecp {

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
  return out.code_count > 0;
}

} // namespace mecp
