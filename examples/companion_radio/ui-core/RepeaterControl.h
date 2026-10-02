#pragma once
// The companion's own repeater mode (NodePrefs::client_repeat and repeat_* /
// repeater_*), shared by ui-new's Tools > Repeater and ui-lvgl's Settings >
// Repeater: switching it on / off with its radio side effects, the network
// choice (the companion's own frequency, or a dedicated radio profile), the
// profile's preset, the flood filters' ranges and labels, and the extra
// scopes it relays. The relaying itself is MyMesh (allowPacketForward()).
//
// Include after RadioControl.h (target.h / MyMesh.h first, like it).

namespace rptctl {

// Filter ranges.
static const uint8_t MAX_HOPS = 8;    // repeat_max_hops 0 (off) .. 8
static const uint8_t MAX_YIELD = 8;   // repeat_delay_boost 0 (off) .. 8, delay x(1 + v)
static const int8_t SNR_MIN = -20, SNR_MAX = 10;   // repeat_min_snr, or REPEAT_SNR_DISABLED

// Duty-cycle RX sleeps between preamble checks, so it's forced off while
// repeating (a repeater must hear every packet); the user's pref is kept.
static void applyPowerSave(const NodePrefs* p) {
  if (!p) return;
#if FEAT_RX_POWERSAVE
  radio_driver.setPowerSaving(p->rx_powersave && !p->client_repeat);
#else
  radio_driver.setPowerSaving(false);   // see Features.h FEAT_RX_POWERSAVE
#endif
}

// On: the radio moves to the profile (with one), RX power save and Adaptive
// Power Control pause. Off: the companion's own params and both come back.
// The caller saves the prefs.
static void setEnabled(NodePrefs* p, bool on) {
  if (!p) return;
  p->client_repeat = on ? 1 : 0;
  the_mesh.applyRepeaterRadio();
  applyPowerSave(p);
  radioctl::applyApc();
}

// Custom network: relay on the dedicated profile. The first switch to it
// starts the profile from the companion's current params if it isn't valid.
static void setUseProfile(NodePrefs* p, bool on) {
  if (!p) return;
  p->repeater_use_profile = on ? 1 : 0;
  if (on && !the_mesh.repeaterProfileValid()) {
    p->repeater_freq = p->freq; p->repeater_bw = p->bw;
    p->repeater_sf = p->sf;     p->repeater_cr = p->cr;
  }
  the_mesh.applyRepeaterRadio();
}

// The profile changed (freq / bw / sf / cr): live if relaying on it now.
static void applyProfile() { the_mesh.applyRepeaterRadio(); }

// List index (radioctl::presetAt) of the preset the profile matches, else -1.
static int currentPreset(const NodePrefs* p) {
  if (!p) return -1;
  const char* name; float f, b; uint8_t s, c;
  for (int i = 0; radioctl::presetAt(p, i, name, f, b, s, c); i++)
    if (radioParamsMatchPreset(p->repeater_freq, p->repeater_bw, p->repeater_sf, p->repeater_cr, f, b, s, c)) return i;
  return -1;
}

static bool choosePreset(NodePrefs* p, int idx) {
  const char* name; float f, b; uint8_t s, c;
  if (!p || !radioctl::presetAt(p, idx, name, f, b, s, c)) return false;
  p->repeater_freq = f; p->repeater_bw = b; p->repeater_sf = s; p->repeater_cr = c;
  applyProfile();
  return true;
}

// ── Filters ─────────────────────────────────────────────────────────────────

// `off`: the frontend's word for a filter that's off ("Off", ui-new's "OFF").
static void fmtHops(char* b, int n, uint8_t v, const char* off = "Off") {
  if (v) snprintf(b, n, "%u", (unsigned)v); else snprintf(b, n, "%s", off);
}
static void fmtYield(char* b, int n, uint8_t v, const char* off = "Off") {
  if (v) snprintf(b, n, "x%u", (unsigned)v + 1); else snprintf(b, n, "%s", off);
}
static void fmtSnr(char* b, int n, int8_t v, const char* off = "Off") {
  if (v == NodePrefs::REPEAT_SNR_DISABLED) snprintf(b, n, "%s", off); else snprintf(b, n, "%d dB", (int)v);
}

// Min SNR as a 0-based choice: 0 = off, then SNR_MIN..SNR_MAX.
static int snrChoiceCount() { return 1 + SNR_MAX - SNR_MIN + 1; }
static int snrToChoice(int8_t v) {
  if (v == NodePrefs::REPEAT_SNR_DISABLED || v < SNR_MIN || v > SNR_MAX) return 0;
  return 1 + v - SNR_MIN;
}
static int8_t snrFromChoice(int c) {
  return c <= 0 ? NodePrefs::REPEAT_SNR_DISABLED : (int8_t)(SNR_MIN + c - 1);
}

// ── Extra scopes (relayed besides the default one) ──────────────────────────
// Bit i of repeat_extra_scope_mask = scope-list index i + 1. Counted only up
// to the list's length: a stray high bit (e.g. from an old save file) never
// shows as picked, the same way rebuildRepeatScopes() ignores it.

static bool extraScope(const NodePrefs* p, uint8_t i) { return p && (p->repeat_extra_scope_mask & (1u << i)); }

static int extraScopesPicked(const NodePrefs* p) {
  int n = 0;
  for (uint8_t i = 0; i < the_mesh.scopeList().count; i++) if (extraScope(p, i)) n++;
  return n;
}

static void setExtraScope(NodePrefs* p, uint8_t i, bool on) {
  if (!p || i >= ScopeList::MAX_SCOPE_ENTRIES) return;
  if (on) p->repeat_extra_scope_mask |= (1u << i);
  else    p->repeat_extra_scope_mask &= ~(uint16_t)(1u << i);
  the_mesh.rebuildRepeatScopes();
}

}  // namespace rptctl
