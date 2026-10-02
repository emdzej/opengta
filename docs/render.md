# The city renderer

How GTA 1 (the 2002 Windows build) turns a camera position into the in-game picture of the city, as
ported in `src/render/` (`camera.c`, `city.c`, `poly.c`), `src/map.c` and `src/style.c`. Addresses are
the original's; names follow `tools/ghidra/names.tsv`. Formats: [formats.md](formats.md).

Everything is integer arithmetic: 16.16 fixed point, C division (truncating toward zero), and the
x86 rasteriser's 8-bit register tricks, reproduced exactly. The only floating point is in the startup
tables (`Math_InitTables` 0x430400, `Poly_BuildBlendTable` 0x497b16).

## Frame

`Game_Run` 0x4148a0 per frame: `Game_Frame` 0x430b20 runs `Game_Update` 0x430c00 (which steps the tile
animation, `Style_UpdateAnims` 0x47d610, among all the game logic), then `Camera_Update(-1000)` 0x43b910
for every player, `Render_ComputeVisibleRect` 0x43b7e0 and `Render_CopyCamera` 0x43b780. `Game_Render`
0x430d40 then queues the visible sprites (`Render_QueueVisibleEntities` 0x437000, not ported), draws the
city (`Render_DrawCity` 0x4389f0) and the HUD. `Gfx_Present` 0x414b10 flips. Nothing clears the back
buffer: pixels no block covers keep an older frame.

The game runs in-game at 640 x 480 (preferred mode 0x504cbc; `Player_SetViewport` 0x464500 gets the
screen size), in 15, 16 or 32 bpp. The port draws the 32 bpp path: one `uint32_t` per pixel in the
format the CLUT was converted to (0x00RRGGBB for the DirectDraw 32-bit surface).

## Camera (0x43b910-0x43cbb0)

Per player (record 0x74f148 + player * 0x1bc) three blocks matter (`camera.h`):

- the **viewport** at +0x18: screen centre, size, `w << 16`, `h << 16`, then the outputs **height**
  (+0x18, the camera's distance above the top plane), **x, y** (+0x1c/+0x20, the point under the camera,
  in world pixels = 64 per block), **scale** (+0x24) and **zoom** (+0x28), and a squash flag (+0x2c, set
  for non-4:3 screens: y is projected * 5/6);
- the **camera** at +0x48: debug offsets (x, y, zoom, height), the target point, the eased zoom and
  height, look-ahead x/y, speed zoom/height, six easing velocities, a zoom bias, a size margin and a
  state (0 follow, 1 move to a new focus, 2 snap);
- the **view rect** at +4 (left, right, top, bottom, half width) for traffic spawning.

The mode byte (+0x188, 0 in practice: `Map_Load` sets it) selects a parameter set at 0x4b0d18 (9 ints
each, read from the exe): mode 0 is speed 8, size 2, max velocity 16, height 300, zoom offset 0, speed
height 5, speed zoom 5.

`Camera_Follow` 0x43bbd0 eases every camera field toward a target value with one rule: the field moves
by half its velocity, the velocity grows by `up` (2 or 4) toward the target or shrinks by `down`, up to
the mode's max velocity, and both snap when the target is reached or passed. Targets: zoom = zoom bias
+ mode zoom offset + mode height (300); height = the target's z (world pixels, growing downward); look-
ahead = sin/cos(heading) * speed * 10 >> 16 (none for peds); speed height = -(speed_height * speed *
mode speed) / 10; speed zoom = speed * mode speed / 2 * speed_zoom / 10 (the speed is clamped to
8..48 first; cars of model 7/33 count triple speed, target kind 1 tenfold).

`Camera_Update` then computes

    x = look-ahead x + target x + debug x         (and y)
    height = mode height - speed height - eased height - debug height,  clamped to 16..716
    zoom = eased zoom - speed zoom - debug zoom,                        clamped to 60..500
    scale = screen width * zoom / 320

and limits the look-ahead to (eased height + height) * 160 / zoom minus the size margin (3/4 of it
vertically). So with a ped standing on NYC's street level (z = 255) the camera height is
300 + 32 - 255 = 77 and the zoom 284 (scale 568): the street, 333 pixels below the camera, shows
64 * 568 / 333 = 109 screen pixels per block, about 6 blocks across. Fast cars lower the zoom and raise
the camera. `Camera_InitAll` 0x43c710 snaps with a zoom bias of -204, which is why a level starts zoomed
out and zooms in.

## Projection

A layer plane z (0 = top of the highest layer, 6 = bottom of the lowest) is at depth `d = height + 64 z`;
a world point projects to `centre + (p - camera) * scale / d`. Two implementations exist with different
rounding:

- **The vertex grid** (`Render_ProjectLayer` 0x43b620): for the 65 x 65 grid of block corners of rect 6
  (below), the origin is `((left * 64 - x) * scale / d + cx) << 16` and every next corner adds
  `(scale << 16) / d * 64` (16.16); stored as `>> 16`. So adjacent corners are exactly one step apart and
  rounding is per plane, not per vertex. Two grid planes (0x54f2a0, `[plane][x][y]` of {sx, sy}) alternate
  as the upper and lower plane of the layer being drawn (0x5c1c20 / 0x5bfbe0).
- **`Sprite_WorldToScreen`** 0x47bb10 for points between planes (slopes, sprites):
  `((X - x << 16) / (Z >> 16 + height) + 127) * scale >> 16) + cx` with 16.16 X and Z.

`Render_ComputeVisibleRect` computes, for depths 64 (k + 1), k = 6..0, how many blocks fit across:
`nx = (w << 16) / ((scale << 16) / depth * 64) + 3`, rounded up to even (same for ny), centred on the
camera's block (`x / 64 - nx / 2`). Rect 6 (the deepest, so the widest) is also the grid descriptor
(the pointer at 0x4b0d10): its corner is the grid origin and its size the grid's. Layer z is walked with rect
z + 1; each rect stores its start and middle relative to the grid origin and the sums `x0 + x1` for
mirroring.

## Drawing the city (`Render_DrawCity` 0x4389f0)

    project plane 6 into grid plane 1
    for z = 5 .. 0 (lowest layer first):
        swap planes; project plane z into the new upper plane     (upper = plane z, lower = plane z + 1)
        Sprite_DrawLevel(z)                                       (sprites of this layer; not ported)
        for y from the rect's top to its middle, for x from its left to its middle:
            draw (x, y), (x_sum - x, y), (x, y_sum - y), (x_sum - x, y_sum - y)

Blocks are thus drawn from the four edges of the screen toward the centre, so a block nearer the
camera's axis (whose sides face outward) overdraws the farther ones; higher layers overdraw lower ones.
A block is dispatched on its type map: slope bits set → `Render_DrawSlope` (and in NYC only,
`Render_DrawFlatAbove` 0x439530: the flat block above it redrawn without lid using this layer's planes);
flat bit set → `Render_DrawFlatBlock` 0x439180; else `Render_DrawBlock` 0x438d60.

### Normal blocks (`Render_DrawBlock` 0x438d60)

With U = upper plane corners and L = lower plane corners, a side is drawn when the camera sees it:
left if `L(x).sx < U(x).sx`, right if `U(x+1).sx < L(x+1).sx`, top if `L(y).sy < U(y).sy`, bottom if
`U(y+1).sy < L(y+1).sy`. Then the lid on the upper plane. The order is left, right, top, bottom, lid.

| Face | Drawer | Face word | CLUT | Tile |
|---|---|---|---|---|
| left | FaceVert, edges U(x) / L(x) | `(~ext & 0x40) << 15 \| 0xc000` | side[left][2] | side_remap[left] |
| right | FaceVert, U(x+1) / L(x+1) | `(ext & 0x40) << 15 \| 0xc000` | side[right][3] | side_remap[right] |
| top | FaceHoriz, edges L(y) / U(y) | `(ext & 0x20) << 17 \| 0x8000` | side[top][0] | side_remap[top] |
| bottom | FaceHoriz, L(y+1) / U(y+1) | `(~ext & 0x20) << 17 \| 0x8000` | side[bottom][1] | side_remap[bottom] |
| lid | FaceHoriz, U(y) / U(y+1) | type map (u16: rotation, flat bit) | lid[lid][ext bits 3-4] | lid_remap[lid] |

So sides always use rotation 180 (horizontal) or 270 (vertical, which `Poly_DrawFaceVert` turns into
0), never the rotation cache; the ext flip bits arrive as the mirror (0x400000) or flip (0x200000) bit,
inverted on the left and bottom sides so a texture reads the same way round the block. The lid's flip bits
are never set (its face word is the u16 type map), only its rotation.

The tile tables (`Tile_Build*Table` 0x437750 / 0x4377b0 / 0x437830, rebuilt by animation through
`Tile_SetSideEntry` 0x4378a0 / `Tile_SetLidEntry` 0x4379c0) hold CLUT pointers: 4 per side tile (one per
direction), 4 per lid (one per remap). `side_remap` (0x775320) / `lid_remap` (0x7750d8) map a map tile
to a texture tile (identity / + side count, changed by animation to aux tiles).

### Flat blocks (0x439180, 0x4392d0, 0x4393f0)

A flat (transparent) block draws at most one side per axis, always on its **left** edge (x) and its
**top** edge (y), using the left (or top) tile if that side is visible and set, else the right (or
bottom) tile, with the transparent bit 0x80 in the face word; the style's skip tile (0x5c1c14: 0xc2 NYC,
0xc1 San Andreas, 0xc5 Vice City) is never drawn. The y side goes first if the camera is up-left of the
corner (`L.sx < U.sx && U.sy <= L.sy`), else the x side. The lid follows (transparent through the type
map's flat bit).

### Slopes (0x4395d0 and 0x4396b0 / 0x439e10 / 0x43a620 / 0x43adf0)

The slope number indexes the 3-byte table at 0x4b0c88 (class, segments n, segment k; see formats.md).
Segment k is the part of a ramp n blocks long that spans heights k/n (its high edge) to (k + 1)/n (its
low edge) below the layer's top plane: `z_hi = (k << 22) / n`, `z_lo = ((k + 1) << 22) / n` added to
`z * 0x400000`. Corners at k = 0 (high edge) and k = n - 1 (low edge) come from the grid, the others from
`Sprite_WorldToScreen`.

- The two sides along the slope are textured polygons (`Poly_DrawQuad`): a triangle for the last segment
  (k = n - 1), a quad otherwise, with u (classes 1/2) or v (3/4) picking the matching slice of the side
  tile: `(n - k) * 64 / n - 1` at the high edge and `((n - k) * 64 - 64) / n - 1` at the low edge (the
  classes 3/4 count from the other side: `64 - ...`, `(64 - 64 (n - k)) / n + 65`). Their face word is
  rotation 270 (0xc000) or 180 (0x8000), so vertical-axis sides use the rotation cache. Quads ignore the
  flip bits.
- The high-end side (top for class 1, bottom for 2, left for 3, right for 4) is a normal face, drawn only
  for segment 0.
- The lid is a face between the high-edge and low-edge corners: FaceHoriz for classes 1/2 (class 2 with
  flip 0x200000), FaceVert for 3/4 (class 4 with mirror 0x400000).

## The rasteriser (`Poly_*` 0x496cf0-0x49b778, `poly.c`)

### Textures and CLUTs

`Poly_SelectTile` 0x497dfc gives a tile's page (`texBase + (t >> 4) * 0x10000`) and origin `u0 = (t & 3) *
64`, `v0 = ((t >> 2) & 3) * 64`. For rotation 90 or 270 the tile comes from an LRU cache of rotated copies
instead (8 slots of 64 x 64 at style buffer + 0x1a0000, 0x4b335c = 0x8000 bytes; a miss rotates the tile
with `Poly_RotateTile90` 0x49b044, `dst[x][63 - y] = src[y][x]`). The cache is ported exactly because a
step overshooting a slot reads its neighbour slot, whose contents depend on the LRU history.

Fillers fetch `page[v << 8 | u]` with u and v in 8-bit registers (BL, BH): u wraps at 256 inside the page
row and v at 256 inside the page. The current CLUT (0x78c10c) points at colour 0 of a palette; the
filler puts the texel into byte 1 of the pointer, i.e. colour t is 256 bytes further per t (the CLUT
page layout). Texel 0 is skipped by every transparent path.

### Face trapezoids (`Poly_DrawFaceHoriz` 0x497035 / `Poly_DrawFaceVert` 0x497332)

Arguments are used as 16-bit values. FaceHoriz takes edge a (`xl_a, xr_a` at `y_a`) and edge b. From the
face word: rotation 180/270 toggles the mirror bit and swaps the edges unless the flip bit is set,
rotation 0/90 swaps them if it is set; 90/270 use the rotation cache. FaceVert adds 90 degrees to the
rotation, exchanges the flip and mirror bits, inverts the mirror bit, then does the same.

`Poly_FaceHoriz32` 0x49a78c (and its column twin `Poly_FaceVert32` 0x49abdc):

- reject if both edges are above, below, left or right of the clip rectangle;
- rows from y_a to y_b inclusive (`dir` = sign, a zero-height face draws one row); d = |y_b - y_a| or 1;
- left x starts at `xl_a << 16 + 0x8000` and steps `((xl_b - xl_a) << 16) / d` (C division), right x
  likewise; v starts at `0x8000 + (v0 << 16)` and steps `recip[d]` where `recip[i] = 0x3f0000 / i`
  (0x788100, 4096 entries; 63/i in 16.16) — so v runs over 0..63.5 of the tile;
- clipped rows are skipped by advancing the accumulators by the skipped row count;
- per row: `x0 = left >> 16`, `x1 = right >> 16`, u starts at `u0 << 16 + 0x8000` (mirrored:
  `+ 0x3f0000` and the step negated), steps `recip[x1 - x0]`; the row draws `x1 - x0 + 1` pixels (one
  if the edges cross);
- the inner loop keeps the u fraction in the high half of ECX and adds the step rotated by 16 bits:
  the carry out of the fraction goes into BL together with the step's integer byte (`add ecx, eax;
  adc bl, al`). Only the low byte of the step's integer part reaches BL;
- transparent faces (0x80) skip texel 0.

A span wider than 4095 pixels or a negative width reads outside `recip` in the original (the port uses
0 and counts it in `poly_recip_oob`; only far off-screen corners do this).

### Polygons (`Poly_DrawQuad` 0x497710, `Poly_Draw` 0x496cf0)

The quad gets 4 (or 3, if `u3 < 0`) vertices with u/v bytes `uv + tile origin` (8-bit wrap) and flags 2
(opaque) or 6 (the face word's 0x80: blended). A polygon without vertical extent is dropped; `Poly_Draw`
rejects it if its bounding box misses the clip or is empty in x or y. Edges start at the top vertex
(`Poly_FindExtents` 0x499fd2, first index on ties): edge A from the last top vertex walking backwards
through the vertex list, edge B from the last top vertex walking forwards. Rows go from the top y to the
bottom y, exclusive (or the clip's y1 inclusive); rows above the clip are stepped one by one.

- Edge A (the span's right end, 0x4b8864): x starts at `x << 16 + 0x8000`; when it moves on to its next
  vertex (`Poly_StepLeftEdge` 0x498540) at `x << 16 + 0x7fff`. Edge B (the left end, 0x4b886c): x starts
  at `x << 16` (no rounding), also after `Poly_StepRightEdge` 0x4986a9. u and v start at `uv << 16 +
  0x8000` and are **not** reset at an edge change: they keep accumulating the new steps.
- A row spans `[B >> 16, A >> 16)`; nothing if empty (so the vertex order decides whether a polygon is
  drawn at all). u/v step `(A - B) / width` per pixel.
- `Poly_SpanTex32` 0x4996c4 (flags 2) skips texel 0, so slope sides are see-through where the tile has
  colour 0. Its v register is `v << 16` plus, in its low half, the steps' integer bytes, which carry
  into the v fraction (a quirk the port keeps).
- `Poly_SpanBlend32` 0x499da4 (flags 6, the flat slopes) writes `blend[clut][screen]` per channel, with the
  64 KB table built as `(int)(a * 0.5f + b * 0.5)` = `(a + b) >> 1`, byte 3 cleared. Its u/v fractions
  are read from dwords straddling the edge's values (0x4b889a, 0x4b8892), so after a left clip only the
  integer parts are adjusted.

## Palettes and animation

`Style_Load` 0x47cf10 keeps the original's memory layout: one 64 KB-aligned buffer with the blend table,
the tile pages (from +0x10000), the rotation cache (+0x1a0000) and the anim section; the CLUT pages in a
second one. `Style_ConvertPalettes` 0x47cd10 converts the CLUT in place to the display format with the
DirectDraw surface's shifts. `Style_UpdateAnims` steps every animation each game frame: every `speed`
frames the next aux tile is shown (`Style_SetTileFrame` 0x47d440 points the tile's remap at aux base +
frame and copies the aux tile's CLUTs into its table entry), and after the last frame the tile itself.

## Not ported yet

- Sprites: `Render_QueueVisibleEntities` 0x437000, `Sprite_DrawLevel` 0x47c030 between the layers, and the
  sprite drawers `Poly_DrawSprite` 0x49787c / `Poly_DrawSpriteBlend` 0x4979c9; `Sprite_LoadInfo` 0x47ca50
  (the sections are loaded raw).
- The 15/16 bpp fillers (`Poly_FaceHoriz16` 0x49b328, `Poly_FaceVert16` 0x49b778, `Poly_SpanTex16`
  0x4994b9, `Poly_SpanBlend15` 0x4998ce, `Poly_SpanBlend16` 0x499b39) and `Poly_DrawRect` 0x49806a.
- `Poly_SetupEdges`' per-vertex shade and second uv set (flags 1 and 0x10; no caller uses them).
- The map change functions (`Map_Set*` 0x437b50-0x438020) and the object / route / nav loaders the map
  sections feed.
- The camera's other target kinds come from entity records not ported yet: the dev viewer and the test
  use a ped-like target record.

## Known deviations and open questions

- `recip` lookups outside 0..4095 (far off-screen spans) and the vertex grid overflowing its 65 columns
  (only beyond the camera's normal range) read neighbouring globals in the original; the port uses 0 /
  slack memory.
- Rotated lids whose texture tile is >= 0x180 (animated lids on aux tiles 0x180+) alias the LRU head
  pointer in the original's 0x180-entry cache index; the port has a bigger index.
- Uninitialised memory the original could read (past the loaded tiles, the 64 KB page of the rotation
  cache beyond its anim data) is zero in the port.
- `Math_InitTables` uses the C library's sin/tan in double precision; the x87 `fsin`/`fptan` results
  match except possibly in the last bit before truncation.
- Not compared against frames captured from the original yet; the decompiled slope drawers were ported
  as read (spot-checked against the disassembly).
