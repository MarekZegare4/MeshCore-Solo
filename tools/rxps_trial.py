#!/usr/bin/env python3
"""
RX power save on the air: device A sends direct messages, device B receives
them with power save in a given mode, and the arrivals are counted. Both must
be test builds (hwtest.py --flash <env>, on each) on the same radio settings;
see docs/solo/developer/rx-power-save.md for what it showed.

    uv run tools/rxps_trial.py <A port> <B port> [N] [cases]

`cases` is a comma-separated list of <B's mode>:<A's preamble>, by default
0:32,1:16,1:32,2:32,2:16 -- mode 0 off, 1 On, 2 Max; preamble 32 is what
firmware from companion v1.16 sends at SF8 and below, 16 what older firmware
sends (and everyone at SF9 and above). 2:16 is the case Max gives up, so it
should lose messages: proof the trial can see a loss at all.

Each case sends N (default 15) messages a second or so apart and waits up to
6 s for each on B. B's `rxps` before and after: "<set>,<armed>,<asleep %>,
<watchdog soft>,<hard>,<packets received>" -- the received count rising by
exactly N means no repeater echoed a message (which could hide a loss).
"""

import json
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import hwtest as H


def main():
    if len(sys.argv) < 3:
        sys.exit(__doc__)
    a_port, b_port = sys.argv[1], sys.argv[2]
    n = int(sys.argv[3]) if len(sys.argv) > 3 else 15
    cases = [c.split(":") for c in (sys.argv[4] if len(sys.argv) > 4 else "0:32,1:16,1:32,2:32,2:16").split(",")]

    a, b = H.Dut(a_port), H.Dut(b_port)
    for x in (a, b):
        if not x.hooks:
            sys.exit(f"{x.board} isn't a test build")
    ra, rb = a.cli.get("get radio"), b.cli.get("get radio")
    if ra != rb:
        sys.exit(f"different radio settings: {ra} / {rb}")
    print("radio", ra)
    ka, kb = H.pubkey(a), H.pubkey(b)
    added = []
    for x, y, ky in ((a, b, kb), (b, a, ka)):   # each other's contact
        if not H.knows_contact(x, ky):
            adv = y.dev.ask(bytes([H.CMD_EXPORT_CONTACT]), H.RESP_EXPORT_CONTACT)[1:]
            x.dev.ask(bytes([H.CMD_IMPORT_CONTACT]) + adv, 0)
            added.append((x, ky))
    results = []
    try:
        for mode, pre in cases:
            b.cli.put(f"rxps {mode}")
            a.cli.put(f"txpre {pre}")
            time.sleep(1.5)
            before = b.cli.get("rxps")
            got = 0
            for i in range(n):
                text = f"ps{mode}p{pre} {i} {int(time.time()) % 100000}"
                H.send_dm(a, kb, text)
                if H.wait_dm(b, text, 6):
                    got += 1
                time.sleep(1.0)
            r = dict(mode=int(mode), tx_preamble=int(pre), got=got, of=n, before=before, after=b.cli.get("rxps"))
            print(r, flush=True)
            results.append(r)
    finally:
        b.cli.put("rxps 0")   # the saved setting comes back at the next restart anyway
        a.cli.put(f"set radio {ra}")   # the preamble back to what the settings give
        for x, key in added:
            try:
                x.dev.ask(bytes([H.CMD_REMOVE_CONTACT]) + key, 0)
            except H.DeviceError:
                pass
        a.close()
        b.close()
    print(json.dumps(results))


if __name__ == "__main__":
    main()
