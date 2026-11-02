#pragma once
// Live location sharing, both directions:
//   • outgoing — while NodePrefs::loc_share_enabled, periodically broadcast my
//     [LOC] to the configured target (channel or DM), movement-gated so a
//     stationary device stays quiet unless a heartbeat is configured. A session
//     always ends after the chosen duration (counted in RAM from enable / boot,
//     so a reboot starts a fresh session) -> UiEventType::LiveShareEnded.
//   • incoming — [LOC] shares heard from peers (MyMesh → Listener::
//     onSharedLocation) kept in a LiveTrackStore for the Live view / map /
//     locator, expired once a minute.

#include "../LiveTrack.h"
#include "../GeoUtils.h"
#include "UiEvents.h"
#include "CourseEngine.h"

class LiveShareEngine {
public:
  void begin(NodePrefs* prefs, const CourseEngine* course, UiEventQueue* events) {
    _prefs = prefs; _course = course; _events = events;
  }

  void loop() {
    // Live-track housekeeping — drop shared positions that have gone stale, so
    // the Live view / map don't show ghosts. Cheap; once a minute.
    if ((int32_t)(millis() - _next_expire_ms) >= 0) {
      _next_expire_ms = millis() + 60000UL;
      _track.expire((uint32_t)rtc_clock.getCurrentTime());
    }
    if (!_prefs) return;

    // A session always ends: switch off once the chosen duration has run out.
    if (_prefs->loc_share_enabled && _was_enabled
        && (uint32_t)(millis() - _session_ms)
           >= (uint32_t)NodePrefs::locShareDurationMins(_prefs->loc_share_duration_idx) * 60000UL) {
      _prefs->loc_share_enabled = 0;
      _was_enabled = false;
      the_mesh.savePrefs();
      _events->push(UiEventType::LiveShareEnded);
    }
    if (_prefs->loc_share_enabled && (int32_t)(millis() - _next_check_ms) >= 0) {
      _next_check_ms = millis() + 2000UL;
      if (!_was_enabled) {
        _has_last = false;   // re-announce on enable
        _session_ms = millis();
      }
      _was_enabled = true;
      int32_t lat, lon;
      if (_course->currentLocation(lat, lon)) {
        uint16_t move_m = NodePrefs::locShareMoveMeters(_prefs->loc_share_move_idx);
        uint16_t gap_s  = NodePrefs::locShareIntervalSecs(_prefs->loc_share_interval_idx);
        uint16_t hb_s   = NodePrefs::locShareHeartbeatSecs(_prefs->loc_share_heartbeat_idx);
        uint32_t now = millis();
        bool first = !_has_last;
        float moved = first ? 1e9f
                            : geo::haversineKm(_last_lat, _last_lon, lat, lon) * 1000.0f;
        bool gap_ok = first || (now - _last_ms) >= (uint32_t)gap_s * 1000UL;
        bool hb_due = (hb_s > 0) && !first && (now - _last_ms) >= (uint32_t)hb_s * 1000UL;
        if ((moved >= (float)move_m && gap_ok) || first || hb_due) {
          if (send(lat, lon)) {
            _last_lat = lat;
            _last_lon = lon;
            _last_ms  = now;
            _has_last = true;
          }
        }
      }
    } else if (!_prefs->loc_share_enabled) {
      _was_enabled = false;
    }
  }

  // Start the session afresh (next tick treats it as a new enable: re-announce
  // + new duration clock).
  void restartSession() { _was_enabled = false; }
  // Sharing on (a session of its own, announced at once) or off.
  void setEnabled(bool on) {
    if (!_prefs) return;
    _prefs->loc_share_enabled = on ? 1 : 0;
    if (on) restartSession();
  }
  // Restart only the session's duration clock -- unlike restartSession(), no
  // re-announce. For a changed "Stop after" length, where position hasn't
  // changed. No-op before a session has started (loop() sets the clock then).
  void restartClock() { if (_was_enabled) _session_ms = millis(); }

  // Seconds left in the running session (the whole length before its first
  // tick), 0 when sharing is off.
  uint32_t remainingSecs() const {
    if (!_prefs || !_prefs->loc_share_enabled) return 0;
    uint32_t total = (uint32_t)NodePrefs::locShareDurationMins(_prefs->loc_share_duration_idx) * 60UL;
    if (!_was_enabled) return total;
    uint32_t el = (uint32_t)(millis() - _session_ms) / 1000UL;
    return el >= total ? 0 : total - el;
  }

  // Send one [LOC] message to the configured live-share target. Returns false
  // if the target can't be resolved (no such channel / contact).
  bool send(int32_t lat, int32_t lon) {
    if (!_prefs) return false;
    // Live Share's own scope, if set: applies to these sends only (0 = follow the
    // target's usual scope). The sends below are synchronous, so bracketing works.
    // A value past the list's end (list shrunk other than via removeScope())
    // follows the target, as LiveShareScreen shows it -- ScopeList::key() would
    // otherwise clamp it to "*" and send unscoped.
    struct ScopeGuard {
      bool on;
      explicit ScopeGuard(uint8_t v) : on(v != 0 && v <= the_mesh.scopeList().count + 1) {
        if (on) the_mesh.setOneShotScope(v - 1);
      }
      ~ScopeGuard() { if (on) the_mesh.clearOneShotScope(); }
    } scope_guard(_prefs->loc_share_scope);
    char text[80];
    if (_prefs->loc_share_target_type == 0) {
      // Channel: sendGroupMessage prepends "<name>: ", so the payload already
      // names the sender — keep the [LOC] text bare.
      geo::fmtLocShare(text, sizeof(text), lat, lon);
      ChannelDetails ch;
      if (!the_mesh.getChannel(_prefs->loc_share_channel_idx, ch)) return false;
      return the_mesh.sendGroupMessage(rtc_clock.getCurrentTime(), ch.channel,
                                       the_mesh.getNodeName(), text, strlen(text));
    }
    // DM carries no per-message sender prefix, so embed the name in the text — the
    // share is then self-describing in any chat client (a trailing token after the
    // coordinate, which parseLocShare ignores on the receiving side).
    ContactInfo* c = the_mesh.lookupContactByPubKey(_prefs->loc_share_dm_prefix,
                                                    NodePrefs::FAVOURITE_PREFIX_LEN);
    if (!c) return false;
    geo::fmtLocShare(text, sizeof(text), lat, lon, the_mesh.getNodeName());
    uint32_t expected_ack = 0, est_timeout = 0;
    return the_mesh.sendMessage(*c, rtc_clock.getCurrentTime(), 0, text, expected_ack, est_timeout) > 0;
  }

  // A peer broadcast its position via a [LOC] message (parsed in MyMesh). Gated
  // on the user preference so tracking stays opt-in.
  void onSharedLocation(const uint8_t* pub_key, const char* name,
                        int32_t lat_1e6, int32_t lon_1e6, uint32_t ts, bool verified) {
    if (!_prefs || !_prefs->track_shared_loc) return;
    _track.update(pub_key, name, lat_1e6, lon_1e6, ts, verified);
  }

  // Someone's SOS. Not gated on Track loc, and named so it stands out on the map;
  // the time is ours, as the sender's clock may never have been set.
  void onSos(const uint8_t* pub_key, const char* name, int32_t lat_1e6, int32_t lon_1e6, bool verified) {
    char label[LiveTrackStore::NAME_LEN];
    snprintf(label, sizeof(label), "SOS %s", name ? name : "");
    _track.update(pub_key, label, lat_1e6, lon_1e6, (uint32_t)rtc_clock.getCurrentTime(), verified, true);
  }

  // An advert with a position: moves that node's SOS, if one is on the table.
  void onAdvertPosition(const uint8_t* pub_key, const char* name, int32_t lat_1e6, int32_t lon_1e6) {
    _track.followSos(pub_key, name, lat_1e6, lon_1e6, (uint32_t)rtc_clock.getCurrentTime());
  }

  LiveTrackStore&       track()       { return _track; }
  const LiveTrackStore& track() const { return _track; }

private:
  NodePrefs*          _prefs  = nullptr;
  const CourseEngine* _course = nullptr;
  UiEventQueue*       _events = nullptr;

  LiveTrackStore _track;
  uint32_t _next_expire_ms = 0;

  uint32_t _next_check_ms = 0;
  uint32_t _last_ms = 0;
  int32_t  _last_lat = 0, _last_lon = 0;
  bool     _has_last = false;
  bool     _was_enabled = false;
  uint32_t _session_ms = 0;   // when the current session began (RAM only)
};
