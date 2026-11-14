#!/usr/bin/env python3
"""
Tests on a device plugged in over USB: what the configurator and the host
tools drive (the CLI, settings, Home pages, quick messages, melodies,
waypoints, routes, screenshots), each test putting back what it changed.
Optionally builds and flashes a firmware first, so one command does a whole
iteration:

    uv run tools/hwtest.py                                  # the device plugged in
    uv run tools/hwtest.py --flash WioTrackerL1_companion_solo_dual
    uv run tools/hwtest.py --only settings,routes --port /dev/cu.usbmodem21101
    uv run tools/hwtest.py --reboot                         # also: settings survive a restart
    uv run tools/hwtest.py --list

--flash builds a test firmware (-D SOLO_TEST_HOOKS, in .pio/build-test, apart
from the release builds): it adds CLI commands for the tests that need a hand
on the device -- keys, a fake GPS fix, following a route (ui-core/TestHooks.h).
`ver` says "+test" on it. --release flashes the plain firmware instead; the
tests that need the hooks are skipped on it.

Flashing: an nRF52 board (a firmware.uf2 in the build) goes into its
bootloader by the 1200-baud touch and gets the UF2 copied to the drive that
appears; an ESP32 one through `pio run -t upload`. Before either the device
must answer as a Solo companion (an observer or repeater on that port does
not), and with more than one plugged in, --port says which.

Screenshots and a report go to .pio/hwtest/<board>-<time>/.
"""

import argparse
import datetime
import glob
import json
import math
import os
import re
import struct
import subprocess
import sys
import time
import traceback
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import serial
import serial.tools.list_ports
from companion import Companion, DeviceError
import config
import route
import screenshot

ROOT = Path(__file__).resolve().parent.parent
PIO = os.path.expanduser("~/.platformio/penv/bin/pio")

# Settings a test doesn't flip even for a moment: they delete messages or
# contacts, can power the device off, or send an advert.
SETTINGS_SKIP = {"messages.keep", "messages.expire", "contacts.expiry", "batt.shutdown", "advert.interval"}


class Fail(Exception):
    pass


def check(cond, msg):
    if not cond:
        raise Fail(msg)


class Skip(Exception):
    pass


# ── Device ───────────────────────────────────────────────────────────────────

def candidate_ports():
    out = []
    for p in serial.tools.list_ports.comports():
        if "usbmodem" in p.device or "ACM" in p.device or p.device.startswith("COM") or "usbserial" in p.device:
            if p.device.startswith("/dev/tty.") and sys.platform == "darwin":
                continue
            out.append(p.device)
    return out


def probe(port):
    """(board, firmware) when a Solo companion answers on `port`, else None."""
    try:
        dev = Companion(port, timeout=2.0)
    except (serial.SerialException, OSError, SystemExit):
        return None
    try:
        info = dev.ask(bytes([config.CMD_DEVICE_QUERY, config.CLI_MIN_VER]), config.RESP_DEVICE_INFO)
        s = lambda a, n: info[a:a + n].split(b"\0")[0].decode(errors="replace")
        return s(20, 40), s(60, 20)
    except (DeviceError, serial.SerialException, OSError):
        return None   # not a companion, or gone mid-way (an ESP32 restarting)
    finally:
        try:
            dev.close()
        except (serial.SerialException, OSError):
            pass


def wait_port(timeout, gone_first=None):
    """A Solo device's port once one answers; first waiting for `gone_first` to vanish."""
    end = time.time() + timeout
    if gone_first:
        while time.time() < end and gone_first in candidate_ports():
            time.sleep(0.3)
    while time.time() < end:
        for p in candidate_ports():
            if probe(p):
                return p
        time.sleep(1)
    return None


class RetryCli(config.Cli):
    """The CLI always answers with a reply frame, errors included, so an error
    frame in its place belongs to something else (seen once, on the first
    commands after flashing): noted, and the command asked again."""

    def __init__(self, dev, dut):
        super().__init__(dev)
        self.dut = dut

    def run(self, cmd):
        try:
            return super().run(cmd)
        except DeviceError as e:
            self.dut.strays.append(f"{cmd!r}: {e}")
            time.sleep(0.3)
            self.dev._rx.clear()
            return super().run(cmd)


class Dut:
    """The device under test: the companion link, the CLI on it, and how to reconnect."""

    def __init__(self, port):
        self.port = port
        self.open()

    def open(self):
        self.dev = Companion(self.port)
        self.cli = RetryCli(self.dev, self)
        self.strays = []
        info = self.dev.ask(bytes([config.CMD_DEVICE_QUERY, config.CLI_MIN_VER]), config.RESP_DEVICE_INFO)
        s = lambda a, n: info[a:a + n].split(b"\0")[0].decode(errors="replace")
        self.proto, self.board, self.firmware = info[1], s(20, 40), s(60, 20)
        self.hooks = "+test" in self.cli.run("ver")

    def close(self):
        try:
            self.dev.close()
        except Exception:
            pass

    def reboot(self):
        try:
            self.cli.run("reboot")
        except (DeviceError, serial.SerialException, OSError):
            pass   # the port goes away mid-reply
        self.close()
        p = wait_port(40, gone_first=self.port)
        check(p, "the device didn't come back after a reboot")
        self.port = p
        time.sleep(1)
        self.open()

    def knows(self, cmd):
        r = self.cli.run(cmd)
        return not re.search(r"(?i)unknown command", r)


# ── Tests ────────────────────────────────────────────────────────────────────
# Each takes the Dut and the output directory, raises Fail / Skip, and leaves
# the device as it found it. Returns a short note for the report, or None.

TESTS = []


def test(name, needs_reboot=False):
    def deco(fn):
        TESTS.append((name, fn, needs_reboot))
        return fn
    return deco


@test("identity")
def t_identity(d, out):
    check(d.proto >= config.CLI_MIN_VER, f"protocol {d.proto}, the CLI needs {config.CLI_MIN_VER}")
    ver = d.cli.run("ver")
    check(ver and "unknown" not in ver.lower(), f"ver: {ver!r}")
    board = d.cli.run("board")
    check(board, "board: empty")
    return f"{board}, {ver}"


@test("unknown-command")
def t_unknown(d, out):
    r = d.cli.run("no such command here")
    check(re.search(r"(?i)unknown command", r), f"got {r!r}")
    r = d.cli.run("ab|no such command")
    check(r.startswith("ab|"), f"the xx| prefix isn't reflected: {r!r}")


@test("batch")
def t_batch(d, out):
    if not d.knows("batch off"):
        raise Skip("no batch in this firmware")
    v = d.cli.get("get tx")
    t0 = time.time(); d.cli.put(f"set tx {v}"); plain = time.time() - t0
    d.cli.put("batch on")
    try:
        t0 = time.time()
        for _ in range(5):
            d.cli.put(f"set tx {v}")
        batched = (time.time() - t0) / 5
    finally:
        t0 = time.time(); d.cli.put("batch off"); flush = time.time() - t0
    check(batched < 0.2, f"a set in a batch took {batched * 1000:.0f} ms")
    return f"set {plain * 1000:.0f} ms, in a batch {batched * 1000:.0f} ms, the save {flush * 1000:.0f} ms"


@test("radio")
def t_radio(d, out):
    r = d.cli.get("get radio")
    check(re.match(r"^[\d.]+,[\d.]+,\d+,\d+$", r), f"get radio: {r!r}")
    d.cli.put(f"set radio {r}")
    check(d.cli.get("get radio") == r, "the radio changed after setting it to itself")
    tx = d.cli.get("get tx")
    d.cli.put(f"set tx {tx}")
    check(d.cli.get("get tx") == tx, "tx changed after setting it to itself")
    return r + f", {tx} dBm"


def opts(d, key):
    r = d.cli.run(f"opts {key}")
    if not r.startswith("> "):
        return None
    f = r[2:].split("|")
    return {"where": f[0], "label": f[1], "value": f[2], "choices": [c for c in f[3:] if c != "..."]}


@test("settings")
def t_settings(d, out):
    keys = config.list_keys(d.cli)
    check(keys, "no keys")
    if not d.knows("opts " + keys[0]):
        raise Skip("no opts in this firmware (before 2.3)")
    batch = d.knows("batch on") and d.cli.run("batch on") == "OK"
    bad, done, skipped = [], 0, []
    try:
        for k in keys:
            o = opts(d, k)
            if o is None:
                continue   # not on this board
            if k in SETTINGS_SKIP:
                skipped.append(k)
                continue
            other = next((c for c in o["choices"] if c.lower() != o["value"].lower()), None)
            if other is None:
                continue
            try:
                d.cli.put(f"set {k} {other}")
                now = opts(d, k)["value"]
                if now.lower() != other.lower():
                    bad.append(f"{k}: set {other!r}, reads {now!r}")
            finally:
                d.cli.put(f"set {k} {o['value']}")
            back = opts(d, k)["value"]
            if back.lower() != o["value"].lower():
                bad.append(f"{k}: didn't go back to {o['value']!r} ({back!r})")
            done += 1
    finally:
        if batch:
            d.cli.run("batch off")
    check(not bad, "; ".join(bad))
    return f"{done} flipped and back" + (f", left alone: {', '.join(skipped)}" if skipped else "")


@test("settings-persist", needs_reboot=True)
def t_persist(d, out):
    key = "units.imperial"
    o = opts(d, key)
    if o is None:
        raise Skip(f"no {key} here")
    other = "on" if o["value"] == "off" else "off"
    batch = d.knows("batch on") and d.cli.run("batch on") == "OK"
    d.cli.put(f"set {key} {other}")
    try:
        if batch:
            time.sleep(7)   # no batch off: the device saves by itself once the CLI goes quiet
        d.reboot()
        check(opts(d, key)["value"] == other, f"{key} wasn't kept over a restart" + (" (the batch's idle save)" if batch else ""))
    finally:
        d.cli.put(f"set {key} {o['value']}")
    return "kept over a restart" + (", saved by the batch's idle timeout" if batch else "")


@test("pages")
def t_pages(d, out):
    if not d.knows("pages"):
        raise Skip("this device sets Home's pages on itself")
    before = d.cli.get("pages")
    shown, hidden = config.parse_pages(before)
    check(len(shown) >= 2, f"pages: {before!r}")
    swapped = [shown[1], shown[0]] + shown[2:]
    try:
        d.cli.put("set pages " + ", ".join(swapped))
        now, _ = config.parse_pages(d.cli.get("pages"))
        check(now == swapped, f"set {swapped}, reads {now}")
    finally:
        d.cli.put("set pages " + ", ".join(shown))
    check(d.cli.get("pages") == before, "Home's pages didn't go back")


@test("quick-messages")
def t_msgs(d, out):
    if not d.knows("msgs"):
        raise Skip("no quick messages over the CLI")
    slot = 10
    before = d.cli.get(f"get msg.{slot}")
    text = "hwtest ąę 123"
    try:
        d.cli.put(f"set msg.{slot} {text}")
        check(d.cli.get(f"get msg.{slot}") == text, "the message doesn't read back")
    finally:
        d.cli.put(f"set msg.{slot} {before}" if before else f"set msg.{slot}")
    check(d.cli.get(f"get msg.{slot}") == before, "msg.10 didn't go back")


@test("melodies")
def t_melodies(d, out):
    if not d.knows("get melody.2"):
        raise Skip("no melodies over the CLI")
    raw = d.cli.get("get melody.2")
    bpm, _, hexs = raw.partition(" ")
    tune = "Tune:d=8,o=5,b=120:c,e,g,c6"
    b2, notes, _ = config.parse_rtttl(tune)
    try:
        d.cli.put(f"set melody.2 {b2} {config.hex_notes(notes)}")
        rb, rn = config.get_melody(d.cli, 2)
        check(rb == b2 and config.hex_notes(rn) == config.hex_notes(notes), "the melody doesn't read back")
        check(config.to_rtttl(rb, rn).split(":", 1)[1] == config.to_rtttl(b2, notes).split(":", 1)[1], "RTTTL round trip differs")
    finally:
        d.cli.put(f"set melody.2 {bpm} {hexs}".strip())
    check(d.cli.get("get melody.2") == raw, "melody 2 didn't go back")


@test("waypoints")
def t_waypoints(d, out):
    if not d.knows("wpts"):
        raise Skip("no waypoints over the CLI")
    have, cap = config.get_waypoints(d.cli)
    if len(have) >= cap:
        raise Skip(f"waypoints full ({cap})")
    n = len(have) + 1
    lat, lon = 49.232417, 19.981722
    d.cli.put(f"set wpt.{n} {lat:.6f},{lon:.6f} hwtest")
    try:
        now, _ = config.get_waypoints(d.cli)
        check(len(now) == n, f"{len(now)} waypoints after adding one to {len(have)}")
        w = now[-1]
        check(abs(w["lat"] - lat) < 1e-5 and abs(w["lon"] - lon) < 1e-5 and w["name"] == "hwtest", f"reads back {w}")
        added, there, full, _ = config.add_waypoints(d.cli, [{"lat": lat, "lon": lon, "name": "x"}], print)
        check((added, there) == (0, 1), "the same place added twice")
    finally:
        d.cli.put(f"set wpt.{n}")
    after, _ = config.get_waypoints(d.cli)
    check(after == have, "the waypoints didn't go back")


def upload_route(d, info, pts, name):
    """Sends (lat, lon, ele|None) points as a route; its slot."""
    has_ele = any(p[2] is not None for p in pts)
    f = d.dev.ask(bytes([route.CMD_ROUTE, route.BEGIN, 0xFF]) + struct.pack("<HB", len(pts), 1 if has_ele else 0)
                  + name.encode(), route.CMD_ROUTE)
    slot = f[2]
    try:
        per = info["per_frame"]
        for i in range(0, len(pts), per):
            chunk = pts[i:i + per]
            body = b"".join(struct.pack("<iih", round(p[0] * 1e6), round(p[1] * 1e6),
                                        route.ELE_NONE if p[2] is None else round(p[2])) for p in chunk)
            d.dev.ask(bytes([route.CMD_ROUTE, route.DATA]) + struct.pack("<HB", i, len(chunk)) + body, 0)
        d.dev.ask(bytes([route.CMD_ROUTE, route.END]), route.CMD_ROUTE)
    except DeviceError:
        d.dev.ask(bytes([route.CMD_ROUTE, route.ABORT]), 0)
        raise
    return slot


@test("routes")
def t_routes(d, out):
    try:
        info = route.dev_info(d.dev)
    except DeviceError as e:
        raise Skip(f"no routes ({e})")
    if info["used"] == (1 << info["slots"]) - 1:
        raise Skip("every route slot is taken")
    before = route.dev_list(d.dev, info)
    # A loop of 40 points round a 300 m circle, with elevation.
    pts = [(49.25 + 0.0027 * math.sin(a), 19.95 + 0.0041 * math.cos(a), 1000 + 10 * math.sin(a))
           for a in (2 * math.pi * i / 40 for i in range(41))]
    want = route.length(pts)
    slot = upload_route(d, info, pts, "hwtest")
    try:
        rows = route.dev_list(d.dev, route.dev_info(d.dev))
        mine = [r for r in rows if r[0] == slot]
        check(mine and mine[0][1] == len(pts) and mine[0][3] == "hwtest", f"listed as {mine}")
        got = mine[0][2]
        check(abs(got - want) < max(5, want * 0.01), f"length {got} m, sent {want:.0f} m")
    finally:
        d.dev.ask(bytes([route.CMD_ROUTE, route.DELETE, slot]), 0)
    check(route.dev_list(d.dev, route.dev_info(d.dev)) == before, "the routes didn't go back")
    return f"{len(pts)} points, {want:.0f} m, slot {slot}"


def shot(d, out, name):
    """The screen as a PNG in `out`; its size, or None without screenshots."""
    ser = d.dev.ser
    ser.reset_input_buffer()
    screenshot.send_command(ser, screenshot.CMD_GET_SCREENSHOT)
    res = screenshot.receive_screenshot(ser)
    d.dev._rx.clear()
    if res is None:
        return None
    buf, w, h, kind, rot = res
    img = screenshot.buffer_to_image(buf, w, h, kind, rotation=rot, scale=1 if w >= 240 else 3)
    img.save(out / name)
    return img


@test("screenshot")
def t_screenshot(d, out):
    img = shot(d, out, "screen.png")
    if img is None:
        raise Skip("no screenshot (old firmware, or the phone app holds it)")
    check(img.getextrema() != ((0, 0), (0, 0), (0, 0)), "the screen is all black")
    return f"{img.width}x{img.height}, {out / 'screen.png'}"


# ── With the test hooks (a --flash build) ──────────────────────────────────

def need_hooks(d):
    if not d.hooks:
        raise Skip("needs a test build (--flash, not --release)")


def ui_state(d):
    r = d.cli.get("ui").split("|")
    return {"screen": r[0], "alert": r[1], "locked": r[2] == "1", "display": r[3]}


@test("keys")
def t_keys(d, out):
    need_hooks(d)
    if not d.knows("key cancel"):
        raise Skip("no keys on this frontend (a touch screen)")
    for _ in range(4):   # wake it, and back out of wherever it is
        d.cli.put("key cancel")
        time.sleep(0.3)
    d.cli.put("key home")
    time.sleep(0.5)
    st = ui_state(d)
    check(st["display"] == "on", "the screen is off after keys")
    if st["locked"]:
        raise Skip("the screen is locked")
    check(st["screen"] == "home", f"Home key: on {st['screen']}")
    d.cli.put("key enter long")   # Home's context menu ...
    time.sleep(0.5)
    shot(d, out, "home-menu.png")
    d.cli.put("key cancel")       # ... and out of it
    time.sleep(0.3)
    check(ui_state(d)["screen"] == "home", "not back on Home after the menu")
    r = d.cli.run("key sideways")
    check(r.startswith("Error"), f"a bad key name: {r!r}")


M_LAT = 1 / 111320.0


def offset(lat, lon, north_m, east_m):
    return lat + north_m * M_LAT, lon + east_m * M_LAT / math.cos(math.radians(lat))


def guide_state(d):
    st, slot, left, off, label = d.cli.get("guide").split(",", 4)
    return {"state": int(st), "slot": int(slot), "left": int(left), "off": int(off), "label": label}


@test("gps-fake")
def t_gps(d, out):
    need_hooks(d)
    lat, lon = 49.2319, 19.9817
    try:
        d.cli.put(f"gps {lat},{lon},1500")
        time.sleep(1.5)
        r = d.cli.run("gps 91,0")
        check(r.startswith("Error"), f"91 N taken: {r!r}")
        r = d.cli.run("gps nowhere")
        check(r.startswith("Error"), f"a bad position taken: {r!r}")
    finally:
        d.cli.put("gps off")


@test("route-walk")
def t_route_walk(d, out):
    """A straight 800 m route north, walked at 40 m a step: on it, off it to the
    east, back on, to the end. The guide's state and its alerts each step."""
    need_hooks(d)
    try:
        info = route.dev_info(d.dev)
    except DeviceError as e:
        raise Skip(f"no routes ({e})")
    if info["used"] == (1 << info["slots"]) - 1:
        raise Skip("every route slot is taken")
    lat0, lon0 = 49.25, 19.95
    pts = [offset(lat0, lon0, 100 * i, 0) + (None,) for i in range(9)]
    slot = upload_route(d, info, pts, "hwtest walk")
    alerts, log = [], []

    def at(north, east, wait=1.1):
        la, lo = offset(lat0, lon0, north, east)
        d.cli.put(f"gps {la:.6f},{lo:.6f}")
        time.sleep(wait)
        g = guide_state(d)
        a = ui_state(d)["alert"] if d.knows("ui") else "-"
        if a != "-" and (not alerts or alerts[-1] != a):
            alerts.append(a)
        log.append(f"{north:4.0f} N {east:3.0f} E: {g['state']} {g['label']}" + (f"  [{a}]" if a != "-" else ""))
        return g

    try:
        d.cli.put(f"gps {lat0},{lon0}")
        time.sleep(1.2)
        d.cli.put(f"follow {slot}")
        g = at(0, 0, 2.2)
        check(g["state"] == 1, f"at the start: state {g['state']} ({g['label']})")
        check(abs(g["left"] - 800) < 30, f"at the start {g['left']} m left, not 800")
        for n in range(40, 321, 40):
            g = at(n, 0)
        check(g["state"] == 1 and abs(g["left"] - 480) < 40, f"at 320 m: {g}")
        for e in (25, 50, 75, 100):
            g = at(320, e)
        g = at(320, 100, 2.2)
        check(g["state"] == 2, f"100 m off the route: state {g['state']} ({g['label']})")
        check(abs(g["off"] - 100) < 15, f"off by {g['off']} m, not 100")
        for e in (60, 20, 0):
            g = at(320, e)
        g = at(320, 0, 2.2)
        check(g["state"] == 1, f"back on the route: state {g['state']} ({g['label']})")
        for n in range(360, 801, 40):
            g = at(n, 0)
        g = at(800, 0, 2.2)
        check(g["state"] == 3, f"at the end: state {g['state']} ({g['label']})")
        if d.knows("ui"):
            for want in ("Off route", "Back on route", "Route finished"):
                check(any(want in a for a in alerts), f"no {want!r} alert (saw {alerts})")
    finally:
        (out / "route-walk.txt").write_text("\n".join(log) + "\n\nalerts: " + ", ".join(alerts) + "\n")
        d.cli.run("follow off")
        d.cli.run("gps off")
        d.dev.ask(bytes([route.CMD_ROUTE, route.DELETE, slot]), 0)
    return "on, off, back on, finished; alerts: " + ", ".join(alerts)


# ── Flashing ─────────────────────────────────────────────────────────────────

def pio_env(release):
    """A test build goes to a directory of its own, so it's never taken for a release."""
    if release:
        return None, ROOT / ".pio" / "build"
    e = dict(os.environ, PLATFORMIO_BUILD_DIR=str(ROOT / ".pio" / "build-test"),
             PLATFORMIO_BUILD_FLAGS=(os.environ.get("PLATFORMIO_BUILD_FLAGS", "") + " -D SOLO_TEST_HOOKS").strip())
    return e, ROOT / ".pio" / "build-test"


def build(env, release):
    penv, bdir = pio_env(release)
    print(f"Building {env}" + ("" if release else " (test build)") + "...")
    r = subprocess.run([PIO, "run", "-e", env], cwd=ROOT, capture_output=True, text=True, env=penv)
    if r.returncode:
        print(r.stdout[-3000:], r.stderr[-2000:])
        sys.exit("The build failed.")
    m = re.findall(r"(RAM|Flash):.*\((used \d+ bytes from \d+ bytes)\)", r.stdout)
    for kind, used in m:
        print(f"  {kind}: {used}")
    return bdir / env


def uf2_volumes():
    return {v for v in glob.glob("/Volumes/*") if os.path.exists(os.path.join(v, "INFO_UF2.TXT"))}


def flash(env, port, release):
    out = build(env, release)
    uf2 = out / "firmware.uf2"
    if uf2.exists():
        before = uf2_volumes()
        vol = None
        for attempt in range(3):   # a touch right after the port was closed is sometimes missed
            print(f"Bootloader on {port} (1200-baud touch)..." if not attempt else "  again...")
            try:
                s = serial.Serial(port, 1200)
                time.sleep(0.3)
                s.close()
            except serial.SerialException:
                pass
            end = time.time() + 10
            while not vol and time.time() < end:
                vol = next(iter(uf2_volumes() - before), None) or next(iter(uf2_volumes()), None)
                time.sleep(0.5)
            if vol:
                break
            time.sleep(1)
        if not vol:
            sys.exit("No UF2 drive appeared: is this an nRF52 board?")
        print(f"Copying firmware.uf2 to {vol}...")
        try:
            with open(uf2, "rb") as src, open(os.path.join(vol, "firmware.uf2"), "wb") as dst:
                dst.write(src.read())
        except OSError:
            pass   # the bootloader restarts the board as soon as it has it all
    else:
        print(f"Uploading to {port}...")
        r = subprocess.run([PIO, "run", "-e", env, "-t", "upload", "--upload-port", port],
                           cwd=ROOT, capture_output=True, text=True, env=pio_env(release)[0])
        if r.returncode:
            print(r.stdout[-3000:], r.stderr[-2000:])
            sys.exit("The upload failed.")
    if not uf2.exists():
        time.sleep(3)   # an ESP32 restarts after the upload: not the old firmware's last breath
    print("Waiting for the device...")
    p = wait_port(60, gone_first=port if uf2.exists() else None)
    if not p:
        sys.exit("The device didn't come back after flashing.")
    time.sleep(1)
    return p


# ── Main ─────────────────────────────────────────────────────────────────────

def pick_port(args):
    if args.port:
        if not probe(args.port):
            sys.exit(f"No Solo companion answers on {args.port} (busy in a browser or the console?).")
        return args.port
    found = [(p, probe(p)) for p in candidate_ports()]
    found = [(p, i) for p, i in found if i]
    if not found:
        sys.exit("No Solo device found (free the port: Disconnect in the browser).")
    if len(found) > 1:
        sys.exit("More than one device: say which with --port\n" + "\n".join(f"  {p}  {b} {f}" for p, (b, f) in found))
    return found[0][0]


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port")
    ap.add_argument("--flash", metavar="ENV", help="build this PlatformIO env (a test build) and flash it first")
    ap.add_argument("--release", action="store_true", help="with --flash: the plain firmware, without the test hooks")
    ap.add_argument("--only", help="comma-separated test names")
    ap.add_argument("--reboot", action="store_true", help="also the tests that restart the device")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    if args.list:
        for name, _, rb in TESTS:
            print(name + ("  (--reboot)" if rb else ""))
        return

    port = pick_port(args)
    board, fw = probe(port)
    print(f"{port}: {board}, {fw}")
    if args.flash:
        port = flash(args.flash, port, args.release)

    want = set(args.only.split(",")) if args.only else None
    if want and want - {n for n, _, _ in TESTS}:
        sys.exit(f"No test {', '.join(want - {n for n, _, _ in TESTS})} (see --list)")
    d = Dut(port)
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    out = ROOT / ".pio" / "hwtest" / f"{re.sub(r'[^A-Za-z0-9]+', '-', d.board).strip('-')}-{stamp}"
    out.mkdir(parents=True, exist_ok=True)
    print(f"{d.board}, {d.firmware} (protocol {d.proto})" + (", test build" if d.hooks else "") + "\n")

    results = []
    try:
        for name, fn, rb in TESTS:
            if want and name not in want:
                continue
            if rb and not args.reboot and not (want and name in want):
                continue
            t0 = time.time()
            try:
                note = fn(d, out)
                status = "PASS"
            except Skip as e:
                status, note = "SKIP", str(e)
            except Fail as e:
                status, note = "FAIL", str(e)
            except Exception as e:
                status, note = "ERROR", f"{type(e).__name__}: {e}"
                (out / f"{name}.trace.txt").write_text(traceback.format_exc())
            dt = time.time() - t0
            results.append({"test": name, "status": status, "seconds": round(dt, 2), "note": note or ""})
            print(f"  {status:5} {name:18} {dt:5.1f} s  {note or ''}")
    finally:
        d.close()

    (out / "report.json").write_text(json.dumps({"board": d.board, "firmware": d.firmware, "port": port,
                                                 "results": results, "stray_errors": d.strays},
                                                indent=2, ensure_ascii=False))
    if d.strays:
        print(f"\nStray error frames, the command asked again: {'; '.join(d.strays)}")
    bad = [r for r in results if r["status"] in ("FAIL", "ERROR")]
    print(f"\n{len(results) - len(bad)}/{len(results)} fine" + (f", {len(bad)} failed" if bad else "") + f". Report: {out}")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
