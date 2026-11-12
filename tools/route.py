#!/usr/bin/env python3
"""
Planned routes on the device: put a GPX route on it to follow, list them,
delete them. The same as the Route card of the Solo site's USB tools. On the
device: Tools > Trail, hold Enter, Routes.

The GPX's tracks (or its route) become one line, simplified to as many points
as the device holds (or --points): Douglas-Peucker by rank, so the bends stay
and the straight stretches go; the deviation printed is how far the simplified
line strays from the original at most.

Usage:
    uv run tools/route.py upload walk.gpx                 # as many points as the device holds
    uv run tools/route.py upload walk.gpx --points 300 --name "Hala Gąsienicowa"
    uv run tools/route.py upload walk.gpx --reverse --dry-run   # just the numbers, nothing sent
    uv run tools/route.py list
    uv run tools/route.py delete 2

Firmware side: examples/companion_radio/MyMeshRoutes.h (CMD_ROUTE) and
PlannedRoute.h. Needs Solo 2.3 or newer on a board that keeps messages on
flash (Wio Tracker L1, Watch13, Heltec V3/V4, Cardputer).
"""

import argparse
import math
import struct
import sys
import xml.etree.ElementTree as ET

CMD_ROUTE = 0x7D
INFO, GET, BEGIN, DATA, END, DELETE, ABORT = range(7)
ELE_NONE = -32768
NAME_MAX = 23   # bytes, UTF-8


# ── GPX ──────────────────────────────────────────────────────────────────────

def read_gpx(path):
    """(points [(lat, lon, ele|None)], name) -- every track segment in order, else the route's points."""
    root = ET.parse(path).getroot()
    pts = []
    for tag in ("{*}trkpt", "{*}rtept"):
        for n in root.iterfind(".//" + tag):
            try:
                lat, lon = float(n.get("lat")), float(n.get("lon"))
            except (TypeError, ValueError):
                continue
            e = n.find("{*}ele")
            try:
                ele = float(e.text) if e is not None else None
            except (TypeError, ValueError):
                ele = None
            pts.append((lat, lon, ele))
        if pts:
            break
    clean = [p for i, p in enumerate(pts) if i == 0 or p[:2] != pts[i - 1][:2]]
    name = None
    for tag in ("{*}trk/{*}name", "{*}rte/{*}name", "{*}metadata/{*}name"):
        n = root.find(tag)
        if n is not None and n.text and n.text.strip():
            name = n.text.strip()
            break
    return clean, name


def haversine(a, b):
    r = math.pi / 180
    dlat, dlon = (b[0] - a[0]) * r, (b[1] - a[1]) * r
    s = math.sin(dlat / 2) ** 2 + math.cos(a[0] * r) * math.cos(b[0] * r) * math.sin(dlon / 2) ** 2
    return 2 * 6371008.8 * math.asin(min(1, math.sqrt(s)))


def length(pts):
    return sum(haversine(pts[i - 1], pts[i]) for i in range(1, len(pts)))


def rank_points(pts):
    """Douglas-Peucker ranks in metres (the ends: inf); the indices by rank, highest first."""
    n = len(pts)
    k = math.cos(math.radians(pts[0][0]))
    X = [p[1] * k * 111320 for p in pts]
    Y = [p[0] * 110574 for p in pts]
    rank = [0.0] * n
    rank[0] = rank[-1] = math.inf
    stack = [(0, n - 1, math.inf)]
    while stack:
        a, b, cap = stack.pop()
        if b - a < 2:
            continue
        dx, dy = X[b] - X[a], Y[b] - Y[a]
        l2 = dx * dx + dy * dy
        best, bi = -1.0, -1
        for i in range(a + 1, b):
            t = ((X[i] - X[a]) * dx + (Y[i] - Y[a]) * dy) / l2 if l2 else 0
            t = max(0.0, min(1.0, t))
            ex, ey = X[a] + t * dx - X[i], Y[a] + t * dy - Y[i]
            d = ex * ex + ey * ey
            if d > best:
                best, bi = d, i
        d = min(math.sqrt(best), cap)
        rank[bi] = d
        stack.append((a, bi, d))
        stack.append((bi, b, d))
    order = sorted(range(n), key=lambda i: -rank[i])
    return rank, order


def simplify(pts, count):
    """(the `count` most telling points in order, the most the line then strays in metres)."""
    if count >= len(pts):
        return pts, 0.0
    rank, order = rank_points(pts)
    keep = sorted(order[:count])
    return [pts[i] for i in keep], rank[order[count]]


def fmt_dist(m):
    return f"{m:.0f} m" if m < 1000 else f"{m / 1000:.2f} km"


def cut_utf8(s, n):
    b = s.encode()
    if len(b) <= n:
        return b
    while n > 0 and (b[n] & 0xC0) == 0x80:
        n -= 1
    return b[:n]


# ── Device ───────────────────────────────────────────────────────────────────

def dev_info(dev):
    f = dev.ask(bytes([CMD_ROUTE, INFO]), CMD_ROUTE)
    slots, max_pts, used = f[3], struct.unpack_from("<H", f, 4)[0], struct.unpack_from("<I", f, 6)[0]
    return {"slots": slots, "max_points": max_pts, "used": used, "per_frame": f[10] or 16}


def dev_list(dev, info):
    rows = []
    for s in range(info["slots"]):
        if not info["used"] & (1 << s):
            continue
        f = dev.ask(bytes([CMD_ROUTE, GET, s]), CMD_ROUTE)
        count, length_m = struct.unpack_from("<HI", f, 3)
        rows.append((s, count, length_m, f[10:].decode(errors="replace")))
    return rows


def connect(port):
    from companion import Companion, DeviceError
    dev = Companion(port)
    try:
        info = dev_info(dev)
    except DeviceError as e:
        dev.close()
        sys.exit(f"The device has no routes ({e}): it needs Solo 2.3 or newer, on a board that keeps messages on flash.")
    return dev, info


def cmd_list(args):
    dev, info = connect(args.port)
    with dev:
        rows = dev_list(dev, info)
    if not rows:
        print(f"No routes on the device (room for {info['slots']}).")
    for s, count, length_m, name in rows:
        print(f"{s}  {name or f'Route {s + 1}':<24} {fmt_dist(length_m):>9}  {count} pts")


def cmd_delete(args):
    from companion import DeviceError
    dev, info = connect(args.port)
    with dev:
        try:
            dev.ask(bytes([CMD_ROUTE, DELETE, args.slot]), 0)
        except DeviceError as e:
            sys.exit(f"Couldn't delete slot {args.slot}: {e}")
    print(f"Deleted slot {args.slot}.")


def cmd_upload(args):
    pts, gpx_name = read_gpx(args.gpx)
    if len(pts) < 2:
        sys.exit("That GPX has no track or route in it.")
    if args.reverse:
        pts.reverse()
    has_ele = any(p[2] is not None for p in pts)
    name = args.name or gpx_name or args.gpx.rsplit("/", 1)[-1].rsplit(".", 1)[0]

    dev = info = None
    max_pts = args.points or 4000
    if not args.dry_run:
        dev, info = connect(args.port)
        max_pts = min(args.points or info["max_points"], info["max_points"])
    kept, dev_m = simplify(pts, max(2, max_pts))
    print(f"{name}: {fmt_dist(length(pts))}, {len(pts)} points -> {len(kept)} (strays up to {dev_m:.0f} m)")
    if args.dry_run:
        return

    from companion import DeviceError
    with dev:
        if info["used"] == (1 << info["slots"]) - 1:
            sys.exit(f"The device holds {info['slots']} routes: delete one first (route.py list / delete).")
        nb = cut_utf8(name, NAME_MAX)
        begun = False
        try:
            f = dev.ask(bytes([CMD_ROUTE, BEGIN, 0xFF]) + struct.pack("<HB", len(kept), 1 if has_ele else 0) + nb, CMD_ROUTE)
            slot, begun = f[2], True
            per = info["per_frame"]
            for i in range(0, len(kept), per):
                chunk = kept[i:i + per]
                body = b"".join(
                    struct.pack("<iih", round(p[0] * 1e6), round(p[1] * 1e6),
                                ELE_NONE if p[2] is None else max(-32767, min(32767, round(p[2]))))
                    for p in chunk)
                dev.ask(bytes([CMD_ROUTE, DATA]) + struct.pack("<HB", i, len(chunk)) + body, 0)
                print(f"\r  sending... {100 * (i + len(chunk)) // len(kept)}%", end="", flush=True)
            f = dev.ask(bytes([CMD_ROUTE, END]), CMD_ROUTE)
            begun = False
            print(f"\rSent to slot {slot}: {fmt_dist(struct.unpack_from('<I', f, 3)[0])}. "
                  "On the device: Tools > Trail, hold Enter, Routes.")
        except DeviceError as e:
            if begun:
                try:
                    dev.ask(bytes([CMD_ROUTE, ABORT]), 0)
                except DeviceError:
                    pass
            sys.exit(f"\nThe route didn't go: {e}")


def main():
    ap = argparse.ArgumentParser(description="Planned routes on the device")
    ap.add_argument("--port", "-p", help="serial port (default: auto-detect)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    up = sub.add_parser("upload", help="put a GPX route on the device")
    up.add_argument("gpx")
    up.add_argument("--points", "-n", type=int, help="points to keep (default: as many as the device holds)")
    up.add_argument("--name", help=f"name on the device (up to {NAME_MAX} bytes; default: the GPX's)")
    up.add_argument("--reverse", "-r", action="store_true", help="the other way round")
    up.add_argument("--dry-run", action="store_true", help="simplify and print, send nothing")
    up.set_defaults(fn=cmd_upload)
    sub.add_parser("list", help="the routes on the device").set_defaults(fn=cmd_list)
    de = sub.add_parser("delete", help="delete a route by its slot (see list)")
    de.add_argument("slot", type=int)
    de.set_defaults(fn=cmd_delete)
    args = ap.parse_args()
    args.fn(args)


if __name__ == "__main__":
    main()
