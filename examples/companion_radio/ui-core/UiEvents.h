#pragma once
// Core → frontend events. Engines never call into the frontend (no drawing, no
// sound, no display power); they push a tagged event here and the frontend
// drains the queue from its own loop() and reacts in its own way (ui-new:
// alert overlay + buzzer; ui-lvgl: a dialog). See docs/solo/developer/ui-core.md.
//
// Fixed-size ring, no heap. When full the oldest event is dropped -- events are
// hints for the view; the authoritative state stays queryable on the engines.

enum class UiEventType : uint8_t {
  None = 0,
  ClockAlert,     // alarm / countdown fired: wake, show `text`, start the ring melody
  ClockRingEnded, // ring window elapsed with no dismiss: stop melody, clear alert
  LiveShareEnded, // live-share session reached its duration and switched itself off
  LocatorCrossed, // geofence crossing: show `text`; flag = arrived (else left)
  LocatorBeep,    // one proximity-beeper tick
  MessageArrived, // incoming text: text = sender / channel name; `kind` = contact /
                  // room / channel message; `key` (flag = valid) = DM sender prefix
                  // for per-contact sounds; `idx` = channel slot (-1 = unknown)
  AdvertHeard,    // an advert was heard; flag = flood (else zero-hop)
  SosSent,        // a round of an SOS was tried; flag = the first that went out
  SosAcked,       // someone answered: text = who; flag = the first of this SOS
  SosHeard,       // someone else's SOS: text = who
  RouteAlert,     // following a planned route: off it, back on, finished, removed; flag = good news
};

struct UiEvent {
  UiEventType type;
  bool        flag;
  UIEventType kind;       // MessageArrived: which notification (AbstractUITask.h)
  int16_t     idx;
  uint8_t     key[4];
  char        text[24];
};

class UiEventQueue {
public:
  static const int SIZE = 12;

  UiEventQueue() : _head(0), _count(0) {}

  // Returns the queued event so a caller can fill the optional fields.
  UiEvent& push(UiEventType type, const char* text = nullptr, bool flag = false) {
    int pos;
    if (_count < SIZE) { pos = (_head + _count) % SIZE; _count++; }
    else               { pos = _head; _head = (_head + 1) % SIZE; }   // drop oldest
    _q[pos].type = type;
    _q[pos].flag = flag;
    _q[pos].kind = UIEventType::none;
    _q[pos].idx  = -1;
    memset(_q[pos].key, 0, sizeof(_q[pos].key));
    if (text) {
      strncpy(_q[pos].text, text, sizeof(_q[pos].text) - 1);
      _q[pos].text[sizeof(_q[pos].text) - 1] = '\0';
    } else {
      _q[pos].text[0] = '\0';
    }
    return _q[pos];
  }

  bool pop(UiEvent& out) {
    if (_count == 0) return false;
    out = _q[_head];
    _head = (_head + 1) % SIZE;
    _count--;
    return true;
  }

private:
  UiEvent _q[SIZE];
  int _head, _count;
};
