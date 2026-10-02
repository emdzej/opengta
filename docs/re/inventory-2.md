# Binary inventory, part 2: 0x418390 - 0x43ce90

Covers 300 functions (names in `tools/ghidra/names.tsv`). Addresses are VAs in `gta.exe`. Units used below:

- **World coordinates** are 32-bit fixed point. One block is `0x400000` (64 px × 0x10000), so `coord >> 22` gives the block index and `coord >> 16` gives pixels.
- **Angles** run 0..1023 (`0x3ff` mask). 0 = facing +y, 0x100 = +x, 0x200 = -y, 0x300 = -x.
- **Road direction bits** (low nibble of `type_map`): 1 = -y ("up"), 2 = +y, 4 = -x, 8 = +x. The angle mapping is in `Traffic_AngleToDir` 0x418390 and in the reverse switches inside `Sentinel_WarpCar`.

**Correction to the anchors in the brief.** `Error_GetMessage 0x422b90` is not the mission.ini/map loader. It is `Error_GetMessage`: a big switch that maps an error code to a text string. It references every error string in the binary, which is why it seemed to "load maps". The real loaders are:

- `Map_Load` 0x438200 (in this range).
- The mission.ini parser at 0x44ab90 (another range). It calls the `TextFile_*` helpers listed here and `Map_SetName` 0x4381c0.
- `Style_Load` at 0x47cf10 (another range). It uses `Mem_AllocAligned64K` and the `Tile_Build*` helpers listed here.

## Modules (object-file runs)

| Span | Module | Purpose / key functions |
|---|---|---|
| 0x418390-0x418f80 | traffic (tail; the module starts before 0x418390, around 0x417b70 `Car_AllocFree`) | Generates traffic around each player's view rectangle. Key functions: `Traffic_SpawnAroundView` 0x4183d0, four edge scanners 0x418970/0x418b40/0x418c80/0x418dc0, `Traffic_InitModelTables` 0x418f80 (3 rows × 100 car models, shuffled). |
| 0x419000-0x4227a0 | sentinel (AI drivers for emergency vehicles) | The binary calls these AI driver records "sentinels" (error string: "Illegal request for a sentinel"). Key functions: `Sentinel_DriveCar` 0x41aed0 (main step, called from `Car_UpdateAll` 0x40adc0), `Sentinel_Steer` 0x41f290, `Emergency_UpdateAll` 0x419880 (the decompiler fails on it; read the disassembly), `Map_FindNearestRoad` 0x41a490, `Sentinel_WarpCar` 0x419000, traffic-light override 0x41e1c0/0x41e140, ambulance call queue 0x41a1a0-0x41a470, police chaser slots 0x4196d0/0x419740. |
| 0x4228b0-0x425170 | error | `Error_Fatal` 0x422900: shuts down via `Sys_Shutdown` 0x4371e0, then shows "Error <code>.<module>" plus the message. `Error_GetMessage` 0x422b90. `Error_SetFileName` 0x4228c0 sets the file name used in file-error messages. |
| 0x425170-0x426320 | explosion | `Expl_Create` 0x425170 (4 sprites in collision kind 0xc), `Expl_DamageArea` 0x425ca0, `Expl_CarExplode` 0x425480, `Expl_UpdateAll` 0x425b60, block-face explosions with debris and fire 0x425520-0x425960. |
| 0x426320-0x42b7a0 | frontend | `Front_Step` 0x426a50 (the per-frame state machine called by WinMain), `Front_SetScreen` 0x427030, screen handlers, `Front_LoadSettings` 0x42b4a0 (player_a.dat), `Front_Enter`/`Front_Leave` 0x42b690/0x42b7a0. |
| 0x42b800-0x42d470 | text/font drawing (probably still the frontend object) | `Font_DrawString` 0x42bad0, `Font_StringWidth` 0x42c0e0, `Text_WordWrap` 0x42b800 (UTF-8, '@' = newline, 0x3ff byte limit), menu-item and title helpers, font set load/free 0x42d2e0/0x42d410. |
| 0x42d490-0x42dc40 | frontend images | `Gfx_LoadRawImage` 0x42d5f0 (.RAW/.RAT), `Front_DrawBackground` 0x42d8e0, `Front_LoadImages` 0x42da60. |
| 0x42dce0-0x42e540 | fileio | Whole-file load/save/append (0x42dce0-0x42e220) and a single shared sequential reader `TextFile_*` 0x42e270-0x42e540 (also used for CMP/G24 binary reads). |
| 0x42e600-0x42f460 | fire (fire engines and fires) | `Fire_Register` 0x42ef80 (fires are objects of type 0x12), `FireEngine_Dispatch` 0x42ec70, `FireEngine_Spawn` 0x42e920 (car model 0x2a, sentinel type 6), `FireEngine_Update` 0x42f460. 0x42e860 is a thunk to `Sentinel_DriveCar`. |
| 0x430400-0x430490 | math | `Math_InitTables` 0x430400 (sin/tan tables), `Math_FixedToFloat` 0x430490. |
| 0x4304a0-0x430970 | font (load and glyph access) | `Font_Load` 0x4304d0, `Font_Select` 0x4307c0, `Font_GlyphWidth`/`Font_GlyphPixels`/`Font_Height` 0x430850/0x4308d0/0x430930. |
| 0x430980-0x430dc0 | game core | `Game_Init` 0x430a20, `Game_Frame` 0x430b20, `Game_Update` 0x430c00, `Game_Render` 0x430d40, `Game_Present` 0x430da0, `Game_HandleKey` 0x430dc0, exit requests 0x4309e0/0x430a00. |
| 0x431500-0x4315b0 | gang (Hells Angels) | A list of 10 gang cars. If a player takes one of them, the rest are set to hunt that player after 30 frames. |
| 0x4317f0-0x431f70 | hunt | 20-entry table at 0x5132a0 (stride 0x1c). `Hunt_UpdateCar` 0x431f70 is called from `Car_UpdateAll`. |
| 0x432b40-0x433bc0 | input + replay + joystick | `Input_ReadControls` 0x432e00 builds the 32-bit control word. Replay record/playback 0x432c50-0x433790. `Joy_Init`/`Joy_Poll` 0x4337f0/0x433bc0 (winmm `joyGetPosEx`). |
| 0x433d50-0x4340b0 | kanji (Japanese font) | `Kanji_Load` 0x433d50 (kanji.idx / kanji.bit), glyph cache 0x433e30/0x4340b0. |
| 0x434160-0x434170 | rng wrapper | `Math_Random` 0x434160 calls the generator at 0x489abe (a lagged additive generator, 15-bit output, state at 0x4b3810). 0x434170 is a thunk that reseeds it from 0x4b3824 (makes replays deterministic). |
| 0x434180-0x436e30 | collision grid | Spatial hash plus rotated-box tests. `Coll_Insert` 0x436c30, `Coll_Remove` 0x436e30, `Coll_BuildBox` 0x434300, `Coll_BoxVsBox` 0x434690, and the 3×3-cell queries 0x434e80/0x4354b0/0x435a90/0x4363c0/0x436650/0x4368d0. |
| 0x437000 | render queue | `Render_QueueVisibleEntities`: walks the grid cells under the view and puts entity positions into per-z sprite lists (via 0x47c940 → 0x480f60). |
| 0x4371e0-0x437230 | sys | `Sys_Shutdown`, `WinMain`. |
| 0x4376c0-0x4379c0 | tile tables (block-renderer object) | Builds texture pointer tables for side, lid and aux tiles from style data (called by `Style_Load` 0x47cf10 and by tile animation at 0x47d440). |
| 0x437ae0-0x438950 | map | Map queries and edits, plus `Map_Load` 0x438200. |
| 0x4389f0-0x43b7e0 | render city | `Render_DrawCity` 0x4389f0, `Render_DrawBlock` 0x438d60, flat-block and slope variants, `Render_ProjectLayer` 0x43b620, `Render_ComputeVisibleRect` 0x43b7e0. The face rasterizers are at 0x497035 and 0x497332 (another range). |
| 0x43b910-0x43cbb0 | camera | `Camera_Update` 0x43b910, `Camera_Follow` 0x43bbd0, `Camera_ComputeViewRect` 0x43ca30, debug free camera 0x43cb20/0x43cbb0. |
| 0x43cbd0-0x43cc40 | util/mem | `Mem_Alloc` 0x43cc00 (fatal on 0 bytes or failure), `Mem_AllocAligned64K` 0x43cc40. |
| 0x43cca0-0x43ce90 | mission runtime (head of the next module) | Process slots (32), `Mission_FindLine` (map of 0x10000 line numbers at 0x693b30), stored "location" records at 0x5f30ec. All are called from the mission interpreter at 0x4471a0. |

## Call-graph links to other ranges

**Frame loop.** `WinMain` 0x437230 runs the frontend loop: `Front_Step` once per 35 ms frame (scancodes are translated to menu bits). It then calls **`Game_Run` 0x4148a0** (other range), which:

1. Calls the mission.ini parser 0x44ab90, then `Map_Load`, then `Game_Init`.
2. Loops until `g_QuitRequest` (0x51322c) becomes non-zero. Each iteration runs:
   - `Game_Present` 0x430da0 (flips the previous frame)
   - `Input_ReadControls` 0x432e00 (stored per player at 0x5031a8)
   - net sync 0x44b930
   - per player: apply controls 0x463ec0, then `Game_HandleKey` 0x430dc0 for key events, then `Camera_DebugMove`
   - `Game_Frame` 0x430b20 (which calls `Game_Update` 0x430c00 → peds 0x45cd50, cars 0x40adc0, objects 0x44d790, trains 0x46a9d0, lights 0x47e420, `Emergency_UpdateAll`, `Expl_UpdateAll`, ...)
   - `Game_Render` 0x430d40 (which calls `Render_QueueVisibleEntities`, `Render_DrawCity`, then the HUD at 0x483390)
3. Returns the quit code.

The in-game loop has no Sleep. `Camera_Update` runs inside `Game_Frame` after the logic tick.

**WinMain return codes.** `Game_Run` returns 3 to restart the level, 4 or 5 to show frontend screen 0x11 (comms failure) or 0x14 (SVGA error), and anything else to show screen 1 (cutscene) or screen 7.

**Calls into other ranges:**

- Car table accessor `Car_Get` 0x408200 (base 0x4be248, stride 0x2b0, 400 cars).
- Ped accessor 0x44f500. Object accessor 0x44c2d0. Player helpers 0x412a70/0x412a90 (first/next player).
- Camera structs: render camera 0x4644e0, camera motion 0x464580, view rect 0x462c30, debug camera velocity 0x4645a0.
- Route finder 0x4716f0. Its route buffers are at 0x7537d0: 0xff bytes per sentinel, i.e. 85 nodes of (x,y,z) bytes.
- Route/location loader 0x471970/0x471b90. Nav loader 0x44b590. Map object loader 0x44ee20. Style load 0x47cee0.
- FXT lookup 0x47d9a0. Ground height 0x4544e0. Atan2 0x4899ee (returns a 0..1023 angle).

**Who calls into this range:**

- The collision grid is used everywhere (`Coll_BuildBox` 41 callers, `Coll_Insert` 50, `Coll_Remove` 41).
- `Error_Fatal` has 180 callers.
- `Math_Random` has 17 callers. The sin/cos tables are used by all movement code.

## Important globals and structures

### Map

- **0x55fab0** `g_MapTypeCache[6][256][256]`, u8 (0x60000 bytes, indexed `[(z*256+y)*256+x]`). Low 7 bits are the block's `type_map` low byte: direction bits 0-3, type bits 4-6 (0x20 = road, 0 = air). Bit 7 is overwritten with "is slope" (`type_map & 0x3f00`). It is rebuilt by `Map_Load` and kept in sync by the `Map_Set*` functions. The game uses it for all AI and road queries.
- **0x5c2c48 / 0x5c1c1c**: pointer to the map buffer = `u32 base[256][256]` (offsets into columns). The buffer is sized base + columns + blocks + 0x4000.
- **0x5c2c70**: pointer to the columns. **0x5c0bfc**: pointer to the blocks (8 bytes each).
- **0x5bfbd8**: end of the loaded data. After it come two 0x2000-byte copy-on-write areas for map edits: column copies through 0x5bfbd4 (bytes used at 0x5bfbd0) and block copies through 0x5c2c6c (bytes used at 0x5c1c2c). Overflow raises "Map change overflow".
- Extents: min x 0x5c1c30, max x 0x5c1c28, min y 0x5c0bf8, max y 0x5bfbec. These are the columns with `column[0] < 6`. If there are none, the error is "empty map".
- Map name: 0x5c1c34 (priority byte at 0x5c1c24, valid flag at 0x5bfbf0).
- **Block (8 bytes)**: u16 `type_map`, u8 `ext`, u8 left, u8 right, u8 top, u8 bottom, u8 lid. Face index for 0x438650/0x438950/0x438020: 0 = left (+3), 1 = right (+4), 2 = top (+5), 3 = bottom (+6), 4 = lid (+7). The lid texture uses remap `(ext >> 3) & 3`. Flip bits come from `ext` 0x20/0x40 (see `Render_DrawBlock`).
- **Z convention in the exe**: z = 0 is the highest layer and z = 5 the lowest. `Map_GetBlock(x,y,z)` returns `blocks[column[1 + z - column[0]]]` when `z >= column[0]`, otherwise NULL. Ground height (0x4544e0) scans z upward in index (downward in space) until a non-air type, and returns `z*0x400000 + 0x3f0000`, i.e. entities stand at the high-z face of their cell.
  - Verified on NYC.CMP: at police station (97,232,3), the z = 3 block is road type 0x22 with lid 0, and the z = 4 block below it has lid 1.
  - `tools/gtafmt.py` labels the first stored entry "top" but its z comment calls z = 0 "bottom". The relation is `z_gtafmt = 5 - z_exe`.

### CMP file (all three cities verified)

The file is laid out in this order:

1. 28-byte header: u32 version = 331; u8 style; u8 sample; u16 pad; u32 route_size; u32 object_pos_size; u32 column_size; u32 block_size; u32 nav_size.
2. base (0x40000 bytes)
3. columns
4. blocks
5. object positions (14 bytes each)
6. routes (route_size bytes)
7. **0x6c bytes of location data** (not counted in the header)
8. nav data (35 bytes each: x, y, w, h, sam, name[30])

File size = 28 + 0x40000 + sum of the sizes + 0x6c, exactly, for NYC, SANB and MIAMI.

`Map_Load` reads routes and the 0x6c location bytes together into 0x5ee750 (max 0x4000). Objects go to 0x5c3078 (max 0xadd4 = 3178 objects) and nav data to 0x5cde50 (max 0x578 = 40 entries).

**Location data**: 6 categories × 6 entries × (x, y, z) bytes:

| Offset | Category |
|---|---|
| +0x00 | police stations (used for sentinel type 2) |
| +0x12 | hospitals (type 1) |
| +0x24 | (unused) |
| +0x36 | (unused) |
| +0x48 | fire stations (type 6; the first 4 are copied to 0x511960) |
| +0x5a | (unused) |

The pointer to this table is at 0x50589c (set by 0x471b90). The counts are at 0x50caa6 (police), 0x50caa4 (hospital) and 0x50f28c (fire).

### Sentinels and emergency services

- **0x507ea0** sentinel table, stride 0x98, 128 entries (accessor `Sentinel_Get` 0x41ad60). Known fields:
  - +0 id
  - +2 type: 1 = ambulance (0x401700/0x401790), 2 = police (0x466f10), 5 = generic route follower, 6 = fire engine, 9 = hunter (0x473660)
  - +0x1b state byte (0xff/0xfe = done/returning, 0x32 chasing, 0x97 spraying...)
  - +0x1e car id
  - +0x24 ten crew ped ids
  - +0x40 car pointer
  - +0x64..0x66 destination block
  - +0x95 "at base" flag
- **0x50cab0** ped-request table, 0x3fc entries × 10 bytes: u16 ped, u8 x, u8 y, u8 z, u8 state, ..., s16 sentinel at +8. The ambulance call queue is at 0x505048 (u16) with its count at 0x50584e.
- **0x505f00** junction override table, 0x58 entries × 0x5c bytes. Timer at +6. Bound to traffic-light objects (type 0x10) by `Junction_InitOverrides`.
- **0x4b3094**: id of the sentinel being "recalled" (-1 = none).
- **0x502f54**: debug flag that appends log lines to the file "adiag".

### Fire

- **0x511988** fires: 4 entries × 0x24 bytes: s16 object, s16 x, y, z (pixels), s16 engine (+8), s16 objects[9] (+0xa), s16 extra (+0x1e). Count at 0x511984.
- **0x511a1c** fire-engine sentinel ids [3]; counts at 0x511a18/0x511a1a; timeout per engine 0x9c4 frames.

### Explosions

- **0x50f7e8**: 25 sprite slots × 0x64 bytes, frame counter at +0x5c. One explosion uses 4 consecutive slots.
- Delayed explosions at 0x50f610: 25 × 16 bytes (x, y, z, s16 timer, s16 owner).

### Car fields seen here (car base 0x4be248 + id×0x2b0)

| Offset | Meaning |
|---|---|
| +0x14/0x18 | rear point |
| +0x22 | model |
| +0x36 | length |
| +0x90 | target angle |
| +0xa2 | lane direction bit |
| +0xd8 | sentinel index |
| +0xfc | damage (100 = wrecked) |
| +0x13c | last attacker player |
| +0x1dc | pending hitbox |
| +0x220/224/228 | pending position |
| +0x250/254/258 | position |
| +0x268 | angle |
| +0x298 | pointer to car info (byte +1 = width; > 0x40 means "wide", which needs `Traffic_IsRoad3x3`) |

### Ped fields seen here

+0x49 health, +0x70 state, +0x90/94/98 position, +0xa8 angle.

### Collision grid

- 128 × 128 cells. Each cell is 2 blocks (`coord >> 23`); valid coordinates are 0..0x40000000.
- Cell heads at 0x5278f8 (`ptr[128][128]`). Per-cell u16 counts at 0x537908, plus two more counters at 0x51be58 and 0x513890.
- 16-byte nodes: u8 kind, u16 id (+2), u8 in-use (+4), entity pointer (+8), next (+0xc).
- Node pools per kind:

| Kind | Entity | Pool address | Nodes |
|---|---|---|---|
| 1 | ped | 0x524958 | 620 |
| 3 | object | 0x5417d0 | 3500 |
| 6 | car | 0x53f908 | 400 |
| 7 | (unknown) | 0x527320 | 88 |
| 8 | (unknown) | 0x527020 | 48 |
| 10 | (unknown) | 0x513710 | 24 |
| 0xc | explosion | 0x523e58 | 25 |
| 0xd | (unknown) | 0x5278a8 | 5 |
| 0xe | (unknown) | 0x541250 | 88 |
| 0x13 | (unknown) | 0x51b8d8 | 88 |
| 0x1e | (unknown) | 0x5378f8 | 1 |

  Kinds 7, 8, 10, 0xe and 0x13 are probably train or traffic-light related, because they are only active when 0x502f48 is set.
- Query results: list at 0x523ff0 (at most 149 × 16 bytes), head 0x5278a0, count 0x524956. Queries take a lock at 0x527018; `Coll_Unlock` releases it, and nesting a query raises "Accessing collision list that is in use".
- **Hitbox (0x44 bytes, `Coll_BuildBox`)**: int x[4] +0, cx +0x10, int y[4] +0x14, cy +0x24, int groundZ[4] +0x28, z +0x38, s16 angle +0x3c, s16 id +0x3e, z +0x40. The corners are centre ± sin/cos × half-extents, with exact shortcuts for angles 0/0x100/0x200/0x300.

### Math

- sin table at 0x511e28: `int[0x500]`, `trunc(sin(i·2π/1024)·65536)`. cos is the same table offset by 256 (0x512228).
- tan table at 0x511a28: 256 entries.

### Rendering

- `the pointer at 0x4b0d10` points to the view-grid descriptor: +0x18 origin block x, +0x1c origin block y, +0x20/+0x24 grid size.
- Vertex grid at 0x54f2a0: `[2][65][65]` of {int sx, sy} (layer stride 0x8408). The two buffers alternate (0x5c1c20 = upper, 0x5bfbe0 = lower), so layer z uses planes z and z+1.
- Per-layer visible rectangles: 10 ints per layer, ending at 0x5bfbc8.
- Tile pointer tables:
  - side tiles 0x5bfbf8 (4 pointers per tile)
  - alternate side tiles 0x5c0c10
  - lid tiles 0x5c1c48 (4 remaps per lid)
  - aux tiles 0x5c2c4c[8]
- Current texture 0x78c10c. Style page base 0x7750cc; tile index table 0x77552c; per-side flags 0x775320; per-lid flags 0x7750d8.
- Tile address = `base + ((idx & ~63)·256 + (idx & 63))·4`.

### Camera

- Per-player camera structs come from 0x4644e0 and 0x464580. Mode parameter sets are at 0x4b0d18 (stride 0x24).
- View rect from 0x462c30: int[5] = {left, right, top, bottom, half width} in pixels. It drives traffic spawning: cars appear one block outside the rect.

### Game state

| Address | Meaning |
|---|---|
| 0x51322c | quit request: 1 mission end, 2 abandon, 3 reload (F12), 4 net fail |
| 0x513230 | result code (7 abandon, 0xb demo timeout) |
| 0x513234 | pause frames |
| 0x513238 | redraw request |
| 0x513240 | phase (1 running, 2 frozen/F6) |
| 0x51323c/0x513244 | render step mode |
| 0x74f83c | local player index |
| 0x74f84c | player count |
| 0x7752d8 | current style number (0x47cec0) |

### Feature switches

These were inferred from how they are used, so treat the names as guesses:

| Address | Switch |
|---|---|
| 0x502f40 | peds |
| 0x503180 | cars |
| 0x5031e0 | objects |
| 0x502f48 | trains + traffic lights |
| 0x5031cc | emergency services |
| 0x5031a4 | draw sprites |
| 0x5031c8 | draw blocks |
| 0x503194 | debug keys / free camera |
| 0x5031bc | demo mode |
| 0x5031d8 | a mode where WinMain skips the normal frontend (it calls 0x426320/0x42d2c0 instead) |

### Frontend

| Address | Meaning |
|---|---|
| 0x5101d0 | screen id |
| 0x5110b4 | return code to WinMain |
| 0x511904 | back-buffer pointer |
| 0x511900 | bytes per pixel |
| 0x785170 | pitch in pixels |
| 0x785174 | height |
| 0x511590 == 0x101 | 8-bit (.RAT) path |
| 0x775558 | Japanese mode |
| 0x513228 | current font |

Fonts: 0x511568 f_mtext, 0x511570 f_mhead, 0x511574 f_key, 0x511578 cuttext, 0x51157c f_mmiss, 0x511154..0x511160 city fonts, 0x511150 kanji.

**Screen ids (0x5101d0)**:

| Id | Screen |
|---|---|
| 0 | session list |
| 1 | cutscene/chapter intro |
| 2 | error |
| 3/5 | network lobby |
| 4 | CD/demo message |
| 6 | loading |
| 7 | start menu |
| 8 | main (city/chapter select) |
| 9 | options |
| 10 | credits scroll (text table 0x4af130) |
| 0xb | player select |
| 0xc | rename / cheat entry |
| 0xd | results |
| 0xe | reset player |
| 0xf | service list |
| 0x10 | enter name |
| 0x11/0x13 | comms error |
| 0x12 | multiplayer options |
| 0x14 | SVGA error |

**Frontend input bits** (built in WinMain from scancodes): 1 up, 2 down, 4 left, 8 right, 0x10 Enter, 0x20 Esc, 0x40 character (ASCII code in bits 16+, from table 0x4a8b78), 0x80 Backspace/Del, 0x100 Shift held, 0x200 Space.

## Data formats verified against game/GTADATA

**.FON** (`Font_Load`): u8 glyph count, u8 height; then for each glyph u8 width followed by width×height bytes of 8-bit pixels; then a 768-byte RGB palette. Verified: F_CITY1, CUTTEXT, SUB1, CUT00 and BIG1 all end exactly with 768 bytes. At load time the palette is converted to 16-bit using the DirectDraw mask shifts (0x775318/0x7750c8, 0x7752e0/0x775528, 0x775520/0x7752dc); in 8-bit mode it is `>> 2`.

In memory the font is a 0x80c-byte record: u8 count, u8 height, u16 first char code (0x21 for text fonts, 1 for the big digit/city fonts), int is-kanji, pointer to u16 palette[256], then 256 × {u8 width; pixel pointer} entries. Space uses the width of 'n'. Codes >= 0x80 go through a remap table embedded right after the string "..\gtadata\cuttext.fon". Strings are UTF-8, decoded by 0x486670.

**.RAW / .RAT images** (`Gfx_LoadRawImage`): headerless. RAW is 24-bit RGB, RAT is 8-bit indices (optionally stored as `255 - v`). The width and height come from the caller:

| Image | Size |
|---|---|
| F_UPPER, F_LOGO0..7 | 640×168 |
| F_LOWER0/1 | 640×312 |
| F_PLAYn | 102×141 |
| F_PLAYN | 180×50 |
| F_RSTAR, F_RSTARN | 64×59 |
| CUTn | 640×480 |

File sizes match these dimensions (e.g. 322560 = 640·168·3). The animated logo cycles 8 frames roughly every 83 ms (`clock()` + 0x53).

**PLAYER_A.DAT** (0x414 bytes, loaded to 0x510298; verified against the shipped file):

| Offset | Contents |
|---|---|
| +0 | sfx volume (0..7) |
| +1 | music volume (0..7) |
| +2 | music mode (1..3) |
| +3 | transition effects |
| +4 | value passed to 0x414c70 |
| +5 | (unknown) |
| +7 | (unknown, default 1) |
| +8 | multiplayer target type (0 score, 1 kills) |
| +0xc | score target (default 100000) |
| +0x10 | kill target (default 10) |
| +0x14 | s8 cheat level (-1 default; set to 99 or 0xff by cheat names; applied via 0x47d680) |
| +0x18 | 6 high-score tables × 3 × {int score; char name[16]} |
| +0x180 | 8 player records × 0x50 bytes (see below) |
| +0x400 | u8 current player |
| +0x401 | char name[19] (network name, default "GTA Game") |

Player record (0x50 bytes): char name[16]; int bestScore[6] (-1 = locked); int city; int (unknown); int mission; int (unknown); int cityDone[6].

Default player names come from 0x4a7394 (Ulrika, Travis, Katie, Mikki, Divine, Bubba, Troy, Kivlov). Default high scores are BILLY/PAUL/STEVE with scores from 0x4a73d0.

**Replay .REP** (`Replay_Begin` / `Input_ReadControls`): headerless array of {u32 frame, u32 control_word}, max 0x4000 records (0x20000 bytes). A record is written only when a control changes. Verified on 1.REP: frame 1 = 0x18002 (accelerate on).

**Control word layout:**

- bit 0 = steer changed; bits 9-12 = signed steer (±7)
- bit 1 = accel changed; bits 15-16 = accel
- bit 7 = brake changed; bits 13-14 = brake
- Actions 4..9 each have a "changed" bit and a "held" bit:

| Action | Changed bit | Held bit |
|---|---|---|
| 4 | 0x4 | 0x100000 |
| 5 | 0x8 | 0x200000 |
| 6 | (none found) | 0x400000 |
| 7 | 0x20 | 0x80000 |
| 8 | 0x10 | 0x40000 |
| 9 | 0x100 | 0x20000 |

- Key event: bit 0x40, with the scancode in bits 23+.

Key bindings for the 10 actions are at 0x513620 (loaded by 0x46e960). Values >= 0x3e9 mean joystick axes or buttons.

**mission.ini tokenizer** (used by 0x44ab90): `TextFile_SkipTo`, `TextFile_ReadInt` (decimal; stops at ',' or '['; whitespace allowed), `TextFile_ReadString` (token up to a delimiter).

## Error codes

`Error_Fatal(code, module, value)`. The full code-to-message table can be regenerated from 0x422b90. Codes used in this range:

| Code | Message |
|---|---|
| -1 | open |
| -2 | read |
| -3 | close |
| -7 | out of memory |
| -14 | zero-byte alloc |
| -15/-16 | ftell/fseek |
| -20 | wrong file version |
| -21 | map change overflow |
| -23 | file too large |
| -24 | buffer exceeds max |
| -34 | empty map |
| -41 | bad replay size |
| -74 | invalid case |
| -92 | map name not specified |
| -116/-126 | collision list errors |
| -145 | sentinel |
| -146 | hunt |
| -154 | Hells Angel |
| -168 | collision list in use |
| -192/-193 | traffic lights / public transport |
| -202..-204 | open/read/write on frontend files |
| -283 | invalid explosion face |
| -295 | car warped |

## What to port first

**(a) Loading and drawing the city**

- `Map_Load` 0x438200, `Map_GetBlock` 0x437ae0, `Map_GetTypeMap` 0x438900 and the type cache 0x55fab0.
- `Mem_Alloc` and the `TextFile_*` readers.
- `Tile_BuildSideTable`/`Tile_BuildLidTable` 0x437750/0x4377b0 (together with `Style_Load` 0x47cf10).
- `Render_ComputeVisibleRect` 0x43b7e0, `Render_ProjectLayer` 0x43b620, `Render_DrawCity` 0x4389f0, `Render_DrawBlock` 0x438d60, `Render_DrawFlatBlock` 0x439180 (+0x4392d0/0x4393f0), `Render_DrawSlope` 0x4395d0 and the four slope drawers.
- Camera: `Camera_Update` 0x43b910 / `Camera_ComputeViewRect` 0x43ca30.
- `Math_InitTables`.

**(b) Player on foot / in a car.** The ped and car physics live in other ranges. From this range port:

- the collision grid (`Coll_Init`, `Coll_Insert`, `Coll_Remove`, `Coll_BuildBox`, `Coll_BoxVsBox`, `Coll_QueryBox`/`Coll_QueryBlock`)
- `Input_ReadControls` with `Input_ActionPressed`/`Input_ActionReleased` (the control word)
- `Map_IsFaceSolid` 0x438650, `Map_GetTypeAt` 0x4388a0
- `Camera_Follow` 0x43bbd0
- `Render_QueueVisibleEntities` 0x437000

Traffic (`Traffic_SpawnAroundView`) comes next.

**(c) Frame loop**

- `WinMain` 0x437230 (frontend loop with the 35 ms throttle)
- `Game_Init` 0x430a20, `Game_Frame` 0x430b20, `Game_Update` 0x430c00, `Game_Render` 0x430d40, `Game_Present` 0x430da0, `Game_HandleKey` 0x430dc0
- the request/quit flags
- `Error_Fatal`

`Game_Run` 0x4148a0 (other range) is the loop body that ties these together.

## Open questions

- **0x41a490 `Map_FindNearestRoad`**: when following a road it moves x-1 for direction bit 1, x+1 for bit 2, y+1 for bit 4 and y-1 for bit 8. That contradicts the -y/+y/-x/+x meaning used by the sentinel and traffic code. Either the original has a quirk, or the struct field order needs rechecking.
- `Emergency_UpdateAll` 0x419880 could not be decompiled. Only its data flow is known (from the disassembly): ambulance queue, police dispatch list at 0x5058a8 (stride 12), junction override list at 0x50586c.
- Exact semantics of 0x5031d8 (WinMain picks a network/direct-start path) and of the switches set by the 43-argument call to 0x4146d0.
- Collision kinds 7, 8, 10, 0xd, 0xe, 0x13, 0x1e are not identified (likely trains, lights and doors).
- Control actions 4..9 still need mapping to fire / handbrake / enter-car / weapon next/prev / special. The bit layout above is exact; the action names are not.
- Sentinel types 5 and 9 (9 = hunter via 0x473660?) and the state-byte values (0x23, 0x27, 0x28, 0x6e, 0x97, 200..0xd1) need the out-of-range handlers to name them.
- The kanji path (Japanese build) is present but not needed for the English data.
