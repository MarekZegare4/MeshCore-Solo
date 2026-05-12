#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after the global KB_* constants are defined.

class BotScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  // Items: 0=Enable(DM), 1=Channel, 2=Trigger, 3=Reply DM, 4=Reply Ch
  static const int ITEM_COUNT = 5;
  static const int ITEM_H     = 11;
  static const int START_Y    = 12;
  static const int VAL_X      = 70;

  int  _sel;

  // keyboard state (reused for trigger and reply fields)
  int  _kb_field;   // -1=off, 2=trigger, 3=reply
  char _kb_buf[KB_MAX_LEN + 1];
  int  _kb_len;
  int  _kb_maxlen;
  int  _kb_row, _kb_col;
  bool _kb_caps;
  bool _kb_ph_mode;
  int  _kb_ph_sel;

  // channel cache (refreshed on enter)
  int     _num_channels;
  uint8_t _channel_indices[MAX_GROUP_CHANNELS];

  void refreshChannels() {
    _num_channels = 0;
    ChannelDetails ch;
    for (int i = 0; i < MAX_GROUP_CHANNELS; i++) {
      if (the_mesh.getChannel(i, ch) && ch.name[0] != '\0')
        _channel_indices[_num_channels++] = (uint8_t)i;
    }
  }

  // Returns index into _channel_indices[] for current bot_channel_idx, or -1 if not found/disabled.
  int currentChannelListIdx() const {
    if (!_prefs->bot_channel_enabled) return -1;
    for (int i = 0; i < _num_channels; i++)
      if (_channel_indices[i] == _prefs->bot_channel_idx) return i;
    return -1;
  }

public:
  BotScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() {
    _sel      = 0;
    _kb_field = -1;
    refreshChannels();
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_kb_field >= 0) {
      const char* label = (_kb_field == 2) ? "Trigger:" : "Reply:";
      const char* disp_start = _kb_buf;
      int disp_len = _kb_len;
      if (disp_len > 20) { disp_start = _kb_buf + (disp_len - 20); disp_len = 20; }
      char preview[28];
      snprintf(preview, sizeof(preview), "%s%.*s_", label, disp_len, disp_start);
      display.setCursor(0, KB_TEXT_Y);
      display.print(preview);
      display.fillRect(0, KB_SEP_Y, display.width(), 1);

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

    static const char* labels[] = { "Enable", "Channel", "Trigger", "Reply DM", "Reply Ch" };
    for (int i = 0; i < ITEM_COUNT; i++) {
      int y = START_Y + i * ITEM_H;
      bool sel = (i == _sel);
      if (sel) {
        display.fillRect(0, y - 1, display.width(), ITEM_H);
        display.setColor(DisplayDriver::DARK);
      }
      display.setCursor(2, y);
      display.print(labels[i]);
      display.setCursor(VAL_X, y);

      if (i == 0) {
        display.print(_prefs->bot_enabled ? "ON" : "OFF");
      } else if (i == 1) {
        if (!_prefs->bot_channel_enabled || _num_channels == 0) {
          display.print("OFF");
        } else {
          ChannelDetails ch;
          if (the_mesh.getChannel(_prefs->bot_channel_idx, ch) && ch.name[0])
            display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, ch.name);
          else
            display.print("?");
        }
      } else if (i == 2) {
        const char* tr = _prefs->bot_trigger;
        display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, tr[0] ? tr : "(none)");
      } else if (i == 3) {
        const char* rp = _prefs->bot_reply_dm;
        display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, rp[0] ? rp : "(none)");
      } else {
        const char* rp = _prefs->bot_reply_ch;
        display.drawTextEllipsized(VAL_X, y, display.width() - VAL_X - 1, rp[0] ? rp : "(none)");
      }
      display.setColor(DisplayDriver::LIGHT);
    }
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
          if (_kb_row == KB_ROWS_CHAR - 1)
            _kb_col = _kb_col * KB_COLS_CHAR / KB_SPECIAL;
        }
        return true;
      }
      if (down) {
        if (_kb_row < KB_ROWS_CHAR) {
          _kb_row++;
          if (_kb_row == KB_ROWS_CHAR)
            _kb_col = _kb_col * KB_SPECIAL / KB_COLS_CHAR;
        }
        return true;
      }
      if (left)  { if (_kb_col > 0) _kb_col--; return true; }
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
              if (_kb_field == 2) {
                strncpy(_prefs->bot_trigger, _kb_buf, sizeof(_prefs->bot_trigger) - 1);
                _prefs->bot_trigger[sizeof(_prefs->bot_trigger) - 1] = '\0';
              } else if (_kb_field == 3) {
                strncpy(_prefs->bot_reply_dm, _kb_buf, sizeof(_prefs->bot_reply_dm) - 1);
                _prefs->bot_reply_dm[sizeof(_prefs->bot_reply_dm) - 1] = '\0';
              } else {
                strncpy(_prefs->bot_reply_ch, _kb_buf, sizeof(_prefs->bot_reply_ch) - 1);
                _prefs->bot_reply_ch[sizeof(_prefs->bot_reply_ch) - 1] = '\0';
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
      if (_num_channels == 0) return false;
      // Cycle: OFF → ch[0] → ch[1] → ... → OFF
      int idx = currentChannelListIdx();  // -1 = OFF
      if (right || enter) {
        idx++;
        if (idx >= _num_channels) idx = -1;  // wrap to OFF
      }
      if (left) {
        idx--;
        if (idx < -1) idx = _num_channels - 1;
      }
      if (left || right || enter) {
        if (idx < 0) {
          _prefs->bot_channel_enabled = 0;
        } else {
          _prefs->bot_channel_enabled = 1;
          _prefs->bot_channel_idx = _channel_indices[idx];
        }
        return true;
      }
    }
    if ((_sel == 2 || _sel == 3 || _sel == 4) && enter) {
      _kb_field   = _sel;
      _kb_row     = 0;
      _kb_col     = 0;
      _kb_caps    = false;
      _kb_ph_mode = false;
      _kb_ph_sel  = 0;
      if (_sel == 2) {
        strncpy(_kb_buf, _prefs->bot_trigger, sizeof(_kb_buf) - 1);
        _kb_maxlen = sizeof(_prefs->bot_trigger) - 1;
      } else if (_sel == 3) {
        strncpy(_kb_buf, _prefs->bot_reply_dm, sizeof(_kb_buf) - 1);
        _kb_maxlen = sizeof(_prefs->bot_reply_dm) - 1;
      } else {
        strncpy(_kb_buf, _prefs->bot_reply_ch, sizeof(_kb_buf) - 1);
        _kb_maxlen = sizeof(_prefs->bot_reply_ch) - 1;
      }
      _kb_buf[sizeof(_kb_buf) - 1] = '\0';
      _kb_len = strlen(_kb_buf);
      return true;
    }
    return false;
  }
};
