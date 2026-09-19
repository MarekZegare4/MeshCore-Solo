#pragma once
// On-device channel Add/Edit form (Messages > Channels > "+ Add channel" / Edit).
// Owned by MessagesScreen, which delegates render()/handleInput() to it while
// active() — the same relationship WaypointsView has with TrailScreen.
//
// "+ Add channel" starts with a Type picker (Public/Hashtag/Private), matching
// the phone app's channel-type trichotomy (docs/companion_protocol.md's
// "Channel Types") instead of exposing the raw Name+Secret form directly:
//   - Public: one tap re-adds the well-known default channel (same secret
//     MyMesh.cpp pre-configures at first boot) — no fields to fill in.
//   - Hashtag: type just a topic name; the channel name and secret are both
//     derived from it ("#topic", secret = first 16 bytes of sha256("#topic")).
//     A topic-based public group chat, discoverable without knowing the hash
//     convention exists.
//   - Private: today's manual Name + Secret form (see below) — a channel only
//     those you share the secret with can join.
// Editing an existing channel (openEdit()) skips the Type picker and goes
// straight to the Name+Secret form — there's no ambiguity to resolve there.
//
// The Secret field (Private, and internally for Public/Hashtag) toggles
// between two entry modes with LEFT/RIGHT:
//   - Passphrase (default): any text, SHA-256'd down to 16 bytes — the same
//     primitive BaseChatMesh::addChannel()/setChannel() already use to derive
//     a channel's routing hash, just applied here to a typed phrase instead of
//     a raw key. Easiest to agree on verbally, like a room password.
//   - Hex key: the exact 32-hex-char (128-bit) secret, e.g. from a channel QR
//     (see docs/qr_codes.md) — for joining a channel whose precise secret you
//     were given rather than agreeing on a new passphrase.
// Either way only a 16-byte (128-bit) secret is produced, matching the
// existing CMD_GET_CHANNEL comment ("NOTE: only 128-bit supported").

#include <Utils.h>

class ChannelsView {
  UITask* _task;

  enum Mode : uint8_t { OFF, TYPE_PICK, ADD, ADD_HASHTAG, EDIT };
  uint8_t _mode = OFF;
  int     _idx = -1;              // channel slot being added/edited

  int  _type_sel = 0;              // TYPE_PICK cursor: 0=Public 1=Hashtag 2=Private

  char _name[32] = "";
  bool _hex_mode = false;         // false = passphrase, true = 32-hex-char raw key
  char _secret_text[33] = "";     // passphrase or hex string typed so far
  char _topic[31] = "";           // ADD_HASHTAG: topic without the leading '#'

  int  _sel = 0;                  // 0=Name 1=Secret 2=[Save] (ADD_HASHTAG: 0=Topic 1=[Save])
  bool _kb_active = false;
  int  _kb_field = -1;            // 0=Name, 1=Secret, 2=Topic

  KeyboardWidget& kb() { return _task->keyboard(); }

  void openKb(const char* initial, int max) {
    kb().begin(initial, max);
    kb().clearPlaceholders();     // literal field — {loc}/{time} make no sense here
    _kb_active = true;
  }

  // Parses exactly 32 hex chars into a 16-byte secret (zero-padded to 32 for
  // ChannelDetails.secret's full field width). Rejects malformed hex and the
  // all-zero sentinel setChannelLocal() reads as "slot deleted" (see
  // isAllZero() in MyMesh.cpp) -- saving one would pass, then immediately
  // trigger onChannelRemoved() on this very slot, silently discarding the
  // channel and its bot/notif/favourite state. Shared by deriveSecret()'s
  // hex-mode branch and the Public quick-add (a fixed, known-good constant,
  // but parsed through the same path rather than duplicated).
  static bool hexToSecret(const char* hex32, uint8_t out[32]) {
    if (strlen(hex32) != 32) return false;
    uint8_t tmp[16];
    for (int i = 0; i < 16; i++) {
      char byte_str[3] = { hex32[i * 2], hex32[i * 2 + 1], 0 };
      char* end = nullptr;
      long v = strtol(byte_str, &end, 16);
      if (*end != 0) return false;
      tmp[i] = (uint8_t)v;
    }
    bool all_zero = true;
    for (int i = 0; i < 16 && all_zero; i++) if (tmp[i]) all_zero = false;
    if (all_zero) return false;
    memset(out, 0, 32);
    memcpy(out, tmp, 16);
    return true;
  }

  // Turn the typed Secret field into a 16-byte channel secret. Returns false
  // (and leaves out[] untouched) if the field can't be parsed as configured.
  bool deriveSecret(uint8_t out[32]) const {
    if (_hex_mode) return hexToSecret(_secret_text, out);
    if (_secret_text[0] == '\0') return false;   // blank passphrase not allowed
    uint8_t digest[32];
    mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*)_secret_text, (int)strlen(_secret_text));
    memset(out, 0, 32);
    memcpy(out, digest, 16);
    return true;
  }

  // True if a *different* slot already holds this secret. The secret is the
  // channel's identity on the air (the name is only a label), so a second slot
  // with the same key would be the same channel listed twice -- with split
  // history/unread state. Edit skips its own slot (_idx) so re-saving a channel
  // under a new name still works.
  bool secretInUse(const uint8_t secret[32]) const {
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (i == _idx) continue;
      ChannelDetails ch;
      if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
      if (memcmp(ch.channel.secret, secret, 16) == 0) return true;
    }
    return false;
  }

  bool saveChannel(const char* name, const uint8_t secret[32]) {
    ChannelDetails ch;
    memset(&ch, 0, sizeof(ch));
    strncpy(ch.name, name, sizeof(ch.name) - 1);
    memcpy(ch.channel.secret, secret, sizeof(ch.channel.secret));
    return the_mesh.setChannelLocal((uint8_t)_idx, ch);
  }

  // Type picker's Public row: no fields to fill in, so this commits directly
  // instead of dropping into the Name+Secret form -- the well-known channel's
  // name and secret are both fixed. Hex confirmed via
  // `python3 -c "import base64; print(base64.b64decode('izOH6cXN6mrJ5e26oRXNcg==').hex())"`
  // to match MyMesh.cpp's PUBLIC_GROUP_PSK (base64) and
  // docs/companion_protocol.md's documented public channel key (hex).
  void commitPublic() {
    static const char PUBLIC_SECRET_HEX[] = "8b3387e9c5cdea6ac9e5edbaa115cd72";
    uint8_t secret[32];
    hexToSecret(PUBLIC_SECRET_HEX, secret);   // fixed, known-good constant -- can't fail
    if (secretInUse(secret)) { _task->showAlert("Already added", 1200); _mode = OFF; return; }
    if (saveChannel("Public", secret)) _task->showAlert("Channel added", 1000);
    else                               _task->showAlert("Save failed", 1200);
    _mode = OFF;
  }

  void commit() {
    if (_mode == ADD_HASHTAG) {
      if (_topic[0] == '\0') { _task->showAlert("Topic required", 1200); return; }
      // "#topic" as both the channel's display name and the passphrase fed to
      // sha256 -- the exact "Hashtag Channels" convention in
      // docs/companion_protocol.md, just synthesized instead of hand-typed.
      snprintf(_name, sizeof(_name), "#%s", _topic);
      strncpy(_secret_text, _name, sizeof(_secret_text) - 1);
      _secret_text[sizeof(_secret_text) - 1] = '\0';
      _hex_mode = false;
    }
    if (_name[0] == '\0') { _task->showAlert("Name required", 1200); return; }
    uint8_t secret[32];
    if (!deriveSecret(secret)) {
      _task->showAlert(_hex_mode ? "Invalid secret" : "Secret required", 1400);
      return;
    }
    if (secretInUse(secret)) { _task->showAlert("Channel already exists", 1400); return; }
    if (saveChannel(_name, secret)) {
      _task->showAlert((_mode == ADD || _mode == ADD_HASHTAG) ? "Channel added" : "Channel updated", 1000);
      _mode = OFF;
    } else {
      _task->showAlert("Save failed", 1200);
    }
  }

public:
  explicit ChannelsView(UITask* task) : _task(task) {}

  bool active() const { return _mode != OFF || _kb_active; }
  void reset() { _mode = OFF; _kb_active = false; _kb_field = -1; _type_sel = 0; _topic[0] = '\0'; }

  // idx = the slot to write on Save (first free slot for Add, existing slot
  // for Edit). Starts at the Type picker (Public/Hashtag/Private) -- see
  // handleInput()'s TYPE_PICK branch for what each option does next.
  void openAdd(int idx) {
    _mode = TYPE_PICK; _idx = idx; _type_sel = 0;
  }
  // Private option's form: today's manual Name + Secret entry, unchanged.
  void openPrivate() {
    _mode = ADD;
    _name[0] = '\0'; _secret_text[0] = '\0'; _hex_mode = false; _sel = 0;
  }
  void openEdit(int idx, const char* existing_name) {
    _mode = EDIT; _idx = idx;
    strncpy(_name, existing_name, sizeof(_name) - 1); _name[sizeof(_name) - 1] = '\0';
    _secret_text[0] = '\0'; _hex_mode = false; _sel = 0;
  }

  int render(DisplayDriver& display) {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);
    if (_kb_active) return kb().render(display);

    const int top  = display.listStart();
    const int step = display.lineStep();

    if (_mode == TYPE_PICK) {
      display.drawCenteredHeader("ADD CHANNEL");
      static const char* TYPE_LABELS[3] = { "Public", "Hashtag", "Private" };
      for (int i = 0; i < 3; i++) {
        int y = top + i * step;
        display.drawSelectionRow(0, y - 1, display.width(), step - 1, i == _type_sel);
        display.drawTextEllipsized(2, y, display.width() - 4, TYPE_LABELS[i]);
        display.setColor(DisplayDriver::LIGHT);
      }
      return 1000;
    }

    if (_mode == ADD_HASHTAG) {
      display.drawCenteredHeader("ADD CHANNEL");
      int mq_delay = 0;
      for (int i = 0; i < 2; i++) {
        int y = top + i * step;
        bool sel = (i == _sel);
        display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
        char row[40];
        if (i == 0) snprintf(row, sizeof(row), "Topic: %s", _topic[0] ? _topic : "(none)");
        else        snprintf(row, sizeof(row), "[Save]");
        int r = display.drawTextEllipsized(2, y, display.width() - 4, row, sel);
        if (sel && r > 0) mq_delay = r;
        display.setColor(DisplayDriver::LIGHT);
      }
      return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
    }

    display.drawCenteredHeader(_mode == ADD ? "ADD CHANNEL" : "EDIT CHANNEL");
    int mq_delay = 0;
    for (int i = 0; i < 3; i++) {
      int y = top + i * step;
      bool sel = (i == _sel);
      display.drawSelectionRow(0, y - 1, display.width(), step - 1, sel);
      char row[40];
      if (i == 0)      snprintf(row, sizeof(row), "Name: %s", _name[0] ? _name : "(none)");
      else if (i == 1) snprintf(row, sizeof(row), "Secret (%s): %s",
                                _hex_mode ? "hex" : "phrase", _secret_text[0] ? _secret_text : "(none)");
      else             snprintf(row, sizeof(row), "[Save]");
      // Ellipsize rather than print() directly -- a long name/secret must not
      // wrap onto the next row's line (print() wraps by default).
      int r = display.drawTextEllipsized(2, y, display.width() - 4, row, sel);
      if (sel && r > 0) mq_delay = r;
      display.setColor(DisplayDriver::LIGHT);
    }
    return (mq_delay > 0 && mq_delay < 1000) ? mq_delay : 1000;
  }

  bool handleInput(char c) {
    if (_kb_active) {
      auto r = kb().handleInput(c);
      if (r == KeyboardWidget::DONE) {
        if      (_kb_field == 0) { strncpy(_name, kb().buf, sizeof(_name) - 1); _name[sizeof(_name) - 1] = '\0'; }
        else if (_kb_field == 1) { strncpy(_secret_text, kb().buf, sizeof(_secret_text) - 1); _secret_text[sizeof(_secret_text) - 1] = '\0'; }
        else                     { strncpy(_topic, kb().buf, sizeof(_topic) - 1); _topic[sizeof(_topic) - 1] = '\0'; }
        _kb_active = false; _kb_field = -1;
      } else if (r == KeyboardWidget::CANCELLED) {
        _kb_active = false; _kb_field = -1;
      }
      return true;
    }

    if (_mode == TYPE_PICK) {
      if (c == KEY_CANCEL) { _mode = OFF; return true; }
      if (c == KEY_UP)   { _type_sel = (_type_sel > 0) ? _type_sel - 1 : 2; return true; }
      if (c == KEY_DOWN) { _type_sel = (_type_sel < 2) ? _type_sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if      (_type_sel == 0) commitPublic();
        else if (_type_sel == 1) { _mode = ADD_HASHTAG; _sel = 0; _topic[0] = '\0'; }
        else                     openPrivate();
      }
      return true;
    }

    if (_mode == ADD_HASHTAG) {
      if (c == KEY_CANCEL) { _mode = OFF; return true; }
      if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 1; return true; }
      if (c == KEY_DOWN) { _sel = (_sel < 1) ? _sel + 1 : 0; return true; }
      if (c == KEY_ENTER) {
        if (_sel == 0) { _kb_field = 2; openKb(_topic, sizeof(_topic) - 1); }
        else            commit();
      }
      return true;
    }

    if (c == KEY_CANCEL) { _mode = OFF; return true; }
    if (c == KEY_UP)   { _sel = (_sel > 0) ? _sel - 1 : 2; return true; }
    if (c == KEY_DOWN) { _sel = (_sel < 2) ? _sel + 1 : 0; return true; }
    if ((keyIsPrev(c) || keyIsNext(c)) && _sel == 1) {
      _hex_mode = !_hex_mode;
      _secret_text[0] = '\0';   // switching mode invalidates whatever was typed
      return true;
    }
    if (c == KEY_ENTER) {
      if      (_sel == 0) { _kb_field = 0; openKb(_name, sizeof(_name) - 1); }
      else if (_sel == 1) { _kb_field = 1; openKb(_secret_text, _hex_mode ? 32 : (int)sizeof(_secret_text) - 1); }
      else                 commit();
      return true;
    }
    return true;
  }
};
