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

With hires on, the HUD, the menus, the cutscene stills and the intro movie are drawn at 640N x 480N too
([HUD and menus](#hud-and-menus)). `--param upscale=xbr` (or `scale2x`, `scale4x`, `xbr4`) upscales the
original art once at load ([Upscaling](#upscaling)), and `--param skin=...` layers replacement art over it
([Skins](/skins), [Creating a skin](/howto/create-a-skin)). gasm's own display filter (`--filter`) applies to
the presented frame afterwards.

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
2. At the start of `HUD_Draw` (`hud_pre_draw_hook`) the app runs the hires city pass into the hires frame.
3. The faithful HUD draws into the back buffer as always; hooks next to its drawing calls record what it
   draws (glyphs, HUD sprites, arrows), where and with which palette, while a hires frame is recorded.
4. At present (`Gfx_Present`), the recorded HUD is redrawn into the hires frame at N times every position
   ([HUD and menus](#hud-and-menus)), and the hires frame is presented (`plat_present` with 640N x 480N; gasm
   allows up to 4096). The frontend and the intro movie present the same way from their own recorded draws.

Like the original, nothing clears the frame: pixels no face covers keep the previous frame. (The city
viewer and `tests/hires_test.c` still use the older composite: a copy of the faithful frame before the HUD
and every changed pixel laid over the hires frame as an N x N block.)

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
skin n, ..., skin 1, then the original art (`hires_tile_original`). This is where skins plug in ([Skins](/skins)); the
upscaler ([Upscaling](#upscaling)) transforms the original-art layer once, after a sprite's damage and door
deltas are applied, so skins still win and are never upscaled.

## HUD and menus

With `hires=N` (or skins at `hires=1`) the HUD is drawn at 640N x 480N over the hires city, and the
frontend (menus, cutscene stills, results, credits) and the intro movie are presented at 640N x 480N.
The ported HUD and frontend code runs unchanged: it keeps its own state (blink counters, popup ages,
pager scroll, menu marker frames, logo clock) and still draws its faithful 640 x 480 frame, which is
what the gasm hash covers without `hires`. Each of its drawing calls has a hook next to it that, while a
hires frame is being recorded (`hires_ui_recording`), notes what was drawn, where, and with which
palette. The list is then replayed at N times the resolution.

- Recorded draws (`hires_text.c`): glyph blits of the font renderers (`Font_DrawString` and its
  variants, `HUD_DrawText`, `HUD_DrawTextClipped`, `HUD_DrawTextMultiline`, `HUD_DrawCenteredLine`, the
  HUD's own score and lives digits with their roll offsets), including the clipped parts of glyphs (the
  pager window, the credits' scrolling lines); the score popups' stretched digits (`Poly_DrawRect`);
  HUD sprites (`Sprite_DrawScreen`: pager, light, icons, cop heads, zone signs, subtitle icon); the
  arrows and the roof marker (`Sprite_Draw`); frontend pictures (`Gfx_BlitImage`, the backdrop's row
  copies, the cutscene still); the movie frame (`hires_front.c`, `hires_hud.c`).
- Layout: every position is the faithful one times N, so the layout is exactly the original's. With
  the nearest filter every pixel the faithful HUD draws comes out as an N x N block of its colour,
  except where the HUD blends (below).
- Glyphs and HUD sprites (pixel art): scaled by `hires_ui=` (launch parameter): `scale` (default),
  `nearest` or `bilinear`. `scale` is the Scale2x / Scale3x family (EPX; 4x is Scale2x twice), run on
  the 8-bit indices before the palette: diagonal edges of glyphs and icons are rounded where equal
  colours meet, colours are never mixed. Compared in `out/hires_ui/hud_*.png` and
  `front_*_filters.png` (faithful | nearest | bilinear | scale): nearest is the scaled HUD of the
  POC; bilinear blurs the 1- and 2-pixel strokes of the HUD fonts and the gradient fonts' outlines;
  scale keeps them sharp and takes the staircase off curves and diagonals (digits, the key, the vest,
  the menu font's outline). The scaled copies are cached by (pixels, palette contents, filter, N).
- Arrows and the roof marker: drawn like the city's sprites (`hr_polygon`, bilinear, blended with the
  50 % blend where the faithful one blends), with `Sprite_Draw`'s centre projected exactly at N x and
  the rotated corners added at N x; the faithful corners decide culling and winding. They blend with
  the hires city below them, so they differ from the faithful blend by design.
- Pictures (backdrop, logo frames, F_PLAY portraits, the Rockstar logos, cutscene stills): scaled once
  per picture with a separable Catmull-Rom (bicubic) filter, integer arithmetic, and cached until the
  picture is reloaded (`Gfx_LoadRawImage` tells the cache) or the game starts (the cache and the
  frontend frame are freed while a level runs).
- The intro movie: its 320 x 200 frame is filtered each frame to (2N x 320) x (2N x 200) at row 40N,
  bicubic at 2x and bilinear at 3x and 4x (a frame costs too much in wasm otherwise); the rest of the
  screen is the movie palette's colour 0, as in the faithful frame.
- Skins can replace glyphs and pictures ([Skins](/skins), "Fonts and pictures"); HUD sprites come from
  the skins' sprite layer like the city's (a skin sprite whose image has exactly the original's size is
  taken for the original and drawn with the pixel-art filter).
- Quirks: the faithful blitters write a glyph that runs past the end of a row onto the next row; the
  hires replay clips it at the screen edge instead. Pixels no draw covers keep the previous hires frame,
  as the faithful frame does.

The proof is `tests/hires_ui_test.c`: hud_test's HUD states at 2x and 3x and without hires, each in its
own process, end with the same game state hash and the same faithful frames; at nearest, of the pixels
the faithful HUD drew, 0.1 to 1.6 % are not exactly N x N blocks of their colour (the blended arrows
and the marker over the player, the popups' stretched digits); the frontend script (intro movie,
start menu, options, player select, city select, the cutscene still and results after a "game",
credits) at 2x and 4x leaves the faithful frames unchanged and the hires frame box-filtered back to
640 x 480 within a mean of 3.2 per channel of the faithful one (the bicubic filter's ringing on the
pictures). In the gasm module `hires=2` prints the same audio hash as without it, and gasm-run and the
Node runner the same video hash.

Cost: the HUD replay is under 0.5 ms a frame natively at 3x. The frontend at 4x presents a 2560 x 1920
frame every 35 ms: gasm-run (headless, with its hashing) runs the menus at 1.1x realtime, the movie at
0.6x (2x: 1.8x); the bicubic pictures are made once (the first frame of each logo frame at 4x takes a
few tens of ms).

## Performance

`gasm-run` 0.6.0 (macOS universal, Apple silicon) headless, mission 1 with the "driving" key script
(`tools/screenshots.sh`), 1150 calls at 70 Hz. A game frame comes every third call, so realtime is 70
calls/s (23.3 game frames/s). With gasm's hashing of every presented frame (the default headless run),
and with `--no-hash`, which is closer to playing in a window:

| hires | presented | calls/s, hashed | calls/s, `--no-hash` | before (hashed / `--no-hash`) |
|---|---|---|---|---|
| 1 (off) | 640 x 480 | ~1080 | ~2750 | the same |
| 2 | 1280 x 960 | ~254 | ~658 | 137 / 201 |
| 3 | 1920 x 1440 | ~126 | ~363 | 62 / 93 |
| 4 | 2560 x 1920 | ~73 | ~223 | 36 / 53 |

So every scale now runs at least realtime in wasm, without SIMD or threads. The hashed 4x figure is
mostly gasm's: hashing a 2560 x 1920 frame costs about 27 ms, so even with no hires pass at all a
hashed 4x run is capped at about 100 calls/s. The hires pass itself is about four times faster than
before: at 4x about 12 ms a frame in wasm (55 ms before) and 8.5 ms natively (36 ms before; arm64,
`tests/hires_perf_test.c`).

`tests/hires_perf_test.c` is the harness: the same mission and key script in a process per scale, the
time of each stage of a frame (the game step with the faithful renderer and HUD, the hires city pass,
the HUD layer), the rasteriser's counters, and a check against reference frames (`HIRES_PERF_REF=1`
writes them to `out/hires/perf/`, later runs report how many pixels changed and by how much). What it
showed, and what was done:

- Overdraw is small: the faces drawn cover 1.17 screens a frame (1.08 opaque, 0.09 keyed sprites and
  flat faces). Skipping hidden faces could save at most about 8 %, so it was not done.
- Almost all of the time went into the per-pixel bilinear filter (four texel reads and three blends
  per pixel, each blend four multiplies).
- Every face trapezoid has one texture coordinate that is constant along a span (unrotated faces: v;
  90 and 270 degree faces: u). Those spans now filter the texels they cross once, across the constant
  coordinate, into a line (0.16 screens of texels a frame against 1.17 screens of pixels), and each
  pixel blends two neighbours of that line. That is one blend per pixel instead of three. Polygons
  (slopes, sprites) keep the general filter.
- A blend is one 64-bit multiply: the four bytes are spread to 16-bit lanes and each lane's difference
  is multiplied in place. This gives exactly the result per byte (checked exhaustively). Line entries
  are kept as (entry, difference to the next) pairs in that form.
- The opaque line loop is unrolled four times. In wasm that is about 5 % faster; natively it makes no
  difference.
- Tried and dropped: walking the texels with the two neighbours kept in registers (more branches;
  slower natively), and setting the opaque alpha in the line (twice as slow under gasm's JIT).

Image change: line spans filter across first and then along the span, where the general filter does it
the other way round. Rounding therefore differs: in mission 1, 23 % of the pixels differ from the
previous renderer by 1 to 3 in a channel, never more, and nothing is visibly different. The hires frames
are still identical between runners (integer arithmetic only), and the faithful frames, audio and game
state are unchanged (`tests/hires_test.c`; gasm `--asset-dir installer` without hires still prints
`video_fnv32=67c465c2 audio_fnv32=15edf4c8`).

The browser runs the same wasm at about the speed of the Node runner, plus the upload of the bigger
frame. Next steps would be wasm SIMD (four lanes per blend in one instruction) or a GPU path.

## Upscaling

`--param upscale=<name>` (an option of this port, off by default) upscales the original tiles and
sprites once, when they are converted to true colour (`hires_upscale.c`, called from `hires_tex.c`'s
conversions). The cache keeps the upscaled copy, and the renderer's bilinear filter draws from it. It
replaces the original art's layer of the overlay stack, so it sits below every skin: a skin's image
still wins, and skin images are never upscaled. Sprites are upscaled after their deltas (damage,
doors) are applied, and each delta mask is cached separately as before. The same texels come out on
every runner (integer arithmetic).

| name | what | texels | 4x city pass, wasm `--no-hash` |
|---|---|---|---|
| `none` | the original art (default) | 64 x 64 a tile | 222 calls/s |
| `scale2x` | Scale2x / AdvMAME2x: only copies texels, so transparency stays exact | 128 | as `xbr` |
| `scale4x` | Scale2x twice | 256 | as `xbr4` |
| `xbr` | xBR level 2 at 2x: blends along the edges it finds (recommended) | 128 | 191 calls/s |
| `xbr4` | xBR level 2 twice | 256 | 148 calls/s |

Both are written from the algorithms' public descriptions. In xBR, colour distances are taken in YUV
with luma weighted most, plus the alpha difference. Blends weight colour by alpha, so the colour of
transparent texel 0 does not bleed into a sprite's visible edge. On opaque faces, where the original
draws texel 0's colour, an edge texel that was half texel 0 shows the other colour with half alpha.

Comparisons are in `out/hires/upscale_tiles.png` (the top-left quarter of seven tiles at 8x: original
nearest, bilinear, scale2x, xbr) and `out/hires/upscale_mission_<present>_<name>.png` (the middle of
the 4x mission-1 frame for each name), written by `tests/hires_perf_test.c`. The textures are fairly
noisy and photographic, so the scalers change little on them. They change sprites and clean-edged
art more: car outlines, windscreens and the flames on the fire station sign get smooth edges with
`xbr`. Scale2x makes a noisy texture stair-stepped (worst with `scale4x`). `xbr4` makes it look
painted. `xbr` is the sane choice and costs about 15 % of the 4x pass (more texels per span line).
Upscaling costs about 0.2 ms a tile natively for `xbr` and 0.02 ms for `scale2x`, once per tile and
CLUT. Mission 1 uses about 140 tiles, 9 MB at 2x and 35 MB at 4x upscaling.

## Not done

- Sprite positions are truncated to whole world pixels as the faithful renderer truncates them, so
  motion is no smoother than the original's.
- Faces wider than the original's grid (views beyond the camera's normal range) are dropped rather than
  spilling into the next grid column as the faithful grid does.
- No mipmaps for the original art (it is only ever magnified at the camera's heights); skins are box
  filtered at load instead.
- At 4x the intro movie runs below realtime in wasm, and a hashed headless 4x run is capped near
  realtime by the runner's frame hashing (not by the renderer).
- A skin HUD sprite of exactly the original's size is taken for the original (drawn with the pixel-art
  scaler instead of from the skin).
- The browser player stores skins in its own storage (OPFS); bundles and gasm-run take a skins folder
  with `--asset-dir skins=<folder>`.
