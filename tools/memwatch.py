#!/usr/bin/env python3
"""
Memory on a Wio Tracker L2 test build, live over USB: PSRAM (what LVGL, the
map tiles and the big buffers live in), the internal heap, and LVGL's
objects, timers and animations. Watch it while you use the device, or play
a script of screens, taps and screenshots again and again and see what each
round leaves behind: objects, blocks or timers that only ever grow are a leak.

Needs a test build (`uv run tools/hwtest.py --flash Wio_Tracker_L2_companion_solo_lvgl`:
-D SOLO_TEST_HOOKS, `ver` says "+test"); flash the release again afterwards
(hwtest.py --flash ... --release).

Usage:
    uv run tools/memwatch.py watch                     # a line every 10 s, Ctrl-C to stop
    uv run tools/memwatch.py watch --every 60 --csv mem.csv
    uv run tools/memwatch.py cycle --rounds 10         # every screen in turn, ten times
    uv run tools/memwatch.py cycle --screens map,chats,diag --rounds 20
    uv run tools/memwatch.py run --repeat 20 "idle map" "wait 3" "flowdemo 8" "wait 6" "idle home" "wait 2"
    uv run tools/memwatch.py run "tap 160 120" "swipe 160 200 160 60" "shot"
    uv run tools/memwatch.py once                      # one reading, every figure

A step of `run` is a console command (`idle <screen>`, `tap x y [ms]`,
`swipe x0 y0 x1 y1 [ms]`, `shot`, `screen on|off`, `flowdemo [n]`, any
`get`/`set`), `wait <s>`, `mem` (a reading now) or `png <file>` (the screen
over USB, saved here as a PNG; `{r}` in the name is the round). After each
round, a reading and its change from the first.
"""

import argparse
import csv
import struct
import sys
import time
import zlib

from companion import Companion, DeviceError

CMD_RUN_CLI = 66
RESP_CLI_REPLY = 29

FIELDS = ["psram_free", "psram_total", "psram_low", "psram_big", "blocks_used", "blocks_free",
          "int_free", "int_big", "objs", "screens", "timers", "anims", "uptime"]

SCREENS = ["home", "chats", "contacts", "nearby", "map", "settings", "radio", "scopes", "repeater", "bot",
           "clock", "gps", "diag", "compass", "gallery", "battery", "quickmsgs", "routes", "storage", "wifi"]


def cli(dev, cmd):
    f = dev.ask(bytes([CMD_RUN_CLI]) + cmd.encode()[:160], RESP_CLI_REPLY)
    return f[1:].decode(errors="replace").strip()


CMD_GET_SCREENSHOT = 0x7E
RESP_SCREENSHOT = 0x7E


def png(dev, path):
    """The screen (a colour UI's RGB565 frame) to a PNG file, without Pillow."""
    dev.send(bytes([CMD_GET_SCREENSHOT]))
    buf, total, got, w, h = bytearray(), None, 0, 0, 0
    end = time.time() + 8
    while time.time() < end and (total is None or got < total):
        f = dev.read_frame(end - time.time())
        if f is None:
            break
        if f[0] != RESP_SCREENSHOT or len(f) < 11 or f[1] != 2:
            continue
        fw, fh, idx, n = struct.unpack_from("<HHHH", f, 3)
        if idx == 0:
            buf, total, got, w, h = bytearray(), n, 0, fw, fh
        if total is None:
            continue
        buf += f[11:]
        got += 1
    if total is None or got < total or len(buf) < w * h * 2:
        print(f"  png {path}: no screenshot")
        return
    rows = bytearray()
    for y in range(h):
        rows.append(0)
        for x in range(w):
            c = buf[2 * (y * w + x)] | buf[2 * (y * w + x) + 1] << 8
            r, g, b = c >> 11 & 31, c >> 5 & 63, c & 31
            rows += bytes(((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2)))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)
    with open(path, "wb") as o:
        o.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(bytes(rows), 6)) + chunk(b"IEND", b""))


def reading(dev):
    r = cli(dev, "psram")
    if not r.startswith(">"):
        raise DeviceError(f"no memory reading ({r!r}): a test build with this command is needed")
    v = [int(x) for x in r[1:].strip().split(",")]
    return dict(zip(FIELDS, v))


def kb(b):
    return f"{b / 1024:7.1f}"


HEAD = " time     uptime   PSRAM free  Δ KB   largest  lowest   used blk   int free  int big   objs   Δ  timers anims"


def line(m, base, t0):
    d = (m["psram_free"] - base["psram_free"]) / 1024
    do = m["objs"] - base["objs"]
    return (f" {time.strftime('%H:%M:%S')} {m['uptime']:7d}s {kb(m['psram_free'])} {d:+7.1f} {kb(m['psram_big'])} "
            f"{kb(m['psram_low'])} {m['blocks_used']:8d}   {kb(m['int_free'])} {kb(m['int_big'])} "
            f"{m['objs']:6d} {do:+4d} {m['timers']:6d} {m['anims']:5d}")


def check_restart(m, prev):
    if prev and m["uptime"] < prev["uptime"]:
        print(" !! the device restarted (uptime went back)")


def open_dev(args):
    dev = Companion(args.port)
    ver = cli(dev, "ver")
    if "+test" not in ver:
        print(f"Not a test build ({ver}): flash one with hwtest.py --flash first.", file=sys.stderr)
        sys.exit(2)
    return dev


def cmd_once(dev, args):
    m = reading(dev)
    for k in FIELDS:
        v = m[k]
        print(f"  {k:12} {v}" + (f"  ({v / 1024:.1f} KB)" if k.startswith(("psram", "int")) else ""))


def cmd_watch(dev, args):
    w = None
    if args.csv:
        f = open(args.csv, "a", newline="")
        w = csv.writer(f)
        if f.tell() == 0:
            w.writerow(["time"] + FIELDS)
    base = prev = None
    t0 = time.time()
    print(HEAD)
    try:
        while True:
            m = reading(dev)
            base = base or m
            check_restart(m, prev)
            prev = m
            print(line(m, base, t0), flush=True)
            if w:
                w.writerow([time.strftime("%Y-%m-%d %H:%M:%S")] + [m[k] for k in FIELDS])
                f.flush()
            time.sleep(args.every)
    except KeyboardInterrupt:
        print()


def play(dev, steps, rounds, settle):
    m0 = reading(dev)
    prev = m0
    print(HEAD)
    print(line(m0, m0, 0) + "  start")
    t0 = time.time()
    for r in range(1, rounds + 1):
        for s in steps:
            if s.startswith("wait "):
                time.sleep(float(s[5:]))
            elif s == "mem":
                print(line(reading(dev), m0, t0) + "  (mem)")
            elif s.startswith("png "):
                png(dev, s[4:].replace("{r}", str(r)))
            else:
                rep = cli(dev, s)
                if rep.startswith("Error") or rep.lower().startswith("unknown"):
                    print(f"  {s}: {rep}")
                if s.startswith("idle "):   # the map asks for USB drive mode, the screen is built in fill steps
                    time.sleep(settle)
        time.sleep(settle)
        m = reading(dev)
        check_restart(m, prev)
        prev = m
        print(line(m, m0, t0) + f"  round {r}", flush=True)
    d = (prev["psram_free"] - m0["psram_free"]) / 1024
    print(f"\n{rounds} round(s): PSRAM free {d:+.1f} KB, blocks in use {prev['blocks_used'] - m0['blocks_used']:+d}, "
          f"LVGL objects {prev['objs'] - m0['objs']:+d}, timers {prev['timers'] - m0['timers']:+d}")
    if rounds > 2:
        print("(the first round loads caches and fonts; look at the change from round 2 on)")


def cmd_cycle(dev, args):
    screens = args.screens.split(",") if args.screens else SCREENS
    steps = []
    for s in screens:
        steps += [f"idle {s}", f"wait {args.wait}"]
    steps += ["idle home"]
    play(dev, steps, args.rounds, 1.0)


def cmd_run(dev, args):
    play(dev, args.steps, args.repeat, 0.5)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--port", help="serial port (default: the one Solo device found)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    sub.add_parser("once", help="one reading, every figure")
    p = sub.add_parser("watch", help="a reading every few seconds")
    p.add_argument("--every", type=float, default=10, help="seconds between readings (10)")
    p.add_argument("--csv", help="also append the readings to this CSV file")
    p = sub.add_parser("cycle", help="open the screens in turn, round after round")
    p.add_argument("--screens", help="comma-separated (default: all): " + ",".join(SCREENS))
    p.add_argument("--rounds", type=int, default=5)
    p.add_argument("--wait", type=float, default=2, help="seconds on each screen (2)")
    p = sub.add_parser("run", help="play console steps, round after round")
    p.add_argument("steps", nargs="+")
    p.add_argument("--repeat", type=int, default=1)
    args = ap.parse_args()
    try:
        with open_dev(args) as dev:
            {"once": cmd_once, "watch": cmd_watch, "cycle": cmd_cycle, "run": cmd_run}[args.cmd](dev, args)
    except DeviceError as e:
        print(f"Error: {e}", file=sys.stderr)
        sys.exit(1)


if __name__ == "__main__":
    main()
