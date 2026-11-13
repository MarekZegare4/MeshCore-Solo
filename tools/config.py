#!/usr/bin/env python3
"""
The device's configuration over USB: a backup to a JSON file and back, the
two melodies, the order of Home's pages and the waypoints. The same as the Solo site's
configurator (configure.html), whose backup files this reads and writes.
Quick messages and single settings: console.py (msgs, set msg.2 ..., get/set <key>).

Usage:
    uv run tools/config.py export                     # solo-config_<name>_<date>.json
    uv run tools/config.py export my-l1.json
    uv run tools/config.py import my-l1.json          # radio, settings, Home, messages, melodies
    uv run tools/config.py import my-l1.json --only settings,melodies
    uv run tools/config.py import my-l1.json --identity   # also the name, Bluetooth PIN and time zone
    uv run tools/config.py melody 1                   # melody 1 as RTTTL
    uv run tools/config.py melody 2 "Tune:d=8,o=5,b=120:c,e,g,c6"
    uv run tools/config.py melody 2 ringtone.txt      # a file holding one RTTTL line
    uv run tools/config.py pages                      # Home's pages, in order
    uv run tools/config.py pages Clock Messages Status Tools Settings   # that order; the rest hidden
    uv run tools/config.py waypoints                  # the device's waypoints
    uv run tools/config.py waypoints walk.gpx         # add a GPX's waypoints (those not there yet)
    uv run tools/config.py waypoints --delete 3       # delete waypoint 3

Firmware side: examples/companion_radio/ui-core/SettingsCli.h (keys, opts,
melody.N, msg.N, wpts, wpt.N) and ui-new UITask.cpp (pages, OLED boards only). Needs Solo
2.3 or newer.
"""

import argparse
import datetime
import json
import re
import sys
import xml.etree.ElementTree as ET
from pathlib import Path

from companion import Companion, DeviceError

CMD_DEVICE_QUERY = 22
RESP_DEVICE_INFO = 13
CMD_RUN_CLI = 66
RESP_CLI_REPLY = 29
CLI_MIN_VER = 14

FORMAT = "meshcore-solo-config"
PARTS = ["identity", "radio", "settings", "home", "messages", "melodies", "waypoints"]
WPT_LABEL = 11   # bytes a device waypoint's name holds
DEFAULT_PARTS = [p for p in PARTS if p != "identity"]

# A melody note (ui-core/SoundControl.h): bits 0-2 pitch (0 rest, 1..7 c d e f
# g a b), 3-4 octave - 4, 5-6 length 1/4 .. 1/32.
MAX_NOTES, OCT_MIN, OCT_MAX = 32, 4, 6
BPMS = [60, 90, 120, 150, 180]
LETTERS = "pcdefgab"


class Cli:
    def __init__(self, dev):
        self.dev = dev

    def run(self, cmd):
        f = self.dev.ask(bytes([CMD_RUN_CLI]) + cmd.encode()[:160], RESP_CLI_REPLY)
        return f[1:].decode(errors="replace")

    def get(self, cmd):
        r = self.run(cmd)
        if not r.startswith("> "):
            raise DeviceError(r or "no reply")
        return r[2:]

    def put(self, cmd):
        r = self.run(cmd)
        if r != "OK" and not r.startswith("> ") and not r.startswith("OK"):
            raise DeviceError(r or "no reply")
        return r


def connect(port):
    dev = Companion(port)
    try:
        info = dev.ask(bytes([CMD_DEVICE_QUERY, CLI_MIN_VER]), RESP_DEVICE_INFO)
    except DeviceError as e:
        dev.close()
        sys.exit(f"The device didn't answer ({e}). Is the phone app connected over Bluetooth?")
    if info[1] < CLI_MIN_VER:
        dev.close()
        sys.exit("This firmware has no console: it needs Solo 2.0 or newer.")
    s = lambda a, n: info[a:a + n].split(b"\0")[0].decode(errors="replace")
    return dev, {"board": s(20, 40), "firmware": s(60, 20)}


# ── Melodies ──────────────────────────────────────────────────────────────────
def unpack(b):
    return {"p": b & 7, "o": ((b >> 3) & 3) + 4, "d": (b >> 5) & 3}


def pack(n):
    return (n["p"] & 7) | (((n["o"] - 4) & 3) << 3) | ((n["d"] & 3) << 5)


def parse_rtttl(text):
    """RTTTL to (bpm, notes, dropped): sharps drop to the note below, octaves fold into 4-6,
    lengths to 1/4..1/32 (dots dropped), the tempo to the nearest of the device's."""
    parts = text.strip().split(":")
    if len(parts) < 3:
        raise ValueError("RTTTL is name:defaults:notes")
    d = {"d": 4, "o": 6, "b": 63}
    for kv in parts[1].split(","):
        if "=" in kv:
            k, v = (x.strip().lower() for x in kv.split("=", 1))
            if k in d and v.isdigit() and int(v):
                d[k] = int(v)
    bpm = min(BPMS, key=lambda b: abs(b - d["b"]))
    notes, dropped = [], 0
    for raw in ":".join(parts[2:]).split(","):
        t = raw.strip().lower()
        if not t:
            continue
        m = re.fullmatch(r"(\d*)([pcdefgab])(#?)(\.?)(\d?)(\.?)", t)
        if not m:
            raise ValueError(f'can\'t read the note "{raw.strip()}"')
        if len(notes) >= MAX_NOTES:
            dropped += 1
            continue
        ln = int(m[1]) if m[1] else d["d"]
        dur = 0 if ln <= 4 else 1 if ln <= 8 else 2 if ln <= 16 else 3
        octv = max(OCT_MIN, min(OCT_MAX, int(m[5]) if m[5] else d["o"]))
        notes.append({"p": LETTERS.index(m[2]), "o": octv, "d": dur})
    return bpm, notes, dropped


def to_rtttl(bpm, notes):
    return f"Solo:d=8,o=5,b={bpm}:" + ",".join(
        f"{4 << n['d']}{LETTERS[n['p']]}{n['o'] if n['p'] else ''}" for n in notes)


def get_melody(cli, slot):
    bpm, _, hexs = cli.get(f"get melody.{slot}").partition(" ")
    return int(bpm), [unpack(int(hexs[i:i + 2], 16)) for i in range(0, len(hexs) - 1, 2)]


def hex_notes(notes):
    return "".join(f"{pack(n):02x}" for n in notes)


# ── Settings ──────────────────────────────────────────────────────────────────
def list_keys(cli):
    keys = []
    for page in range(1, 20):
        r = cli.run("keys" if page == 1 else f"keys {page}")
        if re.match(r"(?i)no more keys|error|unknown", r):
            break
        more = re.search(r"\(more: keys \d+\)\s*$", r)
        keys += re.sub(r"\s*\(more:.*$", "", r).split()
        if not more:
            break
    return keys


def read_settings(cli, progress):
    out = {}
    keys = list_keys(cli)
    for i, k in enumerate(keys):
        progress(f"settings {i + 1}/{len(keys)}")
        r = cli.run(f"opts {k}")
        if r.startswith("> "):
            out[k] = r[2:].split("|")[2]
        elif re.search(r"(?i)unknown", r):   # before 2.3: no opts, ask each value
            try:
                out[k] = cli.get(f"get {k}")
            except DeviceError:
                pass
    return out


def get_waypoints(cli):
    """The device's waypoints and how many it holds."""
    n, cap = (int(x) for x in cli.get("wpts").split("/"))
    out = []
    for i in range(1, n + 1):
        m = re.match(r"(-?[\d.]+),(-?[\d.]+) ?(.*)$", cli.get(f"get wpt.{i}"))
        if m:
            out.append({"lat": float(m[1]), "lon": float(m[2]), "name": m[3]})
    return out, cap


def fit_label(name):
    b = re.sub(r"\s+", " ", name or "").strip().encode()[:WPT_LABEL]
    return b.decode(errors="ignore").strip()   # a letter cut in two is dropped


def add_waypoints(cli, wpts, report):
    """Append those not on the device yet (the same place, to a metre); counts (added, there, full)."""
    have, cap = get_waypoints(cli)
    added = there = full = 0
    for w in wpts:
        if any(abs(h["lat"] - w["lat"]) < 1e-5 and abs(h["lon"] - w["lon"]) < 1e-5 for h in have):
            there += 1
            continue
        if len(have) >= cap:
            full += 1
            continue
        name = fit_label(w.get("name")) or f"WP{len(have) + 1}"
        try:
            cli.put(f"set wpt.{len(have) + 1} {w['lat']:.6f},{w['lon']:.6f} {name}")
            have.append({"lat": w["lat"], "lon": w["lon"], "name": name})
            added += 1
        except DeviceError as e:
            report(f"  {name}: {e}")
    return added, there, full, cap


def wpt_summary(lead, added, there, full, cap):
    return (f"{lead} {added}" + (f", {there} already there" if there else "")
            + (f", {full} didn't fit (the device holds {cap})" if full else "") + ".")


def parse_pages(r):
    shown, _, hidden = r.partition("| hidden:")
    names = lambda s: [x.strip() for x in s.split(",") if x.strip()]
    return names(shown), names(hidden)


# ── Commands ──────────────────────────────────────────────────────────────────
def progress(text):
    print(f"\r  {text}...".ljust(40), end="", file=sys.stderr, flush=True)


def cmd_export(args):
    dev, info = connect(args.port)
    with dev:
        cli = Cli(dev)
        out = {"format": FORMAT, "version": 1, "saved": datetime.datetime.now().astimezone().isoformat(timespec="seconds"),
               "device": info}
        progress("name and radio")
        try:
            tz = int(float(cli.get("get tz.offset")))
        except (DeviceError, ValueError):
            tz = 0
        out["identity"] = {"name": cli.get("get name"), "tz": tz}
        try:
            pin = cli.get("get pin")
            if re.fullmatch(r"\d{6}", pin):
                out["identity"]["pin"] = pin
        except DeviceError:
            pass
        freq, bw, sf, cr = (float(x) for x in cli.get("get radio").split(","))
        out["radio"] = {"freq": freq, "bw": bw, "sf": int(sf), "cr": int(cr), "tx": int(cli.get("get tx"))}
        out["settings"] = read_settings(cli, progress)
        try:
            out["home"] = {"pages": parse_pages(cli.get("pages"))[0]}
        except DeviceError:
            pass   # a color screen: Home is set on the device
        progress("quick messages")
        try:
            out["messages"] = [cli.get(f"get msg.{i}") for i in range(1, 11)]
        except DeviceError:
            pass
        progress("melodies")
        try:
            out["melodies"] = {}
            for s in (1, 2):
                bpm, notes = get_melody(cli, s)
                out["melodies"][str(s)] = {"bpm": bpm, "notes": hex_notes(notes)}
        except DeviceError:
            del out["melodies"]
        progress("waypoints")
        try:
            wp, _ = get_waypoints(cli)
            if wp:
                out["waypoints"] = wp
        except DeviceError:
            pass
    print("\r".ljust(40), end="\r", file=sys.stderr)
    name = args.file or "solo-config_{}_{}.json".format(
        re.sub(r"[^\w.-]+", "_", out["identity"]["name"] or "device")[:24], datetime.date.today().isoformat())
    Path(name).write_text(json.dumps(out, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    parts = [p for p in PARTS if p in out]
    print(f"Saved {name}: {', '.join(parts)} ({len(out['settings'])} settings).")


def cmd_import(args):
    try:
        d = json.loads(Path(args.file).read_text(encoding="utf-8"))
    except (OSError, ValueError) as e:
        sys.exit(f"Can't read {args.file}: {e}")
    if d.get("format") != FORMAT:
        sys.exit(f"{args.file} isn't a Solo configurator backup.")
    want = args.only.split(",") if args.only else DEFAULT_PARTS + (["identity"] if args.identity else [])
    bad_part = [p for p in want if p not in PARTS]
    if bad_part:
        sys.exit(f"No part {', '.join(bad_part)}: parts are {', '.join(PARTS)}")
    cmds = []   # (command, label)
    if "identity" in want and d.get("identity"):
        i = d["identity"]
        if i.get("name"):
            cmds.append((f"set name {i['name']}", None))
        if i.get("pin"):
            cmds.append((f"set pin {i['pin']}", "set pin"))
        if "tz" in i:
            cmds.append((f"set tz.offset {i['tz']}", None))
    if "radio" in want and d.get("radio"):
        r = d["radio"]
        cmds.append((f"set radio {r['freq']:g},{r['bw']:g},{r['sf']},{r['cr']}", None))
        if "tx" in r:
            cmds.append((f"set tx {r['tx']}", None))
    if "settings" in want:
        cmds += [(f"set {k} {v}", None) for k, v in (d.get("settings") or {}).items()]
    if "home" in want and d.get("home"):
        cmds.append(("set pages " + ", ".join(d["home"]["pages"]), None))
    if "messages" in want:
        for i, m in enumerate((d.get("messages") or [])[:10], 1):
            cmds.append((f"set msg.{i} {m}" if m else f"set msg.{i}", f"set msg.{i}"))
    if "melodies" in want:
        for s, m in (d.get("melodies") or {}).items():
            cmds.append((f"set melody.{s} {m['bpm']} {m.get('notes', '')}".strip(), f"set melody.{s}"))
    wpts = (d.get("waypoints") or []) if "waypoints" in want else []
    if not cmds and not wpts:
        sys.exit("Nothing to put back.")
    src = d.get("identity", {}).get("name") or "a device"
    print(f"{args.file}: from {src}, saved {str(d.get('saved', ''))[:10]}; {len(cmds)} commands"
          + (f", {len(wpts)} waypoints." if wpts else "."))
    if args.dry_run:
        for c, label in cmds:
            print("  " + (label or c))
        for w in wpts:
            print(f"  waypoint {w.get('name', '')} (if not there yet)")
        return
    dev, _ = connect(args.port)
    ok = bad = 0
    with dev:
        cli = Cli(dev)
        for c, label in cmds:
            try:
                cli.put(c)
                ok += 1
            except DeviceError as e:
                bad += 1
                print(f"  {label or c}: {e}")
        if wpts:
            try:
                print(wpt_summary("Waypoints added:", *add_waypoints(cli, wpts, print)))
            except DeviceError as e:
                print(f"  waypoints: {e}")
    print(f"Done: {ok} set" + (f", {bad} not (above)." if bad else "."))


def cmd_melody(args):
    if args.slot not in (1, 2):
        sys.exit("Melody 1 or 2.")
    dev, _ = connect(args.port)
    with dev:
        cli = Cli(dev)
        try:
            if not args.rtttl:
                bpm, notes = get_melody(cli, args.slot)
                print(to_rtttl(bpm, notes) if notes else f"(melody {args.slot} is empty)")
                return
            text = args.rtttl
            if ":" not in text and Path(text).is_file():
                text = Path(text).read_text(encoding="utf-8").strip().splitlines()[0]
            try:
                bpm, notes, dropped = parse_rtttl(text)
            except ValueError as e:
                sys.exit(f"RTTTL: {e}")
            cli.put(f"set melody.{args.slot} {bpm} {hex_notes(notes)}".strip())
            print(f"Melody {args.slot}: {len(notes)} notes, {bpm} BPM" + (f"; {dropped} more left out (at most {MAX_NOTES})." if dropped else "."))
        except DeviceError as e:
            sys.exit(f"{e} (needs Solo 2.3 or newer)")


def cmd_pages(args):
    dev, _ = connect(args.port)
    with dev:
        cli = Cli(dev)
        try:
            if args.names:
                cli.put("set pages " + ", ".join(args.names))
            shown, hidden = parse_pages(cli.get("pages"))
        except DeviceError as e:
            if re.search(r"(?i)unknown", str(e)):
                sys.exit("This device sets its Home pages on the device itself.")
            sys.exit(str(e))
    print("Home: " + ", ".join(shown) + (f"\nHidden: {', '.join(hidden)}" if hidden else ""))


def gpx_waypoints(path):
    try:
        root = ET.parse(path).getroot()
    except (OSError, ET.ParseError) as e:
        sys.exit(f"Can't read {path}: {e}")
    out = []
    for el in root.iter():
        if el.tag.rsplit("}", 1)[-1] != "wpt":
            continue
        name = next((c.text or "" for c in el if c.tag.rsplit("}", 1)[-1] == "name"), "")
        try:
            out.append({"lat": float(el.get("lat")), "lon": float(el.get("lon")), "name": name.strip()})
        except (TypeError, ValueError):
            continue   # a <wpt> without a usable lat/lon
    return out


def cmd_waypoints(args):
    wpts = gpx_waypoints(args.gpx) if args.gpx else None
    if wpts is not None and not wpts:
        sys.exit(f"{args.gpx} has no waypoints.")
    dev, _ = connect(args.port)
    with dev:
        cli = Cli(dev)
        try:
            if args.delete:
                cli.put(f"set wpt.{args.delete}")
                print(f"Deleted waypoint {args.delete}.")
            if wpts:
                print(wpt_summary("Added", *add_waypoints(cli, wpts, print)))
            have, cap = get_waypoints(cli)
        except DeviceError as e:
            sys.exit(f"{e} (needs Solo 2.3 or newer)")
    print(f"Waypoints {len(have)}/{cap}:")
    for i, w in enumerate(have, 1):
        print(f"  {i:2}  {w['lat']:.5f},{w['lon']:.5f}  {w['name']}")


def main():
    ap = argparse.ArgumentParser(description="The device's configuration over USB")
    ap.add_argument("--port", "-p", help="serial port (default: auto-detect)")
    sub = ap.add_subparsers(dest="cmd", required=True)
    e = sub.add_parser("export", help="save a backup to a JSON file")
    e.add_argument("file", nargs="?")
    i = sub.add_parser("import", help="load a backup onto the device")
    i.add_argument("file")
    i.add_argument("--only", help=f"comma-separated parts: {', '.join(PARTS)}")
    i.add_argument("--identity", action="store_true", help="also the name, Bluetooth PIN and time zone")
    i.add_argument("--dry-run", action="store_true", help="list the commands, send nothing")
    m = sub.add_parser("melody", help="show a melody as RTTTL, or set it from RTTTL")
    m.add_argument("slot", type=int)
    m.add_argument("rtttl", nargs="?", help="RTTTL text, or a file holding it")
    p = sub.add_parser("pages", help="show Home's pages, or set their order (the rest hidden)")
    p.add_argument("names", nargs="*")
    w = sub.add_parser("waypoints", help="list the waypoints, add a GPX's, or delete one")
    w.add_argument("gpx", nargs="?", help="a GPX whose waypoints to add")
    w.add_argument("--delete", type=int, metavar="N", help="delete waypoint N (from 1)")
    args = ap.parse_args()
    {"export": cmd_export, "import": cmd_import, "melody": cmd_melody, "pages": cmd_pages,
     "waypoints": cmd_waypoints}[args.cmd](args)


if __name__ == "__main__":
    main()
