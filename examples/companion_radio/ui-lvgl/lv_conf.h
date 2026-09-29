// LVGL 9 configuration for the ui-lvgl frontend. Found through
// -D LV_CONF_INCLUDE_SIMPLE + -I examples/companion_radio/ui-lvgl; anything not
// set here takes LVGL's default from lv_conf_internal.h.
#ifndef LV_CONF_H
#define LV_CONF_H

#define LV_COLOR_DEPTH 16

// Memory. On ESP32-S3: the PSRAM heap (lv_mem_psram.h), thread-safe -- map
// tiles decode on the other core. Elsewhere (the sim): LVGL's own pool.
#if defined(ESP32)
  #define LV_USE_STDLIB_MALLOC  LV_STDLIB_CUSTOM
#else
  #define LV_USE_STDLIB_MALLOC  LV_STDLIB_BUILTIN
  #define LV_MEM_SIZE           (2048U * 1024U)   // map tiles decode through it (~0.5 MB peak per PNG)
#endif
#define LV_USE_STDLIB_STRING    LV_STDLIB_CLIB
#define LV_USE_STDLIB_SPRINTF   LV_STDLIB_CLIB

// No OS: the loop draws. LV_OS_FREERTOS with two software draw units was
// measured slower on the L2 (map redraw ~105 ms against 55-80; one unit ~89):
// drawing here is bound by PSRAM bandwidth, which the two units share.
#define LV_USE_OS               LV_OS_NONE
#define LV_DEF_REFR_PERIOD      20     // ms; the loop also services the radio
#define LV_DPI_DEF              130    // 2.8" 320x240

#define LV_USE_LOG              0
#define LV_USE_ASSERT_NULL      1
#define LV_USE_ASSERT_MALLOC    1

// Fonts: generated Noto Sans (European Latin, Greek, Cyrillic + LV_SYMBOL_*),
// see fonts/generate.sh. LVGL's built-in Montserrat (ASCII only) is off.
// Each text font falls back to colour emoji (fonts/ui_emoji.c, Twemoji).
#define LV_FONT_MONTSERRAT_14   0
#define LV_FONT_CUSTOM_DECLARE  LV_FONT_DECLARE(ui_font_12) LV_FONT_DECLARE(ui_font_14) \
                                LV_FONT_DECLARE(ui_font_16) LV_FONT_DECLARE(ui_font_20) \
                                LV_FONT_DECLARE(ui_font_40) \
                                LV_FONT_DECLARE(ui_icons_14) \
                                LV_FONT_DECLARE(ui_emoji_12) LV_FONT_DECLARE(ui_emoji_14) \
                                LV_FONT_DECLARE(ui_emoji_16) LV_FONT_DECLARE(ui_emoji_20)
// Image glyphs (the emoji) are drawn only with this on; lv_imgfont itself is unused.
#define LV_USE_IMGFONT          1
#define LV_FONT_DEFAULT         &ui_font_14
// Where a line may break: not after '.', which would split "4.0", "v1.23" or
// a coordinate across two lines.
#define LV_TXT_BREAK_CHARS      " ,;:-_)]}"
#define LV_USE_FONT_COMPRESSED  0      // fonts are generated uncompressed: no per-glyph decompression on every draw

// PNG decoder for raster map tiles (map/TileProvider.h calls lodepng directly).
#define LV_USE_LODEPNG          1

// Spans: message text with @[nick] mentions picked out (UITask.cpp msgText).
#define LV_USE_SPAN             1

#define LV_USE_THEME_DEFAULT    1
#define LV_THEME_DEFAULT_DARK   1

// Not needed by the frontend (smaller build).
#define LV_USE_CHART            0
#define LV_USE_CALENDAR         0
#define LV_USE_TABLE            0
#define LV_USE_SCALE            0
#define LV_USE_LOTTIE           0
#define LV_BUILD_EXAMPLES       0
#define LV_BUILD_DEMOS          0

#endif // LV_CONF_H
