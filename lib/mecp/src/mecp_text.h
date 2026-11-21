// MECP for people: the English texts of the severities and codes, and a
// message as one readable line.
//   "MECP/0/M01 M07 P05 2pax 48.65,20.13"
//   -> "MAYDAY: Injury, Fracture / immobile, At GPS coordinates - 2pax 48.65,20.13"
#pragma once

#include <stddef.h>

#include "mecp.h"

namespace mecp {

// "MAYDAY", "URGENT", "SAFETY", "ROUTINE".
const char* severityText(Severity s);

// "Injury" for M01; nullptr for a code not in the table (still a code: show it as sent).
const char* codeText(Code code);

// The freetext after the codes of `s`, decoded into `m` ("" when there is none).
const char* freeText(const char* s, const Message& m);

// `s` as one line into `out` (cut to fit `n`). Returns its length, 0 when `s`
// isn't an MECP message (`out` is then "").
size_t readable(const char* s, char* out, size_t n);

// For a message bubble: `body` as readable() puts it when it's an MECP
// message, else `body` itself. The line is in one of two buffers used in
// turn, valid until the second call after this one.
const char* shown(const char* body);

}  // namespace mecp
