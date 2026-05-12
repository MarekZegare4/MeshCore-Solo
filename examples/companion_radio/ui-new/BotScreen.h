#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after the global KB_* constants are defined.

class BotScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int ITEM_COUNT = 4;
  static const int ITEM_H     = 11;
  static const int START_Y    = 12;
  static const int VAL_X      = 70;

  // keyboard state (reused for trigger and reply fields)
  int  _kb_field;   // -1=off, 2=trigger, 3=reply
  char _kb_buf[KB_MAX_LEN + 1];
  int  _kb_len;
  int  _kb_maxlen;
  int  _kb_row, _kb_col;
  bool _kb_caps;
  bool _kb_ph_mode;
  int  _kb_ph_sel;

  int  _sel;

  // channel + DM contact caches (refreshed on enter)
  static const int MAX_BOT_DM = 16;

  int     _num_channels;
  uint8_t _channel_indices[MAX_GROUP_CHANNELS];

  int     _num_dm;
  uint8_t _dm_pubkeys[MAX_BOT_DM][4];
  char    _dm_names[MAX_BOT_DM][32];

  void refreshChannels() {
    _num_channels = 0;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0')
        _channel_indices[_num_channels++] = (uint8_t)i;
    }
  }

  void refreshContacts() {
    _num_dm = 0;
    ContactInfo ci;
    int n = the_mesh.getNumContacts();
    for (int i = 0; i < n && _num_dm < MAX_BOT_DM; i++) {
      if (!the_mesh.getContactByIdx(i, ci)) continue;
      if (ci.type != ADV_TYPE_CHAT) continue;
      if (!(ci.flags & 0x01)) continue;  // favourites only
      memcpy(_dm_pubkeys[_num_dm], ci.id.pub_key, 4);
      strncpy(_dm_names[_num_dm], ci.name, sizeof(_dm_names[0]) - 1);
      _dm_names[_num_dm][sizeof(_dm_names[0]) - 1] = '\0';
      _num_dm++;
    }
  }

  // returns combined index in [channels..., DM contacts...]
  int currentTargetIdx() const {
    if (_prefs->bot_target_type == 0) {
      for (int i = 0; i < _num_channels; i++)
        if (_channel_indices[i] == _prefs->bot_channel_idx) return i;
      return 0;
    } else {
      for (int i = 0; i < _num_dm; i++)
        if (memcmp(_dm_pubkeys[i], _prefs->bot_dm_pubkey, 4) == 0)
          return _num_channels + i;
      return _num_channels;  // fallback: first DM
    }
  }

public:
  BotScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() {
    _sel      = 0;
    _kb_field = -1;
    refreshChannels();
    refreshContacts();
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_kb_field >= 0) {
      // keyboard mode — uses same layout as global keyboard
      const char* label = (_kb_field == 2) ? "Trigger:" : "Reply:";
      const char* disp_start = _kb_buf;
      int disp_len = _kb_len;
      if (disp_len > 20) { disp_start = _kb_buf + (disp_len - 20); disp_len = 20; }
      char preview[28];
      snprintf(preview, sizeof(preview), "%s%.*s_", label, disp_len, disp_start);
      display.setCursor(0, KB_TEXT_Y);
      display.print(preview);
      display.fillRect(0, KB_SEP_Y, display.width(), 1);

      // char rows
      for (int row = 0; row < KB_ROWS_CHAR; row++) {
        int y = KB_CHARS_Y + row * KB_CELL_H;
        for (int col = 0; col < KB_COLS_CHAR; col++) {
          bool sel = (_kb_row == row && _kb_col == col);
          char ch = KB_CHARS[row][col];
          if (_kb_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
          char ch_buf[2] = { ch == ' ' ? '_' : ch, '\0' };
          int cx = col * KB_CELL_W;
          if (sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(cx, y - 1, KB_CELL_W - 1, KB_CELL_H);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(cx + 3, y);
          display.print(ch_buf);
        }
      }

      // special row: [^] [Sp] [Del] [{}] [OK] — 5 buttons at 25px each
      const char* spec[] = { "[^]", "[Sp]", "[Del]", "[{}]", "[OK]" };
      for (int i = 0; i < KB_SPECIAL; i++) {
        bool sel = (_kb_row == KB_ROWS_CHAR && _kb_col == i);
        bool active = (i == 0 && _kb_caps);
        int sx = i * 25;
        if (sel || active) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(sx, KB_SPECIAL_Y - 1, 24, KB_CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(sx + 1, KB_SPECIAL_Y);
        display.print(spec[i]);
        display.setColor(DisplayDriver::LIGHT);
      }

      // placeholder picker overlay
      if (_kb_ph_mode) {
        display.setColor(DisplayDriver::DARK);
        display.fillRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setColor(DisplayDriver::LIGHT);
        display.drawRect(20, 20, 88, 12 + KB_PH_COUNT * 10);
        display.setCursor(24, 21);
        display.print("Placeholder:");
        display.fillRect(20, 30, 88, 1);
        for (int i = 0; i < KB_PH_COUNT; i++) {
          int py = 33 + i * 10;
          if (i == _kb_ph_sel) {
            display.setColor(DisplayDriver::LIGHT);
            display.fillRect(21, py - 1, 86, 10);
            display.setColor(DisplayDriver::DARK);
          } else {
            display.setColor(DisplayDriver::LIGHT);
          }
          display.setCursor(24, py);
          display.print(KB_PH_LIST[i]);
        }
        display.setColor(DisplayDriver::LIGHT);
      }
      return 50;
    }

    display.drawTextCentered(display.width() / 2, 0, "AUTO-REPLY BOT");
    display.fillRect(0, 10, display.width(), 1);

    static const char* labels[] = { "Enable", NULL, "Trigger", "Reply" };
    for (int i = 0; i < ITEM_COUNT; i++) {
      int y = START_Y + i * ITEM_H;
      bool sel = (i == _sel);
      if (sel) {
        display.fillRect(0, y - 1, display.width(), ITEM_H);
        display.setColor(DisplayDriver::DARK);
      }
      display.setCursor(2, y);
      if (i == 1)
        display.print(_prefs->bot_target_type == 0 ? "Channel" : "Contact");
      else
        display.print(labels[i]);
      display.setCursor(VAL_X, y);

      if (i == 0) {
        display.print(_prefs->bot_enabled ? "ON" : "OFF");
      } else if (i == 1) {
        int total = _num_channels + _num_dm;
        if (total == 0) {
          display.print("none");
        } else if (_prefs->bot_target_type == 0) {
          ChannelDetails ch;
          if (_num_channels > 0 && the_mesh.getChannel(_prefs->bot_channel_idx, ch) && ch.name[0])
            display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, ch.name);
          else
            display.print("?");
        } else {
          bool found = false;
          for (int j = 0; j < _num_dm && !found; j++) {
            if (memcmp(_dm_pubkeys[j], _prefs->bot_dm_pubkey, 4) == 0) {
              display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, _dm_names[j]);
              found = true;
            }
          }
          if (!found) display.print("?");
        }
      } else if (i == 2) {
        const char* tr = _prefs->bot_trigger;
        display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, tr[0] ? tr : "(none)");
      } else {
        const char* rp = _prefs->bot_reply;
        display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, rp[0] ? rp : "(none)");
      }
      display.setColor(DisplayDriver::LIGHT);
    }

    display.setCursor(0, 54);
    display.print("ENT:edit  CANCEL:save");
    return 300;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool enter = (c == KEY_ENTER);
    bool cancel = (c == KEY_CANCEL || c == KEY_CONTEXT_MENU);

    if (_kb_field >= 0) {
      // placeholder picker overlay takes priority
      if (_kb_ph_mode) {
        if (c == KEY_UP   && _kb_ph_sel > 0) { _kb_ph_sel--; return true; }
        if (c == KEY_DOWN && _kb_ph_sel < KB_PH_COUNT - 1) { _kb_ph_sel++; return true; }
        if (c == KEY_ENTER) {
          const char* ph = KB_PH_LIST[_kb_ph_sel];
          int ph_len = strlen(ph);
          if (_kb_len + ph_len <= _kb_maxlen) {
            memcpy(_kb_buf + _kb_len, ph, ph_len);
            _kb_len += ph_len;
            _kb_buf[_kb_len] = '\0';
          }
          _kb_ph_mode = false;
          return true;
        }
        if (cancel) { _kb_ph_mode = false; return true; }
        return true;
      }

      if (cancel) { _kb_field = -1; return true; }
      if (up) {
        if (_kb_row > 0) {
          _kb_row--;
          if (_kb_row == KB_ROWS_CHAR - 1)  // leaving special row upward
            _kb_col = _kb_col * KB_COLS_CHAR / KB_SPECIAL;
        }
        return true;
      }
      if (down) {
        if (_kb_row < KB_ROWS_CHAR) {
          _kb_row++;
          if (_kb_row == KB_ROWS_CHAR)  // entering special row
            _kb_col = _kb_col * KB_SPECIAL / KB_COLS_CHAR;
        }
        return true;
      }
      if (left) {
        if (_kb_col > 0) _kb_col--;
        return true;
      }
      if (right) {
        int max_col = (_kb_row == KB_ROWS_CHAR) ? KB_SPECIAL - 1 : KB_COLS_CHAR - 1;
        if (_kb_col < max_col) _kb_col++;
        return true;
      }
      if (enter) {
        if (_kb_row < KB_ROWS_CHAR) {
          if (_kb_len < _kb_maxlen) {
            char ch = KB_CHARS[_kb_row][_kb_col];
            if (_kb_caps && ch >= 'a' && ch <= 'z') ch = ch - 'a' + 'A';
            _kb_buf[_kb_len++] = ch;
            _kb_buf[_kb_len]   = '\0';
          }
        } else {
          switch (_kb_col) {
            case 0: _kb_caps = !_kb_caps; break;
            case 1:
              if (_kb_len < _kb_maxlen) { _kb_buf[_kb_len++] = ' '; _kb_buf[_kb_len] = '\0'; }
              break;
            case 2:
              if (_kb_len > 0) _kb_buf[--_kb_len] = '\0';
              break;
            case 3:
              _kb_ph_mode = true;
              _kb_ph_sel  = 0;
              break;
            case 4:
              // OK — commit to prefs
              if (_kb_field == 2) {
                strncpy(_prefs->bot_trigger, _kb_buf, sizeof(_prefs->bot_trigger) - 1);
                _prefs->bot_trigger[sizeof(_prefs->bot_trigger) - 1] = '\0';
              } else {
                strncpy(_prefs->bot_reply, _kb_buf, sizeof(_prefs->bot_reply) - 1);
                _prefs->bot_reply[sizeof(_prefs->bot_reply) - 1] = '\0';
              }
              _kb_field = -1;
              break;
          }
        }
        return true;
      }
      return true;
    }

    if (cancel) {
      the_mesh.savePrefs();
      _task->gotoToolsScreen();
      return true;
    }
    if (up   && _sel > 0) { _sel--; return true; }
    if (down && _sel < ITEM_COUNT - 1) { _sel++; return true; }

    if (_sel == 0 && (enter || left || right)) {
      _prefs->bot_enabled ^= 1;
      return true;
    }
    if (_sel == 1) {
      int total = _num_channels + _num_dm;
      if (total == 0) return false;
      int idx = currentTargetIdx();
      if (right || enter) idx = (idx + 1) % total;
      if (left)           idx = (idx + total - 1) % total;
      if (left || right || enter) {
        if (idx < _num_channels) {
          _prefs->bot_target_type = 0;
          _prefs->bot_channel_idx = _channel_indices[idx];
        } else {
          int di = idx - _num_channels;
          _prefs->bot_target_type = 1;
          memcpy(_prefs->bot_dm_pubkey, _dm_pubkeys[di], 4);
        }
        return true;
      }
    }
    if ((_sel == 2 || _sel == 3) && enter) {
      _kb_field   = _sel;
      _kb_row     = 0;
      _kb_col     = 0;
      _kb_caps    = false;
      _kb_ph_mode = false;
      _kb_ph_sel  = 0;
      if (_sel == 2) {
        strncpy(_kb_buf, _prefs->bot_trigger, sizeof(_kb_buf) - 1);
        _kb_maxlen = sizeof(_prefs->bot_trigger) - 1;
      } else {
        strncpy(_kb_buf, _prefs->bot_reply, sizeof(_kb_buf) - 1);
        _kb_maxlen = sizeof(_prefs->bot_reply) - 1;
      }
      _kb_buf[sizeof(_kb_buf) - 1] = '\0';
      _kb_len = strlen(_kb_buf);
      return true;
    }
    return false;
  }
};
