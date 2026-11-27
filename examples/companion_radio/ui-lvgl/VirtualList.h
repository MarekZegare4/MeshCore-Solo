#pragma once
// Included into UITask.cpp only, where it stands (it uses what is defined above it).

// ── Virtual list ──────────────────────────────────────────────────────────────
// A long list (contacts, nodes, conversations) as only the rows it shows: a
// row scrolled out of sight takes the next item coming in. The items are
// kinds with a fixed height -- a row made once (make), its texts set for an
// item (bind) -- or fixed objects of their own (a section title, a note).
// The scroll extent comes from the items' heights, so the scrollbar, a throw
// and coming back to where it was work as with every row built. Built in the
// same time whatever the count; 256 contacts: 522 objects, 950 ms filling
// in and 28 ms a scrolled frame before, 19 objects and 13 ms now.
namespace vlist {
struct Kind {
  int16_t h;
  lv_obj_t* (*make)(lv_obj_t* list);      // a row, its event callbacks on (they ask arg())
  void (*bind)(lv_obj_t* row, intptr_t arg);
};
struct Item { const Kind* k; intptr_t arg; int32_t top; int16_t h; };   // k null: arg is its own object
struct Slot { lv_obj_t* o; const Kind* k; int item; int prev; };
static const int SLOTS = 32;   // a screenful of the shortest kind, a few kinds
struct List {
  lv_obj_t* obj;
  Item* items;
  int n, cap;
  int32_t total;
  bool placed;   // the items' tops are known (not while a new set comes in)
  Slot slots[SLOTS];
  int nslots;
};
static List* s_lists[8];   // a screen's and a popup's, two screens while one slides in

static List* of(lv_obj_t* obj) {
  for (List* l : s_lists) if (l && l->obj == obj) return l;
  return nullptr;
}

static void place(lv_obj_t* o, int32_t y) {
  if (lv_obj_get_style_y(o, LV_PART_MAIN) != y) lv_obj_set_y(o, y);
  if (lv_obj_is_hidden(o)) lv_obj_set_hidden(o, false);
}

// The rows for what's in view: kept where they show the same item, the rest
// handed over (bound) to the items coming in, made only when none is free.
// all: every one bound again (the items changed).
static void update(List* l, bool all) {
  if (!l->placed) return;
  int32_t sy = lv_obj_get_scroll_y(l->obj), vh = lv_obj_get_content_height(l->obj);
  if (vh <= 0) vh = LV_VER_RES;   // not laid out yet: a screenful
  int first = 0, hi = l->n;
  while (first < hi) {   // the first item reaching into view
    int m = (first + hi) / 2;
    if (l->items[m].top + l->items[m].h <= sy) first = m + 1; else hi = m;
  }
  int last = first;
  while (last < l->n && l->items[last].top < sy + vh) last++;
  for (int s = 0; s < l->nslots; s++) {
    Slot& sl = l->slots[s];
    if (sl.item < 0) continue;
    if (all || sl.item < first || sl.item >= last) { sl.prev = sl.item; sl.item = -1; }
  }
  for (int i = first; i < last; i++) {
    Item& it = l->items[i];
    if (!it.k) continue;
    int got = -1;
    for (int s = 0; s < l->nslots && got < 0; s++) if (l->slots[s].item == i) got = s;
    if (got >= 0) continue;   // shown already
    for (int s = 0; s < l->nslots; s++) {   // a free one of its kind; the one that showed it, if any
      Slot& sl = l->slots[s];
      if (sl.item >= 0 || sl.k != it.k) continue;
      if (got < 0 || sl.prev == i) got = s;
      if (sl.prev == i) break;
    }
    if (got < 0) {
      if (l->nslots == SLOTS) continue;
      got = l->nslots++;
      l->slots[got] = { it.k->make(l->obj), it.k, -1, -1 };
    }
    Slot& sl = l->slots[got];
    sl.item = i;
    it.k->bind(sl.o, it.arg);
    place(sl.o, it.top);
  }
  for (int s = 0; s < l->nslots; s++) {
    Slot& sl = l->slots[s];
    if (sl.item < 0 && !lv_obj_is_hidden(sl.o)) lv_obj_set_hidden(sl.o, true);
  }
}

static void onEvent(lv_event_t* e) {
  lv_obj_t* obj = (lv_obj_t*)lv_event_get_current_target(e);
  List* l = of(obj);
  if (!l) return;
  switch (lv_event_get_code(e)) {
    case LV_EVENT_SCROLL: case LV_EVENT_SIZE_CHANGED: update(l, false); break;
    case LV_EVENT_GET_SELF_SIZE: {
      lv_point_t* p = (lv_point_t*)lv_event_get_param(e);
      if (p->y < l->total) p->y = l->total;
      break;
    }
    case LV_EVENT_DELETE:
      for (List*& x : s_lists) if (x == l) x = nullptr;
      lv_free(l->items);
      lv_free(l);
      break;
    default: break;
  }
}

// `obj` (a scrolling column: a screen body, a scrollList) becomes a virtual
// list; its children so far are taken away.
static void attach(lv_obj_t* obj) {
  List* l = nullptr;
  for (List*& x : s_lists) if (!x) { x = l = (List*)lv_malloc_zeroed(sizeof(List)); break; }
  if (!l) return;   // can't be: then a plain empty column
  l->obj = obj;
  lv_obj_clean(obj);
  lv_obj_set_layout(obj, LV_LAYOUT_NONE);
  for (lv_event_code_t c : { LV_EVENT_SCROLL, LV_EVENT_SIZE_CHANGED, LV_EVENT_GET_SELF_SIZE, LV_EVENT_DELETE })
    lv_obj_add_event_cb(obj, onEvent, c, NULL);
}

// A new set of items: add() / own() them, then end(). The rows made stay
// (rebound); objects of their own given to own() before are deleted.
static void begin(lv_obj_t* obj) {
  List* l = of(obj);
  if (!l) return;
  for (int i = 0; i < l->n; i++) if (!l->items[i].k) lv_obj_delete((lv_obj_t*)l->items[i].arg);
  l->n = 0;
  l->placed = false;
}
static void push(List* l, const Kind* k, intptr_t arg, int16_t h) {
  if (l->n == l->cap) {
    l->cap = l->cap ? l->cap * 2 : 64;
    l->items = (Item*)lv_realloc(l->items, l->cap * sizeof(Item));
  }
  l->items[l->n++] = { k, arg, 0, h };
}
static void add(lv_obj_t* obj, const Kind* k, intptr_t arg) { if (List* l = of(obj)) push(l, k, arg, k->h); }
// An object of its own (made on the list, its height known after a layout).
static void own(lv_obj_t* obj, lv_obj_t* o) { if (List* l = of(obj)) push(l, nullptr, (intptr_t)o, 0); }

static void end(lv_obj_t* obj) {
  List* l = of(obj);
  if (!l) return;
  bool mine = false;
  for (int i = 0; i < l->n && !mine; i++) mine = !l->items[i].k;
  if (mine) lv_obj_update_layout(obj);   // their heights
  int32_t gap = lv_obj_get_style_pad_row(obj, LV_PART_MAIN), y = 0;
  for (int i = 0; i < l->n; i++) {
    Item& it = l->items[i];
    if (!it.k) {
      lv_obj_t* o = (lv_obj_t*)it.arg;
      it.h = (int16_t)lv_obj_get_height(o);
      place(o, y);
    }
    it.top = y;
    y += it.h + gap;
  }
  l->total = l->n ? y - gap : 0;
  l->placed = true;
  lv_obj_refresh_self_size(obj);
  update(l, true);
  lv_obj_readjust_scroll(obj, LV_ANIM_OFF);   // fewer items: not scrolled past the end
}

// The item a row's event is for (its arg).
static intptr_t arg(lv_event_t* e) {
  lv_obj_t* o = (lv_obj_t*)lv_event_get_current_target(e);
  List* l = of(lv_obj_get_parent(o));
  if (l)
    for (int s = 0; s < l->nslots; s++)
      if (l->slots[s].o == o && l->slots[s].item >= 0) return l->items[l->slots[s].item].arg;
  return -1;
}

// A row held down: what it shows stays until it's let go.
static bool pressed(lv_obj_t* obj) {
  List* l = of(obj);
  for (int s = 0; l && s < l->nslots; s++)
    if (lv_obj_has_state(l->slots[s].o, LV_STATE_PRESSED)) return true;
  return false;
}
}  // namespace vlist
