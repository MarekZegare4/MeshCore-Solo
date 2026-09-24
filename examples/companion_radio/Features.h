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

// Hardware RX duty-cycle ("Pwr save") is disabled: the SX126x's duty-cycle
// preamble detection needs the *actual transmitted* preamble to exactly match
// what we're configured to expect (confirmed hardware behaviour, see SX1262
// datasheet 6.1.3 and https://github.com/jgromes/RadioLib/issues/1597) --
// something we can't guarantee across a mesh with mixed firmware. A mismatch
// doesn't cost a little sensitivity, it silently drops every packet from that
// sender regardless of signal strength (2026-09-22 field report: near-total
// reception loss, unaffected by antenna). The real duty-cycle implementation
// (RadioLibWrapper::armRecv()/CustomSX1262Wrapper::startPowerSaveRecv()) is
// left in place for if a network-wide compatibility mechanism ever lands --
// flipping this back to 1 requires solving that first, not just re-adding the
// Settings toggle. See docs/development/roadmap.md for the full writeup.
// Lives here (not MyMesh.h) so every `#if FEAT_RX_POWERSAVE` user sees the
// same definition: an undefined macro in `#if` silently reads as 0, which
// would split the build the day this is flipped to 1.
#define FEAT_RX_POWERSAVE 0

namespace Features {

#if defined(EINK_DISPLAY_MODEL)
  static constexpr bool     IS_EINK              = true;
  static constexpr bool     BLINK_INDICATORS     = false;   // e-ink avoids high-rate redraws
  static constexpr bool     CLOCK_HIDE_SECONDS_DEFAULT = true;  // pref starting value
  static constexpr unsigned HOME_REFRESH_MS      = 30000;   // slow display polls less
  static constexpr unsigned LOCKSCREEN_REFRESH_MS = 30000;
#else
  static constexpr bool     IS_EINK              = false;
  static constexpr bool     BLINK_INDICATORS     = true;
  static constexpr bool     CLOCK_HIDE_SECONDS_DEFAULT = false;
  static constexpr unsigned HOME_REFRESH_MS      = 1000;
  static constexpr unsigned LOCKSCREEN_REFRESH_MS = 1000;
#endif

} // namespace Features
