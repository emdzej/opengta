# Inventory 6: 0x48a320 - 0x49cac6

295 functions. Names are in `tools/ghidra/names.tsv`. Most of this range is third-party code. The
SciTech **MGL** (MegaGraph Library, Win32 build, with DirectDraw, WinG and GDI back ends) runs from
0x48a320 to 0x496b70. Two DPLAYX import thunks follow, then the one block of game code here,
**DMA's software polygon/tile rasterizer** (0x496cf0 - 0x49b778). The range ends with
the MSVC 4/5-era **old iostream** library and two CRT float-init stubs.

For porting, the only part to reimplement faithfully is `Poly_*`. For MGL, the port only
needs to know *which* entry points GTA calls (listed under "Calls from game code" below) and
replace them with an SDL-style framebuffer layer.

## Modules (object-file order)

| Span | Module | Purpose |
|---|---|---|
| 0x48a320-0x48c250 | mgl_core | DC objects, pages, attributes, blit/stretch dispatch, palette, result/error, driver registry, mode detection, DC create/destroy, viewport/clip |
| 0x48c2d0-0x48df00 | mgl_win32 / mgl_ddraw | AutoRun registry toggle, alt-tab suspend/restore, window subclassing, ddraw/WinG/DIBSection loading, `MGL_init`/`MGL_exit`/`MGL_changeDisplayMode` |
| 0x48e1b0-0x48f9d0 | mgl_packed* glue | Per-depth DIB surface creation, 1x2 doubling, mouse cursor drawing (Ghidra misses many functions here; driver vectors are in data tables) |
| 0x490920-0x490fd0 | mgl_mouse | Software mouse cursor (`MS_*`), with per-page save-under |
| 0x491004-0x4918a0 | mgl_util | 16.16 fixed-point math, pixel packing, malloc hooks, `LST_*` singly linked lists |
| 0x491a20-0x492f10 | mgl_ddraw | DirectDraw fullscreen driver: pixel-format decode, surface lock, flip, DDERR strings |
| 0x493100-0x493a90 | mgl_windc | Windowed (GDI) DC driver |
| 0x493b98-0x494b40 | mgl_packed32 | 32bpp packed-pixel rasterizer (assembly): pixel/line/span/rect/mono glyph/blit/2x2 stretch, plus memset/memcpy helpers |
| 0x494d54-0x495a3d | mgl_packed16 | Same routines for 15/16 bpp |
| 0x495a98-0x49644b | mgl_packed8 | Same routines for 8 bpp |
| 0x496480, 0x496b70 | mgl_core | Generic fallbacks: fillRect via scanlines, pattern span via putPixel |
| 0x4964c0-0x4969e0 | mgl_mesa | 23 exported `glWindowPos*MESA` stubs (the exe's only exports, ordinals 1-23). Nothing calls them |
| 0x496a18-0x496b2f | mgl_ddraw | "Lock surface, call saved packed vector, unlock" wrappers |
| 0x496ce4, 0x496cea | dplay_imp | `jmp [IAT]` thunks: DPLAYX ordinal 2 = DirectPlayEnumerateA (from 0x486880), ordinal 1 = DirectPlayCreate (from 0x4869d0) |
| **0x496cf0-0x49b778** | **Poly (game)** | Polygon/tile/sprite rasterizer, texture-tile LRU rotation cache, blend table |
| 0x49bbdf-0x49ca61 | crt_iostream | ios, streambuf, strstreambuf, ostream, istream, iostream, strstream |
| 0x49caae-0x49cac6 | crt | `_fpmath`, `_cfltcvt_init` |

## Poly: the game's rasterizer (port first)

Global state is all fixed addresses (it is effectively one C/asm translation unit):

- `0x503228` int[]: **scanline pointer table**, `row[y] = surface + y*pitch`. Built by
  `Poly_SetScreenRows(base, pitch, height)` 0x497bfa. The game calls it from the
  display setup (0x414b10) with the back-buffer DC surface (+0x1a0) and bytesPerLine (+0x1c8).
- Clip rect: shorts at 0x4b8850/54/58/5c (x0, x1, y0, y1) and ints at 0x78e548/4c/50/54.
  Set by `Poly_SetClip(x0,y0,x1,y1)` 0x497bab. The game calls (0,0,w-1,h-1).
- `0x504cb8` (owned by the game): screen bits per pixel from `MGL_getBitsPerPixel` (0x48a310).
  Every Poly entry point dispatches on it: 0x20 selects the 32bpp filler. 0x10 selects the 565
  filler. Anything else gets the 15bpp/555 filler, or the shared 16-bit filler where 555 and
  565 do not matter. The game supports only 15, 16 and 32 bpp (0x415290).
- `0x78c10c`: **current CLUT pointer**. The game sets it before every draw. A texel byte `t`
  selects the colour at `clut + t*0x100`, because the code replaces byte 1 of the pointer with
  `t`. That matches the G24 CLUT page layout: 64 palettes per 64 KB page, so palette `p` starts
  at `base + (p>>6)*0x10000 + (p&63)*4`, with entries 256 bytes apart. The game computes exactly
  that in 0x4385b0: `0x7750cc + ((p&~63)*0x100 + (p&63))*4`. Each slot holds a converted 16-
  or 32-bit screen colour. **Texel 0 is transparent** in every "transparent" path.
- `0x78c108`: 64 KB **blend table**, `T[a][b] = (int)(a*alpha + b*(1-alpha))`, built by
  `Poly_BuildBlendTable(tbl, 0.5f)` 0x497b16. The blended span fillers mix the texel and the
  screen pixel channel by channel through T (5-bit channels for 555/565, 8-bit for 32bpp).
- `0x788100` int[4096]: reciprocal table `0x3f0000 / i`, i.e. 63/len in 16.16. It gives the
  per-pixel texture step across a 64-texel tile edge.

### Texture tiles (verified against tools/gtafmt.py G24 layout)

`Poly_SelectTile(tile, faceword)` 0x497dfc sets these values:
- `0x78e8dc` = `texBase + (tile>>4)*0x10000`. Here `texBase` is `0x78c110`, set by `Poly_Init`.
- `0x78e540` = u0 = `(tile&3)*64`, and `0x78e544` = v0 = `((tile>>2)&3)*64`.
- So a tile is 64x64 bytes with a 256-byte row stride: 4 tiles per row and 16 tiles per 64 KB
  page. This is the same layout gtafmt.py decodes.
- **Rotation**: bits 14-15 of the face word hold the lid rotation (type_map bits 14-15). For 90
  and 270 degrees the tile is fetched from an **LRU cache of pre-rotated tiles**. That cache
  holds up to 0x180 tile ids. Lookup goes through `0x78c120[tile] -> entry`. Entries are 16
  bytes at 0x78c740: {prev, next, texptr, u8 u, u8 v, u16 tileId (0xffff = free)}, with head at
  0x78c720 and tail at 0x78c114. Per-tile hit and miss counters live at 0x78df40 and 0x78e900,
  with totals at 0x78e8d8 and 0x78e8d0.
- On a miss, `Poly_RotateTile90` 0x49b044 rotates the tile into its slot:
  `dst[x*256 + 63-y] = src[y*256 + x]`. For 180 and 270 degrees the drawers also swap edges.
- Cache slot `n` sits at `cacheBase + (n>>4)*0x10000 + ((n>>2)&3)*0x4000 + (n&3)*64`.
- `Poly_Init(texBase, cacheBase, nSlots, firstSlotIndex)` 0x497c3b. The game calls it from
  0x47cf10, the style loader, with `nSlots = 0x4b335c >> 12`.

### Face word (first argument of the face/quad drawers)

The game builds the face word from the CMP block_info. The lid uses `type_map | ext<<16`
directly. Sides use synthetic words such as `0x8000 | (ext&0x20)<<17`.
- bits 14-15: rotation (0, 90, 180, 270 degrees).
- bit 7 (type_map "flat"): transparent. Texel 0 is skipped (`Poly_FaceHoriz32` tests `&0x80`).
  For `Poly_DrawQuad` it selects polygon flags 6 instead of 2, which takes the blend path.
- 0x200000 swaps the two edges (vertical flip). 0x400000 mirrors u. These come from
  type_map_ext bit 6 and bit 5 (flip top/bottom, flip left/right).

### Entry points called by the game

| Addr | Name | Args / notes | Callers |
|---|---|---|---|
| 0x497035 | Poly_DrawFaceHoriz | (face, tile, xL_top, xR_top, xL_bot, xR_bot, y_top, y_bot) | 0x438d60 (block faces), 0x439180, 0x4393f0, 0x4396b0, 0x439e10 |
| 0x497332 | Poly_DrawFaceVert | same with x/y roles swapped; adds 90 degrees to the rotation | 0x438d60, 0x4392d0, 0x43a620, 0x43adf0 |
| 0x497710 | Poly_DrawQuad | (face, tile, x0..x3, y0..y3, u0..u3, v0..v3); u3<0 means triangle | 0x4396b0, 0x439e10, 0x43a620, 0x43adf0 (slopes) |
| 0x49787c | Poly_DrawSprite | (tex: low byte u, byte1 v, high16 page; x0..x3, y0..y3 screen corners; w, h) | 0x47bc00, 0x47c130 (rotated car/ped sprites) |
| 0x4979c9 | Poly_DrawSpriteBlend | same, blended (flags 0x42). Selected when obj+0x14 and flag 0x5031e4 are set | 0x47bc00, 0x47c130 |
| 0x49806a | Poly_DrawRect | (x0, x1, y0, y1, uMax, vMax, tex) axis-aligned scaled rect | 0x481620 |
| 0x497b16 / 0x497c3b | BuildBlendTable / Init | | 0x47cf10 |
| 0x497bab / 0x497bfa | SetClip / SetScreenRows | | 0x414b10, 0x414db0 |

In 0x438d60 the vertex screen coordinates come from a **projected vertex grid at 0x54f2a0**. It
holds 8-byte {x, y} entries indexed `[(z*65 + bx)*65 + by]`, so one z-level is 0x8408 bytes. The
grid level used is z top = `0x5c1c20` or z bottom = `0x5bfbe0`. Side CLUT pointers come from
`0x5bfbf8 + side_tile*16` (one dword per face direction), and lid CLUTs from
`0x5c1c48[(ext&0x18)>>3 + lid*4]`, i.e. the remap bits pick one of 4 CLUTs. Tile-number
remaps live at `0x775320[side]` and `0x7750d8[lid]` (= lid + side count). That code is in the
lead's range, but it is the consumer of everything above.

### Polygon descriptor (pointer at 0x78e8c0, built on the caller's stack)

+0x02 u16 flags: bit0 per-vertex shade (+0x0c bytes), 0x02/0x20 textured uv (+0x20), 0x10 second uv
set (+0x2c), 0x04/0x40 blended. +0x06 u16 vertex count (3/4). +0x08 u32 texture pointer.
+0x10 s16 {x, y}[4]. +0x20 u8 {u, v}[4]. +0x28 u32 (copied to 0x4b89b0). `Poly_Draw` 0x496cf0
first finds the extreme vertices (`Poly_FindExtents` 0x499fd2) and rejects polygons outside the
clip rect. If flags == 2 it calls the plain fillers `Poly_SpanTex16/32` (0x4994b9/0x4996c4).
Otherwise it calls the blended fillers (0x4998ce 555, 0x499b39 565, 0x499da4 32bpp). The edge
walkers are 0x498800 (setup), 0x498540 (left) and 0x4986a9 (right). Mapping is affine in 16.16.

Dead code in the module: the floating-number popups 0x498133/0x498189/0x4982b4 (they format
with "%d" at 0x4b8860 and expire after 140 frames), and the profiling counters 0x49b2ad/0x49b2d6.
Nothing references any of them.

## MGL: what the game uses

Calls from game code (callers are mostly 0x414b10, 0x414db0 (display mode setup), 0x415310 (init),
0x415990, 0x422900 (fatal error), 0x44b160 (present)):

- Init (0x415310): `MGL_setAppInstance` 0x48def0, then `MGL_registerDriver` 0x48aee0 for six
  linked drivers (DDRAW8/16/32, PACK8/16/32), then `MGL_detectGraph` 0x48afe0,
  `MGL_availableModes` 0x48b740 and `MGL_modeResolution` 0x48b770. The game keeps only 15, 16
  and 32 bpp modes up to 1600 wide with a 4:3 or 8:5 aspect, and prefers 640x480 (0x504cbc).
  It formats mode names with `strstream` (0x49c11f, `<<` 0x49c293/0x49c1f5). Then come
  `MGL_init(&drv,&mode,path)` 0x48dd40 and `MGL_setWinEventHandler(0x415950)` 0x48df00. Shutdown
  calls `MGL_exit` 0x48ddc0.
- Mode switch (0x414db0): `MGL_destroyDC` 0x48d6b0, then `MGL_changeDisplayMode` 0x48dcc0. The
  front end uses mode 0x31 (640x480x16) or 0x22 (640x480x15). Mode 0x13 is 640x480x8, and the
  game falls back to its chosen default. Then `MGL_availablePages` 0x48b750,
  `MGL_createDisplayDC(pages)` 0x48b3c0 into **display DC `0x504cc8`**, `MGL_makeCurrentDC`
  0x48ae40, `MGL_clearDevice` 0x48c1e0, and `MGL_sizex/sizey` 0x48c1a0/0x48c1c0 into
  **0x504cc0/0x504cc4** (screen w, h).
  If the DC has a single page or no linear access, the game creates **back-buffer memory DC
  `0x504ccc`** with `MGL_getPixelFormat` 0x48a5d0 + `MGL_createMemoryDC` 0x48b680. Then it reads
  the pixel format (+0x20c..+0x220) to build its colour converters.
- Per frame: `MGL_setActivePage` 0x48a370, `MGL_setVisualPage(dc, page, 1)` 0x48a3c0 and
  `MGL_maxPage` 0x48a320 for page flipping (page counter `0x503214`). The other path is
  `MGL_bitBltCoord` 0x48a5f0 from the back buffer to the screen, or `MGL_stretchBltCoord`
  0x48a870 with 2x destination coordinates (pixel doubling, from 0x44b160).
- Palette: `MGL_setPalette` 0x48ac90 + `MGL_realizePalette` 0x48ad90 (0x414b10). Errors:
  `MGL_result` 0x48ae10 + `MGL_errorMsg` 0x48ae20, and `MGL_fatalError` 0x48d680 (from 0x422900).

Key MGL globals: current DC pointer `0x4b48c0`. Current-DC working copy at `0x787980`
(0x764 bytes: the whole DC struct). Last error `0x7873bc` (-16 = grInvalidDevice etc.). Mode
table `0x7878c0` (87 modes x {driver, pages}). Driver table `0x4b4ac0` (0x15-byte entries
{name, registered, driver ptr}). DC lists: display `0x4b4f70`, windowed `0x4b4f74`, memory
`0x4b4f78`. IDirectDraw at `0x4b4ffc`, primary surface `0x4b5004`, flip chain `0x4b5010[]`,
palette `0x4b5054`, fullscreen HWND `0x4b4fb8`. Mouse save-under buffers at `0x4b6c14`.

MGL DC struct (0x764 bytes), as far as it matters:
- +0x000 attribute block (0x1a0 bytes): color +0, backColor +4, writeMode +0x28, viewport
  +0x14c..+0x158, clip rect +0x164..+0x170, clip-on flag +0x174.
- +0x1a0 surface pointer; +0x1b0/+0x1b4 maxx/maxy; +0x1b8 bitsPerPixel; +0x1c0 maxColor;
  +0x1c4 maxPage; +0x1c8 bytesPerLine.
- +0x1fc pixel_format_t: 12 dwords {r, g, b, a masks; rPos, rAdj, gPos, gAdj, bPos, bAdj,
  aPos, aAdj}.
- +0x22c colour table; +0x238..+0x244 bounds; +0x248 access flags.
- +0x24c device-info pointer: +0x1c active page, +0x20 visual page, +0x30 setActivePage,
  +0x34 setVisualPage.
- +0x278 HDC; +0x380 DC type (0 fullscreen, 3 windowed, 4 memory).
- +0x38c..+0x438 driver vectors: 0x41c/0x420 bitBlt, 0x430 stretch 1x2, 0x434 stretch 2x2,
  0x438 generic stretch.

Mode numbers (from the `MGL_modeResolution` table). The groups are 320x200, 320x240,
320x400, 320x480, 400x300, 512x384, 640x350, 640x400, 640x480, 800x600, 1024x768, 1152x864,
1280x960, 1280x1024 and 1600x1200:

| Modes | Depth |
|---|---|
| 0-5 | 4bpp |
| 6-25 | 8bpp (6-9 are 320-wide, 10-14 repeat 320-wide modes with different drivers) |
| 26-40 | 15bpp |
| 41-55 | 16bpp |
| 56-70 | 24bpp |
| 71-85 | 32bpp |

Within each 15-mode group, 640x480 is at offset 8. Mode 86 (0x56) is windowed.

## Data formats touched here

- G24 tiles and CLUT pages: these confirm tools/gtafmt.py. Tiles are 64x64 with a 256-byte
  stride, 16 per 64 KB page. CLUTs are 64 per 64 KB page, with entries 256 bytes apart. No file
  parsing happens in this range.

## Porting priorities

- (a) Loading and drawing the city: `Poly_Init`, `Poly_SelectTile` + `Poly_RotateTile90`
  (or just rotate on the fly), `Poly_DrawFaceHoriz/Vert` + `Poly_FaceHoriz*/FaceVert*` (the
  core of every block face), `Poly_DrawQuad` + `Poly_Draw` + edge/span fillers (slopes),
  `Poly_BuildBlendTable`, `Poly_SetClip`, `Poly_SetScreenRows`. Write one 32bpp C version of
  each filler and drop the 15/16bpp variants.
- (b) Player on foot / in a car: `Poly_DrawSprite` / `Poly_DrawSpriteBlend` (rotated sprite
  quads) and `Poly_DrawRect` (HUD/scaled rects).
- (c) Frame loop: only the MGL present path matters. Use page flip
  (`setActivePage`/`setVisualPage`) or back-buffer blit/2x stretch (`bitBltCoord`,
  `stretchBltCoord`). Replace it with an SDL texture upload. Everything else in MGL can be stubbed.

## Open questions

- The exact meaning of polygon flag bits 0x04, 0x10 and 0x40, and of the +0x04 and +0x28
  descriptor fields. Only flags 2, 6 and 0x42 were seen in callers.
- Whether the blend paths are true 50% translucency or are used for shadows/"flat" lids. They
  take the blend table built with 0.5, so translucency is likely.
- DPLAYX ordinals: 1 = DirectPlayCreate and 2 = DirectPlayEnumerateA, inferred from call
  signatures and HRESULTs. Confirm against a dplayx.def if it matters.
- 0x491dfe and 0x49310b are entries Ghidra split mid-function. The real DDRAW init entry and the
  packed32 drawCursor / createSurface neighbours (0x48e2xx-0x490xxx) are not defined as
  functions. They are reachable only through driver vector tables in .data.
