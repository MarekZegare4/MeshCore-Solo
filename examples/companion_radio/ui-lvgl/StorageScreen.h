#pragma once
// Settings > Storage: how full the SD card and the internal flash are and
// what takes the card's space (maps, message history, GPX trails, the rest),
// how many messages each conversation keeps (HistoryStore.h) and deleting
// the history, and how much space live map tiles may take (map/LiveCache.h).
//
// The card's files are counted in the background while the screen is open
// (a map can be tens of thousands of tiles), a few milliseconds per loop.
//
// Single-TU fragment: included at the end of ui-lvgl/UITask.cpp.

namespace storeview {

enum Cat : uint8_t { C_MAPS, C_LIVE, C_MSGS, C_TRAILS, C_OTHER, C_COUNT };
static const char* const CAT_NAME[C_COUNT] = { "Maps", "Live map tiles", "Messages", "Trails (GPX)", "Other" };
// Each category's colour in the card's bar and on its row; fixed, whatever the accent.
static const uint32_t CAT_COL[C_COUNT] = { 0x4C8DF6, 0x3CC6D8, 0xE8A33C, 0x6FCF6F, 0xB08AE0 };
static const uint32_t UNCOUNTED_COL = 0x5A5750;   // in use, not (yet) in a category: the walk, the filesystem

static uint64_t s_bytes[C_COUNT];
static uint32_t s_files = 0;
static bool     s_walking = false;

// Folder walk, one entry at a time: a stack of open folders.
static const int DEPTH = 8;
static DIR*    s_dir[DEPTH];
static size_t  s_plen[DEPTH];
static uint8_t s_dcat[DEPTH];
static int     s_depth = 0;
static char    s_path[192];

static bool     s_sd_read = false, s_sd_ok = false;
static uint64_t s_sd_total = 0, s_sd_used = 0, s_sd_card = 0;
static lv_obj_t* s_sd_bar = nullptr;           // the card: a bar split by category
static lv_obj_t* s_seg[C_COUNT + 1];           // its segments, the last one uncounted
static lv_obj_t* s_sd_lbl = nullptr;
static lv_obj_t* s_cat_val[C_COUNT];
static lv_obj_t* s_status = nullptr;
static lv_obj_t* s_clear_lbl = nullptr;
static lv_obj_t* s_live_lbl = nullptr;       // "Delete live map tiles" row label
static lv_obj_t* s_live_info = nullptr;      // count / size from the index
static uint32_t  s_shown_ms = 0;
static lv_obj_t* s_fmt_overlay = nullptr;    // Format card: the popup
static lv_obj_t* s_fmt_go_lbl = nullptr;     // its Format button's label

static void fmtBytes(char* out, size_t n, uint64_t b) {
  if (b < 1024) snprintf(out, n, "%u B", (unsigned)b);
  else if (b < (1ULL << 20)) snprintf(out, n, "%u KB", (unsigned)((b + 512) >> 10));
  else if (b < (1ULL << 30)) snprintf(out, n, "%.1f MB", b / 1048576.0);
  else snprintf(out, n, "%.1f GB", b / 1073741824.0);
}

static void stopWalk() {
  while (s_depth > 0) closedir(s_dir[--s_depth]);
  s_walking = false;
}

static void startWalk() {
  stopWalk();
  memset(s_bytes, 0, sizeof(s_bytes));
  s_files = 0;
  snprintf(s_path, sizeof(s_path), "/sdcard");
  if (DIR* d = opendir(s_path)) {
    s_dir[0] = d;
    s_plen[0] = strlen(s_path);
    s_dcat[0] = C_OTHER;
    s_depth = 1;
    s_walking = true;
  }
}

// What a top-level folder counts as.
static uint8_t topCat(const char* name) {
  if (!strcmp(name, "maps") || !strcmp(name, "maps-trails")) return C_MAPS;   // trails: the maps' overlay
  if (!strcmp(name, "maps-live")) return C_LIVE;
  if (!strcmp(name, "meshcore")) return C_MSGS;
  if (!strcmp(name, "trails")) return C_TRAILS;
  return C_OTHER;
}

// Walks for up to `budget_ms`; false once done.
static bool stepWalk(uint32_t budget_ms) {
  uint32_t t0 = millis();
  while (s_depth > 0 && millis() - t0 < budget_ms) {
    int top = s_depth - 1;
    struct dirent* de = readdir(s_dir[top]);
    if (!de) {
      closedir(s_dir[top]);
      s_depth--;
      if (s_depth > 0) s_path[s_plen[s_depth - 1]] = '\0';
      continue;
    }
    if (de->d_name[0] == '.') continue;   // ".", "..", hidden files
    size_t base = s_plen[top];
    snprintf(s_path + base, sizeof(s_path) - base, "/%s", de->d_name);
    uint8_t cat = top == 0 ? topCat(de->d_name) : s_dcat[top];
    struct stat st;
    if (stat(s_path, &st) == 0) {
      if (S_ISDIR(st.st_mode)) {
        if (s_depth < DEPTH) {
          if (DIR* d = opendir(s_path)) {
            s_dir[s_depth] = d;
            s_plen[s_depth] = strlen(s_path);
            s_dcat[s_depth] = cat;
            s_depth++;
            continue;   // the path stays extended while inside
          }
        }
      } else {
        s_bytes[cat] += (uint64_t)st.st_size;
        s_files++;
      }
    }
    s_path[base] = '\0';
  }
  if (s_depth == 0) s_walking = false;
  return s_walking;
}

// Size of everything under `root` (small trees only: the flash in the sim).
static uint64_t treeBytes(const char* root, int depth = 0) {
  uint64_t sum = 0;
  DIR* d = opendir(root);
  if (!d) return 0;
  while (struct dirent* de = readdir(d)) {
    if (de->d_name[0] == '.') continue;
    char p[160];
    snprintf(p, sizeof(p), "%s/%s", root, de->d_name);
    struct stat st;
    if (stat(p, &st) != 0) continue;
    if (S_ISDIR(st.st_mode)) { if (depth < 4) sum += treeBytes(p, depth + 1); }
    else sum += (uint64_t)st.st_size;
  }
  closedir(d);
  return sum;
}

// A group row: a bar with "x used of y" under it.
static lv_obj_t* usageBar(lv_obj_t* g, lv_obj_t** lbl) {
  lv_obj_t* row = infoLine(g);
  lv_obj_set_style_pad_ver(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(row, 6, 0);
  lv_obj_t* bar = lv_bar_create(row);
  lv_obj_set_size(bar, LV_PCT(100), 10);
  lv_bar_set_range(bar, 0, 1000);
  lv_obj_set_style_bg_color(bar, lv_color_hex(theme::BG), LV_PART_MAIN);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, LV_PART_MAIN);
  lv_obj_set_style_bg_color(bar, lv_color_hex(theme::ACCENT), LV_PART_INDICATOR);
  *lbl = label(row, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  return bar;
}

// The card's bar: a track with one segment per category, side by side.
static lv_obj_t* segmentBar(lv_obj_t* g, lv_obj_t** lbl) {
  lv_obj_t* row = infoLine(g);
  lv_obj_set_style_pad_ver(row, 10, 0);
  lv_obj_set_flex_flow(row, LV_FLEX_FLOW_COLUMN);
  lv_obj_set_style_pad_row(row, 6, 0);
  lv_obj_t* bar = lv_obj_create(row);
  lv_obj_remove_style_all(bar);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_set_size(bar, LV_PCT(100), 10);
  lv_obj_set_style_bg_color(bar, lv_color_hex(theme::BG), 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_COVER, 0);
  lv_obj_set_style_radius(bar, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_clip_corner(bar, true, 0);
  lv_obj_set_flex_flow(bar, LV_FLEX_FLOW_ROW);
  for (int c = 0; c <= C_COUNT; c++) {
    lv_obj_t* sg = lv_obj_create(bar);
    lv_obj_remove_style_all(sg);
    lv_obj_remove_flag(sg, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_set_size(sg, 0, LV_PCT(100));
    lv_obj_set_style_bg_color(sg, lv_color_hex(c < C_COUNT ? CAT_COL[c] : UNCOUNTED_COL), 0);
    lv_obj_set_style_bg_opa(sg, LV_OPA_COVER, 0);
    s_seg[c] = sg;
  }
  *lbl = label(row, "", THEME_FONT_SMALL, theme::TEXT_MUTED);
  return bar;
}

// A category row: its colour dot, name, size in its colour.
static lv_obj_t* catRow(lv_obj_t* g, int c) {
  lv_obj_t* v = infoRow(g, CAT_NAME[c], "...", CAT_COL[c]);
  lv_obj_t* d = lv_obj_create(lv_obj_get_parent(v));
  lv_obj_remove_style_all(d);
  lv_obj_set_size(d, 10, 10);
  lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(d, lv_color_hex(CAT_COL[c]), 0);
  lv_obj_set_style_bg_opa(d, LV_OPA_COVER, 0);
  lv_obj_move_to_index(d, 0);
  return v;
}

// Segment widths: each category's share of the card, the rest of `used` grey.
static void showSegments(uint64_t total, uint64_t used) {
  if (!s_sd_bar || !total) return;
  lv_obj_update_layout(s_sd_bar);
  int w = lv_obj_get_width(s_sd_bar), x = 0;
  uint64_t counted = 0;
  for (int c = 0; c <= C_COUNT; c++) {
    uint64_t b = c < C_COUNT ? s_bytes[c] : (used > counted ? used - counted : 0);
    if (c < C_COUNT) counted += b;
    int px = (int)((double)b * w / total + 0.5);
    if (b && px < 2) px = 2;   // a sliver still shows
    if (px > w - x) px = w - x;
    lv_obj_set_width(s_seg[c], px);
    x += px;
  }
}

static void showUsage(lv_obj_t* bar, lv_obj_t* lbl, uint64_t total, uint64_t used) {
  if (!bar || !lbl) return;
  if (used > total) used = total;
  if (bar == s_sd_bar) showSegments(total, used);
  else lv_bar_set_value(bar, total ? (int32_t)(used * 1000 / total) : 0, LV_ANIM_OFF);
  char u[16], t[16], f[16], line[64];
  fmtBytes(u, sizeof(u), used);
  fmtBytes(t, sizeof(t), total);
  fmtBytes(f, sizeof(f), total - used);
  snprintf(line, sizeof(line), "%s used of %s  -  %s free", u, t, f);
  lv_label_set_text(lbl, line);
}

static void refreshSizes() {
  for (int c = 0; c < C_COUNT; c++) {
    if (!s_cat_val[c]) continue;
    char b[16];
    fmtBytes(b, sizeof(b), s_bytes[c]);
    lv_label_set_text(s_cat_val[c], b);
  }
  if (s_status) {
    char t[48];
    snprintf(t, sizeof(t), s_walking ? "counting... %lu" : "%lu", (unsigned long)s_files);
    lv_label_set_text(s_status, t);
  }
}

}  // namespace storeview

static void onOpenStorage(lv_event_t* e) { (void)e; s_ui->showStorage(); }
static void onHistKeep(lv_event_t* e) {
  s_ui->storageKeep((int)choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onHistClear(lv_event_t* e) { (void)e; s_ui->storageClearHistory(); }
static void onLiveCap(lv_event_t* e) {
  s_ui->storageLiveCap((int)choiceSelected((lv_obj_t*)lv_event_get_target(e)));
}
static void onLiveClear(lv_event_t* e) { (void)e; s_ui->storageClearLive(); }
static void onFormatAsk(lv_event_t* e) { (void)e; s_ui->storageFormatAsk(); }
static void onFormatTap(lv_event_t* e) { s_ui->storageFormatTap(lv_event_get_user_data(e) != nullptr); }

void UITask::showStorage() {
  _screen = SCR_STORAGE;
  buildStorage();
}

void UITask::buildStorage() {
  using namespace storeview;
  lv_obj_t* body = newScreen("Storage", true);
  s_sd_bar = s_sd_lbl = s_status = s_clear_lbl = s_live_lbl = s_live_info = nullptr;
  s_fmt_overlay = s_fmt_go_lbl = nullptr;   // went with the old screen
  memset(s_cat_val, 0, sizeof(s_cat_val));

  lv_obj_t* g = group(body, "SD CARD");
  if (!lvport::mountStorage()) {
    infoRow(g, "Card", "None", theme::TEXT_MUTED);
  } else {
    s_sd_bar = segmentBar(g, &s_sd_lbl);
    lv_label_set_text(s_sd_lbl, "Reading the card...");
    for (int c = 0; c < C_COUNT; c++) s_cat_val[c] = catRow(g, c);   // what takes the space, keyed to the bar
    s_status = infoRow(g, "Files", "");
    s_sd_read = false;   // read in pollStorage(): the first read of a big card takes a moment
    startWalk();
  }
  actionRow(g, LV_SYMBOL_WARNING "  Format card", onFormatAsk, NULL, theme::FAIL);   // also one the device can't read

  g = group(body, "DEVICE");
  lv_obj_t* flbl;
  lv_obj_t* fbar = usageBar(g, &flbl);
  uint64_t ft = 0, fu = 0;
  if (lvport::flashInfo(ft, fu)) {
    if (fu == 0) fu = treeBytes(lvport::FLASH_ROOT);   // the sim only counts files
    showUsage(fbar, flbl, ft, fu);
  } else {
    lv_label_set_text(flbl, "Not available");
  }
  groupNote(body, "Contacts, channels, settings, saved trail, waypoints.");

  g = group(body, "MESSAGES");
  int keep_idx = histstore::KEEP_DEFAULT;
  for (int i = 0; i < histstore::KEEP_COUNT; i++) if (histstore::KEEP[i] == s_archive.keep()) keep_idx = i;
  char hint[48];
  snprintf(hint, sizeof(hint), "Newest on the card, %u KB per 100",
           (unsigned)((sizeof(ChHistEntry) * 100 + 512) / 1024));
  choiceRow(g, "Kept per chat", hint, histstore::KEEP_OPTS, keep_idx, onHistKeep);
  s_clear_lbl = actionRow(g, LV_SYMBOL_TRASH "  Delete message history", onHistClear, NULL, theme::FAIL);

  g = group(body, "LIVE MAP TILES");
  int cap_idx = mapview::LIVE_CAP_DEFAULT;
  for (int i = 0; i < mapview::LIVE_CAP_COUNT; i++)
    if (((uint64_t)mapview::LIVE_CAP_MB[i] << 20) == mapview::s_live_cache.limit()) cap_idx = i;
  s_live_info = infoRow(g, "Fetched while browsing", "");
  choiceRow(g, "Keep up to", "Past it, the oldest go first", mapview::LIVE_CAP_OPTS, cap_idx, onLiveCap);
  s_live_lbl = actionRow(g, LV_SYMBOL_TRASH "  Delete live map tiles", onLiveClear, NULL, theme::FAIL);
  groupNote(body, "Downloaded areas stay.");
  s_shown_ms = 0;
}

void UITask::pollStorage() {
  using namespace storeview;
  if (s_sd_bar && !s_sd_read) {
    s_sd_read = true;
    s_sd_ok = lvport::sdInfo(s_sd_total, s_sd_used, s_sd_card);
    if (!s_sd_ok) setText(s_sd_lbl, "Can't read the card");
    else if (s_sd_card > s_sd_total + s_sd_total / 4) {   // most of the card sits outside the partition
      char c[16], t[16], line[160];
      fmtBytes(c, sizeof(c), s_sd_card);
      fmtBytes(t, sizeof(t), s_sd_total);
      snprintf(line, sizeof(line), "The card is %s but its FAT partition only %s. "
               "Format it as one FAT32 partition on a computer to use all of it.", c, t);
      noteLabel(lv_obj_get_parent(s_sd_bar), line, THEME_FONT_SMALL, theme::ACCENT);
    }
  }
  if (s_walking) stepWalk(12);
  if (millis() - s_shown_ms < 300 && s_walking) return;   // labels a few times a second
  s_shown_ms = millis();
  refreshSizes();
  if (s_sd_ok) {
    uint64_t used = s_sd_used;
    if (used == 0) for (int c = 0; c < C_COUNT; c++) used += s_bytes[c];   // the sim counts files
    showUsage(s_sd_bar, s_sd_lbl, s_sd_total, used);
  }
  if (s_live_info) {
    char b[16], t[96];
    fmtBytes(b, sizeof(b), mapview::s_live_cache.bytes());
    if (mapview::s_live_cache.clearing()) snprintf(t, sizeof(t), "Deleting... %lu left", (unsigned long)mapview::s_live_cache.count());
    else snprintf(t, sizeof(t), "%lu tiles, %s", (unsigned long)mapview::s_live_cache.count(), b);
    setText(s_live_info, t);
  }
}

void UITask::storageKeep(int idx) {
  if (idx < 0 || idx >= histstore::KEEP_COUNT) return;
  lvport::saveHistKeep(idx);
  s_archive.setKeep(histstore::KEEP[idx]);
  s_archive.applyKeep();   // trims (or makes room in) every file now
  storeview::startWalk();
}

void UITask::storageClearHistory() {
  using namespace storeview;
  if (!tapConfirmed(s_clear_lbl, LV_SYMBOL_TRASH "  Tap again to delete")) return;
  s_archive.clearAll();
  _core->history.clearAll();
  _core->markAllRead();
  showToast("Message history deleted");
  startWalk();
}

void UITask::storageLiveCap(int idx) {
  if (idx < 0 || idx >= mapview::LIVE_CAP_COUNT) return;
  lvport::saveLiveCap(idx);
  mapview::s_live_cache.setLimit((uint64_t)mapview::LIVE_CAP_MB[idx] << 20);   // trimmed in the background
}

void UITask::storageClearLive() {
  using namespace storeview;
  if (!tapConfirmed(s_live_lbl, LV_SYMBOL_TRASH "  Tap again to delete")) return;
  mapview::s_live_cache.clearAll();   // a few files per loop (mapDownloadTick)
  showToast("Deleting live map tiles");
}

// Format card, confirmed twice: the popup says what goes, then its Format
// button wants a second tap. The card is formatted there and then (the loop
// blocks: a big card takes a while), and the device restarts.
static lv_obj_t* fmtButton(lv_obj_t* row, const char* text, uint32_t bg, bool go) {
  lv_obj_t* b = lv_button_create(row);
  lv_obj_set_height(b, 38);
  lv_obj_set_flex_grow(b, 1);
  lv_obj_set_style_shadow_width(b, 0, 0);
  lv_obj_set_style_radius(b, theme::RADIUS, 0);
  lv_obj_set_style_bg_color(b, lv_color_hex(bg), 0);
  lv_obj_add_event_cb(b, onFormatTap, LV_EVENT_CLICKED, go ? (void*)1 : nullptr);
  lv_obj_t* l = label(b, text, THEME_FONT_BODY, theme::TEXT);
  lv_obj_center(l);
  return l;
}

void UITask::storageFormatAsk() {
  using namespace storeview;
  if (s_fmt_overlay) return;
  lv_obj_t* panel = popupOpen(screen(), POP_FIT, s_fmt_overlay);
  lv_obj_set_style_pad_row(panel, 8, 0);
  label(panel, "Format the SD card?", THEME_FONT_TITLE, theme::TEXT);
  noteLabel(panel, "Everything on it is erased: map areas, live map tiles, message history, "
                   "trails and screenshots. The device restarts afterwards.");
  lv_obj_t* row = flexBox(panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_width(row, LV_PCT(100));
  lv_obj_set_style_pad_column(row, theme::GAP, 0);
  fmtButton(row, "Cancel", theme::SURFACE_2, false);
  s_fmt_go_lbl = fmtButton(row, "Format", theme::FAIL, true);
}

void UITask::storageFormatTap(bool go) {
  using namespace storeview;
  if (!go) {
    if (s_fmt_overlay) lv_obj_delete_async(s_fmt_overlay);
    s_fmt_overlay = s_fmt_go_lbl = nullptr;
    return;
  }
  if (!tapConfirmed(s_fmt_go_lbl, "Tap again")) return;   // the second confirmation
  if (s_fmt_go_lbl) lv_label_set_text(s_fmt_go_lbl, "Formatting...");
  lv_refr_now(NULL);   // shown before the loop blocks
  stopWalk();
  if (mapview::s_dl.active()) mapDownloadStop();
  bool ok = lvport::formatCard();
  if (!ok) {
    if (s_fmt_overlay) lv_obj_delete_async(s_fmt_overlay);
    s_fmt_overlay = s_fmt_go_lbl = nullptr;
    showToast("Couldn't format - is a card in?", 3000);
    return;
  }
  if (s_fmt_overlay) { lv_obj_delete(s_fmt_overlay); s_fmt_overlay = s_fmt_go_lbl = nullptr; }
  restartScreen("The SD card is formatted.");
  showStorage();   // the sim goes on: the emptied card
}

// ── The card as a USB drive ──
// Plugged into a computer, the device asks whether to lend it the SD card
// (only the card: charging and USB serial carry on either way). Lent, a panel stays up over every screen until the computer ejects
// the drive (or the cable comes out); then the device restarts to reload what
// it keeps from the card. lvport's usbDrive*() do the USB side.
namespace usbview {
static lv_obj_t* s_overlay = nullptr;
static bool s_host = false;   // a computer seen on USB
}

static void onUsbTap(lv_event_t* e) { s_ui->usbTap(lv_event_get_user_data(e) != nullptr); }

static void usbPanel(const char* title, const char* text, bool buttons) {
  using namespace usbview;
  if (s_overlay) lv_obj_delete(s_overlay);
  lv_obj_t* panel = popupOpen(lv_layer_top(), POP_FIT, s_overlay);
  lv_obj_set_style_pad_row(panel, 8, 0);
  lv_obj_t* row = flexBox(panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_style_pad_column(row, 8, 0);
  lv_obj_set_flex_align(row, LV_FLEX_ALIGN_START, LV_FLEX_ALIGN_CENTER, LV_FLEX_ALIGN_CENTER);
  label(row, LV_SYMBOL_USB, THEME_FONT_TITLE, theme::ACCENT);
  label(row, title, THEME_FONT_TITLE, theme::TEXT);
  noteLabel(panel, text);
  if (!buttons) return;
  lv_obj_t* br = flexBox(panel, LV_FLEX_FLOW_ROW);
  lv_obj_set_width(br, LV_PCT(100));
  lv_obj_set_style_pad_column(br, theme::GAP, 0);
  for (int i = 0; i < 2; i++) {
    lv_obj_t* b = lv_button_create(br);
    lv_obj_set_height(b, 38);
    lv_obj_set_flex_grow(b, 1);
    lv_obj_set_style_shadow_width(b, 0, 0);
    lv_obj_set_style_radius(b, theme::RADIUS, 0);
    lv_obj_set_style_bg_color(b, lv_color_hex(i ? theme::ACCENT : theme::SURFACE_2), 0);
    lv_obj_add_event_cb(b, onUsbTap, LV_EVENT_CLICKED, i ? (void*)1 : nullptr);
    lv_obj_center(label(b, i ? "USB drive" : "Keep card", THEME_FONT_BODY, i ? theme::BG : theme::TEXT));
  }
}

namespace splash { static bool up(); }   // Splash.h

void UITask::usbPoll() {
  using namespace usbview;
  static uint32_t next = 0;
  if ((int32_t)(millis() - next) < 0) return;
  if (splash::up()) return;   // a cable in at boot: the popup comes once the splash is gone
  next = millis() + 250;
  if (lvport::usbDriveOn()) {
    if (!lvport::usbDriveDone()) return;
    lvport::usbDriveStop();
    if (s_overlay) { lv_obj_delete(s_overlay); s_overlay = nullptr; }
    wake();
    restartScreen("The computer has given the SD card back.");
    return;
  }
  // Behind a screen PIN the card (message history) isn't offered: a cable
  // plugged in while locked is noticed once the PIN is entered.
  if (locked() && pinSet()) return;
  bool host = lvport::usbHost();
  if (host == s_host) return;
  s_host = host;
  if (host) {   // just plugged into a computer
    wake();
    usbPanel("Connected to a computer",
             "Lend the SD card to the computer as a USB drive? Until you eject it there, the device "
             "can't use the card (maps, message history). Charging and USB work either way.", true);
  } else if (s_overlay) {   // unplugged before choosing
    lv_obj_delete(s_overlay);
    s_overlay = nullptr;
  }
}

void UITask::usbTap(bool drive) {
  using namespace usbview;
  if (s_overlay) { lv_obj_delete_async(s_overlay); s_overlay = nullptr; }
  if (!drive || (locked() && pinSet())) return;
  storeview::stopWalk();
  if (mapview::s_dl.active()) mapDownloadStop();
  if (!lvport::usbDriveStart()) { showToast("No SD card to share", 3000); return; }
  usbPanel("USB drive",
           "The computer has the SD card now. Eject it there (or unplug the cable) when you're done - "
           "the device then restarts.", false);
}
