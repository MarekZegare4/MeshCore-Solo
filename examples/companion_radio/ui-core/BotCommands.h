#pragma once
// The bot's ! commands as a table: which ones a target (channel, room, direct)
// answers. A target keeps a mask of the ones switched off, so 0 is all of them
// and a command added later starts on. No radio and no prefs here, so it's
// tested on the host (test/test_bot_commands). The commands themselves are in
// MyMeshBot.h, the mask's rows in BotConfig.h.

#include <stdint.h>
#include <stdio.h>
#include <string.h>

namespace botcmd {

enum Id : uint8_t { PING, BATT, TEMP, TIME, LOC, HOPS, PATH, SNR, DIST, STATUS, HELP,
                    GPS, BUZZ, ADVERT, GPIO, COUNT };

// name: as !help lists it. hint: a one-line explanation for frontends with room.
struct Cmd { const char* name; const char* label; const char* hint; };

static const Cmd CMDS[COUNT] = {
  { "ping",    "!ping",         "Answers pong" },
  { "batt",    "!batt",         "Battery" },
  { "temp",    "!temp",         "Temperature" },
  { "time",    "!time",         "Clock" },
  { "loc",     "!loc position", "Off: hidden in !status, !dist, !gps fix too" },
  { "hops",    "!hops",         "Hops the command took" },
  { "path",    "!path",         "The route it came by" },
  { "snr",     "!snr",          "How its last hop was heard" },
  { "dist",    "!dist",         "How far the sender is, and which way" },
  { "status",  "!status",       "Battery, position and time" },
  { "help",    "!help",         "The list of commands" },
  { "gps",     "!gps",          "Action: on, off, fix" },
  { "buzz",    "!buzz",         "Action: sounds the buzzer" },
  { "advert",  "!advert",       "Action: sends an advert" },
  { "gpio1-4", "!gpio1-4",      "Action: reads and sets the pins" },
};

static inline uint16_t flag(int id) { return (uint16_t)(1u << id); }

// How many of the table a target's list shows: the actions, last in it, only
// with its Actions on. Their switches keep their state while hidden.
static inline int count(bool actions) { return actions ? (int)COUNT : (int)GPS; }

// What `off` switches off, by the word after the "!" (lowercase). -1: not a
// command of the table.
static inline int find(const char* cmd) {
  if (!strncmp(cmd, "gpio", 4) && cmd[4] >= '1' && cmd[4] <= '4' && !cmd[5]) return GPIO;
  for (int i = 0; i < GPIO; i++) if (!strcmp(cmd, CMDS[i].name)) return i;
  return -1;
}

// Where a message's commands begin, nullptr if it has none: only at its start,
// past a reply's "@[name] ", so text that merely mentions one doesn't run it,
// nor does another bot's !help answer ("cmds: !ping !buzz ...").
static inline const char* start(const char* body) {
  const char* p = body;
  while (*p == ' ') p++;
  if (p[0] == '@' && p[1] == '[') {
    p = strchr(p, ']');
    if (!p) return nullptr;
    p++;
    while (*p == ' ') p++;
  }
  return *p == '!' ? p : nullptr;
}

// The position may leave: !loc is the switch for all of it. Anything else with
// !loc off would give away what the user has just hidden: !dist from any spot
// the sender claims, !gps fix outright.
static inline bool position(uint16_t off) { return !(off & flag(LOC)); }

// Unknown words count as enabled: they're the caller's to reject.
static inline bool enabled(const char* cmd, uint16_t off) {
  int id = find(cmd);
  if (id < 0) return true;
  if (id == DIST && !position(off)) return false;
  return !(off & flag(id));
}

// "cmds: !ping !batt ...": only what would answer. Actions only when allowed.
static inline void help(char* out, int n, uint16_t off, bool actions) {
  int o = snprintf(out, n, "cmds:");
  for (int i = 0; i < COUNT && o < n; i++) {
    if (i == HELP || (i >= GPS && !actions)) continue;
    if ((off & flag(i)) || (i == DIST && !position(off))) continue;
    o += snprintf(out + o, n - o, " !%s", CMDS[i].name);
  }
}

// A row's value: "All", or how many of the listed ones are on.
static inline void shown(uint16_t off, bool actions, char* out, int n) {
  const int total = count(actions);
  int on = 0;
  for (int i = 0; i < total; i++) if (!(off & flag(i))) on++;
  if (on == total) snprintf(out, n, "All");
  else             snprintf(out, n, "%d of %d", on, total);
}

}  // namespace botcmd
