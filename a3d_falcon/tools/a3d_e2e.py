#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""End-to-end check of an amiga3d game on the Falcon in Hatari.

    a3d_e2e.py GAME [--no-060] [--shots]

From the game's directory's build/ (make first):
  1. a stock Falcon030 (16 MHz, 14 MB): the title (with its demo) and a
     screenshot; the start key leaves the title; each of the game's keys,
     pressed through the agent API, reaches the game (its in_keys variable
     gets the key's input bit: the IKBD, the ST -> Amiga key map and the
     game's own map); Esc goes back to the title; then a game with the
     autopilot, a screenshot and its frame rate;
  2. an accelerated Falcon (68060 at 32 MHz, TT-RAM, as games.ini's
     rolling_steel): the same game with the autopilot, its frame rate.
--shots saves docs/title.png and docs/play.png in the game's directory.
Prints a summary and exits 1 if a check failed.
"""
import os
import re
import sys
import time

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)
import a3d_agent as ag  # noqa: E402

ACCEL = ["--cpulevel", "6", "--cpuclock", "32", "--addr24", "off", "--ttram", "64"]

# prg; the hooks that start a game and turn its autopilot on; the key that
# leaves the title; the keys to try in play (agent key name, input bit name)
STD_KEYS = [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
            ("a", "IN_LEFT"), ("d", "IN_RIGHT"), ("w", "IN_UP"), ("s", "IN_DOWN"),
            ("space", "IN_FIRE"), ("p", "IN_PAUSE"), ("b", "IN_AUTO"), ("h", "IN_HELP")]
GAMES = {
    "block_breaker_3d": ("BBREAK3D", ["start 1"], ["autoplay on"], "space", STD_KEYS),
    "marble_tilt": ("MARBLE", ["start"], ["autoplay on"], "space", STD_KEYS),
    "harbour_crane": ("HARBOUR", ["start"], ["autoplay on"], "space", STD_KEYS),
    "tank_duel": ("TANKDUEL", ["start"], ["autoplay on"], "space", STD_KEYS),
    "orbit_docking": ("ORBITDCK", ["start"], ["autoplay on"], "space",
                      [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                       ("w", "IN_FWD"), ("s", "IN_BACK"), ("a", "IN_ROLL_L"), ("d", "IN_ROLL_R"),
                       ("space", "IN_FIRE"), ("p", "IN_PAUSE"), ("b", "IN_AUTO"), ("h", "IN_HELP")]),
    "stack_tower": ("STACKTWR", ["start"], [], "space",
                    [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                     ("x", "IN_UP"), ("z", "IN_CCW"), ("c", "IN_HOLD"), ("lshift", "IN_HOLD"),
                     ("g", "IN_GHOST"), ("space", "IN_FIRE"), ("p", "IN_PAUSE"), ("h", "IN_HELP")]),
    "ridge_runner": ("RIDGERUN", ["start"], ["autoplay on"], "space",
                     STD_KEYS + [("lshift", "IN_BOOST"), ("rshift", "IN_BOOST")]),
    "drone_courier": ("DRONE", ["start"], ["autoplay on"], "space", STD_KEYS),
    "canyon_rail": ("CANYRAIL", ["start"], ["autoplay on"], "space", STD_KEYS),
    "lander_3d": ("LANDER3D", ["start moon"], ["autopilot on"], "space",
                  [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                   ("space", "IN_FIRE"), ("j", "IN_FIRE"), ("e", "IN_THR_UP"), ("q", "IN_THR_DN"),
                   ("p", "IN_PAUSE"), ("h", "IN_HELP")]),
    "boing_pinball": ("BOINGPIN", ["start comet"], ["bot on"], "f1",
                      [("lshift", "IN_LFLIP"), ("z", "IN_LFLIP"), ("alt", "IN_LFLIP"),
                       ("rshift", "IN_RFLIP"), ("/", "IN_RFLIP"), ("space", "IN_PLUNGE"),
                       ("down", "IN_PLUNGE"), ("return", "IN_PLUNGE"), ("left", "IN_NL"),
                       ("right", "IN_NR"), ("up", "IN_NU"), ("p", "IN_PAUSE"), ("h", "IN_HELP")]),
    "scairy_fun": ("SCAIRY", ["start"], ["ap on"], "return",
                   [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                    ("space", "IN_FIRE"), ("alt", "IN_FIRE"), ("v", "IN_VIEW"), ("c", "IN_VIEW"),
                    ("p", "IN_PAUSE"), ("h", "IN_HELP")]),
    "ace_pilot2": ("ACEPILT2", ["start"], ["autoplay on"], "space",
                   [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                    ("space", "IN_FIRE"), ("a", "IN_THR_UP"), ("z", "IN_THR_DN"), ("v", "IN_VIEW"),
                    ("c", "IN_VIEW"), ("i", "IN_INVERT"), ("p", "IN_PAUSE"), ("b", "IN_AUTO"),
                    ("h", "IN_HELP"), ("return", "IN_START"), ("f1", "IN_START")]),
    # its own engine (../terra_strike); no autopilot: the play shots are a
    # scene with tanks ahead, and the firing and lift-off are checked (terra_checks)
    "terra_strike": ("TERRA", ["start"], ["scene tanks"], "space",
                     [("left", "IN_LEFT"), ("right", "IN_RIGHT"), ("up", "IN_UP"), ("down", "IN_DOWN"),
                      ("a", "IN_FAST"), ("z", "IN_SLOW"), ("space", "IN_FIRE"), ("alt", "IN_FIRE"),
                      ("p", "IN_PAUSE"), ("return", "IN_START")]),
}


def terra_checks(res):
    """Terra Strike: up lifts the craft off the pad; held fire destroys the tanks of the "tanks" scene"""
    ag.command("hook start", 60)
    time.sleep(1)
    ag.call("/input/key", "POST", key="up", action="down")
    t = time.time()
    while get("pstate") != 1 and time.time() - t < 15:     # PS_FLYING
        time.sleep(0.5)
    ag.call("/input/key", "POST", key="up", action="up")
    if get("pstate") != 1:
        res["fails"].append("up: the craft didn't lift off")
    ag.command("hook scene tanks", 60)
    score0 = get("score")
    _, since = ag.console()
    ag.call("/input/key", "POST", key="space", action="down")
    t = time.time()
    while get("score") == score0 and time.time() - t < 30:
        time.sleep(1)
    ag.call("/input/key", "POST", key="space", action="up")
    kills = [l for l in ag.console(since)[0].splitlines() if " KILL " in l]
    if get("score") == score0:
        res["fails"].append("fire: no hits in 30 s")
    else:
        res["keys"].append("fire->%s" % (kills[0].split(" I ")[-1] if kills else "score"))
    music = [l for l in ag.console()[0].splitlines() if "music loaded" in l]
    if not music:
        res["fails"].append("no music")


EXTRA = {"terra_strike": terra_checks}


def in_bits(gdir):
    src = "".join(open(os.path.join(gdir, f)).read() for f in sorted(os.listdir(gdir)) if f.endswith((".c", ".h")))
    return {k: int(v, 0) for k, v in re.findall(r"#define\s+(IN_\w+)\s+(0x[0-9A-Fa-f]+|\d+)", src)}


def get(name, timeout=60):
    return int(ag.command("get " + name, timeout).split()[0])


def fps(seconds=11):
    _, since = ag.console()
    time.sleep(seconds)
    text, _ = ag.console(since)
    vals = [float(m) for m in re.findall(r"PERF fps (\d+\.\d)", text)]
    return max(vals) if vals else 0.0, min(vals) if vals else 0.0


def start_game(cfg):
    for h in cfg[1]:
        ag.command("hook " + h, 120)    # Lander 3D makes its level in the hook
    for h in cfg[2]:
        ag.command("hook " + h)


def main(argv):
    game = argv[0]
    shots = "--shots" in argv
    gdir = os.path.join(ag.REPO, game)
    cfg = GAMES[game]
    prg = cfg[0]
    bits = in_bits(gdir)
    out = os.path.join(gdir, "build")
    res = {"game": game, "fails": []}

    ag.start(out, prg, [])
    t = time.time()
    while "MAILBOX" not in ag.console()[0] and time.time() - t < 60:
        time.sleep(0.5)
    t = time.time()                     # the title running (its first PERF line, 5 s in) ...
    while "PERF" not in ag.console()[0] and time.time() - t < 60:
        time.sleep(0.5)
    time.sleep(2)
    if "help=" in ag.command("vars", 60):     # ... and not on a help page (Scairy Fun's title shows them in turn)
        t = time.time()
        while get("help") and time.time() - t < 40:
            time.sleep(1)
    if game == "scairy_fun":            # its title cycles demo / rules from the last time it left play
        ag.command("hook start", 60)
        time.sleep(1.5)
        ag.command("hook title", 60)
        time.sleep(4)
    if shots:
        ag.shot(os.path.join(gdir, "docs", "title.png"))
    # the start key leaves the title
    s0 = get("state")
    ag.key(cfg[3], 4)
    time.sleep(1.5)
    s1 = get("state")
    if s1 == s0:
        res["fails"].append("start key %s: state stayed %d" % (cfg[3], s0))
    # the keys
    ok = []
    has_help = "help=" in ag.command("vars")
    for key, name in cfg[4]:
        if has_help:
            ag.command("set help 0")
        ag.command("set in_keys 0")
        ag.call("/input/key", "POST", key=key, action="down")
        t = time.time()                 # held until the game has seen it (a slow game reads keys ~1 a second)
        while True:
            time.sleep(0.3)
            v = get("in_keys")
            if v & bits[name] or time.time() - t > 5:
                break
        ag.call("/input/key", "POST", key=key, action="up")
        if v & bits[name]:
            ok.append(key)
        else:
            res["fails"].append("key %s: in_keys 0x%x, no %s (0x%x)" % (key, v, name, bits[name]))
        if name in ("IN_PAUSE", "IN_HELP", "IN_AUTO"):     # undo toggles
            ag.key(key, 4)
            time.sleep(0.3)
    res["keys"] = ok
    if has_help:
        ag.command("set help 0")
    for _ in range(3):                  # Esc closes a help page first, then leaves the game
        ag.key("esc", 4)
        time.sleep(1.5)
        s2 = get("state")
        if s2 == s0:
            break
    if s2 != s0:
        res["fails"].append("esc: state %d, not the title's %d" % (s2, s0))
    if game in EXTRA:
        EXTRA[game](res)
    # the autopilot
    start_game(cfg)
    time.sleep(6)
    if shots:
        ag.shot(os.path.join(gdir, "docs", "play.png"))
    res["status"] = ag.command("hook status")
    res["fps_stock"] = fps()
    ag.quit_hatari()

    if "--no-060" not in argv:
        ag.start(out, prg, ACCEL)
        t = time.time()
        while "MAILBOX" not in ag.console()[0] and time.time() - t < 60:
            time.sleep(0.5)
        time.sleep(2)
        start_game(cfg)
        time.sleep(4)
        if shots:
            ag.shot(os.path.join(gdir, "docs", "play_060.png"))
        res["fps_060"] = fps()
        res["status_060"] = ag.command("hook status")
        ag.quit_hatari()

    print("%s: keys ok %s" % (game, " ".join(res["keys"])))
    print("  fps stock (max, min of 5 s) %s, 68060 %s" % (res["fps_stock"], res.get("fps_060")))
    print("  status: %s" % res["status"])
    if res.get("status_060"):
        print("  status 060: %s" % res["status_060"])
    for f in res["fails"]:
        print("  FAIL " + f)
    return 1 if res["fails"] else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
