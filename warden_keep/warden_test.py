#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Warden Keep (Falcon port) test through the Hatari agent API.

Resets the emulator (the game autostarts), plays the room through (the
three jewels) with key presses, checks the WARDEN state lines on
/console (NatFeats) and saves screenshots.

    make run            # in another shell (HATARI_API if not :7777)
    ./warden_test.py [outdir]
"""
import json
import os
import re
import sys
import urllib.parse
import urllib.request

API = os.environ.get("HATARI_API", "http://127.0.0.1:7777")
OUT = sys.argv[1] if len(sys.argv) > 1 else "test_shots"
failures = []


def call(path, method="GET", **params):
    url = API + path + ("?" + urllib.parse.urlencode(params) if params else "")
    with urllib.request.urlopen(urllib.request.Request(url, method=method), timeout=120) as r:
        data = r.read()
    return json.loads(data) if data[:1] == b"{" else data


def run(frames):
    call("/emu/run", "POST", frames=frames, pause=1)


def press(key, hold=4, after=16):
    call("/input/key", "POST", key=key, action="down")
    run(hold)
    call("/input/key", "POST", key=key, action="up")
    run(after)


def console():
    return call("/console")["text"]


def wait_for(text, frames=600, since=0):
    for _ in range(frames // 20):
        if text in console()[since:]:
            return True
        run(20)
    return text in console()[since:]


def shot(name):
    """Screenshot at 2x (nearest neighbour) when PIL is there, else 1x."""
    os.makedirs(OUT, exist_ok=True)
    path = os.path.join(OUT, name + ".png")
    with open(path, "wb") as f:
        f.write(call("/screen"))
    try:
        from PIL import Image
        img = Image.open(path)
        img.resize((img.width * 2, img.height * 2), Image.NEAREST).save(path)
    except ImportError:
        pass


def check(label, ok):
    print(("  PASS " if ok else "  FAIL ") + label)
    if not ok:
        failures.append(label)


# cold reset: Hatari autostarts the game again (make run set it up)
boot = len(console())
call("/emu/reset", "POST", type="cold")
check("game starts (state=TITLE)", wait_for("WARDEN I state=TITLE", 1500, boot))
run(60)
shot("title")
mark = len(console())
press("space")
check("Space starts (state=PLAYING)", wait_for("state=PLAYING", 120, mark))
# player starts at (3,6); jewels at (2,6), (6,6), (6,3)
for key, n in [("left", 1), ("right", 4), ("up", 3)]:
    for _ in range(n):
        press(key)
    if key == "right":
        shot("play")
check("three jewels -> state=WIN", wait_for("state=WIN", 120, mark))
run(30)
shot("win")
perf = re.findall(r"WARDEN I PERF frame=\d+ fps=([\d.]+) mode=(\d)", console())
if perf:
    print("  INFO fps (last PERF lines):", ", ".join(f"{f} (mode {m})" for f, m in perf[-3:]))
mark = len(console())
call("/input/key", "POST", key="esc", action="down")
run(10)
call("/input/key", "POST", key="esc", action="up")
check("Esc quits", wait_for("shutting down", 120, mark))
print("FAIL" if failures else "PASS", f"({len(failures)} failure(s))")
sys.exit(1 if failures else 0)
