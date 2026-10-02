# Inventory 1: 0x401000-0x417d90

This range covers 300 functions. Names, modules and confidence levels are in `tools/ghidra/names.tsv`. Every
function was read; the small accessors were skimmed. Addresses below are virtual addresses in
`gta.exe`.

## Conventions seen everywhere in this range

- **World coordinates** are 32-bit fixed point. One map block is `1<<22` (0x400000), and one tile pixel
  is `1<<16` (64 px per block). `x>>22` gives the block index (0..255). The high short of a coordinate
  is the pixel position. Z uses the same scale: 6 levels, and cars clamp to `z <= 0x13f0000`.
- **Angles** are 0..1023 (`& 0x3ff`), with 0x100 as a quarter turn. Headings are snapped to 0/0x100/0x200/0x300
  for road directions.
- **Trig**: `0x511e28` is an int table indexed by angle (the x component). `0x512228`, which is the same
  table 256 entries (0x400 bytes) further on, is used as the y component. So it is one sine table of
  at least 1280 entries, read as sin and cos. Displacement = `table[a] * length`, where length is in
  pixels and the table is 16.16.
- **Road direction bits** come from the type map: 1, 2, 4, 8. The headings they map to are 1->0x200,
  2->0, 4->0x300, 8->0x100. Heading -> dir bits: [0x80,0x180) gives 8, [0x180,0x280) gives 1,
  [0x280,0x380) gives 4, and anything else gives 2.
- **Fatal errors** go through `Error_Fatal 0x422900(code, module_line, arg)`.
- `0x49cb45` is `sprintf`. Most of its uses here format debug strings that are then discarded,
  e.g. "CD_STOP...".
- `0x49cb27` is MSVC `rand`. `Math_Random 0x434160` is another RNG that stores its result in
  `0x523fe8`.
- Sound code has its own LCG in `0x4bddac`: `s = s*0x79+1 & 0x7ff`.
- **Player iteration** uses `Player_First` 0x412a70 and `Player_Next` 0x412a90, which return -1 at
  the end. In single player this is just player 0. These are thin network wrappers; network state is
  in `0x501d7c`.

## Modules (object-file order)

| Span | Module | Purpose |
|---|---|---|
| 401000-401280 | event | Frame clock and timed event queue |
| 401340-401790 | ambulance | Emergency-crew AI that revives dead peds (identification is low confidence) |
| 402240-402610 | blockanim | Animated map faces (doors, garage doors) using tile remap slots |
| 402640-405760 | sound | Positional sfx, loop channels, voices, police scanner, game/frontend sound modes |
| 405790-40c0c0 | car | Car table, spawn/init, dummy traffic movement, wreck/bomb logic, per-frame car loop, accessors |
| 40c100 | car (info) | Sets up the `car_info` records from the style file |
| 40c300-40d620 | music | Streamed WAV "CD" music, per-car radio stations (Miles streams) |
| 40d640-40da90 | police (pursuit) | 4 pursuit records, wanted-level upkeep, cop spawning |
| 40dc80-40dfb0 | heli | One scripted pickup aircraft (land, take player, fly off, end level) |
| 40e350-412310 | collision | Shape vs map (walls/solids/slopes), object grid hits, car-car/car-map response, ground height |
| 412950-412d00 | net | DirectPlay session wrappers (via 0x487xxx), lobby texts, player iteration |
| 412d20-412e90 | tuning | Optional `..\gtadata\config.ini` "[car N param] value" overrides of car_info |
| 4131d0-414280 | police (crime) | 4 criminal records, crime reports, wanted thresholds per city, radio reports |
| 414310-414590 | sprite | Sprite delta (damage/door overlay) bitmask and composite cache |
| 414620-4148a0 | game | Audio mode switch, launch options, **in-game session loop** `Game_Run` |
| 414a70-4159b0 | gfx/input | SciTech MGL (MegaGraph) video modes, palette, page flip, keyboard events |
| 415a50-417d90 (continues) | carai | Traffic driving: throttle, lane keeping, junctions, recycling off-screen cars |

## Key structures and globals

### Car table: `0x4be248`, 400 slots, stride 0x2b0

`Car_Get(n)` 0x408200 returns `0x4be248 + n*0x2b0`. The number of used slots is in `0x501554` (`Cars_GetCount`).
Each slot n also owns a driver ped object with id `200+n` (see Ped/object below).

| Off | Type | Meaning |
|---|---|---|
| +0x00 | s16 | own index |
| +0x02 | s16 | driver ped id (-1 none) |
| +0x04 | s16 | control mode: 0 traffic dummy (`Car_DummyFollowRoad`), 1 physics/player (`0x460be0`), 2/3/9/10 AI driver (`0x41aed0`), 0x32 special (`0x431f70`) |
| +0x06 | s16 | "active this frame" (visible to a player, or bomb/fire/AI) |
| +0x08 | s16 | status; -1 = free slot. 7 = fallen bike/burnt frame, 1/8 select door deltas |
| +0x0a | s16 | accel/steer input for dummies |
| +0x14/+0x18, +0xac/+0xb0 | s32 | front / rear wheel point x,y |
| +0x1c | s16 | speed (signed) |
| +0x22 | s16 | model id (index into `0x4be178`). Special models: 0x25 tank, 0x2f odd model ignored by car collision, 0x2a truck with attachment, 4/5/0xf/0x10/0x20/0x2a emergency (`0x4769e0`) |
| +0x28 | s16 | max speed (from info) |
| +0x34 | s16 | vtype (car_info +0x6a): 0 bus, 3 bike, 4 car, 8 train, 9 tram, 13 boat, 14 tank |
| +0x38..+0x7b | | collision shape: 5 x + 5 y ints (4 corners + centre) + z; saved copy at +0x1dc |
| +0x8a/+0x8c | s16 | half width / half length (pixels) |
| +0x90, +0xb4 | s16 | front / rear wheel heading; +0x96 turn delta, +0x92 turn progress |
| +0x9c | s32 | bomb state: 1 armed on entry -> 2 timer (+0xa0 counts down, beep at 0x7c), 4 detonate on damage, 5 speed bomb arms at 3/4 max speed ("click"), 6 explodes below 1/2 speed |
| +0xa2 | s16 | current road dir bits; +0xd4 previous |
| +0xd8 | s16 | AI driver record index (`0x507ea0` table) |
| +0xee | s16 | siren type (models 0x13/1/0x2b give 1/2/3) |
| +0xf0 | s16 | tank turret angle offset / bike lean counter |
| +0xfc | s16 | damage 0..100 (100 = explodes; 0x65 = wreck) |
| +0xfe | s16 | burning timer (>0 = on fire) |
| +0x104 | s16 | cruise speed |
| +0x108 | s32 | z offset above ground (0x20000/0x30000/0x40000 by model) |
| +0x10e | s16 | lane offset in block (7 or 0x37 pixels) |
| +0x11a/+0x11c | s16 | horn/siren flags |
| +0x128 | s16 | owner status: 1 normal, 2/3/4 transient, 99 mission-locked (no damage) |
| +0x13c | s16 | player index of driver |
| +0x14c | f32 | engine thrust (scaled down by damage > 25) |
| +0x150 | f32 | skid amount |
| +0x188..+0x1b0 | f32 | physics state (x,y floats at +0x190/+0x194, angle in radians at +0x198, velocity +0x19c/+0x1a0) |
| +0x220/+0x224/+0x228/+0x22c | | next position x,y,z and heading (committed by `Car_CommitMove`) |
| +0x24c | u8 | remap |
| **+0x250** | sprite | embedded sprite object: x +0, y +4, z +8, ground z +0xc, remap +0x10, sprite number +0x16, heading +0x18 (=car+0x268), delta mask +0x1c, sprite info ptr +0x48 (`0x773e38[sprite]`) |
| +0x2ac | ptr | `car_info` record |

The same sprite sub-object layout appears in peds (ped+0x90). `0x47c960` sets the sprite number, `0x47c9c0` the remap
and `0x47c9f0` the position. The Sprite_* deltas in this range use the mask at +0x1c.

### car_info (style G24), checked against `STYLE001.G24`

`CarInfo_Setup` 0x40c100 walks `car_size` bytes. Each record is `0xae + doors*8` bytes, with the door count
as s16 at +0xac. STYLE001 has 38 records that add up to exactly `car_size` = 6972. Field offsets:

| Off | Field | Notes |
|---|---|---|
| +0x00/+0x02/+0x04 | w, h, depth (s16) | e.g. model 0: 30x62x10, tank 50x96x24 |
| +0x06 | sprite_num (s16) | made absolute by adding a per-vtype base (`0x774ef0/ee8/ef6/eea/f04/efc/efe`) |
| +0x08 | weight | |
| +0x0a/+0x0c | max/min speed | used by `Car_DummyThrottle` |
| +0x0e/+0x10 | acceleration, braking | |
| +0x12/+0x14 | grip, handling | |
| +0x6a | vtype (u8) | |
| +0x6b | model (u8) | `Cars_Init` builds `model -> record index` in `0x4be178` (s16[100]) |
| +0x6c..+0x75 | turning, damagable, value[4] | assumed from cds.doc order |
| +0x76/+0x77 | centre of mass x/y (s8) | tuning keys `centre_of_mass_x/y` |
| +0x78 | moment of inertia (fixed, stays int) | |
| +0x7c..+0x94 | mass, gear_1 thrust, tyre_adhesion_x, tyre_adhesion_y, handbrake_friction, footbrake_friction, front_brake_bias | fixed 16.16, converted to float in place (`* 1/65536`, `0x430490`) |
| +0x98/+0x9a/+0x9c | turn_ratio, drive_wheel_offset, steering_wheel_offset (s16) | turn_ratio drives player steering in `Car_ApplyPlayerControls` |
| +0x9e/+0xa2 | back_end_slide, handbrake_slide | fixed -> float |
| +0xa6 | convertible flags (bit0 = driver visible, bit1 = some looping delta) | |
| +0xa7 | engine sound (sample = engine+0x2d) | |
| +0xa8 | radio station type | `Music_UpdateRadio` |
| +0xa9 | horn (siren when >= 0x3c; 127 on models 4/5) | |
| +0xaa | sound function (0..5: 2 boat, 4 tank, 5 bus air-brake) | |
| +0xab | fast change flag | |
| +0xac | doors (s16) then door[] 8 bytes each | |

Record pointers live at `0x5f2ce0` (pointer table `0x501574`, count `0x501570`, max 256).

### Map and world tables used here (owned elsewhere)

- `0x5c1c1c` base[256*256] (dword offsets), `0x5c2c70` column data (first s16 = height offset, then block
  ids), `0x5c0bfc` block_info[] (8 bytes: type_map u16, type_map_ext u8, left, right, top, bottom, lid).
  Readers: `0x437ae0` (block ptr), `0x438900`/`0x4388a0` (type map), `0x438800` (covered?), `0x438950` (face tile).
- **`0x55fab0`**: byte per cell `[z][y][x]` (index `x + (z*256+y)*256`), a cache of the type-map low byte.
  Bits 0-3 are the direction bits, bits 4-6 the type (0 air, 1 water, 2 road, 3 pavement, 4 field, 5 building,
  6/7 also treated as road by the car spawners), and bit 7 marks flat/slope. The neighbours `0x55f9b0`/`0x55fbb0` (y-/+1)
  and `0x55faaf`/`0x55fab1` (x-/+1) are read directly, which shows x varies fastest. It is filled outside this range.
- `0x4544e0(x,y,z)`: ground z under a point.
- **Object grid**: `0x5278f8` holds 128x128 list heads, with cell = coord>>23 (2x2 blocks) and per-cell counts in `0x537908`.
  A node is {u8 type, s16 id, ptr obj, next}. Insert is `0x436c30(type,id,obj,spr,x,y)`, remove is `0x436e30`.
  Queries are `0x4363c0`/`0x4354b0`, and `0x436b20` ends a query (there is a reentrancy guard `0x527018`).
  Types: 1 ped, 3 object, 6 car, 8 (hit-tested by projectiles), 10, 0x1e heli.
- Ped/object table: `0x7284e0`, stride 0x100 (`Ped_Get 0x44f500(id)`). +0x90 x, +0x94 y, +0x98 z, +0x49 health, +0x70 state (0xc = down/dead, 0x17, 0x18).
- Static objects: `0x6b40d0`, stride 0x88 (`0x44c2d0`). The spawner is `0x44cef0(x,y,z,kind,angle)`; kinds include
  10/11/0x11 skid marks, 0x43 smoke, 0x36 fire, 0x5c shadow, 0x5d.
- AI driver records: `0x507ea0`, stride 0x98, 0x81 entries (`0x41ad60`).
- Players: `0x74f148`, stride 0x1bc. +2 ped id. +4 view rect {minx,maxx,miny,maxy,margin} in pixels.
  +0xbc mode (0 = in car), +0xc0 car index, +0xd0 camera target kind. +0x150 wanted points, +0x154 wanted level.
  The local player is `0x74f83c` (also used as the sound listener: `0x462d50(p)` returns the camera target position).
- Incident/target table `0x50cab0`, 10-byte entries {.., x, y, z (block bytes at +2..+4), state +5, link s16 +8}.
  Index = ped id, or car index + 0x26c. The active list is at `0x505048`, count `0x50584e`. Used for wrecks and dead peds.

### Other globals owned by this range

| Address | Size | Meaning |
|---|---|---|
| 4bbb80 | u32 | game frame counter (incremented by `Event_Tick`). The demo/time limit in 0x430c00 compares it with 10000 |
| 4bbb84/4bbc64, 4bbb88.. | 10x20 B | event node free list / sorted queue |
| 4bbc68, 4bbc70 | 64 x 0x74 | block face animations: +0 is-lid, +4 tile slot, +8 active, +0xc tick, +0x10 rate, +0x14 step, +0x18 event type, +0x1c event arg, +0x20 n steps, +0x24 {dword, tile byte}[] |
| 4ab040 / 4ab041 / 4ab058 | u8 | music enabled / sfx enabled / music usable |
| 4bddf0 | u8 | sound system running |
| 4bdc88 | 10 x 0x1c | wanted loop list {s16 src, sample, rate, dist, vol, pan, flag}, sorted by distance |
| 4bdb60 | 10 x 0x1c | playing loop channels |
| 4bd978 / 4bdc78 / 4bdc80 | 100 dwords + head/tail | police scanner speech queue |
| 4ab048 / 4ab050 | 8 B each | music / sfx volume tables |
| 501aa0..501aac, 501578.. | | music: on flag, sequential mode, current track (0..9), state (0 off,1 stopped,2/4 playing,3 paused), per-track {start,length,pos} |
| 4ab270 / 501ab0 / 4ab2e0 | | radio stations: 3 track ids per station, rotating index, current station (10 = special) |
| 501ac0 | 4 x 0x3a | police pursuits |
| 502dd8 | 4 x 0x2c | criminal records (target ped +8, wanted +0x24, 3 sightings at +0x12) |
| 501bc8..501c14 | | heli state (state 501be0 1..8, sprite pos 501bfc/c00/c04, speed 501be8) |
| 501c5c/501c60/501d04/501d08/501c64/501d18/501cac | | collision bounds of the current shape |
| 501d10 | s16 | collision kind (-1 none, 2 solid, 3 slope, 4 wall, 5 ped, 6 car, 7/8 object, 9-12 combinations) |
| 523ff0 | 0x95 x 16 | gathered object hits, count 501d16 |
| 50f290 / 505840 | 10 x s16 | active ambulance crews |
| 505898/50589a/5058a0 | | hospital block x,y,z |
| 502e88 | 10 x 16 | sprite-delta composite cache {buffer, info ptr, mask, lru stamp} |
| 502f34..5031f8 | | launch options (`Game_SetOptions`): 502f40 peds on, 503180 cars on, 502f6c sound on, 502f50 timing, 5031cc AI on, 5031e6 debug text, 5031bc demo, 5031f8 audio mode |
| 504cc8 / 504ccc | ptr | MGL display DC / offscreen DC. 504cc0/504cc4 screen w/h. 503214 active page |
| 504cd0, 504cd4..dc | | video mode table (0x1c each) and per-depth (15/16/32 bpp) sorted lists |
| 51322c / 513230 | | in-game exit flag / exit reason (`Game_SetExit` 0x4309e0) |

## Data formats parsed here

- **car_info**: see above (verified).
- **config.ini tuning** (`Tune_LoadFile`): reads `..\gtadata\config.ini` (not shipped) into a 4 KB buffer.
  Each entry is `[name] value` with an optionally negative integer value, passed both as an int and as `value/65536.0`. Names start with
  `car` + model number, followed by the parameter name. Unknown models are ignored.
- **Music**: `..\music\track1.wav`..`track10.wav` (table 0x4ab2e8, stride 0x108) through Miles `AIL_open_stream`.

## Call-graph links to other ranges

- **WinMain 0x437230** calls `Game_SetOptions`, `Gfx_InitDrivers` 0x415310, `Game_Run` 0x4148a0, `Gfx_Shutdown`, `Input_GetKey`, `Audio_Shutdown`.
- **Game_Run 0x4148a0** (in-game frame loop). It calls 0x47ced0/0x44ab90/0x438200/0x47d390 (loaders),
  `World_Init 0x430a20`, `Tune_LoadFile`, net start 0x44bd30, `Gfx_SelectMode`, style load 0x47cd10. Then it loops
  until `0x51322c` != 0. Each iteration: `0x430da0` (present), read input bits `0x432e00` into `0x5031a8[player]`,
  net exchange `0x44b930`, then per player `0x461740` (set current), `0x463ec0` (player update), `0x4643a0`/`0x430dc0`
  (key actions such as the radio change through `Music_NextStation`) and `0x43cb20`. After the players it calls `0x430b20`
  (sim step, which calls **World_Tick 0x430c00** unless paused, then render 0x43b910/0x43b7e0/0x43b780), then `0x430d40`, then `Audio_Update`.
- **World_Init 0x430a20** calls `Audio_EnterGame`, `Event_Init`, `Cars_Init` (if cars are on), `Heli_Init`, `BlockAnim_Reset` (via 0x430a20).
- **World_Tick 0x430c00** calls `Event_Tick`, `Cars_UpdateAll`, `Heli_Update`, peds 0x45cd50, `0x419880` (AI/world objects;
  **the dump has no decompilation for it ("FAILED")**: it calls `Police_SpawnRoadblock`, `Police_UpdatePursuits`, `Ambu_AssignVictim`,
  `Car_TryRemoveWreck` and `Police_TickRadioReports`), and `BlockAnim_Tick`.
- The style loader 0x47cf10 calls `CarInfo_Setup`. The sprite renderer 0x47bc00/0x47c130 calls `Sprite_GetComposite`, and 0x47ca50 calls `SpriteCache_Init`.
- The camera 0x462d50 calls `Car_GetCamTarget`. The AI driver 0x41aed0 calls `Ambu_Update`/`Ambu_Remove`. The traffic generator 0x4183d0
  calls `Traffic_FindRecyclable` and `Police_CopsForWanted`. 0x418970/0x418b40/0x418c80/0x418dc0 call `Traffic_RespawnCar`.
- Mission script code calls into this range: 0x4471a0 calls `Heli_Spawn`, `Police_ReportCrime` and `Event_Schedule`, 0x475930 calls `Car_SpawnEx`,
  and 0x4740f0 (door create) calls `BlockAnim_Create`.
- Sound back end: Miles wrappers 0x4722f0..0x473430 (channels `0x7709b0..`, sample table `0x7709c8` stride 12) and
  `0x471be0`/`0x471f10` (bank load).
- Network back end: 0x487000..0x4879a0. Video back end: SciTech MGL 0x486830..0x48e000 (see `tools/ghidra/names.tsv`).

## Port priorities

(a) **Loading and drawing the city**: `CarInfo_Setup` 0x40c100 (car sprites and stats from the G24), `Gfx_InitDrivers`/`Gfx_SetVideoMode`/
`Gfx_SetPalette`/`Gfx_Present` (replace with SDL), `BlockAnim_*` (animated faces), `Sprite_GetComposite` 0x4143a0
(car damage/door deltas). The map loader and renderer themselves are outside this range.

(b) **Player in a car**: `Car_Init` 0x4067c0, `Car_SpawnEx`/`Car_SpawnOnRoad`, `Cars_UpdateAll` 0x40adc0, `Car_Update` 0x40a640,
`Car_ApplyPlayerControls` 0x40a2e0, `Car_CollideMap` 0x411280, `Coll_MapAll` and its helpers 0x40e350-0x40ffe0,
`Car_BisectMove` 0x405ed0, `Car_CollideObjects`/`Car_CollideCar`, `Car_UpdateGround` 0x412310, `Car_UpdateWreck`
0x408af0, `Car_GetCamTarget`. The physics integrator 0x460be0/0x461570 is outside this range. Traffic is next:
`Car_DummyDrive` 0x416610, `Car_DummyFollowRoad` 0x408440, `Car_DummyMove` 0x409a30, `Car_DummyThrottle` 0x4170e0.
On foot: nothing in this range beyond the ped/object layout above.

(c) **Frame loop**: `Game_Run` 0x4148a0, `Event_Init`/`Event_Tick`/`Event_Schedule`, `Snd_UpdateGame` 0x405510
(with `Snd_GatherLoops`, `Snd_UpdateLoopChannels`, `Snd_Play3D`), `Music_*`.

## Open questions

- The ambulance identification rests on: model 5 is spawned, the crew walks to a "down" ped (state 0xc), and after 75
  frames sets its health to 100 and state 2, with rear-door animations. Hospital coords are at 505898. Confirm this in game.
- Heli: it uses the sprite of car_info model 88 (a vtype 0 record in STYLE001), lands (z rising toward ground-0x420000)
  and climbs (z falling to 0x20000). That implies **world z grows downward (0 = sky)**, which needs checking against the map renderer.
  Its state 7 schedules the level-exit event.
- `0x419880` failed to decompile in the dump. Re-decompile it in Ghidra, because it drives the police and ambulance systems.
- Car fields +0x110, +0x119, +0x120, +0x134 and +0xc2 lane modes 1-4 are only partially understood. Model 0x2f
  (w29 h30 depth 1, no doors) gets special treatment; it may be a non-car vehicle such as a train/tram part or a turret.
- Wanted thresholds: `Police_SetWantedCity1` handles levels 1-2, `City2` handles levels 0x66/0x67, and `City3` handles 0xca/0xcb and any other value
  (`0x6b3e28` is the current level id). Map the level ids to cities.
- The 0x4ab270 station table and the meaning of station 10 (music state 4) are unknown.
