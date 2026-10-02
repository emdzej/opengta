# Data formats

Verified against the data of the 2002 release (`game/GTADATA`) with `tools/gtafmt.py`; the loaders in the
original executable are the authority (addresses are cited as they are identified). Little-endian throughout.

## City map (`NYC.CMP`, `SANB.CMP`, `MIAMI.CMP`)

| Offset | Size | Field |
|---|---|---|
| 0 | u32 | version, 331 |
| 4 | u8 | style number (`STYLE%03d.G24`; NYC 1) |
| 5 | u8 | sample (sound bank) number |
| 6 | 2 | reserved |
| 8 | u32 | route size |
| 12 | u32 | object position size |
| 16 | u32 | column size (bytes) |
| 20 | u32 | block size (bytes, 8 per block) |
| 24 | u32 | nav data size |
| 28 | 256 × 256 × u32 | base: byte offset of each column in the column data, row by row (y major) |
| | column size | columns: u16 words |
| | block size | block descriptors |
| | object size | object positions, 14 bytes each (at most 0xadd4 bytes) |
| | route size | routes |
| | 0x6c | service locations (not counted in the header): 6 categories × 6 × (x, y, z) bytes: police stations, hospitals, -, -, fire stations, - |
| | nav size | nav data (area names), 35 bytes each: x, y, w, h, sample, name[30] |

The file size is exactly 28 + 0x40000 + the five sizes + 0x6c (all three cities). `Map_Load` 0x438200
reads base, columns and blocks into one buffer (followed by 0x4000 bytes for the copy-on-write map change
areas), routes plus locations into a 0x4000-byte buffer, and fails if the objects exceed 0xadd4 bytes or
the nav data 0x578.

**Layers.** The exe numbers layers from the top: **z = 0 is the highest layer, z = 5 the lowest**. Block z
occupies depths 64 z .. 64 z + 64 below the top plane (world z grows downward, 0x400000 per block in 16.16
pixels); its lid is drawn on plane z (its top), its sides between planes z and z + 1. Street level is
usually the lid of z = 4 (water: z = 5). A cell's *type* describes the space a walker is in, so the ground
under a pavement cell z is the lid of block z + 1 (`Map_GetGroundZ` 0x4544e0 returns z * 0x400000 +
0x3f0000 for the first non-air cell). MISSION.INI coordinates `(x, y, z)` put objects at height z * 64,
on the lid of block z.

A **column** starts with an s16 `h`, the number of empty layers at the top (6 = empty column); `6 - h`
u16 block indices follow for z = h .. 5, the top block first: block (x, y, z) is
`blocks[column[1 + z - h]]` if z >= h (`Map_GetBlock` 0x437ae0, which also clamps x and y to 0..255).
Columns are shared between map squares.

A **block** (8 bytes):

| Offset | Size | Field |
|---|---|---|
| 0 | u16 | type map: bits 0–3 directions (up, down, left, right), 4–6 block type (0 air, 1 water, 2 road, 3 pavement, 4 field, 5 building), 7 flat (transparent: texel 0 not drawn, sides on one edge only), 8–13 slope (0 none; class/segment table at 0x4b0c88), 14–15 lid rotation (90° steps) |
| 2 | u8 | type map ext: bits 0–2 traffic light / train hints, 3–4 lid remap (one of the lid's 4 palettes), 5 flip top/bottom, 6 flip left/right, 7 railway |
| 3 | u8 ×5 | face tiles: left, right, top, bottom (side tiles), lid (lid tile); 0 = no face |

Side tile `t` is texture tile `t` (the first side tile is never drawn); lid `L` is texture tile `sides + L`.
Side faces take their palette per direction (top 0, bottom 1, left 2, right 3: palette index `4t + d`),
lids per remap (`4 (sides + L) + remap`).

**Slopes** (`Render_DrawSlope` 0x4395d0, table of 3-byte {class, segments, segment} at 0x4b0c88, read from
the exe): 1–8 are 2-block ramps (class up/down/left/right × segment 1, 0), 9–40 8-block ramps (segments
7..0), 41–44 one-block ramps; class 1 = high at the north (top, -y) edge, 2 south, 3 west, 4 east. Segment k
of n spans heights k/n .. (k+1)/n of the block below its top plane. Slopes 45–63 read past the table into
unrelated data: nothing is drawn for them.

The type cache (0x55fab0, `[z][y][x]` bytes) holds each cell's type map low 7 bits with bit 7 = sloped;
`Map_Load` builds it.

NYC: 5,318 block descriptors, 117,694 bytes of columns.

## Style (`STYLE001.G24` … `STYLE003.G24`)

The header is sixteen u32: version (336), side size, lid size, aux size, anim size, CLUT size, tile CLUT size,
sprite CLUT size, new-car CLUT size, font CLUT size, palette index size, object info size, car size, sprite
info size, sprite graphics size, sprite numbers size. The sections follow in that order (`Style_Load`
0x47cf10):

- **Tiles**: side, lid, aux tiles, 64 × 64 bytes each, 4096-byte multiples (checked), at most 0x190000 bytes
  in all, padded in the file to 16 KB. Pages of 64 KB hold 16 tiles in a 4 × 4 grid with a 256-byte row
  stride: tile `n` (sides first, then lids, then aux) pixel `(u, v)` is at
  `(n >> 4) * 0x10000 + ((n >> 2) & 3) * 0x4000 + v * 256 + (n & 3) * 64 + u`. The renderer addresses texels
  as `page[v << 8 | u]` with 8-bit u and v, so steps past a tile's edge read its neighbour in the page.
- **Animations** (anim size bytes): a count byte (at most 128), then per animation `{u8 block, u8 which
  (0 side, 1 lid), u8 speed (frames per step), u8 n, u8 frame[n]}`; frames are aux tile numbers. A cycle
  is n + 1 steps: the n aux tiles, then the tile itself (`Style_UpdateAnims` 0x47d610). NYC's first anim is
  lid 41, the water.
- **CLUTs**: CLUT size rounded up to 64 KB pages, 64 palettes of 256 colours per page; row `e` (256 bytes) of
  a page holds colour `e` of each of the page's 64 palettes as B, G, R, 0. Palette `p` starts at byte
  `(p >> 6) * 0x10000 + (p & 63) * 4`, colour `e` 256 bytes further per e. `Style_ConvertPalettes` 0x47cd10
  rewrites the words in place into the display's pixel format (`(c >> precision) << position` per channel;
  at 32 bpp the word is 0x00RRGGBB, i.e. the file's bytes with byte 3 cleared).
- **Palette index**: u16 per logical palette, mapping it to a CLUT. Tile `n` uses `index[4n + r]`
  (r = direction for sides, remap for lids). Sprite palettes follow the tile palettes (from 4 × all tiles),
  then the car remaps (from (tile + sprite CLUT size) / 1024), then the fonts (from (tile + sprite + new
  car CLUT size) / 1024; the first 8 are the aux palettes of `Tile_BuildAuxTable` 0x4376f0).
- Object info, cars, sprite info, sprite graphics, sprite numbers (to be documented; see
  docs/re/inventory-5.md for the sprite records).

STYLE001: 195 side, 154 lid, 37 aux tiles; 1,024 palettes.

## Text, fonts, frontend pictures, settings (`*.FXT`, `*.FON`, `*.RAW`/`*.RAT`, `PLAYER_A.DAT`)

Summary; the details, loaders and quirks are in [text-fonts.md](text-fonts.md).

- **FXT**: the whole file is encrypted bytewise, `b -= k; k += m; m *= 2` (mod 256) from k = 0x64,
  m = 0x63. Decrypted, it is `[identifier]text\0` repeated, ending in `[]`. Text is UTF-8: accents in
  French, German and Italian, pure ASCII in English and special.
- **FON**: u8 glyph count, u8 height, then per glyph a u8 width and `width × height` 8-bit pixels
  (0 = transparent), then a 768-byte RGB palette. Glyph 0 is the character code the loader is given:
  0x21 for text fonts, 1 for icon/animation fonts, 0 for digit fonts. Codes >= 0x80 are remapped
  through a table in the exe.
- **RAW**: headerless 24-bit R, G, B, size given by the loader:
  - 640 × 168: `F_UPPER`, `F_LOGO0..7`
  - 640 × 312: `F_LOWER0/1`
  - 640 × 480: `CUT0..5`
  - 102 × 141: `F_PLAY1..8`
  - 180 × 50: `F_PLAYN`
  - 64 × 59: `F_RSTAR`, `F_RSTARN`
- **RAT**: the same pictures as 8-bit indices. They use the palette `F_PAL.RAW`, or `CUTn.ACT` for the
  cuts (768 bytes RGB each). This is the dead 8-bit path; the Windows exe never reads the palettes.
- **PLAYER_A.DAT**: 0x414 bytes. Settings at +0, the language override at +0x14, high scores (6 levels ×
  3 × {s32, char[16]}) at +0x18, 8 player records of 0x50 bytes at +0x180, the current player at
  +0x400, the network name at +0x401.
