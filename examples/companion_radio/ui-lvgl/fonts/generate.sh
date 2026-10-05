#!/usr/bin/env bash
# Regenerates the ui-lvgl fonts (Noto Sans + LVGL's built-in symbols).
#
# Coverage: full European Latin (Latin-1, Latin Extended-A/B -- incl. Polish,
# Czech, Slovak, Hungarian, Romanian ș/ț, Baltic, Turkish, Nordic, Icelandic,
# Maltese, Welsh ŵ/ŷ/ẁ…, German ẞ), Greek, Cyrillic (+ supplement: Ukrainian,
# Belarusian, Serbian, Macedonian, Bulgarian…), typographic punctuation,
# currency (€ ₴ ₽ …) and №, plus LVGL's LV_SYMBOL_* icons; emoji fall
# back to colour Twemoji images (emoji.py -> ui_emoji_data.c, ui_emoji.c).
#
# Needs node (npx fetches lv_font_conv) and curl. Run from anywhere:
#   examples/companion_radio/ui-lvgl/fonts/generate.sh
# Output: ui_font_<size>.c next to this script (commit them).
set -euo pipefail
HERE="$(cd "$(dirname "$0")" && pwd)"
CACHE="${FONT_CACHE:-${TMPDIR:-/tmp}/meshcore-ui-fonts}"
mkdir -p "$CACHE"

NOTO_BASE=https://github.com/notofonts/notofonts.github.io/raw/main/fonts/NotoSans/hinted/ttf
for w in Medium SemiBold; do
  [ -f "$CACHE/NotoSans-$w.ttf" ] || curl -sSL -o "$CACHE/NotoSans-$w.ttf" "$NOTO_BASE/NotoSans-$w.ttf"
done
FA_URL=https://raw.githubusercontent.com/lvgl/lvgl/v9.2.2/scripts/built_in_font/FontAwesome5-Solid+Brands+Regular.woff
[ -f "$CACHE/fa.woff" ] || curl -sSL -o "$CACHE/fa.woff" "$FA_URL"

# Text ranges
TEXT="0x20-0x7E,0xA0-0x24F,0x370-0x3FF,0x400-0x52F,0x1E80-0x1E85,0x1E9E,0x1EF2-0x1EF3"
TEXT="$TEXT,0x2010-0x2027,0x2030,0x2039-0x203A,0x20A4-0x20BF,0x2116,0x2122"
# LVGL built-in symbols (same list as lvgl/scripts/built_in_font/built_in_font_gen.py)
# plus 61612 = globe (keyboard script key), 61445 = star (favourite),
# 62073 = map, 61632 = users (home tiles), 61774 = compass (Navigate),
# 61476 = flag (waypoint), 62405 = map marker (mark here), 61463 = clock,
# 62194 = stopwatch, 62745 = broadcast tower (radio), 61568 = bar chart (diagnostics),
# 61572 = key (admin), 62679 = route (trail in the status bar),
# 63145 = volume-mute (muted, in the status bar), 61581 = thumbtack (pin to the Home dial),
# 61475 = lock, 61829 = sun (the quick panel's lock and brightness)
SYMS="61475,61829,61581,63145,61572,62679,61568,62745,61463,62194,61612,61445,62073,61632,61774,61476,62405,61441,61448,61451,61452,61453,61457,61459,61461,61465,61468,61473,61478,61479,61480,61502,61507,61512,61515,61516,61517,61521,61522,61523,61524,61543,61544,61550,61552,61553,61556,61559,61560,61561,61563,61587,61589,61636,61637,61639,61641,61664,61671,61674,61683,61724,61732,61787,61931,62016,62017,62018,62019,62020,62087,62099,62212,62189,62810,63426,63650"

CONV="npx -y lv_font_conv@1.5.3"
gen() {   # size weight ranges name
  $CONV --bpp 4 --size "$1" --no-compress --force-fast-kern-format \
    --font "$CACHE/NotoSans-$2.ttf" -r "$3" \
    --font "$CACHE/fa.woff" -r "$SYMS" \
    --format lvgl --lv-include lvgl.h --lv-font-name "$4" -o "$HERE/$4.c"
}
for s in 12 14 16 20; do
  gen "$s" Medium "$TEXT" "ui_font_$s"
  sed -i.bak "s/\.fallback = NULL,/.fallback = \&ui_emoji_$s,/" "$HERE/ui_font_$s.c" && rm "$HERE/ui_font_$s.c.bak"
done
# Clock: digits, colon, dash, dot, space only.
$CONV --bpp 4 --size 40 --no-compress --font "$CACHE/NotoSans-SemiBold.ttf" -r "0x20,0x2D-0x2E,0x30-0x3A" \
  --format lvgl --lv-include lvgl.h --lv-font-name ui_font_40 -o "$HERE/ui_font_40.c"
# Status bar icons: FontAwesome refitted to one size (status_icons.py, needs
# python3 with fonttools). Line metrics forced to ui_font_12's and text falls
# back to it, so "<battery> 85%" is one label.
ICONS="0xF024,0xF079,0xF0E7,0xF0F3,0xF124,0xF1EB,0xF240-0xF244,0xF293,0xF3C5,0xF4D7,0xF519,0xF6A9"
python3 "$HERE/status_icons.py" "$CACHE/fa.woff" "$CACHE/ui-icons.ttf"
$CONV --bpp 4 --size 14 --no-compress --font "$CACHE/ui-icons.ttf" -r "$ICONS" \
  --format lvgl --lv-include lvgl.h --lv-font-name ui_icons_14 -o "$HERE/ui_icons_14.c"
sed -i.bak -e 's/\.line_height = [0-9]*,/.line_height = 18,/' -e 's/\.base_line = [0-9]*,/.base_line = 4,/' \
  -e 's/\.fallback = NULL,/.fallback = \&ui_font_12,/' "$HERE/ui_icons_14.c" && rm "$HERE/ui_icons_14.c.bak"
# Colour emoji the text fonts fall back to (ui_emoji.c): Twemoji 15.1, CC-BY 4.0.
# Needs python3 with Pillow.
TW="$CACHE/twemoji-15.1.0"
[ -d "$TW" ] || curl -sSL https://codeload.github.com/jdecked/twemoji/tar.gz/refs/tags/v15.1.0 | tar xz -C "$CACHE" twemoji-15.1.0/assets/72x72
python3 "$HERE/emoji.py" "$TW/assets/72x72" "$HERE/ui_emoji_data.c"
echo "done:$(ls "$HERE"/ui_font_*.c | wc -l) fonts"
