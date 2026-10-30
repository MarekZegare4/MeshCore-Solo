#pragma once
// On-device channel management shared by the frontends: add (Public /
// Hashtag / Private, the phone app's channel types -- docs/companion_protocol.md
// "Channel Types"), edit, delete, and the per-channel prefs the channel
// options show (notifications, favourite, scope). Also the cleanup of every
// channel-keyed pref when a channel goes away (from here or from the app).
//
// Secrets: only a 16-byte (128-bit) secret is produced, matching the
// CMD_GET_CHANNEL note ("only 128-bit supported"). Two ways to give one:
//   - passphrase: any text, SHA-256'd down to 16 bytes -- easy to agree on
//     verbally, like a room password;
//   - hex key: the exact 32 hex chars, e.g. from a channel QR (docs/qr_codes.md).
// A hashtag channel "#topic" uses "#topic" itself as the passphrase.

#include <Utils.h>
#include "Favourites.h"

namespace chanctl {

enum Result : uint8_t { OK, FULL, NO_NAME, NO_SECRET, BAD_SECRET, EXISTS, FAILED };

// A channel's name without the '#' a hashtag channel's starts with -- for a
// label that puts its own '#' in front (as ui-new's tiles do), never "##test".
static inline const char* bareName(const char* name) { return name[0] == '#' ? name + 1 : name; }

static const char* resultText(Result r) {
  switch (r) {
    case OK:         return "Saved";
    case FULL:       return "Channels full";
    case NO_NAME:    return "Name required";
    case NO_SECRET:  return "Secret required";
    case BAD_SECRET: return "Secret must be 32 hex characters";
    case EXISTS:     return "Channel already exists";
    default:         return "Save failed";
  }
}

// First free slot (never configured, or blank name), or -1.
static int freeSlot() {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') return i;
  }
  return -1;
}

static bool exists(int idx) {
  ChannelDetails ch;
  return idx >= 0 && idx < MAX_GROUP_CHANNELS && the_mesh.getChannel(idx, ch) && ch.name[0];
}

// Exactly 32 hex chars -> 16-byte secret (zero-padded to ChannelDetails'
// 32-byte field). Rejects malformed hex and the all-zero key, which
// setChannelLocal() reads as "slot deleted" (isAllZero() in MyMesh.cpp):
// saving one would drop the channel and its prefs on the spot.
static bool hexToSecret(const char* hex32, uint8_t out[32]) {
  if (!hex32 || strlen(hex32) != 32) return false;
  uint8_t tmp[16];
  bool any = false;
  for (int i = 0; i < 16; i++) {
    char byte_str[3] = { hex32[i * 2], hex32[i * 2 + 1], 0 };
    char* end = nullptr;
    long v = strtol(byte_str, &end, 16);
    if (*end != 0) return false;
    tmp[i] = (uint8_t)v;
    if (tmp[i]) any = true;
  }
  if (!any) return false;
  memset(out, 0, 32);
  memcpy(out, tmp, 16);
  return true;
}

static bool phraseToSecret(const char* phrase, uint8_t out[32]) {
  if (!phrase || !phrase[0]) return false;
  uint8_t digest[32];
  mesh::Utils::sha256(digest, sizeof(digest), (const uint8_t*)phrase, (int)strlen(phrase));
  memset(out, 0, 32);
  memcpy(out, digest, 16);
  return true;
}

// Another slot already holds this secret? The secret is the channel's
// identity on the air (the name is only a label), so a second slot with it
// would be the same channel twice, with split history / unread.
static bool secretInUse(const uint8_t secret[32], int except_idx) {
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    if (i == except_idx) continue;
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
    if (memcmp(ch.channel.secret, secret, 16) == 0) return true;
  }
  return false;
}

static Result save(int idx, const char* name, const uint8_t secret[32]) {
  if (idx < 0) return FULL;
  if (!name || !name[0]) return NO_NAME;
  if (secretInUse(secret, idx)) return EXISTS;
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  strncpy(ch.name, name, sizeof(ch.name) - 1);
  memcpy(ch.channel.secret, secret, sizeof(ch.channel.secret));
  return the_mesh.setChannelLocal((uint8_t)idx, ch) ? OK : FAILED;
}

// The well-known public channel (MyMesh.cpp's PUBLIC_GROUP_PSK, as hex).
static Result addPublic() {
  static const char PUBLIC_SECRET_HEX[] = "8b3387e9c5cdea6ac9e5edbaa115cd72";
  uint8_t secret[32];
  hexToSecret(PUBLIC_SECRET_HEX, secret);
  return save(freeSlot(), "Public", secret);
}

// "#topic": name and passphrase both "#topic" (a leading '#' typed is kept once).
// Spaces around it are dropped and the case kept, as the phone app does
// (meshcore-open's derivePskFromHashtag()), so "#Kraków " joins the same channel.
static Result addHashtag(const char* topic) {
  if (!topic) return NO_NAME;
  while (*topic == ' ') topic++;
  if (topic[0] == '#') topic++;
  char name[32];
  snprintf(name, sizeof(name), "#%s", topic);
  size_t n = strlen(name);
  while (n > 1 && name[n - 1] == ' ') name[--n] = '\0';
  if (n < 2) return NO_NAME;
  uint8_t secret[32];
  phraseToSecret(name, secret);
  return save(freeSlot(), name, secret);
}

// Private channel, or an edit of slot `idx` (-1 = add to a free slot).
static Result savePrivate(int idx, const char* name, const char* secret_text, bool hex) {
  if (!name || !name[0]) return NO_NAME;
  uint8_t secret[32];
  if (hex) { if (!hexToSecret(secret_text, secret)) return BAD_SECRET; }
  else if (!phraseToSecret(secret_text, secret)) return NO_SECRET;
  return save(idx < 0 ? freeSlot() : idx, name, secret);
}

// Edit: new name, secret kept.
static Result rename(int idx, const char* name) {
  ChannelDetails ch;
  if (!exists(idx) || !the_mesh.getChannel(idx, ch)) return FAILED;
  if (!name || !name[0]) return NO_NAME;
  return save(idx, name, ch.channel.secret);
}

// The secret as 32 hex chars (to show / share it).
static bool secretHex(int idx, char out[33]) {
  ChannelDetails ch;
  if (!exists(idx) || !the_mesh.getChannel(idx, ch)) return false;
  for (int i = 0; i < 16; i++) snprintf(out + i * 2, 3, "%02x", ch.channel.secret[i]);
  return true;
}

// Delete: an all-zero slot; setChannelLocal() then runs onChannelRemoved().
static bool remove(int idx) {
  if (idx < 0 || idx >= MAX_GROUP_CHANNELS) return false;
  ChannelDetails ch;
  memset(&ch, 0, sizeof(ch));
  return the_mesh.setChannelLocal((uint8_t)idx, ch);
}

// ── Per-channel prefs ───────────────────────────────────────────────────────

// Notifications: 0 = follow the global setting, 1 = muted, 2 = always.
enum Notif : uint8_t { NOTIF_DEFAULT, NOTIF_MUTED, NOTIF_ALWAYS };

static uint8_t notif(const NodePrefs* p, uint8_t idx) {
  uint64_t m = 1ULL << idx;
  if (!p || !(p->ch_notif_override & m)) return NOTIF_DEFAULT;
  return (p->ch_notif_muted & m) ? NOTIF_MUTED : NOTIF_ALWAYS;
}

static void setNotif(NodePrefs* p, uint8_t idx, uint8_t state) {
  if (!p) return;
  uint64_t m = 1ULL << idx;
  if (state == NOTIF_DEFAULT) { p->ch_notif_override &= ~m; p->ch_notif_muted &= ~m; return; }
  p->ch_notif_override |= m;
  if (state == NOTIF_MUTED) p->ch_notif_muted |= m; else p->ch_notif_muted &= ~m;
}

// Melody slot: 0 = the global channel sound, 1 / 2 = the user melodies.
static uint8_t melody(const NodePrefs* p, uint8_t idx) {
  uint64_t m = 1ULL << idx;
  if (!p || !(p->ch_notif_melody_set & m)) return 0;
  return (p->ch_notif_melody_2 & m) ? 2 : 1;
}

static void setMelody(NodePrefs* p, uint8_t idx, uint8_t slot) {
  if (!p) return;
  uint64_t m = 1ULL << idx;
  if (slot == 0) { p->ch_notif_melody_set &= ~m; p->ch_notif_melody_2 &= ~m; return; }
  p->ch_notif_melody_set |= m;
  if (slot == 2) p->ch_notif_melody_2 |= m; else p->ch_notif_melody_2 &= ~m;
}

static bool favourite(const NodePrefs* p, uint8_t idx) { return p && (p->ch_fav_bitmask & (1ULL << idx)); }

// The channels a list shows, in order: out[] gets the slot numbers, the count
// comes back. Favourites first (fav_sort), then by ch_sort -- slot order, the
// channel with the newest message first (channels without one after those,
// in slot order), or by name. Stable, so equal rows keep their slot order.
static int order(const NodePrefs* p, const MessageHistory& hist, uint8_t* out, bool fav_only) {
  int n = 0;
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(i, ch) || ch.name[0] == '\0') continue;
    if (fav_only && !favourite(p, i)) continue;
    out[n++] = (uint8_t)i;
  }
  const bool fav_first = !p || p->fav_sort;
  const uint8_t mode = p && p->ch_sort < NodePrefs::CH_SORT_COUNT ? p->ch_sort : 0;
  if (n < 2 || (!fav_first && mode == 0)) return n;

  uint16_t rank[MAX_GROUP_CHANNELS];   // newest message = 0
  for (int i = 0; i < MAX_GROUP_CHANNELS; i++) rank[i] = 0xFFFF;
  if (mode == 1) {
    int left = n;
    for (int j = 0; j < hist.chHistCount() && left > 0; j++) {
      uint8_t c = hist.chAtPos(hist.chHistPosNewest(j)).ch_idx;
      if (c < MAX_GROUP_CHANNELS && rank[c] == 0xFFFF) { rank[c] = (uint16_t)j; left--; }
    }
  }
  // true when a goes before b
  auto before = [&](uint8_t a, uint8_t b) {
    if (fav_first) {
      bool fa = favourite(p, a), fb = favourite(p, b);
      if (fa != fb) return fa;
    }
    if (mode == 1) return rank[a] < rank[b];
    if (mode == 2) {
      ChannelDetails ca, cb;
      the_mesh.getChannel(a, ca);
      the_mesh.getChannel(b, cb);
      const char* x = bareName(ca.name);
      const char* y = bareName(cb.name);
      for (;; x++, y++) {
        char u = *x >= 'A' && *x <= 'Z' ? *x + 32 : *x;
        char v = *y >= 'A' && *y <= 'Z' ? *y + 32 : *y;
        if (u != v) return (uint8_t)u < (uint8_t)v;
        if (!u) return false;
      }
    }
    return false;
  };
  for (int i = 1; i < n; i++) {   // insertion sort: stable
    uint8_t v = out[i];
    int j = i;
    while (j > 0 && before(v, out[j - 1])) { out[j] = out[j - 1]; j--; }
    out[j] = v;
  }
  return n;
}

static void setFavourite(NodePrefs* p, uint8_t idx, bool on) {
  if (!p) return;
  if (on) p->ch_fav_bitmask |= 1ULL << idx; else p->ch_fav_bitmask &= ~(1ULL << idx);
}

// Whether new messages of this channel go to the flash log (HistoryLog.h).
static bool saved(const NodePrefs* p, uint8_t idx) { return !p || !(p->ch_no_save & (1ULL << idx)); }
static void setSaved(NodePrefs* p, uint8_t idx, bool on) {
  if (!p) return;
  if (on) p->ch_no_save &= ~(1ULL << idx); else p->ch_no_save |= 1ULL << idx;
}

// Scope as a picker row: 0 = Default (follow the list's default), then the
// list itself from 1 ("*" = unscoped, then the named entries).
static uint8_t scope(const NodePrefs* p, uint8_t idx) {
  const uint8_t ci = (p && idx < NodePrefs::MAX_SCOPED_CHANNELS) ? p->ch_scope_idx[idx] : NodePrefs::CH_SCOPE_DEFAULT;
  return ci == NodePrefs::CH_SCOPE_DEFAULT ? 0 : ci + 1;
}
// A picker row back to the ch_scope_idx value (MyMesh::setChannelScope()).
static uint8_t scopeFromRow(int row) {
  return row <= 0 ? NodePrefs::CH_SCOPE_DEFAULT : (uint8_t)(row - 1);
}

// CONTRACT: every NodePrefs field that keys on a channel index is cleared
// here, so a channel re-added at a freed slot can't inherit the old one's
// settings. Adding such a field: clear it below (and mark it in NodePrefs.h).
// Covers bot_channel_idx, loc_share_channel_idx, ch_notif_melody_*,
// ch_notif_override / ch_notif_muted, ch_fav_bitmask, ch_no_save, favourite dial
// slots, ch_scope_idx. True when something changed (caller saves).
static bool onRemoved(NodePrefs* p, uint8_t idx) {
  if (!p) return false;
  bool changed = false;
  if (p->bot_channel_enabled && p->bot_channel_idx == idx) { p->bot_channel_enabled = 0; changed = true; }
  // Fail closed: a share whose channel is gone stops rather than picking another.
  if (p->loc_share_target_type == 0 && p->loc_share_channel_idx == idx) { p->loc_share_enabled = 0; changed = true; }
  uint64_t m = 1ULL << idx;
  if (p->ch_notif_melody_set & m) { p->ch_notif_melody_set &= ~m; p->ch_notif_melody_2 &= ~m; changed = true; }
  if (p->ch_notif_override & m)   { p->ch_notif_override &= ~m; p->ch_notif_muted &= ~m; changed = true; }
  if (p->ch_fav_bitmask & m)      { p->ch_fav_bitmask &= ~m; changed = true; }
  if (p->ch_no_save & m)          { p->ch_no_save &= ~m; changed = true; }
  if (idx < NodePrefs::MAX_SCOPED_CHANNELS && p->ch_scope_idx[idx] != NodePrefs::CH_SCOPE_DEFAULT) {
    p->ch_scope_idx[idx] = NodePrefs::CH_SCOPE_DEFAULT;   // back to Default, as a new channel
    changed = true;
  }
  int slot = favslots::findChannel(p, idx);
  if (slot >= 0) { favslots::clear(p, slot); changed = true; }
  return changed;
}

}  // namespace chanctl
