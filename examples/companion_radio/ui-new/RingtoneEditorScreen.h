#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after the global KB_* constants are defined.

class RingtoneEditorScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int MAX_NOTES     = 32;
  static const int VISIBLE_NOTES = 7;
  static const int CELL_W        = 18;
  static const int NOTES_Y       = 12;
  static const int CELL_H        = 14;
  static const int MENU_VISIBLE  = 5;

  static const uint16_t BPM_OPTS[5];
  static const uint8_t  DUR_VALS[4];
  static const char*    DUR_LABELS[4];
  static const char     PITCH_NAMES[8];  // lowercase rtttl names

  uint8_t _notes[MAX_NOTES];
  uint8_t _len;
  uint8_t _bpm_idx;
  int     _cursor;
  int     _scroll;
  bool    _menu_open;
  int     _menu_sel;
  int     _menu_scroll;

  enum MenuOpt {
    M_PLAY_STOP, M_DURATION, M_BPM_UP, M_BPM_DOWN,
    M_INSERT, M_DELETE, M_SAVE, M_DISCARD, M_COUNT
  };
  static const char* MENU_LABELS[M_COUNT];

  static uint8_t notePitch(uint8_t b)  { return b & 0x07; }
  static uint8_t noteOctave(uint8_t b) { return ((b >> 3) & 0x03) + 4; }
  static uint8_t noteDurIdx(uint8_t b) { return (b >> 5) & 0x03; }
  static uint8_t packNote(uint8_t pitch, uint8_t octave, uint8_t dur_idx) {
    return (pitch & 0x07) | (((octave - 4) & 0x03) << 3) | ((dur_idx & 0x03) << 5);
  }

  void clampScroll() {
    if (_cursor < _scroll)                  _scroll = _cursor;
    if (_cursor >= _scroll + VISIBLE_NOTES) _scroll = _cursor - VISIBLE_NOTES + 1;
    if (_scroll < 0) _scroll = 0;
  }

  void buildRTTTL(char* buf, int buf_len) {
    uint16_t bpm = BPM_OPTS[_bpm_idx < 5 ? _bpm_idx : 2];
    int pos = snprintf(buf, buf_len, "Ring:d=8,o=5,b=%u:", bpm);
    for (int i = 0; i < _len && pos < buf_len - 8; i++) {
      if (i > 0 && pos < buf_len - 1) buf[pos++] = ',';
      uint8_t pitch   = notePitch(_notes[i]);
      uint8_t octave  = noteOctave(_notes[i]);
      uint8_t dur_val = DUR_VALS[noteDurIdx(_notes[i])];
      if (pitch == 0)
        pos += snprintf(buf + pos, buf_len - pos, "%dp", dur_val);
      else
        pos += snprintf(buf + pos, buf_len - pos, "%d%c%d", dur_val, PITCH_NAMES[pitch], octave);
    }
    if (pos < buf_len) buf[pos] = '\0';
  }

  void previewNote(uint8_t note_byte) {
    uint8_t pitch = notePitch(note_byte);
    if (pitch == 0) { _task->stopMelody(); return; }
    char buf[28];
    snprintf(buf, sizeof(buf), "P:d=16,o=5,b=240:%c%d", PITCH_NAMES[pitch], noteOctave(note_byte));
    _task->playMelody(buf);
  }

public:
  RingtoneEditorScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs) {}

  void enter() {
    _bpm_idx    = (_prefs && _prefs->ringtone_bpm_idx < 5) ? _prefs->ringtone_bpm_idx : 2;
    _len        = (_prefs && _prefs->ringtone_len <= (uint8_t)MAX_NOTES) ? _prefs->ringtone_len : 0;
    if (_prefs) memcpy(_notes, _prefs->ringtone_notes, sizeof(_notes));
    _cursor     = 0;
    _scroll     = 0;
    _menu_open  = false;
    _menu_sel   = 0;
    _menu_scroll = 0;
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    char hdr[32];
    snprintf(hdr, sizeof(hdr), "BPM:%u  %d/%d", BPM_OPTS[_bpm_idx], _len, MAX_NOTES);
    display.setCursor(0, 0);
    display.print(hdr);
    display.fillRect(0, 10, display.width(), 1);

    for (int i = 0; i < VISIBLE_NOTES; i++) {
      int ni = _scroll + i;
      int cx = i * CELL_W;
      bool sel = (ni == _cursor);
      if (ni < _len) {
        uint8_t pitch  = notePitch(_notes[ni]);
        uint8_t octave = noteOctave(_notes[ni]);
        char label[3];
        if (pitch == 0) {
          label[0] = '-'; label[1] = '-'; label[2] = '\0';
        } else {
          label[0] = PITCH_NAMES[pitch] - 32;  // uppercase
          label[1] = '0' + octave;
          label[2] = '\0';
        }
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
        }
        display.setCursor(cx + 3, NOTES_Y + 3);
        display.print(label);
        display.setColor(DisplayDriver::LIGHT);
      } else if (ni == _len && _len < MAX_NOTES) {
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(cx, NOTES_Y, CELL_W - 1, CELL_H);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        display.setCursor(cx + 6, NOTES_Y + 3);
        display.print("+");
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    int info_y = NOTES_Y + CELL_H + 2;
    if (_scroll > 0) { display.setCursor(0, info_y); display.print("<"); }
    if (_scroll + VISIBLE_NOTES <= _len) { display.setCursor(display.width() - 6, info_y); display.print(">"); }
    if (_cursor < _len) {
      char info[24];
      snprintf(info, sizeof(info), "oct:%d dur:%s",
               noteOctave(_notes[_cursor]), DUR_LABELS[noteDurIdx(_notes[_cursor])]);
      display.setCursor(10, info_y);
      display.print(info);
    } else if (_cursor == _len) {
      display.setCursor(10, info_y);
      display.print("U/D to add note");
    }

    display.setCursor(0, 54);
    display.print("ENT:oct MENU:opts");

    if (_menu_open) {
      static const int mw = 100, mh = MENU_VISIBLE * 10 + 4;
      int mx = (128 - mw) / 2;
      int my = (64  - mh) / 2;
      display.setColor(DisplayDriver::DARK);
      display.fillRect(mx, my, mw, mh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(mx, my, mw, mh);
      for (int i = 0; i < MENU_VISIBLE; i++) {
        int item = _menu_scroll + i;
        if (item >= M_COUNT) break;
        int iy = my + 2 + i * 10;
        bool sel = (item == _menu_sel);
        if (sel) {
          display.fillRect(mx + 1, iy - 1, mw - 2, 10);
          display.setColor(DisplayDriver::DARK);
        }
        display.setCursor(mx + 4, iy);
        if (item == M_PLAY_STOP)
          display.print(_task->isMelodyPlaying() ? "Stop" : "Play");
        else
          display.print(MENU_LABELS[item]);
        display.setColor(DisplayDriver::LIGHT);
      }
      if (_menu_scroll > 0) { display.setCursor(mx + mw - 7, my + 2); display.print("^"); }
      if (_menu_scroll + MENU_VISIBLE < M_COUNT) { display.setCursor(mx + mw - 7, my + mh - 10); display.print("v"); }
    }

    return 200;
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool left  = (c == KEY_LEFT  || c == KEY_PREV);
    bool right = (c == KEY_RIGHT || c == KEY_NEXT);
    bool enter = (c == KEY_ENTER);
    bool menu_key = (c == KEY_CONTEXT_MENU);
    bool cancel   = (c == KEY_CANCEL);

    if (_menu_open) {
      if (cancel) { _menu_open = false; return true; }
      if (up && _menu_sel > 0) {
        _menu_sel--;
        if (_menu_sel < _menu_scroll) _menu_scroll = _menu_sel;
        return true;
      }
      if (down && _menu_sel < M_COUNT - 1) {
        _menu_sel++;
        if (_menu_sel >= _menu_scroll + MENU_VISIBLE) _menu_scroll = _menu_sel - MENU_VISIBLE + 1;
        return true;
      }
      if (!enter) return true;

      _menu_open = false;
      switch ((MenuOpt)_menu_sel) {
        case M_PLAY_STOP:
          if (_task->isMelodyPlaying()) {
            _task->stopMelody();
          } else if (_len > 0) {
            char buf[220];
            buildRTTTL(buf, sizeof(buf));
            _task->playMelody(buf);
          }
          break;
        case M_DURATION:
          if (_cursor < _len) {
            uint8_t p  = notePitch(_notes[_cursor]);
            uint8_t o  = noteOctave(_notes[_cursor]);
            uint8_t di = (noteDurIdx(_notes[_cursor]) + 1) & 0x03;
            _notes[_cursor] = packNote(p, o, di);
          }
          break;
        case M_BPM_UP:   if (_bpm_idx < 4) _bpm_idx++; break;
        case M_BPM_DOWN: if (_bpm_idx > 0) _bpm_idx--; break;
        case M_INSERT:
          if (_len < MAX_NOTES) {
            int ins = (_cursor < _len) ? _cursor + 1 : _cursor;
            for (int i = _len; i > ins; i--) _notes[i] = _notes[i - 1];
            _notes[ins] = packNote(1, 5, 1);
            _len++;
            _cursor = ins;
            clampScroll();
          }
          break;
        case M_DELETE:
          if (_len > 0 && _cursor < _len) {
            for (int i = _cursor; i < _len - 1; i++) _notes[i] = _notes[i + 1];
            _len--;
            if (_cursor >= _len && _len > 0) _cursor = _len - 1;
            else if (_len == 0) _cursor = 0;
            clampScroll();
          }
          break;
        case M_SAVE:
          if (_prefs) {
            _prefs->ringtone_bpm_idx = _bpm_idx;
            _prefs->ringtone_len     = _len;
            memcpy(_prefs->ringtone_notes, _notes, sizeof(_notes));
            the_mesh.savePrefs();
          }
          _task->stopMelody();
          _task->gotoToolsScreen();
          break;
        case M_DISCARD:
          _task->stopMelody();
          _task->gotoToolsScreen();
          break;
        default: break;
      }
      return true;
    }

    if (cancel)   { _task->stopMelody(); _task->gotoToolsScreen(); return true; }
    if (menu_key) { _menu_open = true; _menu_sel = 0; _menu_scroll = 0; return true; }

    if (left && _cursor > 0) { _cursor--; clampScroll(); return true; }
    if (right) {
      int max_cur = (_len < MAX_NOTES) ? _len : _len - 1;
      if (_cursor < max_cur) { _cursor++; clampScroll(); return true; }
    }

    if ((up || down) && _cursor == _len && _len < MAX_NOTES) {
      _notes[_len] = packNote(1, 5, 1);
      _len++;
      clampScroll();
    }
    if ((up || down) && _cursor < _len) {
      uint8_t p  = notePitch(_notes[_cursor]);
      uint8_t o  = noteOctave(_notes[_cursor]);
      uint8_t di = noteDurIdx(_notes[_cursor]);
      if (up)   p = (p + 1) & 0x07;
      if (down) p = (p + 7) & 0x07;
      _notes[_cursor] = packNote(p, o, di);
      previewNote(_notes[_cursor]);
      return true;
    }

    if (enter && _cursor < _len) {
      uint8_t p  = notePitch(_notes[_cursor]);
      uint8_t o  = noteOctave(_notes[_cursor]);
      uint8_t di = noteDurIdx(_notes[_cursor]);
      if (p != 0) o = (o < 6) ? o + 1 : 4;
      _notes[_cursor] = packNote(p, o, di);
      previewNote(_notes[_cursor]);
      return true;
    }
    if (enter && _cursor == _len && _len < MAX_NOTES) {
      _notes[_len] = packNote(1, 5, 1);
      _len++;
      clampScroll();
      previewNote(_notes[_cursor]);
      return true;
    }
    return false;
  }
};

const uint16_t RingtoneEditorScreen::BPM_OPTS[5]   = { 60, 90, 120, 150, 180 };
const uint8_t  RingtoneEditorScreen::DUR_VALS[4]    = { 4, 8, 16, 32 };
const char*    RingtoneEditorScreen::DUR_LABELS[4]  = { "1/4", "1/8", "1/16", "1/32" };
const char     RingtoneEditorScreen::PITCH_NAMES[8] = { 'p', 'c', 'd', 'e', 'f', 'g', 'a', 'b' };
const char*    RingtoneEditorScreen::MENU_LABELS[RingtoneEditorScreen::M_COUNT] = {
  "Play/Stop", "Duration", "BPM+", "BPM-", "Insert", "Delete", "Save & Exit", "Discard"
};
