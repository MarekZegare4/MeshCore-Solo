#pragma once
// Message history store for MessagesScreen: two RAM ring buffers (channel + DM)
// with their per-entry delivery state (channel "relayed into mesh" echo, DM
// end-to-end ACK + auto-resend) and the per-channel unread counters. Pure
// storage + queries — no UI/phase state lives here. The screen keeps selection,
// scroll, the unread "viewing session" bookkeeping, the room-login table, and
// all rendering, and reaches entries through the accessors below.
//
// Single-TU fragment: included by UITask.cpp before MessagesScreen.h. AckState,
// MSG_TEXT_BUF and the two entry structs are file-scope (not nested) so the
// phase machine in MessagesScreen keeps referring to them unqualified.

// Outgoing-message delivery state. DM: a real end-to-end ACK (✓ delivered to
// the recipient). Channel: only a "relayed into mesh" echo from a repeater (no
// recipient ACK exists for floods), so a missing echo is NOT shown as failure.
enum AckState : uint8_t { ACK_NONE = 0, ACK_PENDING, ACK_OK, ACK_FAIL };

// History text holds a full received message. Channel messages carry the sender
// embedded as "Name: body" in the payload, so a message can be up to the
// over-the-air maximum (MAX_TEXT_LEN). Size the buffers to that + NUL, otherwise
// long messages (and Polish text, where each accented char is two UTF-8 bytes)
// get their tail clipped.
static const int MSG_TEXT_BUF = MAX_TEXT_LEN + 1;

// Cap on the per-entry path/relay-hash buffer below. Real mesh hop depths are
// small in practice (a handful at most), so this comfortably covers any
// realistic path at any path_hash_mode (16 hops at 1-byte hashes, down to 4 at
// 4-byte) without the RAM cost of sizing every ring entry to MAX_PATH_SIZE (64).
// A path deeper than this is silently truncated to the first entries that fit
// (capturePath() below keeps the stored hop count consistent with what's
// actually copied, so a display walk over the buffer can never run past it).
static const uint8_t MAX_HIST_PATH_BYTES = 16;

// Copies up to MAX_HIST_PATH_BYTES of a mesh::Packet-style path into dest_path,
// capping the hop count encoded in dest_len so the two always agree. src_len_packed
// uses the same (hash_size-1)<<6|hash_count packing as mesh::Packet::path_len.
inline void capturePath(uint8_t& dest_len, uint8_t* dest_path, const uint8_t* src_path, uint8_t src_len_packed) {
  uint8_t hash_size = (src_len_packed >> 6) + 1;
  uint8_t hash_count = src_len_packed & 63;
  uint8_t max_hops = MAX_HIST_PATH_BYTES / hash_size;
  if (hash_count > max_hops) hash_count = max_hops;
  memcpy(dest_path, src_path, (size_t)hash_count * hash_size);
  dest_len = ((hash_size - 1) << 6) | hash_count;
}

struct ChHistEntry {
  uint8_t  ch_idx;
  char     text[MSG_TEXT_BUF];
  uint32_t timestamp;
  uint8_t  relay_status;   // AckState; only PENDING/OK used (no failure for floods)
  uint32_t relay_seq;      // MyMesh relay seq to match against onChannelRelayed()
  // Incoming: the hop path this post actually took to reach us (resolved to
  // repeater names by the UI). Outgoing: repurposed to hold the distinct
  // repeater hashes that have echoed this send back (see markChannelRelayed) --
  // a message is never both directions at once, so one buffer serves either.
  // path_len packs (hash_size-1)<<6|hop_count, same as mesh::Packet::path_len.
  uint8_t  path_len;
  uint8_t  path[MAX_HIST_PATH_BYTES];
};

struct DmHistEntry {
  uint8_t  prefix[4];
  uint8_t  outgoing;
  char     text[MSG_TEXT_BUF];
  uint32_t timestamp;
  uint8_t  ack_status;       // AckState; meaningful only when outgoing
  uint32_t ack_tag;          // expected_ack CRC to match against onMsgAck()
  uint32_t ack_deadline_ms;  // millis() by which a pending ACK must arrive
  // Sender-perspective message timestamp: the send timestamp for outgoing
  // (reused verbatim on resend so the recipient treats it as a retry), or the
  // sender_timestamp for incoming (used to dedup retried copies). 0 = unknown.
  uint32_t msg_ts;
  uint8_t  attempt;          // last attempt number sent (outgoing); next resend = attempt+1
  uint8_t  resends_left;     // remaining auto-resends before the marker shows ✗
  // Incoming only -- the hop path this DM took to reach us. A DM's outgoing
  // confirmation is the real end-to-end ack_status/ack_tag above (not a
  // repeater-echo concept), so this stays unset (0) for outgoing entries.
  uint8_t  path_len;
  uint8_t  path[MAX_HIST_PATH_BYTES];
};

class MessageHistory {
public:
  // Shared ring for all channels combined; each slot carries a full MSG_TEXT_BUF,
  // so this ring dominates RAM — kept modest to leave heap headroom (see
  // DM_HIST_MAX). The DM ring is likewise bounded.
  static const int CH_HIST_MAX = 48;
  static const int DM_HIST_MAX = 32;

  MessageHistory()
    : _hist_head(0), _hist_count(0), _dm_hist_head(0), _dm_hist_count(0) {
    memset(_ch_unread, 0, sizeof(_ch_unread));
  }

  // ── Channel ring ──────────────────────────────────────────────────────────

  // Append a channel message. `viewing` = the user is currently in this
  // channel's history (so it isn't counted unread). `timestamp` is the sender's
  // own send time (0 = unknown — use receipt time). Returns the ring position
  // (an opaque handle for armChannelRelay / chAtPos), or -1 if rejected.
  // path/path_len_packed: the hop path this incoming post actually took (from
  // the received mesh::Packet), or nullptr/0 when not known (e.g. this is our
  // own outgoing post, before any relay echo has arrived).
  // own_message: this is our own post (e.g. mirrored from an app-originated
  // send) -- never counted unread, regardless of `viewing`. An on-device
  // compose already forces `viewing` true itself (it's necessarily looking at
  // the channel it just sent to), so this only matters for a send the device's
  // own UI wasn't necessarily showing at the time -- otherwise our own sent
  // message could bump the very badge it's supposed to leave alone.
  int addChannelMsg(uint8_t ch_idx, const char* text, bool viewing, uint32_t timestamp = 0,
                     const uint8_t* path = nullptr, uint8_t path_len_packed = 0,
                     bool own_message = false) {
    // Guard against bogus channel indices (e.g. findChannelIdx() returned -1
    // and was cast to uint8_t → 255). Storing such an entry would burn a ring
    // slot for a message that no visible channel can ever surface.
    if (ch_idx >= MAX_GROUP_CHANNELS) return -1;
    int pos;
    if (_hist_count < CH_HIST_MAX) {
      pos = (_hist_head + _hist_count) % CH_HIST_MAX;
      _hist_count++;
    } else {
      pos = _hist_head;
      // Evicting the oldest entry — drop its share of the unread counter so
      // the badge can't claim a message the ring no longer holds. The counter
      // refers to the channel's NEWEST unread messages, so losing the oldest
      // entry only costs an unread one when everything the ring still holds
      // for that channel is unread; otherwise the evicted entry was already
      // read and decrementing would undercount the newer unread ones.
      uint8_t evicted = _hist[pos].ch_idx;
      if (evicted < MAX_GROUP_CHANNELS && _ch_unread[evicted] > 0 &&
          _ch_unread[evicted] >= histCountForChannel(evicted)) {
        _ch_unread[evicted]--;
      }
      _hist_head = (_hist_head + 1) % CH_HIST_MAX;
    }
    _hist[pos].ch_idx = ch_idx;
    _hist[pos].timestamp = displayTimestamp(timestamp, own_message);
    strncpy(_hist[pos].text, text, sizeof(_hist[pos].text) - 1);
    _hist[pos].text[sizeof(_hist[pos].text) - 1] = '\0';
    _hist[pos].relay_status = ACK_NONE;
    _hist[pos].relay_seq = 0;
    if (path && path_len_packed) capturePath(_hist[pos].path_len, _hist[pos].path, path, path_len_packed);
    else _hist[pos].path_len = 0;

    if (!viewing && !own_message && _ch_unread[ch_idx] < 99) _ch_unread[ch_idx]++;
    return pos;
  }

  // count history entries for a specific channel
  int histCountForChannel(int ch_idx) const {
    int n = 0;
    for (int i = 0; i < _hist_count; i++) {
      if (_hist[(_hist_head + i) % CH_HIST_MAX].ch_idx == (uint8_t)ch_idx) n++;
    }
    return n;
  }

  // get ring-buffer position of j-th history entry for channel (newest first)
  int histEntryForChannel(int ch_idx, int j) const {
    int n = 0;
    for (int i = _hist_count - 1; i >= 0; i--) {
      int pos = (_hist_head + i) % CH_HIST_MAX;
      if (_hist[pos].ch_idx == (uint8_t)ch_idx) {
        if (n == j) return pos;
        n++;
      }
    }
    return -1;
  }

  // Called when a repeater echo of one of our channel sends is heard. May fire
  // more than once per send -- every repeater within earshot that independently
  // rebroadcasts triggers its own call with the same seq -- so this matches on
  // relay_seq alone (not "still ACK_PENDING") and keeps appending. repeater_hash
  // (when given) is that repeater's path hash, appended de-duplicated so
  // "Hold Enter" can list every distinct repeater that confirmed the send.
  void markChannelRelayed(uint32_t seq, const uint8_t* repeater_hash = nullptr, uint8_t hash_size = 0) {
    if (seq == 0) return;
    for (int i = 0; i < _hist_count; i++) {
      ChHistEntry& e = _hist[(_hist_head + i) % CH_HIST_MAX];
      if (e.relay_seq != seq) continue;
      e.relay_status = ACK_OK;
      if (repeater_hash && hash_size) {
        uint8_t cur_size  = (e.path_len >> 6) + 1;
        uint8_t cur_count = e.path_len & 63;
        if (cur_count == 0) cur_size = hash_size;  // first hash recorded sets the size for this entry
        if (cur_size == hash_size) {               // ignore a mismatch rather than corrupt the buffer
          bool dup = false;
          for (uint8_t h = 0; h < cur_count; h++) {
            if (memcmp(&e.path[h * hash_size], repeater_hash, hash_size) == 0) { dup = true; break; }
          }
          uint8_t max_hops = MAX_HIST_PATH_BYTES / hash_size;
          if (!dup && cur_count < max_hops) {
            memcpy(&e.path[cur_count * hash_size], repeater_hash, hash_size);
            cur_count++;
            e.path_len = ((hash_size - 1) << 6) | cur_count;
          }
        }
      }
      return;
    }
  }

  // Arm the "relayed into mesh" marker on a just-sent entry (pos from
  // addChannelMsg) — MyMesh tracked the flood it originated and will report a
  // heard repeater echo by seq. Clears path_len: this entry's path buffer now
  // holds echoing-repeater hashes (see markChannelRelayed), not a received hop
  // path, so any stale value from a reused ring slot must not linger.
  void armChannelRelay(int pos, uint32_t seq) {
    if (pos < 0 || pos >= CH_HIST_MAX) return;
    _hist[pos].relay_status = ACK_PENDING;
    _hist[pos].relay_seq    = seq;
    _hist[pos].path_len     = 0;
  }

  ChHistEntry&       chAtPos(int pos)       { return _hist[pos]; }
  const ChHistEntry& chAtPos(int pos) const { return _hist[pos]; }

  // ── Per-channel unread counters ─────────────────────────────────────────────
  // Always clamped to what the ring still holds for that channel. The raw
  // counter and the ring can drift apart — MessagesScreen's viewing-session
  // bookkeeping re-applies the unread snapshot taken when the channel was
  // opened, so entries evicted mid-session would otherwise leave the badge
  // promising messages the history list can no longer show (badge says 7,
  // opening the channel shows an empty list). Capping on read keeps the badge
  // honest whatever the raw counter says.
  uint8_t chUnread(int ch) const {
    if (ch < 0 || ch >= MAX_GROUP_CHANNELS) return 0;
    int held = histCountForChannel(ch);
    return _ch_unread[ch] < held ? _ch_unread[ch] : (uint8_t)held;
  }
  void setChUnread(int ch, uint8_t v) {
    if (ch >= 0 && ch < MAX_GROUP_CHANNELS) _ch_unread[ch] = v;
  }
  void clearAllChannelUnread() { memset(_ch_unread, 0, sizeof(_ch_unread)); }
  int  getTotalChannelUnread() const {
    // Same clamp as chUnread(), but counting ring occupancy for every channel
    // in one pass instead of re-walking the ring once per channel.
    uint8_t held[MAX_GROUP_CHANNELS];
    memset(held, 0, sizeof(held));
    for (int i = 0; i < _hist_count; i++) {
      uint8_t ch = _hist[(_hist_head + i) % CH_HIST_MAX].ch_idx;
      if (ch < MAX_GROUP_CHANNELS && held[ch] < 255) held[ch]++;
    }
    int total = 0;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++)
      total += (_ch_unread[i] < held[i]) ? _ch_unread[i] : held[i];
    return total;
  }

  // ── DM ring ─────────────────────────────────────────────────────────────────

  // ack_tag != 0 marks an outgoing DM as awaiting an end-to-end ACK by
  // ack_deadline_ms; 0 means "sent, no confirmation possible" (no path / incoming).
  // msg_ts = sender-perspective timestamp (send ts for outgoing / sender_timestamp
  // for incoming); resends = remaining auto-resends for an outgoing pending DM.
  // path/path_len_packed: the hop path this incoming DM actually took, or
  // nullptr/0 for outgoing (no path concept there -- see DmHistEntry).
  // The timestamp an entry's age is measured from, on THIS device's clock.
  // An outgoing message was sent just now, so its own send time is simply
  // "now" -- taking the phone app's timestamp instead (CMD_SEND_*) put it on
  // the phone's clock, and a phone running ahead of the device (or on local
  // time) left the age reading "0s" until the device caught up. A sender
  // timestamp that is unknown or still in the future is likewise clamped to
  // receipt time, so it ages normally from here on instead of sticking at 0s.
  static uint32_t displayTimestamp(uint32_t ts, bool outgoing) {
    uint32_t now = rtc_clock.getCurrentTime();
    return (outgoing || ts == 0 || ts > now) ? now : ts;
  }

  void storeDMMsg(const uint8_t* pub_key, bool outgoing, const char* text,
                  uint32_t ack_tag = 0, uint32_t ack_deadline_ms = 0,
                  uint32_t msg_ts = 0, uint8_t resends = 0,
                  const uint8_t* path = nullptr, uint8_t path_len_packed = 0) {
    int pos;
    if (_dm_hist_count < DM_HIST_MAX) {
      pos = (_dm_hist_head + _dm_hist_count) % DM_HIST_MAX;
      _dm_hist_count++;
    } else {
      pos = _dm_hist_head;
      _dm_hist_head = (_dm_hist_head + 1) % DM_HIST_MAX;
    }
    memcpy(_dm_hist[pos].prefix, pub_key, 4);
    _dm_hist[pos].outgoing = outgoing ? 1 : 0;
    // Prefer the sender's own timestamp — a room-sync replay or an
    // offline-queued message held by a repeater can arrive long after it was
    // actually sent, so "now" would mislabel every backlog message as fresh.
    // Fall back to receipt time when the sender's timestamp is unknown or
    // ahead of our clock, and always use our own clock for outgoing.
    _dm_hist[pos].timestamp = displayTimestamp(msg_ts, outgoing);
    strncpy(_dm_hist[pos].text, text, sizeof(DmHistEntry::text) - 1);
    _dm_hist[pos].text[sizeof(DmHistEntry::text) - 1] = '\0';
    _dm_hist[pos].ack_status      = (outgoing && ack_tag) ? ACK_PENDING : ACK_NONE;
    _dm_hist[pos].ack_tag         = ack_tag;
    _dm_hist[pos].ack_deadline_ms = ack_deadline_ms;
    _dm_hist[pos].msg_ts          = msg_ts;
    _dm_hist[pos].attempt         = 0;
    _dm_hist[pos].resends_left    = (outgoing && ack_tag) ? resends : 0;
    if (path && path_len_packed) capturePath(_dm_hist[pos].path_len, _dm_hist[pos].path, path, path_len_packed);
    else _dm_hist[pos].path_len = 0;
  }

  // ack_tag/ack_deadline_ms/resends let an outgoing DM (e.g. one the phone app
  // just sent via CMD_SEND_TXT_MSG) carry the same pending-ACK tracking a
  // message composed on-device gets from storeDMMsg() directly — otherwise it
  // shows with no delivery status at all. Unused (0) for incoming.
  void addDMMsg(const uint8_t* pub_key, bool outgoing, const char* text,
                uint32_t sender_timestamp = 0, uint32_t ack_tag = 0,
                uint32_t ack_deadline_ms = 0, uint8_t resends = 0,
                const uint8_t* path = nullptr, uint8_t path_len_packed = 0) {
    // Drop retried copies of an incoming DM: a resend reuses the sender's
    // timestamp and text but carries a fresh packet hash, so the mesh dup-filter
    // lets it through. Match on prefix + sender_timestamp + text to suppress it.
    if (!outgoing && sender_timestamp != 0) {
      for (int i = 0; i < _dm_hist_count; i++) {
        const DmHistEntry& e = _dm_hist[(_dm_hist_head + i) % DM_HIST_MAX];
        if (!e.outgoing && e.msg_ts == sender_timestamp &&
            memcmp(e.prefix, pub_key, 4) == 0 && strcmp(e.text, text) == 0)
          return;  // duplicate retry — already in history
      }
    }
    storeDMMsg(pub_key, outgoing, text, ack_tag, ack_deadline_ms, sender_timestamp, resends, path, path_len_packed);
  }

  int dmHistCountForContact(const uint8_t* prefix) const {
    int n = 0;
    for (int i = 0; i < _dm_hist_count; i++)
      if (memcmp(_dm_hist[(_dm_hist_head + i) % DM_HIST_MAX].prefix, prefix, 4) == 0) n++;
    return n;
  }

  int dmHistEntryForContact(const uint8_t* prefix, int j) const { // j=0 = newest
    int n = 0;
    for (int i = _dm_hist_count - 1; i >= 0; i--) {
      int pos = (_dm_hist_head + i) % DM_HIST_MAX;
      if (memcmp(_dm_hist[pos].prefix, prefix, 4) == 0) {
        if (n == j) return pos;
        n++;
      }
    }
    return -1;
  }

  // Effective status for display. A pending ACK only reads as failed once its
  // deadline has passed AND no auto-resends remain — while resends_left > 0 the
  // entry stays pending (tickDmResends() retries / finalises it). Safety net for
  // when the tick hasn't run yet; the tick is the authority that writes ACK_FAIL.
  AckState dmEffectiveStatus(const DmHistEntry& e) const {
    if (e.ack_status == ACK_PENDING && e.resends_left == 0 &&
        (int32_t)(millis() - e.ack_deadline_ms) >= 0)
      return ACK_FAIL;
    return (AckState)e.ack_status;
  }

  // Called when an end-to-end ACK arrives (routed from MyMesh::onAckRecv).
  // Marks the matching pending outgoing DM as delivered.
  void markDmDelivered(uint32_t ack_crc) {
    if (ack_crc == 0) return;
    for (int i = 0; i < _dm_hist_count; i++) {
      DmHistEntry& e = _dm_hist[(_dm_hist_head + i) % DM_HIST_MAX];
      if (e.outgoing && e.ack_status == ACK_PENDING && e.ack_tag == ack_crc) {
        e.ack_status = ACK_OK;
        return;
      }
    }
  }

  // Periodic resend driver for outgoing DMs whose ACK deadline lapsed with no
  // ACK: resend with the next attempt# (reusing the original timestamp so the
  // recipient dedups) while resends remain, else mark it failed (✗).
  void tickDmResends() {
    uint32_t now = millis();
    for (int i = 0; i < _dm_hist_count; i++) {
      DmHistEntry& e = _dm_hist[(_dm_hist_head + i) % DM_HIST_MAX];
      if (!e.outgoing || e.ack_status != ACK_PENDING) continue;
      if ((int32_t)(now - e.ack_deadline_ms) < 0) continue;   // still waiting
      if (e.resends_left == 0) { e.ack_status = ACK_FAIL; continue; }
      ContactInfo c;
      if (!contactByPrefix(e.prefix, c)) { e.ack_status = ACK_FAIL; continue; }
      uint32_t expected_ack = 0, est_timeout = 0;
      uint8_t next_attempt = e.attempt + 1;
      if (the_mesh.sendMessage(c, e.msg_ts, next_attempt, e.text,
                               expected_ack, est_timeout) > 0 && expected_ack) {
        e.attempt         = next_attempt;
        e.ack_tag         = expected_ack;   // each attempt has a distinct ACK CRC
        e.ack_deadline_ms = now + est_timeout + 4000;
        e.resends_left--;
      } else {
        e.ack_status = ACK_FAIL;            // couldn't compose/send — give up
      }
    }
  }

  // Recent DM contacts, newest first, deduped. Resolves the 4-byte _dm_hist
  // prefix to a 6-byte pub_key prefix by walking the contact list once per
  // unique sender. Returns the number filled.
  DmHistEntry&       dmAtPos(int pos)       { return _dm_hist[pos]; }
  const DmHistEntry& dmAtPos(int pos) const { return _dm_hist[pos]; }

private:
  // Look up a contact by 4-byte pub_key prefix (as stored in DmHistEntry).
  bool contactByPrefix(const uint8_t* prefix, ContactInfo& out) const {
    int total = the_mesh.getNumContacts();
    // +MAX_ANON_CONTACTS: see MessagesScreen.h's buildContactList() for why
    // getContactByIdx() needs this offset (raw table index, not the
    // anon-excluded logical count getNumContacts() returns).
    for (int i = 0; i < total; i++) {
      ContactInfo c;
      if (!the_mesh.getContactByIdx(MAX_ANON_CONTACTS + i, c)) continue;
      if (memcmp(c.id.pub_key, prefix, 4) == 0) { out = c; return true; }
    }
    return false;
  }

  ChHistEntry _hist[CH_HIST_MAX];
  int _hist_head, _hist_count;
  uint8_t _ch_unread[MAX_GROUP_CHANNELS];

  DmHistEntry _dm_hist[DM_HIST_MAX];
  int _dm_hist_head, _dm_hist_count;
};
