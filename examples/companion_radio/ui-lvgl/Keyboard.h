#pragma once
// Phone-style on-screen keyboard on top of lv_keyboard (USER_1 mode):
//  - one letter layout per script (Latin QWERTY, Cyrillic ЙЦУКЕН, Greek);
//    which scripts are offered comes from the same prefs ui-new uses
//    (NodePrefs::keyboard_main_alphabet / keyboard_alt_alphabet). The globe
//    key switches between the two when they differ.
//  - hold a key for its variants (ą ć ę … / ё є ї … / ά έ … / punctuation
//    behind '.'), from the shared table in ui-core/KeyboardData.h. The popup
//    opens above the key; tap a variant to type it, tap elsewhere to close.
//  - one-shot shift; digits / symbols page behind "1#"; the keyboard key
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

// Layout ids; letter layouts in (lower, upper) pairs, in NodePrefs
// KB_ALPHABET_* order (Latin, Cyrillic, Greek) so script * 2 = its lower layout.
enum : uint8_t { L_LAT, L_LAT_UP, L_CYR, L_CYR_UP, L_GRK, L_GRK_UP, L_PAIRED_END = L_GRK_UP,
                 L_SYM, L_COUNT };
static const char* const* const ROWS[L_COUNT] = { LAT, LAT_UP, CYR, CYR_UP, GRK, GRK_UP, SYM };

// Full lv_keyboard maps: the rows above + a bottom row assembled in
// buildMaps() (globe only when a second script is enabled).
static const int MAX_KEYS = 40;
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
  static const char* const SP[] = { KB_SH, KB_BS, KB_OK, KB_HIDE, K_SYM, K_BACK, K_GLOBE };
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
    if (l == L_SYM) {
      s_maps[l][m++] = KB_HIDE; s_maps[l][m++] = K_BACK; s_maps[l][m++] = " ";
      s_maps[l][m++] = "*";    s_maps[l][m++] = KB_OK;
    } else {
      s_maps[l][m++] = KB_HIDE; s_maps[l][m++] = K_SYM;
      if (alt) s_maps[l][m++] = K_GLOBE;
      s_maps[l][m++] = " ";    s_maps[l][m++] = "."; s_maps[l][m++] = KB_OK;
    }
    s_maps[l][m] = "";

    int k = 0;
    for (const char* const* p = s_maps[l]; **p; p++) {
      if (strcmp(*p, "\n") == 0) continue;
      uint32_t c;
      if (strcmp(*p, " ") == 0)          c = 6;
      else if (strcmp(*p, KB_BS) == 0)   c = 3 | LV_BUTTONMATRIX_CTRL_CHECKED;   // keeps auto-repeat
      else if (isSpecial(*p))            c = 3 | LV_BUTTONMATRIX_CTRL_CHECKED | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
      else                               c = 2 | LV_BUTTONMATRIX_CTRL_POPOVER | LV_BUTTONMATRIX_CTRL_NO_REPEAT;
      if (k < MAX_KEYS) s_ctrl[l][k++] = (lv_buttonmatrix_ctrl_t)c;
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

static void apply(lv_obj_t* kbd, uint8_t layout) {
  s_layout = layout;
  lv_keyboard_set_map(kbd, LV_KEYBOARD_MODE_USER_1, s_maps[layout], s_ctrl[layout]);
  lv_keyboard_set_mode(kbd, LV_KEYBOARD_MODE_USER_1);
}

static void releaseShift(lv_obj_t* kbd) {
  if (s_layout <= L_PAIRED_END && (s_layout & 1)) apply(kbd, s_layout ^ 1);
}

// ── Variants popup ───────────────────────────────────────────────────────────
static char        s_var_txt[12][8];
static const char* s_var_map[13];

// The cursor is only drawn while the field is FOCUSED, and the popup (a touch
// on another object) can take that state away: put it back after a pick.
static void keepCursor(lv_obj_t* ta) {
  lv_obj_add_state(ta, LV_STATE_FOCUSED);
  lv_textarea_set_cursor_pos(ta, lv_textarea_get_cursor_pos(ta));   // restarts the blink (see fieldFocus)
  lv_obj_invalidate(ta);
}

static void closePopup() {
  if (s_popup) { lv_obj_delete_async(s_popup); s_popup = nullptr; }
}

static void onPopupPick(lv_event_t* e) {
  lv_obj_t* m = (lv_obj_t*)lv_event_get_target(e);
  uint32_t id = lv_buttonmatrix_get_selected_button(m);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE || !s_popup_kbd) return;
  lv_obj_t* ta = lv_keyboard_get_textarea(s_popup_kbd);
  if (ta) {
    lv_textarea_add_text(ta, lv_buttonmatrix_get_button_text(m, id));
    keepCursor(ta);
  }
  releaseShift(s_popup_kbd);
  closePopup();
}

static void onPopupOutside(lv_event_t* e) { (void)e; closePopup(); }

static void openPopup(lv_obj_t* kbd, const char* variants, bool upper) {
  closePopup();
  int n = ::kbd::utf8Len(variants);
  if (n > 12) n = 12;
  for (int i = 0; i < n; i++) {
    char one[8];
    ::kbd::utf8At(variants, i, one);
    if (upper) ::kbd::toUpperUtf8(one, s_var_txt[i], sizeof(s_var_txt[i]));
    else       memcpy(s_var_txt[i], one, sizeof(one));
    s_var_map[i] = s_var_txt[i];
  }
  s_var_map[n] = "";

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

  // Above the pressed key, but never above the keyboard's own top edge: the
  // compose field sits right there and must stay readable while picking.
  lv_point_t pt = { 0, 0 };
  lv_indev_t* indev = lv_indev_active();
  if (indev) lv_indev_get_point(indev, &pt);
  lv_area_t kb_area;
  lv_obj_get_coords(kbd, &kb_area);
  int scr_w = lv_display_get_horizontal_resolution(NULL);
  int x = pt.x - w / 2;
  if (x < 2) x = 2;
  if (x + w > scr_w - 2) x = scr_w - 2 - w;
  int y = pt.y - 60;
  if (y < kb_area.y1) y = kb_area.y1;
  lv_obj_set_pos(m, x, y);
  lv_obj_add_event_cb(m, onPopupPick, LV_EVENT_VALUE_CHANGED, NULL);
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
  openPopup(kbd, variants, upper);
}

static void onEvent(lv_event_t* e) {
  lv_obj_t* kbd = (lv_obj_t*)lv_event_get_target(e);
  if (s_suppress_release) { s_suppress_release = false; return; }
  uint32_t id = lv_keyboard_get_selected_button(kbd);
  if (id == LV_BUTTONMATRIX_BUTTON_NONE) return;
  const char* t = lv_keyboard_get_button_text(kbd, id);
  if (!t) return;

  if (strcmp(t, KB_SH) == 0)   { if (s_layout <= L_PAIRED_END) apply(kbd, s_layout ^ 1); return; }
  if (strcmp(t, K_SYM) == 0)   { apply(kbd, L_SYM); return; }
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
  lv_obj_add_event_cb(kbd, onDelete, LV_EVENT_DELETE, NULL);
  lv_keyboard_set_popovers(kbd, true);
  apply(kbd, s_script * 2);
  return kbd;
}

}  // namespace kb
