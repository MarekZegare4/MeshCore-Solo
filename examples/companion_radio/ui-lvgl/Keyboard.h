#pragma once
// Phone-style on-screen keyboard on top of lv_keyboard (USER_1 mode):
//  - one letter layout per script (Latin QWERTY, Cyrillic ЙЦУКЕН, Greek);
//    which scripts are offered comes from the same prefs ui-new uses
//    (NodePrefs::keyboard_main_alphabet / keyboard_alt_alphabet). The globe
//    key switches between the two when they differ.
//  - hold a key for its variants (ą ć ę … / ё є ї … / ά έ … / punctuation
//    behind '.'), from the shared table in ui-core/KeyboardData.h. The popup
//    opens above the key with the variant last picked for it first, already
//    marked: let go to type it, or slide along the row to another. Let go
//    away from the row and the popup stays: tap a variant, or elsewhere to
//    close it.
//  - every letter row as wide as the longest one: a shorter row's end keys
//    take the rest (as a phone's 'a' and 'l'), so the columns line up;
//    the space bar takes what the bottom row has left. A comma beside it.
//  - shift for the next letter, tapped twice quickly for caps lock (the key
//    filled); backspace held deletes letters, then whole words.
//  - digits / symbols page behind "1#", the rest of ASCII
//    (brackets, \ | ~ ^ ` $ …) on a second page behind "#+="; the keyboard key
//    closes it (LV_EVENT_CANCEL: the compose bar keeps its text, a dialog
//    is left without saving).
// Letters type on release (popover keys), so a long press never types the
// base letter first.
//
// Single-TU fragment: included by ui-lvgl/UITask.cpp only.

namespace kb {

#define KB_BS  LV_SYMBOL_BACKSPACE
#define KB_OK  LV_SYMBOL_OK
#define KB_SH  LV_SYMBOL_UP
#define KB_HIDE LV_SYMBOL_KEYBOARD   // closes it: lv_keyboard_def_event_cb sends LV_EVENT_CANCEL

static const char* const K_SYM   = "1#";
static const char* const K_BACK  = "abc";
static const char* const K_SYM2  = "#+=";   // symbols page 1 -> 2
static const char* const K_SYM1  = "123";   // symbols page 2 -> 1
static const char* const K_GLOBE = "\xEF\x82\xAC";   // FontAwesome globe (U+F0AC) -- see fonts/generate.sh

static const char* const LAT[] = {
  "q","w","e","r","t","y","u","i","o","p","\n",
  "a","s","d","f","g","h","j","k","l","\n",
  KB_SH,"z","x","c","v","b","n","m",KB_BS,"\n",
  nullptr };
static const char* const LAT_UP[] = {
  "Q","W","E","R","T","Y","U","I","O","P","\n",
  "A","S","D","F","G","H","J","K","L","\n",
  KB_SH,"Z","X","C","V","B","N","M",KB_BS,"\n",
  nullptr };
static const char* const CYR[] = {
  "й","ц","у","к","е","н","г","ш","щ","з","х","\n",
  "ф","ы","в","а","п","р","о","л","д","ж","э","\n",
  KB_SH,"я","ч","с","м","и","т","ь","б","ю",KB_BS,"\n",
  nullptr };
static const char* const CYR_UP[] = {
  "Й","Ц","У","К","Е","Н","Г","Ш","Щ","З","Х","\n",
  "Ф","Ы","В","А","П","Р","О","Л","Д","Ж","Э","\n",
  KB_SH,"Я","Ч","С","М","И","Т","Ь","Б","Ю",KB_BS,"\n",
  nullptr };
static const char* const GRK[] = {
  "ς","ε","ρ","τ","υ","θ","ι","ο","π","\n",
  "α","σ","δ","φ","γ","η","ξ","κ","λ","\n",
  KB_SH,"ζ","χ","ψ","ω","β","ν","μ",KB_BS,"\n",
  nullptr };
static const char* const GRK_UP[] = {
  "Σ","Ε","Ρ","Τ","Υ","Θ","Ι","Ο","Π","\n",
  "Α","Σ","Δ","Φ","Γ","Η","Ξ","Κ","Λ","\n",
  KB_SH,"Ζ","Χ","Ψ","Ω","Β","Ν","Μ",KB_BS,"\n",
  nullptr };
static const char* const SYM[] = {
  "1","2","3","4","5","6","7","8","9","0","\n",
  "-","/",":",";","(",")","€","&","@","\"","\n",
  ".",",","?","!","'","#","%","+","=","_",KB_BS,"\n",
  nullptr };
static const char* const SYM2[] = {
  "[","]","{","}","<",">","^","~","`","|","\n",
  "\\","$","£","¥","§","°","±","×","÷","•","\n",
  ".",",","?","!","'","\"","…","«","»","–",KB_BS,"\n",
  nullptr };

// Layout ids; letter layouts in (lower, upper) pairs, in NodePrefs
// KB_ALPHABET_* order (Latin, Cyrillic, Greek) so script * 2 = its lower layout.
enum : uint8_t { L_LAT, L_LAT_UP, L_CYR, L_CYR_UP, L_GRK, L_GRK_UP, L_PAIRED_END = L_GRK_UP,
                 L_SYM, L_SYM2, L_COUNT };
static const char* const* const ROWS[L_COUNT] = { LAT, LAT_UP, CYR, CYR_UP, GRK, GRK_UP, SYM, SYM2 };

// Full lv_keyboard maps: the rows above + a bottom row assembled in
// buildMaps() (globe only when a second script is enabled).
static const int MAX_KEYS = 44;
static const char* (*s_maps)[MAX_KEYS + 8] = psramBuf<const char*[MAX_KEYS + 8]>(L_COUNT);   // in PSRAM
static lv_buttonmatrix_ctrl_t (*s_ctrl)[MAX_KEYS] = psramBuf<lv_buttonmatrix_ctrl_t[MAX_KEYS]>(L_COUNT);
static uint8_t   s_layout = L_LAT;
static uint8_t   s_script = 0;          // current script (NodePrefs KB_ALPHABET_*)
static NodePrefs* s_prefs = nullptr;
static bool      s_suppress_release = false;   // a long press opened the popup: don't type the base
static lv_obj_t* s_popup = nullptr;    // overlay (on lv_layer_top) holding the variants row
static lv_obj_t* s_popup_kbd = nullptr;

static uint8_t mainScript() {
  uint8_t s = s_prefs ? s_prefs->keyboard_main_alphabet : 0;
  return s < NodePrefs::KB_ALPHABET_COUNT ? s : 0;
}
static uint8_t altScript() {
  uint8_t s = s_prefs ? s_prefs->keyboard_alt_alphabet : 0;
  return s < NodePrefs::KB_ALPHABET_COUNT ? s : 0;
}
static bool hasAlt() { return altScript() != mainScript(); }

static bool isSpecial(const char* t) {
  static const char* const SP[] = { KB_SH, KB_BS, KB_OK, KB_HIDE, K_SYM, K_BACK, K_GLOBE, K_SYM2, K_SYM1 };
  for (const char* s : SP) if (strcmp(t, s) == 0) return true;
  return false;
}

// Assemble the maps and derive key widths / styles from the labels: letters
// narrow, typed on release with a magnified popover and no auto-repeat;
// special keys wider and highlighted; space widest.
static void buildMaps() {
  bool alt = hasAlt();
  for (int l = 0; l < L_COUNT; l++) {
    int m = 0;
    for (const char* const* p = ROWS[l]; *p; p++) s_maps[l][m++] = *p;
    if (l >= L_SYM) {
      s_maps[l][m++] = KB_HIDE; s_maps[l][m++] = K_BACK;
      s_maps[l][m++] = l == L_SYM ? K_SYM2 : K_SYM1; s_maps[l][m++] = " ";
      s_maps[l][m++] = "*";    s_maps[l][m++] = KB_OK;
    } else {
      s_maps[l][m++] = KB_HIDE; s_maps[l][m++] = K_SYM;
      if (alt) s_maps[l][m++] = K_GLOBE;
      s_maps[l][m++] = ",";    s_maps[l][m++] = " "; s_maps[l][m++] = "."; s_maps[l][m++] = KB_OK;
    }
    s_maps[l][m] = "";

    // Widths, row by row: letters 2 units, special keys 3 (the keyboard and
    // globe keys 2 beside the letters), the space bar what its row has left.
    const bool letters = l <= L_PAIRED_END;
    struct Row { int first, end, units, fl, ll; } rows[6];
    int nrows = 0, k = 0, space_k = -1, target = 0;
    Row cur = { 0, 0, 0, -1, -1 };
    for (const char* const* p = s_maps[l];; p++) {
      if (!**p || strcmp(*p, "\n") == 0) {
        cur.end = k;
        if (nrows < 6) rows[nrows++] = cur;
        if (space_k < 0 && cur.units > target) target = cur.units;   // the rows above the space bar's
        cur = { k, k, 0, -1, -1 };
        if (!**p) break;
        continue;
      }
      uint32_t c;
      const bool narrow = letters && (strcmp(*p, KB_HIDE) == 0 || strcmp(*p, K_GLOBE) == 0);
      if (strcmp(*p, " ") == 0)          { c = 0; space_k = k; }
      else if (strcmp(*p, KB_BS) == 0)   c = 3 | LV_BUTTONMATRIX_CTRL_CHECKED;   // keeps auto-repeat
      else if (isSpecial(*p))            c = (narrow ? 2 : 3) | LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
      else {
        c = 2 | LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
        if (cur.fl < 0) cur.fl = k;
        cur.ll = k;
      }
      cur.units += c & 0x000F;   // the width (lv_buttonmatrix.c: LV_BUTTONMATRIX_WIDTH_MASK)
      if (k < MAX_KEYS) s_ctrl[l][k] = (lv_buttonmatrix_ctrl_t)c;
      k++;
    }
    for (int r = 0; r < nrows; r++) {
      const Row& w = rows[r];
      int spare = target - w.units;
      if (space_k >= w.first && space_k < w.end && space_k < MAX_KEYS) {
        s_ctrl[l][space_k] = (lv_buttonmatrix_ctrl_t)(s_ctrl[l][space_k] | (spare < 4 ? 4 : spare > 15 ? 15 : spare));
      } else if (letters && spare > 0 && w.fl >= 0 && w.ll < MAX_KEYS) {   // a short letter row: its end keys wider
        s_ctrl[l][w.fl] = (lv_buttonmatrix_ctrl_t)(s_ctrl[l][w.fl] + (spare + 1) / 2);
        s_ctrl[l][w.ll] = (lv_buttonmatrix_ctrl_t)(s_ctrl[l][w.ll] + spare / 2);
      }
    }
  }
}

// Label of button `id` (buttons numbered without the "\n" row breaks).
static const char* keyText(uint8_t layout, uint32_t id) {
  uint32_t k = 0;
  for (const char* const* p = s_maps[layout]; **p; p++) {
    if (strcmp(*p, "\n") == 0) continue;
    if (k++ == id) return *p;
  }
  return "";
}

// Shift: one tap for the next letter, two in quick succession lock it (caps
// lock) until shift is tapped again or the letters page is left. The shift
// key shows which: its arrow in the accent colour, locked the key filled.
static const uint32_t CAPS_TAP_MS = 350;
static bool     s_caps = false;
static uint32_t s_shift_ms = 0;
static int      s_shift_id = -1;   // the shift key's button index on the layout shown

static bool isUpper(uint8_t layout) { return layout <= L_PAIRED_END && (layout & 1); }

static void apply(lv_obj_t* kbd, uint8_t layout) {
  s_layout = layout;
  if (!isUpper(layout)) s_caps = false;
  s_shift_id = -1;
  uint32_t k = 0;
  for (const char* const* p = s_maps[layout]; **p; p++) {
    if (strcmp(*p, "\n") == 0) continue;
    if (strcmp(*p, KB_SH) == 0) { s_shift_id = (int)k; break; }
    k++;
  }
  lv_keyboard_set_map(kbd, LV_KEYBOARD_MODE_USER_1, s_maps[layout], s_ctrl[layout]);
  lv_keyboard_set_mode(kbd, LV_KEYBOARD_MODE_USER_1);
}

static void releaseShift(lv_obj_t* kbd) {
  if (isUpper(s_layout) && !s_caps) apply(kbd, s_layout ^ 1);
}

static void onShift(lv_obj_t* kbd) {
  if (s_layout > L_PAIRED_END) return;
  const uint32_t now = lv_tick_get();
  if (s_caps) {
    apply(kbd, s_layout ^ 1);                  // locked: off
  } else if (isUpper(s_layout) && now - s_shift_ms < CAPS_TAP_MS) {
    s_caps = true;                             // the second tap: locked
    lv_obj_invalidate(kbd);
  } else {
    apply(kbd, s_layout ^ 1);
  }
  s_shift_ms = now;
}

static void onDrawTask(lv_event_t* e) {
  if (s_shift_id < 0 || !isUpper(s_layout)) return;
  lv_draw_task_t* t = lv_event_get_draw_task(e);
  const lv_draw_dsc_base_t* b = (const lv_draw_dsc_base_t*)lv_draw_task_get_draw_dsc(t);
  if (!b || b->part != LV_PART_ITEMS || (int)b->id1 != s_shift_id) return;
  if (lv_draw_fill_dsc_t* f = lv_draw_task_get_fill_dsc(t)) {
    if (s_caps) f->color = lv_color_hex(theme::ACCENT);
  }
  if (lv_draw_label_dsc_t* l = lv_draw_task_get_label_dsc(t)) l->color = lv_color_hex(s_caps ? theme::BG : theme::ACCENT);
}

// Backspace held: a letter at a time, then -- after BS_WORDS_AFTER of them --
// a word at a time, more slowly (every third auto-repeat), as on a phone.
static const uint32_t BS_WORDS_AFTER = 12;
static uint32_t s_bs_count = 0;

static bool beforeCursorIsSpace(lv_obj_t* ta) {
  uint32_t pos = lv_textarea_get_cursor_pos(ta);
  char c[8];
  ::kbd::utf8At(lv_textarea_get_text(ta), (int)pos - 1, c);
  return c[0] == ' ' || c[0] == '\n';
}
// The spaces before the cursor, then the word before them.
static void deleteWord(lv_obj_t* ta) {
  while (lv_textarea_get_cursor_pos(ta) > 0 && beforeCursorIsSpace(ta)) lv_textarea_delete_char(ta);
  while (lv_textarea_get_cursor_pos(ta) > 0 && !beforeCursorIsSpace(ta)) lv_textarea_delete_char(ta);
}


// ── Variants popup ───────────────────────────────────────────────────────────
static char        s_var_txt[12][8];     // as shown (uppercase while shifted)
static char        s_var_low[12][8];     // lowercase, as remembered
static const char* s_var_map[13];
static lv_obj_t*   s_var_m = nullptr;    // the popup's row of variants
static int         s_var_n = 0;
static int         s_var_sel = -1;       // marked: letting go of the finger that opened it types it
static char        s_var_base[8];        // the key it is for (lowercase)

// The variant picked last for each base key, first in its popup next time.
// Until a restart; a handful of keys are all anyone uses.
struct Recent { char base[8]; char pick[8]; };
static Recent  s_recent[12];
static uint8_t s_recent_next = 0;
static const char* recentFor(const char* base) {
  for (const Recent& r : s_recent) if (r.base[0] && !strcmp(r.base, base)) return r.pick;
  return nullptr;
}
static void recentSet(const char* base, const char* pick) {
  Recent* r = nullptr;
  for (Recent& x : s_recent) if (x.base[0] && !strcmp(x.base, base)) r = &x;
  if (!r) {
    r = &s_recent[s_recent_next];
    s_recent_next = (s_recent_next + 1) % (sizeof(s_recent) / sizeof(s_recent[0]));
    snprintf(r->base, sizeof(r->base), "%s", base);
  }
  snprintf(r->pick, sizeof(r->pick), "%s", pick);
}

// The cursor is only drawn while the field is FOCUSED, and the popup (a touch
// on another object) can take that state away: put it back after a pick.
static void keepCursor(lv_obj_t* ta) {
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  lv_textarea_set_cursor_pos(ta, lv_textarea_get_cursor_pos(ta));   // restarts the blink (see fieldFocus)
  lv_obj_invalidate(ta);
}

static void closePopup() {
  if (s_popup) { lv_obj_delete_async(s_popup); s_popup = nullptr; }
  s_var_m = nullptr;
  s_var_sel = -1;
}

static void typeVariant(int id) {
  if (!s_popup_kbd || !s_var_m || id < 0 || id >= s_var_n) return;
  lv_obj_t* ta = lv_keyboard_get_textarea(s_popup_kbd);
  if (ta) {
    lv_textarea_add_text(ta, s_var_txt[id]);
    keepCursor(ta);
  }
  recentSet(s_var_base, s_var_low[id]);
  releaseShift(s_popup_kbd);
  closePopup();
}

static void onPopupPick(lv_event_t* e) {
  uint32_t id = lv_buttonmatrix_get_selected_button((lv_obj_t*)lv_event_get_target(e));
  if (id != LV_BUTTONMATRIX_BUTTON_NONE) typeVariant((int)id);
}

// The variant under the finger still down (LVGL keeps it pressing the
// keyboard): the one under it along the row, none once it has gone well
// above or below the row.
static void markVariant(int sel) {
  if (!s_var_m || sel == s_var_sel) return;
  if (s_var_sel >= 0) lv_buttonmatrix_clear_button_ctrl(s_var_m, s_var_sel, LV_BUTTONMATRIX_CTRL_CHECKED);
  if (sel >= 0) lv_buttonmatrix_set_button_ctrl(s_var_m, sel, LV_BUTTONMATRIX_CTRL_CHECKED);
  s_var_sel = sel;
}
static int variantAt(const lv_point_t& pt) {
  lv_area_t a;
  lv_obj_get_coords(s_var_m, &a);
  if (pt.y < a.y1 - 40 || pt.y > a.y2 + 40 || s_var_n <= 0) return -1;
  int w = lv_area_get_width(&a);
  int x = pt.x < a.x1 ? 0 : pt.x > a.x2 ? w - 1 : pt.x - a.x1;
  int i = w > 0 ? x * s_var_n / w : 0;
  return i < 0 ? 0 : i >= s_var_n ? s_var_n - 1 : i;
}

static void onPopupOutside(lv_event_t* e) { (void)e; closePopup(); }

static void openPopup(lv_obj_t* kbd, const char* base, const char* variants, bool upper) {
  closePopup();
  snprintf(s_var_base, sizeof(s_var_base), "%s", base);
  // The last pick first, then the rest in the table's order.
  const char* recent = recentFor(base);
  int n = 0;
  if (recent) snprintf(s_var_low[n++], sizeof(s_var_low[0]), "%s", recent);
  int all = ::kbd::utf8Len(variants);
  for (int i = 0; i < all && n < 12; i++) {
    char one[8];
    ::kbd::utf8At(variants, i, one);
    if (recent && !strcmp(one, recent)) continue;
    memcpy(s_var_low[n++], one, sizeof(one));
  }
  for (int i = 0; i < n; i++) {
    if (upper) ::kbd::toUpperUtf8(s_var_low[i], s_var_txt[i], sizeof(s_var_txt[i]));
    else       memcpy(s_var_txt[i], s_var_low[i], sizeof(s_var_txt[i]));
    s_var_map[i] = s_var_txt[i];
  }
  s_var_map[n] = "";
  s_var_n = n;

  // Full-screen transparent overlay: a tap outside the row closes the popup.
  s_popup = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(s_popup);
  lv_obj_set_size(s_popup, LV_PCT(100), LV_PCT(100));
  lv_obj_add_flag(s_popup, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(s_popup, onPopupOutside, LV_EVENT_CLICKED, NULL);
  s_popup_kbd = kbd;

  lv_obj_t* m = lv_buttonmatrix_create(s_popup);
  lv_buttonmatrix_set_map(m, s_var_map);
  const int key_w = 30;
  int w = n * key_w + 8;
  lv_obj_set_size(m, w, 44);
  lv_obj_set_style_pad_all(m, 3, 0);
  lv_obj_set_style_pad_gap(m, 2, 0);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::SURFACE_2), 0);
  lv_obj_set_style_border_color(m, lv_color_hex(theme::ACCENT), 0);
  lv_obj_set_style_border_width(m, 1, 0);
  lv_obj_set_style_radius(m, theme::RADIUS, 0);
  lv_obj_set_style_text_font(m, THEME_FONT_LARGE, LV_PART_ITEMS);
  lv_obj_set_style_bg_color(m, lv_color_hex(theme::ACCENT), LV_PART_ITEMS | LV_STATE_CHECKED);
  lv_obj_set_style_text_color(m, lv_color_hex(theme::BG), LV_PART_ITEMS | LV_STATE_CHECKED);

  // Above the pressed key, but never above the keyboard's own top edge: the
  // compose field sits right there and must stay readable while picking.
  lv_point_t pt = { 0, 0 };
  lv_indev_t* indev = lv_indev_active();
  if (indev) lv_indev_get_point(indev, &pt);
  lv_area_t kb_area;
  lv_obj_get_coords(kbd, &kb_area);
  int scr_w = lv_display_get_horizontal_resolution(NULL);
  int x = pt.x - key_w / 2 - 3;   // the first variant right over the finger
  if (x < 2) x = 2;
  if (x + w > scr_w - 2) x = scr_w - 2 - w;
  int y = pt.y - 60;
  if (y < kb_area.y1) y = kb_area.y1;
  lv_obj_set_pos(m, x, y);
  lv_obj_add_event_cb(m, onPopupPick, LV_EVENT_VALUE_CHANGED, NULL);
  s_var_m = m;
  lv_obj_update_layout(m);
  markVariant(variantAt(pt));
}

// ── Events ───────────────────────────────────────────────────────────────────
static void onLongPress(lv_event_t* e) {
  lv_obj_t* kbd = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_keyboard_get_selected_button(kbd);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
  bool upper = s_layout <= L_PAIRED_END && (s_layout & 1);
  uint8_t lower = upper ? (uint8_t)(s_layout ^ 1) : s_layout;
  const char* variants = ::kbd::variantsFor(keyText(lower, id));
  if (!variants) return;              // plain key: the release types it as usual
  s_suppress_release = true;
  openPopup(kbd, keyText(lower, id), variants, upper);
}

// The finger that opened the popup, sliding along it and letting go.
static void onPressing(lv_event_t* e) {
  if (!s_var_m) return;
  lv_point_t pt;
  lv_indev_get_point(lv_event_get_indev(e), &pt);
  markVariant(variantAt(pt));
}
static void onReleased(lv_event_t* e) {
  (void)e;
  // Runs after lv_keyboard's own release: a finger that slid off its key
  // sends no VALUE_CHANGED, and the flag left set would eat the next key.
  s_suppress_release = false;
  s_bs_count = 0;   // (not on PRESSED: lv_keyboard types the first one before that reaches us)
  if (s_var_sel >= 0) typeVariant(s_var_sel);
}

static void onEvent(lv_event_t* e) {
  lv_obj_t* kbd = (lv_obj_t*)lv_event_get_target(e);
  if (s_suppress_release) { s_suppress_release = false; return; }
  uint32_t id = lv_keyboard_get_selected_button(kbd);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
  const char* t = lv_keyboard_get_button_text(kbd, id);
  if (!t) return;

  if (strcmp(t, KB_SH) == 0)   { onShift(kbd); return; }
  if (strcmp(t, KB_BS) == 0 && ++s_bs_count > BS_WORDS_AFTER) {
    lv_obj_t* ta = lv_keyboard_get_textarea(kbd);
    if (ta && (s_bs_count - BS_WORDS_AFTER) % 3 == 1) { deleteWord(ta); keepCursor(ta); }
    return;
  }
  if (strcmp(t, K_SYM) == 0 || strcmp(t, K_SYM1) == 0) { apply(kbd, L_SYM); return; }
  if (strcmp(t, K_SYM2) == 0)  { apply(kbd, L_SYM2); return; }
  if (strcmp(t, K_BACK) == 0)  { apply(kbd, s_script * 2); return; }
  if (strcmp(t, K_GLOBE) == 0) {
    s_script = (s_script == mainScript()) ? altScript() : mainScript();
    apply(kbd, s_script * 2);
    return;
  }
  lv_keyboard_def_event_cb(e);    // type the character / backspace / OK
  // OK and hide put the keyboard (and the field's cursor) away themselves
  const bool closes = strcmp(t, KB_OK) == 0 || strcmp(t, KB_HIDE) == 0;
  lv_obj_t* ta = lv_keyboard_get_textarea(kbd);
  if (ta && !closes && !lv_obj_has_flag(kbd, LV_OBJ_FLAG_HIDDEN)) keepCursor(ta);
  if (strcmp(t, KB_BS) != 0) releaseShift(kbd);
}

static void onDelete(lv_event_t* e) { (void)e; closePopup(); s_popup_kbd = nullptr; }

// Create a keyboard on `parent`, opening on the main script.
static lv_obj_t* create(lv_obj_t* parent, NodePrefs* prefs) {
  s_prefs = prefs;
  buildMaps();                          // cheap; picks up a changed alphabet setting
  s_script = mainScript();
  lv_obj_t* kbd = lv_keyboard_create(parent);
  lv_obj_remove_event_cb(kbd, lv_keyboard_def_event_cb);
  lv_obj_add_event_cb(kbd, onEvent, LV_EVENT_VALUE_CHANGED, NULL);
  lv_obj_add_event_cb(kbd, onLongPress, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_add_event_cb(kbd, onPressing, LV_EVENT_PRESSING, NULL);
  lv_obj_add_event_cb(kbd, onReleased, LV_EVENT_RELEASED, NULL);
  lv_obj_add_event_cb(kbd, onDrawTask, LV_EVENT_DRAW_TASK_ADDED, NULL);
  lv_obj_add_flag(kbd, LV_OBJ_FLAG_SEND_DRAW_TASK_EVENTS);
  lv_obj_add_event_cb(kbd, onDelete, LV_EVENT_DELETE, NULL);
  lv_keyboard_set_popovers(kbd, true);
  // A finger rolling on a key is still that key: no scroll or gesture taken up the tree
  lv_obj_remove_flag(kbd, (lv_obj_flag_t)(LV_OBJ_FLAG_SCROLL_CHAIN | LV_OBJ_FLAG_GESTURE_BUBBLE));
  apply(kbd, s_script * 2);
  return kbd;
}

}  // namespace kb
