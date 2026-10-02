# Sprites

How GTA 1 (2002 Windows build) stores, composes and draws sprites: cars, peds, objects, HUD markers.
The port lives in `src/render/sprite.c` (the sprite module 0x47bb10-0x47cd00, the delta overlays
0x414310-0x414590, `Tile_SelectSprite*` 0x4385b0/0x4385e0, `Render_QueueVisibleEntities` 0x437000),
`src/render/drawlist.c` (0x480ed0-0x480fd0) and `src/render/poly_sprite.c` (`Poly_DrawSprite` 0x49787c,
`Poly_DrawSpriteBlend` 0x4979c9, `Poly_DrawRect` 0x49806a). Addresses are the original's. The block
renderer is in [render.md](render.md) and the file format in [formats.md](formats.md).

## Style sections

`Style_Load` 0x47cf10 allocates one 64 KB aligned sprite buffer (0x7750b0) of
`0x10000 + graphics + info + numbers` bytes and reads the three sprite sections into it: graphics at
+0x10000, info right after them, numbers after the info. The first 64 KB page is the work area of the delta
composite cache. `Sprite_LoadInfo` 0x47ca50 then runs on it (before `Obj_LoadInfos` and `CarInfo_Setup`,
which need the group bases).

**Sprite info**: records of `12 + 6 * deltas` bytes, one after another:

| Offset | Size | Field |
|---|---|---|
| 0 | u8 | width |
| 1 | u8 | height |
| 2 | u8 | delta count (at most 17 in the shipped styles) |
| 3 | u8 | padding |
| 4 | u16 | size, w * h |
| 6 | u16 | palette, relative to the sprite palette base |
| 8 | u32 | offset of the graphic in the sprite graphics |
| 12 + 6i | u16, u32 | delta i: stream size, offset in the sprite graphics |

`Sprite_LoadInfo` puts a pointer to every record into the table at 0x773e38 (0x424 entries, the rest
cleared; the count goes to 0x774ee4 as a short) and adds the graphics address to the graphic and delta
offsets in place. STYLE001 has 1,034 records, STYLE002 1,037, STYLE003 1,049.

**Graphics**: 64 KB pages of 256-byte rows, as the tiles. A graphic at offset `o` is texel `(u, v)` =
`(o & 0xff, (o >> 8) & 0xff)` of page `o >> 16`. The rasteriser receives that single pointer and splits it
the same way, which only works because the buffer is 64 KB aligned.

**Deltas**: a stream of `{u16 skip, u8 n, n bytes}` records patching a copy of the graphic (256-byte rows):
skip bytes forward from the end of the previous run, then copy n bytes (`Blit_ApplyDelta` 0x48958a).
Cars use them for damage and opening doors.

**Sprite numbers**: 21 u16 counts, one per group. `Sprite_SetGroupBases` 0x47cbd0 turns them into
cumulative bases (shorts):

| # | Group | Base | STYLE001 count |
|---|---|---|---|
| 0 | arrow (HUD icons) | 0x774efa | 49 |
| 1 | digits | 0x774f0a | 0 |
| 2 | boat | 0x774efc | 1 |
| 3 | box | 0x774eee | 0 |
| 4 | bus | 0x774ef0 | 6 |
| 5 | car | 0x774ee8 | 27 |
| 6 | object | 0x774f0c | 562 |
| 7 | ped | 0x774ef8 | 295 |
| 8 | speedo | 0x774f10 | 0 |
| 9 | tank | 0x774efe | 2 |
| 10 | traffic lights | 0x774eec | 6 |
| 11 | train | 0x774eea | 5 |
| 12 | train doors | 0x774f0e | 0 |
| 13 | bike | 0x774ef6 | 26 |
| 14 | tram | 0x774f04 | 0 |
| 15 | wbus | 0x774f06 | 0 |
| 16 | wcar | 0x774f02 | 7 |
| 17 | ex (explosions) | 0x774ef4 | 48 |
| 18 | tumcar | 0x774ef2 | 0 |
| 19 | tumtruck | 0x774f00 | 0 |
| 20 | ferry | 0x774f08 | 0 |

The counts add up to the record count. `CarInfo_Setup` 0x40c100 adds a base to each car record's sprite
number by vtype: 0 and 2 bus, 1 and 4 car, 3 bike, 8 train, 9 tram, 13 boat, 14 tank.

## Palettes

The palette index (u16 per logical palette) holds, in order: 4 per tile (4 * all tiles), the sprite
palettes (base 0x77531c), the car remaps (base 0x77530c = (tile + sprite CLUT size) / 1024), then the
fonts (0x7752f8). `Tile_SelectSpriteRemap` 0x4385e0 (clut, remap, palette) sets the rasteriser's CLUT
(0x78c10c) to

- remap 0: index[sprite base + clut], the sprite's own palette;
- remap r: index[palette + r - 1] (16-bit sums), where `palette` is the sprite's +0x12.

Cars set +0x12 to `car base + record * 12` (`Car_Create`, 4771 in the dump): 12 remaps per car info
record. Some records have 12 identical entries (STYLE001 record 5, an emergency vehicle). Peds use
0x7750d0 = `car base + car records * 12` (set in `Style_Load` after `CarInfo_Setup` counted the records),
so ped remaps follow the car remaps. HUD sprites use the first font palette + 1. `Tile_SelectSprite`
0x4385b0 takes a palette index entry directly.

## The sprite object (0x5c bytes)

Every drawable entity embeds one: cars at +0x250 (0x4be498 + n * 0x2b0), peds at +0x90, objects at
+0x2c. `Sprite_Init` 0x47c9f0 (x, y, z, angle, frame) sets the fields below, the depth key to z, and
clears remap, palette, blend, the attached chain, the corner cache's info pointer and the delta mask.

| Offset | Size | Field |
|---|---|---|
| +0x00 | s32 x3 | x, y, z (16.16 world; only the integer parts are projected) |
| +0x0c | s32 | depth key: the draw tree (key >> 22) and the order in it |
| +0x10 | u8 | remap (0 none) |
| +0x12 | s16 | remap palette base |
| +0x14 | u8 | blend flag (`Sprite_SetBlend` 0x47c9e0) |
| +0x16 | u16 | sprite number (`Sprite_SetFrame` 0x47c960 also clears the deltas) |
| +0x18 | s16 | angle 0..1023 |
| +0x1a | s16 | angle of the cached corners |
| +0x1c | u32 | delta mask |
| +0x20 | 8 | not used by the sprite code |
| +0x28 | s32 x8 | cached rotated corner offsets |
| +0x48 | ptr | sprite info record (0x773e38[frame]) |
| +0x4c | ptr | info record of the cached corners |
| +0x50 | ptr | next attached sprite |
| +0x54 | u16 | saved frame (`Sprite_SaveFrame` 0x47c9a0) |
| +0x58 | u32 | saved delta mask (`Sprite_SaveDeltas` 0x414390) |

`sprite.h` mirrors it (exact size and offsets with 32-bit pointers, i.e. in the gasm build).

## Deltas and the composite cache (0x414310-0x414590)

`Sprite_AddDelta` 0x414310 and `Sprite_ToggleDelta` 0x414330 set or flip bit n of the mask if the
sprite has a delta n; `Sprite_RemoveDelta` 0x414360 and `Sprite_ClearDeltas` 0x414380 don't check.

`Sprite_GetComposite` 0x4143a0 returns the graphic to draw. With no deltas it is the raw graphic. Else
it uses a cache of 10 slots at 0x502e88 (`{buffer, info, mask, stamp}`, stamp counter 0x502f28), built by
`SpriteCache_Init` 0x414590 in the first page of the sprite buffer: slots 0-7 are 64 x 64 (u 0/64/128/192
of rows 0 and 64), slots 8-9 128 x 128 (u 0/128 of row 128). Sprites of up to 0x1000 texels use 0-7, larger
ones 8-9. In its range:

1. a slot with this sprite and exactly this mask is used as is;
2. otherwise the **last** slot with this sprite and a mask m with `m < mask` (signed) and
   `(mask - m) | m == mask` gets the deltas of `mask - m` applied on top;
3. otherwise the slot with the smallest stamp (first one on ties) gets a copy of the raw graphic
   (`Gfx_CopyRect256` 0x47cb90) and all the deltas, lowest bit first, skipping empty ones.

The slot takes a new stamp, the info pointer and the mask. Removing a delta never matches step 2, so it
always rebuilds.

## Drawing

### Corners

A sprite of w x h has half extents `hw = (w - 1) * 0x8000` and `hh = h * 0x8000` (16.16). The corners,
y up, are (-hw, hh) top left, (hw, hh) top right, (-hw, -hh) bottom left, (hw, -hh) bottom right. At
angle 0 they are used as they are. Otherwise each corner's integer parts (`>> 16`, rounding toward minus
infinity, so an even width loses half a pixel on one side) are multiplied by the 16.16 sine s and cosine
c of the angle (0x511e28, cos = sin + 256 entries): `x' = c x - s y`, `y' = s x + c y`. Angle 0 has the
graphic's top toward -y (north).

### `Sprite_DrawCached` 0x47c130 (the world sprites)

The corners are cached in the sprite (+0x28) and recomputed only when the angle (+0x18 vs +0x1a) or the
info record (+0x48 vs +0x4c) changed. Every corner is projected separately with the formula of
`Sprite_WorldToScreen` 0x47bb10: world point `(x & 0xffff0000) + cx`, `(y & 0xffff0000) - cy`, depth
`(s16)(z >> 16) + camera height`. So sprites scale with perspective: higher things look bigger.

### `Sprite_Draw` 0x47bc00 (screen-sized)

The same corners, but only the centre is projected and the corner offsets are added in screen pixels
(`x + cx >> 16`, `y - cy >> 16`): the sprite keeps its pixel size at any camera height. Used by the HUD
for markers and arrows (0x77ed48, 0x784658, 0x7846c0, 0x784770).

### Common tail

- Cull: drawn only if the corners' bounding box overlaps 0..w-1, 0..h-1 of the render camera
  (0x5c0c00, 0x5bfab0).
- Palette: `Tile_SelectSpriteRemap(info clut, remap, palette)`.
- Graphic: if `|y3 - y0| + |x3 - x0| < 10` (corner 0 to corner 3) the raw graphic, otherwise
  `Sprite_GetComposite`. Distant or tiny sprites therefore show no damage.
- Rasteriser: `Poly_DrawSpriteBlend` if the sprite's blend flag and the option 0x5031e4 (from the
  frontend option 0x51029f, on by default) are set, else `Poly_DrawSprite`. The vertices go in the order
  top right, top left, bottom left, bottom right.

### `Poly_DrawSprite` 0x49787c / `Poly_DrawSpriteBlend` 0x4979c9

They build a 4-vertex polygon descriptor for `Poly_Draw` 0x496cf0 (see render.md): flags 2 (textured,
texel 0 skipped) or 0x42 (blended with the 50% table, texel 0 skipped), texture page = graphic pointer &
~0xffff, and per vertex u/v bytes from the pointer's low bytes u0, v0: `(u0 + w - 1, v0)`, `(u0, v0)`,
`(u0, v0 + h - 1)`, `(u0 + w - 1, v0 + h - 1)`, wrapping in 8 bits. The walk direction is -1 and a quad
without height is dropped, as for `Poly_DrawQuad`.

### `Poly_DrawRect` 0x49806a / `Poly_RectFill32` 0x49a539

An axis-aligned scaled image for the score popups (`HUD_DrawScorePopups` 0x481620, font glyphs): screen
rectangle x0..x1, y0..y1, texels (0, 0)..(umax, vmax) of a **packed** image (stride umax + 1), current CLUT,
texel 0 transparent. It is rejected when it misses the clip rectangle. The filler works per texel:

- x starts at `x0 << 16 + 0x8000` and steps `dx = (x1 - x0 << 16) / (umax + 1)` per texel, one pixel per
  texel. A rectangle wider than the image leaves gaps, a narrower one overwrites pixels;
- every row restarts at the integer part of the start x;
- y steps `(y1 - y0 << 16) / vmax` per texel row (vmax, not vmax + 1: vmax = 0 would divide by zero),
  from `y0 << 16 + 0x8000`, while y <= `min(y1, clip y1) << 16 + 0x8000`, with a first test using <;
- the clips count texels one at a time: the right clip counts from 1 while `x + k dx < (clip x1 + 1) << 16`;
  the left one skips texels while `x + k dx < clip x0 << 16`; the top one skips whole texel rows.

### `Sprite_DrawScreen` 0x47bbc0

The sprite unrotated with its own palette, top left at (x, y), through `Blit_Tile32` 0x4894a1 (unclipped).
The HUD uses it.

## Depth sorting: the draw trees

`Sprite_LoadInfo` creates six trees, one per map layer (roots at 0x774ec8). `DrawList_*`:

- nodes `{item, key, greater, less-or-equal}` of 16 bytes: 6 roots at 0x77d480, then a pool of 300 nodes at
  0x77d4e0 (counts 0x77d478 nodes, 0x77d47c roots). A root has key 0x7fffffff and no item;
- `DrawList_Insert` 0x480f60 walks down (to `greater` if the node's key is below the new key, else to
  `less-or-equal`) and adds a leaf. Past 300 nodes in all, inserts are ignored: the sprite is not drawn;
- `DrawList_Walk` 0x480fd0 is a reverse in-order walk with an explicit stack (0x774f20): the largest key
  first, equal keys in insertion order;
- `DrawList_Clear` 0x480ef0 (also inlined as 0x47c020) empties every tree; `Game_Frame` 0x430b20 calls it.

`Sprite_Queue` 0x47c940 inserts a sprite into tree `key >> 22` with its depth key (+0xc). World z grows
downward, so lower sprites are drawn first. A key outside 0..6 * 0x400000 would index past the roots.

`Sprite_DrawLevel` 0x47c030 walks tree z with the callback at 0x47c050. Ghidra has no function there,
so it is read from the disassembly. The callback draws the sprite with `Sprite_DrawCached`, then each
sprite in its `+0x50` chain (a list, not recursive), each placed relative to the parent for the draw and
restored after:

- the child's (x, y) offset is rotated by the parent's angle if that angle is non-zero (integer parts times
  sine/cosine, as for corners);
- x = parent x + x', y = parent y - y', z = parent z - child z;
- angle = (parent + child) & 1023.

## Queueing the visible entities (`Render_QueueVisibleEntities` 0x437000)

`Game_Render` 0x430d40 calls it when sprites are on (0x5031a4), then `Render_DrawCity` and `HUD_Draw`. It
takes the local player's view rect (`Player_GetViewRect`, +4 of the player record: left, right, top,
bottom in world pixels). It walks the collision grid at 0x5278f8 (128 x 128 cells of 128 pixels, each a list
of `{u8 kind, ..., owner +8, next +0xc}`) over cells `(left >> 7) - 1 .. (right >> 7) + 1` by
`(top >> 7) - 1 .. (bottom >> 7) + 1`, clamped to 0..127. Columns are the outer loop, rows the inner. It
queues, per kind:

| Kind | Sprite |
|---|---|
| 1 | ped + 0x90 |
| 3 | object + 0x2c, unless the object type's byte +0x12 is 3 (invisible) |
| 6 | car + 0x250 |
| 0x1e | owner + 0x34 |
| 0xc, 0xd | the owner (a sprite) |
| 7, 8, 10, 0xe, 0x13 | the owner, only with the trains/lights switch 0x502f48 |

The port puts this behind `SpriteWorld` (`sprite.h`): the entity side provides the cell walk and the
embedded-sprite lookup.

## Interleaving with the city

`Render_DrawCity` 0x4389f0 handles layer z = 5 (lowest) up to 0 like this: project the layer's top plane,
call `Sprite_DrawLevel(z)` if sprites are on, then draw the layer's blocks. A ped standing on the street
(lid of layer 4) has z just above plane 4 (`Map_GetGroundZ` gives 3 * 0x400000 + 0x3f0000), so it is in
tree 3. It is drawn after every block of layer 4 and below, then layer 3's blocks are drawn over it, which
includes the walls of buildings next to it, even the ones behind it from the camera's view.

## Port notes and deviations

- `poly_sprite.c` has its own copy of `poly.c`'s polygon filler and of the target state (scanline table,
  clip, blend table), because `poly.c` keeps them static. Callers set both
  (`poly_sprite_set_screen_rows` / `_set_clip` / `_set_blend`) until `poly.c` exposes them.
- `city.c` does not call `sprite_draw_level` yet. `tests/sprite_test.c` draws the six levels after the city,
  so its frames show sprites over buildings that would hide them in the original.
- Out-of-range reads in the original are guarded: angles outside 0..1023 (masked), depth keys outside the
  six trees (dropped), palette index entries past the index (entry 0), writes off the screen in the
  rect filler and `Sprite_DrawScreen` (dropped), `vmax` = 0 in `Poly_DrawRect` (nothing drawn, the
  original faults). A sprite with no info record is not drawn (the original dereferences NULL).
- Not compared against frames captured from the original yet.
