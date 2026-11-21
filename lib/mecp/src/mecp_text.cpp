// MECP's English texts: the severities and the codes, for showing a message.
// See mecp_text.h.
//
// Generated from mecp-org/MECP languages/en.json (MECP 1.4.1), CC BY 4.0:
// https://github.com/mecp-org/MECP (LICENSE-LANGUAGES). Em dashes made ASCII
// (", "), as the small screens' fonts are.

#include "mecp_text.h"

#include <stdio.h>
#include <string.h>

namespace mecp {

namespace {

struct Entry { char category; uint8_t number; const char* text; };

const Entry kCodes[] = {
  { 'M', 1, "Injury" },
  { 'M', 2, "Unconscious person" },
  { 'M', 3, "Breathing difficulty" },
  { 'M', 4, "Cardiac event" },
  { 'M', 5, "Hypothermia" },
  { 'M', 6, "Severe bleeding" },
  { 'M', 7, "Fracture / immobile" },
  { 'M', 8, "Burns" },
  { 'M', 9, "Multiple casualties" },
  { 'M', 10, "Deceased" },
  { 'M', 11, "Animal bite / sting" },
  { 'M', 12, "Allergic reaction / anaphylaxis" },
  { 'M', 13, "Poisoning / toxic exposure" },
  { 'M', 14, "Persons located alive" },
  { 'M', 15, "Area searched, no victims found" },
  { 'M', 16, "Medical supply drop needed" },
  { 'T', 1, "Road blocked" },
  { 'T', 2, "Bridge out" },
  { 'T', 3, "Building collapsed" },
  { 'T', 4, "Flooding" },
  { 'T', 5, "Landslide" },
  { 'T', 6, "Power out" },
  { 'T', 7, "Fire" },
  { 'T', 8, "Avalanche" },
  { 'T', 9, "Path impassable" },
  { 'T', 10, "Shelter available" },
  { 'T', 11, "Drowning / water rescue needed" },
  { 'T', 12, "Water contamination" },
  { 'T', 13, "Earthquake" },
  { 'T', 14, "Gas leak" },
  { 'T', 15, "Chemical spill / HAZMAT" },
  { 'T', 16, "Vehicle accident" },
  { 'T', 17, "Vehicle fire" },
  { 'W', 1, "Storm approaching" },
  { 'W', 2, "Visibility zero" },
  { 'W', 3, "Extreme cold" },
  { 'W', 4, "Extreme heat" },
  { 'W', 5, "Air quality danger" },
  { 'W', 6, "Tsunami / tidal surge warning" },
  { 'S', 1, "Need water" },
  { 'S', 2, "Need food" },
  { 'S', 3, "Need medication" },
  { 'S', 4, "Need battery / power" },
  { 'S', 5, "Need fuel" },
  { 'S', 6, "Need tools / equipment" },
  { 'P', 1, "Stranded / stuck" },
  { 'P', 2, "Evacuating toward" },
  { 'P', 3, "Sheltering in place" },
  { 'P', 4, "En route to" },
  { 'P', 5, "At GPS coordinates" },
  { 'P', 6, "Lost" },
  { 'P', 7, "Group separated" },
  { 'C', 1, "Send rescue" },
  { 'C', 2, "Need transport" },
  { 'C', 3, "Relay this message" },
  { 'C', 4, "Confirm received" },
  { 'C', 5, "How many people" },
  { 'C', 6, "What is status" },
  { 'C', 7, "Can you reach" },
  { 'C', 8, "Rendezvous at" },
  { 'R', 1, "Acknowledged" },
  { 'R', 2, "Help coming" },
  { 'R', 3, "ETA [time]" },
  { 'R', 4, "Cannot assist" },
  { 'R', 5, "Redirecting to" },
  { 'R', 6, "Stand by" },
  { 'R', 7, "Situation resolved / all clear" },
  { 'D', 1, "This is a drill" },
  { 'D', 2, "This is a test" },
  { 'D', 3, "End of drill" },
  { 'D', 4, "Ignore previous, sent in error" },
  { 'L', 1, "Beer / drinks" },
  { 'L', 2, "Coffee" },
  { 'L', 3, "Food ready" },
  { 'L', 4, "Summit reached" },
  { 'L', 5, "At camp" },
  { 'L', 6, "Running late" },
  { 'L', 7, "Good signal here" },
  { 'L', 8, "Photo opportunity" },
  { 'L', 9, "Wildlife spotted" },
  { 'L', 10, "Beautiful view" },
  { 'L', 11, "Trail conditions good" },
  { 'L', 12, "Trail conditions bad" },
  { 'L', 13, "Need a break" },
  { 'L', 14, "Heading home" },
  { 'L', 15, "Good morning / check-in" },
  { 'L', 16, "Good night" },
  { 'L', 17, "Thank you" },
  { 'L', 18, "Having fun" },
  { 'L', 19, "Festival / event here" },
  { 'L', 20, "Node test / ping" },
  { 'X', 1, "Dangerous person / threat nearby" },
  { 'X', 2, "Area unsafe, avoid" },
  { 'X', 3, "Gunfire / explosions heard" },
  { 'X', 4, "Civil unrest / crowd danger" },
  { 'X', 5, "Theft / looting reported" },
  { 'X', 6, "Authorities / emergency services present" },
  { 'X', 7, "Checkpoint / road closure (authority)" },
  { 'H', 1, "Have water available" },
  { 'H', 2, "Have food available" },
  { 'H', 3, "Have medical supplies" },
  { 'H', 4, "Have power / charging" },
  { 'H', 5, "Have fuel" },
  { 'H', 6, "Have tools / equipment" },
  { 'H', 7, "Have shelter / space for [N]pax" },
  { 'H', 8, "Have transport / vehicle" },
  { 'B', 1, "Automated distress beacon active, sender may be unresponsive" },
  { 'B', 2, "Beacon acknowledged, reduce transmission rate" },
  { 'B', 3, "Cancel beacon, I am OK" },
};

const char* const kSeverities[] = { "MAYDAY", "URGENT", "SAFETY", "ROUTINE" };

}  // namespace

const char* severityText(Severity s) {
  const uint8_t i = static_cast<uint8_t>(s);
  return i < sizeof(kSeverities) / sizeof(kSeverities[0]) ? kSeverities[i] : "";
}

const char* codeText(Code code) {
  for (const Entry& e : kCodes)
    if (e.category == code.category && e.number == code.number) return e.text;
  return nullptr;
}

const char* freeText(const char* s, const Message& m) {
  if (!s || !m.code_count) return s;
  const char* p = s + kHeaderLen + m.code_count * (kCodeLen + 1) - 1;   // past the last code
  while (*p == ' ') p++;
  return p;
}

size_t readable(const char* s, char* out, size_t n) {
  if (!out || n == 0) return 0;
  out[0] = '\0';
  Message m;
  if (!Message::decode(s, m)) return 0;
  size_t o = 0;
  auto put = [&](const char* t) {
    while (*t && o + 1 < n) out[o++] = *t++;
    out[o] = '\0';
  };
  put(severityText(m.severity));
  put(": ");
  for (size_t i = 0; i < m.code_count; i++) {
    if (i) put(", ");
    const char* t = codeText(m.codes[i]);
    char raw[4];
    if (!t) {   // a well-formed code we don't know yet: shown as sent
      snprintf(raw, sizeof(raw), "%c%02u", m.codes[i].category, (unsigned)m.codes[i].number);
      t = raw;
    }
    put(t);
  }
  const char* f = freeText(s, m);
  if (f && *f) { put(" - "); put(f); }
  return o;
}

const char* shown(const char* body) {
  static char buf[2][200];
  static uint8_t k = 0;
  if (!body || strncmp(body, "MECP/", 5) != 0) return body;
  k ^= 1;
  return readable(body, buf[k], sizeof(buf[k])) ? buf[k] : body;
}

}  // namespace mecp
