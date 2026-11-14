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


def wait_port(timeout, gone_first=None, board=None, prefer=None):
    """The port of a Solo device once it answers (`board`: that board only,
    with others plugged in; `prefer`: tried first); first waiting for
    `gone_first` to vanish."""
    end = time.time() + timeout
    if gone_first:   # an nRF52's port goes away as it restarts; an ESP32-S3's USB/JTAG port stays
        gone_by = time.time() + 3
        while time.time() < gone_by and gone_first in candidate_ports():
            time.sleep(0.3)
    while time.time() < end:
        ports = candidate_ports()
        if prefer in ports:
            ports.remove(prefer)
            ports.insert(0, prefer)
        for p in ports:
            i = probe(p)
            if i and (board is None or i[0] == board):
                return p
        time.sleep(1)
    return None


class TaggedCli(config.Cli):
    """The CLI with each command tagged ("k3|get tx"): the device reflects the
    tag, so a reply is matched to its command. Without it one late or
    duplicated reply would shift every later one by one. Replies with another
    tag, and error frames (which the CLI never sends: they belong to something
    else), are noted in the report and passed over."""

    TAGS = "abcdefghijkmnpqrstuvwxyz23456789"

    def __init__(self, dev, dut):
        super().__init__(dev)
        self.dut = dut
        self.n = 0

    def raw(self, cmd):
        """Untagged, as the configurator sends it."""
        return super().run(cmd)

    def run(self, cmd):
        self.n += 1
        tag = self.TAGS[self.n % 32] + self.TAGS[(self.n // 32) % 32]
        self.dev.send(bytes([config.CMD_RUN_CLI]) + f"{tag}|{cmd}".encode()[:160])
        end = time.time() + self.dev.timeout
        while time.time() < end:
            f = self.dev.read_frame(end - time.time())
            if f is None:
                break
            if f[0] == config.RESP_CLI_REPLY:
                text = f[1:].decode(errors="replace")
                if text.startswith(tag + "|"):
                    return text[3:]
                self.dut.strays.append(f"{self.dut.current}: {cmd!r}: a reply to another command ({text[:30]!r})")
            elif f[0] == 1 and len(f) <= 2:   # RESP_ERR
                self.dut.strays.append(f"{self.dut.current}: {cmd!r}: an error frame ({f[1] if len(f) > 1 else '?'})")
        raise DeviceError(f"no reply to {cmd!r}")


class Dut:
    """The device under test: the companion link, the CLI on it, and how to reconnect."""

    def __init__(self, port):
        self.port = port
        self.open()

    def open(self):
        self.dev = Companion(self.port, timeout=8.0)   # an ESP32 writing its flash can stall for seconds
        self.cli = TaggedCli(self.dev, self)
        self.strays = getattr(self, "strays", [])
        self.current = getattr(self, "current", "-")
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
        p = wait_port(40, gone_first=self.port, board=self.board, prefer=self.port)
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
    """ver, board and the protocol version."""
    check(d.proto >= config.CLI_MIN_VER, f"protocol {d.proto}, the CLI needs {config.CLI_MIN_VER}")
    ver = d.cli.run("ver")
    check(ver and "unknown" not in ver.lower(), f"ver: {ver!r}")
    board = d.cli.run("board")
    check(board, "board: empty")
    return f"{board}, {ver}"


@test("unknown-command")
def t_unknown(d, out):
    """An unknown command's reply, and the xx| prefix reflected."""
    r = d.cli.run("no such command here")
    check(re.search(r"(?i)unknown command", r), f"got {r!r}")
    r = d.cli.raw("ab|no such command")
    check(r.startswith("ab|"), f"the xx| prefix isn't reflected: {r!r}")


@test("batch")
def t_batch(d, out):
    """A set inside batch on / batch off takes milliseconds, the save once at the end."""
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
    """get radio, set radio and set tx to what they are."""
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
    """Every setting set to another choice, read back and set back."""
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
    """A setting changed in a batch never closed is there after a restart."""
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
    """Home's pages reordered, read back, put back."""
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
    """A quick message written, read back (UTF-8), put back."""
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
    """A melody written, read back, through RTTTL, put back."""
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
    """A waypoint added, read back, refused twice, deleted."""
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
    """A 41-point route sent, listed with its length, deleted."""
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
    """The screen as a PNG in `out` (the image), or None without screenshots.
    Read through the companion reader, which passes over anything else on the line."""
    d.dev.send(bytes([screenshot.CMD_GET_SCREENSHOT]))
    buf, total, got, w = bytearray(), None, 0, None
    end = time.time() + 6
    while time.time() < end:
        f = d.dev.read_frame(end - time.time())
        if f is None:
            break
        if f[0] == 1 and len(f) <= 2:
            return None   # no screenshots here
        if f[0] != screenshot.RESP_CODE_SCREENSHOT or len(f) < screenshot.HEADER_SIZE:
            continue
        kind, rot = f[1], f[2]
        fw, fh, idx, n = struct.unpack_from("<HHHH", f, 3)
        if idx == 0:
            buf, total, got, w, h, k, r = bytearray(), n, 0, fw, fh, kind, rot
        if w is None:
            continue
        buf += f[screenshot.HEADER_SIZE:]
        got += 1
        if got >= total:
            img = screenshot.buffer_to_image(bytes(buf), w, h, k, rotation=r, scale=1 if w >= 240 else 3)
            img.save(out / name)
            return img
    return None


@test("screenshot")
def t_screenshot(d, out):
    """The screen over USB as a PNG, not all black."""
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
    return {"screen": r[0], "alert": r[1], "locked": r[2] == "1", "display": r[3],
            "seq": int(r[4]) if len(r) > 4 else None}


def has_keys(d):
    return d.hooks and d.knows("key cancel")


def go_home(d):
    """Out of wherever the UI is, onto Home."""
    for _ in range(4):
        d.cli.put("key cancel")
        time.sleep(0.25)
    d.cli.put("key home")
    time.sleep(0.5)
    st = ui_state(d)
    if st["locked"]:
        raise Skip("the screen is locked")
    check(st["screen"].startswith("home"), f"not on Home: {st['screen']}")
    return st


@test("keys")
def t_keys(d, out):
    """Keys move the UI: Home, its context menu opened and closed."""
    need_hooks(d)
    if not has_keys(d):
        raise Skip("no keys on this frontend (a touch screen)")
    st = go_home(d)
    check(st["display"] == "on", "the screen is off after keys")
    d.cli.put("key enter long")   # Home's context menu ...
    time.sleep(0.5)
    shot(d, out, "home-menu.png")
    d.cli.put("key cancel")       # ... and out of it
    time.sleep(0.3)
    check(ui_state(d)["screen"].startswith("home"), "not back on Home after the menu")
    r = d.cli.run("key sideways")
    check(r.startswith("Error"), f"a bad key name: {r!r}")


def next_page(d, press, tries=8):
    """Home's next page: `press` (a key, a click) until the page changes, as
    Favourites first walks its tiles. The page now."""
    was = ui_state(d)["screen"]
    for _ in range(tries):
        press()
        now = ui_state(d)["screen"]
        if now != was:
            return now
    return was


def key_right(d):
    d.cli.put("key right")
    time.sleep(0.35)


def home_to(d, page, press=None, pages=14):
    """Home's pages stepped until `page` shows; False if it never does (hidden)."""
    press = press or (lambda: key_right(d))
    for _ in range(pages):
        if ui_state(d)["screen"] == f"home/{page}":
            return True
        next_page(d, press)
    return ui_state(d)["screen"] == f"home/{page}"


@test("screens")
def t_screens(d, out):
    """Home's pages one by one, each opened where it opens a screen, with a
    screenshot of each; then back to Home. The UI stays up throughout."""
    need_hooks(d)
    if not has_keys(d):
        raise Skip("no keys on this frontend (a touch screen)")
    go_home(d)
    seen, opened = [], []
    for _ in range(14):
        sc = ui_state(d)["screen"]
        page = sc.split("/", 1)[-1]
        if page in seen:
            break
        seen.append(page)
        shot(d, out, f"home-{page}.png")
        if page in ("tools", "settings", "map"):
            d.cli.put("key enter")
            time.sleep(0.6)
            now = ui_state(d)["screen"]
            check(not now.startswith("home/") or now != sc, f"Enter on {page} opened nothing")
            shot(d, out, f"open-{page}.png")
            opened.append(f"{page}>{now}")
            go_home(d)
            home_to(d, page)
        next_page(d, lambda: key_right(d))
    check(len(seen) >= 3, f"Home's pages: {seen}")
    return f"pages {', '.join(seen)}; opened {', '.join(opened) or 'none'}"


M_LAT = 1 / 111320.0


def offset(lat, lon, north_m, east_m):
    return lat + north_m * M_LAT, lon + east_m * M_LAT / math.cos(math.radians(lat))


def guide_state(d):
    st, slot, left, off, label = d.cli.get("guide").split(",", 4)
    return {"state": int(st), "slot": int(slot), "left": int(left), "off": int(off), "label": label}


@test("gps-fake")
def t_gps(d, out):
    """A fake fix taken, bad ones refused."""
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


LAT0, LON0 = 49.25, 19.95
ON, OFF, FINISHED = 1, 2, 3


class Walk:
    """A route on the device walked by fake fixes, in metres north / east of
    LAT0, LON0. As a context manager it sends the route and cleans up after:
    the guide stopped, the fake fix off, the route deleted. Each step logs the
    guide's state and the banner showing, for <name>.txt in the report."""

    def __init__(self, d, out, name, legs=None):
        self.d, self.out, self.name = d, out, name
        self.legs = legs   # [(north, east), ...] of the route, or None (a trail's Track back)
        self.alerts, self.log, self.slot = [], [], None
        self.stale, self.last = ("-", None), ("-", None)
        self.has_ui = d.knows("ui")

    def __enter__(self):
        if self.legs is not None:
            try:
                info = route.dev_info(self.d.dev)
            except DeviceError as e:
                raise Skip(f"no routes ({e})")
            if info["used"] == (1 << info["slots"]) - 1:
                raise Skip("every route slot is taken")
            pts = [offset(LAT0, LON0, n, e) + (None,) for n, e in self.legs]
            self.slot = upload_route(self.d, info, pts, "hwtest " + self.name)
        return self

    def __exit__(self, *exc):
        (self.out / f"{self.name}.txt").write_text("\n".join(self.log) + "\n\nalerts: " + ", ".join(self.alerts) + "\n")
        self.d.cli.run("follow off")
        self.d.cli.run("gps off")
        if self.slot is not None:
            try:
                self.d.dev.ask(bytes([route.CMD_ROUTE, route.DELETE, self.slot]), 0)
            except DeviceError:
                pass
        return False

    def put(self, north, east):
        la, lo = offset(LAT0, LON0, north, east)
        self.d.cli.put(f"gps {la:.6f},{lo:.6f}")

    def follow(self, reversed=False):
        self.d.cli.put(f"follow {self.slot}" + (" reversed" if reversed else ""))
        self.left_over()

    def left_over(self):
        """A banner still up from before (the last test's "Route finished") isn't this walk's:
        the count of banners shown tells a new one with the same text from it."""
        st = ui_state(self.d) if self.has_ui else None
        self.stale = (st["alert"], st["seq"]) if st else ("-", None)

    def at(self, north, east=0, wait=1.1):
        self.put(north, east)
        time.sleep(wait)
        g = guide_state(self.d)
        st = ui_state(self.d) if self.has_ui else {"alert": "-", "seq": None}
        a, seq = st["alert"], st["seq"]
        if (a, seq) == self.stale:
            a = "-"
        elif a != "-" and (seq is None and a != self.last[0] or seq is not None and seq != self.last[1]):
            self.alerts.append(a)
        if a != "-":
            self.last = (a, seq)
        self.log.append(f"{north:5.0f} N {east:5.0f} E: {g['state']} {g['label']}" + (f"  [{a}]" if a != "-" else ""))
        return g

    def path(self, points, step=40, wait=1.1):
        """Walks straight between (north, east) points, a fix every `step` m; the last state."""
        g = None
        for (n0, e0), (n1, e1) in zip(points, points[1:]):
            dist = math.hypot(n1 - n0, e1 - e0)
            k = max(1, int(math.ceil(dist / step)))
            for i in range(1, k + 1):
                g = self.at(n0 + (n1 - n0) * i / k, e0 + (e1 - e0) * i / k, wait)
        return g

    def saw(self, text):
        return any(text in a for a in self.alerts)

    def check_alerts(self, *want, absent=()):
        if not self.has_ui:
            return
        for w in want:
            check(self.saw(w), f"no {w!r} alert (saw {self.alerts})")
        for w in absent:
            check(not any(a.startswith(w) for a in self.alerts), f"a {w!r} alert, which shouldn't be (saw {self.alerts})")


STRAIGHT = [(100 * i, 0) for i in range(9)]   # 800 m north


@test("route-walk")
def t_route_walk(d, out):
    """800 m north: on it, 100 m off to the east, back on, to the end."""
    need_hooks(d)
    with Walk(d, out, "route-walk", STRAIGHT) as w:
        w.put(0, 0); time.sleep(1.2)
        w.follow()
        g = w.at(0, 0, 2.2)
        check(g["state"] == ON, f"at the start: state {g['state']} ({g['label']})")
        check(abs(g["left"] - 800) < 30, f"at the start {g['left']} m left, not 800")
        g = w.path([(0, 0), (320, 0)])
        check(g["state"] == ON and abs(g["left"] - 480) < 40, f"at 320 m: {g}")
        w.path([(320, 0), (320, 100)], step=25)
        g = w.at(320, 100, 2.2)
        check(g["state"] == OFF, f"100 m off the route: state {g['state']} ({g['label']})")
        check(abs(g["off"] - 100) < 15, f"off by {g['off']} m, not 100")
        w.path([(320, 100), (320, 0)])
        g = w.at(320, 0, 2.2)
        check(g["state"] == ON, f"back on the route: state {g['state']} ({g['label']})")
        w.path([(320, 0), (800, 0)])
        g = w.at(800, 0, 2.2)
        check(g["state"] == FINISHED, f"at the end: state {g['state']} ({g['label']})")
        w.check_alerts("Off route", "Back on route", "Route finished")
    return "on, off, back on, finished; alerts: " + ", ".join(w.alerts)


@test("route-reversed")
def t_route_reversed(d, out):
    """The same 800 m followed reversed: from its north end down to the south."""
    need_hooks(d)
    with Walk(d, out, "route-reversed", STRAIGHT) as w:
        w.put(800, 0); time.sleep(1.2)
        w.follow(reversed=True)
        g = w.at(800, 0, 2.2)
        check(g["state"] == ON and abs(g["left"] - 800) < 30, f"at the north end: {g}")
        g = w.path([(800, 0), (400, 0)])
        check(g["state"] == ON and abs(g["left"] - 400) < 40, f"half way down: {g}")
        w.path([(400, 0), (0, 0)])
        g = w.at(0, 0, 2.2)
        check(g["state"] == FINISHED, f"at the south end: state {g['state']} ({g['label']})")
        w.check_alerts("Route finished")
    return f"finished at the start of the route"


@test("route-loop")
def t_route_loop(d, out):
    """A loop (a 200 m square, start = end): followed from the start, it does
    not finish at once, and does finish once walked round."""
    need_hooks(d)
    sq = [(0, 0), (200, 0), (200, 200), (0, 200), (0, 0)]
    legs = [(0, 0), (100, 0), (200, 0), (200, 100), (200, 200), (100, 200), (0, 200), (0, 100), (0, 0)]
    with Walk(d, out, "route-loop", legs) as w:
        w.put(0, 0); time.sleep(1.2)
        w.follow()
        g = w.at(0, 0, 3.2)
        check(g["state"] == ON, f"at the start of a loop: state {g['state']} ({g['label']})")
        check(abs(g["left"] - 800) < 40, f"at the start {g['left']} m left, not 800")
        g = w.path(sq[:2])
        check(g["state"] == ON, f"200 m into the loop: state {g['state']} ({g['label']})")
        w.path(sq[1:])
        g = w.at(0, 0, 2.2)
        check(g["state"] == FINISHED, f"round the loop: state {g['state']} ({g['label']})")
        w.check_alerts("Route finished")
    return "not finished at the start, finished round it"


@test("route-far-start")
def t_route_far(d, out):
    """Started 300 m east of the route: off it, no "Off route" for that; on
    reaching it "On route", not "Back on route"."""
    need_hooks(d)
    with Walk(d, out, "route-far-start", STRAIGHT) as w:
        w.put(200, 300); time.sleep(1.2)
        w.follow()
        g = w.at(200, 300, 2.2)
        check(g["state"] == OFF, f"300 m away: state {g['state']} ({g['label']})")
        w.path([(200, 300), (200, 0)])
        g = w.at(200, 0, 2.2)
        check(g["state"] == ON, f"on reaching the route: state {g['state']} ({g['label']})")
        w.check_alerts("On route", absent=("Off route", "Back on route"))
    return "alerts: " + ", ".join(w.alerts)


@test("route-shortcut")
def t_route_short(d, out):
    """A U (400 m north, 100 m east, 400 m south) with a long tail east: cut
    across the U, the guide takes up the far side instead of calling you off."""
    need_hooks(d)
    legs = [(0, 0), (200, 0), (400, 0), (400, 100), (200, 100), (0, 100), (0, 600), (0, 1100)]
    with Walk(d, out, "route-shortcut", legs) as w:
        w.put(0, 0); time.sleep(1.2)
        w.follow()
        w.path([(0, 0), (200, 0)])
        w.path([(200, 0), (200, 100)], step=25)
        g = None
        for _ in range(12):   # off the route a full search runs at most every 10 s
            g = w.at(200, 100, 1.0)
            if g["state"] == ON:
                break
        check(g["state"] == ON, f"across the U: state {g['state']} ({g['label']})")
        # 1900 m in all; across the U is 700 m along it (400 up, 100 over, 200 down)
        check(abs(g["left"] - 1200) < 60, f"taken up with {g['left']} m left, not ~1200 (the far side)")
    return f"{g['left']} m left after the shortcut"


@test("route-resume", needs_reboot=True)
def t_route_resume(d, out):
    """An out-and-back route (400 m north and back down the same line), a
    restart on the way back: the guide picks up on the way back, where it was.
    Found afresh it would take the way out, the earliest of the two."""
    need_hooks(d)
    legs = [(100 * n, 0) for n in (0, 1, 2, 3, 4, 3, 2, 1, 0)]
    with Walk(d, out, "route-resume", legs) as w:
        w.put(0, 0); time.sleep(1.2)
        w.follow()
        w.path([(0, 0), (400, 0), (200, 0)])
        before = guide_state(d)
        check(before["state"] == ON and abs(before["left"] - 200) < 40, f"before the restart: {before}")
        d.reboot()
        g = guide_state(d)
        check(g["slot"] == w.slot, f"after the restart the guide follows slot {g['slot']}, not {w.slot}")
        g = w.at(200, 0, 2.5)   # the fake fix went with the restart: where we are, again
        check(g["state"] == ON, f"after the restart: state {g['state']} ({g['label']})")
        check(abs(g["left"] - before["left"]) < 60,
              f"after the restart {g['left']} m left, before {before['left']}" + (" (taken up on the way out)" if g["left"] > 500 else ""))
    return f"{before['left']} m left before the restart, {g['left']} m after"


@test("trackback")
def t_trackback(d, out):
    """A trail recorded on an L (400 m north, 200 m east), then Track back:
    "to start", walked back to "Back at the start"."""
    need_hooks(d)
    rec, pts = (int(x) for x in d.cli.get("trail").split(","))
    if rec or pts:
        raise Skip(f"a trail is on the device ({pts} points): not wiping it")
    with Walk(d, out, "trackback") as w:
        try:
            w.put(0, 0); time.sleep(1.2)
            d.cli.put("trail on")
            w.path([(0, 0), (400, 0), (400, 200)], step=30)
            time.sleep(1.5)
            n = int(d.cli.get("trail").split(",")[1])
            check(n >= 2, f"the trail has {n} points")
            w.left_over()
            r = d.cli.run("trackback")
            check(r == "OK", f"trackback: {r}")
            g = w.at(400, 200, 2.2)
            check(g["state"] == ON and g["label"].endswith("to start"), f"Track back at the far end: {g}")
            check(abs(g["left"] - 600) < 60, f"{g['left']} m to the start, not 600")
            w.path([(400, 200), (400, 0), (0, 0)])
            g = w.at(0, 0, 2.2)
            check(g["state"] == FINISHED, f"at the start: state {g['state']} ({g['label']})")
            w.check_alerts("Back at the start")
        finally:
            d.cli.run("trail off")
            d.cli.run("trail clear")
    return f"{n} trail points, back at the start"


# One button: the gestures timed by the host, through the firmware's own OneButton.

def btn(d, held, gap=0.45):
    d.cli.put("btn down")
    time.sleep(held)
    d.cli.put("btn up")
    time.sleep(gap)


def click(d):
    btn(d, 0.08)


def double(d):
    btn(d, 0.08, 0.12)
    btn(d, 0.08)


@test("one-button")
def t_one_button(d, out):
    """Click (next), double click (back), hold (OK), on a one-button board."""
    need_hooks(d)
    r = d.cli.run("btn up")
    if r != "OK":
        raise Skip("not a one-button board")
    go_home(d)
    p0 = ui_state(d)["screen"]
    p1 = next_page(d, lambda: click(d))   # a page with rows (Favourites, Tools) walks them first
    check(p1.startswith("home/") and p1 != p0, f"clicks on Home: {p0} -> {p1}")
    double(d)
    check(ui_state(d)["screen"] == p0, f"a double click: not back on {p0} ({ui_state(d)['screen']})")
    if not home_to(d, "tools", press=lambda: click(d)):
        raise Skip("no Tools page on Home")
    btn(d, 0.8)   # LONG: OK
    now = ui_state(d)["screen"]
    check(now == "tools", f"a hold on Home's Tools: {now}")
    shot(d, out, "one-button-tools.png")
    click(d)
    double(d)
    check(ui_state(d)["screen"].startswith("home"), f"a double click out of Tools: {ui_state(d)['screen']}")
    return f"{p0} -> {p1} -> back; Tools opened and left"


# ── Two devices (--pair) ─────────────────────────────────────────────────────

CMD_APP_START, RESP_SELF_INFO = 1, 5
CMD_SEND_TXT_MSG, RESP_SENT = 2, 6
CMD_EXPORT_CONTACT, RESP_EXPORT_CONTACT, CMD_IMPORT_CONTACT = 17, 11, 18
CMD_REMOVE_CONTACT, CMD_GET_CONTACT_BY_KEY, RESP_CONTACT = 15, 30, 3
PUSH_SEND_CONFIRMED = 0x82


def pubkey(d):
    f = d.dev.ask(bytes([CMD_APP_START]) + bytes(7) + b"hwtest", RESP_SELF_INFO)
    return f[4:36]


def knows_contact(d, key):
    try:
        d.dev.ask(bytes([CMD_GET_CONTACT_BY_KEY]) + key, RESP_CONTACT)
        return True
    except DeviceError:
        return False


def send_dm(a, b_key, text):
    """A sends a direct message; (the ACK code to wait for, est. timeout ms)."""
    ts = int(time.time())
    f = a.dev.ask(bytes([CMD_SEND_TXT_MSG, 0, 0]) + struct.pack("<I", ts) + b_key[:6] + text.encode(), RESP_SENT)
    ack = f[2:6]
    timeout = struct.unpack_from("<I", f, 6)[0] if len(f) >= 10 else 15000
    return ack, timeout


def wait_ack(a, ack, timeout_s):
    end = time.time() + timeout_s
    while time.time() < end:
        f = a.dev.read_frame(end - time.time())
        if f and f[0] == PUSH_SEND_CONFIRMED and f[1:5] == ack:
            return struct.unpack_from("<I", f, 5)[0]
    return None


def wait_dm(b, text, timeout_s):
    end = time.time() + timeout_s
    while time.time() < end:
        r = b.cli.get("lastdm")
        if r != "none":
            direction, _, _, got = r.split(",", 3)
            if direction == "in" and got == text:
                return True
        time.sleep(0.5)
    return False


def pair_dm(a, b, out):
    """A's message to B over the air: B has it, A gets the ACK."""
    for x in (a, b):
        if not x.hooks:
            raise Skip(f"{x.board} isn't a test build")
    ra, rb = a.cli.get("get radio"), b.cli.get("get radio")
    if ra != rb:
        raise Skip(f"different radio settings: {ra} / {rb}")
    ka, kb = pubkey(a), pubkey(b)
    added = []
    try:
        for x, y, ky in ((a, b, kb), (b, a, ka)):   # each other's contact, as a QR would give it
            if not knows_contact(x, ky):
                adv = y.dev.ask(bytes([CMD_EXPORT_CONTACT]), RESP_EXPORT_CONTACT)[1:]
                x.dev.ask(bytes([CMD_IMPORT_CONTACT]) + adv, 0)
                added.append((x, ky))
        notes = []
        for x, y, ky, tag in ((a, b, kb, "A->B"), (b, a, ka, "B->A")):
            text = f"hwtest {tag} {int(time.time()) % 100000}"
            ack, est = send_dm(x, ky, text)
            got = wait_dm(y, text, max(20, est / 1000 * 3))
            check(got, f"{tag}: {y.board} never got {text!r}")
            trip = wait_ack(x, ack, max(20, est / 1000 * 3))
            check(trip is not None, f"{tag}: no ACK on {x.board}")
            notes.append(f"{tag} delivered, ACK in {trip} ms")
        return "; ".join(notes)
    finally:
        for x, key in added:
            try:
                x.dev.ask(bytes([CMD_REMOVE_CONTACT]) + key, 0)
            except DeviceError:
                pass


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
    board = probe(port)[0]
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
    p = wait_port(60, gone_first=port if uf2.exists() else None, board=board, prefer=port)
    if not p:
        sys.exit("The device didn't come back after flashing.")
    time.sleep(1)
    return p


# ── Main ─────────────────────────────────────────────────────────────────────

def found_devices():
    out = [(p, probe(p)) for p in candidate_ports()]
    return [(p, i) for p, i in out if i]


def pick_port(args):
    if args.port:
        if not probe(args.port):
            sys.exit(f"No Solo companion answers on {args.port} (busy in a browser or the console?).")
        return args.port
    found = found_devices()
    if not found:
        sys.exit("No Solo device found (free the port: Disconnect in the browser).")
    if len(found) > 1:
        sys.exit("More than one device: say which with --port, or test them all with --all\n"
                 + "\n".join(f"  {p}  {b} {f}" for p, (b, f) in found))
    return found[0][0]


def mem(d):
    """(free heap, uptime s) on a test build, else None."""
    if not d.hooks:
        return None
    f = d.cli.get("mem").split(",")
    return int(f[0]), int(f[3])


def run_one(name, fn, *a):
    t0 = time.time()
    trace = None
    try:
        note = fn(*a)
        status = "PASS"
    except Skip as e:
        status, note = "SKIP", str(e)
    except Fail as e:
        status, note = "FAIL", str(e)
    except Exception as e:
        status, note = "ERROR", f"{type(e).__name__}: {e}"
        trace = traceback.format_exc()
    return {"test": name, "status": status, "seconds": round(time.time() - t0, 2), "note": note or ""}, trace


def show(r, prefix=""):
    print(f"  {prefix}{r['status']:5} {r['test']:18} {r['seconds']:5.1f} s  {r['note']}")


def out_dir(d):
    stamp = datetime.datetime.now().strftime("%Y%m%d-%H%M%S")
    o = ROOT / ".pio" / "hwtest" / f"{re.sub(r'[^A-Za-z0-9]+', '-', d.board).strip('-')}-{stamp}"
    o.mkdir(parents=True, exist_ok=True)
    return o


def suite(d, args, want):
    """Every test wanted on one device, --repeat times over; its results."""
    out = out_dir(d)
    print(f"\n{d.board}, {d.firmware} (protocol {d.proto})" + (", test build" if d.hooks else "") + f", {d.port}")
    results, marks = [], []
    m0 = mem(d)
    up = m0[1] if m0 else None
    for rnd in range(args.repeat):
        if args.repeat > 1:
            print(f" round {rnd + 1}/{args.repeat}")
        for name, fn, rb in TESTS:
            if want and name not in want:
                continue
            if rb and not args.reboot and not (want and name in want):
                continue
            d.current = name
            r, trace = run_one(name, fn, d, out)
            if r["status"] == "ERROR" and re.search(r"PortNotOpen|SerialException|Errno 6|Device not configured", r["note"]):
                d.close()   # the link went (a restart gone wrong): back to the same board, if it comes
                p = wait_port(40, board=d.board, prefer=d.port)
                if not p:
                    results.append(r); show(r)
                    print("  the device is gone: the rest skipped")
                    break
                d.port = p
                d.open()
            if trace:
                (out / f"{name}.trace.txt").write_text(trace)
            m = None
            try:
                m = mem(d)
            except (DeviceError, serial.SerialException, OSError):
                pass
            if m and up is not None and not rb and m[1] < up:   # the clock since boot went back: it restarted
                r["status"], r["note"] = "FAIL", f"the device restarted during this test ({r['note']})".rstrip(" ()")
            if m:
                up = m[1]
            r["round"] = rnd + 1
            results.append(r)
            show(r)
        m = mem(d)
        if m:
            marks.append(m[0])
    if m0 and marks:   # free heap: the first round's end against the last's (or the start against the end)
        base, last = (marks[0], marks[-1]) if len(marks) > 1 else (m0[0], marks[-1])
        drop = base - last
        r = {"test": "memory", "seconds": 0, "round": args.repeat,
             "status": "FAIL" if drop > 4096 else "PASS",
             "note": f"free heap {base} -> {last} bytes ({-drop:+d})" + (", rounds' ends compared" if len(marks) > 1 else "")}
        results.append(r)
        show(r)
    (out / "report.json").write_text(json.dumps({"board": d.board, "firmware": d.firmware, "port": d.port,
                                                 "results": results, "stray_errors": d.strays},
                                                indent=2, ensure_ascii=False))
    if d.strays:
        print(f"  Stray frames, passed over: {'; '.join(d.strays)}")
    return results, out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--port")
    ap.add_argument("--flash", metavar="ENV", help="build this PlatformIO env (a test build) and flash it first")
    ap.add_argument("--release", action="store_true", help="with --flash: the plain firmware, without the test hooks")
    ap.add_argument("--only", help="comma-separated test names")
    ap.add_argument("--reboot", action="store_true", help="also the tests that restart the device")
    ap.add_argument("--all", action="store_true", help="every device plugged in, one after another")
    ap.add_argument("--pair", action="store_true", help="two devices: also a message each way over the air")
    ap.add_argument("--repeat", type=int, default=1, metavar="N", help="the tests N times over (a soak; the heap compared)")
    ap.add_argument("--list", action="store_true")
    args = ap.parse_args()
    if args.list:
        for name, fn, rb in TESTS:
            doc = " ".join((fn.__doc__ or "").split())
            print(f"{name:18}" + (" (--reboot) " if rb else " ") + (doc if len(doc) < 100 else doc[:97] + "..."))
        print(f"{'memory':18} test build: free heap at the end against the start (or the rounds' ends)")
        print(f"{'pair':18} --pair: a direct message each way between two devices, delivered and ACKed")
        return

    want = set(args.only.split(",")) if args.only else None
    names = {n for n, _, _ in TESTS} | {"pair", "memory"}
    if want and want - names:
        sys.exit(f"No test {', '.join(want - names)} (see --list)")
    if args.pair:
        args.all = True
    if args.all:
        if args.flash:
            sys.exit("--flash takes one device: flash each with --port, then --all / --pair.")
        ports = [p for p, _ in found_devices()]
        if not ports:
            sys.exit("No Solo device found (free the port: Disconnect in the browser).")
        if args.pair and len(ports) != 2:
            sys.exit(f"--pair needs two devices plugged in, found {len(ports)}.")
    else:
        port = pick_port(args)
        board, fw = probe(port)
        print(f"{port}: {board}, {fw}")
        if args.flash:
            port = flash(args.flash, port, args.release)
        ports = [port]

    duts = [Dut(p) for p in ports]
    bad = total = 0
    try:
        for d in duts:
            if want == {"pair"}:
                continue
            res, _ = suite(d, args, want)
            total += len(res)
            bad += sum(r["status"] in ("FAIL", "ERROR") for r in res)
        if args.pair:
            a, b = duts
            print(f"\nPair: {a.board} <-> {b.board}")
            r, trace = run_one("pair", pair_dm, a, b, None)
            if trace:
                print(trace)
            show(r)
            total += 1
            bad += r["status"] in ("FAIL", "ERROR")
    finally:
        for d in duts:
            d.close()
    print(f"\n{total - bad}/{total} fine" + (f", {bad} failed" if bad else "") + ". Reports in .pio/hwtest/")
    sys.exit(1 if bad else 0)


if __name__ == "__main__":
    main()
