#!/usr/bin/env python3
"""Tiny PNG inspector (stdlib only) for screenshot assertions.

  pngtool.py size FILE                 -> "W H"
  pngtool.py pixel FILE X Y            -> "R G B"
  pngtool.py hash FILE [X Y W H]       -> sha1 of the RGB pixels in region
  pngtool.py count FILE R G B X Y W H  -> number of pixels with that color
"""
import hashlib
import struct
import sys
import zlib


def load(path):
    data = open(path, "rb").read()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", "not a PNG"
    pos, idat, palette = 8, b"", None
    while pos < len(data):
        length, ctype = struct.unpack(">I4s", data[pos:pos + 8])
        chunk = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if ctype == b"IHDR":
            w, h, depth, color = struct.unpack(">IIBB", chunk[:10])
        elif ctype == b"PLTE":
            palette = [tuple(chunk[i:i + 3]) for i in range(0, len(chunk), 3)]
        elif ctype == b"IDAT":
            idat += chunk
    assert depth == 8 and color in (2, 3), "only 8-bit RGB/palette PNGs"
    bpp = 3 if color == 2 else 1
    raw = zlib.decompress(idat)
    stride = w * bpp
    rows, prev = [], bytearray(stride)
    for y in range(h):
        ftype = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - bpp] if i >= bpp else 0
            b = prev[i]
            c = prev[i - bpp] if i >= bpp else 0
            if ftype == 1:
                line[i] = (line[i] + a) & 255
            elif ftype == 2:
                line[i] = (line[i] + b) & 255
            elif ftype == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif ftype == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                pred = a if pa <= pb and pa <= pc else b if pb <= pc else c
                line[i] = (line[i] + pred) & 255
        rows.append(line)
        prev = line

    def pixel(x, y):
        if color == 2:
            return tuple(rows[y][x * 3:x * 3 + 3])
        return palette[rows[y][x]]
    return w, h, pixel


def main():
    cmd, path, *args = sys.argv[1:]
    w, h, pixel = load(path)
    nums = [int(a) for a in args]
    if cmd == "size":
        print(w, h)
    elif cmd == "pixel":
        print(*pixel(nums[0], nums[1]))
    elif cmd == "hash":
        x, y, rw, rh = nums if nums else (0, 0, w, h)
        sha = hashlib.sha1()
        for yy in range(y, y + rh):
            for xx in range(x, x + rw):
                sha.update(bytes(pixel(xx, yy)))
        print(sha.hexdigest())
    elif cmd == "count":
        rgb, (x, y, rw, rh) = tuple(nums[:3]), nums[3:]
        print(sum(1 for yy in range(y, y + rh) for xx in range(x, x + rw)
                  if pixel(xx, yy) == rgb))
    else:
        sys.exit("unknown command " + cmd)


if __name__ == "__main__":
    main()
