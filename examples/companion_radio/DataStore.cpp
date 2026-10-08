#include <Arduino.h>
#include "DataStore.h"
#include "Features.h"   // FEAT_JOYSTICK_ROTATION_SETTING (else `#if !FEAT_…` is always true)
#include "ui-core/Battery.h"   // validCurve() for the saved battery curve
#include <target.h>     // radio_driver — repeater-profile freq bounds (getFreqBounds)

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

// Atomically swap a fully-written temp file over its final path. LittleFS
// (nRF52/STM32) rename replaces an existing destination atomically, so a crash
// leaves either the old file or the new one intact — never a truncated mix.
// Other Arduino filesystems can't rename onto an existing file, so the
// destination is dropped first (a metadata-only window, vs. the record-by-record
// write window of a direct overwrite). Returns false if the swap fails.
static bool commitTempFile(FILESYSTEM* fs, const char* tmp, const char* final_path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM)
  return fs->rename(tmp, final_path);
#else
  fs->remove(final_path);
  return fs->rename(tmp, final_path);
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
  #include <helpers/esp32/InternalFS.h>
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
  return ESP32_FS.usedBytes() / 1024;
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
  return ESP32_FS.totalBytes() / 1024;
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

bool DataStore::commitFile(const char* tmp_path, const char* final_path) {
  return commitTempFile(_fs, tmp_path, final_path);
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
  bool fs_success = ((ESP32_FS_CLASS *)_fs)->format();
  esp_err_t nvs_err = nvs_flash_erase(); // no need to reinit, will be done by reboot
  return fs_success && (nvs_err == ESP_OK);
#elif defined(SIM_PLATFORM)
  return _fs->format();
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

// ── Prefs ──
// /prefs: a header, the node's position, then NodePrefs as it is in memory
// (see the comment at the top of NodePrefs.h). Only the build that wrote it
// reads it back, so the struct's padding and alignment are no concern.
struct PrefsHeader {
  uint32_t magic;     // PREFS_MAGIC
  uint16_t version;   // NodePrefs::PREFS_VERSION
  uint16_t size;      // sizeof(NodePrefs) when written
  uint32_t crc;       // CRC-32 of the position and the struct's bytes
};
static const uint32_t PREFS_MAGIC = 0x534F4C4F;   // "SOLO"

static uint32_t crc32(uint32_t crc, const uint8_t* p, size_t n) {
  crc = ~crc;
  while (n--) {
    crc ^= *p++;
    for (int k = 0; k < 8; k++) crc = (crc >> 1) ^ (0xEDB88320u & (0u - (crc & 1)));
  }
  return ~crc;
}

// Every value inside its range, whatever the file held: a damaged byte reads
// as the default rather than as a setting no screen can show.
static void sanitize(NodePrefs& p) {
#if !FEAT_JOYSTICK_ROTATION_SETTING
  p.joystick_rotation = 0;   // no setting for it on this build
#endif
  {   // only the duty-cycle presets Settings offers
    static const uint32_t GPS_DUTY_PRESETS[] = { 0, 60, 300, 900, 1800, 3600 };
    bool known = false;
    for (uint32_t v : GPS_DUTY_PRESETS) if (p.gps_interval == v) { known = true; break; }
    if (!known) p.gps_interval = 0;
  }
  auto flag = [](uint8_t& v, uint8_t def) { if (v > 1) v = def; };
  if (p.ringtone_len > 32) p.ringtone_len = 0;
  if (p.ringtone2_len > 32) p.ringtone2_len = 0;
  if (p.notif_melody_dm > 3) p.notif_melody_dm = 0;
  if (p.notif_melody_ch > 3) p.notif_melody_ch = 0;
  if (p.notif_melody_ad > 3) p.notif_melody_ad = 0;
  if (p.page_order_set != NodePrefs::PAGE_ORDER_MAGIC) p.page_order_set = 0;
  for (uint8_t i = 0; i < NodePrefs::PAGE_ORDER_LEN; i++)
    if (p.page_order[i] > NodePrefs::HPB_COUNT) p.page_order[i] = 0;
  flag(p.repeat_skip_adverts, 0);
  if (p.repeat_max_hops > 64) p.repeat_max_hops = 0;
  if (p.repeat_delay_boost > 8) p.repeat_delay_boost = 0;
  if (p.repeat_min_snr != NodePrefs::REPEAT_SNR_DISABLED && (p.repeat_min_snr < -20 || p.repeat_min_snr > 10))
    p.repeat_min_snr = NodePrefs::REPEAT_SNR_DISABLED;   // the UI's -20..10
  flag(p.repeat_suppress_dup, 0);
  flag(p.repeat_scope_only, 0);
  flag(p.repeater_use_profile, 0);
  float lo, hi;
  radio_driver.getFreqBounds(lo, hi);
  if (!isValidRepeaterProfile(p.repeater_freq, p.repeater_bw, p.repeater_sf, p.repeater_cr, lo, hi))
    seedDefaultRepeaterProfile(p);
  flag(p.track_shared_loc, 0);
  flag(p.loc_share_enabled, 0);
  flag(p.loc_share_target_type, 0);
  if (p.loc_share_channel_idx >= MAX_GROUP_CHANNELS) p.loc_share_channel_idx = 0;
  if (p.loc_share_move_idx >= NodePrefs::LOC_SHARE_MOVE_COUNT) p.loc_share_move_idx = 1;
  if (p.loc_share_interval_idx >= NodePrefs::LOC_SHARE_INTERVAL_COUNT) p.loc_share_interval_idx = 1;
  if (p.loc_share_heartbeat_idx >= NodePrefs::LOC_SHARE_HEARTBEAT_COUNT) p.loc_share_heartbeat_idx = 0;
  if (p.loc_share_scope > ScopeList::MAX_SCOPE_ENTRIES + 1) p.loc_share_scope = 0;
  if (p.loc_share_duration_idx >= NodePrefs::LOC_SHARE_DURATION_COUNT) p.loc_share_duration_idx = 0;
  flag(p.locator_enabled, 0);
  flag(p.locator_has_target, 0);
  flag(p.locator_beeper, 0);
  if (p.locator_radius_idx >= NodePrefs::LOCATOR_RADIUS_COUNT) p.locator_radius_idx = 1;
  if (p.locator_mode >= NodePrefs::LOCATOR_MODE_COUNT) p.locator_mode = 0;
  if (p.locator_target_kind > 2) p.locator_target_kind = 0;
  p.locator_label[sizeof(p.locator_label) - 1] = '\0';
  if (p.trail_autopause_idx >= NodePrefs::TRAIL_AUTOPAUSE_COUNT) p.trail_autopause_idx = 0;
  flag(p.trail_autosave_lowbatt, 0);
  if (p.gps_avg_idx >= NodePrefs::GPS_AVG_COUNT) p.gps_avg_idx = 0;
  flag(p.alarm_on, 0);
  if (p.alarm_hour > 23) p.alarm_hour = 0;
  if (p.alarm_min > 59) p.alarm_min = 0;
  if (NodePrefs::alarmRepeatIdxForMask(p.alarm_repeat_mask) == 0) p.alarm_repeat_mask = 0;
  flag(p.keyboard_type, 0);
  if (p.keyboard_alt_alphabet >= NodePrefs::KB_ALPHABET_COUNT) p.keyboard_alt_alphabet = 0;
  if (p.keyboard_main_alphabet >= NodePrefs::KB_ALPHABET_COUNT) p.keyboard_main_alphabet = 0;
  flag(p.keyboard_cardkb_compact, 0);
  flag(p.bot_commands_enabled, 0);
  flag(p.bot_commands_ch, 0);
  flag(p.bot_commands_room, 0);
  flag(p.bot_actions_dm, 0);
  flag(p.bot_actions_ch, 0);
  flag(p.bot_actions_room, 0);
  flag(p.bot_dm_scope, 0);
  flag(p.bot_room_enabled, 0);
  if (p.bot_quiet_start > 23) p.bot_quiet_start = 0;
  if (p.bot_quiet_end > 23) p.bot_quiet_end = 0;
  flag(p.use_lemon_font, 0);
  flag(p.units_imperial, 0);
  flag(p.trail_show_pace, 0);
  if (p.advert_sound_scope > 1) p.advert_sound_scope = ADVERT_SOUND_SCOPE_ALL;
  flag(p.rx_powersave, 0);
  flag(p.tx_apc, 0);
  if (p.dm_resend_count > 5) p.dm_resend_count = 2;
  if (p.gpio1_mode > 4) p.gpio1_mode = 0;   // gpio1/2 can be analog (AIN0/AIN5)
  if (p.gpio2_mode > 4) p.gpio2_mode = 0;
  if (p.gpio3_mode > 3) p.gpio3_mode = 0;
  if (p.gpio4_mode > 3) p.gpio4_mode = 0;
  if (p.interference_threshold > 30) p.interference_threshold = 0;
  flag(p.cad_enabled, 0);
  for (uint8_t i = 0; i < NodePrefs::FAVOURITES_COUNT; i++)
    if (p.favourite_kinds[i] > NodePrefs::FAV_KIND_MAX) p.favourite_kinds[i] = NodePrefs::FAV_KIND_CONTACT;
  flag(p.fav_sort, 1);
  flag(p.msg_wake, 1);
  if (p.contact_expiry_idx >= NodePrefs::CONTACT_EXPIRY_COUNT) p.contact_expiry_idx = 0;
  if (p.display_brightness_pct > 100) p.display_brightness_pct = 0;
  flag(p.quiet_hours, 0);
  if (p.quiet_from > 23) p.quiet_from = 22;
  if (p.quiet_to > 23) p.quiet_to = 7;
  if (!battery::validCurve(p.batt_curve_mv)) memset(p.batt_curve_mv, 0, sizeof(p.batt_curve_mv));
  flag(p.lock_compact, 0);
  flag(p.ble_off, 0);
  if (p.msg_alert > NodePrefs::MSG_ALERT_OFF) p.msg_alert = NodePrefs::MSG_ALERT_NORMAL;
  if (!p.ch_scope_follow) {
    for (uint8_t i = 0; i < NodePrefs::MAX_SCOPED_CHANNELS; i++)
      if (p.ch_scope_idx[i] == 0) p.ch_scope_idx[i] = NodePrefs::CH_SCOPE_DEFAULT;
    p.ch_scope_follow = 1;
  }
}

// The settings a firmware before 2.0 kept in /new_prefs that still matter
// after the update: its first block, laid out the same in upstream MeshCore
// and in every Solo -- the node's name and position, its radio, the BLE PIN
// and the default scope. Without them a device would come back on the
// default radio, deaf to its mesh. The rest starts from the defaults.
static void importOldPrefs(File& f, NodePrefs& p, double& lat, double& lon) {
  uint8_t pad[4];
  f.read((uint8_t*)&p.airtime_factor, sizeof(p.airtime_factor));
  f.read((uint8_t*)p.node_name, sizeof(p.node_name));
  f.read(pad, 4);
  f.read((uint8_t*)&lat, sizeof(lat));
  f.read((uint8_t*)&lon, sizeof(lon));
  f.read((uint8_t*)&p.freq, sizeof(p.freq));
  f.read((uint8_t*)&p.sf, sizeof(p.sf));
  f.read((uint8_t*)&p.cr, sizeof(p.cr));
  f.read((uint8_t*)&p.client_repeat, sizeof(p.client_repeat));
  f.read((uint8_t*)&p.manual_add_contacts, sizeof(p.manual_add_contacts));
  f.read((uint8_t*)&p.bw, sizeof(p.bw));
  f.read((uint8_t*)&p.tx_power_dbm, sizeof(p.tx_power_dbm));
  f.read((uint8_t*)&p.telemetry_mode_base, sizeof(p.telemetry_mode_base));
  f.read((uint8_t*)&p.telemetry_mode_loc, sizeof(p.telemetry_mode_loc));
  f.read((uint8_t*)&p.telemetry_mode_env, sizeof(p.telemetry_mode_env));
  f.read((uint8_t*)&p.rx_delay_base, sizeof(p.rx_delay_base));
  f.read((uint8_t*)&p.advert_loc_policy, sizeof(p.advert_loc_policy));
  f.read((uint8_t*)&p.multi_acks, sizeof(p.multi_acks));
  f.read((uint8_t*)&p.path_hash_mode, sizeof(p.path_hash_mode));
  f.read(pad, 1);
  f.read((uint8_t*)&p.ble_pin, sizeof(p.ble_pin));
  f.read((uint8_t*)&p.buzzer_quiet, sizeof(p.buzzer_quiet));
  f.read((uint8_t*)&p.gps_enabled, sizeof(p.gps_enabled));
  f.read((uint8_t*)&p.gps_interval, sizeof(p.gps_interval));
  f.read((uint8_t*)&p.autoadd_config, sizeof(p.autoadd_config));
  f.read((uint8_t*)&p.autoadd_max_hops, sizeof(p.autoadd_max_hops));
  f.read((uint8_t*)&p.rx_boosted_gain, sizeof(p.rx_boosted_gain));
  f.read((uint8_t*)p.default_scope_name, sizeof(p.default_scope_name));
  f.read((uint8_t*)p.default_scope_key, sizeof(p.default_scope_key));
  p.node_name[sizeof(p.node_name) - 1] = '\0';
  p.default_scope_name[sizeof(p.default_scope_name) - 1] = '\0';
  if (p.client_repeat > 1) p.client_repeat = 0;
  if (p.buzzer_quiet > 1) p.buzzer_quiet = 0;
}

// A device that ran Meshtastic before (the L1 ships with it) can still have
// its settings folder at /prefs: our file can't be read from a folder, and the
// rename that commits every save can't replace one, so no setting survived a
// restart. The folder is of no use here: it goes.
#if defined(ESP32)
static void removeTree(FILESYSTEM* fs, const char* path) {
  File dir = fs->open(path, "r", false);
  if (!dir) return;
  File f = dir.openNextFile();
  while (f) {
    char sub[64];
    snprintf(sub, sizeof(sub), "%s", f.path());
    const bool is_dir = f.isDirectory();
    f.close();
    if (is_dir) removeTree(fs, sub); else fs->remove(sub);
    f = dir.openNextFile();
  }
  dir.close();
  fs->rmdir(path);
}
#endif
static void removeStrayDir(FILESYSTEM* fs, const char* path) {
#if defined(NRF52_PLATFORM) || defined(STM32_PLATFORM) || defined(ESP32)
#if defined(ESP32)
  File f = fs->open(path, "r", false);
#else
  File f = fs->open(path, FILE_O_READ);
#endif
  const bool is_dir = f && f.isDirectory();
  if (f) f.close();
  if (!is_dir) return;
#if defined(ESP32)
  removeTree(fs, path);
#else
  fs->rmdir_r(path);
#endif
#endif
}

// `prefs` comes in holding the defaults; what the file holds replaces them.
void DataStore::loadPrefs(NodePrefs& prefs, double& node_lat, double& node_lon) {
  removeStrayDir(_fs, "/prefs");
  File file = openRead(_fs, "/prefs");
  if (file) {
    PrefsHeader h;
    NodePrefs* tmp = new NodePrefs(prefs);   // a field the file is too short for keeps its default
    double lat = 0, lon = 0;
    bool ok = file.read((uint8_t*)&h, sizeof(h)) == sizeof(h)
           && h.magic == PREFS_MAGIC && h.version == NodePrefs::PREFS_VERSION
           && file.read((uint8_t*)&lat, sizeof(lat)) == sizeof(lat)
           && file.read((uint8_t*)&lon, sizeof(lon)) == sizeof(lon);
    if (ok) {
      const size_t n = h.size < sizeof(NodePrefs) ? h.size : sizeof(NodePrefs);
      ok = file.read((uint8_t*)tmp, n) == (int)n;
      uint32_t crc = crc32(0, (const uint8_t*)&lat, sizeof(lat));
      crc = crc32(crc, (const uint8_t*)&lon, sizeof(lon));
      ok = ok && crc32(crc, (const uint8_t*)tmp, n) == h.crc;
    }
    file.close();
    if (ok) { prefs = *tmp; node_lat = lat; node_lon = lon; }
    else MESH_DEBUG_PRINTLN("prefs: /prefs unreadable (another version or damaged), defaults kept");
    delete tmp;
  } else {   // first boot after an update from before 2.0 (/node_prefs: older still, the same layout)
    for (const char* old : { "/new_prefs", "/node_prefs" }) {
      if (!_fs->exists(old)) continue;
      if ((file = openRead(_fs, old))) {
        importOldPrefs(file, prefs, node_lat, node_lon);
        file.close();
        savePrefs(prefs, node_lat, node_lon);
      }
      _fs->remove(old);
      break;
    }
  }
  sanitize(prefs);
}

void DataStore::savePrefs(const NodePrefs& prefs, double node_lat, double node_lon) {
  // Atomic temp-then-rename (see commitTempFile): an interrupted save leaves
  // the previous /prefs in place.
  File file = ::openWrite(_fs, "/prefs.tmp");
  if (!file) return;
  PrefsHeader h = { PREFS_MAGIC, NodePrefs::PREFS_VERSION, (uint16_t)sizeof(NodePrefs), 0 };
  h.crc = crc32(0, (const uint8_t*)&node_lat, sizeof(node_lat));
  h.crc = crc32(h.crc, (const uint8_t*)&node_lon, sizeof(node_lon));
  h.crc = crc32(h.crc, (const uint8_t*)&prefs, sizeof(NodePrefs));
  // Once the flash fills, writes come back short: only a whole record replaces the old one.
  bool ok = file.write((const uint8_t*)&h, sizeof(h)) == sizeof(h)
         && file.write((const uint8_t*)&node_lat, sizeof(node_lat)) == sizeof(node_lat)
         && file.write((const uint8_t*)&node_lon, sizeof(node_lon)) == sizeof(node_lon)
         && file.write((const uint8_t*)&prefs, sizeof(NodePrefs)) == sizeof(NodePrefs);
  file.close();
  if (ok) commitTempFile(_fs, "/prefs.tmp", "/prefs");
  else _fs->remove("/prefs.tmp");
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
#ifndef SIM_PLATFORM
    // Real hardware has no other way to know the time before a GPS fix or
    // a phone/CLI sync, so restoring the last-known saved time is the
    // right call there. The sim's RTCClock (SimRTCClock.h) is already
    // backed by the real host wall clock (time(NULL)) from the moment it's
    // constructed -- overwriting that with a stale save from a previous
    // visit (persisted via IDBFS, see the site's "returning visitor" note)
    // would make a returning instance's on-screen clock drift away from
    // the visitor's own real time instead of just showing it.
    if (t > 1000000000UL) _clock->setCurrentTime(t);
#endif
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
  FILESYSTEM* fs = _getContactsChannelsFS();
  // Write to a temp file, then atomically rename it over /contacts3 only once
  // every record has written cleanly. The old code truncated /contacts3 up
  // front and wrote in place, so a crash, reset or full flash mid-save wiped
  // the entire contact list. Now an interrupted save leaves the previous good
  // file untouched.
  File file = ::openWrite(fs, "/contacts3.tmp");
  if (!file) return;

  bool ok = true;
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

    if (!success) { ok = false; break; } // write failed (e.g. flash full)

    idx++;  // advance to next contact
  }
  file.close();

  if (ok) {
    commitTempFile(fs, "/contacts3.tmp", "/contacts3");
  } else {
    fs->remove("/contacts3.tmp");   // keep the previous good /contacts3
  }
}

bool DataStore::loadChannels(DataStoreHost* host) {
    FILESYSTEM* fs = _getContactsChannelsFS();
    File file = openRead(fs, "/channels3");
    if (file) {
      // /channels3: the leading 4-byte field's first byte is the channel's
      // original slot index (see saveChannels()) — load it back into that
      // exact slot. The old /channels2 format instead reassigned indices
      // 0,1,2… sequentially on every load, which silently shifted every
      // later channel down a slot once an earlier one was removed — anything
      // that remembers a channel by index (Live Share's target, the bot's
      // channel, per-channel melody) would then point at the wrong channel
      // after the next reboot.
      bool full = false;
      uint8_t skipped = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t hdr[4];

        bool success = (file.read(hdr, 4) == 4);
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

        if (!host->onChannelLoaded(hdr[0], ch)) full = true;
      }
      file.close();
      if (skipped > 0) {
        MESH_DEBUG_PRINTLN("loadChannels: skipped %u corrupted/empty channel entr%s",
                           (unsigned)skipped, skipped == 1 ? "y" : "ies");
      }
      return true;
    }

    // One-time migration from the old /channels2 format (sequential index,
    // reassigned on every load — the bug /channels3 above replaces). Loads
    // with that old semantics once, then resaves as /channels3 so this
    // fallback is never hit again on this device.
    file = openRead(fs, "/channels2");
    if (file) {
      bool full = false;
      uint8_t channel_idx = 0;
      while (!full) {
        ChannelDetails ch;
        uint8_t unused[4];

        bool success = (file.read(unused, 4) == 4);
        success = success && (file.read((uint8_t *)ch.name, 32) == 32);
        success = success && (file.read((uint8_t *)ch.channel.secret, 32) == 32);
        if (!success) break; // EOF

        bool secret_empty = true;
        for (int b = 0; b < 32; b++) if (ch.channel.secret[b] != 0) { secret_empty = false; break; }
        if (secret_empty) continue;
        ch.name[31] = '\0';

        if (host->onChannelLoaded(channel_idx, ch)) channel_idx++;
        else full = true;
      }
      file.close();
      saveChannels(host);   // write /channels3 so the migration runs only once
      return true;
    }

    return false;   // neither file exists -- genuinely fresh device
}

void DataStore::saveChannels(DataStoreHost* host) {
  FILESYSTEM* fs = _getContactsChannelsFS();
  // Same atomic temp-then-rename pattern as saveContacts() — never truncate the
  // live /channels3 before the new copy is fully written.
  File file = ::openWrite(fs, "/channels3.tmp");
  if (!file) return;

  bool ok = true;
  uint8_t channel_idx = 0;
  ChannelDetails ch;

  while (host->getChannelForSave(channel_idx, ch)) {
    uint8_t idx = channel_idx++;
    // getChannelForSave() returns every slot up to MAX_GROUP_CHANNELS, so skip
    // the unused ones (all-zero secret) rather than writing all 40 — otherwise
    // the file is always ~2.7 KB and wears the flash needlessly. Unlike the old
    // /channels2 format, loadChannels() no longer compacts: the slot index
    // travels with the record (hdr[0] below) so a removed channel just leaves
    // a hole instead of shifting every later index down a slot.
    bool empty = true;
    for (int b = 0; b < 32; b++) if (ch.channel.secret[b]) { empty = false; break; }
    if (empty) continue;

    uint8_t hdr[4] = { idx, 0, 0, 0 };
    bool success = (file.write(hdr, 4) == 4);
    success = success && (file.write((uint8_t *)ch.name, 32) == 32);
    success = success && (file.write((uint8_t *)ch.channel.secret, 32) == 32);
    if (!success) { ok = false; break; } // write failed
  }
  file.close();

  if (ok) {
    commitTempFile(fs, "/channels3.tmp", "/channels3");
  } else {
    fs->remove("/channels3.tmp");   // keep the previous good /channels3
  }
}

bool DataStore::loadScopeList(ScopeList& list, const NodePrefs& prefs) {
  File file = openRead("/scopes1");
  if (file) {
    uint8_t hdr[2] = { 0, 0 };   // default_idx is read back below even if the header read fails
    bool success = (file.read(hdr, 2) == 2);
    uint8_t count = success ? hdr[1] : 0;
    if (count > ScopeList::MAX_SCOPE_ENTRIES) count = 0;   // corrupt header -- start empty rather than overrun entries[]

    uint8_t loaded = 0;
    for (uint8_t i = 0; i < count; i++) {
      ScopeEntry e;
      bool ok = (file.read((uint8_t *)e.name, sizeof(e.name)) == sizeof(e.name));
      ok = ok && (file.read(e.key, sizeof(e.key)) == sizeof(e.key));
      if (!ok) break;   // truncated file -- keep whatever loaded fine so far
      e.name[sizeof(e.name) - 1] = '\0';
      list.entries[loaded++] = e;
    }
    file.close();
    list.count = loaded;
    list.default_idx = list.clamp(hdr[0]);
    return false;   // the file was already there -- nothing migrated this boot
  }

  // No /scopes1 yet -- one-time migration of an existing single
  // default_scope_name/key (Settings > Radio > Scope, pre-list) into list
  // entry 1 and mark it default, which covers DMs and the relay filter. The
  // caller finishes the job for channels by seeding their per-channel picks
  // once channels[] is loaded (see this function's return value). A
  // never-configured device just stays at the default-constructed ScopeList
  // (empty, default_idx 0 == "*").
  list.count = 0;
  list.default_idx = 0;
  bool migrated = (prefs.default_scope_name[0] != '\0');
  if (migrated) {
    ScopeEntry& e = list.entries[0];
    StrHelper::strncpy(e.name, prefs.default_scope_name, sizeof(e.name));
    memcpy(e.key, prefs.default_scope_key, sizeof(e.key));   // already-derived key, no need to re-derive
    list.count = 1;
    list.default_idx = 1;
  }
  saveScopeList(list);   // write /scopes1 so this migration runs only once
  return migrated;       // caller seeds the existing channels with entry 1
}

void DataStore::saveScopeList(const ScopeList& list) {
  File file = ::openWrite(_fs, "/scopes1.tmp");
  if (!file) return;

  uint8_t hdr[2] = { list.default_idx, list.count };
  bool ok = (file.write(hdr, 2) == 2);
  for (uint8_t i = 0; ok && i < list.count; i++) {
    ok = (file.write((uint8_t *)list.entries[i].name, sizeof(list.entries[i].name)) == sizeof(list.entries[i].name));
    ok = ok && (file.write(list.entries[i].key, sizeof(list.entries[i].key)) == sizeof(list.entries[i].key));
  }
  file.close();

  if (ok) {
    commitTempFile(_fs, "/scopes1.tmp", "/scopes1");
  } else {
    _fs->remove("/scopes1.tmp");   // keep the previous good /scopes1
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

    // search for matching key OR evict by oldest timestamp
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
