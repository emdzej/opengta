# Inventory 5: 0x476550 to 0x48a310

300 functions. The names are in `tools/ghidra/names.tsv`. Module boundaries come from address order (MSVC links one object file after another) plus who calls whom. Prefixes: `Mission_/Car_/Ped_/Obj_` (mission helpers), `Trigger_/Door_/Garage_`, `Sprite_`, `Style_`, `Text_`, `Timer_`, `Lights_/Rail_`, `DrawList_`, `HUD_/Pager_`, `Net_`, `Proj_/Weapon_`, `Blit_`, `Math_`, `Event_`.

Conventions used below:
- **World coordinates** are 16.16 fixed point, with 64 integer units per block. So `v>>22` gives the block number and `(v>>16)&63` gives the position inside the block. The high short of a coordinate is the integer pixel position.
- **Angles** run from 0 to 1023. Angle 0 points along +y, 0x100 along +x, 0x200 along -y, 0x300 along -x. The x component comes from table `0x511e28[1024]` and the y component from `0x512228[1024]`.
- **Map z** looks like it points down (medium confidence). Level 0 is the top and level 5 is the ground. A column's first word is its lowest-index level that is present, and entries are indexed `z - col[0]`. This matches `tools/gtafmt.py`, which stores the top entry first. NYC.CMP column heights are 0..5, mostly 3.

## Modules

| span | module | purpose |
|---|---|---|
| 0x476550-0x479000 | mission | Helpers for the mission interpreter (in other ranges: 0x4471a0, 0x445800, 0x443690, 0x43d460, 0x441600, 0x43ef80, 0x43fb20, 0x440040): clear blocks of cars/peds/objects, create peds with AI presets, change ped AI, briefs, sound slots, car-list goals. Includes three thunks. |
| 0x479020-0x47baf0 | trigger | Kill scoring, player respawn, per-city tables, door and trigger state machines (respray, bomb shop, races), respray cost. |
| 0x47bb10-0x47cd00 | sprite | World-to-screen projection, rotated sprite drawing with per-sprite cache, sprite-info loader, sprite group bases, z-level draw trees. |
| 0x47cd10-0x47d610 | style | G24 loader, palette pixel-format conversion, tile animation. |
| 0x47d680-0x47dbd0 | text | FXT language select, decrypt, lookup, character mapping. |
| 0x47dc00-0x47dcb0 | timer | 70 Hz Miles AIL timer for frame pacing. |
| 0x47dcf0-0x480e10 | lights / rail | Traffic-light junction detection and cycling. Railway tracer: crossings, switches, stations. |
| 0x480ed0-0x480fd0 | drawlist | Binary search tree keyed by z, used for depth-sorted sprite drawing. |
| 0x481030-0x486830 | hud | Fonts, pager, subtitles, zone names, arrows, score/wanted/weapon display, pause screen, chat, in-game video menu, UTF-8 helpers. |
| 0x486880-0x4879a0 | net | DirectPlay (IDirectPlay2) wrapper with 4 player slots. |
| 0x4879b0-0x489300 | proj | Projectiles and weapon firing. |
| 0x489470-0x489c7c | blit / math | Hand-written code (unaligned entry points): CLUT blitters, atan2, RNG, slope height, rectangle overlap. |
| 0x489dc0-0x48a2c0 | event | Win32 message pump feeding an input event queue. |
| 0x48a310 | unk | One getter (`+0x1b8`), apparently the first function of the next module (called by 0x414db0). |

## Key functions

### Mission helpers (0x476550-0x479000)
- `Mission_ClearBlock` 0x4770a0 and `Mission_ClearRow` 0x477160 clear a block (or a row of blocks) before mission objects are placed.
- `Car_RemoveIfOffscreen` 0x476720 and `Car_RemoveIfOffscreenEx` 0x476880 delete a car only if it is outside every player's view rectangle. The view rectangle is player struct +0 (x1, x2, y1, y2) in integer world units, with the valid range 0x10..0x3ff0.
- `Car_IsEmergencyModel` 0x4769e0 returns true for models 4, 5, 0xF, 0x10, 0x20 and 0x2A.
- `Mission_PedCreate_*` 0x4773f0-0x477b50 are the PED_ON variants. They are selected by `switch(0x5fe004)` in 0x43d460. Each one calls `Ped_Create` 0x453e90 and then sets AI fields: +0x70 state, +0x6c objective, +0x7c behaviour, +0x86 target, +0x84 = 100, +0x8b = 1.
- `Mission_PedSetObj_*` 0x477bd0-0x478570 change the AI of a live ped. Each one first detaches the ped from a car it is driving (behaviour 0x11).
- `Mission_Brief*` 0x478920-0x478ac0 forward to `HUD_Brief` 0x4821c0. Text ids must be >= 1000.
- `Map_SetBlock_thunk` 0x478c80 jumps to 0x437b50. That function does copy-on-write column/block editing (CHANGE_BLOCK), described under Globals.

### Trigger / door / garage (0x479020-0x47baf0)
- `Mission_UpdateTriggers` 0x479e00 runs every frame from 0x446fe0. It calls `Door_Update` 0x479f00 for each door, `Trigger_Update` 0x47a3a0 for each live trigger, and 0x475090 for each 0x771108 entry, then 0x4315b0 and 0x473ef0.
- `Trigger_Update` handles the following trigger types. FXT keys: clean_done, resray_done, bomb_cost "That'll be $%s.", all_your, no_respray, no_tank_respray (model 0x25 = Tank), no_modelcar_respray (0x2F = Model Car), no_emerg_respray, bomb_added, bomb_off, 1st_over_checkpoint, second_checkpoint, win_race, lost_race.
  - 3: respray
  - 8: bomb shop
  - 0x13 and 0x14: race checkpoint and finish
  - 0x17: disarm
  - 0 to 2 and 10: door open/close
  - 0xb: 0x4318e0
  - 0x16: 0x4319b0
  - 0x1d and 0x20: stopped/onscreen counters
- `Garage_ResprayCost` 0x47ba30 computes `((W + value*1000*damage/100) * (100+M)) / 100`:
  - `value` is car-info +0x6e (u16).
  - `damage` is car +0xfc.
  - `W` is 0 if the wanted level (0x4619b0) is 0, otherwise `1000 << (wanted-1)`.
  - `M` comes from 0x463090.
  - The result is 500 when both terms are zero.
- `Score_PedKilled` 0x479020 is called by 0x408af0, 0x425ca0, 0x44fa70 and `Proj_UpdateAll`. It maps the victim's AI type to a score event and calls `Score_Award` 0x462000.
- `Player_ChooseRespawnPoint` 0x479610 picks a respawn point. Points are 3-byte (x,y,z) block entries at `*(0x50589c)+0x12`, count `0x50caa4`. It prefers points more than 20 blocks (Chebyshev distance) from every player.

### Sprite (0x47bb10-0x47cd00)
- `Sprite_WorldToScreen` 0x47bb10 projects as follows, where `d = z_int + camZ`:
  - `sx = (((X - camX<<16)/d + 127) * scale >> 16) + cx`
  - `sy` is computed the same way with Y, then multiplied by 5/6 if the flag at 0x5bfbdc is set.
- `Sprite_Draw` 0x47bc00 and `Sprite_DrawCached` 0x47c130 take the sprite size as w*0x8000 by h*0x8000 (half extents), rotate the corners by the angle, project them, cull against 0..0x5c0c00 by 0..0x5bfab0, select the palette (0x4385e0), and pass the 4 corners to the rasteriser:
  - The rasteriser is 0x49787c, or 0x4979c9 when sprite +0x14 is set and option 0x5031e4 is on.
  - Small quads (|dx|+|dy| < 10) use the raw graphic. Larger ones go through `0x4143a0` (graphic cache, which uses `Blit_ApplyDelta` and `Gfx_CopyRect256`).
- Depth sorting:
  - `Sprite_Queue` 0x47c940 inserts the sprite into `roots[z>>22]` (6 roots at 0x774ec8).
  - The renderer 0x4389f0 calls `Sprite_DrawLevel(level)` 0x47c030, which runs `DrawList_Walk` with callback 0x47c050. That callback is not defined as a function in Ghidra; it calls `Sprite_DrawCached`.
- `Sprite_LoadInfo` 0x47ca50 (called from `Style_Load`):
  - Builds the table `0x773e38[]`, up to 0x424 entries, each a pointer to a sprite-info record.
  - Adds the graphics base to each record's data pointer and to each delta's pointer.
  - Calls `Sprite_SetGroupBases` and creates the 6 draw-tree roots.

### Style (0x47cd10-0x47d610)
- `Style_Load` 0x47cf10 is called through `Style_LoadRequested` 0x47d390 from startup 0x4148a0. The format is given under Data formats.
- Style number == city (1 NYC, 2 SANB, 3 MIAMI). `Style_GetNumber` 0x47cec0 is used as the city id by the trigger code and the HUD.
- `Style_ConvertPalettes` 0x47cd10 rewrites every 4-byte CLUT entry `(b,g,r,x)` into the display pixel format: each channel is shifted right by a precision shift and left by a position shift (globals 0x775318/0x7750c8/0x7752e0/0x775528/0x775520/0x7752dc). It reloads the CLUT from the file first if it was already converted.
- `Style_UpdateAnims` 0x47d610 runs every frame (0x430c00, 0x430b20). Each anim has a tick counter and a frame index, and `Style_SetTileFrame` 0x47d440 remaps tile `block` (side if which=0, lid if which=1) through the tables 0x775320 (side) and 0x7750d8 (lid).

### Text (0x47d680-0x47dbd0) and timer
- `Text_InitLanguage` 0x47d7f0 is called from WinMain. The language index is: 0 english, 1 french, 2 german, 3 italian, 4 japanese (wide chars), 99 special.
- `Text_Get` 0x47d9a0 does the FXT lookup and returns a pointer into the decrypted buffer. Callers check for NULL.
- `Timer_Start` 0x47dc00 sets up a 70 Hz AIL timer (`AIL_register_timer`, frequency 0x46). `Timer_WaitTicks` 0x47dc80 busy-waits on the tick counter 0x775580. Both are used by 0x4148a0. This is separate from the 35 ms Sleep in WinMain.

### Traffic lights and railway (0x47dcf0-0x480e10)
- `Lights_Init` 0x47dcf0 is called from level init 0x430a20. Block flags come from `Map_BlockQuery(q,x,y,z)` 0x44b310, which reads `packed = type_map | type_map_ext<<16` (0x438900):

  | q | meaning |
  |---|---|
  | 1 | railway (bit 23) |
  | 2 | ext traffic-light field == 1 (junction cell) |
  | 3 | block type 2/6/7 |
  | 4 | direction bits |
  | 6 | ext lights field |
  | 7 | ext field 4 or 5 |
  | 8 | ext field == 2 |
  | 9 | block type 3 |

- Junction build: `Lights_AddJunction` 0x47e6c0 runs `Lights_FloodJunction` 0x47e7c0 and `Lights_TraceArm` 0x47f240 per direction, then `Lights_ClassifyJunction` 0x47ebd0 against 11 templates at 0x4b3520 (21 bytes each), then `Lights_CreateSprites` 0x47f560.
- `Lights_Update` 0x47e420 runs every frame and advances phases every 8 frames. The phase table at 0x4b3514 holds 6 entries of (sprite state, duration).
- Car AI (0x416610, 0x417d90, 0x41e1c0, ...) reads light state through `Lights_Query` 0x47df00 (codes 0x32..0x3D).
- Rail: `Rail_Init` 0x47fa90 and `Rail_TraceTrack` 0x47fbe0 follow railway cells in 12 step directions. They record:
  - level crossings (ext field 2) in 0x77cf58 (4 bytes each)
  - switches (ext field 3) in 0x77d0c8 / 0x77d010
  - stations (ext 4 and 5) in 0x77d180 (7 bytes each) and 0x77d369.. (up to 6 platforms)
- The train code (0x46bd60, 0x46da30, 0x46b240) uses the `Rail_*` getters. Matching error strings: "No Station found on Track", "Switch and Crossing too close", "Too Many Lights in Junction".

### HUD (0x481030-0x486830)
- Per-frame calls: `HUD_Update` 0x485d70 from the game update 0x430c00, and `HUD_Draw` 0x483390 from the draw step 0x430d40.
- Init and teardown: `HUD_Init` 0x483010 at level init, `HUD_LoadFonts` 0x481030 from 0x414cc0 (on video mode change), `HUD_FreeFonts` 0x4832c0.
- Message channels:
  - `Pager_AddMessage` 0x4824a0: 16 slots of 0x4E0 bytes.
  - `HUD_ShowSubtitle` 0x481d40: kinds 0..5 use HUD sprite icons 0xb, 0xa, 0xc, 0xe, 0xd, 0xf.
  - `HUD_ShowZoneText` 0x481a40: type 1 = 90 frames, 2 = 45, 3 = 270, 200 = car name. Area names come from `0x44b5c0(camX>>6, camY>>6)`.
  - `HUD_ShowBigMessage` 0x481320: FXT keys 2500 "MISSION COMPLETE!", 2501 "MISSION FAILED!", 2504 "FRENZY FAILED!" select sounds 2, 1, 5 (0x404e90).
- HUD sprites are sprite numbers `arrowBase(0x774efa) + (hires ? 0x18 : 0) + n`. Font palettes start at `0x7752f8+1`.
- Text drawing: UTF-8 decode `Text_Utf8Next` 0x486670, then glyph lookup 0x430850/0x4308d0, then `Blit_Sprite` 0x4897d3.
- `HUD_HandleKey` 0x482d00 is called from 0x430dc0. Scan codes: 0x1c Enter, 0x148/0x150 up/down, 0x14b/0x14d left/right.

### Net (0x486880-0x4879a0)
- Uses DirectPlay with IID at 0x4a8cb8. `Ordinal_1` = DirectPlayCreate, `Ordinal_2` = DirectPlayEnumerateA.
- IDirectPlay2 vtable offsets: +0x10 Close, +0x18 CreatePlayer, +0x24 DestroyPlayer, +0x30 EnumPlayers, +0x34 EnumSessions, +0x60 Open (2 = create, 1 = join), +0x64 Receive, +0x68 Send.
- Player slots are `0x785190[4]` = {dpid, active}. The local id is in 0x7851b8.
- `Player_First` 0x412a70 uses `Net_FirstSlot` 0x4875e0 when the mode flag 0x501d7c == 2.
- Handshake strings: "Is your seatbelt fastened?" and "Let's go!".

### Projectiles (0x4879b0-0x489300)
- Projectiles live in the generic object table 0x6b40d0 (stride 0x88, `Obj_Get` 0x44c2d0). The active list is `0x785318[]` (short ids), count 0x785314, max 0x28.
- Object types: 0x4a bullet, 0x1f rocket, 0x4b flame (11 animation frames).
- `Proj_UpdateAll` 0x487a70 is called from the object update 0x44d790.
- Ped weapon dispatch 0x4532f0 calls `Weapon_FireBullet/Flame/Rocket`. The car weapon 0x40a2e0 calls `Car_FireRocket` 0x489300.

### Blit, math and events
- Blitters take 256-byte-stride 8-bit source data. Colour 0 is transparent. Output is 16 or 32 bpp depending on 0x50321c, through CLUT pointer 0x78c10c, where `entry = clut[pix*256]`. The `*_640` variants are for the 640-wide front-end buffer.
- `Math_Atan2` 0x4899ee. `Math_Rand` 0x489abe is an additive RNG: `s0 = (s1+s2+s3) & 0x7fff`, then the state shifts. `Map_SlopeZ` 0x489b19 uses a slope table at 0x4b3838 and slope type `(type_map>>8)&0x3f`.
- `Event_Get(mask)` 0x489dc0 is used by input 0x414a80. Event = 9 dwords: [1] type (1 down, 2 repeat, 4 up, 8 mouse down, 0x10 mouse up, 0x20 mouse move, 0x40 WM_TIMER), [3]/[4] mouse x/y scaled, [5] key code (scan<<8 | char), [6] modifiers. The pool is at 0x785378, max 100 events.

## Globals and structures

### Entity tables

| addr | stride / size | meaning |
|---|---|---|
| 0x4be248 | 0x2b0 | Car table (`Car_Get` 0x408200). Fields: +0 id, +2 driver ped, +0x22 model, +0x9c bomb fitted, +0xfc damage 0..100, +0x134 linked item, +0x139 parked flag, +0x13c last attacker, +0x250/+0x254/+0x258 x/y/z, +0x260 remap, +0x268 angle |
| 0x7284e0 | 0x100 | Ped table (`Ped_Get` 0x44f500). Fields: +0x49 health, +0x4c car id, +0x5a killer, +0x6c objective, +0x70 state (6/7 = in car, 0xC/0x17/0x15/0x18 special), +0x7c behaviour, +0x86 target, +0x90/+0x94/+0x98 pos |
| 0x6b40d0 | 0x88 | Generic objects (`Obj_Get` 0x44c2d0). Fields: +2 speed, +4 life, +6 angle, +8 type, +0xc state, +0x1e owner ped, +0x2c.. sprite position |
| 0x74f14c | 0x1bc | Players (`Player_Get` 0x462c30). +0 view rectangle (x1,x2,y1,y2). Getters: 0x462fc0 state (0 in car, 1 train?, 2 on foot), 0x462fe0 vehicle/ped id, 0x462ef0 pos. 0x74f83c = local player, 0x74f84c = player count |
| 0x501574 + 0x4be178 | | Car-info pointer table and model→index map. Info +0x6e = value |

### Map

| addr | meaning |
|---|---|
| 0x5c1c1c | ptr to base[y*256+x] u32 column offsets |
| 0x5c2c70 | column data |
| 0x5c0bfc | block info (8 bytes each) |
| 0x5bfbd8 | end of the loaded data; columns or blocks below it are copied before an edit |
| 0x5bfbd4 / 0x5bfbd0 | column change area pointer / bytes used (max 0x2000) |
| 0x5c2c6c / 0x5c1c2c | block change area pointer / bytes used (max 0x2000) |
| 0x55fab0 | u8 [6][256][256] block-type cache: low 7 bits of type_map, bit 7 = sloped. Index `(z*256+y)*256+x` |

### Camera and screen

| addr | meaning |
|---|---|
| 0x5c0c04, 0x5c0c08 | camera x, y (integer world units) |
| 0x5c0c0c | camera height added to depth |
| 0x5c1c10 | projection scale |
| 0x5bfbe4, 0x5bfbe8 | screen centre |
| 0x5c0c00 | width |
| 0x5bfab0 | height |
| 0x5bfbdc | 5/6 aspect flag |
| 0x503228[] | row byte offsets |
| 0x50321c | bytes per pixel |
| 0x503220 | pitch (pixels) |

### Sprites

| addr | meaning |
|---|---|
| (object) | Sprite object, 0x5c bytes: +0 x, +4 y, +8 z, +0xc z2, +0x10 remap, +0x12 palette, +0x14 flag, +0x16 frame, +0x18 angle, +0x1a cached angle, +0x28..0x44 cached corners, +0x48 info ptr, +0x4c cached info, +0x54 saved frame |
| 0x773e38[0x424] | sprite-info pointers |
| 0x774efa..0x774f10 | 21 sprite group bases |
| 0x774ec8[6] | draw-tree roots |
| 0x77d478 / 0x77d47c | draw-tree node and root counts. Nodes at 0x77d4e0 (16 bytes, 300 max) |

### Style

| addr | meaning |
|---|---|
| 0x7752d8 | current style |
| 0x7752e8 | tile buffer, 0x1a0000 + anim |
| 0x7750cc | CLUT |
| 0x77552c | palette index |
| 0x775534 / 0x7752fc / 0x7750d4 | side / lid / aux tile counts (size / 4096) |
| 0x77530c | (tileclut+spriteclut)/1024 |
| 0x7752f8 | first font palette |
| 0x5f2750 | anim table: 8 bytes per anim (tick, frame, ptr) |

### Gameplay state

| addr | meaning |
|---|---|
| 0x776ae8 | Junction lights, stride 0x124. +0 mode (0 auto, 3 forced), +1 orientation, +2 phase, +3 kind (template; >9 = rail crossing), +5/+6/+7 block x,y,z, +8 group, +9 tick, +0xa frame, +0xc countdown, +0x10 light sprite, +0x6c pole sprite. Count 0x77d463 |
| 0x771628 | Triggers, stride 0x20. +0 state (0 idle, 1 armed, 2 delayed, 3 fired, 4 dead), +4 type, +8/+0xc params, +0x10/+0x11/+0x12 block x,y,z, +0x14 radius, +0x18 counter, +0x1c proc line. Count 0x7710f8 |
| 0x773220 | Doors, stride 0x28. +0 door anim id, +4 state (0 closed, 1 armed, 2 open, 3 closing), +0xc range, +0x14..0x16 block x,y,z, +0x18/+0x1c face params, +0x20 condition, +0x22 car id/model, +0x24 remap, +0x26 flag index. Count 0x7710f0 |
| 0x77f280 | Pager slots, 16 × 0x4E0 |
| 0x784080 | Zone texts, 6 × 0x88 |
| 0x77e7f0 | Score popups, 32 × 0x24 |
| 0x785150 / 0x785128 / 0x785130 / 0x785158 / 0x785138 / 0x785140 / 0x785148 | Font handles: sub, street, pager, big, expscor, score, missmul |
| 0x785160 | resolution (1/2) |

## Data formats (checked against game/GTADATA)

### G24 (STYLE001-003)

Checked against all three files; the walk ends exactly at the file size.
- 64-byte header of 16 u32: version (=336), side, lid, aux, anim, clut, tileclut, spriteclut, newcarclut, fontclut, palette_index, object_info, car, sprite_info, sprite_graphics, sprite_numbers. These are the field names already used in gtafmt.py.
- Body, in order:
  1. Tiles: side+lid+aux, padded in the file to a 16 KB boundary. gtafmt currently assumes no padding.
  2. Anim
  3. CLUT, padded to 64 KB
  4. Palette index
  5. Object info
  6. Car info
  7. Sprite info
  8. Sprite graphics
  9. Sprite numbers
- Anim section: a u8 count, then per anim `{u8 block, u8 which (0 side/1 lid), u8 speed, u8 n, u8 frame[n]}`. STYLE001 has 5 anims; the first is block 41 (lid), speed 7, 11 frames.
- Sprite info: 12-byte record `{u8 w, u8 h, u8 deltas, u8 pad, u16 size(=w*h), u16 clut, u32 offset}` followed by `deltas × {u16 size, u32 offset}`. Offsets point into 256-byte-wide graphic pages. STYLE001 has 1034 records.
- Delta stream: `{u16 skip, u8 n, n bytes}`, repeated.
- Sprite numbers: 21 u16 group counts (arrow, digits, boat, box, bus, car, object, ped, speedo, tank, traffic_lights, train, trdoors, bike, tram, wbus, wcar, ex, tumcar, tumtruck, ferry). For STYLE001 these are 49,0,1,0,6,27,562,295,0,2,6,5,0,26,0,0,7,48,0,0,0; the sum, 1034, equals the record count. Traffic-light sprites are base 0x774eec+0..4.

### FXT

Checked against ENGLISH.FXT.
- Decrypt the whole file with `key=0x64, mul=0x63`; for each byte: `b -= key; key += mul; mul *= 2` (all bytes, mod 256).
- The decrypted text is `[KEY]text\0` repeated, ending with `[]`.
- Keys seen: car%d, 2500.., quit1-3, paused, bomb_set, clean_done, etc.
- Characters may be UTF-8-like multibyte codes (2 or 3 bytes, 6-bit payloads), decoded by `Text_Utf8Next`.

### FON names

`..\gtadata\{big,sub,street,pager,expscor,score,missmul}%d.fon` with %d = 1 or 2 (resolution).

### CMP

Edits only (no new parsing here). Block info is 8 bytes; `packed = type_map | type_map_ext<<16`:
- type_map bits 0-3: directions
- bits 4-6: block type
- bits 8-13: slope
- type_map_ext bits 0-2: lights/rail feature (1 junction, 2 crossing, 3 switch, 4/5 station)
- bit 23 of packed (bit 7 of ext): railway

## Links into other ranges

- Frame loop:
  - update 0x430c00 calls `Lights_Update`, `Style_UpdateAnims`, `HUD_Update`
  - draw 0x430d40 calls `HUD_Draw`
  - level init 0x430a20 calls `Lights_Init`, `HUD_Init`, `Math_RandSeed`, `Proj_Reset`
  - 0x430b20 calls `DrawList_Clear`, `Style_UpdateAnims`
  - 0x430dc0 calls `HUD_HandleKey`
- Startup 0x4148a0 calls `Style_LoadRequested`, `Style_ConvertPalettes`, `Timer_*`. WinMain 0x437230 and shutdown 0x4371e0 call `Text_InitLanguage`, `Timer_Stop`, `Style_Free`, `Text_Free`, `HUD_FreeFonts`.
- Renderer: 0x4389f0 calls `Sprite_DrawLevel`; 0x437000 calls `Sprite_Queue`; the block draw code (0x4396b0, 0x439e10, 0x43a620, 0x43adf0) calls `Sprite_WorldToScreen`; the rasterisers are 0x49787c and 0x4979c9.
- Map: `Map_SetBlock` 0x437b50, block query 0x44b310/0x438900, zone lookup 0x44b5c0.
- Mission interpreter: 0x4471a0 and the related functions listed under Mission helpers call almost all mission helpers. 0x446fe0 calls `Mission_UpdateTriggers` and `Player_Respawn`.
- Spatial grid: query 0x4363c0 / 0x4368d0 / 0x435a90 / 0x434e80, end query 0x436b20, register 0x436c30, unregister 0x436e30, box 0x434300.
- Error reporting: `Fatal(code, line, value)` 0x422900.
- Explosion: 0x425170. Object create: 0x44cef0.
- Sounds: 0x404e90 (jingle), 0x404d50/0x404d90 (positional loops), 0x4047f0/0x404810 (one-shot at position).
- File I/O: 0x42e270 open, 0x42e390 read, 0x42e320 seek/skip, 0x42e2e0 close, 0x42dce0 load whole file.
- Alloc: 0x43cc40 / 0x43cc00. Free: 0x49d612.

## Port priorities

- **(a) Load and draw the city:**
  1. `Style_Load` 0x47cf10, `Sprite_LoadInfo` 0x47ca50, `Sprite_SetGroupBases` 0x47cbd0, `Style_InitAnims` / `Style_UpdateAnims`, `Style_ConvertPalettes`.
  2. `Sprite_WorldToScreen` 0x47bb10, `Sprite_Draw` / `Sprite_DrawCached`, `DrawList_*` 0x480ed0-0x480fd0, `Sprite_Queue` / `Sprite_DrawLevel`.
  3. `Blit_*` 0x4894a1-0x4897d3, `Blit_ApplyDelta`, `Map_SetBlock` (0x437b50), `Map_SlopeZ`, `Math_Atan2`, `Math_Rand`.
- **(b) Player on foot / in a car:**
  1. `Weapon_Fire*` and `Proj_UpdateAll` 0x487a70, `Lights_Init` / `Lights_Update` / `Lights_Query` (traffic AI depends on them), `Coll_RectsOverlap` 0x489c7c.
  2. HUD basics: `HUD_LoadFonts`, `HUD_Draw`, `HUD_DrawScore`, `HUD_DrawWanted`, `Pager_*`, `Text_Get`.
  3. `Trigger_Update` / `Door_Update` for garages.
- **(c) Frame loop:** `Event_Pump` / `Event_Get` 0x489ec0 / 0x489dc0, `Timer_Start` / `Timer_WaitTicks`, `HUD_Update`, `Mission_UpdateTriggers`. Net can be stubbed out (single player: `Player_First` returns 0).

## Open questions

- These functions have no xrefs in Ghidra and may be called through pointers: 0x47c050 (sprite draw callback; needs a function defined), 0x489b19, 0x489c7c, 0x489470/0x48947d (DOS VGA leftovers), 0x48a310 (module start?).
- Ped field meanings (+0x6c objective, +0x70 state, +0x7c behaviour) are inferred from mission presets only; the `Mission_PedCreate_*` → PED command mapping needs the parser's switch at 0x43d460.
- The `Player_SetStatByCity` 0x4799d0 constants (0x98..0x1F6) and the target of 0x461840/0x4617e0 are unknown.
- Junction template table 0x4b3520 (11 × 21 bytes) and phase table 0x4b3514 should be dumped and documented. The meaning of rail ext values 4 vs 5 (platform) is unconfirmed.
- The z-axis direction (0 = top) is inferred from column indexing and should be confirmed with the renderer.
- The CLUT row layout used by the blitters (`pix*256`) agrees with gtafmt's 64-palettes-per-64 KB-page layout; the palette offset is presumably folded into 0x78c10c by 0x4385e0/0x437730 (not checked).
