#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after KeyboardWidget.h is defined.

class BotScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  // Items: 0=Enable(DM), 1=Channel, 2=Trigger, 3=Reply DM, 4=Reply Ch
  static const int ITEM_COUNT = 5;
  static const int ITEM_H     = 11;
  static const int START_Y    = 12;
  static const int VAL_X      = 70;

  int  _sel;
  bool _dirty;

  // keyboard state (reused for trigger and reply fields)
  int            _kb_field;   // -1=off, 2=trigger, 3=reply DM, 4=reply Ch
  KeyboardWidget _kb;

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
    _dirty    = false;
    refreshChannels();
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    if (_kb_field >= 0) {
      return _kb.render(display);
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
    return 2000;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool enter = (c == KEY_ENTER);
    bool cancel = (c == KEY_CANCEL || c == KEY_CONTEXT_MENU);

    if (_kb_field >= 0) {
      auto res = _kb.handleInput(c);
      if (res == KeyboardWidget::DONE) {
        if (_kb_field == 2) {
          strncpy(_prefs->bot_trigger, _kb.buf, sizeof(_prefs->bot_trigger) - 1);
          _prefs->bot_trigger[sizeof(_prefs->bot_trigger) - 1] = '\0';
        } else if (_kb_field == 3) {
          strncpy(_prefs->bot_reply_dm, _kb.buf, sizeof(_prefs->bot_reply_dm) - 1);
          _prefs->bot_reply_dm[sizeof(_prefs->bot_reply_dm) - 1] = '\0';
        } else {
          strncpy(_prefs->bot_reply_ch, _kb.buf, sizeof(_prefs->bot_reply_ch) - 1);
          _prefs->bot_reply_ch[sizeof(_prefs->bot_reply_ch) - 1] = '\0';
        }
        _dirty    = true;
        _kb_field = -1;
      } else if (res == KeyboardWidget::CANCELLED) {
        _kb_field = -1;
      }
      return true;
    }

    if (cancel) {
      if (_dirty) the_mesh.savePrefs();
      _task->gotoToolsScreen();
      return true;
    }
    if (up   && _sel > 0) { _sel--; return true; }
    if (down && _sel < ITEM_COUNT - 1) { _sel++; return true; }

    if (_sel == 0 && (enter || left || right)) {
      _prefs->bot_enabled ^= 1;
      _dirty = true;
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
        _dirty = true;
        return true;
      }
    }
    if ((_sel == 2 || _sel == 3 || _sel == 4) && enter) {
      _kb_field = _sel;
      const char* initial;
      int max;
      if (_sel == 2) {
        initial = _prefs->bot_trigger;
        max     = sizeof(_prefs->bot_trigger) - 1;
      } else if (_sel == 3) {
        initial = _prefs->bot_reply_dm;
        max     = sizeof(_prefs->bot_reply_dm) - 1;
      } else {
        initial = _prefs->bot_reply_ch;
        max     = sizeof(_prefs->bot_reply_ch) - 1;
      }
      _kb.begin(initial, max);
      if (_sel == 2) {
        _kb._ph_count = 0;  // trigger is literal text — placeholders never match incoming msgs
      } else {
        kbAddSensorPlaceholders(_kb, &sensors);
      }
      return true;
    }
    return false;
  }
};
