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


CMD_GET_DEVICE_TIME, CMD_SET_DEVICE_TIME, RESP_CURR_TIME = 5, 6, 9


def device_time(d):
    return struct.unpack_from("<I", d.dev.ask(bytes([CMD_GET_DEVICE_TIME]), RESP_CURR_TIME), 1)[0]


def app_set_time(d, t):
    """CMD_SET_DEVICE_TIME as the phone app sends it: True when taken."""
    try:
        d.dev.ask(bytes([CMD_SET_DEVICE_TIME]) + struct.pack("<I", t), 0)
        return True
    except DeviceError:
        return False


@test("clock")
def t_clock(d, out):
    """The clock set from the CLI (seconds and local time, back too) and from
    the app (back by an hour at most); the computer's time put back after."""
    if not d.knows("clock"):
        raise Skip("no clock command (older firmware)")
    try:
        now = int(time.time())
        r = d.cli.get(f"time {now - 120}")
        check(abs(device_time(d) - (now - 120)) <= 3, f"time <seconds>: {r!r}, the device at {device_time(d)}")
        m = re.search(r"UTC([+-]\d+)", r)
        check(m, f"no UTC offset in {r!r}")
        tz = int(m.group(1))
        local = datetime.datetime.fromtimestamp(now - 600, datetime.timezone.utc) + datetime.timedelta(hours=tz)
        r = d.cli.get("time " + local.strftime("%Y-%m-%d %H:%M"))
        want = (now - 600) // 60 * 60
        check(abs(device_time(d) - want) <= 3, f"time <local>: {r!r}, the device at {device_time(d)}, not {want}")
        check(d.cli.run("time soon").startswith("Error"), "time soon taken")
        check(d.cli.run("time 2026-02-30 10:00").startswith("Error"), "30 February taken")
        now = int(time.time())
        check(app_set_time(d, now), "the app's time (now) refused")
        check(app_set_time(d, now - 1800), "the app half an hour back: refused")
        check(abs(device_time(d) - (now - 1800)) <= 3, "the app's time half an hour back not set")
        check(not app_set_time(d, now - 1800 - 7200), "the app two hours back: taken")
    finally:
        app_set_time(d, int(time.time()))
    drift = device_time(d) - int(time.time())
    check(abs(drift) <= 3, f"the clock {drift} s off after the test")
    return f"UTC{tz:+d}, CLI back 2 and 10 min, the app back 30 min taken and 2.5 h refused"


def opts(d, key):
    r = d.cli.run(f"opts {key}")
    if not r.startswith("> "):
        return None
    f = r[2:].split("|")
    return {"where": f[0], "label": f[1], "value": f[2], "choices": [c for c in f[3:] if c != "..."]}


@test("optsall")
def t_optsall(d, out):
    """"optsall" (2.4): every key's opts in one ask, the same as key by key."""
    if not d.knows("optsat 0"):
        raise Skip("no optsall in this firmware (before 2.4)")
    t0 = time.time()
    d.dev.send(bytes([config.CMD_RUN_CLI]) + b"optsall")   # untagged, as the configurator
    got, vals, end = {}, {}, None
    while end is None:
        f = d.dev.read_frame()
        check(f is not None, f"optsall stopped after {len(got)} keys")
        if f[0] != config.RESP_CLI_REPLY:
            continue
        r = f[1:].decode(errors="replace")
        k, _, rest = r.partition(" ")
        if r.startswith("@"):
            got[k[1:]] = rest
        elif r.startswith("="):
            vals[k[1:]] = rest
        elif r.startswith("> end "):
            end = int(r[6:])
        else:
            raise Fail(f"optsall: {r[:40]!r}")
    ms = (time.time() - t0) * 1000
    keys = config.list_keys(d.cli)
    check(end == len(got), f"optsall: end says {end}, {len(got)} came")
    check(list(got) == keys, f"optsall's keys aren't keys': {set(got) ^ set(keys)}")
    for k in keys:
        one = d.cli.run(f"opts {k}")
        a, b = got[k].split("|"), one.split("|")
        cut = a[-1] == "..."   # less room next to the key: fewer choices
        check(a[:3] == b[:3] and (cut or a == b), f"{k}: optsall …{got[k][-40:]!r} ({len(got[k])} B), opts …{one[-40:]!r} ({len(one)} B)")
        if a[2].startswith("#"):
            check(k in vals, f"{k}: no value after it")
            check("> " + vals[k] == d.cli.run(f"get {k}"), f"{k}: value {vals[k]!r} isn't get's")
    return f"{len(got)} keys ({len(vals)} values) in {ms:.0f} ms"


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
            if o is None or o["value"].startswith("#"):
                continue   # not on this board; a text, list or pick (2.4: no choice to flip)
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


def shot_raw(d, rle=False):
    """(type, rotation, w, h, bytes) of the screen, or None without screenshots.
    Read through the companion reader, which passes over anything else on the
    line. rle: asked for RLE (2.4), which a colour screen answers as type 3 --
    returned decoded, as type 2."""
    d.dev.send(bytes([screenshot.CMD_GET_SCREENSHOT, 1] if rle else [screenshot.CMD_GET_SCREENSHOT]))
    parts, total, w = {}, None, None
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
            parts, total, w, h, k, r = {}, None, fw, fh, kind, rot
        if w is None:
            continue
        parts[idx] = f[screenshot.HEADER_SIZE:]
        if n != 0xFFFF:
            total = n   # RLE: only the last chunk says how many
        if total is not None and len(parts) >= total:
            buf = b"".join(parts[i] for i in sorted(parts))
            if k == 3:
                buf, k = un_rle(buf), 2
            return k, r, w, h, buf
    return None


def un_rle(d):
    """MyMeshScreenshot.h's RLE: a byte c, then c + 1 pixels (c < 128) or one pixel c - 127 times."""
    o, i = bytearray(), 0
    while i < len(d):
        c = d[i]
        i += 1
        if c >= 128:
            o += d[i:i + 2] * (c - 127)
            i += 2
        else:
            o += d[i:i + 2 * (c + 1)]
            i += 2 * (c + 1)
    return bytes(o)


def shot(d, out, name):
    """The screen as a PNG in `out` (the image), or None without screenshots."""
    s = shot_raw(d)
    if s is None:
        return None
    k, r, w, h, buf = s
    img = screenshot.buffer_to_image(buf, w, h, k, rotation=r, scale=1 if w >= 240 else 3)
    img.save(out / name)
    return img


@test("screenshot")
def t_screenshot(d, out):
    """The screen over USB as a PNG, not all black."""
    img = shot(d, out, "screen.png")
    if img is None:
        raise Skip("no screenshot (old firmware, or the phone app holds it)")
    check(img.getextrema() != ((0, 0), (0, 0), (0, 0)), "the screen is all black")
    note = ""
    if shot_raw(d)[0] == 2:
        # A colour screen asked for RLE: the same picture as without, while
        # the screen stays put (a clock may tick between two: a few goes).
        for _ in range(5):
            a, z, b = shot_raw(d), shot_raw(d, rle=True), shot_raw(d)
            check(z is not None and len(z[4]) == len(a[4]), "the RLE frame is not a whole screen")
            if a[4] == b[4]:
                check(z[4] == a[4], "the RLE frame is not the screen")
                note = ", RLE the same"
                break
    return f"{img.width}x{img.height}{note}, {out / 'screen.png'}"


# ── With the test hooks (a --flash build) ──────────────────────────────────

@test("loops")
def t_loops(d, out):
    """How often loop() runs and how busy it is, screen on and off: the
    firmware's own measure of what it wakes for (no current meter needed)."""
    need_hooks(d)
    if not d.knows("loops"):
        raise Skip("no loops command (older test build)")
    res = {}
    try:
        for state, secs in (("on", 8), ("off", 12)):
            d.cli.run(f"screen {state}")
            time.sleep(2)
            d.cli.get("loops")   # from here
            time.sleep(secs)
            f = d.cli.get("loops").split(",")
            res[state] = (int(f[0]), float(f[1]))
    finally:
        d.cli.run("screen on")
    (out / "loops.json").write_text(json.dumps(res))
    return "  ".join(f"screen {k}: {v[0]}/s, {v[1]}% busy" for k, v in res.items())


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
        d.cli.put("guidesave 0")   # the walk takes seconds; the guide saves at most every 2 min
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


@test("route-resume-away", needs_reboot=True)
def t_route_resume_away(d, out):
    """A loop route (a 400 m square, start = end), a restart halfway round
    with the first fix 1.3 km away (a detour): off the route, the progress
    kept; back where it was, on it again with the same distance left. Found
    afresh it would take the start of the loop."""
    need_hooks(d)
    legs = [(0, 0), (400, 0), (400, 400), (0, 400), (0, 0)]
    with Walk(d, out, "route-resume-away", legs) as w:
        d.cli.put("guidesave 0")
        w.put(0, 0); time.sleep(1.2)
        w.follow()
        w.path([(0, 0), (400, 0), (400, 400)])
        before = guide_state(d)
        check(before["state"] == ON and abs(before["left"] - 800) < 60, f"before the restart: {before}")
        d.reboot()
        g = w.at(400, 1700, 2.5)
        check(g["state"] == OFF, f"restarted 1.3 km away: state {g['state']} ({g['label']}), not off the route")
        check(abs(g["left"] - before["left"]) < 60, f"away: {g['left']} m left, before {before['left']}")
        w.path([(400, 1700), (400, 900), (400, 400)])
        g = guide_state(d)
        check(g["state"] == ON, f"back where it was: state {g['state']} ({g['label']})")
        check(abs(g["left"] - before["left"]) < 80,
              f"back: {g['left']} m left, before {before['left']}" + (" (taken up at the loop's start)" if g["left"] > 1200 else ""))
    return f"{before['left']} m left before, {g['left']} m back on it"


@test("loop-watchdog", needs_reboot=True)
def t_loop_watchdog(d, out):
    """The L2's main loop stuck ("hang"): within 30 s the watchdog restarts the
    device, and the reason and a core dump are there after ("boot", as
    Diagnostics' "Last start" / "Last crash")."""
    need_hooks(d)
    if not d.knows("boot"):
        raise Skip("no loop watchdog on this board")
    try:
        d.cli.run("hang")
    except (DeviceError, serial.SerialException, OSError):
        pass
    t0 = time.time()
    d.close()
    while time.time() - t0 < 45 and d.port in candidate_ports():   # stuck: the port stays till the restart
        time.sleep(0.3)
    check(d.port not in candidate_ports(), "still there after 45 s: frozen, no watchdog?")
    p = wait_port(60, board=d.board, prefer=d.port)
    check(p, "the device didn't come back from the hang (frozen: no watchdog?)")
    secs = time.time() - t0
    d.port = p
    time.sleep(1)
    d.open()
    why, crash = d.cli.get("boot").split(",", 1)
    check(why == "Watchdog", f"started again by {why!r}, not the watchdog")
    check(crash.strip() != "", "no core dump of the hang")
    return f"restarted after {secs:.0f} s, last crash {crash}"


@test("crash-report", needs_reboot=True)
def t_crash_report(d, out):
    """A hang after a few screens: after the watchdog's restart a report is on
    the card (meshcore/crash/) with the backtrace and the last things done,
    the screen opened just before the hang the last of them. Deleted after."""
    need_hooks(d)
    if not d.knows("crash"):
        raise Skip("no crash report on this board")
    for w in ("map", "settings", "diag"):
        d.cli.put(f"idle {w}")
        time.sleep(0.8)
    try:
        d.cli.run("hang")
    except (DeviceError, serial.SerialException, OSError):
        pass
    t0 = time.time()
    d.close()
    while time.time() - t0 < 45 and d.port in candidate_ports():
        time.sleep(0.3)
    p = wait_port(60, board=d.board, prefer=d.port)
    check(p, "the device didn't come back from the hang")
    d.port = p
    time.sleep(1)
    d.open()
    r = d.cli.get("crash")
    try:
        check(r, "no report written after the watchdog restart (an SD card in?)")
        name, ev, bt, last = r.split(",", 3)
        check(bt == "1", f"{name}: no backtrace in it")
        check(int(ev) >= 3, f"{name}: {ev} things noted, not the screens opened")
        check(last.startswith("scr ") and "Diag" in last, f"{name}: the last thing noted is {last!r}, not the Diagnostics screen")
    finally:
        if r:
            d.cli.run("crash rm")
    return f"{name}: {ev} things noted, the last {last!r}"


@test("coverage-crash", needs_reboot=True)
def t_coverage_crash(d, out):
    """Rows recorded, then a hang and the watchdog's restart: the rows are in
    the file after it (synced to the card, not only flushed -- a file left at
    0 B in its directory entry was deleted as empty). A test build's files only."""
    need_hooks(d)
    if not d.knows("boot"):
        raise Skip("no loop watchdog on this board")
    r = d.cli.run("cov")
    if not r.startswith("> "):
        raise Skip(f"no coverage recording here ({r})")
    st = cov_state(d)
    home0, auto, on0 = d.cli.get("cov home"), st["auto"], st["on"]
    try:
        d.cli.put("cov auto off")
        d.cli.get("cov home 0,0 0")
        d.cli.put(f"gps {LAT0},{LON0}")
        time.sleep(2.5)
        d.cli.put("cov on")
        for _ in range(20):
            time.sleep(1)
            if cov_state(d)["rows"] >= 1:
                break
        name = d.cli.get("cov file")
        check(name, f"no file being written: {cov_state(d)}")
        time.sleep(17)   # past the flush (15 s)
        try:
            d.cli.run("hang")
        except (DeviceError, serial.SerialException, OSError):
            pass
        t0 = time.time()
        d.close()
        while time.time() - t0 < 45 and d.port in candidate_ports():
            time.sleep(0.3)
        p = wait_port(60, board=d.board, prefer=d.port)
        check(p, "the device didn't come back from the hang")
        d.port = p
        time.sleep(1)
        d.open()
        c = d.cli.get(f"cov count {name}.csv")
        check(c.split(",")[0] not in ("-1", "0"), f"{name} after the restart: {c} (rows, in the zone): the rows lost")
    finally:
        d.cli.run("cov off")
        d.cli.run("cov clear")
        d.cli.run("gps off")
        la, lo, rr = home0.split(",")
        d.cli.run(f"cov home {la},{lo} {rr}")
        d.cli.run("cov auto " + ("on" if auto else "off"))
        if on0:
            d.cli.run("cov on")
    return f"{name}: {c.split(',')[0]} rows kept through the hang"


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


@test("guide-switch")
def t_guide_switch(d, out):
    """Track back started while a route is followed takes its place, and a
    route followed during Track back takes Track back's: one guide at a time."""
    need_hooks(d)
    rec, pts = (int(x) for x in d.cli.get("trail").split(","))
    if rec or pts:
        raise Skip(f"a trail is on the device ({pts} points): not wiping it")
    with Walk(d, out, "guide-switch", STRAIGHT) as w:
        trail_slot = route.dev_info(d.dev)["slots"]   # PlannedRoute::TRAIL_SLOT = ROUTE_SLOTS
        try:
            w.put(0, 0); time.sleep(1.2)
            d.cli.put("trail on")
            w.follow()
            g = w.path([(0, 0), (300, 0)], step=30)
            check(g["state"] == ON and g["slot"] == w.slot, f"following the route: {g}")
            time.sleep(1.5)
            r = d.cli.run("trackback")
            check(r == "OK", f"trackback during the route: {r}")
            g = w.at(300, 0, 2.2)
            check(g["slot"] == trail_slot and g["label"].endswith("to start"),
                  f"Track back didn't take the route's place: {g}")
            check(abs(g["left"] - 300) < 50, f"{g['left']} m to the start, not 300")
            w.follow()
            g = w.at(300, 0, 2.2)
            check(g["slot"] == w.slot and g["state"] == ON and not g["label"].endswith("to start"),
                  f"the route didn't take Track back's place: {g}")
            check(abs(g["left"] - 500) < 50, f"back on the route {g['left']} m left, not 500")
        finally:
            d.cli.run("trail off")
            d.cli.run("trail clear")
    return "route -> Track back -> route"


def cov_state(d):
    on, auto, rows, pend, fix, home, newest = d.cli.get("cov").split(",", 6)
    return {"on": on == "1", "auto": auto == "1", "rows": int(rows), "pending": int(pend),
            "fix": fix == "1", "home": home == "1", "newest": newest}


@test("coverage")
def t_coverage(d, out):
    """MatterMap recording (opt-in): with a fake fix, turned on, a "nothing
    heard" row; turned off, the file finished and waiting. The test's file is
    deleted after, auto upload held off meanwhile: nothing is sent."""
    need_hooks(d)
    r = d.cli.run("cov")
    if not r.startswith("> "):
        raise Skip(f"no coverage recording here ({r})")
    st = cov_state(d)
    if st["on"]:
        raise Skip("recording is on: not touching the user's file")
    made, auto = None, st["auto"]
    try:
        d.cli.put("cov auto off")
        d.cli.put(f"gps {LAT0},{LON0}")
        time.sleep(2.5)
        st = cov_state(d)
        if st["home"]:
            raise Skip("the test position is in the home zone")
        before = st["pending"]
        d.cli.put("cov on")
        for _ in range(20):
            time.sleep(1)
            st = cov_state(d)
            if st["rows"] >= 1:
                break
        check(st["fix"], f"the fake fix not taken for recording: {st}")
        check(st["rows"] >= 1, f"no row written in 20 s: {st} (an SD card in?)")
        row = d.cli.get("cov last")
        # t,lat,lon,acc,age,src,kind,snr,rssi,hop,hb,len,rt: a "nothing heard" row, or a packet's
        check(re.match(r"^\d{10},-?\d+\.\d{5},-?\d+\.\d{5},\d*,\d+,R,(N,,,,,,|P,-?\d+\.\d,-?\d+,[0-9a-f]*,[1-3]?,\d+,[FD])$", row),
              f"a row not in MatterMap's format: {row!r}")
        la, lo = (float(x) for x in row.split(",")[1:3])
        check(abs(la - LAT0) < 1e-4 and abs(lo - LON0) < 1e-4, f"the row's position {la},{lo} isn't the fix")
        d.cli.put("cov off")
        time.sleep(1)
        st = cov_state(d)
        check(st["pending"] == before + 1, f"no finished file waiting: {st}")
        made = st["newest"]
        check(re.match(r"^\d{8}-\d{6}\.csv$", made), f"file name {made!r}")
    finally:
        d.cli.run("cov off")
        if made:
            d.cli.run(f"cov rm {made}")
        d.cli.run("gps off")
        d.cli.run("cov auto " + ("on" if auto else "off"))
    return f"a row recorded, {made} finished, deleted"


@test("coverage-home")
def t_coverage_home(d, out):
    """The home zone set after recording: a finished file keeps only its rows
    away from it, the file being written loses its rows in it and, being in
    the zone now, is finished; then "cov clear" deletes every file. A test build's files only
    (coverage-test/: the user's recordings are elsewhere); recording, home
    and auto upload put back."""
    need_hooks(d)
    r = d.cli.run("cov")
    if not r.startswith("> "):
        raise Skip(f"no coverage recording here ({r})")
    st = cov_state(d)
    home0, auto, on0 = d.cli.get("cov home"), st["auto"], st["on"]
    d.cli.put("cov off")
    a = (int(LAT0 * 1e6), int(LON0 * 1e6))
    b = (int((LAT0 + 0.02) * 1e6), a[1])   # ~2.2 km north
    def fix(p):
        d.cli.put(f"gps {p[0] / 1e6:.6f},{p[1] / 1e6:.6f}")
        time.sleep(2.5)
    def rows_at_least(k, secs):
        for _ in range(secs):
            time.sleep(1)
            if cov_state(d)["rows"] >= k:
                return True
        return False
    try:
        d.cli.put("cov auto off")
        d.cli.get("cov home 0,0 0")   # no zone while recording
        fix(a)
        d.cli.put("cov on")
        check(rows_at_least(1, 20), f"no row at A in 20 s: {cov_state(d)}")
        f1 = d.cli.get("cov file")
        fix(b)
        def at_b():   # a row at B (a "nothing heard" one every 30 s; packets heard add rows at A too)
            for _ in range(40):
                time.sleep(1)
                row = d.cli.get("cov last").split(",")
                if len(row) > 2 and abs(float(row[1]) - b[0] / 1e6) < 1e-4:
                    return True
            return False
        check(at_b(), f"no row at B: {cov_state(d)}")
        d.cli.put("cov off")
        time.sleep(1.5)
        fix(a)
        d.cli.put("cov on")
        check(rows_at_least(1, 20), f"no row at A in the second file: {cov_state(d)}")
        f2 = d.cli.get("cov file")
        check(f2 and f2 != f1, f"the second file {f2!r} is the first {f1!r}")
        n1 = int(d.cli.get(f"cov count {f1}.csv").split(",")[0])
        check(n1 >= 2, f"{f1} has {n1} rows, not A's and B's")
        gone = d.cli.get(f"cov home {a[0]},{a[1]} 1")   # 250 m round A
        check(gone.isdigit() and int(gone) >= 2, f"rows taken out: {gone}, not A's in both files")
        r1, h1 = (int(x) for x in d.cli.get(f"cov count {f1}.csv").split(","))
        check(r1 >= 1 and h1 == 0 and r1 + int(gone) >= n1 + 1, f"{f1} after: {r1},{h1} (rows, in the zone): B's rows only")
        c2 = d.cli.get(f"cov count {f2}.csv")
        check(c2 in ("0,0", "-1,-1"), f"the file being written after: {c2}, not 0,0 (its rows out)")
        # Now in the home zone: the file being written is finished -- with no
        # rows left, deleted -- and nothing more is recorded here.
        time.sleep(1.5)
        check(d.cli.get("cov file") == "", "a file still being written in the home zone")
        check(d.cli.get(f"cov count {f2}.csv") == "-1,-1", f"{f2}, empty, not deleted when finished")
        check(cov_state(d)["home"], "the fix at A isn't in the home zone")
        d.cli.put("cov clear")
        st = cov_state(d)
        check(st["pending"] == 0 and d.cli.get(f"cov count {f1}.csv") == "-1,-1", f"files left after clear: {st}")
    finally:
        d.cli.run("cov off")
        d.cli.run("cov clear")
        d.cli.run("gps off")
        la, lo, rr = home0.split(",")
        d.cli.run(f"cov home {la},{lo} {rr}")
        d.cli.run("cov auto " + ("on" if auto else "off"))
        if on0:
            d.cli.run("cov on")
    return "home rows out of a finished file and the one being written (finished at home), then all cleared"


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


# ── Companion protocol: frames the phone app sends ──────────────────────────
# One test per group of commands; each reads what is there, changes it, reads
# it back, and puts the old value back (a key by its own bytes, not by a CLI
# round trip, where the stored value is a float).

CMD_GET_CONTACTS, CMD_ADD_UPDATE_CONTACT = 4, 9
CMD_GET_BATT_AND_STORAGE, CMD_SET_TUNING_PARAMS = 20, 21
CMD_GET_CHANNEL, CMD_SET_CHANNEL = 31, 32
CMD_GET_TUNING_PARAMS, CMD_GET_STATS = 43, 56
CMD_SET_OTHER_PARAMS, CMD_SET_PATH_HASH_MODE = 38, 61
CMD_SET_AUTOADD_CONFIG, CMD_GET_AUTOADD_CONFIG = 58, 59
RESP_OK, RESP_ERR_CODE = 0, 1
RESP_CONTACTS_START, RESP_END_OF_CONTACTS = 2, 4
RESP_BATT_AND_STORAGE, RESP_CHANNEL_INFO = 12, 18
RESP_TUNING_PARAMS, RESP_STATS, RESP_AUTOADD_CONFIG = 23, 24, 25
ERR_NOT_FOUND, ERR_ILLEGAL_ARG = 2, 6


def ask_err(d, payload):
    """The error code a request is answered with (it must be an error frame)."""
    d.dev.send(payload)
    end = time.time() + d.dev.timeout
    while time.time() < end:
        f = d.dev.read_frame(end - time.time())
        if f is None:
            break
        if f[0] == RESP_ERR_CODE and len(f) <= 2:
            return f[1] if len(f) > 1 else 0
        if f[0] == RESP_OK:
            raise Fail(f"{payload[:2].hex()}: answered OK, an error was expected")
    raise Fail(f"{payload[:2].hex()}: no answer")


def app_start(d):
    """CMD_APP_START parsed: a dict of what the device says about itself."""
    f = d.dev.ask(bytes([CMD_APP_START]) + bytes(7) + b"hwtest", RESP_SELF_INFO)
    check(len(f) >= 58, f"self info is {len(f)} bytes")
    return {"type": f[1], "tx": f[2], "max_tx": f[3], "key": f[4:36],
            "lat": struct.unpack_from("<i", f, 36)[0], "lon": struct.unpack_from("<i", f, 40)[0],
            "multi_acks": f[44], "advert_loc": f[45], "telemetry": f[46], "manual_add": f[47],
            "freq_khz": struct.unpack_from("<I", f, 48)[0], "bw_hz": struct.unpack_from("<I", f, 52)[0],
            "sf": f[56], "cr": f[57], "name": f[58:].decode(errors="replace")}


@test("app-start")
def t_app_start(d, out):
    """CMD_APP_START: the self-info frame against the CLI's own answers."""
    a = app_start(d)
    check(any(a["key"]), "the public key is all zeros")
    check(a["tx"] <= a["max_tx"] and a["max_tx"] > 0, f"tx {a['tx']} dBm of {a['max_tx']}")
    fr, bw, sf, cr = d.cli.get("get radio").split(",")
    check(abs(a["freq_khz"] - float(fr) * 1000) < 2, f"freq {a['freq_khz']} kHz, the CLI says {fr} MHz")
    check(abs(a["bw_hz"] - float(bw) * 1000) < 2, f"bandwidth {a['bw_hz']} Hz, the CLI says {bw} kHz")
    check(a["sf"] == int(sf) and a["cr"] == int(cr), f"sf/cr {a['sf']}/{a['cr']}, the CLI says {sf}/{cr}")
    name = d.cli.get("get name")
    check(a["name"] == name, f"name {a['name']!r}, the CLI says {name!r}")
    again = app_start(d)   # a second start gives the same answer (the contacts iterator is reset, nothing else)
    check(again["key"] == a["key"] and again["name"] == a["name"], "a second APP_START answered differently")
    return f"{a['name']}, {a['tx']} dBm, key {a['key'][:4].hex()}"


def contact_frame(key, name, ctype=1, flags=0, lat=0, lon=0, adv=0):
    """The optional lastmod is left out, so the device stamps its own clock."""
    return (bytes([CMD_ADD_UPDATE_CONTACT]) + key + bytes([ctype, flags, 0xFF]) + bytes(64)
            + name.encode().ljust(32, b"\0") + struct.pack("<Iii", adv, lat, lon))


def parse_contact(f):
    return {"key": f[1:33], "type": f[33], "flags": f[34], "path_len": f[35],
            "name": f[100:132].split(b"\0")[0].decode(errors="replace"),
            "adv": struct.unpack_from("<I", f, 132)[0],
            "lat": struct.unpack_from("<i", f, 136)[0], "lon": struct.unpack_from("<i", f, 140)[0],
            "lastmod": struct.unpack_from("<I", f, 144)[0]}


def list_contacts(d, since=None):
    """CMD_GET_CONTACTS: (the count the start frame announces, the contacts sent)."""
    d.dev.send(bytes([CMD_GET_CONTACTS]) + (struct.pack("<I", since) if since is not None else b""))
    got, count = [], None
    end = time.time() + 20
    while time.time() < end:
        f = d.dev.read_frame(end - time.time())
        if f is None:
            break
        if f[0] == RESP_CONTACTS_START:
            count = struct.unpack_from("<I", f, 1)[0]
        elif f[0] == RESP_CONTACT and count is not None:
            got.append(parse_contact(f))
        elif f[0] == RESP_END_OF_CONTACTS:
            check(count is not None, "the end of the contacts came without a start")
            return count, got
    raise Fail("the contact list never ended")


@test("contacts")
def t_contacts(d, out):
    """A contact added, read, updated, listed (all and since) and removed over the companion protocol."""
    key = bytes([0xC0, 0x7E]) + bytes((i * 7 + 3) & 0xFF for i in range(30))
    check(not knows_contact(d, key), "the test contact is already there")
    n0, _ = list_contacts(d)
    added = False
    try:
        d.dev.ask(contact_frame(key, "hwtest one", lat=50123456, lon=19654321, adv=1700000000), RESP_OK)
        added = True
        f = parse_contact(d.dev.ask(bytes([CMD_GET_CONTACT_BY_KEY]) + key, RESP_CONTACT))
        check(f["name"] == "hwtest one" and f["type"] == 1 and f["path_len"] == 0xFF, f"read back {f}")
        check(f["lat"] == 50123456 and f["lon"] == 19654321 and f["adv"] == 1700000000, f"position/advert: {f}")
        check(f["lastmod"] > 1000000000, f"lastmod {f['lastmod']}: not the device's clock")
        # an update of the same key: the name and the position change, the entry is still one
        d.dev.ask(contact_frame(key, "hwtest two", flags=1, lat=-1000, lon=2000, adv=1700000001), RESP_OK)
        f = parse_contact(d.dev.ask(bytes([CMD_GET_CONTACT_BY_KEY]) + key, RESP_CONTACT))
        check(f["name"] == "hwtest two" and f["flags"] & 1 and f["lat"] == -1000 and f["lon"] == 2000,
              f"after the update {f}")
        n1, every = list_contacts(d)
        check(n1 == n0 + 1, f"{n0} contacts before, {n1} announced with the new one")
        check(len(every) == n1, f"{n1} announced, {len(every)} sent")
        check(sum(c["key"] == key for c in every) == 1, "the new contact isn't in the list exactly once")
        _, since = list_contacts(d, since=f["lastmod"] - 1)
        check(any(c["key"] == key for c in since), "the list since its own lastmod doesn't have it")
        _, later = list_contacts(d, since=0xFFFFFFF0)
        check(not later, f"{len(later)} contacts modified since the far future")
    finally:
        if added:
            d.dev.ask(bytes([CMD_REMOVE_CONTACT]) + key, RESP_OK)
    check(not knows_contact(d, key), "the contact is still there after the removal")
    check(ask_err(d, bytes([CMD_REMOVE_CONTACT]) + key) == ERR_NOT_FOUND, "a second removal wasn't 'not found'")
    n2, _ = list_contacts(d)
    check(n2 == n0, f"{n0} contacts at the start, {n2} at the end")
    return f"{n0} contacts, one added, updated and removed"


@test("channels")
def t_channels(d, out):
    """CMD_GET_CHANNEL / SET_CHANNEL: channel 0 renamed and put back; an index past the end is an error."""
    f = d.dev.ask(bytes([CMD_GET_CHANNEL, 0]), RESP_CHANNEL_INFO)
    check(len(f) >= 50 and f[1] == 0, f"channel 0: {len(f)} bytes")
    name, secret = f[2:34].split(b"\0")[0], f[34:50]
    check(not any(f[2:34].split(b"\0", 1)[1]), f"the name field carries bytes after its NUL: {f[2:34]!r}")
    new = (name[:28] + b" hw").ljust(32, b"\0")
    old = name.ljust(32, b"\0")
    try:
        d.dev.ask(bytes([CMD_SET_CHANNEL, 0]) + new + secret, RESP_OK)
        g = d.dev.ask(bytes([CMD_GET_CHANNEL, 0]), RESP_CHANNEL_INFO)
        check(g[2:34].split(b"\0")[0] == new.rstrip(b"\0") and g[34:50] == secret, f"renamed: {g[2:34]!r}")
    finally:
        d.dev.ask(bytes([CMD_SET_CHANNEL, 0]) + old + secret, RESP_OK)
    g = d.dev.ask(bytes([CMD_GET_CHANNEL, 0]), RESP_CHANNEL_INFO)
    check(g[1:50] == f[1:50], "channel 0 didn't come back as it was")
    check(ask_err(d, bytes([CMD_GET_CHANNEL, 200])) == ERR_NOT_FOUND, "channel 200 isn't 'not found'")
    check(ask_err(d, bytes([CMD_SET_CHANNEL, 200]) + old + secret) == ERR_NOT_FOUND, "setting channel 200 isn't 'not found'")
    return f"channel 0 {name.decode(errors='replace')!r} renamed and back"


@test("device-stats")
def t_stats(d, out):
    """GET_STATS (core, radio, packets) and GET_BATT_AND_STORAGE: the sizes and what's plausible."""
    f = d.dev.ask(bytes([CMD_GET_STATS, 0]), RESP_STATS)
    check(len(f) == 11 and f[1] == 0, f"core stats: {len(f)} bytes")
    batt, up = struct.unpack_from("<HI", f, 2)
    check(batt == 0 or 2500 <= batt <= 5500, f"battery {batt} mV")
    mv = mem(d)   # only a test build has it
    if mv:
        check(abs(up - mv[1]) < 10, f"uptime {up} s, mem says {mv[1]} s")
    f = d.dev.ask(bytes([CMD_GET_STATS, 1]), RESP_STATS)
    check(len(f) == 14 and f[1] == 1, f"radio stats: {len(f)} bytes")
    nf = struct.unpack_from("<h", f, 2)[0]
    check(-140 <= nf <= 0, f"noise floor {nf} dBm")
    f = d.dev.ask(bytes([CMD_GET_STATS, 2]), RESP_STATS)
    check(len(f) >= 26 and f[1] == 2, f"packet stats: {len(f)} bytes")
    check(ask_err(d, bytes([CMD_GET_STATS, 9])) == ERR_ILLEGAL_ARG, "stats type 9 isn't an illegal argument")
    f = d.dev.ask(bytes([CMD_GET_BATT_AND_STORAGE]), RESP_BATT_AND_STORAGE)
    check(len(f) == 11, f"battery and storage: {len(f)} bytes")
    bmv, used, total = struct.unpack_from("<HII", f, 1)
    check(total > 0 and used <= total, f"storage {used} of {total} kB")
    return f"battery {bmv} mV, storage {used}/{total} kB, noise floor {nf} dBm"


@test("tuning")
def t_tuning(d, out):
    """GET/SET_TUNING_PARAMS against rxdelay and af, put back through the CLI (a float: no round trip by bytes)."""
    f = d.dev.ask(bytes([CMD_GET_TUNING_PARAMS]), RESP_TUNING_PARAMS)
    check(len(f) == 9, f"tuning: {len(f)} bytes")
    rx, af = struct.unpack_from("<II", f, 1)
    cli_rx, cli_af = d.cli.get("get rxdelay"), d.cli.get("get af")
    check(abs(float(cli_rx) * 1000 - rx) <= 1 and abs(float(cli_af) * 1000 - af) <= 1,
          f"frame {rx}/{af}, CLI {cli_rx}/{cli_af}")
    try:
        d.dev.ask(bytes([CMD_SET_TUNING_PARAMS]) + struct.pack("<II", 1500, 2250), RESP_OK)
        g = d.dev.ask(bytes([CMD_GET_TUNING_PARAMS]), RESP_TUNING_PARAMS)
        check(struct.unpack_from("<II", g, 1) == (1500, 2250), f"set 1500/2250, reads {struct.unpack_from('<II', g, 1)}")
        check(abs(float(d.cli.get("get af")) - 2.25) < 0.001, "the CLI doesn't see the new af")
    finally:
        d.cli.put(f"set rxdelay {cli_rx}")
        d.cli.put(f"set af {cli_af}")
    check(d.cli.get("get rxdelay") == cli_rx and d.cli.get("get af") == cli_af, "rxdelay/af didn't come back")
    return f"rxdelay {cli_rx}, af {cli_af}"


@test("other-params")
def t_other_params(d, out):
    """SET_OTHER_PARAMS, SET_AUTOADD_CONFIG and SET_PATH_HASH_MODE, each read back and put back."""
    a = app_start(d)
    try:
        new_multi = 1 if a["multi_acks"] == 0 else 0
        new_manual = 1 - (a["manual_add"] & 1)
        d.dev.ask(bytes([CMD_SET_OTHER_PARAMS, new_manual, a["telemetry"], a["advert_loc"], new_multi]), RESP_OK)
        b = app_start(d)
        check(b["manual_add"] == new_manual and b["multi_acks"] == new_multi, f"set {new_manual}/{new_multi}, reads {b['manual_add']}/{b['multi_acks']}")
        check(b["telemetry"] == a["telemetry"] and b["advert_loc"] == a["advert_loc"], "telemetry or advert policy changed on the way")
        check(d.cli.get("get multi.acks") == str(new_multi), "the CLI doesn't see multi.acks")
    finally:
        d.dev.ask(bytes([CMD_SET_OTHER_PARAMS, a["manual_add"], a["telemetry"], a["advert_loc"], a["multi_acks"]]), RESP_OK)
    b = app_start(d)
    check((b["manual_add"], b["telemetry"], b["advert_loc"], b["multi_acks"]) ==
          (a["manual_add"], a["telemetry"], a["advert_loc"], a["multi_acks"]), "the other params didn't come back")

    f = d.dev.ask(bytes([CMD_GET_AUTOADD_CONFIG]), RESP_AUTOADD_CONFIG)
    check(len(f) == 3, f"autoadd: {len(f)} bytes")
    cfg, hops = f[1], f[2]
    try:
        d.dev.ask(bytes([CMD_SET_AUTOADD_CONFIG, cfg ^ 1, (hops + 3) % 60]), RESP_OK)
        g = d.dev.ask(bytes([CMD_GET_AUTOADD_CONFIG]), RESP_AUTOADD_CONFIG)
        check(g[1] == cfg ^ 1 and g[2] == (hops + 3) % 60, f"autoadd set {cfg ^ 1}/{(hops + 3) % 60}, reads {g[1]}/{g[2]}")
        d.dev.ask(bytes([CMD_SET_AUTOADD_CONFIG, 0, 200]), RESP_OK)
        check(d.dev.ask(bytes([CMD_GET_AUTOADD_CONFIG]), RESP_AUTOADD_CONFIG)[2] == 64, "max hops 200 isn't cut to 64")
    finally:
        d.dev.ask(bytes([CMD_SET_AUTOADD_CONFIG, cfg, hops]), RESP_OK)
    g = d.dev.ask(bytes([CMD_GET_AUTOADD_CONFIG]), RESP_AUTOADD_CONFIG)
    check((g[1], g[2]) == (cfg, hops), f"autoadd {g[1]}/{g[2]}, was {cfg}/{hops}")

    mode = d.cli.get("get path.hash.mode")
    try:
        want = (int(mode) + 1) % 3
        d.dev.ask(bytes([CMD_SET_PATH_HASH_MODE, 0, want]), RESP_OK)
        check(d.cli.get("get path.hash.mode") == str(want), "the CLI doesn't see the new path hash mode")
        check(ask_err(d, bytes([CMD_SET_PATH_HASH_MODE, 0, 3])) == ERR_ILLEGAL_ARG, "path hash mode 3 isn't an illegal argument")
    finally:
        d.dev.ask(bytes([CMD_SET_PATH_HASH_MODE, 0, int(mode)]), RESP_OK)
    check(d.cli.get("get path.hash.mode") == mode, "the path hash mode didn't come back")
    return f"manual add, multi acks, autoadd {cfg}/{hops}, path hash {mode}: set and back"


@test("chats")
def t_chats(d, out):
    """hist list / hist <conv> <j> / hist read: the conversations the configurator's Chats tab reads (no send: that goes on air)."""
    if not d.knows("hist list 0"):
        raise Skip("no chats over USB in this firmware")
    convs = []
    for k in range(60):
        r = d.cli.get(f"hist list {k}")
        if r == "end":
            break
        f = r.split("|")
        check(len(f) >= 6 and f[0] in ("ch", "dm"), f"hist list {k}: {r!r}")
        convs.append({"kind": f[0], "id": f[1], "name": "|".join(f[2:-3]),
                      "count": int(f[-3]), "unread": int(f[-2]), "ts": int(f[-1])})
    else:
        raise Fail("hist list never ended")
    ts = [v["ts"] for v in convs]
    check(ts == sorted(ts, reverse=True), f"not newest first: {ts}")
    check(len({(v["kind"], v["id"]) for v in convs}) == len(convs), "a conversation listed twice")
    check(d.cli.run("hist list 999") == "> end", "past the end isn't '> end'")
    check(d.cli.run("hist xx 1 0").startswith("Error"), "a bad conversation isn't an error")
    if not convs:
        raise Skip("no history on the device")
    for v in convs[:5]:
        conv = f"{v['kind']} {v['id']}"
        r = d.cli.get(f"hist {conv} 0")
        m = re.match(r"^(\d+)\|(\d+)\|([io])(\d)\|(\d+)\|(.*)$", r, re.S)
        check(m, f"hist {conv} 0: {r!r}")
        check(int(m[1]) == v["count"], f"{conv}: {m[1]} messages, the list says {v['count']}")
        check(int(m[2]) == v["ts"], f"{conv}: the newest at {m[2]}, the list says {v['ts']}")
        total, got = int(m[5]), m[6].encode()
        while len(got) < total:   # the rest of a long one, from where the frame cut it
            r = d.cli.get(f"hist {conv} 0 {len(got)}")
            part = r.split("|", 4)[4].encode()
            check(part, f"{conv}: no progress at byte {len(got)} of {total}")
            got += part
        check(len(got) == total, f"{conv}: {len(got)} bytes put together, {total} said")
        check(d.cli.get(f"hist {conv} {v['count']}") == f"{v['count']}|none", f"{conv}: past the last isn't 'none'")
    calm = next((v for v in convs if v["unread"] == 0), None)   # marking read can't be undone: only where nothing is unread
    if calm:
        check(d.cli.run(f"hist read {calm['kind']} {calm['id']}") == "OK", "hist read didn't answer OK")
    check(d.cli.run("hist read dm 00000000").startswith("Error"), "hist read of an unknown contact isn't an error")
    return f"{len(convs)} conversations, the newest of {min(5, len(convs))} read whole"


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


# The L2 joins WiFi once after boot for the clock (ui-lvgl/WifiScreen.h:
# WIFI_CLOCK_BOOT_MS 15 s, then up to WIFI_CLOCK_JOIN_MS 50 s): the internal
# heap is ~40 KB down meanwhile, and the WiFi stack's first start keeps ~14 KB
# for good (the lwIP task, netif, event loop; checked: five more on/off rounds
# came back to the byte). A heap baseline taken before that is no baseline.
BOOT_WIFI_JOIN_S = 15 + 50 + 3


def settle(d):
    """Past the device's own start-up work, so the memory check starts from where it stays."""
    m = mem(d)
    if not m or m[1] >= BOOT_WIFI_JOIN_S or d.cli.run("get wifi").lower() != "> on":
        return
    print(f"  waiting {BOOT_WIFI_JOIN_S - m[1]} s for the boot's WiFi clock join to end")
    time.sleep(BOOT_WIFI_JOIN_S - m[1])


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
    settle(d)
    if d.hooks and d.knows("usb"):
        d.cli.get("usb clear")
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
    if d.hooks and d.knows("usb"):   # every frame to the host whole (a cut one: the 300 ms limit hit)
        sent, cut, slow, longest = (int(x) for x in d.cli.get("usb").split(","))
        r = {"test": "usb-frames", "seconds": 0, "round": args.repeat, "status": "FAIL" if cut else "PASS",
             "note": f"{sent} frames, {cut} cut, {slow} over 50 ms, the longest {longest} ms"}
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
