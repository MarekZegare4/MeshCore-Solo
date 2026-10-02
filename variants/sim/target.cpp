#include <Arduino.h>
#include <cstring>
#include "target.h"
#include "SimRNG.h"

// Global stdout-backed Serial object, declared extern in Arduino.h.
SimSerialClass Serial;

SimMainBoard board;
SimRadio radio_driver;
SimRTCClock rtc_clock;
// Phase 4: SimSensorManager wires in the sim's GPS + one JS-settable
// environment channel -- see SimSensorManager.h (its methods' bodies are
// defined further down in this file, not there, for the reason explained
// in that header's comment).
SimSensorManager sensors;

#ifdef DISPLAY_CLASS
DISPLAY_CLASS display;
#endif

// SimSensorManager's methods (see that header's comment on why they're
// defined here rather than inline).
LocationProvider* SimSensorManager::getLocationProvider() {
  return &sim_location_provider();
}

bool SimSensorManager::querySensors(uint8_t requester_permissions, CayenneLPP& telemetry) {
  if ((requester_permissions & TELEM_PERM_LOCATION) && sim_location_provider().isValid()) {
    telemetry.addGPS(TELEM_CHANNEL_SELF, (float)node_lat, (float)node_lon, (float)node_altitude);
  }
  if (requester_permissions & TELEM_PERM_ENVIRONMENT) {
    telemetry.addTemperature(TELEM_CHANNEL_SELF + 1, envTemperatureRef());
  }
  return true;
}

bool radio_init() {
  // No real radio hardware to initialise -- always succeeds (see
  // variants/sim/SimRadio.h; Phase 3 of the sim plan is where two SimRadio
  // instances actually exchange bytes through a shared in-memory "ether").
  return true;
}

mesh::LocalIdentity radio_new_identity() {
  static SimRNG rng;
  rng.begin();
  return mesh::LocalIdentity(&rng);
}

// DISPLAY_CLASS too, not just __EMSCRIPTEN__: SimDisplayDriverCanvas itself
// only exists when DISPLAY_CLASS is defined (target.h only #includes
// SimDisplayDriver.h -- where the class lives -- inside its own #ifdef
// DISPLAY_CLASS block), and the headless repeater/room_server wasm builds
// never define DISPLAY_CLASS at all. This was a latent gap from the
// pixel-perfect-font change (companion_radio's own build_wasm.sh happens to
// always define DISPLAY_CLASS, so it never surfaced there) -- only found now
// that build_wasm_repeater.sh got rebuilt for Phase 4.
#if defined(__EMSCRIPTEN__) && defined(DISPLAY_CLASS)
// Real bitmap-font text rendering for SimDisplayDriverCanvas::print()
// (declared in SimDisplayDriver.h, defined here -- the one TU allowed to
// include MiscFixedRenderer.h; see that header's own "include only from a
// .cpp" comment). Uses the exact same font tables + glyph-plotting math a
// real MeshCore-Solo board renders with OLED_MISC_FIXED_FONT=1 (see
// solo/heltec_v3/platformio.ini), instead of the browser's own system font.
// Adafruit_GFX.h/.cpp branch on `#if ARDUINO >= 100` in exactly two spots
// (which Arduino.h to #include, and whether write(uint8_t) returns size_t
// or void) -- our own Arduino.h shim deliberately never defines ARDUINO
// globally (other vendored libs branch on #ifdef ARDUINO to pick their
// portable std:: path instead of Arduino String/Stream), so define it here,
// scoped to this one translation unit, purely so Adafruit_GFX picks the
// modern branch that actually matches our Print.h shim's
// `size_t write(uint8_t)` signature.
#define ARDUINO 100
#include <Adafruit_GFX.h>
#include <helpers/ui/MiscFixedRenderer.h>

// Adafruit_GFX subclass that plots into a plain 128x64 byte buffer (one
// pixel per byte, 0/1) instead of real display hardware -- drawPixel() is
// the only thing MiscFixedRenderer.h's glyph-plotting code and
// Adafruit_GFX's own fillRect()/writeFillRect() (used for the "unmapped
// codepoint" substitution box) ultimately call down to.
class SimGfxCanvas : public Adafruit_GFX {
public:
  uint8_t px[128 * 64];
  // Inclusive bounding box of everything plotted since the last resetDirty().
  // Without it, print() blitted all 8192 cells on every single call -- and a
  // busy screen makes dozens of print() calls per frame, at 60fps, so the
  // JS-side pixel loop dominated the whole frame budget for what is usually
  // one short row of text.
  int dx0, dy0, dx1, dy1;
  SimGfxCanvas() : Adafruit_GFX(128, 64) { memset(px, 0, sizeof(px)); resetDirty(); }
  void resetDirty() { dx0 = 128; dy0 = 64; dx1 = -1; dy1 = -1; }
  bool isDirty() const { return dx1 >= dx0 && dy1 >= dy0; }
  void drawPixel(int16_t x, int16_t y, uint16_t color) override {
    if ((unsigned)x >= 128 || (unsigned)y >= 64) return;
    px[y * 128 + x] = (color != 0) ? 1 : 0;
    if (x < dx0) dx0 = x;
    if (x > dx1) dx1 = x;
    if (y < dy0) dy0 = y;
    if (y > dy1) dy1 = y;
  }
};

// Real MiscFixed metrics, same source of truth the glyph plotting above
// uses -- see SimDisplayDriver.h for why these are here and not inline.
uint16_t SimDisplayDriverCanvas::getTextWidth(const char* str) {
  return str ? miscFixedTextWidth(str, _text_sz) : 0;
}

uint16_t SimDisplayDriverCanvas::getCodepointWidth(uint32_t cp) {
  return miscFixedXAdvance(cp, _text_sz);
}

void SimDisplayDriverCanvas::print(const char* str) {
  if (!str) return;
  static SimGfxCanvas gfx;
  // Only the previously-dirtied region needs clearing, not all 8 KB.
  if (gfx.isDirty()) {
    for (int y = gfx.dy0; y <= gfx.dy1; y++)
      memset(&gfx.px[y * 128 + gfx.dx0], 0, (size_t)(gfx.dx1 - gfx.dx0 + 1));
  }
  gfx.resetDirty();
  gfx.setCursor(_cursor_x, _cursor_y);
  // color arg is just our own internal "lit" marker (1) -- the real on-screen
  // white/black choice is applied once at blit time below, from _color, same
  // as every other primitive in this class. sz is the real current text
  // size (set via setTextSize(), e.g. the Clock screen's big digits) --
  // miscFixedPrint()/miscFixedDrawGlyph() scale both the glyph pixels and
  // the advance width by it already.
  miscFixedPrint(gfx, str, _text_sz, 1);

  // startFrame() already blanks the whole canvas to black every frame, so
  // only the lit pixels need drawing here -- unlit buffer cells are already
  // correct background. One EM_ASM call blits the buffer (reading it directly
  // out of wasm memory, same pattern as drawXbm() below) rather than one call
  // per glyph pixel, and only over the rows/columns this string actually
  // touched rather than the full 128x64.
  if (gfx.isDirty()) {
    EM_ASM({
      if (!Module.__simCtx) return;
      var ctx = Module.__simCtx;
      var buf = $0;
      ctx.fillStyle = UTF8ToString($5) === 'L' ? '#fff' : '#000';
      for (var y = $2; y <= $4; y++) {
        for (var x = $1; x <= $3; x++) {
          if (HEAPU8[buf + y * 128 + x]) ctx.fillRect(x, y, 1, 1);
        }
      }
    }, gfx.px, gfx.dx0, gfx.dy0, gfx.dx1, gfx.dy1, (_color != DARK) ? "L" : "D");
  }

  // Same external contract as every other DisplayDriver backend here (see
  // SimDisplayDriver's own ASCII print()): only _cursor_x advances by the
  // printed width; _cursor_y is left for the caller to manage via
  // setCursor() between lines, even though miscFixedPrint() itself tracks a
  // real y position across embedded '\n's internally.
  _cursor_x += getTextWidth(str);
}

#ifdef SIM_EINK_PANEL_W
#include <helpers/ui/EinkGfxDisplayImpl.h>

// The panel's RAM: 1 bit a pixel, ink = 1. GFXcanvas1 rotates like GxEPD2 does.
Adafruit_GFX& SimEinkDisplay::canvas() {
  static GFXcanvas1 c(PANEL_W, PANEL_H);
  return c;
}

SimEinkDisplay::SimEinkDisplay()
    : EinkGfxDisplay((DISPLAY_ROTATION & 1) ? PANEL_H : PANEL_W,
                     (DISPLAY_ROTATION & 1) ? PANEL_W : PANEL_H, canvas(), 1, 0) {
  canvas().setRotation(DISPLAY_ROTATION);
}

// Sizes the page's canvas to the panel as it's turned, at Module.simEinkScale
// (default 3) CSS pixels a pixel; 0 leaves the CSS size to the page.
static void simEinkFitCanvas(int w, int h) {
  EM_ASM({
    var tag = (typeof Module !== 'undefined' && Module['simInstanceTag']) ? Module['simInstanceTag'] : '';
    var c = document.getElementById(tag ? ('sim-canvas-' + tag) : 'sim-canvas');
    if (!c) { console.error('[sim] e-ink canvas not found in the host page'); return; }
    var k = ('simEinkScale' in Module) ? Module.simEinkScale : 3;
    c.width = $0; c.height = $1;
    if (k) { c.style.width = ($0 * k) + 'px'; c.style.height = ($1 * k) + 'px'; }
    Module.__simCtx = c.getContext('2d');
    Module.__simCtx.imageSmoothingEnabled = false;
    Module.__einkRefreshes = Module.__einkRefreshes || 0;
  }, w, h);
}

bool SimEinkDisplay::begin() {
  _on = true;
  simEinkFitCanvas(width(), height());
  return true;
}

void SimEinkDisplay::clear() {
  canvas().fillScreen(0);
  forceRedraw();
}

void SimEinkDisplay::setDisplayRotation(uint8_t rot) {
  canvas().setRotation(rot & 3);
  setDimensions(canvas().width(), canvas().height());
  updateLayout();
  simEinkFitCanvas(width(), height());
}

// Only a frame that differs reaches the page, as only that one costs the
// board a refresh. Read back through getPixel(), which applies the rotation.
void SimEinkDisplay::endFrame() {
  if (!frameChanged()) return;
  const int w = width(), h = height();
  static uint8_t px[SIM_EINK_PANEL_W * SIM_EINK_PANEL_H];
  const GFXcanvas1& c = (const GFXcanvas1&)canvas();
  for (int y = 0; y < h; y++)
    for (int x = 0; x < w; x++) px[y * w + x] = c.getPixel(x, y);
  EM_ASM({
    if (!Module.__simCtx) return;
    var w = $1;
    var h = $2;
    var img = Module.__simCtx.createImageData(w, h);
    var d = img.data;
    for (var i = 0; i < w * h; i++) {
      var ink = HEAPU8[$0 + i];
      var o = i * 4;
      d[o] = ink ? 0x1a : 0xdc;
      d[o + 1] = ink ? 0x1a : 0xdc;
      d[o + 2] = ink ? 0x1a : 0xd2;
      d[o + 3] = 255;
    }
    Module.__simCtx.putImageData(img, 0, 0);
    Module.__einkRefreshes++;
  }, px, w, h);
}
#endif // SIM_EINK_PANEL_W
#endif // __EMSCRIPTEN__
