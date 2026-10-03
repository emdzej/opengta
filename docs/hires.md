# The hires renderer (not part of the original)

An opt-in addition to the port, not a ported function: `--param hires=N` (N = 2, 3 or 4) draws the
in-game view again at 640N x 480N, with the same camera, the same visible area, the same faces in the same
order and the same occlusion, but with sub-pixel geometry and filtered true-colour textures. Without the
parameter (or with `hires=1`) nothing of it runs and the output is exactly the faithful renderer's: every
test and every gasm hash is unchanged. The code is in `src/render/hires/`; skins (replacement art) are
described in [Skins](/skins).

```sh
gasm-run opengta.wasm --asset-dir GTA --param intro=0 --param mission=1 --param hires=2
gasm-run opengta.wasm --asset-dir GTA --param front=0 --param hires=3      # the city viewer too
```

gasm's own display filter (`--filter`) applies to the presented frame afterwards. The frontend, menus and
the intro movie stay 640 x 480 (the platform scales them).

## What stays faithful

The faithful renderer keeps running every frame into its 640 x 480 back buffer, because rendering feeds
game state: `Render_QueueVisibleEntities` fills the sprite draw trees, `Sprite_DrawCached` updates the
corner caches in the entities, `Sprite_GetComposite` keeps its delta cache, and the HUD keeps blink
counters. The hires pass is display only: it reads `render_rects`, `render_cam`, the vertex grid's
inputs, the map, the style's tile tables and the draw trees, and writes nothing but its own frame and
texture caches. It never calls the faithful drawers (it doesn't touch `poly_clut`, the rotated-tile LRU
cache or the composite cache) and puts attached sprites in place on a copy.

The proof is `tests/hires_test.c`: mission 1 played by a key script (out of the start, into a car, down
the road; 1150 ticks, 384 frames) with `hires=1`, `2` and `4`, each in its own process, ends with the same
game state hash (every car, ped, object and player record, the RNGs, the mission) and the same CRC chain
of the faithful 640 x 480 frames; two `hires=1` runs are compared too, as a control. In the gasm module the
audio hash, which depends on the whole simulation, is the same with and without `hires`.

## How a frame is made

1. `Game_Render` as ported: `Render_QueueVisibleEntities`, `Render_DrawCity` into the back buffer.
2. At the start of `HUD_Draw` (`hud_pre_draw_hook`, the port's only hook in the HUD) the app calls
   `hires_frame_begin`: the hires city pass into the hires frame, then a copy of the faithful frame.
3. The faithful HUD draws into the back buffer as always.
4. At present (`Gfx_Present`), `hires_frame_end` takes every faithful pixel that differs from the copy
   (what the HUD drew) and lays it over the hires frame as an N x N block (nearest neighbour), and the
   hires frame is presented (`plat_present` with 640N x 480N; gasm allows up to 4096).

So the HUD is the faithful HUD, scaled. Blended HUD pixels (the arrows) come out as the faithful blend
with the 640 x 480 city below them. A HUD pixel of exactly the colour already below it is not seen and
shows the hires city there instead (the same colour). Like the original, nothing clears the frame:
pixels no face covers keep the previous frame (HUD included).

## The city pass (`hires_city.c`)

`Render_DrawCity` walked again: layers 5 to 0, each layer's top plane projected, the layer's sprites
(the same draw tree, `sprite_walk_level`), then its blocks from the edges of each visible rectangle
inward, four mirrored blocks at a time. The block drawers (`Render_DrawBlock`, the flat sides, the four
slope classes, NYC's `Render_DrawFlatAbove`) follow `city.c` line by line with one change: every corner
carries two values,

- `f`, the faithful renderer's integer screen position (the vertex grid of `Render_ProjectLayer` with its
  per-plane rounding, and `Sprite_WorldToScreen`), which every visibility test and choice uses
  (`L.sx < U.sx` for a side, the flat block's side order, the polygon drop tests, sprite culling, the
  raw-or-composite choice), so the same faces are drawn in the same order;
- `h`, the exact projection `centre + (p - camera) * scale / depth` times N, in 1/256 of a hires pixel,
  which is drawn.

Tiles, CLUTs, rotations, flips and remaps come from the same tables (`side_remap` / `lid_remap`,
`side_clut` / `lid_clut`, the ext bits, the type map's rotation); tile animation therefore follows
without anything to invalidate. Sprites use the same corners (`sprite_get_corners`), palette
(`sprite_remap_clut`, the pure half of `Tile_SelectSpriteRemap`), blend flag and delta mask (applied to a
copy of the graphic).

## The rasteriser (`hires_raster.c`)

True colour in the presentation byte order (R, G, B, A in memory), integer arithmetic only (the same
output on every runner). Pixels are covered when their centre is inside the shape, so neighbouring faces
meet without the faithful filler's inclusive edges.

- Face trapezoids (`hr_face_horiz` / `hr_face_vert`): the mapping of `Poly_FaceHoriz32` /
  `Poly_FaceVert32` (u along the edges, v from edge a to edge b, linear in screen space: affine, like
  the original), oriented by exactly the bit logic of `Poly_DrawFaceHoriz` / `Poly_DrawFaceVert`
  (rotation 180/270 mirrors and swaps unless flipped, FaceVert adds 90 degrees and trades flip and
  mirror); 90/270 degree faces sample the tile turned the way `Poly_RotateTile90` turns it.
- Polygons (`hr_polygon`): per-vertex texture coordinates interpolated along the edges and across the
  rows (slope sides, sprites). They are drawn only with the faithful winding (`Poly_Draw`'s spans run from
  edge B to edge A, so the other winding draws nothing), decided on the faithful coordinates.
- Sampling: bilinear, clamped to the texture's own edges, so a tile never bleeds into its neighbours in
  the 256 x 256 page (the faithful filler's 8-bit wrap does read them). Texel 0 has alpha 0: opaque faces
  draw its colour as the original does, transparent faces (the flat bit) and sprites skip it with
  premultiplied alpha at its edges, blended sprites and quads average with the screen (the 50 % blend
  table) where they cover it.

Slope quads give u/v as texel indices 0..63 (`m * 64 / n - 1` and so on); the hires pass maps index i to
`i * 64 / 63` so that 0 and 63 land on the tile's edges (the faithful filler samples texel centres, half a
texel in).

## Textures (`hires_tex.c`)

The 8-bit tiles and sprites are converted through their CLUTs once and cached: tiles by (texture tile,
CLUT pointer), sprites by (sprite number, CLUT pointer, delta mask). A CLUT pointer is the faithful
table's entry, so an animation step or a remap selects another entry rather than changing one. The caches
start over when the style or its pixel format changes, or when they fill up (3072 tiles, 1536 sprites).

Every lookup goes through an overlay stack first (`hires_tile`, `hires_sprite`, `HiresOverlayFn`):
skin n, ..., skin 1, then the original art (`hires_tile_original`). This is where replacement texture
packs plug in ([Skins](/skins)), and where an upscaler would: a layer that answers with an upscaled copy
of the original (for example an xBR / ESRGAN-style pass over `hires_tile_original`'s texels) needs no
other change.

## Performance

`gasm-run` (macOS universal, Apple silicon) headless, mission 1 with the "driving" key script, 1150 calls
at 70 Hz (a game frame every third call, so realtime is 70 calls/s, i.e. 23.3 frames/s), including gasm's
hashing of every presented frame:

| hires | presented | calls/s | game frames/s |
|---|---|---|---|
| 1 (off) | 640 x 480 | 923 | ~308 |
| 2 | 1280 x 960 | 124 | ~41 |
| 3 | 1920 x 1440 | 56 | ~19 |
| 4 | 2560 x 1920 | 32 | ~10.5 |

Natively (`tests/hires_test.c`, arm64 -O2) the hires pass takes about 9-10 ms per frame at 2x and 35 ms
at 4x. So 2x has headroom; 3x and 4x run below realtime in wasm. The browser runs the same wasm at
roughly the speed of the Node runner, plus the upload of a larger frame each presented frame, so 2x is
the practical setting there. Costs are dominated by overdraw (lower layers' faces under higher ones are
drawn, as in the original) and the scalar bilinear filter; wasm SIMD, skipping fully hidden faces, or a
GPU path are the obvious next steps.

## Not done

- The frontend, menus, intro movie and the HUD itself stay at 640 x 480 (the HUD is scaled, not redrawn).
- Sprite positions are truncated to whole world pixels as the faithful renderer truncates them, so
  motion is no smoother than the original's.
- Faces wider than the original's grid (views beyond the camera's normal range) are dropped rather than
  spilling into the next grid column as the faithful grid does.
- No mipmaps for the original art (it is only ever magnified at the camera's heights); skins are box
  filtered at load instead.
- New art packs beyond the skin loader, and upscalers: see [Skins](/skins) and the overlay hook above.
