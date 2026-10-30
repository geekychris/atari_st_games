#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 Chris Collins <chris@hitorro.com>
"""Package the Atari ports for download: one zip per game (a folder with
the program, its DATA folder if it has one, and READ_ME.TXT), and
manifest.json, which the website's /games/ page is made from.

  python3 tools/package_release.py [--out dist/release] [--tag games-YYYY-MM-DD]

Uses the programs already built in each game's build/ (make); a game with
none is listed as not built.
"""
import argparse
import configparser
import datetime
import json
import os
import shutil
import zipfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
REPO = "geekychris/atari_st_games"

MACHINES = {
    "st": ("ST", "Any Atari ST (or STE, Mega ST, Falcon) with TOS or EmuTOS."),
    "ste": ("STE", "An Atari STE (or Mega STE, Falcon): it uses the STE's blitter, DMA sound or hardware scrolling."),
    "megaste": ("Mega STE", "An Atari Mega STE."),
    "tt": ("TT", "An Atari TT030."),
    "falcon": ("Falcon030", "An Atari Falcon030, VGA or RGB monitor."),
}
CT60 = "An Atari Falcon030 with a 68060 accelerator (CT60 or CT63)."


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(ROOT, "dist", "release"))
    ap.add_argument("--tag", default="games-" + datetime.date.today().isoformat())
    opt = ap.parse_args()
    if os.path.exists(opt.out):
        shutil.rmtree(opt.out)
    os.makedirs(opt.out)
    ini = configparser.ConfigParser(interpolation=None)
    ini.read(os.path.join(ROOT, "games.ini"))
    games = []
    for gid in ini.sections():
        if gid == "launcher":
            continue
        s = ini[gid]
        machine = s.get("machine", "st")
        ct60 = "--cpulevel 6" in s.get("options", "")
        label, needs = MACHINES.get(machine, (machine, ""))
        if ct60:
            label, needs = "Falcon + 68060", CT60
        g = {"id": gid, "title": s.get("title", gid), "description": " ".join(s.get("description", "").split()),
             "controls": "; ".join(l.strip() for l in s.get("controls", "").splitlines() if l.strip()), "machine": label, "needs": needs, "packages": {}}
        build = os.path.join(ROOT, s.get("dir", gid), "build")
        prog = os.path.join(build, s.get("program", ""))
        if os.path.isfile(prog):
            folder = os.path.splitext(os.path.basename(prog))[0]
            name = f"{gid}-atari.zip"
            with zipfile.ZipFile(os.path.join(opt.out, name), "w", zipfile.ZIP_DEFLATED) as z:
                z.write(prog, f"{folder}/{os.path.basename(prog)}")
                data = os.path.join(build, "DATA")
                if os.path.isdir(data):
                    for f in sorted(os.listdir(data)):
                        z.write(os.path.join(data, f), f"{folder}/DATA/{f}")
                text = [g["title"], "=" * len(g["title"]), "", g["description"], "",
                        "Controls: " + g["controls"], "", "Needs: " + needs, "",
                        f"To run: copy the {folder} folder to a floppy, a hard disk or an",
                        "emulator's GEMDOS drive (Hatari: --harddrive <folder>) and open",
                        f"{os.path.basename(prog)} from the desktop.", "",
                        f"Source: https://github.com/{REPO}/tree/main/{gid}", "MIT licence."]
                z.writestr(f"{folder}/READ_ME.TXT", "\r\n".join(text) + "\r\n")
            g["packages"]["atari"] = {"file": name, "bytes": os.path.getsize(os.path.join(opt.out, name)),
                                      "url": f"https://github.com/{REPO}/releases/download/{opt.tag}/{name}"}
        games.append(g)
    with open(os.path.join(opt.out, "manifest.json"), "w") as fh:
        json.dump({"repo": REPO, "tag": opt.tag, "platforms": ["atari"], "games": games}, fh, indent=1)
    print(f"{len(games)} games, {sum(1 for g in games if g['packages'])} packaged -> {opt.out}")
    for g in games:
        if not g["packages"]:
            print(f"  {g['id']}: not built")


if __name__ == "__main__":
    main()
