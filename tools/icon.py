#!/usr/bin/env python3
"""Draw OpenGTA's app icon as a PNG of any size.

The design is the site's own mark (docs/public/favicon.svg): a top-down arrow in the menu text's
yellow-to-amber gradient, with a soft drop shadow, on a rounded square of rusted dark metal with an orange
rim. It is drawn here from the same geometry (a 64 x 64 canvas) rather than rasterised from the SVG, so the
packaging needs nothing but the Python standard library (CI runners have no SVG renderer or Pillow).
Edges are anti-aliased from signed distances, one evaluation per pixel. No game art is involved.

    tools/icon.py <size> <out.png>
"""
import math
import struct
import sys
import zlib

# The favicon's geometry, in its 64 x 64 viewBox.
RECT = (2.0, 2.0, 62.0, 62.0, 12.0)   # x0 y0 x1 y1 corner radius
RIM = 2.5                             # stroke width, centred on the rectangle's edge
ARROW = [(32, 9), (53, 32), (41, 32), (41, 55), (23, 55), (23, 32), (11, 32)]
SHADOW = (2.0, 2.0, 0.7)              # offset x, y and opacity of the arrow's shadow


def hexrgb(h):
    return tuple(int(h[i:i + 2], 16) / 255.0 for i in (1, 3, 5))


ARROW_STOPS = [(0.0, hexrgb('#ffff9b')), (0.35, hexrgb('#ffff00')), (0.7, hexrgb('#ffcf00')), (1.0, hexrgb('#cf9b00'))]
METAL_STOPS = [(0.0, hexrgb('#7a3a10')), (0.55, hexrgb('#2a1a0c')), (1.0, hexrgb('#120e0a'))]
RIM_RGB = hexrgb('#f08000')
SHADOW_RGB = hexrgb('#131313')


def ramp(stops, t):
    t = max(0.0, min(1.0, t))
    for (t0, c0), (t1, c1) in zip(stops, stops[1:]):
        if t <= t1:
            f = 0.0 if t1 == t0 else (t - t0) / (t1 - t0)
            return tuple(a + (b - a) * f for a, b in zip(c0, c1))
    return stops[-1][1]


def rounded_rect_sd(x, y):
    """Signed distance to the rounded rectangle (negative inside)."""
    x0, y0, x1, y1, r = RECT
    cx, cy = (x0 + x1) / 2, (y0 + y1) / 2
    hx, hy = (x1 - x0) / 2 - r, (y1 - y0) / 2 - r
    qx, qy = abs(x - cx) - hx, abs(y - cy) - hy
    out = math.hypot(max(qx, 0.0), max(qy, 0.0))
    return out + min(max(qx, qy), 0.0) - r


def polygon_sd(poly, x, y):
    """Signed distance to a simple polygon (negative inside), even-odd inside test."""
    d2, inside = float('inf'), False
    n = len(poly)
    for i in range(n):
        ax, ay = poly[i]
        bx, by = poly[(i + 1) % n]
        ex, ey = bx - ax, by - ay
        wx, wy = x - ax, y - ay
        t = max(0.0, min(1.0, (wx * ex + wy * ey) / (ex * ex + ey * ey)))
        dx, dy = wx - ex * t, wy - ey * t
        d2 = min(d2, dx * dx + dy * dy)
        if (ay > y) != (by > y) and x < ax + (y - ay) * ex / ey:
            inside = not inside
    d = math.sqrt(d2)
    return -d if inside else d


def coverage(sd, px):
    """Fraction of a pixel (px canvas units wide) covered by the shape at signed distance sd."""
    return max(0.0, min(1.0, 0.5 - sd / px))


def over(dst, rgb, a):
    """Composite a straight-alpha colour over a premultiplied pixel."""
    r, g, b, da = dst
    k = 1.0 - a
    return (rgb[0] * a + r * k, rgb[1] * a + g * k, rgb[2] * a + b * k, a + da * k)


def render(size):
    px = 64.0 / size
    x0, y0, x1, y1, _ = RECT
    gx, gy, gr = x0 + 0.35 * (x1 - x0), y0 + 0.8 * (y1 - y0), 0.9 * (x1 - x0)   # radialGradient cx cy r
    ay0, ay1 = min(p[1] for p in ARROW), max(p[1] for p in ARROW)
    sx, sy, sa = SHADOW
    bx0, bx1 = min(q[0] for q in ARROW) - px, max(q[0] for q in ARROW) + px + sx   # the arrow and its shadow
    by0, by1 = ay0 - px, ay1 + px + sy
    rows = []
    for j in range(size):
        y = (j + 0.5) * px
        line = bytearray()
        for i in range(size):
            x = (i + 0.5) * px
            p = (0.0, 0.0, 0.0, 0.0)
            rd = rounded_rect_sd(x, y)
            if rd < RIM / 2 + px:
                p = over(p, ramp(METAL_STOPS, math.hypot(x - gx, y - gy) / gr), coverage(rd, px))
                p = over(p, RIM_RGB, coverage(abs(rd) - RIM / 2, px))
                if bx0 <= x <= bx1 and by0 <= y <= by1:
                    p = over(p, SHADOW_RGB, coverage(polygon_sd(ARROW, x - sx, y - sy), px) * sa)
                    p = over(p, ramp(ARROW_STOPS, (y - ay0) / (ay1 - ay0)), coverage(polygon_sd(ARROW, x, y), px))
            a = p[3]
            u = 1.0 / a if a > 0 else 0.0
            line += bytes((min(255, round(p[0] * u * 255)), min(255, round(p[1] * u * 255)),
                           min(255, round(p[2] * u * 255)), min(255, round(a * 255))))
        rows.append(line)
    return rows


def write_png(path, w, h, rows):
    def chunk(kind, body):
        return struct.pack('>I', len(body)) + kind + body + struct.pack('>I', zlib.crc32(kind + body))
    raw = b''.join(b'\0' + bytes(r) for r in rows)
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 6, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))


def main():
    if len(sys.argv) != 3:
        raise SystemExit(__doc__)
    size = int(sys.argv[1])
    if not 8 <= size <= 2048:
        raise SystemExit('size must be 8..2048')
    write_png(sys.argv[2], size, size, render(size))


if __name__ == '__main__':
    main()
