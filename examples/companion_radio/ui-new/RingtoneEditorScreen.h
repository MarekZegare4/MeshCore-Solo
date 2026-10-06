#pragma once
// Custom screen — not part of upstream UITask.cpp
// Included by UITask.cpp after the global KB_* constants are defined.

class RingtoneEditorScreen : public UIScreen {
  UITask*    _task;
  NodePrefs* _prefs;

  static const int MAX_NOTES = soundctl::MAX_NOTES;   // melody model: ui-core/SoundControl.h

  // Menu item indices
  enum MenuIdx { MI_PLAY=0, MI_SWITCH, MI_DURATION, MI_BPM, MI_INSERT, MI_DELETE, MI_SAVE, MI_DISCARD, MI_COUNT };

  int _visible_notes = 7;  // updated in render(); used by clampScroll()

  soundctl::Melody _mel;
  uint8_t* const _notes = _mel.notes;
  uint8_t& _len = _mel.len;
  uint8_t& _bpm_idx = _mel.bpm_idx;
  int     _slot;   // 0=melody1, 1=melody2
  int     _cursor;
  int     _scroll;
  PopupMenu _menu;
  char    _play_buf[220];  // persistent RTTTL buffer — library holds a pointer into it
  char    _menu_play_label[8];
  char    _menu_slot_label[16];
  char    _menu_dur_label[16];
  char    _menu_bpm_label[10];

  static uint8_t notePitch(uint8_t b)  { return soundctl::notePitch(b); }
  static uint8_t noteOctave(uint8_t b) { return soundctl::noteOctave(b); }
  static uint8_t noteDurIdx(uint8_t b) { return soundctl::noteDur(b); }
  static uint8_t packNote(uint8_t pitch, uint8_t octave, uint8_t dur_idx) { return soundctl::packNote(pitch, octave, dur_idx); }

  void clampScroll() {
    if (_cursor < _scroll)                      _scroll = _cursor;
    if (_cursor >= _scroll + _visible_notes) _scroll = _cursor - _visible_notes + 1;
    if (_scroll < 0) _scroll = 0;
  }

  void buildRTTTL() { soundctl::toRtttl(_mel, _play_buf, sizeof(_play_buf)); }

  void previewNote(uint8_t note_byte) {
    soundctl::notePreview(note_byte, _play_buf, sizeof(_play_buf));
    if (!_play_buf[0]) { _task->stopMelody(); return; }
    _task->playMelody(_play_buf);
  }

public:
  RingtoneEditorScreen(UITask* task, NodePrefs* prefs) : _task(task), _prefs(prefs), _slot(0) {}

  // Load a melody slot for editing. Called by gotoRingtoneEditor() right after
  // setCurrScreen() (whose onShow() can't carry the slot argument).
  void selectSlot(int slot = 0) {
    _slot    = (slot == 1) ? 1 : 0;
    soundctl::load(_prefs, _slot, _mel);
    _cursor  = 0;
    _scroll  = 0;
    _menu.active = false;
  }

  void openMenu() {
    snprintf(_menu_play_label, sizeof(_menu_play_label), "%s", _task->isMelodyPlaying() ? "Stop" : "Play");
    snprintf(_menu_slot_label, sizeof(_menu_slot_label), "Melody %d", (_slot == 0) ? 2 : 1);
    uint8_t di = (_cursor < _len) ? noteDurIdx(_notes[_cursor]) : 0;
    snprintf(_menu_dur_label, sizeof(_menu_dur_label), "Duration: %s", soundctl::durLabel(di));
    snprintf(_menu_bpm_label, sizeof(_menu_bpm_label), "BPM: %u", soundctl::bpm(_bpm_idx));
    _menu.begin("Options", 5);
    _menu.addItem(_menu_play_label);
    _menu.addItem(_menu_slot_label);
    _menu.addValueItem(_menu_dur_label);
    _menu.addValueItem(_menu_bpm_label);
    _menu.addItem("Insert");
    _menu.addItem("Delete");
    _menu.addItem("Save & Exit");
    _menu.addItem("Discard");
  }

  int render(DisplayDriver& display) override {
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int lh            = display.getLineHeight();
    const int cw            = display.getCharWidth();
    const bool wide         = display.width() >= 40 * cw;
    const int cell_w        = cw * 2 + (wide ? 12 : 6);  // fits 2-char label with margin
    const int notes_y       = display.listStart();
    const int cell_h        = lh + 6;
    _visible_notes          = display.width() / cell_w;

    // A screen 40 characters wide (the 4.2"): a titled header, and the
    // melody drawn as a roll under the notes (see drawRoll()).
    char hdr[40];
    if (wide) {
      snprintf(hdr, sizeof(hdr), "Melody %d  %u BPM  %d/%d", _slot + 1, soundctl::bpm(_bpm_idx), _len, MAX_NOTES);
      drawScreenHeader(display, hdr, -1, 0, true, _menu.active);
    } else {
      snprintf(hdr, sizeof(hdr), "M%d BPM:%u %d/%d", _slot + 1, soundctl::bpm(_bpm_idx), _len, MAX_NOTES);
      display.setCursor(0, 0);
      display.print(hdr);
      display.fillRect(0, display.headerH() - 1, display.width(), display.sepH());
    }

    for (int i = 0; i < _visible_notes; i++) {
      int ni = _scroll + i;
      int cx = i * cell_w;
      bool sel = (ni == _cursor);
      if (ni < _len) {
        uint8_t pitch  = notePitch(_notes[ni]);
        uint8_t octave = noteOctave(_notes[ni]);
        char label[3];
        if (pitch == 0) {
          label[0] = '-'; label[1] = '-'; label[2] = '\0';
        } else {
          label[0] = soundctl::pitchChar(pitch) - 32;  // uppercase
          label[1] = '0' + octave;
          label[2] = '\0';
        }
        if (sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillSoftRect(cx, notes_y, cell_w - 1, cell_h);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
          display.drawRect(cx, notes_y, cell_w - 1, cell_h);
        }
        display.setCursor(cx + (cell_w - cw * 2) / 2, notes_y + 3);
        display.print(label);
        display.setColor(DisplayDriver::LIGHT);
      } else if (ni == _len && _len < MAX_NOTES) {
        display.drawSelectionRow(cx, notes_y, cell_w - 1, cell_h, sel);
        display.setCursor(cx + (cell_w - cw) / 2, notes_y + 3);
        display.print("+");
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    const int info_y   = notes_y + cell_h + 2;
    const int bottom_y = display.height() - display.lineStep();
    if (_scroll > 0) { display.setCursor(0, info_y); display.print("<"); }
    if (_scroll + _visible_notes <= _len) { display.setCursor(display.width() - display.getCharWidth(), info_y); display.print(">"); }
    if (_cursor < _len) {
      char info[24];
      snprintf(info, sizeof(info), "oct:%d dur:%s",
               noteOctave(_notes[_cursor]), soundctl::durLabel(noteDurIdx(_notes[_cursor])));
      display.setCursor(display.getCharWidth() + 2, info_y);
      display.print(info);
    } else if (_cursor == _len) {
      display.setCursor(display.getCharWidth() + 2, info_y);
      display.print("U/D to add note");
    }

    if (wide) drawRoll(display, info_y + lh + 6, bottom_y - 6, cell_w);
    display.setCursor(0, bottom_y);
    display.print(wide ? "Up/Down: note  Enter: octave  Hold: options" : "ENT:oct MENU:opts");

    if (_menu.active) _menu.render(display);

    return 200;
  }

  // The visible notes as a roll from y0 to y1: under each note's cell a bar
  // as high as its pitch (C4 at the bottom to B6 at the top, a dotted line
  // at each C) and as wide as its length (a 1/4 fills the cell); a rest is
  // a dash on the floor. The selected note's column is marked at its sides.
  void drawRoll(DisplayDriver& d, int y0, int y1, int cell_w) {
    const int steps = (soundctl::OCT_MAX - soundctl::OCT_MIN + 1) * 7;   // C4..B6
    const int h = y1 - y0, W = _visible_notes * cell_w;
    if (h < steps * 2) return;
    const int bar_h = h / steps > 5 ? 5 : h / steps > 2 ? h / steps : 2;
    d.setColor(DisplayDriver::LIGHT);
    for (int o = 0; o <= soundctl::OCT_MAX - soundctl::OCT_MIN; o++) {
      const int y = y1 - (o * 7) * h / steps;
      for (int x = 0; x < W; x += 3) d.fillRect(x, y, 1, 1);
    }
    for (int i = 0; i < _visible_notes; i++) {
      const int ni = _scroll + i, cx = i * cell_w;
      if (ni == _cursor)
        for (int y = y0; y < y1; y += 2) { d.fillRect(cx, y, 1, 1); d.fillRect(cx + cell_w - 2, y, 1, 1); }
      if (ni >= _len) continue;
      const uint8_t p = notePitch(_notes[ni]);
      const int w = (cell_w - 4) >> noteDurIdx(_notes[ni]);
      if (p == 0) { d.fillRect(cx + 2, y1 - 1, w > 2 ? w : 2, 1); continue; }
      const int k = (noteOctave(_notes[ni]) - soundctl::OCT_MIN) * 7 + p - 1;
      d.fillRect(cx + 2, y1 - (k + 1) * h / steps, w > 2 ? w : 2, bar_h);
    }
  }

  void cycleMenuValue(int sel, int dir) {
    if (sel == MI_DURATION && _cursor < _len) {
      uint8_t p  = notePitch(_notes[_cursor]);
      uint8_t o  = noteOctave(_notes[_cursor]);
      uint8_t di = noteDurIdx(_notes[_cursor]);
      di = (dir > 0) ? (di + 1) & 0x03 : (di + 3) & 0x03;
      _notes[_cursor] = packNote(p, o, di);
      snprintf(_menu_dur_label, sizeof(_menu_dur_label), "Duration: %s", soundctl::durLabel(di));
    } else if (sel == MI_BPM) {
      if (dir > 0) { if (_bpm_idx < 4) _bpm_idx++; }
      else         { if (_bpm_idx > 0) _bpm_idx--; }
      snprintf(_menu_bpm_label, sizeof(_menu_bpm_label), "BPM: %u", soundctl::bpm(_bpm_idx));
    }
  }

  bool handleInput(char c) override {
    bool up    = (c == KEY_UP);
    bool down  = (c == KEY_DOWN);
    bool left  = keyIsPrev(c);
    bool right = keyIsNext(c);
    bool enter = (c == KEY_ENTER);
    bool menu_key = (c == KEY_CONTEXT_MENU);
    bool cancel   = (c == KEY_CANCEL);

    if (_menu.active) {
      // LEFT/RIGHT -- and Enter, via VALUE_NEXT -- cycle Duration and BPM in
      // place; the menu stays open and only Back closes it.
      if (left || right) { cycleMenuValue(_menu.selectedIndex(), right ? 1 : -1); return true; }
      auto res = _menu.handleInput(c);
      if (res == PopupMenu::VALUE_NEXT) { cycleMenuValue(_menu.selectedIndex(), 1); return true; }
      if (res == PopupMenu::SELECTED) {
        switch ((MenuIdx)_menu.selectedIndex()) {
          case MI_PLAY:
            if (_task->isMelodyPlaying()) { _task->stopMelody(); }
            else if (_len > 0) { buildRTTTL(); _task->playMelody(_play_buf); }
            break;
          case MI_SWITCH:
            _task->stopMelody();
            this->selectSlot(1 - _slot);
            break;
          case MI_DURATION: break;  // value rows -- see cycleMenuValue()
          case MI_BPM:      break;
          case MI_INSERT:
            if (_len < MAX_NOTES) {
              int ins = (_cursor < _len) ? _cursor + 1 : _cursor;
              for (int i = _len; i > ins; i--) _notes[i] = _notes[i - 1];
              _notes[ins] = packNote(1, 5, 1);
              _len++;
              _cursor = ins;
              clampScroll();
            }
            break;
          case MI_DELETE:
            if (_len > 0 && _cursor < _len) {
              for (int i = _cursor; i < _len - 1; i++) _notes[i] = _notes[i + 1];
              _len--;
              if (_cursor >= _len && _len > 0) _cursor = _len - 1;
              else if (_len == 0) _cursor = 0;
              clampScroll();
            }
            break;
          case MI_SAVE:
            if (_prefs) {
              soundctl::store(_prefs, _slot, _mel);
              the_mesh.savePrefs();
            }
            _task->stopMelody();
            _task->gotoToolsScreen();
            break;
          case MI_DISCARD:
            _task->stopMelody();
            _task->gotoToolsScreen();
            break;
          default: break;
        }
      }
      return true;
    }

    if (cancel)   { _task->stopMelody(); _task->gotoToolsScreen(); return true; }
    if (menu_key) { openMenu(); return true; }

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
