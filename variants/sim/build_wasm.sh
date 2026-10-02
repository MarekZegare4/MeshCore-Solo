#!/usr/bin/env bash
# Phase 2 (Emscripten) build script for the companion_radio sim.
#
# Why a shell script instead of a PlatformIO env: PlatformIO's `platform =
# native` env (see variants/sim/platformio.ini's [env:sim_companion_radio])
# was tried first, by pointing a `pre:` extra_script at this same source
# list and overriding env['CC']/env['CXX'] to em++/emcc via env.Replace().
# That override *did* take effect (confirmed: the extra_script's own print()
# showed the correct em++ path) but was silently discarded before any file
# was actually compiled -- PlatformIO's native platform package
# (~/.platformio/platforms/native/builder/main.py) unconditionally calls
# env.Tool("gcc") / env.Tool("g++") to (re-)detect the toolchain, and that
# happens to run *after* extra_scripts regardless of pre:/post: ordering,
# re-overwriting CC/CXX back to the real system clang++ every time. Every
# object file in that experiment was still compiled by Xcode's clang++, not
# em++ -- a genuine, reproducible wall (not a config typo), and PlatformIO's
# native platform has no supported hook to stop it from re-detecting the
# toolchain like that. Rather than fight PlatformIO's SCons integration
# further, this script just invokes em++ directly -- it mirrors
# platformio.ini's build_flags/build_src_filter/-I list by hand (see the
# SRCS/INCLUDES/DEFINES arrays below), so if that .ini file's source list
# ever changes, this script's arrays need the same edit alongside it.
#
# Usage:
#   variants/sim/build_wasm.sh            # release-ish build (-O2)
#   variants/sim/build_wasm.sh debug       # -O0 -g, easier to debug in devtools
#   SIM_UI=lvgl variants/sim/build_wasm.sh            # the L2 (web/lvgl.html)
#   SIM_EINK=landscape variants/sim/build_wasm.sh     # the L1 E-ink, also portrait
#
# Requires emsdk 6.0.9 (pinned; see variants/sim/tools/emsdk/ -- installed by
# this same task, see the Phase 2 report for the exact activation command).
# This script finds em++ itself via a fixed relative path, so `source
# emsdk_env.sh` first is NOT required to run it (but IS required for
# interactive use of emcc/em++/emrun directly on the command line).
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
REPO_ROOT="$(cd "$SCRIPT_DIR/../.." && pwd)"
EMSDK_DIR="$SCRIPT_DIR/tools/emsdk"
EMXX="$EMSDK_DIR/upstream/emscripten/em++"
OUT_DIR="$SCRIPT_DIR/web/build"

if [ ! -x "$EMXX" ]; then
  echo "error: em++ not found at $EMXX" >&2
  echo "Install it first:" >&2
  echo "  cd $EMSDK_DIR && python3 ./emsdk.py install 6.0.9 && python3 ./emsdk.py activate 6.0.9" >&2
  exit 1
fi

BUILD_MODE="${1:-release}"
if [ "$BUILD_MODE" = "debug" ]; then
  OPT_FLAGS=(-O0 -g)
else
  OPT_FLAGS=(-O2)
fi

# Which frontend: SIM_UI=new (default, 128x64 OLED-style ui-new) or
# SIM_UI=lvgl (320x240 touch ui-lvgl, Wio Tracker L2 shape -> web/lvgl.html).
# The lvgl build takes the LVGL 9 sources from PlatformIO's libdeps of the L2
# env, so run `pio run -e Wio_Tracker_L2_companion_solo_lvgl` once first, or
# point LVGL_DIR at a checkout of the same version (CI does, see
# .github/workflows/build-solo-sim.yml).
SIM_UI="${SIM_UI:-new}"
LVGL_DIR="${LVGL_DIR:-$REPO_ROOT/.pio/libdeps/Wio_Tracker_L2_companion_solo_lvgl/lvgl}"
if [ "$SIM_UI" = "lvgl" ] && [ ! -f "$LVGL_DIR/lvgl.h" ]; then
  echo "error: LVGL sources not found at $LVGL_DIR" >&2
  echo "Fetch them first: pio run -e Wio_Tracker_L2_companion_solo_lvgl" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"
cd "$REPO_ROOT"

# Same list as variants/sim/platformio.ini's build_src_filter, just spelled
# as real paths from the repo root instead of PlatformIO's src-relative
# "+<../x>" syntax. Keep in sync with that file by hand.
SRCS=(
  src/Dispatcher.cpp
  src/Identity.cpp
  src/Mesh.cpp
  src/Packet.cpp
  src/Utils.cpp
  src/helpers/AdvertDataHelpers.cpp
  src/helpers/BaseChatMesh.cpp
  src/helpers/ClientACL.cpp
  src/helpers/CommonCLI.cpp
  src/helpers/ConfigSerializer.cpp
  src/helpers/DeviceDiag.cpp
  src/helpers/IdentityStore.cpp
  src/helpers/RegionMap.cpp
  src/helpers/StaticPoolPacketManager.cpp
  src/helpers/TransportKeyStore.cpp
  src/helpers/TxtDataHelpers.cpp
  src/helpers/ui/buzzer.cpp
  lib/ed25519/add_scalar.c
  lib/ed25519/fe.c
  lib/ed25519/ge.c
  lib/ed25519/key_exchange.c
  lib/ed25519/keypair.c
  lib/ed25519/sc.c
  lib/ed25519/seed.c
  lib/ed25519/sha512.c
  lib/ed25519/sign.c
  lib/ed25519/verify.c
  variants/sim/sim_main.cpp
  variants/sim/target.cpp
  variants/sim/thirdparty/crypto/AES128.cpp
  variants/sim/thirdparty/crypto/AESCommon.cpp
  variants/sim/thirdparty/crypto/BigNumberUtil.cpp
  variants/sim/thirdparty/crypto/BlockCipher.cpp
  variants/sim/thirdparty/crypto/Crypto.cpp
  variants/sim/thirdparty/crypto/Curve25519.cpp
  variants/sim/thirdparty/crypto/Ed25519.cpp
  variants/sim/thirdparty/crypto/Hash.cpp
  variants/sim/thirdparty/crypto/rng_stub.cpp
  variants/sim/thirdparty/crypto/SHA256.cpp
  variants/sim/thirdparty/crypto/SHA512.cpp
  variants/sim/thirdparty/cayennelpp/CayenneLPP.cpp
  variants/sim/thirdparty/cayennelpp/CayenneLPPPolyline.cpp
  variants/sim/thirdparty/gfx/Adafruit_GFX.cpp
  examples/companion_radio/main.cpp
  examples/companion_radio/MyMesh.cpp
  examples/companion_radio/DataStore.cpp
  "examples/companion_radio/ui-$SIM_UI/UITask.cpp"
)

INCLUDES=(
  -Ivariants/sim/arduino
  -Ivariants/sim
  -Ivariants/sim/thirdparty/crypto
  -Ivariants/sim/thirdparty/cayennelpp
  -Ivariants/sim/thirdparty/arduinojson
  -Ivariants/sim/thirdparty/gfx
  -Ilib/ed25519
  -Isrc
  -Iexamples/companion_radio
  "-Iexamples/companion_radio/ui-$SIM_UI"
)

DEFINES=(
  -DSIM_PLATFORM
  -DMESH_DEBUG=0
  # The one difference from platformio.ini's native env: the canvas-backed
  # DisplayDriver (variants/sim/SimDisplayDriver.h's __EMSCRIPTEN__-guarded
  # SimDisplayDriverCanvas class) instead of the ASCII/stdout one.
  -DDISPLAY_CLASS=SimDisplayDriverCanvas
  # Dummy sentinel (no real pin) -- see platformio.ini's own comment on the
  # native env's identical flag.
  -DPIN_BUZZER=0
  # See platformio.ini's native env for the same flag: without it the splash
  # screen's "Solo <version>" bar never draws.
  -DFIRMWARE_SOLO_BUILD=1
  -DMAX_CONTACTS=100
  -DMAX_GROUP_CHANNELS=8
  # Both off by default on every real board variant except the GPS/sensor-
  # carrying ones (see variants/wio-tracker-l1/platformio.ini,
  # variants/heltec_t114/platformio.ini etc.) -- without ENV_INCLUDE_GPS,
  # HomeScreen's GPS enum entry (ui-new/UITask.cpp) doesn't even exist in
  # the build, so the "GPS" home page from sim_test_show_all_home_pages'
  # mask=0 has nothing to map to. The demo site wants both on: SimSensorManager
  # already feeds a real (JS-settable) GPS fix and one temperature/battery
  # channel (see SimSensorManager.h) -- these two flags are what actually
  # surface them as on-device pages.
  -DENV_INCLUDE_GPS=1
  -DUI_SENSORS_PAGE=1
  -DGPS_SKYVIEW   # satellite screens (GpsSkySim.h feeds a made-up sky)
)

# The on-device splash's "Solo <version>" bar shows FIRMWARE_VERSION. Without
# it, MyMesh.h falls back to "dev-<compile date>" -- what the live demo showed
# even for a tagged release. build-solo-sim.yml exports it (tag name for a
# release, "dev-<commit>" otherwise), same as build-solo-firmwares.yml does
# for the hardware builds; a local build with it unset keeps the old fallback.
if [ -n "${FIRMWARE_VERSION:-}" ]; then
  DEFINES+=("-DFIRMWARE_VERSION=\"${FIRMWARE_VERSION}\"")
fi

OUT_NAME=meshcore_sim
EXPORT_NAME=MeshCoreSim
LINK_EXTRA=()
if [ "$SIM_UI" = "lvgl" ]; then
  OUT_NAME=meshcore_sim_lvgl
  EXPORT_NAME=MeshCoreSimLvgl   # distinct factory: web/lvgl.html loads both
  # The 320x240 LCD driver (SimDisplayDriver.h's SimLcdDisplay) instead of
  # the OLED canvas one, and the L2's channel count.
  kept=()
  for d in "${DEFINES[@]}"; do
    case "$d" in -DDISPLAY_CLASS=*|-DMAX_GROUP_CHANNELS=*) ;; *) kept+=("$d") ;; esac
  done
  # The L2's PSRAM-sized limits (solo/wio-tracker-l2/platformio.ini).
  DEFINES=("${kept[@]}" -DDISPLAY_CLASS=SimLcdDisplay -DMAX_GROUP_CHANNELS=40 -DUI_ZOOM=1 -DLV_CONF_INCLUDE_SIMPLE
           -DTRAIL_CAPACITY=32768 -DTRAIL_SIMPLIFY=0 -DTRAIL_FIXED_MIN_DELTA_M=5 -DWAYPOINT_CAPACITY=64 -DHIST_CH_MAX=256 -DHIST_DM_MAX=128 -DNEARBY_MAX=64 -DHIST_ARCHIVE)
  INCLUDES+=("-I$LVGL_DIR")
  LINK_EXTRA=(-sFETCH=1)   # map downloads (LvglPort.h) go through emscripten_fetch
fi

# SIM_EINK=landscape|portrait: ui-new on the Wio Tracker L1 E-ink's 2.13" panel
# (122x250, solo/wio-tracker-l1-eink's two builds) instead of the OLED ->
# web/index.html?build=meshcore_sim_eink_landscape (or _portrait).
SIM_EINK="${SIM_EINK:-}"
if [ -n "$SIM_EINK" ]; then
  case "$SIM_EINK" in
    landscape) EINK_DEFS=(-DDISPLAY_ROTATION=1 -DEINK_LARGE_FONT=1) ;;
    portrait)  EINK_DEFS=(-DDISPLAY_ROTATION=0) ;;
    *) echo "error: SIM_EINK must be landscape or portrait" >&2; exit 1 ;;
  esac
  OUT_NAME="meshcore_sim_eink_$SIM_EINK"
  EXPORT_NAME=MeshCoreSimEink   # distinct factory: web/mesh.html?a=... loads it beside MeshCoreSim
  kept=()
  for d in "${DEFINES[@]}"; do
    case "$d" in -DDISPLAY_CLASS=*|-DMAX_GROUP_CHANNELS=*) ;; *) kept+=("$d") ;; esac
  done
  # EINK_DISPLAY_MODEL is only tested for presence outside GxEPDDisplay
  # (Features.h: the e-ink settings and timings), which the sim doesn't build.
  DEFINES=("${kept[@]}" -DDISPLAY_CLASS=SimEinkDisplay -DSIM_EINK_PANEL_W=122 -DSIM_EINK_PANEL_H=250
           -DEINK_DISPLAY_MODEL=SimEinkPanel -DMAX_GROUP_CHANNELS=40 "${EINK_DEFS[@]}")
fi

# -funsigned-char: carried over from Phase 1 verbatim -- real ARM cores
# default `char` to unsigned; em++'s target (wasm32) defaults it to signed,
# same mismatch Phase 1 hit on a native x86/ARM64 host, for the same reason
# (KEY_* codes up to 0xF3 compared as plain `char` throughout UIScreen.h/
# KeyboardWidget.h/PopupMenu.h) -- without it keyboard input compiles but
# silently never matches.
COMMON_FLAGS=(-std=c++17 -funsigned-char "${OPT_FLAGS[@]}" "${DEFINES[@]}" "${INCLUDES[@]}")

# Compile each source to its own object file, one em++ invocation per file,
# with the object path mirroring the source's own directory (obj/<same
# relative path>.o) rather than every object landing in one flat directory.
# This isn't just tidiness: a first attempt passed every source straight to
# a single em++ invocation and let it manage its own (flat) temp object
# directory internally, which broke on this specific source tree --
# lib/ed25519/sha512.c (ed25519's own plain-C SHA512, unrelated to the
# rweather/Crypto library) and variants/sim/thirdparty/crypto/SHA512.cpp
# (rweather's C++ SHA512 class) both produce a "sha512.o"/"SHA512.o" object,
# which collided as the SAME file on macOS's case-insensitive-by-default
# APFS -- wasm-ld then reported duplicate symbols for the *second* file's
# whole contents, because it was quite literally linking the first file's
# object twice under two different names. PlatformIO's native build never
# hits this because SCons mirrors each source's own directory under
# .pio/build/<env>/ -- that's exactly what this does too, by hand.
OBJ_DIR="$OUT_DIR/obj"
rm -rf "$OBJ_DIR"
OBJS=()
for src in "${SRCS[@]}"; do
  obj="$OBJ_DIR/${src%.*}.o"
  mkdir -p "$(dirname "$obj")"
  extra_flags=()
  if [ "$src" = "variants/sim/thirdparty/gfx/Adafruit_GFX.cpp" ]; then
    # Adafruit_GFX.h/.cpp branch on `#if ARDUINO >= 100` in exactly two spots
    # (which Arduino.h to #include, and whether write(uint8_t) returns
    # size_t or void) -- ARDUINO is deliberately never defined globally for
    # this build (CayenneLPP.cpp/ArduinoJson branch on #ifdef ARDUINO to
    # pick their portable std:: path instead of Arduino String/Stream), so
    # this is scoped to just this one file's own compile, matching the same
    # `#define ARDUINO 100` target.cpp does locally before its own
    # #include <Adafruit_GFX.h> (see that file's comment) -- both need it so
    # Adafruit_GFX's declaration (parsed by target.cpp) and its out-of-line
    # definition (parsed here) agree on the same write(uint8_t) signature.
    extra_flags=(-DARDUINO=100)
  fi
  "$EMXX" -c "${COMMON_FLAGS[@]}" ${extra_flags[@]+"${extra_flags[@]}"} "$src" -o "$obj"
  OBJS+=("$obj")
done

# ui-lvgl: LVGL itself and the generated fonts, compiled as C. Several hundred
# files that rarely change, so their objects are cached in obj_lvgl/ and only
# rebuilt when the source or lv_conf.h is newer.
if [ "$SIM_UI" = "lvgl" ]; then
  EMCC="$EMSDK_DIR/upstream/emscripten/emcc"
  LV_CONF="examples/companion_radio/ui-lvgl/lv_conf.h"
  # Only LVGL + lv_conf.h on the path: examples/companion_radio/features.h
  # would shadow the libc <features.h> the C headers include.
  C_FLAGS=(-std=gnu99 "${OPT_FLAGS[@]}" "${DEFINES[@]}" "-I$LVGL_DIR" -Iexamples/companion_radio/ui-lvgl)
  LV_OBJ_DIR="$OUT_DIR/obj_lvgl"
  # Another LVGL version: its files needn't be newer than the cached objects.
  LV_VER="$(grep '"version"' "$LVGL_DIR/library.json")"
  if [ "$(cat "$LV_OBJ_DIR/.lvgl_version" 2>/dev/null)" != "$LV_VER" ]; then
    rm -rf "$LV_OBJ_DIR"
    mkdir -p "$LV_OBJ_DIR"
    echo "$LV_VER" > "$LV_OBJ_DIR/.lvgl_version"
  fi
  C_SRCS=()
  while IFS= read -r f; do C_SRCS+=("$f"); done < <(
    find "$LVGL_DIR/src" -name '*.c' | sed "s|^$REPO_ROOT/||" | sort
    find examples/companion_radio/ui-lvgl/fonts -name '*.c' | sort)
  built=0
  for src in "${C_SRCS[@]}"; do
    obj="$LV_OBJ_DIR/${src%.*}.o"
    if [ ! -f "$obj" ] || [ "$src" -nt "$obj" ] || [ "$LV_CONF" -nt "$obj" ]; then
      mkdir -p "$(dirname "$obj")"
      "$EMCC" -c "${C_FLAGS[@]}" "$src" -o "$obj"
      built=$((built + 1))
    fi
    OBJS+=("$obj")
  done
  echo "LVGL/fonts: $built of ${#C_SRCS[@]} C files rebuilt"
fi

# FS is exported so a host page (or a manual verification script) can
# directly inspect what DataStore/IdentityStore actually wrote -- e.g.
# Module.FS.readFile('/sim_data/identity/_main.id') -- to prove IDBFS
# persistence with a real file-content comparison across a reload, not just
# "the app didn't crash". Not required for the app itself.
#
# EXPORTED_FUNCTIONS=_main,_malloc,_free (Phase 3 addition): every
# EMSCRIPTEN_KEEPALIVE-attributed function (all the sim_*() hooks across
# variants/sim/ and examples/companion_radio/) is exported regardless of
# this list -- that's what the attribute is FOR -- so this only adds
# malloc()/free() themselves, needed by web/mesh.html's JS "ether" to
# allocate a scratch buffer per instance for sim_radio_poll_tx()/
# sim_radio_inject_rx() (see variants/sim/SimRadio.h). Without this,
# Module._malloc() would abort at runtime with "malloc() called but not
# included in the build" -- confirmed by hitting exactly that during Phase
# 3. Purely additive: nothing Phase 2's web/index.html already does
# (sim_enqueue_key() with a plain number, no buffer marshaling) is affected.
#
# HEAPF32,HEAP32 (relay-bridge addition): sim_radio_get_params() writes its
# freq/bw (float) and sf/cr (int) results into a JS-malloc'd scratch buffer
# via out-params -- reading them back needs the typed-array views, same
# reasoning as HEAPU8 above for sim_radio_poll_tx()'s byte buffer.
"$EMXX" \
  "${OBJS[@]}" \
  -lidbfs.js \
  -sALLOW_MEMORY_GROWTH=1 \
  -sFORCE_FILESYSTEM=1 \
  -sMODULARIZE=1 \
  -sEXPORT_NAME=$EXPORT_NAME \
  -sENVIRONMENT=web \
  -sEXIT_RUNTIME=0 \
  -sEXPORTED_RUNTIME_METHODS=FS,ccall,cwrap,HEAPU8,HEAPF32,HEAP32 \
  -sEXPORTED_FUNCTIONS=_main,_malloc,_free \
  ${LINK_EXTRA[@]+"${LINK_EXTRA[@]}"} \
  -o "$OUT_DIR/$OUT_NAME.js"

echo ""
echo "Built: $OUT_DIR/$OUT_NAME.js (+ .wasm alongside it)"
echo "Serve variants/sim/web/ locally and open index.html, e.g.:"
echo "  cd $SCRIPT_DIR/web && python3 -m http.server 8080"
