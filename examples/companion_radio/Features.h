#pragma once

// Build-time feature flags derived from board defines. Two flavours:
//
//   FEAT_* preprocessor macros — for conditional compilation of struct
//   members, enum entries, and class fields where the preprocessor must
//   run. Use `#if FEAT_X` instead of `#ifdef EINK_DISPLAY_MODEL`; the
//   name documents *what* the flag toggles rather than *why* it's bound
//   to e-ink.
//
//   Features::* constexpr values — for runtime-shape decisions (booleans,
//   timings, defaults). Compiler dead-branch-eliminates on the constexpr,
//   so cost is identical to a preprocessor branch but the code stays
//   reachable to tooling.

#if defined(EINK_DISPLAY_MODEL)
  // E-ink build: slow refresh, rotatable panel, no per-second redraws
  #define FEAT_BRIGHTNESS_SETTING         0
  #define FEAT_CLOCK_SECONDS_SETTING      0
  #define FEAT_DISPLAY_ROTATION_SETTING   1
  #define FEAT_JOYSTICK_ROTATION_SETTING  1
  #define FEAT_FULL_REFRESH_SETTING       1
#else
  // OLED build: fast refresh, fixed orientation
  #define FEAT_BRIGHTNESS_SETTING         1
  #define FEAT_CLOCK_SECONDS_SETTING      1
  #define FEAT_DISPLAY_ROTATION_SETTING   0
  #define FEAT_JOYSTICK_ROTATION_SETTING  0
  #define FEAT_FULL_REFRESH_SETTING       0
#endif

// Hardware RX duty-cycle ("Power save", NodePrefs::rx_powersave): the radio
// sleeps between listens short enough for the shortest preamble expected of
// senders to span one (RadioLibWrapper::_power_save). Off from September 2026
// after near-total reception loss in the field; two causes, both fixed 2026-11:
// the sleep assumed every sender's preamble was 32 symbols (firmware before
// companion v1.16 sends 16), and a check run every loop pass talked to the
// sleeping chip over SPI, which wakes it into a deaf standby.
// Lives here (not MyMesh.h) so every `#if FEAT_RX_POWERSAVE` user sees the
// same definition: an undefined macro in `#if` silently reads as 0.
#define FEAT_RX_POWERSAVE 1

// Planned routes (PlannedRoute.h, ui-core/RouteGuide.h): a GPX route uploaded
// from the site over USB and followed. Where there is room for them on flash:
// the boards that keep messages there (HIST_FLASH: the QSPI of the L1s and the
// Watch13, an ESP32's spiffs), and the sim; the L2 turns it on itself
// (-D FEAT_ROUTES=1, with GPX import from its card).
#ifndef FEAT_ROUTES
  #if defined(HIST_FLASH) || defined(SIM_PLATFORM)
    #define FEAT_ROUTES 1
  #else
    #define FEAT_ROUTES 0
  #endif
#endif

// The display rotation at which the joystick's own up reads as up. An e-ink
// panel turned from it turns the joystick with it (UITask), on top of the
// Joystick setting. Unset, the build's own rotation: only a flip turns it.
#if defined(DISPLAY_ROTATION) && !defined(JOYSTICK_UPRIGHT_ROTATION)
  #define JOYSTICK_UPRIGHT_ROTATION DISPLAY_ROTATION
#endif

namespace Features {

#if defined(EINK_DISPLAY_MODEL)
  static constexpr bool     IS_EINK              = true;
  static constexpr bool     CLOCK_HIDE_SECONDS_DEFAULT = true;  // pref starting value
  static constexpr int      HOME_REFRESH_MS      = 30000;   // slow display polls less
  static constexpr int      LOCKSCREEN_REFRESH_MS = 30000;
#else
  static constexpr bool     IS_EINK              = false;
  static constexpr bool     CLOCK_HIDE_SECONDS_DEFAULT = false;
  static constexpr int      HOME_REFRESH_MS      = 1000;
  static constexpr int      LOCKSCREEN_REFRESH_MS = 1000;
#endif

} // namespace Features
