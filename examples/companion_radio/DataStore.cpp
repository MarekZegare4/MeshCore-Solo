#include <Arduino.h>
#include "DataStore.h"

#if defined(EXTRAFS) || defined(QSPIFLASH)
  #define MAX_BLOBRECS 100
#else
  #define MAX_BLOBRECS 20
#endif

DataStore::DataStore(FILESYSTEM& fs, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(nullptr), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}

#if defined(EXTRAFS) || defined(QSPIFLASH)
DataStore::DataStore(FILESYSTEM& fs, FILESYSTEM& fsExtra, mesh::RTCClock& clock) : _fs(&fs), _fsExtra(&fsExtra), _clock(&clock),
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
    identity_store(fs, "")
#elif defined(RP2040_PLATFORM)
    identity_store(fs, "/identity")
#else
    identity_store(fs, "/identity")
#endif
{
}
#endif

static File openWrite(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  fs->remove(filename);
  return fs->open(filename, FILE_O_WRITE);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "w");
#else
  return fs->open(filename, "w", true);
#endif
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  static uint32_t _ContactsChannelsTotalBlocks = 0;
#endif

void DataStore::begin() {
#if defined(RP2040_PLATFORM)
  identity_store.begin();
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  _ContactsChannelsTotalBlocks = _getContactsChannelsFS()->_getFS()->cfg->block_count;
  checkAdvBlobFile();
  #if defined(EXTRAFS) || defined(QSPIFLASH)
  migrateToSecondaryFS();
  #endif
#else
  // init 'blob store' support
  _fs->mkdir("/bl");
#endif
}

#if defined(ESP32)
  #include <SPIFFS.h>
  #include <nvs_flash.h>
#elif defined(RP2040_PLATFORM)
  #include <LittleFS.h>
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  #if defined(QSPIFLASH)
    #include <CustomLFS_QSPIFlash.h>
  #elif defined(EXTRAFS)
    #include <CustomLFS.h>
  #else 
    #include <InternalFileSystem.h>
  #endif
#endif

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
int _countLfsBlock(void *p, lfs_block_t block){
      if (block > _ContactsChannelsTotalBlocks) {
        MESH_DEBUG_PRINTLN("ERROR: Block %d exceeds filesystem bounds - CORRUPTION DETECTED!", block);
        return LFS_ERR_CORRUPT;  // return error to abort lfs_traverse() gracefully
    }
  lfs_size_t *size = (lfs_size_t*) p;
  *size += 1;
    return 0;
}

lfs_ssize_t _getLfsUsedBlockCount(FILESYSTEM* fs) {
  lfs_size_t size = 0;
  int err = lfs_traverse(fs->_getFS(), _countLfsBlock, &size);
  if (err) {
    MESH_DEBUG_PRINTLN("ERROR: lfs_traverse() error: %d", err);
    return 0;
  }
  return size;
}
#endif

uint32_t DataStore::getStorageUsedKb() const {
#if defined(ESP32)
  return SPIFFS.usedBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.usedBytes = 0;
  _fs->info(info);
  return info.usedBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int usedBlockCount = _getLfsUsedBlockCount(_getContactsChannelsFS());
  int usedBytes = config->block_size * usedBlockCount;
  return usedBytes / 1024;
#else
  return 0;
#endif
}

uint32_t DataStore::getStorageTotalKb() const {
#if defined(ESP32)
  return SPIFFS.totalBytes() / 1024;
#elif defined(RP2040_PLATFORM)
  FSInfo info;
  info.totalBytes = 0;
  _fs->info(info);
  return info.totalBytes / 1024;
#elif defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  const lfs_config* config = _getContactsChannelsFS()->_getFS()->cfg;
  int totalBytes = config->block_size * config->block_count;
  return totalBytes / 1024;
#else
  return 0;
#endif
}

File DataStore::openRead(const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return _fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return _fs->open(filename, "r");
#else
  return _fs->open(filename, "r", false);
#endif
}

File DataStore::openRead(FILESYSTEM* fs, const char* filename) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->open(filename, FILE_O_READ);
#elif defined(RP2040_PLATFORM)
  return fs->open(filename, "r");
#else
  return fs->open(filename, "r", false);
#endif
}

File DataStore::openWrite(const char* filename) {
  return ::openWrite(_fs, filename);
}

bool DataStore::removeFile(const char* filename) {
  return _fs->remove(filename);
}

bool DataStore::removeFile(FILESYSTEM* fs, const char* filename) {
  return fs->remove(filename);
}

bool DataStore::formatFileSystem() {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  if (_fsExtra == nullptr) {
    return _fs->format();
  } else {
    return _fs->format() && _fsExtra->format();
  }
#elif defined(RP2040_PLATFORM)
  return LittleFS.format();
#elif defined(ESP32)
  bool fs_success = ((fs::SPIFFSFS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#else
  #error "need to implement format()"
#endif
}

bool DataStore::loadMainIdentity(mesh::LocalIdentity &identity) {
  return identity_store.load("_main", identity);
}

bool DataStore::saveMainIdentity(const mesh::LocalIdentity &identity) {
  return identity_store.save("_main", identity);
}

void DataStore::loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon) {
  if (_fs->exists("/new_prefs")) {
    loadPrefsInt("/new_prefs", prefs, node_lat, node_lon); // new filename
  } else if (_fs->exists("/node_prefs")) {
    loadPrefsInt("/node_prefs", prefs, node_lat, node_lon);
    savePrefs(prefs, node_lat, node_lon);                // save to new filename
    _fs->remove("/node_prefs"); // remove old
  }
}

void DataStore::loadPrefsInt(const char *filename, NodePrefs& _prefs, double& node_lat, double& node_lon) {
  // Set hardware defaults before reading — if the file is older and lacks these fields,
  // the compile-time values apply rather than the zero from memset in MyMesh::begin().
#ifdef DISPLAY_ROTATION
  _prefs.display_rotation = DISPLAY_ROTATION;
#endif
  File file = openRead(_fs, filename);
  if (!file) return;

  uint8_t pad[8];

  // Core fields — present in every saved file (written unconditionally since the beginning).
  file.read((uint8_t *)&_prefs.airtime_factor, sizeof(_prefs.airtime_factor));
  file.read((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));
  file.read(pad, 4);
  file.read((uint8_t *)&node_lat, sizeof(node_lat));
  file.read((uint8_t *)&node_lon, sizeof(node_lon));
  file.read((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));
  file.read((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));
  file.read((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));
  file.read((uint8_t *)&_prefs.client_repeat, sizeof(_prefs.client_repeat));
  file.read((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts));
  file.read((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));
  file.read((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));
  file.read((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base));
  file.read((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));
  file.read((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));
  file.read((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));
  file.read((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));
  file.read((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));
  file.read((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));
  file.read(pad, 1);
  file.read((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));
  file.read((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));
  file.read((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));
  file.read((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));
  file.read((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));
  file.read((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));
  file.read((uint8_t *)&_prefs.rx_boosted_gain, sizeof(_prefs.rx_boosted_gain));
  file.read((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));
  file.read((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));
  file.read((uint8_t *)&_prefs.display_brightness, sizeof(_prefs.display_brightness));
  file.read((uint8_t *)&_prefs.auto_off_secs, sizeof(_prefs.auto_off_secs));
  file.read((uint8_t *)&_prefs.tz_offset_hours, sizeof(_prefs.tz_offset_hours));
  file.read((uint8_t *)&_prefs.low_batt_mv, sizeof(_prefs.low_batt_mv));
  file.read((uint8_t *)&_prefs.batt_display_mode, sizeof(_prefs.batt_display_mode));

  // Extension fields — append-only, newest at the bottom.
  // Each read is gated on file.available(); fields absent in older files stay at their
  // zero-initialised or hardware-default values set above.
  auto rd = [&](void* p, size_t n) {
    if (file.available() >= (int)n) file.read((uint8_t*)p, n);
  };

  rd(_prefs.custom_msgs,                sizeof(_prefs.custom_msgs));
  rd(&_prefs.ch_notif_override,         sizeof(_prefs.ch_notif_override));
  rd(&_prefs.ch_notif_muted,            sizeof(_prefs.ch_notif_muted));
  rd(&_prefs.dm_show_all,               sizeof(_prefs.dm_show_all));
  rd(&_prefs.room_fav_only,             sizeof(_prefs.room_fav_only));
  rd(&_prefs.buzzer_volume,             sizeof(_prefs.buzzer_volume));
  rd(&_prefs.ringtone_bpm_idx,          sizeof(_prefs.ringtone_bpm_idx));
  rd(&_prefs.ringtone_len,              sizeof(_prefs.ringtone_len));
  if (_prefs.ringtone_len > 32) _prefs.ringtone_len = 0;
  rd(_prefs.ringtone_notes,             sizeof(_prefs.ringtone_notes));
  rd(&_prefs.home_pages_mask,           sizeof(_prefs.home_pages_mask));
  rd(&_prefs.bot_enabled,               sizeof(_prefs.bot_enabled));
  rd(&_prefs.bot_channel_enabled,       sizeof(_prefs.bot_channel_enabled));
  rd(&_prefs.bot_channel_idx,           sizeof(_prefs.bot_channel_idx));
  rd(_prefs.bot_trigger,                sizeof(_prefs.bot_trigger));
  for (char* p = _prefs.bot_trigger; *p; p++) *p = (char)tolower((uint8_t)*p);
  rd(_prefs.bot_reply_dm,               sizeof(_prefs.bot_reply_dm));
  rd(_prefs.bot_reply_ch,               sizeof(_prefs.bot_reply_ch));
  rd(&_prefs.clock_hide_seconds,        sizeof(_prefs.clock_hide_seconds));
  rd(&_prefs.buzzer_auto,               sizeof(_prefs.buzzer_auto));
  rd(_prefs.dm_notif,                   sizeof(_prefs.dm_notif));
  rd(_prefs.dashboard_fields,           sizeof(_prefs.dashboard_fields));
  rd(&_prefs.advert_auto_interval_sec,  sizeof(_prefs.advert_auto_interval_sec));
  rd(&_prefs.ringtone2_bpm_idx,         sizeof(_prefs.ringtone2_bpm_idx));
  rd(&_prefs.ringtone2_len,             sizeof(_prefs.ringtone2_len));
  if (_prefs.ringtone2_len > 32) _prefs.ringtone2_len = 0;
  rd(_prefs.ringtone2_notes,            sizeof(_prefs.ringtone2_notes));
  rd(&_prefs.notif_melody_dm,           sizeof(_prefs.notif_melody_dm));
  if (_prefs.notif_melody_dm > 3) _prefs.notif_melody_dm = 0;
  rd(&_prefs.notif_melody_ch,           sizeof(_prefs.notif_melody_ch));
  if (_prefs.notif_melody_ch > 3) _prefs.notif_melody_ch = 0;
  rd(&_prefs.ch_notif_melody_set,       sizeof(_prefs.ch_notif_melody_set));
  rd(&_prefs.ch_notif_melody_2,         sizeof(_prefs.ch_notif_melody_2));
  rd(_prefs.dm_melody,                  sizeof(_prefs.dm_melody));
  rd(&_prefs.auto_lock,                 sizeof(_prefs.auto_lock));
  rd(&_prefs.clock_12h,                 sizeof(_prefs.clock_12h));
  rd(&_prefs.use_lemon_font,            sizeof(_prefs.use_lemon_font));
  rd(&_prefs.display_rotation,          sizeof(_prefs.display_rotation));
  rd(_prefs.page_order,                 sizeof(_prefs.page_order));
  rd(&_prefs.joystick_rotation,         sizeof(_prefs.joystick_rotation));
  // Override any stale stored value — must come AFTER rd() so files that already
  // have the field (e.g. migrated from e-ink with rotation=2) are also corrected.
#ifdef JOYSTICK_ROTATION
  _prefs.joystick_rotation = JOYSTICK_ROTATION;
#elif !FEAT_JOYSTICK_ROTATION_SETTING
  _prefs.joystick_rotation = 0;
#endif
  rd(&_prefs.eink_full_refresh_every,   sizeof(_prefs.eink_full_refresh_every));
  rd(&_prefs.page_order_set,            sizeof(_prefs.page_order_set));
  // Migration: pre-magic firmware wrote page_order without a flag. If we see a plausible
  // first entry from such a save, accept it once — savePrefs will then persist the magic.
  if (_prefs.page_order_set != NodePrefs::PAGE_ORDER_MAGIC
      && _prefs.page_order[0] >= 1 && _prefs.page_order[0] <= NodePrefs::HPB_COUNT) {
    _prefs.page_order_set = NodePrefs::PAGE_ORDER_MAGIC;
  }

  rd(_prefs.favourite_contacts, sizeof(_prefs.favourite_contacts));
  rd(&_prefs.trail_interval_idx,  sizeof(_prefs.trail_interval_idx));
  rd(&_prefs.trail_min_delta_idx, sizeof(_prefs.trail_min_delta_idx));
  rd(&_prefs.trail_units_idx,     sizeof(_prefs.trail_units_idx));
  rd(&_prefs.ch_fav_bitmask,      sizeof(_prefs.ch_fav_bitmask));
  rd(&_prefs.ch_fav_only,         sizeof(_prefs.ch_fav_only));
  rd(&_prefs.notif_melody_ad,     sizeof(_prefs.notif_melody_ad));
  rd(&_prefs.units_imperial,      sizeof(_prefs.units_imperial));
  rd(&_prefs.trail_show_pace,     sizeof(_prefs.trail_show_pace));
  rd(&_prefs.advert_sound_scope,  sizeof(_prefs.advert_sound_scope));
  rd(&_prefs.rx_powersave,        sizeof(_prefs.rx_powersave));
  // These fields were appended over successive schema bumps; an older file
  // can leave stray bytes here, so clamp out-of-range values back to defaults.
  // Values for notif_melody_ad: 0=built-in, 1=melody1, 2=melody2, 3=none.
  if (_prefs.notif_melody_ad > 3) _prefs.notif_melody_ad = 0;
  if (_prefs.units_imperial  > 1) _prefs.units_imperial  = 0;
  if (_prefs.trail_show_pace > 1) _prefs.trail_show_pace = 0;
  if (_prefs.advert_sound_scope > 1) _prefs.advert_sound_scope = ADVERT_SOUND_SCOPE_ALL;
  if (_prefs.rx_powersave    > 1) _prefs.rx_powersave    = 0;

  // Schema sentinel: bumped on layout changes. Mismatch means an older file
  // (or a different schema); rd() already zero-inits any fields not present,
  // so we just log it — next savePrefs writes the current sentinel.
  uint32_t sentinel = 0;
  rd(&sentinel, sizeof(sentinel));
  if (sentinel != NodePrefs::SCHEMA_SENTINEL) {
    MESH_DEBUG_PRINTLN("prefs schema sentinel mismatch: got 0x%08X, expected 0x%08X — re-saving on next change",
                       (unsigned)sentinel, (unsigned)NodePrefs::SCHEMA_SENTINEL);
    // 0xC0DE0001 → 0xC0DE0002: FAVOURITES home page added. Older saves have it
    // implicitly off (their mask doesn't include HP_FAVOURITES); turn it on so
    // upgraded users see the new page by default and can toggle it later.
    if (_prefs.home_pages_mask != 0) {
      _prefs.home_pages_mask |= NodePrefs::HP_FAVOURITES;
    }
    // 0xC0DE0003 → 0xC0DE0004: trail_units_idx added after trail_min_delta_idx.
    // On a 0xC0DE0003 file the sentinel bytes sit where trail_units_idx is now,
    // so rd() picks up 0x03 (low byte of the old sentinel) — reset just that
    // case to default 0. Newer mismatches (e.g. 0xC0DE0005 → 0xC0DE0006) had
    // the field saved correctly and must not be clobbered.
    if (sentinel == 0xC0DE0003) {
      _prefs.trail_units_idx = 0;
    }
    // → 0xC0DE0008: append advert_sound_scope after the existing 0xC0DE0007
    // tail (notif_melody_ad + units_imperial + trail_show_pace). Older files
    // leave stray/old bytes in these fields; they're clamped above, so
    // upgraders fall back to built-in advert sound + metric + speed + All.
  }

  file.close();
}

void DataStore::savePrefs(const NodePrefs& _prefs, double node_lat, double node_lon) {
  File file = ::openWrite(_fs, "/new_prefs");
  if (file) {
    uint8_t pad[8];
    memset(pad, 0, sizeof(pad));

    file.write((uint8_t *)&_prefs.airtime_factor, sizeof(float));
    file.write((uint8_t *)_prefs.node_name, sizeof(_prefs.node_name));
    file.write(pad, 4);
    file.write((uint8_t *)&node_lat, sizeof(node_lat));
    file.write((uint8_t *)&node_lon, sizeof(node_lon));
    file.write((uint8_t *)&_prefs.freq, sizeof(_prefs.freq));
    file.write((uint8_t *)&_prefs.sf, sizeof(_prefs.sf));
    file.write((uint8_t *)&_prefs.cr, sizeof(_prefs.cr));
    file.write((uint8_t *)&_prefs.client_repeat, sizeof(_prefs.client_repeat));
    file.write((uint8_t *)&_prefs.manual_add_contacts, sizeof(_prefs.manual_add_contacts));
    file.write((uint8_t *)&_prefs.bw, sizeof(_prefs.bw));
    file.write((uint8_t *)&_prefs.tx_power_dbm, sizeof(_prefs.tx_power_dbm));
    file.write((uint8_t *)&_prefs.telemetry_mode_base, sizeof(_prefs.telemetry_mode_base));
    file.write((uint8_t *)&_prefs.telemetry_mode_loc, sizeof(_prefs.telemetry_mode_loc));
    file.write((uint8_t *)&_prefs.telemetry_mode_env, sizeof(_prefs.telemetry_mode_env));
    file.write((uint8_t *)&_prefs.rx_delay_base, sizeof(_prefs.rx_delay_base));
    file.write((uint8_t *)&_prefs.advert_loc_policy, sizeof(_prefs.advert_loc_policy));
    file.write((uint8_t *)&_prefs.multi_acks, sizeof(_prefs.multi_acks));
    file.write((uint8_t *)&_prefs.path_hash_mode, sizeof(_prefs.path_hash_mode));
    file.write(pad, 1);
    file.write((uint8_t *)&_prefs.ble_pin, sizeof(_prefs.ble_pin));
    file.write((uint8_t *)&_prefs.buzzer_quiet, sizeof(_prefs.buzzer_quiet));
    file.write((uint8_t *)&_prefs.gps_enabled, sizeof(_prefs.gps_enabled));
    file.write((uint8_t *)&_prefs.gps_interval, sizeof(_prefs.gps_interval));
    file.write((uint8_t *)&_prefs.autoadd_config, sizeof(_prefs.autoadd_config));
    file.write((uint8_t *)&_prefs.autoadd_max_hops, sizeof(_prefs.autoadd_max_hops));
    file.write((uint8_t *)&_prefs.rx_boosted_gain, sizeof(_prefs.rx_boosted_gain));
    file.write((uint8_t *)_prefs.default_scope_name, sizeof(_prefs.default_scope_name));
    file.write((uint8_t *)_prefs.default_scope_key, sizeof(_prefs.default_scope_key));
    file.write((uint8_t *)&_prefs.display_brightness, sizeof(_prefs.display_brightness));
    file.write((uint8_t *)&_prefs.auto_off_secs, sizeof(_prefs.auto_off_secs));
    file.write((uint8_t *)&_prefs.tz_offset_hours, sizeof(_prefs.tz_offset_hours));
    file.write((uint8_t *)&_prefs.low_batt_mv, sizeof(_prefs.low_batt_mv));
    file.write((uint8_t *)&_prefs.batt_display_mode, sizeof(_prefs.batt_display_mode));
    file.write((uint8_t *)_prefs.custom_msgs, sizeof(_prefs.custom_msgs));
    file.write((uint8_t *)&_prefs.ch_notif_override, sizeof(_prefs.ch_notif_override));
    file.write((uint8_t *)&_prefs.ch_notif_muted, sizeof(_prefs.ch_notif_muted));
    file.write((uint8_t *)&_prefs.dm_show_all, sizeof(_prefs.dm_show_all));
    file.write((uint8_t *)&_prefs.room_fav_only, sizeof(_prefs.room_fav_only));
    file.write((uint8_t *)&_prefs.buzzer_volume, sizeof(_prefs.buzzer_volume));
    file.write((uint8_t *)&_prefs.ringtone_bpm_idx, sizeof(_prefs.ringtone_bpm_idx));
    file.write((uint8_t *)&_prefs.ringtone_len, sizeof(_prefs.ringtone_len));
    file.write((uint8_t *)_prefs.ringtone_notes, sizeof(_prefs.ringtone_notes));
    file.write((uint8_t *)&_prefs.home_pages_mask, sizeof(_prefs.home_pages_mask));
    file.write((uint8_t *)&_prefs.bot_enabled, sizeof(_prefs.bot_enabled));
    file.write((uint8_t *)&_prefs.bot_channel_enabled, sizeof(_prefs.bot_channel_enabled));
    file.write((uint8_t *)&_prefs.bot_channel_idx, sizeof(_prefs.bot_channel_idx));
    file.write((uint8_t *)_prefs.bot_trigger, sizeof(_prefs.bot_trigger));
    file.write((uint8_t *)_prefs.bot_reply_dm, sizeof(_prefs.bot_reply_dm));
    file.write((uint8_t *)_prefs.bot_reply_ch, sizeof(_prefs.bot_reply_ch));
    file.write((uint8_t *)&_prefs.clock_hide_seconds, sizeof(_prefs.clock_hide_seconds));
    file.write((uint8_t *)&_prefs.buzzer_auto, sizeof(_prefs.buzzer_auto));
    file.write((uint8_t *)_prefs.dm_notif, sizeof(_prefs.dm_notif));
    file.write((uint8_t *)_prefs.dashboard_fields, sizeof(_prefs.dashboard_fields));
    file.write((uint8_t *)&_prefs.advert_auto_interval_sec, sizeof(_prefs.advert_auto_interval_sec));
    file.write((uint8_t *)&_prefs.ringtone2_bpm_idx, sizeof(_prefs.ringtone2_bpm_idx));
    file.write((uint8_t *)&_prefs.ringtone2_len, sizeof(_prefs.ringtone2_len));
    file.write((uint8_t *)_prefs.ringtone2_notes, sizeof(_prefs.ringtone2_notes));
    file.write((uint8_t *)&_prefs.notif_melody_dm, sizeof(_prefs.notif_melody_dm));
    file.write((uint8_t *)&_prefs.notif_melody_ch, sizeof(_prefs.notif_melody_ch));
    file.write((uint8_t *)&_prefs.ch_notif_melody_set, sizeof(_prefs.ch_notif_melody_set));
    file.write((uint8_t *)&_prefs.ch_notif_melody_2, sizeof(_prefs.ch_notif_melody_2));
    file.write((uint8_t *)_prefs.dm_melody, sizeof(_prefs.dm_melody));
    file.write((uint8_t *)&_prefs.auto_lock, sizeof(_prefs.auto_lock));
    file.write((uint8_t *)&_prefs.clock_12h, sizeof(_prefs.clock_12h));
    file.write((uint8_t *)&_prefs.use_lemon_font, sizeof(_prefs.use_lemon_font));
    file.write((uint8_t *)&_prefs.display_rotation, sizeof(_prefs.display_rotation));
    file.write((uint8_t *)_prefs.page_order, sizeof(_prefs.page_order));
    file.write((uint8_t *)&_prefs.joystick_rotation, sizeof(_prefs.joystick_rotation));
    file.write((uint8_t *)&_prefs.eink_full_refresh_every, sizeof(_prefs.eink_full_refresh_every));
    file.write((uint8_t *)&_prefs.page_order_set, sizeof(_prefs.page_order_set));
    file.write((uint8_t *)_prefs.favourite_contacts, sizeof(_prefs.favourite_contacts));
    file.write((uint8_t *)&_prefs.trail_interval_idx,  sizeof(_prefs.trail_interval_idx));
    file.write((uint8_t *)&_prefs.trail_min_delta_idx, sizeof(_prefs.trail_min_delta_idx));
    file.write((uint8_t *)&_prefs.trail_units_idx,     sizeof(_prefs.trail_units_idx));
    file.write((uint8_t *)&_prefs.ch_fav_bitmask,      sizeof(_prefs.ch_fav_bitmask));
    file.write((uint8_t *)&_prefs.ch_fav_only,         sizeof(_prefs.ch_fav_only));
    file.write((uint8_t *)&_prefs.notif_melody_ad,     sizeof(_prefs.notif_melody_ad));
    file.write((uint8_t *)&_prefs.units_imperial,      sizeof(_prefs.units_imperial));
    file.write((uint8_t *)&_prefs.trail_show_pace,     sizeof(_prefs.trail_show_pace));
    file.write((uint8_t *)&_prefs.advert_sound_scope,  sizeof(_prefs.advert_sound_scope));
    file.write((uint8_t *)&_prefs.rx_powersave,        sizeof(_prefs.rx_powersave));

    // Tail sentinel — must be last. See NodePrefs::SCHEMA_SENTINEL.
    uint32_t sentinel = NodePrefs::SCHEMA_SENTINEL;
    file.write((uint8_t *)&sentinel, sizeof(sentinel));

    file.close();
  }
}

void DataStore::saveRTCTime() {
  uint32_t t = _clock->getCurrentTime();
  if (t < 1000000000UL) return;  // don't save if time not yet synced
  File file = ::openWrite(_fs, "/rtc_save");
  if (file) {
    file.write((uint8_t *)&t, sizeof(t));
    file.close();
  }
}

void DataStore::restoreRTCTime() {
  File file = openRead(_fs, "/rtc_save");
  if (file) {
    uint32_t t = 0;
    file.read((uint8_t *)&t, sizeof(t));
    file.close();
    if (t > 1000000000UL) _clock->setCurrentTime(t);
  }
}

void DataStore::loadContacts(DataStoreHost* host) {
File file = openRead(_getContactsChannelsFS(), "/contacts3");
    if (file) {
      bool full = false;
      while (!full) {
        ContactInfo c;
        uint8_t pub_key[32];
        uint8_t unused;

        bool success = (file.read(pub_key, 32) == 32);
        success = success && (file.read((uint8_t *)&c.name, 32) == 32);
        success = success && (file.read(&c.type, 1) == 1);
        success = success && (file.read(&c.flags, 1) == 1);
        success = success && (file.read(&unused, 1) == 1);
        success = success && (file.read((uint8_t *)&c.sync_since, 4) == 4); // was 'reserved'
        success = success && (file.read((uint8_t *)&c.out_path_len, 1) == 1);
        success = success && (file.read((uint8_t *)&c.last_advert_timestamp, 4) == 4);
        success = success && (file.read(c.out_path, 64) == 64);
        success = success && (file.read((uint8_t *)&c.lastmod, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lat, 4) == 4);
        success = success && (file.read((uint8_t *)&c.gps_lon, 4) == 4);

        if (!success) break; // EOF

        c.id = mesh::Identity(pub_key);
        if (!host->onContactLoaded(c)) full = true;
      }
      file.close();
    }
}

void DataStore::saveContacts(DataStoreHost* host, bool (*filter)(const ContactInfo& c)) {
  File file = ::openWrite(_getContactsChannelsFS(), "/contacts3");
  if (file) {
    uint32_t idx = 0;
    ContactInfo c;
    uint8_t unused = 0;

    while (host->getContactForSave(idx, c)) {
      if (filter && !filter(c)) {
        idx++;  // advance to next contact
        continue;
      }
      bool success = (file.write(c.id.pub_key, 32) == 32);
      success = success && (file.write((uint8_t *)&c.name, 32) == 32);
      success = success && (file.write(&c.type, 1) == 1);
      success = success && (file.write(&c.flags, 1) == 1);
      success = success && (file.write(&unused, 1) == 1);
      success = success && (file.write((uint8_t *)&c.sync_since, 4) == 4);
      success = success && (file.write((uint8_t *)&c.out_path_len, 1) == 1);
      success = success && (file.write((uint8_t *)&c.last_advert_timestamp, 4) == 4);
      success = success && (file.write(c.out_path, 64) == 64);
      success = success && (file.write((uint8_t *)&c.lastmod, 4) == 4);
      success = success && (file.write((uint8_t *)&c.gps_lat, 4) == 4);
      success = success && (file.write((uint8_t *)&c.gps_lon, 4) == 4);

      if (!success) break; // write failed

      idx++;  // advance to next contact
    }
    file.close();
  }
}

void DataStore::loadChannels(DataStoreHost* host) {
    File file = openRead(_getContactsChannelsFS(), "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      uint8_t skipped = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);

        if (!success) break; // EOF

        // Sanity check: an all-zero secret means the entry is uninitialised
        // or the file format was corrupted by a previous firmware (different
        // layout). Loading such a channel makes findChannelIdx() match the
        // wrong slot for incoming messages — drop it. The companion app can
        // re-sync the channel afterwards.
        bool secret_empty = true;
        for (int b = 0; b < 32; b++) {
          if (ch.channel.secret[b] != 0) { secret_empty = false; break; }
        }
        if (secret_empty) {
          skipped++;
          continue;
        }
        // Defensive: ensure name is null-terminated so callers can treat it
        // as a C string regardless of how the file was written.
        ch.name[31] = '\0';

        if (host->onChannelLoaded(channel_idx, ch)) {
          channel_idx++;
        } else {
          full = true;
        }
      }
      file.close();
      if (skipped > 0) {
        MESH_DEBUG_PRINTLN("loadChannels: skipped %u corrupted/empty channel entr%s",
                           (unsigned)skipped, skipped == 1 ? "y" : "ies");
      }
    }
}

void DataStore::saveChannels(DataStoreHost* host) {
  File file = ::openWrite(_getContactsChannelsFS(), "/channels2");
  if (file) {
    uint8_t channel_idx = 0;
    ChannelDetails ch;
    uint8_t unused[4];
    memset(unused, 0, 4);

    while (host->getChannelForSave(channel_idx, ch)) {
      bool success = (file.write(unused, 4) == 4);
      success = success && (file.write((uint8_t *)ch.name, 32) == 32);
      success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);

      if (!success) break; // write failed
      channel_idx++;
    }
    file.close();
  }
}

#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)

#define MAX_ADVERT_PKT_LEN   (2 + 32 + PUB_KEY_SIZE + 4 + SIGNATURE_SIZE + MAX_ADVERT_DATA_SIZE)

struct BlobRec {
  uint32_t timestamp;
  uint8_t  key[7];
  uint8_t  len;
  uint8_t  data[MAX_ADVERT_PKT_LEN];
};

void DataStore::checkAdvBlobFile() {
  if (!_getContactsChannelsFS()->exists("/adv_blobs")) {
    File file = ::openWrite(_getContactsChannelsFS(), "/adv_blobs");
    if (file) {
      BlobRec zeroes;
      memset(&zeroes, 0, sizeof(zeroes));
      for (int i = 0; i < MAX_BLOBRECS; i++) {     // pre-allocate to fixed size
        file.write((uint8_t *) &zeroes, sizeof(zeroes));
      }
      file.close();
    }
  }
}

void DataStore::migrateToSecondaryFS() {
  // migrate old adv_blobs, contacts3 and channels2 files to secondary FS if they don't already exist
  if (!_fsExtra->exists("/adv_blobs")) {
    if (_fs->exists("/adv_blobs")) {
    File oldAdvBlobs = openRead(_fs, "/adv_blobs");
    File newAdvBlobs = ::openWrite(_fsExtra, "/adv_blobs");

    if (oldAdvBlobs && newAdvBlobs) {
      BlobRec rec;
      size_t count = 0;

      // Copy 20 BlobRecs from old to new
      while (count < 20 && oldAdvBlobs.read((uint8_t *)&rec, sizeof(rec)) == sizeof(rec)) {
        newAdvBlobs.seek(count * sizeof(BlobRec));
        newAdvBlobs.write((uint8_t *)&rec, sizeof(rec));
        count++;
      }
    }
    if (oldAdvBlobs) oldAdvBlobs.close();
    if (newAdvBlobs) newAdvBlobs.close();
    _fs->remove("/adv_blobs");
    }
  }
  if (!_fsExtra->exists("/contacts3")) {
    if (_fs->exists("/contacts3")) {
      File oldFile = openRead(_fs, "/contacts3");
      File newFile = ::openWrite(_fsExtra, "/contacts3");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/contacts3");
    }
  }
  if (!_fsExtra->exists("/channels2")) {
    if (_fs->exists("/channels2")) {
      File oldFile = openRead(_fs, "/channels2");
      File newFile = ::openWrite(_fsExtra, "/channels2");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fs->remove("/channels2");
    }
  }
  // cleanup nodes which have been testing the extra fs, copy _main.id and new_prefs back to primary
  if (_fsExtra->exists("/_main.id")) {
      if (_fs->exists("/_main.id")) {_fs->remove("/_main.id");}
      File oldFile = openRead(_fsExtra, "/_main.id");
      File newFile = ::openWrite(_fs, "/_main.id");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    if (_fs->exists("/new_prefs")) {_fs->remove("/new_prefs");}
      File oldFile = openRead(_fsExtra, "/new_prefs");
      File newFile = ::openWrite(_fs, "/new_prefs");

      if (oldFile && newFile) {
        uint8_t buf[64];
        int n;
        while ((n = oldFile.read(buf, sizeof(buf))) > 0) {
          newFile.write(buf, n);
        }
      }
      if (oldFile) oldFile.close();
      if (newFile) newFile.close();
      _fsExtra->remove("/new_prefs");
  }
  // remove files from where they should not be anymore
  if (_fs->exists("/adv_blobs")) {
    _fs->remove("/adv_blobs");
  }
  if (_fs->exists("/contacts3")) {
    _fs->remove("/contacts3");
  }
  if (_fs->exists("/channels2")) {
    _fs->remove("/channels2");
  }
  if (_fsExtra->exists("/_main.id")) {
    _fsExtra->remove("/_main.id");
  }
  if (_fsExtra->exists("/new_prefs")) {
    _fsExtra->remove("/new_prefs");
  }
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  File file = openRead(_getContactsChannelsFS(), "/adv_blobs");
  uint8_t len = 0;  // 0 = not found
  if (file) {
    BlobRec tmp;
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        len = tmp.len;
        memcpy(dest_buf, tmp.data, len);
        break;
      }
    }
    file.close();
  }
  return len;
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  if (len < PUB_KEY_SIZE+4+SIGNATURE_SIZE || len > MAX_ADVERT_PKT_LEN) return false;
  checkAdvBlobFile();
  File file = _getContactsChannelsFS()->open("/adv_blobs", FILE_O_WRITE);
  if (file) {
    uint32_t pos = 0, found_pos = 0;
    uint32_t min_timestamp = 0xFFFFFFFF;

    // search for matching key OR evict by oldest timestmap
    BlobRec tmp;
    file.seek(0);
    while (file.read((uint8_t *) &tmp, sizeof(tmp)) == sizeof(tmp)) {
      if (memcmp(key, tmp.key, sizeof(tmp.key)) == 0) {  // only match by 7 byte prefix
        found_pos = pos;
        break;
      }
      if (tmp.timestamp < min_timestamp) {
        min_timestamp = tmp.timestamp;
        found_pos = pos;
      }

      pos += sizeof(tmp);
    }

    memcpy(tmp.key, key, sizeof(tmp.key));  // just record 7 byte prefix of key
    memcpy(tmp.data, src_buf, len);
    tmp.len = len;
    tmp.timestamp = _clock->getCurrentTime();

    file.seek(found_pos);
    file.write((uint8_t *) &tmp, sizeof(tmp));

    file.close();
    return true;
  }
  return false; // error
}
bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  return true; // this is just a stub on NRF52/STM32 platforms
}
#else
inline void makeBlobPath(const uint8_t key[], int key_len, char* path, size_t path_size) {
  char fname[18];
  if (key_len > 8) key_len = 8; // just use first 8 bytes (prefix)
  mesh::Utils::toHex(fname, key, key_len);
  sprintf(path, "/bl/%s", fname);
}

uint8_t DataStore::getBlobByKey(const uint8_t key[], int key_len, uint8_t dest_buf[]) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  if (_fs->exists(path)) {
    File f = openRead(_fs, path);
    if (f) {
      int len = f.read(dest_buf, 255); // currently MAX 255 byte blob len supported!!
      f.close();
      return len;
    }
  }
  return 0; // not found
}

bool DataStore::putBlobByKey(const uint8_t key[], int key_len, const uint8_t src_buf[], uint8_t len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  File f = ::openWrite(_fs, path);
  if (f) {
    int n = f.write(src_buf, len);
    f.close();
    if (n == len) return true; // success!

    _fs->remove(path); // blob was only partially written!
  }
  return false; // error
}

bool DataStore::deleteBlobByKey(const uint8_t key[], int key_len) {
  char path[64];
  makeBlobPath(key, key_len, path, sizeof(path));

  _fs->remove(path);
  
  return true; // return true even if file did not exist
}
#endif
