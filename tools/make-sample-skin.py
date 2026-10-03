#!/usr/bin/env python3
"""Generate the sample skin (assets/skins/sample/) for the hires renderer: our own procedural art, made
from code alone. It reads no game file; the only knowledge of the game in here is a handful of asset
numbers (which tile and sprite numbers to replace), listed below. See docs/skins.md.

    python3 tools/make-sample-skin.py [output dir]      (default: assets/skins/sample)

What it replaces in style 1 (Liberty City), everything else falls back to the original art:
  lid 1      road: blue-grey asphalt with a dotted cyan edge line (RGB PNG)
  lid 8      pavement: terracotta hexagons (palette PNG with transparency, exercising that decoder path)
  lid 41     water, and aux 25..35, the frames its animation shows: moving green-blue waves (RGBA)
  side 138   a wall: blue bricks with alternating mortar (RGBA)
  sprite 72  the car parked next to mission 1's start: a rounded top-down coupe (RGBA), with
             sprite/72_mask.png marking its paint for remapped copies
The images are larger than the original 64 x 64 tiles (256 x 256): the hires renderer shows them at their
own resolution, and box-filters them down at small scales.
Rows are written with all five PNG filters in turn so the decoder's unfiltering is exercised. Stdlib only.
"""
import math
import os
import struct
import sys
import zlib

TILE = 256


def png(path, w, h, rows, kind="rgba", palette=None, trns=None):
    """rows: list of h lists of pixels: (r, g, b, a) tuples, or palette indices for kind 'pal'."""
    ch = {"rgba": 4, "rgb": 3, "pal": 1}[kind]
    ctype = {"rgba": 6, "rgb": 2, "pal": 3}[kind]
    raw = bytearray()
    prev = bytes(w * ch)
    for y, row in enumerate(rows):
        if kind == "pal":
            cur = bytes(row)
        elif kind == "rgb":
            cur = bytes(c for p in row for c in p[:3])
        else:
            cur = bytes(c for p in row for c in p)
        f = y % 5
        out = bytearray(len(cur))
        for i in range(len(cur)):
            a = cur[i - ch] if i >= ch else 0
            b = prev[i]
            c = prev[i - ch] if i >= ch else 0
            if f == 0:
                v = cur[i]
            elif f == 1:
                v = cur[i] - a
            elif f == 2:
                v = cur[i] - b
            elif f == 3:
                v = cur[i] - ((a + b) >> 1)
            else:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                v = cur[i] - (a if pa <= pb and pa <= pc else b if pb <= pc else c)
            out[i] = v & 0xFF
        raw.append(f)
        raw += out
        prev = cur

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xFFFFFFFF)

    data = b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, ctype, 0, 0, 0))
    if palette:
        data += chunk(b"PLTE", bytes(c for p in palette for c in p))
    if trns:
        data += chunk(b"tRNS", bytes(trns))
    data += chunk(b"IDAT", zlib.compress(bytes(raw), 9)) + chunk(b"IEND", b"")
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, "wb") as f:
        f.write(data)


def noise(x, y, seed):
    """Deterministic value noise in 0..1."""
    n = (x * 374761393 + y * 668265263 + seed * 2147483647) & 0xFFFFFFFF
    n = ((n ^ (n >> 13)) * 1274126177) & 0xFFFFFFFF
    return ((n ^ (n >> 16)) & 0xFFFF) / 65535.0


def clamp(v):
    return max(0, min(255, int(v)))


def road():
    rows = []
    for y in range(TILE):
        row = []
        for x in range(TILE):
            g = 70 + 25 * noise(x, y, 1) + 10 * noise(x // 4, y // 4, 2)
            r, gg, b = g * 0.85, g * 0.9, g * 1.15
            if 18 <= x < 30 and (y // 24) % 2 == 0:   # dotted cyan edge line
                r, gg, b = 40, 200, 210
            row.append((clamp(r), clamp(gg), clamp(b), 255))
        rows.append(row)
    return rows


def pavement():
    palette = [(0, 0, 0)] + [(clamp(150 + 60 * t), clamp(80 + 40 * t), clamp(50 + 20 * t)) for t in
                             (i / 14.0 for i in range(15))] + [(60, 40, 30)]
    rows = []
    size = 32.0
    for y in range(TILE):
        row = []
        for x in range(TILE):
            # hexagon grid: distance to the nearest centre in a offset-row lattice
            q = y / (size * 0.866)
            r0 = int(math.floor(q))
            best = 1e9
            for rr in (r0 - 1, r0, r0 + 1):
                off = (rr % 2) * size / 2
                cx0 = round((x - off) / size) * size + off
                for cx in (cx0 - size, cx0, cx0 + size):
                    cy = rr * size * 0.866
                    d = math.hypot(x - cx, y - cy)
                    best = min(best, d)
            if best > size * 0.47:
                row.append(16)                    # mortar
            else:
                row.append(1 + int(14 * noise(x // 3, y // 3, 5)))
        rows.append(row)
    return palette, rows


def water(phase):
    rows = []
    for y in range(TILE):
        row = []
        for x in range(TILE):
            w = math.sin((x + y) * 2 * math.pi / 64 + phase) + 0.5 * math.sin((x - 2 * y) * 2 * math.pi / 96 - phase)
            r, g, b = 20 + 15 * w, 110 + 35 * w, 120 + 30 * w
            if w > 1.2:
                r, g, b = 200, 240, 230               # crests
            row.append((clamp(r), clamp(g), clamp(b), 255))
        rows.append(row)
    return rows


def bricks():
    rows = []
    bw, bh = 64, 32
    for y in range(TILE):
        row = []
        for x in range(TILE):
            course = y // bh
            xo = (x + (bw // 2 if course % 2 else 0)) % bw
            if y % bh < 3 or xo < 3:
                c = (210, 205, 190, 255) if course % 2 else (170, 170, 160, 255)
            else:
                k = noise((x + (bw // 2 if course % 2 else 0)) // bw, course, 9)
                c = (clamp(40 + 30 * k), clamp(70 + 40 * k), clamp(150 + 60 * k + 20 * noise(x, y, 3)), 255)
            row.append(c)
        rows.append(row)
    return rows


def car(w, h):
    """A top-down coupe facing up (the sprite's top is the car's front), and the mask of its paint."""
    rows, mask = [], []
    for y in range(h):
        row, mrow = [], []
        for x in range(w):
            u, v = (x + 0.5) / w * 2 - 1, (y + 0.5) / h * 2 - 1           # -1..1
            body = (abs(u) / 0.82) ** 4 + (abs(v) / 0.96) ** 4 <= 1
            if not body:
                row.append((0, 0, 0, 0))
                mrow.append((0, 0, 0, 255))
                continue
            shade = 1 - 0.35 * abs(u) ** 2
            glass = (-0.55 < v < -0.2 and abs(u) < 0.62 - 0.3 * (v + 0.55)) or (0.45 < v < 0.68 and abs(u) < 0.55)
            roof = -0.2 <= v <= 0.45 and abs(u) < 0.6
            light = v < -0.88 and abs(u) > 0.45
            if glass:
                c, m = (clamp(60 * shade), clamp(90 * shade), clamp(110 * shade), 255), 0
            elif light:
                c, m = (250, 240, 190, 255), 0
            elif roof:
                c, m = (clamp(230 * shade), clamp(70 * shade), clamp(40 * shade), 255), 255
            else:
                c, m = (clamp(200 * shade), clamp(50 * shade), clamp(30 * shade), 255), 255
            row.append(c)
            mrow.append((m, m, m, 255))
        rows.append(row)
        mask.append(mrow)
    return rows, mask


def main():
    out = sys.argv[1] if len(sys.argv) > 1 else os.path.join(os.path.dirname(__file__), "..", "assets", "skins", "sample")
    s = os.path.join(out, "style001")
    os.makedirs(out, exist_ok=True)
    with open(os.path.join(out, "skin.ini"), "w") as f:
        f.write("; The OpenGTA sample skin: procedural art generated by tools/make-sample-skin.py from code\n"
                "; alone. No game data went into it. See docs/skins.md.\n"
                "name = OpenGTA sample skin\n"
                "author = OpenGTA (tools/make-sample-skin.py)\n"
                "scale = 4\n")
    png(os.path.join(s, "lid", "1.png"), TILE, TILE, road(), "rgb")
    pal, rows = pavement()
    png(os.path.join(s, "lid", "8.png"), TILE, TILE, rows, "pal", pal, [0] + [255] * 16)
    frames = [41] + list(range(25, 36))           # lid 41 and the aux frames of its animation
    for i, n in enumerate(frames):
        folder = "lid" if i == 0 else "aux"
        png(os.path.join(s, folder, "%d.png" % n), TILE, TILE, water(i * 2 * math.pi / len(frames)))
    png(os.path.join(s, "side", "138.png"), TILE, TILE, bricks())
    rows, mask = car(62 * 4, 64 * 4)
    png(os.path.join(s, "sprite", "72.png"), 62 * 4, 64 * 4, rows)
    png(os.path.join(s, "sprite", "72_mask.png"), 62 * 4, 64 * 4, mask)
    print("wrote", os.path.normpath(out))


if __name__ == "__main__":
    main()
