#pragma once
// Remote admin of a repeater / room server you have admin rights on -- the
// on-device equivalent of the app's "repeater admin" (docs/cli_commands.md).
// Shared by ui-new's Tools > Admin and ui-lvgl's Admin screen: the frontends
// draw; this owns the login (saved password, silent retry, self-healing on a
// wrong / stale password), the CLI round trips with their timeouts, and the
// field table.
//
// Fields: {label, get_cmd, set_prefix, kind}
//   set_prefix == null -> action: send get_cmd literally ("reboot")
//   get_cmd == null    -> set-only: the frontend asks for text, sendSet()
//   both               -> get, then edit, then set
//   both null          -> "Custom command...": free text, sendRaw()
// Typed kinds (on/off, number, the four views of "radio f,bw,sf,cr") fetch
// the value into `value` / `radio_*`; the frontend edits those, sendValue()
// sends the set. Results arrive as Outcomes, one at a time, via take().

#include <helpers/ClientACL.h>   // PERM_ACL_ADMIN / PERM_ACL_ROLE_MASK

namespace admin {

enum Kind : uint8_t { K_TEXT, K_ONOFF, K_NUMBER, K_RADIO_FREQ, K_RADIO_BW, K_RADIO_SF, K_RADIO_CR };

struct Field {
  const char* label; const char* get_cmd; const char* set_prefix;
  Kind kind;
  float min_val, max_val, step;   // K_NUMBER (K_RADIO_FREQ: range)
  // Explicit ctor: the nRF52 toolchain's C++ standard predates aggregates with
  // default member initializers, and most rows are plain {label, get, set}.
  Field(const char* l, const char* g, const char* s, Kind k = K_TEXT, float mn = 0, float mx = 0, float st = 1)
    : label(l), get_cmd(g), set_prefix(s), kind(k), min_val(mn), max_val(mx), step(st) {}
  bool isCustom() const  { return !get_cmd && !set_prefix; }
  bool isAction() const  { return get_cmd && !set_prefix; }
  bool isSetOnly() const { return !get_cmd && set_prefix; }
  bool isRadio() const   { return kind >= K_RADIO_FREQ; }
};

enum Tab : uint8_t { TAB_SYSTEM, TAB_RADIO, TAB_ROUTING, TAB_ACTIONS, TAB_COUNT };

static const char* const TAB_LABELS[TAB_COUNT] = { "System", "Radio", "Routing", "Actions" };

static const Field SYSTEM_FIELDS[] = {
  { "Name",           "get name",       "set name" },
  { "Owner info",     "get owner.info", "set owner.info" },
  { "Admin password", nullptr,          "password" },
};
// The four radio rows share "get radio" / "set radio": each fetches and
// re-sends the whole f,bw,sf,cr tuple, only its own part edited.
static const Field RADIO_FIELDS[] = {
  { "Frequency (MHz)",  "get radio", "set radio", K_RADIO_FREQ, 150.0f, 2500.0f },
  { "Bandwidth (kHz)",  "get radio", "set radio", K_RADIO_BW },
  { "Spreading factor", "get radio", "set radio", K_RADIO_SF },
  { "Coding rate",      "get radio", "set radio", K_RADIO_CR },
  { "TX power (dBm)",   "get tx",    "set tx",    K_NUMBER, -9, 30, 1 },
};
static const Field ROUTING_FIELDS[] = {
  { "Repeat",                    "get repeat",                "set repeat",                K_ONOFF },
  { "Advert interval (min)",     "get advert.interval",       "set advert.interval",       K_NUMBER, 0, 240, 2 },
  { "Flood advert interval (h)", "get flood.advert.interval", "set flood.advert.interval", K_NUMBER, 0, 168, 1 },
  { "Max hops",                  "get flood.max",             "set flood.max",             K_NUMBER, 0, 64, 1 },
};
static const Field ACTION_FIELDS[] = {
  { "Send advert",          "advert",         nullptr },
  { "Send zero-hop advert", "advert.zerohop", nullptr },
  { "Sync clock",           "clock sync",     nullptr },
  { "Reboot",               "reboot",         nullptr },   // confirmPrompt()
  { "Start OTA",            "start ota",      nullptr },   // confirmPrompt()
  { "Custom command...",    nullptr,          nullptr },
};

static int rowCount(int tab) {
  switch (tab) {
    case TAB_SYSTEM:  return sizeof(SYSTEM_FIELDS) / sizeof(Field);
    case TAB_RADIO:   return sizeof(RADIO_FIELDS) / sizeof(Field);
    case TAB_ROUTING: return sizeof(ROUTING_FIELDS) / sizeof(Field);
    default:          return sizeof(ACTION_FIELDS) / sizeof(Field);
  }
}

static const Field& field(int tab, int row) {
  switch (tab) {
    case TAB_SYSTEM:  return SYSTEM_FIELDS[row];
    case TAB_RADIO:   return RADIO_FIELDS[row];
    case TAB_ROUTING: return ROUTING_FIELDS[row];
    default:          return ACTION_FIELDS[row];
  }
}

// Actions that take an unattended node out of service for a while (OTA into
// DFU mode, reboot offline for seconds): the frontend confirms these first.
static const char* confirmPrompt(const Field& f) {
  if (!f.isAction()) return nullptr;
  if (!strcmp(f.get_cmd, "reboot")) return "Reboot node?";
  if (!strcmp(f.get_cmd, "start ota")) return "Start OTA update?";
  return nullptr;
}

// Completions for the custom command: every remotely usable command from
// docs/cli_commands.md (serial-only ones and the region sub-grammar
// excluded). Entries taking a value end in a space.
static const char* const CLI_COMMANDS[] = {
  "reboot", "poweroff", "shutdown", "clkreboot", "clock", "clock sync",
  "advert", "advert.zerohop", "start ota", "erase",
  "neighbors", "neighbor.remove ", "discover.neighbors",
  "ver", "board",
  "get radio", "set radio ", "get tx", "set tx ", "tempradio ",
  "get freq", "set freq ", "get radio.rxgain", "set radio.rxgain ",
  "get name", "set name ", "get lat", "set lat ", "get lon", "set lon ",
  "get owner.info", "set owner.info ", "get adc.multiplier", "set adc.multiplier ",
  "get public.key", "get role",
  "powersaving", "powersaving on", "powersaving off",
  "get repeat", "set repeat ",
  "get path.hash.mode", "set path.hash.mode ",
  "get loop.detect", "set loop.detect ",
  "get txdelay", "set txdelay ", "get direct.txdelay", "set direct.txdelay ",
  "get rxdelay", "set rxdelay ", "get dutycycle", "set dutycycle ",
  "get af", "set af ", "get int.thresh", "set int.thresh ",
  "get agc.reset.interval", "set agc.reset.interval ",
  "get multi.acks", "set multi.acks ",
  "get flood.advert.interval", "set flood.advert.interval ",
  "get advert.interval", "set advert.interval ",
  "get flood.max", "set flood.max ",
  "get flood.max.unscoped", "set flood.max.unscoped ",
  "get flood.max.advert", "set flood.max.advert ",
  "setperm ", "get acl", "get allow.read.only", "set allow.read.only ",
  "region", "region load", "region save", "region list ",
  "gps", "gps sync", "gps setloc", "gps advert ",
  "sensor list", "sensor get ", "sensor set ",
  "get bridge.type", "get bridge.enabled", "set bridge.enabled ",
  "get bridge.delay", "set bridge.delay ", "get bridge.source", "set bridge.source ",
  "get bridge.baud", "set bridge.baud ", "get bridge.channel", "set bridge.channel ",
  "get bridge.secret", "set bridge.secret ",
  "get pwrmgt.support", "get pwrmgt.source", "get pwrmgt.bootreason", "get pwrmgt.bootmv",
  "password ", "get guest.password", "set guest.password ",
};
static const int CLI_COMMAND_COUNT = sizeof(CLI_COMMANDS) / sizeof(CLI_COMMANDS[0]);

// "freq,bw,sf,cr" (a "get radio" value) -> parts.
static bool parseRadio(const char* val, float& freq, float& bw, uint8_t& sf, uint8_t& cr) {
  char tmp[48];
  strncpy(tmp, val, sizeof(tmp) - 1);
  tmp[sizeof(tmp) - 1] = '\0';
  char* parts[4];
  char* p = tmp;
  for (int i = 0; i < 4; i++) {
    parts[i] = p;
    if (i < 3) {
      char* c = strchr(p, ',');
      if (!c) return false;
      *c = '\0';
      p = c + 1;
    }
  }
  freq = strtof(parts[0], nullptr);
  bw   = strtof(parts[1], nullptr);
  sf   = (uint8_t)atoi(parts[2]);
  cr   = (uint8_t)atoi(parts[3]);
  return true;
}

static void trimRight(char* s) {
  size_t n = strlen(s);
  while (n > 0 && (s[n - 1] == '\n' || s[n - 1] == '\r' || s[n - 1] == ' ')) s[--n] = '\0';
}

}  // namespace admin

class AdminSession {
public:
  enum State : uint8_t {
    IDLE,            // no target
    NEED_PASSWORD,   // no saved password: the frontend asks, then login()
    LOGGING_IN,
    READY,           // logged in as admin, nothing in flight
    WAITING,         // a command is in flight
  };
  enum Outcome : uint8_t {
    NONE,
    LOGGED_IN,
    NOT_ADMIN,       // right password, not an admin: session ends
    LOGIN_FAILED,    // wrong password (forgotten) or not sent: session ends
    LOGIN_TIMEOUT,   // no answer (a stale saved password is silence, not a nack): forgotten, session ends
    SEND_FAILED,
    REPLY,           // reply() holds the node's answer to show
    VALUE_READY,     // a typed field's value is in value / radio_*: edit, then sendValue()
    TEXT_READY,      // text() holds the current value to edit, then sendSet()
    FETCH_FAILED,    // a typed fetch got no / a bad answer
    TIMEOUT,         // a command got no answer (a text fetch still gives TEXT_READY, empty)
  };

  // Open a session with `ci`: straight to READY if it's the node last logged
  // into, else a silent login with its saved password, else NEED_PASSWORD.
  void start(const ContactInfo& ci) {
    end();
    _target = ci;
    _outcome = NONE;
    if (_ok && memcmp(_ok_prefix, ci.id.pub_key, 4) == 0) { _state = READY; return; }
    char pw[sizeof(_pw)];
    if (the_mesh.getRoomPassword(ci.id.pub_key, pw, sizeof(pw))) login(pw);
    else _state = NEED_PASSWORD;
  }

  bool login(const char* password) {
    snprintf(_pw, sizeof(_pw), "%s", password ? password : "");
    uint32_t est = 0;
    if (!the_mesh.sendRoomLogin(_target, _pw, est)) {
      _state = IDLE;
      _outcome = LOGIN_FAILED;
      return false;
    }
    _state = LOGGING_IN;
    _deadline_ms = millis() + est + 4000;
    return true;
  }

  // Leave (the frontend closed the screen): stop tracking a login in flight,
  // so a late answer isn't taken as some other screen's login.
  void end() {
    if (_state == LOGGING_IN) the_mesh.cancelUiPendingLogin(_target.id.pub_key);
    _state = IDLE;
    _purpose = P_PLAIN;
    _field = nullptr;
  }

  // Give up the current wait: a login ends the session, a text fetch still
  // opens the editor (blank), anything else is dropped.
  void cancelWait() {
    if (_state == LOGGING_IN) { end(); return; }
    if (_state != WAITING) return;
    _state = READY;
    if (_purpose == P_TEXT) { _text[0] = '\0'; _outcome = TEXT_READY; }
    _purpose = P_PLAIN;
  }

  // Tap / Enter on a field (confirmPrompt() already answered). Set-only and
  // custom rows need text first: TEXT_READY straight away (text() empty for
  // set-only, the last custom command for custom).
  bool run(const admin::Field& f) {
    if (_state != READY) return false;
    _field = &f;
    if (f.isCustom())  { snprintf(_text, sizeof(_text), "%s", _last_custom); _outcome = TEXT_READY; return true; }
    if (f.isSetOnly()) { _text[0] = '\0'; _outcome = TEXT_READY; return true; }
    if (f.isAction())  return send(f.get_cmd, P_PLAIN);
    return send(f.get_cmd, f.kind == admin::K_TEXT ? P_TEXT : P_VALUE);
  }

  // The edited text for the field run() opened: "<set_prefix> <text>", or the
  // command itself for the custom row.
  bool sendSet(const char* text) {
    if (!_field) return false;
    if (_field->isCustom()) {
      snprintf(_last_custom, sizeof(_last_custom), "%s", text);
      return text[0] ? send(text, P_PLAIN) : false;
    }
    if (!_field->set_prefix) return false;
    char cmd[sizeof(_cmd)];
    // Never a cut command: the editors stop at textRoom(), so this only
    // refuses what got past them.
    if (snprintf(cmd, sizeof(cmd), "%s %s", _field->set_prefix, text) >= (int)sizeof(cmd)) {
      _outcome = SEND_FAILED;
      return false;
    }
    return send(cmd, P_PLAIN);
  }

  // Bytes the text edited for the field run() opened may take: a whole
  // command is one mesh text (MAX_TEXT_LEN), less "<set_prefix> " before it.
  int textRoom() const {
    const int whole = (int)sizeof(_cmd) - 1;
    if (!_field || !_field->set_prefix || _field->isCustom()) return whole;
    return whole - (int)strlen(_field->set_prefix) - 1;
  }

  // The edited typed value (value / radio_*) for the field run() fetched.
  bool sendValue() {
    if (!_field || !_field->set_prefix) return false;
    char cmd[sizeof(_cmd)];
    if (_field->isRadio())
      snprintf(cmd, sizeof(cmd), "%s %.3f,%.3f,%d,%d", _field->set_prefix, radio_freq, radio_bw, (int)radio_sf, (int)radio_cr);
    else if (_field->kind == admin::K_ONOFF)
      snprintf(cmd, sizeof(cmd), "%s %s", _field->set_prefix, value != 0 ? "on" : "off");
    else
      snprintf(cmd, sizeof(cmd), "%s %d", _field->set_prefix, (int)value);
    return send(cmd, P_PLAIN);
  }

  // Frontend step for a K_NUMBER / K_ONOFF value, kept in range.
  void stepValue(int dir) {
    if (!_field) return;
    if (_field->kind == admin::K_ONOFF) { value = value != 0 ? 0 : 1; return; }
    float nv = value + dir * _field->step;
    if (nv >= _field->min_val && nv <= _field->max_val) value = nv;
  }

  // ── From the mesh (via UiCore); true when the answer was this session's ──

  bool onLoginResult(const uint8_t* pub_key, bool success, uint8_t permissions) {
    if (_state != LOGGING_IN || memcmp(_target.id.pub_key, pub_key, 4) != 0) return false;
    if (success && (permissions & PERM_ACL_ROLE_MASK) == PERM_ACL_ADMIN) {
      memcpy(_ok_prefix, pub_key, 4);
      _ok = true;
      the_mesh.saveRoomPassword(pub_key, _pw);
      _state = READY;
      _outcome = LOGGED_IN;
    } else if (success) {
      _state = IDLE;             // the password is right: keep it, retyping won't help
      _outcome = NOT_ADMIN;
    } else {
      the_mesh.forgetRoomPassword(pub_key);   // wrong / stale: ask afresh next time
      _state = IDLE;
      _outcome = LOGIN_FAILED;
    }
    return true;
  }

  bool onReply(const uint8_t* pub_key, const char* text) {
    if (_state != WAITING || memcmp(_target.id.pub_key, pub_key, 4) != 0) return false;
    _state = READY;
    // "get" replies come back as "> value" (CommonCLI::handleGetCmd); the
    // plain reply keeps it (a typed custom "get" echoes the wire reply).
    const char* val = (text[0] == '>' && text[1] == ' ') ? text + 2 : text;
    uint8_t purpose = _purpose;
    _purpose = P_PLAIN;
    if (purpose == P_VALUE) {
      _outcome = takeValue(val) ? VALUE_READY : FETCH_FAILED;
      return true;
    }
    if (purpose == P_TEXT) {
      snprintf(_text, sizeof(_text), "%s", val);
      admin::trimRight(_text);
      _outcome = TEXT_READY;
      return true;
    }
    // "password <new>" echoes "password now: <value>" (CommonCLI): that's the
    // password needed from now on, so keep the saved one in step.
    static const char PW_ECHO[] = "password now: ";
    if (!strncmp(text, PW_ECHO, sizeof(PW_ECHO) - 1)) {
      char pw[32];
      snprintf(pw, sizeof(pw), "%s", text + sizeof(PW_ECHO) - 1);
      admin::trimRight(pw);
      the_mesh.saveRoomPassword(_target.id.pub_key, pw);
    }
    snprintf(_reply, sizeof(_reply), "%s", text);
    _outcome = REPLY;
    return true;
  }

  void loop() {
    if ((_state != LOGGING_IN && _state != WAITING) || (int32_t)(millis() - _deadline_ms) < 0) return;
    if (_state == LOGGING_IN) {
      the_mesh.cancelUiPendingLogin(_target.id.pub_key);   // a late answer mustn't land elsewhere
      // No answer says nothing about the password (out of range, busy mesh):
      // keep a saved one, as RoomSessions does. Only a rejection forgets it.
      _state = IDLE;
      _outcome = LOGIN_TIMEOUT;
      return;
    }
    _state = READY;
    if (_purpose == P_TEXT)       { _text[0] = '\0'; _outcome = TEXT_READY; }   // set it blind
    else if (_purpose == P_VALUE) _outcome = FETCH_FAILED;
    else                          _outcome = TIMEOUT;
    _purpose = P_PLAIN;
  }

  // The next result (NONE when nothing happened since the last call).
  Outcome take() { Outcome o = _outcome; _outcome = NONE; return o; }

  State state() const { return _state; }
  bool active() const { return _state != IDLE; }
  bool fetching() const { return _state == WAITING && _purpose != P_PLAIN; }
  const ContactInfo& target() const { return _target; }
  const admin::Field* currentField() const { return _field; }
  const char* reply() const { return _reply; }
  const char* text() const { return _text; }

  // Typed editor values (see VALUE_READY)
  float   value = 0;
  float   radio_freq = 0, radio_bw = 0;
  uint8_t radio_sf = 0, radio_cr = 0;

private:
  enum Purpose : uint8_t { P_PLAIN, P_VALUE, P_TEXT };

  bool send(const char* cmd, uint8_t purpose) {
    snprintf(_cmd, sizeof(_cmd), "%s", cmd);
    uint32_t est = 0;
    if (!the_mesh.sendAdminCommand(_target, _cmd, est)) { _outcome = SEND_FAILED; return false; }
    _state = WAITING;
    _purpose = purpose;
    _deadline_ms = millis() + est + 4000;   // margin for a slow multi-hop reply
    return true;
  }

  bool takeValue(const char* val) {
    if (!_field) return false;
    switch (_field->kind) {
      case admin::K_ONOFF:
        value = memcmp(val, "on", 2) == 0 ? 1 : 0;
        return true;
      case admin::K_NUMBER:
        // Clamped: a value set some other way may sit outside the row's range,
        // and every step from there would be rejected.
        value = constrain((float)atoi(val), _field->min_val, _field->max_val);
        return true;
      default:
        return _field->isRadio() && admin::parseRadio(val, radio_freq, radio_bw, radio_sf, radio_cr);
    }
  }

  State    _state = IDLE;
  Outcome  _outcome = NONE;
  uint8_t  _purpose = P_PLAIN;
  ContactInfo _target;
  const admin::Field* _field = nullptr;
  char     _pw[16] = "";
  char     _cmd[MAX_TEXT_LEN + 1] = "";   // one mesh text (BaseChatMesh::sendCommandData)
  char     _text[161] = "";
  char     _last_custom[161] = "";
  char     _reply[200] = "";
  uint32_t _deadline_ms = 0;
  uint8_t  _ok_prefix[4] = {0};   // last node logged into as admin
  bool     _ok = false;
};
