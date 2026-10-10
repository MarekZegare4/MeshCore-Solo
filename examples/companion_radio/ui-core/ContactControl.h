#pragma once
// Conversation-level helpers shared by the messages screens (ui-new's
// Messages, ui-lvgl's Chats / thread): per-contact notification and melody
// overrides, the favourite flag, the reply prefix, splitting a room post into
// author and text, and naming the hops of a message's path.

namespace contactctl {

// Per-contact overrides live in small {prefix[4], value} tables in NodePrefs:
// value 0 = no override = free slot. Full table: slot 0 is overwritten.
enum : uint8_t { NOTIF_DEFAULT = 0, NOTIF_MUTED = 1, NOTIF_ALWAYS = 2 };

template <class Entry>
inline uint8_t tableGet(const Entry* tbl, int n, const uint8_t* pub_key, uint8_t Entry::* val) {
  for (int i = 0; i < n; i++)
    if (tbl[i].*val && memcmp(tbl[i].prefix, pub_key, 4) == 0) return tbl[i].*val;
  return 0;
}
template <class Entry>
inline void tableSet(Entry* tbl, int n, const uint8_t* pub_key, uint8_t Entry::* val, uint8_t v) {
  for (int i = 0; i < n; i++)
    if (tbl[i].*val && memcmp(tbl[i].prefix, pub_key, 4) == 0) {
      if (v == 0) memset(&tbl[i], 0, sizeof(tbl[i])); else tbl[i].*val = v;
      return;
    }
  if (v == 0) return;
  for (int i = 0; i < n; i++)
    if (tbl[i].*val == 0) { memcpy(tbl[i].prefix, pub_key, 4); tbl[i].*val = v; return; }
  memcpy(tbl[0].prefix, pub_key, 4); tbl[0].*val = v;
}

inline uint8_t notif(const NodePrefs* p, const uint8_t* pub_key) {
  return p ? tableGet(p->dm_notif, NodePrefs::DM_NOTIF_TABLE_MAX, pub_key, &NodePrefs::DmNotifEntry::state) : 0;
}
inline void setNotif(NodePrefs* p, const uint8_t* pub_key, uint8_t state) {
  if (p) tableSet(p->dm_notif, NodePrefs::DM_NOTIF_TABLE_MAX, pub_key, &NodePrefs::DmNotifEntry::state, state);
}
// Melody slot: 0 = default, 1 / 2 = the user melodies.
inline uint8_t melody(const NodePrefs* p, const uint8_t* pub_key) {
  return p ? tableGet(p->dm_melody, NodePrefs::DM_MELODY_TABLE_MAX, pub_key, &NodePrefs::DmMelodyEntry::slot) : 0;
}
inline void setMelody(NodePrefs* p, const uint8_t* pub_key, uint8_t slot) {
  if (p) tableSet(p->dm_melody, NodePrefs::DM_MELODY_TABLE_MAX, pub_key, &NodePrefs::DmMelodyEntry::slot, slot);
}

// ContactInfo::flags bit 0, the same star the companion app sets.
inline bool favourite(const ContactInfo& ci) { return (ci.flags & 0x01) != 0; }
inline bool setFavourite(const uint8_t* pub_key, bool on) { return the_mesh.setContactFavourite(pub_key, on); }

// ContactInfo::flags bits 1-3: what this contact may ask for (TELEM_PERM_*)
// where Settings > Privacy says "Allowed" -- only then do they matter.
inline uint8_t telemetry(const ContactInfo& ci) { return (ci.flags >> 1) & 0x07; }
inline bool setTelemetry(const uint8_t* pub_key, uint8_t perms) { return the_mesh.setContactTelemetry(pub_key, perms); }
inline bool telemetryPerContact(const NodePrefs* p) {
  return p && (p->telemetry_mode_base == TELEM_MODE_ALLOW_FLAGS || p->telemetry_mode_loc == TELEM_MODE_ALLOW_FLAGS
               || p->telemetry_mode_env == TELEM_MODE_ALLOW_FLAGS);
}
// A path to forget: the contact has a learned one.
inline bool hasPath(const ContactInfo& ci) { return ci.out_path_len != OUT_PATH_UNKNOWN; }
inline bool resetPath(const uint8_t* pub_key) { return the_mesh.resetContactPath(pub_key); }
// Its last advert, sent again zero-hop so the nodes in range can add it.
inline bool share(const uint8_t* pub_key) {
  ContactInfo* c = the_mesh.lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  return c && the_mesh.shareContactZeroHop(*c);
}

// "@[nick] " -- how a reply names who it answers (raw UTF-8, over the air as is).
inline void replyPrefix(const char* nick, char* out, size_t n) { snprintf(out, n, "@[%.31s] ", nick); }

// A room post is filed "Author: text" (UiCore::onMessageRecvEx). Author into
// `author` ("" when there is none), returns the text after it.
inline const char* splitRoomPost(const char* text, char* author, size_t n) {
  const char* sep = strstr(text, ": ");
  if (!sep || (size_t)(sep - text) >= n) { if (n) author[0] = '\0'; return text; }
  memcpy(author, text, sep - text);
  author[sep - text] = '\0';
  return sep + 2;
}

// Hops recorded with a history entry: path_len packs hash size (top 2 bits,
// minus 1) and hop count (low 6 bits).
inline uint8_t hopCount(uint8_t path_len_packed) {
  uint8_t hash_size = (path_len_packed >> 6) + 1;
  uint8_t n = path_len_packed & 63;
  uint8_t max_hops = MAX_HIST_PATH_BYTES / hash_size;   // what capturePath() stores at most
  return n > max_hops ? max_hops : n;
}

// Hop i's name: the contact whose key starts with the hash, else "?AABB".
inline void hopName(uint8_t path_len_packed, const uint8_t* path, int i, char* out, size_t n) {
  uint8_t hash_size = (path_len_packed >> 6) + 1;
  const uint8_t* hash = &path[i * hash_size];
  for (int idx = 0; ; idx++) {
    ContactInfo c;
    if (!the_mesh.getContactByIdx(idx, c)) break;
    if (c.id.isHashMatch(hash, hash_size)) { snprintf(out, n, "%s", c.name); return; }
  }
  char hex[9] = {0};
  uint8_t b = hash_size > 4 ? 4 : hash_size;
  for (uint8_t k = 0; k < b; k++) snprintf(hex + k * 2, 3, "%02X", hash[k]);
  snprintf(out, n, "?%s", hex);
}

// A favourites dial slot's name (ui-core/Favourites.h): contact / room or
// channel name; false when empty or the contact / channel is gone.
// contact_out (optional) gets the contact.
inline bool favName(const NodePrefs* p, int slot, char* out, size_t n, ContactInfo* contact_out = nullptr) {
  if (favslots::isEmpty(p, slot)) return false;
  const uint8_t* pfx = p->favourite_contacts[slot];
  if (favslots::kind(p, slot) == NodePrefs::FAV_KIND_CHANNEL) {
    ChannelDetails ch;
    if (!the_mesh.getChannel(pfx[0], ch) || !ch.name[0]) return false;
    snprintf(out, n, "%s", ch.name);
    return true;
  }
  ContactInfo* c = the_mesh.lookupContactByPubKey(pfx, NodePrefs::FAVOURITE_PREFIX_LEN);
  if (!c) return false;
  snprintf(out, n, "%s", c->name);
  if (contact_out) *contact_out = *c;
  return true;
}

}  // namespace contactctl
