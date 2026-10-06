#!/usr/bin/env python3
"""Fractalus (Falcon port) scenario test through the Hatari agent API.

Starts a mission, then for each kind of pilot (human, jaggi) teleports
the ship onto the nearest one by writing the ship position into emulated
memory, presses L to land and follows the rescue state machine until it
flies again (moving the ship away from the other pilots at take-off),
saving a screenshot of each state.  Checks that a human
rescue increments pilots_rescued and a jaggi does not.

Addresses come from the "FRACTALUS S" lines the game logs at start-up
(NatFeats -> /console), so no symbol table is needed.

    make run            # in another shell, or: make run &
    ./rescue_test.py [outdir]
"""
import json
import os
import re
import struct
import sys
import time
import urllib.request

API = os.environ.get("HATARI_API", "http://127.0.0.1:7777")
OUT = sys.argv[1] if len(sys.argv) > 1 else "rescue_shots"
STATES = ["FLYING", "LANDING", "AIRLOCK", "REVEAL", "JUMPSCARE", "TAKEOFF"]


def call(path, method="GET"):
    req = urllib.request.Request(API + path, method=method)
    with urllib.request.urlopen(req, timeout=30) as r:
        data = r.read()
    return json.loads(data) if data[:1] == b"{" else data


def mem(addr, n):
    return bytes.fromhex(call("/mem?addr=%d&len=%d" % (addr, n))["hex"])


def poke_long(addr, val):
    call("/mem?addr=%d&hex=%s" % (addr, struct.pack(">l", val).hex()), "POST")


def key(name, frames=6):
    call("/input/key?key=%s&frames=%d" % (name, frames), "POST")


def addrs():
    con = call("/console")["text"]
    found = dict(re.findall(r"FRACTALUS S (\w+) 0x([0-9a-f]+)", con))
    if "g_state" not in found:
        sys.exit("game not running (no FRACTALUS S lines on /console)")
    return {k: int(v, 16) for k, v in found.items()}


def long_at(addr):
    return struct.unpack(">l", mem(addr, 4))[0]


def start_mission(a):
    for _ in range(10):
        if long_at(a["mode"]) == 0:     # GM_PLAYING
            return
        key("space", 8)
        time.sleep(1.5)
    sys.exit("could not start a mission")


def teleport_to(a, want_jaggi):
    gs = a["g_state"]
    sx, _, sz = struct.unpack(">3l", mem(gs, 12))
    best = None
    for i in range(12):
        x, z, y, state, jag = struct.unpack(">3lBB2x", mem(a["pilots"] + 16 * i, 16))
        if state != 0 or bool(jag) != want_jaggi:
            continue
        d = (x - (sx >> 16)) ** 2 + (z - (sz >> 16)) ** 2
        if best is None or d < best[0]:
            best = (d, i, x, z)
    if best is None:
        return None
    _, i, x, z = best
    poke_long(gs, x << 16)              # ship.x (16.16), right on the
    poke_long(gs + 8, z << 16)          # pilot, so it is the nearest one
    return i


def wait_flying(a, timeout=30):
    t0 = time.time()
    while long_at(a["rescue_state"]) != 0:
        if time.time() - t0 > timeout:
            sys.exit("ship never got back to flying")
        time.sleep(0.3)


def run_rescue(a, want_jaggi, tag):
    wait_flying(a)
    before = long_at(a["pilots_rescued"])
    i = teleport_to(a, want_jaggi)
    if i is None:
        print("%s: no active pilot of that kind left" % tag)
        return True
    key("l", 10)
    seen = [0]
    t0 = time.time()
    while time.time() - t0 < 30:
        rs = long_at(a["rescue_state"])
        if seen[-1] != rs:
            seen.append(rs)
            if rs == 5:
                # taking off: move away from the other pilots (they spawn
                # within 1200 units), or the game lands again next to one
                gs = a["g_state"]
                poke_long(gs, long_at(gs) + (4000 << 16))
            png = call("/screen")
            with open(os.path.join(OUT, "%s_%d_%s.png" % (tag, len(seen), STATES[rs].lower())), "wb") as f:
                f.write(png)
        if len(seen) > 1 and rs == 0:
            break           # one landing only (the game auto-lands again
                            # when it is still low and slow near a pilot)
        time.sleep(0.3)
    after = long_at(a["pilots_rescued"])
    names = " -> ".join(STATES[s] for s in seen)
    ok = (after == before + 1) if not want_jaggi else (after == before and 4 in seen)
    print("%s (pilot %d): %s, rescued %d -> %d: %s" %
          (tag, i, names, before, after, "OK" if ok else "FAIL"))
    return ok


def main():
    os.makedirs(OUT, exist_ok=True)
    for f in os.listdir(OUT):
        if f.endswith(".png"):
            os.remove(os.path.join(OUT, f))
    a = addrs()
    start_mission(a)
    time.sleep(2)
    ok = run_rescue(a, False, "human")
    ok = run_rescue(a, True, "jaggi") and ok
    print("screenshots in", OUT)
    sys.exit(0 if ok else 1)


if __name__ == "__main__":
    main()
