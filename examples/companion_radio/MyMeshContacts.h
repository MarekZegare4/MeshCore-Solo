#pragma once
// Contacts and rooms from the UI: add / delete / favourite, telemetry
// permissions, path reset, factory reset, stale-contact pruning, room passwords.
// Not part of upstream MyMesh.cpp: included at its bottom, after all class
// definitions (as MyMeshBot.h and MyMeshCli.h).

bool MyMesh::addDiscoveredContact(const uint8_t* pub_key, const char* name, uint8_t type) {
  if (lookupContactByPubKey(pub_key, PUB_KEY_SIZE)) return true;  // already a contact
  ContactInfo c{};   // value-initialised: every field zero, the Identity constructed
  c.id = mesh::Identity(pub_key);
  strncpy(c.name, name ? name : "", sizeof(c.name) - 1);
  c.name[sizeof(c.name) - 1] = '\0';
  c.type = type;
  c.out_path_len = OUT_PATH_UNKNOWN;   // no path yet → flood until one is learned
  c.lastmod = getRTCClock()->getCurrentTime();
  c.shared_secret_valid = false;
  if (!addContact(c)) return false;    // contacts table full
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

// Full delete sequence shared by CMD_REMOVE_CONTACT and the on-device Nearby menu:
// drop the contact, its persisted blob and any saved room login, run the UI cleanup
// (favourite slot / Locator / Live Share target), then schedule the lazy write.
bool MyMesh::deleteContactByKey(const uint8_t* pub_key) {
  ContactInfo* recipient = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!recipient || !removeContact(*recipient)) return false;
  _store->deleteBlobByKey(pub_key, PUB_KEY_SIZE);
  forgetRoomPassword(pub_key);
  if (_listener) _listener->onContactRemoved(pub_key);
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

// lastmod is bumped so the app's next 'since'-filtered contact sync picks the
// change up; the write itself is lazy, like every other contact edit.
bool MyMesh::setContactFavourite(const uint8_t* pub_key, bool fav) {
  ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!c) return false;
  if (fav) c->flags |= 0x01; else c->flags &= ~0x01;
  c->lastmod = getRTCClock()->getCurrentTime();
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

bool MyMesh::setContactTelemetry(const uint8_t* pub_key, uint8_t perms) {
  ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!c) return false;
  c->flags = (c->flags & ~0x0E) | ((perms & 0x07) << 1);
  c->lastmod = getRTCClock()->getCurrentTime();
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

bool MyMesh::factoryReset() {
  bool was_on = _serial && _serial->isEnabled();
  if (_serial) {
    MESH_DEBUG_PRINTLN("Factory reset: disabling serial interface to prevent reconnects (BLE/WiFi)");
    _serial->disable();
  }
  if (_store->formatFileSystem()) return true;
  if (was_on) _serial->enable();   // nothing erased: the link as it was
  return false;
}

// lastmod bumped (unlike CMD_RESET_PATH, which the app sent itself) so the
// app's copy of the contact drops the old path too.
bool MyMesh::resetContactPath(const uint8_t* pub_key) {
  ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!c) return false;
  c->out_path_len = OUT_PATH_UNKNOWN;
  c->flags &= ~CONTACT_FLAG_ROUTE_PINNED;   // a deliberate reset ends a kept route
  c->lastmod = getRTCClock()->getCurrentTime();
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

bool MyMesh::setContactPinned(const uint8_t* pub_key, bool pin) {
  ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!c) return false;
  if (pin) c->flags |= CONTACT_FLAG_ROUTE_PINNED; else c->flags &= ~CONTACT_FLAG_ROUTE_PINNED;
  c->lastmod = getRTCClock()->getCurrentTime();
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

// A route typed on the device (ui-core/PathEditor.h): `hops` hashes of
// `hash_size` bytes into the contact's out_path (0 hops: direct), the packed length byte as
// the mesh stores a learned path (top 2 bits: size - 1). lastmod bumped, as
// for resetContactPath(), so the app's copy follows.
bool MyMesh::setContactPath(const uint8_t* pub_key, const uint8_t* path, uint8_t hash_size, uint8_t hops, bool pin) {
  if (hash_size < 1 || hash_size > 3 || hops > 63 || hops * hash_size > MAX_PATH_SIZE) return false;
  ContactInfo* c = lookupContactByPubKey(pub_key, PUB_KEY_SIZE);
  if (!c) return false;
  memcpy(c->out_path, path, hops * hash_size);
  c->out_path_len = (uint8_t)(((hash_size - 1) << 6) | hops);
  if (pin) c->flags |= CONTACT_FLAG_ROUTE_PINNED; else c->flags &= ~CONTACT_FLAG_ROUTE_PINNED;
  c->lastmod = getRTCClock()->getCurrentTime();
  dirty_contacts_expiry = futureMillis(LAZY_CONTACTS_WRITE_DELAY);
  return true;
}

// Shared staleness test for the Settings > Contacts "Prune now" sweep.
//
// Caveats on lastmod, deliberately accepted: it's "last touched", not purely
// "last heard over the air" -- setContactFavourite() above bumps it, and the
// app can write it outright (CMD_IMPORT_CONTACT / the contact-update handler).
// Every one of those pushes a contact *away* from being pruned, never towards
// it, so the error only ever errs on the side of keeping data.
//
// Favourites are always exempt; a contact never actually heard from
// (lastmod==0, shouldn't happen for a real saved contact, but a defensive
// check costs nothing) or whose lastmod reads ahead of "now" (clock skew, or
// an unset RTC after a battery pull) is left alone rather than guessed at
// either way.
static bool contactIsStale(const ContactInfo& ci, uint32_t now, uint32_t threshold_secs) {
  if (ci.flags & 0x01) return false;   // favourite bit, same one setContactFavourite() writes
  if (ci.lastmod == 0 || now < ci.lastmod) return false;
  return (now - ci.lastmod) >= threshold_secs;
}

// Threshold in seconds for the configured expiry index, or 0 when expiry is
// Off -- the one place the NodePrefs table is read, so count and prune can't
// disagree about which contacts are in scope.
uint32_t MyMesh::staleContactThresholdSecs() const {
  return (uint32_t)NodePrefs::contactExpiryDays(_prefs.contact_expiry_idx) * 86400UL;
}

int MyMesh::countStaleContacts() {
  uint32_t threshold_secs = staleContactThresholdSecs();
  if (threshold_secs == 0) return 0;
  uint32_t now = getRTCClock()->getCurrentTime();
  int count = 0;
  int n = getNumContacts();
  for (int i = 0; i < n; i++) {
    ContactInfo ci;
    if (getContactByIdx(MAX_ANON_CONTACTS + i, ci) && contactIsStale(ci, now, threshold_secs)) count++;
  }
  return count;
}

int MyMesh::pruneStaleContacts() {
  uint32_t threshold_secs = staleContactThresholdSecs();
  if (threshold_secs == 0) return 0;
  uint32_t now = getRTCClock()->getCurrentTime();
  int removed = 0;
  // Walk backwards: removeContact() compacts the array by shifting everything
  // *after* the removed slot down one, so entries at lower indices keep their
  // positions and a descending scan never revisits or skips one. (Forwards
  // would need a restart after every delete.)
  for (int i = getNumContacts() - 1; i >= 0; i--) {
    ContactInfo ci;
    if (getContactByIdx(MAX_ANON_CONTACTS + i, ci) && contactIsStale(ci, now, threshold_secs)) {
      if (deleteContactByKey(ci.id.pub_key)) removed++;
    }
  }
  return removed;
}

bool MyMesh::getRoomPassword(const uint8_t* pub_key, char* out_password, uint8_t max_len) {
  File f = _store->openRead(ROOM_PW_FILE);
  if (!f) return false;

  RoomPwRec rec;
  bool found = false;
  while (f.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
    if (memcmp(rec.key, pub_key, 4) == 0) {
      strncpy(out_password, rec.pw, max_len - 1);
      out_password[max_len - 1] = 0;
      found = true;
      break;
    }
  }
  f.close();
  return found;
}

bool MyMesh::saveRoomPassword(const uint8_t* pub_key, const char* password) {
  // The table is tiny (<= MAX_SAVED_ROOM_PASSWORDS * 20 bytes), so just load
  // it whole, update/append/evict in RAM, then rewrite -- simpler and just
  // as crash-safe as a record seek given how rarely this runs (once per new
  // room login).
  RoomPwRec recs[MAX_SAVED_ROOM_PASSWORDS];
  int count = 0;
  File rf = _store->openRead(ROOM_PW_FILE);
  if (rf) {
    RoomPwRec rec;
    while (count < MAX_SAVED_ROOM_PASSWORDS && rf.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
      if (memcmp(rec.key, pub_key, 4) != 0) { // drop stale entry for this key -- replaced below
        recs[count++] = rec;
      }
    }
    rf.close();
  }

  RoomPwRec new_rec;
  memcpy(new_rec.key, pub_key, 4);
  strncpy(new_rec.pw, password, sizeof(new_rec.pw) - 1);
  new_rec.pw[sizeof(new_rec.pw) - 1] = 0;

  if (count < MAX_SAVED_ROOM_PASSWORDS) {
    recs[count++] = new_rec;
  } else { // table full and not already present -- evict oldest (front)
    memmove(&recs[0], &recs[1], sizeof(RoomPwRec) * (MAX_SAVED_ROOM_PASSWORDS - 1));
    recs[MAX_SAVED_ROOM_PASSWORDS - 1] = new_rec;
  }

  // Write to a temp file and atomically swap it over /room_pw, so an
  // interrupted save leaves the previous good table intact rather than a
  // truncated mix (mirrors how contacts/channels are persisted).
  File wf = _store->openWrite(ROOM_PW_TMP);
  if (!wf) return false;
  size_t want = sizeof(RoomPwRec) * count;
  bool ok = (wf.write((uint8_t *)recs, want) == want);
  wf.close();
  if (!ok) { _store->removeFile(ROOM_PW_TMP); return false; } // keep previous good file
  return _store->commitFile(ROOM_PW_TMP, ROOM_PW_FILE);
}

void MyMesh::forgetRoomPassword(const uint8_t* pub_key) {
  RoomPwRec recs[MAX_SAVED_ROOM_PASSWORDS];
  int count = 0;
  File rf = _store->openRead(ROOM_PW_FILE);
  if (!rf) return;

  RoomPwRec rec;
  bool removed = false;
  while (count < MAX_SAVED_ROOM_PASSWORDS && rf.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
    if (memcmp(rec.key, pub_key, 4) == 0) {
      removed = true;
    } else {
      recs[count++] = rec;
    }
  }
  rf.close();
  if (!removed) return; // nothing to do, avoid a pointless rewrite

  File wf = _store->openWrite(ROOM_PW_TMP);
  if (!wf) return;
  size_t want = sizeof(RoomPwRec) * count;
  bool ok = (wf.write((uint8_t *)recs, want) == want);
  wf.close();
  if (!ok) { _store->removeFile(ROOM_PW_TMP); return; } // keep previous good file
  _store->commitFile(ROOM_PW_TMP, ROOM_PW_FILE);
}
