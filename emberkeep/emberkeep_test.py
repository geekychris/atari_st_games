#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Emberkeep (Falcon port) test through the Hatari agent API.

Resets the emulator (the game autostarts), starts the recorded run
through level 1 from the title (F1), checks that it clears the level
(the EMBERKEEP state lines on /console, NatFeats), plays on with key
presses and saves screenshots.

    make run            # in another shell (HATARI_API if not :7777)
    ./emberkeep_test.py [outdir]
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
check("game starts (state=TITLE)", wait_for("EMBERKEEP I state=TITLE", 1500, boot))
run(60)
shot("title")
mark = len(console())
press("f1")
check("F1 starts the demo", wait_for("demo: start", 120, mark))
run(150)
shot("demo")
check("demo clears level 1 (state=LEVELWIN)", wait_for("state=LEVELWIN", 1500, mark))
check("demo hands over on level 2", wait_for("demo: done, level=2", 300, mark))
check("level 2 starts", wait_for("state=PLAYING level=2", 300, mark))
run(30)
shot("level2")
for key, frames in [("down", 50), ("right", 50), ("up", 50), ("left", 50)]:
    call("/input/key", "POST", key=key, action="down")
    run(frames)
    call("/input/key", "POST", key=key, action="up")
    run(4)
shot("play")
perf = re.findall(r"EMBERKEEP I PERF frame=\d+ fps=([\d.]+) mode=(\d)", console()[mark:])
if perf:
    print("  INFO fps (PERF lines):", ", ".join(f"{f} (mode {m})" for f, m in perf[-4:]))
check("still running (PERF lines)", len(perf) >= 2)
mark = len(console())
call("/input/key", "POST", key="esc", action="down")
run(10)
call("/input/key", "POST", key="esc", action="up")
check("Esc quits", wait_for("shutting down", 120, mark))
print("FAIL" if failures else "PASS", f"({len(failures)} failure(s))")
sys.exit(1 if failures else 0)
