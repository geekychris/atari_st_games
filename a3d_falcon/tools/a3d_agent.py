#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Drive the amiga3d games on a Falcon in Hatari through the agent API.

    a3d_agent.py run DIR PRG [hatari options]   start Hatari, DIR as C:, autostart PRG.PRG
    a3d_agent.py test DIR TPRG [options]        run the test program, fast forward, until
                                                its TESTS line; exit 1 unless all passed
    a3d_agent.py trace DIR [options]            run DIR/TRACE.PRG (make trace) until it is done
    a3d_agent.py cmd 'hook autoplay on'         a command for the game's mailbox (bridge.c):
                                                get NAME, set NAME V, vars, hooks, hook NAME ARGS
    a3d_agent.py shot FILE.png                  screenshot (native 320x240)
    a3d_agent.py key NAME [frames]              press a key (agent API names: space, esc, up...)
    a3d_agent.py hold NAME | release NAME       hold a key down / let it go
    a3d_agent.py perf [seconds]                 wait, then print the game's PERF lines
    a3d_agent.py console                        everything the game logged
    a3d_agent.py quit                           quit Hatari

The port is $A3_PORT (default 7790), so these Hatari instances keep off
the launcher's 7777. Hatari and EmuTOS come from ../hatari_augmented (or
$HATARI_DIR). A3_HEADLESS=1 runs Hatari without a window (SDL dummy).
"""
import json
import os
import re
import struct
import subprocess
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
HATARI_DIR = os.environ.get("HATARI_DIR", os.path.join(REPO, "..", "hatari_augmented"))
PORT = int(os.environ.get("A3_PORT", "7790"))
API = "http://127.0.0.1:%d" % PORT
FALCON = ["--machine", "falcon", "--dsp", "none", "--memsize", "14", "--monitor", "vga", "--natfeats", "on"]
TOS = os.path.join(HATARI_DIR, "roms/emutos/emutos-1024k-1.4/etos1024k.img")


def call(path, method="GET", raw=False, timeout=30, **params):
    url = API + path
    if params:
        url += "?" + urllib.parse.urlencode(params)
    req = urllib.request.Request(url, method=method)
    with urllib.request.urlopen(req, timeout=timeout) as r:
        data = r.read()
    return data if raw else json.loads(data)


def alive():
    try:
        call("/status", timeout=2)
        return True
    except (urllib.error.URLError, OSError, ValueError):
        return False


def quit_hatari():
    if alive():
        try:
            call("/emu/quit", "POST", timeout=5)
        except (urllib.error.URLError, OSError, ValueError):
            pass
        for _ in range(50):
            if not alive():
                break
            time.sleep(0.1)


def start(d, prg, opts, fast=False):
    quit_hatari()
    env = dict(os.environ, AGENT_PORT=str(PORT), AGENT_TOS=TOS,
               AGENT_LOG=os.environ.get("A3_LOG", "/tmp/a3d-hatari-%d.log" % PORT))
    if os.environ.get("A3_HEADLESS"):
        env["SDL_VIDEODRIVER"] = "dummy"
        env["SDL_AUDIODRIVER"] = "dummy"
    args = FALCON + ["--harddrive", os.path.abspath(d), "--auto", "C:\\%s.PRG" % prg] + list(opts)
    if fast:
        args += ["--fast-forward", "on", "--sound", "off"]
    subprocess.run([os.path.join(HATARI_DIR, "tools/agent/hatari-agent-run.sh")] + args, env=env, check=True)


def console(since=0):
    r = call("/console", since=since)
    return r["text"], r["next"]


def mailbox():
    text, _ = console()
    m = re.findall(r"MAILBOX 0x([0-9a-f]+)", text)
    if not m:
        sys.exit("the game hasn't started (no MAILBOX line on /console)")
    return int(m[-1], 16)


def mem(addr, n):
    return bytes.fromhex(call("/mem", addr=addr, len=n)["hex"])


def poke(addr, data):
    call("/mem", "POST", addr=addr, hex=data.hex())


def command(text, timeout=10):
    mb = mailbox()
    seq, done = struct.unpack(">ll", mem(mb + 8, 8))
    poke(mb + 16, text.encode()[:127] + b"\0")
    seq = max(seq, done) + 1
    poke(mb + 8, struct.pack(">l", seq))
    t = time.time()
    while time.time() - t < timeout:
        if struct.unpack(">l", mem(mb + 12, 4))[0] == seq:
            return mem(mb + 144, 512).split(b"\0")[0].decode(errors="replace")
        time.sleep(0.05)
    raise TimeoutError("the game didn't answer: " + text)


def shot(path, scale=2):
    """the screen, scaled 2x (nearest) like the repository's other screenshots if Pillow is there"""
    png = call("/screen", raw=True)
    os.makedirs(os.path.dirname(os.path.abspath(path)), exist_ok=True)
    with open(path, "wb") as f:
        f.write(png)
    try:
        from PIL import Image
        im = Image.open(path)
        im.resize((im.width * scale, im.height * scale), Image.NEAREST).save(path, optimize=True)
    except ImportError:
        pass


def key(name, frames=4):
    call("/input/key", "POST", key=name, frames=frames)


def run_test(d, tprg, opts, timeout=1800):
    start(d, tprg, opts, fast=True)
    t = time.time()
    since = 0
    out = ""
    while time.time() - t < timeout:
        text, since = console(since)
        out += text
        sys.stdout.write(text)
        sys.stdout.flush()
        m = re.search(r"TESTS (\d+) passed, (\d+) failed", out)
        if m:
            quit_hatari()
            return 0 if m.group(2) == "0" and int(m.group(1)) > 0 else 1
        time.sleep(1)
    quit_hatari()
    print("timed out")
    return 1


def main(a):
    if not a:
        sys.exit(__doc__)
    c = a[0]
    if c == "run":
        start(a[1], a[2], a[3:])
        t = time.time()                 # until the game is up (its mailbox)
        while time.time() - t < 60 and "MAILBOX" not in console()[0]:
            time.sleep(0.5)
    elif c == "test":
        return run_test(a[1], a[2], a[3:])
    elif c == "trace":                  # build/TRACE.PRG until TRACE_DONE
        start(a[1], "TRACE", a[2:], fast=True)
        t = time.time()
        while time.time() - t < 1800 and "TRACE_DONE" not in console()[0]:
            time.sleep(1)
        print([l for l in console()[0].splitlines() if "TRACE_DONE" in l] or "timed out")
        quit_hatari()
    elif c == "cmd":
        print(command(" ".join(a[1:])))
    elif c == "shot":
        shot(a[1])
    elif c == "key":
        key(a[1], int(a[2]) if len(a) > 2 else 4)
    elif c == "hold":
        call("/input/key", "POST", key=a[1], action="down")
    elif c == "release":
        call("/input/key", "POST", key=a[1], action="up")
    elif c == "perf":
        _, since = console()
        time.sleep(float(a[1]) if len(a) > 1 else 11)
        text, _ = console(since)
        print("\n".join(l for l in text.splitlines() if "PERF" in l))
    elif c == "console":
        print(console()[0])
    elif c == "quit":
        quit_hatari()
    else:
        sys.exit(__doc__)
    return 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
