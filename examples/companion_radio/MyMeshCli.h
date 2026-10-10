#pragma once
// Companion CLI (CMD_RUN_CLI_COMMAND) -- not part of upstream MyMesh.cpp.
// Included at the bottom of MyMesh.cpp after all class definitions.
//
// The commands upstream's companion CLI has (meshcore-dev MyMesh::handleCommand
// and CommonRadioPrefs::handleCommand), with the same names and replies, so
// the phone app's console works as it does on upstream firmware. What isn't
// one of those goes to the listener: the frontend's settings keys
// (ui-core/SettingsCli.h), the same rows its Settings screen shows.
// No factory reset or key export here: those stay on the device.

static bool cliOnOff(const char* v) { return strncmp(v, "on", 2) == 0 || strcmp(v, "1") == 0; }

bool MyMesh::handleCliCommand(const char* command, char* reply, int n) {
  while (*command == ' ') command++;
  if (strlen(command) > 4 && command[2] == '|') {   // optional "xx|" prefix, reflected back
    memcpy(reply, command, 3);
    reply += 3;
    n -= 3;
    *reply = 0;
    command += 3;
  }
  if (cliRadioCommand(command, reply, n)) return true;

  if (strcmp(command, "reboot") == 0 || strcmp(command, "poweroff") == 0 || strcmp(command, "shutdown") == 0) {
    bool restart = command[0] == 'r';
    if (!(_listener && _listener->requestShutdown(restart))) {
      flushDirtyContacts();
      savePrefs();
      if (restart) board.reboot(); else board.powerOff();
    }
    return true;
  }
  if (memcmp(command, "set name ", 9) == 0) {
    if (command[9] && strlen(&command[9]) < sizeof(_prefs.node_name)) {
      StrHelper::strncpy(_prefs.node_name, &command[9], sizeof(_prefs.node_name));
      savePrefs();
      snprintf(reply, n, "OK");
    } else {
      snprintf(reply, n, "Error, bad name");
    }
    return true;
  }
  if (strcmp(command, "get name") == 0) { snprintf(reply, n, "> %s", _prefs.node_name); return true; }
  if (memcmp(command, "set pin ", 8) == 0) {
    char* end;
    uint32_t pin = (uint32_t)strtoul(&command[8], &end, 10);
    if (end == &command[8] || *end || (pin != 0 && (pin < 100000 || pin > 999999))) {
      snprintf(reply, n, "Error, 6 digits or 0");
      return true;
    }
    _prefs.ble_pin = pin;
    savePrefs();
    if (pin) snprintf(reply, n, "> pin is now %06lu", (unsigned long)pin);
    else snprintf(reply, n, "> pin is now random each start");
    return true;
  }
  if (strcmp(command, "get pin") == 0) {
    if (_prefs.ble_pin) snprintf(reply, n, "> %06lu", (unsigned long)_prefs.ble_pin);
    else snprintf(reply, n, "> 0 (random each start)");
    return true;
  }
  if (strcmp(command, "board") == 0) { snprintf(reply, n, "%s", board.getManufacturerName()); return true; }
  if (strcmp(command, "ver") == 0) {
    snprintf(reply, n, "%s (Build: %s)", FIRMWARE_VERSION, FIRMWARE_BUILD_DATE);
    return true;
  }
  if (strcmp(command, "get tz.offset") == 0) { snprintf(reply, n, "> %d", _prefs.tz_offset_hours); return true; }
  if (memcmp(command, "set tz.offset ", 14) == 0) {
    int tz = atoi(&command[14]);
    if (tz < -12 || tz > 14) snprintf(reply, n, "Error, must be from -12 to +14");
    else { _prefs.tz_offset_hours = (int8_t)tz; savePrefs(); snprintf(reply, n, "OK"); }
    return true;
  }
  // The position adverts carry while the GPS is off (Settings > Privacy > Position).
  if (strcmp(command, "get lat") == 0) { snprintf(reply, n, "> %.6f", sensors.node_lat); return true; }
  if (strcmp(command, "get lon") == 0) { snprintf(reply, n, "> %.6f", sensors.node_lon); return true; }
  if (memcmp(command, "set lat ", 8) == 0 || memcmp(command, "set lon ", 8) == 0) {
    char* end;
    double v = strtod(&command[8], &end);
    bool lat = command[6] == 't';
    if (end == &command[8] || *end || (lat ? (v < -90 || v > 90) : (v < -180 || v > 180))) { snprintf(reply, n, "Error, bad coordinate"); return true; }
    (lat ? sensors.node_lat : sensors.node_lon) = v;
    savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }

  return _listener && _listener->onCliCommand(command, reply, n);
}

// CommonRadioPrefs::handleCommand's keys, over this firmware's NodePrefs.
bool MyMesh::cliRadioCommand(const char* command, char* reply, int n) {
  if (strcmp(command, "get radio") == 0) {
    char freq[16], bw[16];
    strcpy(freq, StrHelper::ftoa(_prefs.freq));
    strcpy(bw, StrHelper::ftoa3(_prefs.bw));
    snprintf(reply, n, "> %s,%s,%d,%d", freq, bw, (int)_prefs.sf, (int)_prefs.cr);
    return true;
  }
  if (memcmp(command, "set radio ", 10) == 0) {
    char* p = (char*)&command[10];   // "freq,bw,sf,cr" (strtof: no float scanf on every libc)
    float freq = strtof(p, &p);
    float bw = *p == ',' ? strtof(p + 1, &p) : 0;
    int sf = *p == ',' ? (int)strtol(p + 1, &p, 10) : 0;
    int cr = *p == ',' ? (int)strtol(p + 1, &p, 10) : 0;
    if (freq >= 150.0f && freq <= 2500.0f && sf >= 5 && sf <= 12 && cr >= 5 && cr <= 8 && bw >= 7.0f && bw <= 500.0f) {
      _prefs.freq = freq; _prefs.bw = bw; _prefs.sf = sf; _prefs.cr = cr;
      savePrefs();
      applyRepeaterRadio();   // at once, as CMD_SET_RADIO_PARAMS does
      snprintf(reply, n, "OK");
    } else {
      snprintf(reply, n, "Error, invalid radio params");
    }
    return true;
  }
  if (strcmp(command, "get freq") == 0) { snprintf(reply, n, "> %s", StrHelper::ftoa(_prefs.freq)); return true; }

  if (strcmp(command, "get af") == 0) { snprintf(reply, n, "> %s", StrHelper::ftoa(_prefs.airtime_factor)); return true; }
  if (memcmp(command, "set af ", 7) == 0) {
    char* end;
    float af = strtof(&command[7], &end);
    if (end == &command[7] || af < 0 || af > 9) snprintf(reply, n, "ERROR: af must be 0-9");
    else { _prefs.airtime_factor = af; savePrefs(); snprintf(reply, n, "OK"); }
    return true;
  }
  if (strcmp(command, "get dutycycle") == 0) {
    float dc = 100.0f / (_prefs.airtime_factor + 1.0f);
    snprintf(reply, n, "> %d.%d%%", (int)dc, (int)((dc - (int)dc) * 10.0f + 0.5f));
    return true;
  }
  if (memcmp(command, "set dutycycle ", 14) == 0) {
    float dc = atof(&command[14]);
    if (dc < 1 || dc > 100) { snprintf(reply, n, "ERROR: dutycycle must be 1-100"); return true; }
    _prefs.airtime_factor = 100.0f / dc - 1.0f;
    savePrefs();
    float a = 100.0f / (_prefs.airtime_factor + 1.0f);
    snprintf(reply, n, "OK - %d.%d%%", (int)a, (int)((a - (int)a) * 10.0f + 0.5f));
    return true;
  }
  if (strcmp(command, "get int.thresh") == 0) { snprintf(reply, n, "> %d", (int)_prefs.interference_threshold); return true; }
  if (memcmp(command, "set int.thresh ", 15) == 0) {
    _prefs.interference_threshold = (uint8_t)constrain(atoi(&command[15]), 0, 255);
    savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }
  if (strcmp(command, "get cad") == 0) { snprintf(reply, n, "> %s", _prefs.cad_enabled ? "on" : "off"); return true; }
  if (memcmp(command, "set cad ", 8) == 0) {
    _prefs.cad_enabled = cliOnOff(&command[8]) ? 1 : 0;
    savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }
  if (strcmp(command, "get radio.rxgain") == 0) { snprintf(reply, n, "> %s", _prefs.rx_boosted_gain ? "on" : "off"); return true; }
  if (memcmp(command, "set radio.rxgain ", 17) == 0) {
#if defined(USE_SX1262) || defined(USE_SX1268) || defined(USE_LR2021)
    _prefs.rx_boosted_gain = cliOnOff(&command[17]) ? 1 : 0;
    savePrefs();
    snprintf(reply, n, radio_driver.setRxBoostedGainMode(_prefs.rx_boosted_gain) ? "OK" : "Error: unsupported");
#else
    snprintf(reply, n, "Error: unsupported");
#endif
    return true;
  }
  if (memcmp(command, "get tx", 6) == 0 && (command[6] == 0 || command[6] == ' ')) {
    snprintf(reply, n, "> %d", (int)_prefs.tx_power_dbm);
    return true;
  }
  if (memcmp(command, "set tx ", 7) == 0) {
    int p = atoi(&command[7]);
    if (p < -9 || p > MAX_LORA_TX_POWER) { snprintf(reply, n, "Error, must be -9 to %d", MAX_LORA_TX_POWER); return true; }
    radio_driver.setTxPower(p);
    _prefs.tx_power_dbm = radio_driver.getTxPower();   // what the radio applied (see CMD_SET_RADIO_TX_POWER)
    savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }
  if (strcmp(command, "get rxdelay") == 0) { snprintf(reply, n, "> %s", StrHelper::ftoa(_prefs.rx_delay_base)); return true; }
  if (memcmp(command, "set rxdelay ", 12) == 0) {
    float d = atof(&command[12]);
    if (d < 0 || d > 20.0f) snprintf(reply, n, "Error, must be 0-20");
    else { _prefs.rx_delay_base = d; savePrefs(); snprintf(reply, n, "OK"); }
    return true;
  }
  if (strcmp(command, "get path.hash.mode") == 0) { snprintf(reply, n, "> %d", (int)_prefs.path_hash_mode); return true; }
  if (memcmp(command, "set path.hash.mode ", 19) == 0) {
    int m = atoi(&command[19]);
    if (m < 0 || m > 2) snprintf(reply, n, "Error, must be 0,1, or 2");
    else { _prefs.path_hash_mode = (uint8_t)m; savePrefs(); snprintf(reply, n, "OK"); }
    return true;
  }
  if (strcmp(command, "get multi.acks") == 0) { snprintf(reply, n, "> %d", (int)_prefs.multi_acks); return true; }
  if (memcmp(command, "set multi.acks ", 15) == 0) {
    _prefs.multi_acks = (uint8_t)constrain(atoi(&command[15]), 0, 1);
    savePrefs();
    snprintf(reply, n, "OK");
    return true;
  }
  return false;
}
