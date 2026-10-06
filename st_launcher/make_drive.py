#!/usr/bin/env python3
"""Assemble an Atari C: drive with every game port and the ST launcher.

    python3 make_drive.py [--out build/drive] [--no-build] [--run [--machine ste]]

Reads examples/games.ini (the host launcher's game list) and writes:

    <out>/LAUNCHER.PRG        the menu (examples/st_launcher)
    <out>/GAMES.INI           the games, in the launcher's format
    <out>/GAMES/<NAME>/...    each game's build/ files (program + data)
    <out>/GAMES/<NAME>/THUMB.DAT  160x100 thumbnail of its screenshot

--run starts Hatari on that drive (STE, EmuTOS 1024k, 4 MB) with the
launcher autostarted; --machine falcon boots a Falcon030 instead, where
the Falcon games can be started (the ST/STE ones are then greyed out).

Needs Pillow for the thumbnails (games are listed without one otherwise).
"""
import argparse
import os
import shutil
import subprocess
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.abspath(os.path.join(HERE, "..", ".."))
sys.path.insert(0, os.path.join(REPO, "tools", "games"))
from launcher import Launcher, DEFAULT_INI, RUN_SCRIPT  # noqa: E402

THUMB_W, THUMB_H = 160, 100


def ste_colour(r, g, b):
    """8-bit RGB -> STE palette word (4 bits per gun, LSB in bit 3)."""
    def gun(v):
        v = v >> 4
        return (v >> 1) | ((v & 1) << 3)
    return (gun(r) << 8) | (gun(g) << 4) | gun(b)


def pick8(im):
    """8 of the screenshot's own colours (the game's palette): the most
    frequent first, then greedily by count x distance to those chosen,
    which keeps small bright accents (sprites, text)"""
    cols = sorted(im.getcolors(1 << 20), reverse=True)
    if len(cols) <= 8:
        return [c for _, c in cols]
    chosen = [cols[0][1]]

    def d2(a, b):
        return sum((x - y) ** 2 for x, y in zip(a, b))
    while len(chosen) < 8:
        best = max(cols, key=lambda nc: nc[0] ** 0.5 * min(d2(nc[1], c) for c in chosen))
        chosen.append(best[1])
    return chosen


def thumbnail(png, out):
    """160x100, 8 colours on pens 8-15, ST low resolution planes."""
    from PIL import Image
    im = Image.open(png).convert("RGB")
    # 2x captures: ST 640x400, Falcon 640x480 (crop to 16:10 first)
    w, h = im.size
    if h * 16 > w * 10:
        nh = w * 10 // 16
        im = im.crop((0, (h - nh) // 2, w, (h - nh) // 2 + nh))
    colours = pick8(im)
    pal_img = Image.new("P", (1, 1))
    pal_img.putpalette([v for c in colours for v in c] + [0] * (3 * (256 - len(colours))))
    q = im.resize((THUMB_W, THUMB_H), Image.BOX).quantize(palette=pal_img,
                                                          dither=Image.Dither.NONE)
    pal = q.getpalette()[:24] + [0] * 24
    words = [ste_colour(*pal[i * 3:i * 3 + 3]) for i in range(8)]
    px = q.load()
    for y in range(THUMB_H):
        for gx in range(THUMB_W // 16):
            planes = [0, 0, 0, 0]
            for b in range(16):
                pen = 8 + px[gx * 16 + b, y]
                for p in range(4):
                    if pen & (1 << p):
                        planes[p] |= 0x8000 >> b
            words += planes
    with open(out, "wb") as f:
        f.write(b"".join(w.to_bytes(2, "big") for w in words))


def ascii(s):
    return s.encode("ascii", "replace").decode().replace("?", "-") if s else ""


def wrap(text, width=40):
    """the INI's lines, each wrapped to the 40 column screen"""
    out = []
    for para in text.splitlines():
        line = ""
        for word in ascii(para).split():
            if line and len(line) + 1 + len(word) > width:
                out.append(line)
                line = word
            else:
                line = (line + " " + word) if line else word
        if line:
            out.append(line)
    return out


def main():
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--ini", default=DEFAULT_INI)
    ap.add_argument("--out", default=os.path.join(HERE, "build", "drive"))
    ap.add_argument("--no-build", action="store_true", help="don't run make")
    ap.add_argument("--run", action="store_true", help="start Hatari on the drive")
    ap.add_argument("--machine", default="ste", help="ste (default), megaste or falcon")
    ap.add_argument("--no-sound", action="store_true")
    args = ap.parse_args()

    lch = Launcher(args.ini)
    build = not args.no_build
    if build:
        p = subprocess.run(["make", "CROSS=" + lch.cross], cwd=HERE)
        if p.returncode:
            sys.exit("launcher build failed")
    out = args.out
    if os.path.isdir(out):
        shutil.rmtree(out)
    os.makedirs(os.path.join(out, "GAMES"))
    shutil.copy(os.path.join(HERE, "build", "LAUNCHER.PRG"), out)

    ini = ["; written by examples/st_launcher/make_drive.py from examples/games.ini", ""]
    for g in lch.games:
        if build:
            lch.make(g)
        src = os.path.join(g.build_dir, g.program)
        if not os.path.exists(src):
            print("skipping %s: %s not built" % (g.id, src))
            continue
        folder = os.path.splitext(g.program)[0][:8]
        dst = os.path.join(out, "GAMES", folder)
        os.makedirs(dst)
        for f in os.listdir(g.build_dir):
            p = os.path.join(g.build_dir, f)
            if os.path.isfile(p) and not f.endswith(".o"):
                shutil.copy(p, os.path.join(dst, f.upper()))
        if g.screenshot and os.path.exists(g.screenshot):
            try:
                thumbnail(g.screenshot, os.path.join(dst, "THUMB.DAT"))
            except ImportError:
                print("no Pillow: no thumbnails")
        ini += ["[%s]" % g.id, "title=" + ascii(g.title), "folder=" + folder,
                "program=" + g.program, "machine=" + g.machine]
        ini += ["desc=" + l for l in wrap(" ".join(g.description.split()))]
        ini += ["ctrl=" + l for l in wrap(g.controls)]
        ini.append("")
        print("%-20s -> GAMES\\%s\\%s" % (g.title, folder, g.program))
    with open(os.path.join(out, "GAMES.INI"), "w", newline="\r\n") as f:
        f.write("\n".join(ini))
    print("drive: %s" % out)

    if args.run:
        if not lch.stop():
            sys.exit("could not stop the running Hatari")
        falcon = args.machine == "falcon"
        cmd = [RUN_SCRIPT, "--machine", args.machine]
        cmd += ["--dsp", "none", "--memsize", "14", "--monitor", "vga"] if falcon \
            else ["--memsize", "4"]
        cmd += ["--natfeats", "on", "--harddrive", out, "--auto", "C:\\LAUNCHER.PRG"]
        if args.no_sound or not lch.sound:
            cmd += ["--sound", "off"]
        tos = os.path.join(REPO, "roms/emutos/emutos-1024k-1.4/etos1024k.img")
        env = dict(os.environ, AGENT_TOS=tos, AGENT_PORT=str(lch.port))
        sys.exit(subprocess.run(cmd, env=env).returncode)


if __name__ == "__main__":
    main()
