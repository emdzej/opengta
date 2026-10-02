"""Reference decoders for GTA 1 data formats (Python 3, standard library only).

Used to check our understanding of the formats against the data before the C port, and as a
reference renderer to compare the C code with. Format notes live in docs/formats.md; the authority
is the original executable (WINO/Grand Theft Auto.exe), the layouts here follow DMA's "CityScape Data
Structure" document as summarised by the community (cds.doc v12.10) and are checked against the data.

  python3 tools/gtafmt.py map  game/GTADATA/NYC.CMP out/nyc.png [px per block]
  python3 tools/gtafmt.py tiles game/GTADATA/STYLE001.G24 out/tiles.png
"""
import struct
import sys
import zlib

MAP_W = 256  # blocks per side
MAP_Z = 6    # layers


def png(path, w, h, rgb):
    """Writes an RGB8 PNG (rgb: bytes of w*h*3)."""
    raw = b"".join(b"\0" + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))

    def chunk(t, d):
        return struct.pack(">I", len(d)) + t + d + struct.pack(">I", zlib.crc32(t + d) & 0xffffffff)

    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", w, h, 8, 2, 0, 0, 0))
                + chunk(b"IDAT", zlib.compress(raw, 6)) + chunk(b"IEND", b""))


class Block:
    __slots__ = ("type_map", "type_map_ext", "left", "right", "top", "bottom", "lid")

    def __init__(self, b):
        (self.type_map, self.type_map_ext, self.left, self.right, self.top, self.bottom,
         self.lid) = struct.unpack("<HBBBBBB", b)

    @property
    def slope(self):
        return (self.type_map >> 8) & 0x3f

    @property
    def lid_rot(self):
        return self.type_map >> 14

    @property
    def remap(self):
        return (self.type_map_ext >> 3) & 3


class Cmp:
    """A .CMP city map: header, base[256][256] u32 column offsets, columns (u16), blocks (8 bytes)."""

    def __init__(self, path):
        d = open(path, "rb").read()
        (self.version, self.style, self.sample, _, _, self.route_size, self.object_pos_size,
         self.column_size, self.block_size, self.nav_data_size) = struct.unpack_from("<IBBBBIIIII", d, 0)
        assert self.version == 331, self.version
        o = 28
        self.base = struct.unpack_from("<%dI" % (MAP_W * MAP_W), d, o)
        o += MAP_W * MAP_W * 4
        self.columns = d[o:o + self.column_size]
        o += self.column_size
        self.blocks = [Block(d[o + i * 8:o + i * 8 + 8]) for i in range(self.block_size // 8)]
        o += self.block_size
        self.rest_offset = o
        self.size = len(d)

    def column(self, x, y):
        """Blocks of column (x, y) indexed by z in the exe's convention: z = 0 is the TOP layer, z = 5 the
        lowest; None above the column's top (Map_GetBlock 0x437ae0)."""
        off = self.base[y * MAP_W + x]
        height = struct.unpack_from("<H", self.columns, off)[0]  # empty layers at the top
        ids = struct.unpack_from("<%dH" % (MAP_Z - height), self.columns, off + 2)
        # after the height word, entry k is z = height + k (the top block first)
        return [None] * height + [self.blocks[i] for i in ids]


class Style:
    """A .G24 style: 64x64 block tiles (side, lid, aux) in 256x256 pages, paged CLUTs, palette index."""

    HDR = ("version side_size lid_size aux_size anim_size clut_size tileclut_size spriteclut_size "
           "newcarclut_size fontclut_size palette_index_size object_info_size car_size sprite_info_size "
           "sprite_graphics_size sprite_numbers_size").split()

    def __init__(self, path):
        d = open(path, "rb").read()
        self.h = dict(zip(self.HDR, struct.unpack_from("<16I", d, 0)))
        h = self.h
        assert h["version"] == 336, h["version"]
        o = 64
        ntiles = (h["side_size"] + h["lid_size"] + h["aux_size"]) // 4096
        self.nside, self.nlid = h["side_size"] // 4096, h["lid_size"] // 4096
        ntiles_padded = (ntiles + 3) // 4 * 4
        self.tiles = d[o:o + ntiles_padded * 4096]
        o += ntiles_padded * 4096
        o += h["anim_size"]
        clut_bytes = (h["clut_size"] + 0xffff) // 0x10000 * 0x10000
        clut = d[o:o + clut_bytes]
        o += clut_bytes
        # 64 palettes per 64 KB page; row e of a page holds entry e (BGRA) of each of the 64 palettes
        self.pals = []
        for page in range(clut_bytes // 0x10000):
            for p in range(64):
                base = page * 0x10000 + p * 4
                self.pals.append([(clut[base + e * 256 + 2], clut[base + e * 256 + 1], clut[base + e * 256])
                                  for e in range(256)])
        self.pal_index = struct.unpack_from("<%dH" % (h["palette_index_size"] // 2), d, o)
        o += h["palette_index_size"]

    def tile_rgb(self, n, remap=0):
        """Tile n (linear: sides, then lids, then aux) as 64*64 RGB tuples."""
        pal = self.pals[self.pal_index[4 * n + remap]]
        page, k = divmod(n, 4)
        px = []
        for y in range(64):
            row = page * 16384 + y * 256 + k * 64  # 4 tiles side by side per 256-byte row
            px.extend(pal[c] for c in self.tiles[row:row + 64])
        return px


def cmd_map(cmp_path, out, scale=8):
    m = Cmp(cmp_path)
    style = Style(cmp_path.rsplit("/", 1)[0] + "/STYLE%03d.G24" % m.style)
    print("style", m.style, "blocks", len(m.blocks), "columns", m.column_size, "rest at", m.rest_offset, "of", m.size)
    step = 64 // scale
    cache = {}
    w = MAP_W * scale
    img = bytearray(w * w * 3)
    for y in range(MAP_W):
        for x in range(MAP_W):
            col = m.column(x, y)
            top = next((b for b in col if b is not None and b.lid), None)  # highest lid
            if top is None:
                continue
            key = (top.lid, top.remap, top.lid_rot)
            if key not in cache:
                t = style.tile_rgb(style.nside + top.lid, top.remap)
                small = []
                for v in range(scale):
                    for u in range(scale):
                        su, sv = u * step, v * step
                        r = top.lid_rot  # 0, 90, 180, 270 degrees clockwise
                        for _ in range(r):
                            su, sv = sv, 63 - su
                        small.append(t[sv * 64 + su])
                cache[key] = small
            small = cache[key]
            for v in range(scale):
                o = ((y * scale + v) * w + x * scale) * 3
                for u in range(scale):
                    img[o:o + 3] = bytes(small[v * scale + u])
                    o += 3
    png(out, w, w, bytes(img))


def cmd_tiles(g24, out):
    s = Style(g24)
    n = (s.h["side_size"] + s.h["lid_size"] + s.h["aux_size"]) // 4096
    cols = 16
    rows = (n + cols - 1) // cols
    w, h = cols * 64, rows * 64
    img = bytearray(w * h * 3)
    for i in range(n):
        t = s.tile_rgb(i)
        ox, oy = (i % cols) * 64, (i // cols) * 64
        for y in range(64):
            o = ((oy + y) * w + ox) * 3
            img[o:o + 64 * 3] = bytes(c for p in t[y * 64:(y + 1) * 64] for c in p)
    png(out, w, h, bytes(img))
    print(s.h, "tiles", n, "palettes", len(s.pals))


if __name__ == "__main__":
    {"map": lambda a: cmd_map(a[0], a[1], int(a[2]) if len(a) > 2 else 8),
     "tiles": lambda a: cmd_tiles(a[0], a[1])}[sys.argv[1]](sys.argv[2:])
