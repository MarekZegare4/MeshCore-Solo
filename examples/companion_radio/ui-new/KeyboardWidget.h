#pragma once

#include <helpers/ui/DisplayDriver.h>
#include <Arduino.h>
#include "PopupMenu.h"
#include "icons.h"   // mini-icons for the special-key row (⇧ ⌫ ⎵ ✓)
#include "../NodePrefs.h"

// Layout constants shared by all keyboard users.
// Two pages: letters (page 0) and symbols (page 1), toggled by the "#@"/"abc"
// special key. Space lives only on the ⎵ special key now, so the freed grid
// slot on page 0 holds the comma; punctuation is grouped as . , ! ?
static const int KB_PAGES      = 2;
static const char KB_CHARS[KB_PAGES][4][10] = {
  { // page 0 — letters + digits
    {'a','b','c','d','e','f','g','h','i','j'},
    {'k','l','m','n','o','p','q','r','s','t'},
    {'u','v','w','x','y','z','.',',','!','?'},
    {'1','2','3','4','5','6','7','8','9','0'},
  },
  { // page 1 — symbols + digits (ASCII only — one byte per key)
    {'@','#','&','*','(',')','-','_','+','='},
    {'/','\\',':',';','\'','"','<','>','[',']'},
    {'{','}','|','~','^','$','%','`',',','.'},
    {'1','2','3','4','5','6','7','8','9','0'},
  },
};
static const int KB_ROWS_CHAR  = 4;
static const int KB_COLS_CHAR  = 10;
static const int KB_SPECIAL    = 6;   // ⇧ ⎵ ⌫ {} #@/abc ✓

// T9 multi-tap layout (Settings › Keyboard). A classic phone keypad: 9 cells (keys
// 1-9) laid out 3x3, each holding a handful of letters/symbols. Repeated Enter
// presses on the same cell within KB_T9_TIMEOUT_MS cycle through the group, ending
// on the cell's own digit (computed as '1'+cell, not stored here) before wrapping.
// Keys 0/*/# aren't part of the grid — space/backspace/etc. already live on the
// special row below, shared with the ABC layout.
static const int KB_T9_ROWS = 3;
static const int KB_T9_COLS = 3;
static const uint32_t KB_T9_TIMEOUT_MS = 800;
static const char* const KB_T9_GROUPS[KB_PAGES][9] = {
  { ".,!?'-", "abc", "def", "ghi", "jkl", "mno", "pqrs", "tuv", "wxyz" },   // page 0 — letters
  { "@#&", "*()", "-_+", "=/\\", ":;'\"", "<>[]", "{}|~", "^$%`", ",." },  // page 1 — symbols
};

// Non-Latin keyboard scripts. NodePrefs::keyboard_main_alphabet/
// keyboard_alt_alphabet (Settings > Keyboard's Main/Additional rows) pick
// which script occupies page 0 (the keyboard's default/opening page) and
// which joins it as page 1 -- either can be Latin, Cyrillic, or Greek; the
// same value in both collapses to a single-script + Symbols cycle (see
// hasAltAlphabet()). Every alphabet here must fall inside the misc-fixed
// font's U+0020-04FF range (src/helpers/ui/MiscFixedFont.h) since that's what
// actually draws these glyphs on-screen.
//
// Unlike KB_CHARS (single ASCII byte per cell), these hold UTF-8 strings —
// Cyrillic is 2 bytes/codepoint — so cells are `const char*`, not `char`.
// KeyboardWidget's insertion/backspace/T9-cycle logic works in codepoints via
// the kbUtf8*() helpers below, not raw bytes, to stay correct for either.

// Cyrillic ABC grid: rows 0-2 hold the 30 letters that fit alphabetically
// (а-э); row 3 holds the remaining 3 (ё, ю, я) plus basic punctuation. No
// digit row on this page (digits are already reachable via the Symbols page,
// same as the Latin page's own row 3 duplicates them there).
static const char* const KB_CYRILLIC_CHARS[4][10] = {
  { "а","б","в","г","д","е","ж","з","и","й" },
  { "к","л","м","н","о","п","р","с","т","у" },
  { "ф","х","ц","ч","ш","щ","ъ","ы","ь","э" },
  { "ю","я","ё",".",",","!","?","-","'","\"" },
};
// Cyrillic T9 groups: the classic Russian phone-keypad distribution. Cell 0
// (digit '1') is punctuation, matching KB_T9_GROUPS' page-0 convention;
// cells 1-8 (digits '2'-'9') hold the 33 letters, 3-5 per key.
static const char* const KB_T9_GROUPS_CYRILLIC[9] = {
  ".,!?'-", "абвг", "деёжз", "ийкл", "мноп", "рсту", "фхцч", "шщъыь", "эюя"
};

// Greek ABC grid: the 24-letter modern alphabet plus final sigma (ς, used
// only at the end of a word — σ is the regular form) = 25 letters, fitting
// rows 0-2 with 5 basic punctuation marks to spare; row 3 keeps the digit row
// (unlike Cyrillic or most of the Latin-diacritic alphabets below, Greek has
// room left over in its own letter rows).
// NOTE: monotonic Modern Greek normally marks stress with a tonos accent
// (ά έ ή ί ό ύ ώ) — omitted here to keep this a single, simple page. Fine for
// informal/transliteration-style typing; flag if proper accented Greek
// composition turns out to matter and we can add a second Greek page for them.
static const char* const KB_GREEK_CHARS[4][10] = {
  { "α","β","γ","δ","ε","ζ","η","θ","ι","κ" },
  { "λ","μ","ν","ξ","ο","π","ρ","σ","ς","τ" },
  { "υ","φ","χ","ψ","ω",".",",","!","?","'" },
  { "1","2","3","4","5","6","7","8","9","0" },
};
// Greek T9 groups: cell 0 (digit '1') is punctuation; cells 1-8 (digits
// '2'-'9') split the 25 letters roughly evenly, final sigma grouped with the
// regular sigma it's a variant of.
static const char* const KB_T9_GROUPS_GREEK[9] = {
  ".,!?'-", "αβγ", "δεζ", "ηθι", "κλμ", "νξο", "πρσς", "τυφ", "χψω"
};

// Buffer cap for typed text, in bytes. Matches MeshCore's MAX_TEXT_LEN
// (10*CIPHER_BLOCK_SIZE = 160) so a full-length message can be composed; each
// field passes its own smaller max to begin() where its store is smaller.
static const int KB_MAX_LEN    = 160;

// Longest preview line we render per row, in CHARACTERS. Caps the per-line
// stack buffers so a very wide display (small font → many chars per line)
// can't overrun them.
static const int KB_PREVIEW_CAP = 46;
// The same cap in BYTES. Every alphabet this keyboard can produce fits the
// misc-fixed font's U+0020-04FF range, i.e. at most 2 UTF-8 bytes per
// codepoint -- so a full line of Cyrillic/Greek/accented text is twice as
// many bytes as it is characters.
static const int KB_PREVIEW_BYTES = KB_PREVIEW_CAP * 2;

// ── UTF-8 helpers for the alt-alphabet pages ─────────────────────────────────
// KB_CHARS/KB_T9_GROUPS content is ASCII (1 byte/char); KB_CYRILLIC_CHARS and
// KB_T9_GROUPS_CYRILLIC are UTF-8 (2 bytes/char in this range). These helpers
// let insertion, backspace and T9 cycling work in codepoints for either,
// instead of assuming 1 byte == 1 character.

// Apply Shift/caps to every codepoint in a UTF-8 string, writing the result
// (same codepoint count, each independently shifted) into `out`. Every script
// here pairs lower/uppercase differently, so each gets its own rule:
//  - ASCII a-z and Cyrillic а-я: flat -0x20 codepoint offset. ё/Ё (U+0451/
//    U+0401) break the Cyrillic pattern by 0x50 and are special-cased.
//  - Latin-1 Supplement à-þ (U+00E0-00FE, used by every Latin-diacritic
//    alphabet below — ö/ü/é/á/etc.): also a flat -0x20 offset, same as ASCII —
//    the block was designed as parallel case pairs. U+00F7 (÷, division sign)
//    sits in that numeric range but isn't a letter; excluded. ß (U+00DF) has
//    no simple uppercase in this range (its uppercase ẞ is U+1E9E, outside
//    Lemon's U+0020-04FF) — left as-is. ÿ (U+00FF, French) is the one letter
//    in this block whose uppercase Ÿ (U+0178) falls outside it entirely —
//    special-cased before the range rule.
//  - Latin Extended-A (ą/č/ĺ/œ/etc., U+0100-017F): NOT a flat offset like
//    Latin-1 — this block alternates even=uppercase/odd=lowercase in adjacent
//    pairs, so lowercase - 1 = uppercase. But the parity flips around the
//    unpaired codepoints ĸ (U+0138), ŉ (U+0149) and Ÿ (U+0178), so a single
//    "odd=lower" rule is wrong for U+0139-0148 and U+0179-017E (this broke
//    ł ń ź ż among others). Verified exhaustively over the whole
//    U+0100-U+017F block — see the four sub-ranges below; ı and ſ are the
//    only remaining exceptions and appear in no keyboard table here.
//  - Greek α-ω (U+03B1-03C9): flat -0x20 offset, same shape as Cyrillic/ASCII.
//    Final sigma ς (U+03C2) is the one exception — it has no uppercase of its
//    own; -0x20 would land on U+03A2, which is unassigned. It capitalizes to
//    regular Σ (U+03A3) instead, same as σ, and is special-cased before the
//    general range rule (03C2 falls inside 03B1-03C9, so order matters here).
// Used both for a single cell (one codepoint) and a whole T9 group label/string.
static void kbApplyCapsUtf8(const char* in, bool caps, char* out, size_t out_size) {
  size_t o = 0;
  const uint8_t* p = (const uint8_t*)in;
  while (*p && o + 2 < out_size) {
    uint32_t cp = DisplayDriver::decodeCodepoint(p);
    if (caps) {
      if (cp == 0x0451)                                    cp = 0x0401;  // ё -> Ё
      else if (cp == 0x03C2)                               cp = 0x03A3;  // ς -> Σ
      else if (cp == 0x00FF)                               cp = 0x0178;  // ÿ -> Ÿ (French; breaks the à-þ flat -0x20 rule below — Ÿ sits outside Latin-1 Supplement entirely)
      else if (cp >= 0x0430 && cp <= 0x044F)                cp -= 0x20;   // а-я -> А-Я
      else if (cp >= 0x03B1 && cp <= 0x03C9)                cp -= 0x20;   // α-ω -> Α-Ω
      else if (cp >= 0x00E0 && cp <= 0x00FE && cp != 0x00F7) cp -= 0x20;  // à-þ -> À-Þ
      // ą-ż (Latin Extended-A): pairing parity flips around the unpaired
      // codepoints ĸ (U+0138), ŉ (U+0149), Ÿ (U+0178) -- verified exhaustively
      // over the whole U+0100-U+017F block, resolves ł ń ź ż + ĺ ľ ň ž too.
      else if (cp >= 0x0100 && cp <= 0x0137 && (cp & 1) == 1) cp -= 1;     // odd=lower
      else if (cp >= 0x0139 && cp <= 0x0148 && (cp & 1) == 0) cp -= 1;     // even=lower
      else if (cp >= 0x014A && cp <= 0x0177 && (cp & 1) == 1) cp -= 1;     // odd=lower
      else if (cp >= 0x0179 && cp <= 0x017E && (cp & 1) == 0) cp -= 1;     // even=lower
      else if (cp >= 'a' && cp <= 'z')                      cp -= 0x20;   // a-z -> A-Z
    }
    if (cp < 0x80) {
      out[o++] = (char)cp;
    } else {
      out[o++] = (char)(0xC0 | (cp >> 6));
      out[o++] = (char)(0x80 | (cp & 0x3F));
    }
  }
  out[o] = '\0';
}

// Codepoint count of a UTF-8 string (byte length overcounts once a 2-byte
// alphabet is involved — this is what T9 cycling needs instead of strlen()).
static int kbUtf8Len(const char* s) {
  int n = 0;
  const uint8_t* p = (const uint8_t*)s;
  while (*p) { DisplayDriver::decodeCodepoint(p); n++; }
  return n;
}

// Extract the idx-th codepoint of a UTF-8 string as its own NUL-terminated
// UTF-8 bytes in `out` (>= 5 bytes). Empty string if idx is out of range.
static void kbUtf8CharAt(const char* s, int idx, char* out) {
  const uint8_t* p = (const uint8_t*)s;
  for (int i = 0; *p; i++) {
    const uint8_t* start = p;
    DisplayDriver::decodeCodepoint(p);
    if (i == idx) {
      int n = (int)(p - start);
      memcpy(out, start, n);
      out[n] = '\0';
      return;
    }
  }
  out[0] = '\0';
}

// Byte width of the LAST codepoint in buf[0..len) — for backspace and the T9
// in-place replace, which must remove/overwrite a whole codepoint, not one
// byte (a lone trailing continuation byte would otherwise corrupt a 2-byte
// character). Capped at 4 (UTF-8's max), though this codebase's alphabets are
// all <= 2 bytes today.
static int kbUtf8LastCharBytes(const char* buf, int len) {
  if (len <= 0) return 0;
  int n = 1;
  while (n < len && n < 4 && ((uint8_t)buf[len - n] & 0xC0) == 0x80) n++;
  return n;
}

// Byte width of the codepoint STARTING at buf[pos] (pos in [0,len)) — the
// forward counterpart to kbUtf8LastCharBytes, for moving the edit cursor
// right by one whole codepoint instead of one byte.
static int kbUtf8CharBytesAt(const char* buf, int pos, int len) {
  if (pos >= len) return 0;
  uint8_t c = (uint8_t)buf[pos];
  int n = 1;
  if ((c & 0xE0) == 0xC0)      n = 2;
  else if ((c & 0xF0) == 0xE0) n = 3;
  else if ((c & 0xF8) == 0xF0) n = 4;
  if (pos + n > len) n = len - pos;   // truncated/corrupt sequence: don't overrun
  return n;
}

// Accented variants of a Latin base letter, reachable by Hold-Enter on that
// letter's cell on the plain Latin ABC page (see handleInput's KEY_CONTEXT_MENU
// block and the accent_active state machine). Replaces the old per-language
// full alt-alphabet pages (Polish/Czech/Slovak/German/French/Spanish/
// Portuguese/Nordic) with one popup covering the union of all their accented
// letters -- closer to a phone keyboard's long-press accent picker than a
// second full page per language. Each entry is one UTF-8 string of
// concatenated variants (like KB_T9_GROUPS_* group strings), read with the
// same kbUtf8Len()/kbUtf8CharAt() helpers used there.
// Ligature/non-diacritic letters are filed under their conventional key, same
// as a phone keyboard's long-press: ß (German) -> s, œ (French) -> o. ĺ/ŕ
// (Slovak) are l/r with an acute, not i/e variants, so they're filed there.
static const char KB_ACCENT_BASES[] = "acdeilnorstuyz";
static const char* const KB_ACCENT_VARIANTS[] = {
  "áàâãäåą",  // a
  "çćč",      // c
  "ď",        // d
  "éèêëěę",   // e
  "íîï",      // i
  "łĺľ",      // l
  "ñńň",      // n
  "óòôõöøœ",  // o
  "řŕ",       // r
  "śšß",      // s
  "ť",        // t
  "úùûüů",    // u
  "ýÿ",       // y
  "źżž",      // z
};
static const int KB_ACCENT_COUNT = sizeof(KB_ACCENT_BASES) - 1;   // exclude the trailing NUL
static int findAccentGroup(char base) {
  for (int i = 0; i < KB_ACCENT_COUNT; i++) if (KB_ACCENT_BASES[i] == base) return i;
  return -1;
}

static const int KB_PH_MAX     = 20;  // max placeholders in list (PopupMenu::PM_MAX_ITEMS=24 is the hard ceiling)
static const int KB_PH_LEN     = 30;  // max placeholder string length incl. null -- sized for the longest
                                       // CLI-command candidate (AdminScreen), not just the short {x} tokens
static const int KB_PH_VISIBLE = 3;   // items shown at once in overlay

struct KeyboardWidget;
// Optional hook: if set, called right before the placeholder picker opens so
// the caller can repopulate the list contextually (e.g. AdminScreen's CLI
// command autocomplete, filtered by what's already typed). When set, picking
// an entry also replaces the in-progress word (the text since the last
// space) instead of appending it -- true completion, not insertion. Fields
// that don't set this (the common case -- {loc}/{time} etc.) keep the
// original static-list, append-only behaviour untouched.
typedef void (*PlaceholderRefreshFn)(KeyboardWidget& kb, void* ctx);

struct KeyboardWidget {
  char buf[KB_MAX_LEN + 1];
  int  len;
  int  max_len;
  int  cursor_pos;    // insertion point into buf, in bytes; defaults to len (append)
  bool cursor_mode;   // true while UP-from-row-0 has parked the grid to reposition cursor_pos
  bool accent_active = false;   // true while the Hold-Enter accent popup is open
  int  accent_group  = -1;      // index into KB_ACCENT_VARIANTS for the held cell's base letter
  int  accent_sel    = 0;       // selected variant within that group
  int  row, col;
  int  page;        // see totalPages()/scriptAt()/pageIsSymbols() below
  bool caps;
  // Shift is one-shot by default (like a phone keyboard: capitalises just the
  // next letter, then reverts) — Hold-Enter on Shift toggles caps_lock, which
  // keeps it on for a whole run of capitals instead.
  bool caps_lock = false;
  // Set by render() every time it's actually called; UITask clears it before
  // curr->render() each frame (beginFrame()) so it reflects only "was the
  // keyboard the thing on screen this frame" -- lets the alert overlay (new
  // message toast) skip drawing over a full-screen keyboard, regardless of
  // which screen (Messages/Bot/Settings/Admin/...) currently owns it.
  bool _visible = false;
  void beginFrame() { _visible = false; }
  bool isVisible() const { return _visible; }

  // True while the plain letter/symbol grid is the active input surface --
  // showing, no placeholder/accent popup open, not mid cursor-reposition.
  // Used by CardKB's Compact-mode handling (UITask::pollCardKB()) to tell
  // "grid navigation" apart from every other state arrows/Enter already mean
  // something else in (those all render their own visible feedback, so they
  // don't need Compact's special-casing).
  bool inPlainGridState() const { return isVisible() && !_ph_menu.active && !cursor_mode && !accent_active; }

  char _ph_buf[KB_PH_MAX][KB_PH_LEN];
  int  _ph_count;
  PopupMenu _ph_menu;
  PlaceholderRefreshFn _ph_refresh = nullptr;
  void* _ph_refresh_ctx = nullptr;
  const char* _ph_title = "Placeholder:";   // popup title -- overridable so e.g. AdminScreen can say "Commands:"
  void setPlaceholderRefresh(PlaceholderRefreshFn fn, void* ctx, const char* title = "Placeholder:") {
    _ph_refresh = fn; _ph_refresh_ctx = ctx; _ph_title = title;
  }

  // Live setting lookup — set once by UITask::begin(). NULL only in tests/tools
  // that construct a KeyboardWidget standalone, in which case isT9() defaults
  // to ABC and mainScript()/altScript() default to Latin-only.
  NodePrefs* prefs = nullptr;
  bool isT9() const { return prefs && prefs->keyboard_type == 1; }
  // Which script occupies page 0 (the keyboard's default/opening page) and
  // which occupies page 1 (reached by the #@/abc cycle key) -- Settings >
  // Keyboard's Main/Additional rows. Additional equal to Main collapses to no
  // second page at all (see hasAltAlphabet), same as the old Latin-hardcoded
  // design's "alt == Latin means no alt".
  uint8_t mainScript() const { return prefs ? prefs->keyboard_main_alphabet : NodePrefs::KB_ALPHABET_LATIN_ONLY; }
  uint8_t altScript()  const { return prefs ? prefs->keyboard_alt_alphabet  : NodePrefs::KB_ALPHABET_LATIN_ONLY; }
  bool hasAltAlphabet() const { return altScript() != mainScript(); }

  // T9 multi-tap state: which grid cell is mid-cycle (-1 = none), its cycle
  // position, and when the last Enter landed on it (for the timeout).
  int      t9_cell = -1;
  int      t9_cycle = 0;
  uint32_t t9_last_ms = 0;
  // Caps state the *first* tap of the current T9 cycle applied -- reused by every
  // later cycling tap on the same cell, since one-shot Shift is consumed (see
  // below) right after that first tap, before the user has settled on a letter.
  // Without this, cycling to the 2nd/3rd/... candidate would always render
  // lowercase regardless of Shift.
  bool     t9_caps = false;

  int gridRows() const { return isT9() ? KB_T9_ROWS : KB_ROWS_CHAR; }
  int gridCols() const { return isT9() ? KB_T9_COLS : KB_COLS_CHAR; }

  // ── Page model ────────────────────────────────────────────────────────────
  // Logical page order: 0 = mainScript(), [1 = altScript(), if it differs],
  // last = symbols. Without a distinct additional script this is exactly the
  // original 2-page cycle; a distinct one inserts its page in the middle, so
  // the #@/abc key's existing cycle (case 4 below) reaches it for free.
  int totalPages() const { return hasAltAlphabet() ? 3 : 2; }
  bool pageIsSymbols(int pg) const { return pg == totalPages() - 1; }
  // Which script (Latin/Cyrillic/Greek) the given non-symbols page shows.
  uint8_t scriptAt(int pg) const { return pg == 0 ? mainScript() : altScript(); }

  // One script's ABC-grid cell content as a NUL-terminated UTF-8 string (1
  // codepoint). Latin comes back through a small scratch buffer since
  // KB_CHARS stores single ASCII bytes, not strings; Cyrillic/Greek cells are
  // literal string-table entries, returned directly.
  const char* scriptCellStr(uint8_t script, int r, int c) const {
    switch (script) {
      case NodePrefs::KB_ALPHABET_CYRILLIC: return KB_CYRILLIC_CHARS[r][c];
      case NodePrefs::KB_ALPHABET_GREEK:    return KB_GREEK_CHARS[r][c];
      default: {
        static char single[2];
        single[0] = KB_CHARS[0][r][c];
        single[1] = '\0';
        return single;
      }
    }
  }
  const char* cellStr(int r, int c) const {
    if (pageIsSymbols(page)) {
      static char single[2];
      single[0] = KB_CHARS[1][r][c];
      single[1] = '\0';
      return single;
    }
    return scriptCellStr(scriptAt(page), r, c);
  }

  // One script's T9 group string (UTF-8) for the given cell (0-8).
  const char* scriptT9GroupStr(uint8_t script, int cell) const {
    switch (script) {
      case NodePrefs::KB_ALPHABET_CYRILLIC: return KB_T9_GROUPS_CYRILLIC[cell];
      case NodePrefs::KB_ALPHABET_GREEK:    return KB_T9_GROUPS_GREEK[cell];
      default:                              return KB_T9_GROUPS[0][cell];
    }
  }
  const char* t9GroupStr(int cell) const {
    if (pageIsSymbols(page)) return KB_T9_GROUPS[1][cell];
    return scriptT9GroupStr(scriptAt(page), cell);
  }

  // Compact ASCII hint for the #@/abc key when it's about to switch to
  // `script`'s page. Deliberately ASCII (not the script's own glyphs): it's
  // two characters wide in a one-sixth-of-the-screen cell, and "CY"/"GR" name
  // the destination more legibly at that size than a sample glyph would.
  static const char* scriptHint(uint8_t script) {
    switch (script) {
      case NodePrefs::KB_ALPHABET_CYRILLIC: return "CY";
      case NodePrefs::KB_ALPHABET_GREEK:    return "GR";
      default:                              return "abc";
    }
  }

  enum Result { NONE, DONE, CANCELLED };

  void begin(const char* initial = "", int max = KB_MAX_LEN) {
    max_len = (max > KB_MAX_LEN) ? KB_MAX_LEN : max;
    strncpy(buf, initial, max_len);
    buf[max_len] = '\0';
    len = strlen(buf);
    cursor_pos = len;
    cursor_mode = false;
    accent_active = false;
    accent_group = -1;
    accent_sel = 0;
    row = col = 0;
    page = 0;
    caps = false;
    caps_lock = false;
    t9_cell = -1;
    t9_cycle = 0;
    _ph_menu.active = false;
    _ph_refresh = nullptr;      // opt-in per session -- the owning screen re-sets it if it wants
    _ph_refresh_ctx = nullptr;  // contextual autocomplete right after this begin()
    _ph_title = "Placeholder:";
    // default placeholders — always available
    _ph_count = 0;
    addPlaceholder("{loc}");
    addPlaceholder("{time}");
  }

  // Insert one UTF-8 codepoint (a grid cell's own glyph, or a picked accent
  // variant) at cursor_pos, applying Shift/caps-lock the same way every cell
  // commit does. Shared by the plain-Latin-cell commit below and the accent
  // popup's Enter commit, so the two paths can't drift apart.
  void insertGlyph(const char* one, bool use_caps) {
    char shown[5];
    kbApplyCapsUtf8(one, use_caps, shown, sizeof(shown));
    int n = (int)strlen(shown);
    int tail_len = len - cursor_pos;
    if (len + n <= max_len) {
      memmove(buf + cursor_pos + n, buf + cursor_pos, tail_len);
      memcpy(buf + cursor_pos, shown, n);
      len += n;
      cursor_pos += n;
      buf[len] = '\0';
    }
  }

  // Insert one character typed literally on an external keyboard (CardKB or
  // similar), as opposed to committed off the on-screen grid. The source
  // already sends the correct case, so no Shift/caps-lock is re-applied.
  //
  // This is the single translation point for external-keyboard input: today
  // it's the identity mapping (CardKB is a Latin QWERTY, so what it sends is
  // what gets typed, regardless of the on-screen grid's script/T9 settings --
  // those only govern grid navigation). To support relabelled keycaps
  // (Cyrillic/Greek/...) later, map `c` to that layout's codepoint here and
  // hand the resulting UTF-8 to insertGlyph() -- everything downstream already
  // works in codepoints, not bytes. Such a layout belongs on its own setting,
  // not on keyboard_main_alphabet: it describes the physical keycaps, which
  // are independent of what the on-screen grid shows. Digits/punctuation
  // should keep passing through unmapped, and Fn+letter accents
  // (openAccentFor()) stay Latin-only -- they're meaningless under non-Latin
  // keycaps.
  void insertTyped(char c) {
    t9_cell = -1;   // otherwise a same-cell T9 tap within KB_T9_TIMEOUT_MS would
                    // overwrite this character instead of inserting a new one
    char one[2] = { c, '\0' };
    insertGlyph(one, false);
  }

  void clearPlaceholders() { _ph_count = 0; }

  void addPlaceholder(const char* ph) {
    if (_ph_count < KB_PH_MAX) {
      strncpy(_ph_buf[_ph_count], ph, KB_PH_LEN - 1);
      _ph_buf[_ph_count][KB_PH_LEN - 1] = '\0';
      _ph_count++;
    }
  }

  // Opens the placeholder picker directly -- same as navigating the grid to
  // the special row's {} cell and pressing Enter (see handleInput's KEY_ENTER
  // special-row case 3, which now calls this too). Used by CardKB's Compact
  // mode (UITask::pollCardKB(), plain Tab) so a placeholder is reachable
  // without ever seeing or navigating the grid.
  void openPlaceholders() {
    t9_cell = -1;   // finalize any pending multi-tap cycle -- the pick below moves the
                    // cursor, so a later same-cell tap must not "continue" onto it
    if (_ph_refresh) _ph_refresh(*this, _ph_refresh_ctx);   // contextual repopulate, if wired up
    _ph_menu.begin(_ph_title, KB_PH_VISIBLE);
    for (int i = 0; i < _ph_count; i++) _ph_menu.addItem(_ph_buf[i]);
  }

  // Moves the text cursor directly (LEFT/RIGHT one codepoint, UP/DOWN to
  // start/end) without engaging cursor_mode's grid-boundary wrap (continuing
  // into the special row / row 0 once already at an end) -- that wrap exists
  // so a physical-button user can see where they land back on the grid, which
  // doesn't apply here: CardKB's Compact mode never navigates the grid at
  // all, so there's no grid position to wrap into. See pollCardKB().
  void moveCursorDirect(char key) {
    t9_cell = -1;   // same invariant every other cursor-moving path keeps: a pending
                    // multi-tap cycle must not resume against a moved cursor and
                    // overwrite an unrelated character. (cursor_mode gets this for
                    // free -- its KEY_UP entry point already clears t9_cell.)
    if (key == KEY_LEFT)       { if (cursor_pos > 0)   cursor_pos -= kbUtf8LastCharBytes(buf, cursor_pos); }
    else if (key == KEY_RIGHT) { if (cursor_pos < len) cursor_pos += kbUtf8CharBytesAt(buf, cursor_pos, len); }
    else if (key == KEY_UP)    { cursor_pos = 0; }
    else if (key == KEY_DOWN)  { cursor_pos = len; }
  }

  int render(DisplayDriver& display) {
    _visible = true;
    // A stale mid-cycle T9 press (no further input since) finalizes on its own —
    // the character is already committed to buf, this just stops a later Enter
    // on the same cell from being treated as a continued cycle.
    if (t9_cell >= 0 && millis() - t9_last_ms > KB_T9_TIMEOUT_MS) t9_cell = -1;

    // Single UI font (misc-fixed 5x7) covers Latin/Greek/Cyrillic — the keyboard
    // renders in it directly, no per-render font switching or headroom padding.
    display.setTextSize(1);
    display.setColor(DisplayDriver::LIGHT);

    const int rows = gridRows();
    const int cols = gridCols();
    const int lh      = display.getLineHeight();
    const int cw      = display.getCharWidth();
    const int cell_w  = display.width() / cols;
    bool compact_ui = prefs && prefs->keyboard_cardkb_compact;
    // compact: don't stretch cells beyond lh; freed vertical space goes to preview lines.
    // Compact mode only ever draws 2 short hint lines (no grid, no status line
    // -- see below), but reserves at least as much height as the smallest real
    // grid (T9's 3 rows) would need, rather than shrinking to just those 2
    // lines: cursor_mode's own 3-line hint (drawn in this same region,
    // regardless of Compact, for a physical-button user) already relies on
    // that floor and would otherwise get clipped.
    const int kb_h      = compact_ui ? (KB_T9_ROWS + 1) * lh : (rows + 1) * lh;
    const int preview_h = display.height() - kb_h - display.sepH();
    const int prev_lines = (preview_h / lh) > 1 ? (preview_h / lh) : 1;
    const int sep_y   = prev_lines * lh;
    const int chars_y = sep_y + display.sepH();
    const int cell_h  = (display.height() - chars_y) / (rows + 1);
    const int spec_y  = chars_y + rows * cell_h;
    const int spec_w  = display.width() / KB_SPECIAL;

    // Multi-line text preview: the view follows cursor_pos (normally == len,
    // i.e. the end — so this is identical to the old "always the last line"
    // behaviour until cursor mode moves cursor_pos elsewhere, at which point
    // the preview scrolls to keep the repositioned cursor in view).
    // Line breaks are counted in CODEPOINTS, not bytes: cpl is how many
    // characters physically fit, so dividing byte offsets by it would count a
    // 2-byte Cyrillic/Greek/accented character as two -- halving the usable
    // line width and, worse, letting a break land inside a codepoint, which
    // reaches print() as a truncated sequence and draws as garbage (both
    // display drivers are permanently single-font, so translateUTF8ToBlocks()
    // passes UTF-8 straight through). Everything else in this widget already
    // works in codepoints via the kbUtf8*() helpers; this was the last
    // byte-based holdout.
    int cpl = display.width() / cw;  // chars per preview line
    if (cpl < 1) cpl = 1;
    if (cpl > KB_PREVIEW_CAP) cpl = KB_PREVIEW_CAP;  // never overrun linebuf below
    // Which preview line the cursor sits on = how many whole codepoints precede it.
    int cursor_chars = 0;
    for (int p = 0; p < cursor_pos; ) { p += kbUtf8CharBytesAt(buf, p, len); cursor_chars++; }
    int cursor_line = cursor_chars / cpl;
    int first_line  = (cursor_line >= prev_lines) ? (cursor_line - prev_lines + 1) : 0;
    // ...and the byte offset that line starts at.
    int ps = 0;
    for (int n = first_line * cpl; n > 0 && ps < len; n--) ps += kbUtf8CharBytesAt(buf, ps, len);
    for (int pl = 0; pl < prev_lines; pl++) {
      int pe = ps;   // byte offset cpl codepoints further along (or end of text)
      for (int k = 0; k < cpl && pe < len; k++) pe += kbUtf8CharBytesAt(buf, pe, len);
      // Which row the cursor is on is already decided above, by the same
      // cursor_chars / cpl the scroll window uses -- just ask it. Deriving it a
      // second time from byte offsets here is what put the cursor in the wrong
      // place: "cursor_pos >= ps and this is the bottom row" stranded it on the
      // last blank row whenever the text was shorter than the preview area,
      // and "text ends here" instead pinned it to the end of a full line
      // (drawing the '_' one character past the display width) at every
      // wrap boundary, where cursor_line has already moved to the next row.
      bool cursor_here = (first_line + pl == cursor_line);
      int line_end = (len < pe) ? len : pe;
      char linebuf[KB_PREVIEW_BYTES + 2];   // cpl codepoints + cursor '_' + NUL
      if (cursor_here) {
        // Cursor drawn as an inserted '_' between whatever text precedes and
        // follows it on this line -- reduces to the old "text + trailing _"
        // when cursor_pos == len (after_n is always 0 in that case).
        int before_n = cursor_pos - ps;        if (before_n < 0) before_n = 0;
        if (before_n > line_end - ps) before_n = line_end - ps;
        int after_n  = line_end - cursor_pos;  if (after_n < 0) after_n = 0;
        snprintf(linebuf, sizeof(linebuf), "%.*s_%.*s", before_n, buf + ps, after_n, buf + ps + before_n);
      } else if (len > ps) {
        snprintf(linebuf, sizeof(linebuf), "%.*s", line_end - ps, buf + ps);
      } else {
        linebuf[0] = '\0';
      }
      char linebuf_t[KB_PREVIEW_BYTES + 2];
      display.translateUTF8ToBlocks(linebuf_t, linebuf, sizeof(linebuf_t));
      display.setCursor(0, pl * lh);
      display.print(linebuf_t);
      ps = pe;
    }
    display.fillRect(0, sep_y, display.width(), display.sepH());

    // Cursor-positioning mode: LEFT/RIGHT/UP/DOWN now drive the text cursor
    // instead of the grid (see handleInput), so the grid would otherwise just
    // sit there frozen with no sign anything's different. Replace it with an
    // explicit hint instead.
    if (cursor_mode) {
      const int hh = lh + 2;
      display.setColor(DisplayDriver::LIGHT);
      display.fillRect(0, chars_y, display.width(), hh);
      display.setColor(DisplayDriver::DARK);
      display.drawTextCentered(display.width() / 2, chars_y + 1, "CURSOR MODE");
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, chars_y + hh + 2, "L/R move");
      display.drawTextCentered(display.width() / 2, chars_y + hh + 2 + lh, "U/D start/end");
      return 50;
    }

    // Compact mode (Settings > Keyboard's "Ext. KB" row): an external-keyboard
    // typist never looks at the letter grid or special-row icons, so skip
    // drawing them entirely -- no status line either, since nothing it could
    // show (script/page, T9-vs-ABC, caps) is actually actionable from CardKB:
    // typing is always plain Latin ASCII regardless of Main/Additional
    // alphabet or keyboard_type (direct-typing passthrough, see
    // UITask::pollCardKB()), Fn+letter's accent popup now works the same way
    // regardless of them too (see openAccentFor()), and caps-lock has no
    // CardKB gesture to toggle it at all. Just the two shortcuts that still do
    // something here (arrows/Enter are self-explanatory -- cursor movement and
    // submit -- so they get no hint of their own). Physical buttons (if used
    // instead of/alongside CardKB) still drive row/col/page as normal; it
    // just won't be visible on this screen which cell is selected.
    if (compact_ui) {
      display.setColor(DisplayDriver::LIGHT);
      display.drawTextCentered(display.width() / 2, chars_y, "Tab: placeholders");
      display.drawTextCentered(display.width() / 2, chars_y + lh, "Fn+letter: accent");
    } else {
      // character grid
      if (isT9()) {
        for (int r = 0; r < rows; r++) {
          int y = chars_y + r * cell_h;
          for (int c = 0; c < cols; c++) {
            bool sel = (row == r && col == c);
            int cell = r * cols + c;
            // Label the cell "<digit><group>" so it reads like a phone keypad. The
            // digit is what the multi-tap cycle lands on after the letters (see
            // handleInput: '1'+cell). No separator space — the widest group
            // (Cyrillic "деёжз"/"шщъыь", 5 letters x up to 2 UTF-8 bytes) + digit
            // still fits with room to spare.
            char group_shown[12];
            kbApplyCapsUtf8(t9GroupStr(cell), caps, group_shown, sizeof(group_shown));
            char label[14];
            snprintf(label, sizeof(label), "%c%s", (char)('1' + cell), group_shown);
            int cx = c * cell_w;
            display.drawSelectionRow(cx, y - 1, cell_w - 1, cell_h, sel);
            int tw = display.getTextWidth(label);
            display.setCursor(cx + (cell_w - tw) / 2, y);
            display.print(label);
          }
        }
      } else {
        for (int r = 0; r < rows; r++) {
          int y = chars_y + r * cell_h;
          for (int c = 0; c < cols; c++) {
            bool sel = (row == r && col == c);
            char ch_buf[3];
            kbApplyCapsUtf8(cellStr(r, c), caps, ch_buf, sizeof(ch_buf));
            if (ch_buf[0] == ' ' && ch_buf[1] == '\0') ch_buf[0] = '_';
            int cx = c * cell_w;
            display.drawSelectionRow(cx, y - 1, cell_w - 1, cell_h, sel);
            int tw = display.getTextWidth(ch_buf);
            display.setCursor(cx + (cell_w - tw) / 2, y);
            display.print(ch_buf);
          }
        }
      }

      // special row: caps ⇧ · space ⎵ · delete ⌫ · placeholders {} (text) · OK ✓
      const int s   = miniIconScale(display);
      const int icy = spec_y + (cell_h - lh) / 2;   // centre icons within the cell
      for (int i = 0; i < KB_SPECIAL; i++) {
        bool sel    = (row == rows && col == i);
        bool active = (i == 0 && caps);
        int sx = i * spec_w;
        display.drawSelectionRow(sx, spec_y - 1, spec_w - 1, cell_h, sel || active);
        if (i == 3 || i == 4) {               // text keys: {} picker, page toggle
          // Shows what pressing it lands on next, same "reads as the
          // destination" convention as the original 2-page abc<->#@ toggle,
          // generalized to however many pages are in the cycle right now.
          const char* lbl;
          if (i == 3) {
            lbl = "{}";
          } else {
            int next = (page + 1) % totalPages();
            lbl = pageIsSymbols(next) ? "#@" : scriptHint(scriptAt(next));
          }
          int tw = display.getTextWidth(lbl);
          display.setCursor(sx + (spec_w - tw) / 2, spec_y);
          display.print(lbl);
        } else if (i == 1) {                  // space ⎵ — two halves side by side
          int icw = (ICON_SPACE_L.w + ICON_SPACE_R.w) * s;
          int ix  = sx + (spec_w - icw) / 2;
          miniIconDraw(display, ix, icy, ICON_SPACE_L);
          miniIconDraw(display, ix + ICON_SPACE_L.w * s, icy, ICON_SPACE_R);
        } else {
          const MiniIcon& ic = (i == 0) ? ICON_SHIFT
                             : (i == 2) ? ICON_BACKSPACE
                                        : ICON_CHECK;   // i == 5 → OK
          int ix = sx + (spec_w - ic.w * s) / 2;
          miniIconDraw(display, ix, icy, ic);
          // Underline the ⇧ icon while caps_lock is held. Without it the two
          // Shift states are indistinguishable -- caps_lock sets caps too, so
          // the highlight above is identical -- even though they behave
          // completely differently (one letter vs. every following letter).
          // caps_lock implies the cell is filled, so the current (inverted)
          // ink colour is the one that shows against it.
          if (i == 0 && caps_lock) display.fillRect(sx + 2, spec_y + cell_h - 3, spec_w - 5, 1);
        }
        display.setColor(DisplayDriver::LIGHT);
      }
    }

    // Accent popup: floats over the still-visible grid (same idea as the
    // placeholder overlay just below), anchored on the held letter's own row
    // so it reads as "popping out of" that key instead of taking over the
    // whole keyboard. LEFT/RIGHT picks, Enter commits, Cancel dismisses.
    // In Compact there's no grid drawn and `row` was never deliberately
    // navigated to, so anchoring on it would just park the popup at an
    // arbitrary height (and possibly over a hint line) -- it gets a fixed slot
    // under the two hints instead.
    if (accent_active) {
      const char* group = KB_ACCENT_VARIANTS[accent_group];
      int n = kbUtf8Len(group);
      const int pad = 3;
      int seg_w = cw * 2 + pad;
      int bw = n * seg_w;
      int max_bw = display.width() - 4;
      if (bw > max_bw) { bw = max_bw; seg_w = bw / n; }
      int bx = (display.width() - bw) / 2;
      int by = compact_ui ? (chars_y + 2 * lh + 2) : (chars_y + row * cell_h - 1);
      int bh = compact_ui ? (lh + 2) : (cell_h + 1);
      display.setColor(DisplayDriver::DARK);
      display.fillRect(bx, by, bw, bh);
      display.setColor(DisplayDriver::LIGHT);
      display.drawRect(bx, by, bw, bh);
      for (int i = 0; i < n; i++) {
        char one[5]; kbUtf8CharAt(group, i, one);
        char shown[5]; kbApplyCapsUtf8(one, caps, shown, sizeof(shown));
        int x = bx + i * seg_w;
        if (i == accent_sel) {
          display.setColor(DisplayDriver::LIGHT);
          display.fillRect(x + 1, by + 1, seg_w - 1, bh - 2);
          display.setColor(DisplayDriver::DARK);
        } else {
          display.setColor(DisplayDriver::LIGHT);
        }
        int tw = display.getTextWidth(shown);
        display.setCursor(x + (seg_w - tw) / 2, by + 1);
        display.print(shown);
      }
      display.setColor(DisplayDriver::LIGHT);
    }

    // placeholder picker overlay (drawn on top of keyboard)
    if (_ph_menu.active) _ph_menu.render(display);
    return 50;
  }

  // CardKB's Fn+letter ("alt") gesture, see UITask::pollCardKB(): open the
  // accent popup for this base Latin letter directly, skipping the
  // arrow-hunt to find its cell first. `base` always comes from CardKB's own
  // physical QWERTY layout -- CardKB is a Latin keyboard, so it always types
  // plain ASCII regardless of the on-screen grid's current page/script/T9
  // setting (those only govern what the *grid* shows for physical-button
  // navigation, a completely separate input path with its own copy of this
  // same gate at the Hold-Enter-on-a-letter-cell site in handleInput()).
  // Gating this one on the grid's page/script/T9 state would make Fn+letter
  // silently stop working whenever Main alphabet is set to Cyrillic/Greek or
  // keyboard_type to T9, even though CardKB is still typing plain Latin text
  // just fine -- so this only checks that no other exclusive input mode
  // (popup/cursor-move) is already in progress, same as inPlainGridState().
  bool openAccentFor(char base) {
    if (!isVisible() || _ph_menu.active || cursor_mode || accent_active) return false;
    int gi = findAccentGroup(base);
    if (gi < 0) return false;
    accent_active = true;
    accent_group = gi;
    accent_sel = 0;
    t9_cell = -1;
    return true;
  }

  Result handleInput(char c) {
    // placeholder overlay consumes all input
    if (_ph_menu.active) {
      auto res = _ph_menu.handleInput(c);
      if (res == PopupMenu::SELECTED) {
        int idx = _ph_menu.selectedIndex();
        const char* ph = _ph_buf[idx];
        int ph_len = strlen(ph);
        // Contextual (refresh-hook) fields complete the in-progress word --
        // the text since the last space, up to the cursor -- instead of
        // appending after it, so picking a match doesn't duplicate what's
        // already been typed. Anything after the cursor (if it's not at the
        // end) shifts along with the insertion, same as a normal keystroke.
        int base_len = cursor_pos;
        if (_ph_refresh) {
          while (base_len > 0 && buf[base_len - 1] != ' ') base_len--;
        }
        int tail_len = len - cursor_pos;
        if (base_len + ph_len + tail_len <= max_len) {
          memmove(buf + base_len + ph_len, buf + cursor_pos, tail_len);
          memcpy(buf + base_len, ph, ph_len);
          len = base_len + ph_len + tail_len;
          cursor_pos = base_len + ph_len;
          buf[len] = '\0';
        }
      }
      return NONE;
    }

    // Cursor-positioning sub-mode (see the KEY_UP block below): the grid
    // selection is parked while LEFT/RIGHT walk cursor_pos one codepoint at a
    // time. UP/DOWN jump to the very start/end -- Home/End, in effect -- and,
    // once already at that boundary, continue the wrap the entry trigger
    // interrupted: UP again lands on the special row, DOWN again back on the
    // letter grid's row 0, same destinations the plain grid wrap used to reach
    // directly (see the entry/exit comment below). Enter/Cancel just leave the
    // mode from anywhere; the actual edit (insert/backspace) happens back in
    // normal typing, now targeting the repositioned cursor.
    if (cursor_mode) {
      if (c == KEY_LEFT)  { if (cursor_pos > 0)   cursor_pos -= kbUtf8LastCharBytes(buf, cursor_pos); return NONE; }
      if (c == KEY_RIGHT) { if (cursor_pos < len) cursor_pos += kbUtf8CharBytesAt(buf, cursor_pos, len); return NONE; }
      if (c == KEY_UP) {
        if (cursor_pos > 0) { cursor_pos = 0; return NONE; }
        cursor_mode = false;
        row = gridRows();
        col = col * KB_SPECIAL / gridCols();
        return NONE;
      }
      if (c == KEY_DOWN) {
        if (cursor_pos < len) { cursor_pos = len; return NONE; }
        cursor_mode = false;
        row = 0;
        return NONE;
      }
      // KEY_KB_ENTER (external keyboard's Fn+Enter) leaves the mode too rather
      // than being silently eaten -- it's the one key an external-keyboard
      // typist would reach for here, and cursor mode is only ever entered from
      // a physical button, so without this it looks like a dead key.
      if (c == KEY_ENTER || c == KEY_CANCEL || c == KEY_KB_ENTER) { cursor_mode = false; return NONE; }
      return NONE;
    }

    if (accent_active) {
      const char* group = KB_ACCENT_VARIANTS[accent_group];
      int n = kbUtf8Len(group);
      if (keyIsPrev(c)) { accent_sel = (accent_sel > 0) ? accent_sel - 1 : n - 1; return NONE; }
      if (keyIsNext(c)) { accent_sel = (accent_sel < n - 1) ? accent_sel + 1 : 0; return NONE; }
      if (c == KEY_ENTER) {
        char one[5]; kbUtf8CharAt(group, accent_sel, one);
        insertGlyph(one, caps);
        if (caps && !caps_lock) caps = false;
        accent_active = false;
        return NONE;
      }
      if (c == KEY_CANCEL) { accent_active = false; return NONE; }
      return NONE;
    }

    if (c == KEY_CANCEL) return CANCELLED;

    // Direct-typing passthrough (CardKB or similar literal-ASCII input
    // source, see UITask::pollCardKB()). Printable characters insert
    // straight at the cursor, bypassing the on-screen grid entirely -- no
    // caps re-application, the source already sends the correct case.
    // Backspace deletes the previous character. KEY_KB_ENTER submits the
    // field directly -- pollCardKB() emits it for Fn+Enter always, and for
    // plain Enter too when Compact mode's plain grid state applies (there's
    // no grid cell to commit there). Every other plain Enter (Full mode, or
    // Compact but mid popup/cursor-move) arrives here as ordinary KEY_ENTER
    // and falls through to the grid dispatch below instead, so there's still
    // no ambiguity to resolve at this layer -- pollCardKB() already decided.
    if (c == KEY_KB_ENTER) return DONE;
    if (c == 0x08) {
      t9_cell = -1;   // invalidate any pending T9 cycle -- see the grid paths below
      if (cursor_pos > 0) {
        int n = kbUtf8LastCharBytes(buf, cursor_pos);
        memmove(buf + cursor_pos - n, buf + cursor_pos, len - cursor_pos);
        len -= n; cursor_pos -= n;
        buf[len] = '\0';
      }
      return NONE;
    }
    if (c >= 0x20 && c <= 0x7E) {
      insertTyped(c);
      return NONE;
    }

    const int rows = gridRows();
    const int cols = gridCols();

    // Hold-Enter is normally "cancel", but three places give it a more useful
    // meaning instead: Shift -> toggle a persistent caps-lock (a plain tap is
    // one-shot -- see the commit sites below); Backspace -> clear the whole
    // field in one action instead of holding it down; a Latin-page letter cell
    // with accented variants -> open the accent popup (see accent_active
    // above). Every other special-row cell keeps hold-to-cancel; any other
    // letter/symbol cell (a plain letter with no accents, or any T9/alt-
    // alphabet/symbols cell) is a silent no-op instead, so it can't
    // accidentally close the keyboard. Cursor mode itself moved off Hold-Enter
    // entirely -- see the KEY_UP block below, where UP from row 0 now enters
    // it instead.
    if (c == KEY_CONTEXT_MENU) {
      if (row == rows && col == 0) {          // Shift
        caps_lock = !caps_lock;
        caps = caps_lock;
        return NONE;
      }
      if (row == rows && col == 2) {          // Backspace
        len = 0; buf[0] = '\0';
        cursor_pos = 0;
        t9_cell = -1;
        return NONE;
      }
      if (row < rows) {
        if (!isT9() && !pageIsSymbols(page) && scriptAt(page) == NodePrefs::KB_ALPHABET_LATIN_ONLY) {
          int gi = findAccentGroup(cellStr(row, col)[0]);
          if (gi >= 0) { accent_active = true; accent_group = gi; accent_sel = 0; t9_cell = -1; return NONE; }
        }
        return NONE;   // no variants for this cell, or T9/non-Latin/symbols page
      }
      return CANCELLED;
    }

    if (c == KEY_UP) {
      if (row > 0) {
        row--;
        if (row == rows - 1)  // leaving special row upward
          col = col * cols / KB_SPECIAL;
      } else {
        // row 0: enter cursor mode instead of wrapping to the special row --
        // row/col are deliberately left as-is so the cursor-mode UP/DOWN
        // continuation above can still reach the special row proportionally.
        cursor_mode = true;
        t9_cell = -1;
        return NONE;
      }
      t9_cell = -1;   // navigating away finalizes any pending multi-tap cycle
      return NONE;
    }
    if (c == KEY_DOWN) {
      if (row < rows) {
        row++;
        if (row == rows)  // entering special row
          col = col * KB_SPECIAL / cols;
      } else {
        row = 0;                        // wrap down onto the first char row
        col = col * cols / KB_SPECIAL;
      }
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_LEFT) {
      int max_col = (row == rows) ? KB_SPECIAL - 1 : cols - 1;
      col = (col > 0) ? col - 1 : max_col;
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_RIGHT) {
      int max_col = (row == rows) ? KB_SPECIAL - 1 : cols - 1;
      col = (col < max_col) ? col + 1 : 0;
      t9_cell = -1;
      return NONE;
    }
    if (c == KEY_ENTER) {
      // By the time a plain KEY_ENTER reaches here, it's a real grid
      // interaction -- repeat a letter, cycle T9, switch page/script, reach
      // the special row's DONE cell -- same as a physical button. CardKB's
      // own Enter is only ever KEY_ENTER when that's true (see pollCardKB()'s
      // KEY_KB_ENTER cases just above), so there's no ambiguity here.
      if (row < rows && isT9()) {
        int cell = row * cols + col;
        const char* group = t9GroupStr(cell);
        int glen = kbUtf8Len(group);       // codepoint count, not byte length
        int total = glen + 1;   // + the cell's own digit, at the end of the cycle
        bool cycling = (t9_cell == cell) && (millis() - t9_last_ms < KB_T9_TIMEOUT_MS);
        if (cycling) {
          t9_cycle = (t9_cycle + 1) % total;
          if (cursor_pos > 0) {
            char one[5];
            if (t9_cycle < glen) kbUtf8CharAt(group, t9_cycle, one);
            else { one[0] = (char)('1' + cell); one[1] = '\0'; }
            char shown[5];
            kbApplyCapsUtf8(one, t9_caps, shown, sizeof(shown));
            // Replace the codepoint just before the cursor (what the previous
            // tap inserted), preserving anything after the cursor too.
            int old_n = kbUtf8LastCharBytes(buf, cursor_pos);
            int new_pos = cursor_pos - old_n;
            int tail_len = len - cursor_pos;
            int n = (int)strlen(shown);
            if (new_pos + n + tail_len <= max_len) {
              memmove(buf + new_pos + n, buf + cursor_pos, tail_len);
              memcpy(buf + new_pos, shown, n);
              len = new_pos + n + tail_len;
              cursor_pos = new_pos + n;
              buf[len] = '\0';
            }
          }
        } else if (len < max_len) {
          char one[5]; kbUtf8CharAt(group, 0, one);
          char shown[5]; kbApplyCapsUtf8(one, caps, shown, sizeof(shown));
          int n = (int)strlen(shown);
          int tail_len = len - cursor_pos;
          if (len + n <= max_len) {
            memmove(buf + cursor_pos + n, buf + cursor_pos, tail_len);
            memcpy(buf + cursor_pos, shown, n);
            len += n;
            cursor_pos += n;
            buf[len] = '\0';
            t9_cell = cell;
            t9_cycle = 0;
            t9_caps = caps;   // remember it for every later cycling tap on this cell
            if (caps && !caps_lock) caps = false;   // one-shot: only this first tap gets capitalised
          }
        }
        t9_last_ms = millis();
      } else if (row < rows) {
        insertGlyph(cellStr(row, col), caps);
        if (caps && !caps_lock) caps = false;   // one-shot: revert after the letter it capitalised
      } else {
        t9_cell = -1;   // any special-row action finalizes a pending multi-tap cycle
        switch (col) {
          // Tap toggles one-shot caps on/off; while caps_lock is held (Hold-Enter
          // on this key, see handleInput's top), a tap cancels the lock instead.
          case 0: if (caps_lock) { caps = false; caps_lock = false; } else { caps = !caps; } break;
          case 1:
            if (len < max_len) {
              memmove(buf + cursor_pos + 1, buf + cursor_pos, len - cursor_pos);
              buf[cursor_pos] = ' ';
              len++; cursor_pos++;
              buf[len] = '\0';
            }
            break;
          case 2:
            if (cursor_pos > 0) {
              int n = kbUtf8LastCharBytes(buf, cursor_pos);
              memmove(buf + cursor_pos - n, buf + cursor_pos, len - cursor_pos);
              len -= n; cursor_pos -= n;
              buf[len] = '\0';
            }
            break;
          case 3:
            openPlaceholders();
            break;
          case 4:
            page = (page + 1) % totalPages();   // cycle letters -> [alt alphabet] -> symbols
            break;
          case 5:
            return DONE;
        }
      }
    }
    return NONE;
  }
};
