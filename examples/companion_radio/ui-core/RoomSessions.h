#pragma once
// Room server sessions for the on-device messages screens (ui-new's
// Messages > Room Servers, ui-lvgl's Messages > Rooms). Posting to a room needs
// a login handshake first, even with a blank password; this keeps which rooms
// were logged in to this power-on, the one login in flight, and the password
// memory (MyMesh::saveRoomPassword): a room logged in to on an earlier boot
// logs in again silently with the saved password, a failed login forgets it.
//
// RAM-only memo: the server's ACL is the real permission store and survives
// reboot on its own, but the device can't query it, so this only saves asking
// again for a password entered this session.

class RoomSessions {
public:
  enum Open : uint8_t { OPEN_NOW, LOGGING_IN, NEED_PASSWORD, SEND_FAILED };
  enum Outcome : uint8_t { NONE, LOGGED_IN, LOGIN_FAILED, LOGIN_TIMEOUT };

  bool isLoggedIn(const uint8_t* pub_key) const {
    for (int i = 0; i < _count; i++)
      if (memcmp(_prefix[(_head + i) % TABLE_SIZE], pub_key, 4) == 0) return true;
    return false;
  }

  // Opening a room: straight in when logged in this session, a background
  // login with the saved password, else the frontend asks for one.
  Open open(const ContactInfo& ci) {
    if (isLoggedIn(ci.id.pub_key)) return OPEN_NOW;
    char saved[sizeof(_pw)];
    if (the_mesh.getRoomPassword(ci.id.pub_key, saved, sizeof(saved)))
      return login(ci, saved) ? LOGGING_IN : SEND_FAILED;
    return NEED_PASSWORD;
  }

  bool login(const ContactInfo& ci, const char* password) {
    snprintf(_pw, sizeof(_pw), "%s", password);
    uint32_t est = 0;
    if (!the_mesh.sendRoomLogin(ci, password, est)) { _pending = false; return false; }
    memcpy(_pending_key, ci.id.pub_key, 4);
    _pending = true;
    _deadline = millis() + est + 4000;   // same margin as a DM's ACK wait
    return true;
  }

  bool loggingIn() const { return _pending; }
  bool loggingIn(const uint8_t* pub_key) const { return _pending && memcmp(_pending_key, pub_key, 4) == 0; }

  // Local only: the room ACL has no session to tear down (MyMesh::logoutRoom()).
  void logout(const uint8_t* pub_key) {
    the_mesh.logoutRoom(pub_key);
    forget(pub_key);
    if (loggingIn(pub_key)) cancel();
  }

  void cancel() {
    if (_pending) the_mesh.cancelUiPendingLogin(_pending_key);
    _pending = false;
  }

  // From UiCore::onRoomLoginResult (after the admin session passed on it).
  // True when it answers this session's login.
  bool onLoginResult(const uint8_t* pub_key, bool success) {
    if (!loggingIn(pub_key)) return false;
    _pending = false;
    if (success) {
      markLoggedIn(pub_key);
      the_mesh.saveRoomPassword(pub_key, _pw);
      _outcome = LOGGED_IN;
    } else {
      // The saved password no longer works: forget it, so the next open asks
      // instead of retrying the same bad one forever.
      the_mesh.forgetRoomPassword(pub_key);
      _outcome = LOGIN_FAILED;
    }
    memcpy(_outcome_key, pub_key, 4);
    return true;
  }

  void loop() {
    if (_pending && (int32_t)(millis() - _deadline) > 0) {
      memcpy(_outcome_key, _pending_key, 4);
      cancel();
      _outcome = LOGIN_TIMEOUT;
    }
  }

  // The last login's outcome, once (NONE when nothing new); key = its room.
  Outcome take(uint8_t* key_out = nullptr) {
    Outcome o = _outcome;
    _outcome = NONE;
    if (key_out) memcpy(key_out, _outcome_key, 4);
    return o;
  }

private:
  static const int TABLE_SIZE = 8;   // oldest dropped when full

  void markLoggedIn(const uint8_t* pub_key) {
    if (isLoggedIn(pub_key)) return;
    int pos;
    if (_count < TABLE_SIZE) pos = (_head + _count++) % TABLE_SIZE;
    else { pos = _head; _head = (_head + 1) % TABLE_SIZE; }
    memcpy(_prefix[pos], pub_key, 4);
  }

  void forget(const uint8_t* pub_key) {
    for (int i = 0; i < _count; i++) {
      if (memcmp(_prefix[(_head + i) % TABLE_SIZE], pub_key, 4) != 0) continue;
      for (int j = i; j < _count - 1; j++)
        memcpy(_prefix[(_head + j) % TABLE_SIZE], _prefix[(_head + j + 1) % TABLE_SIZE], 4);
      _count--;
      return;
    }
  }

  uint8_t _prefix[TABLE_SIZE][4];
  int _head = 0, _count = 0;
  char _pw[16] = "";            // of the login in flight, saved on success
  uint8_t _pending_key[4] = {0};
  bool _pending = false;
  uint32_t _deadline = 0;
  Outcome _outcome = NONE;
  uint8_t _outcome_key[4] = {0};
};
