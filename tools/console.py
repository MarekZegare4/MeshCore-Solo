#!/usr/bin/env python3
"""
The device's console over USB: the same commands as the phone app's console
and the Solo site's (get radio, set tx 14, get name, keys, any setting by its
key). Needs Solo 2.0 or newer.

Usage:
    uv run tools/console.py                  # interactive; Ctrl-D or "exit" to leave
    uv run tools/console.py get radio        # one command, then exit
    uv run tools/console.py --port /dev/cu.usbmodem1101 keys
"""

import argparse
import sys

from companion import Companion, DeviceError

CMD_DEVICE_QUERY = 22
RESP_DEVICE_INFO = 13
CMD_RUN_CLI = 66
RESP_CLI_REPLY = 29
CLI_MIN_VER = 14


def run(dev, cmd):
    f = dev.ask(bytes([CMD_RUN_CLI]) + cmd.encode()[:160], RESP_CLI_REPLY)
    return f[1:].decode(errors="replace")


def main():
    ap = argparse.ArgumentParser(description="The device's console over USB")
    ap.add_argument("--port", "-p", help="serial port (default: auto-detect)")
    ap.add_argument("command", nargs="*", help="one command to run (default: interactive)")
    args = ap.parse_args()

    with Companion(args.port) as dev:
        try:
            info = dev.ask(bytes([CMD_DEVICE_QUERY, CLI_MIN_VER]), RESP_DEVICE_INFO)
        except DeviceError as e:
            sys.exit(f"The device didn't answer ({e}). Is the phone app connected over Bluetooth?")
        if info[1] < CLI_MIN_VER:
            sys.exit("This firmware has no console: it needs Solo 2.0 or newer.")

        if args.command:
            try:
                print(run(dev, " ".join(args.command)))
            except DeviceError as e:
                sys.exit(str(e))
            return

        print(f"Connected on {dev.port}. Type a command (keys lists the settings), exit to leave.")
        while True:
            try:
                cmd = input("> ").strip()
            except (EOFError, KeyboardInterrupt):
                print()
                break
            if cmd in ("exit", "quit"):
                break
            if not cmd:
                continue
            try:
                print(run(dev, cmd) or "(no reply)")
            except DeviceError as e:
                print(f"! {e}")


if __name__ == "__main__":
    main()
