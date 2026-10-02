# Inventory 4: 0x463100 to 0x476500 (300 functions)

Names are in `tools/ghidra/names.tsv`. All coordinates below are in the game's world format unless noted:
16.16 fixed point where one map block is 64 units, so `block = coord >> 22`, a block centre is
`b * 0x400000 + 0x200000`, and z uses the same scale (levels 0..5). Angles are 10-bit (0..0x3ff, one
full turn). A car facing "down" (+y) has angle 0, +x is 0x100, -y is 0x200, -x is 0x300 (from
`Police_SpawnCar`, which converts road direction bits to a heading).

## Module map

| Span | Module | Purpose |
|---|---|---|
| (<0x463100) .. 0x464e1f | player | Per-player record (4 slots): state, input bytes, power-ups, lives, score, viewport, camera, death/bust |
| 0x464e20 .. 0x46a09f | police | Criminal record, police controllers, pursuit groups, roadblocks |
| 0x46a0a0 .. 0x46a99f | powerup | Power-up (crate) table: add, reveal, collect |
| 0x46a9a0 .. 0x46e81f | train | Trains: creation from map list, track following, stations, doors, crashes, player riding |
| 0x46e820 .. 0x46e9df | config | Registry: language and 10 control keys |
| 0x46e9e0 .. 0x471b8f | path | Block-grid best-first pathfinder for AI cars, plus CMP route-section parser |
| 0x471bc0 .. 0x473430 | snd | Miles (MSS32) sample manager: init, banks, channels, 3D sfx, voice, music stream |
| 0x473440 .. 0x473b8f | dummy | Mission DUMMY cars driving to a target, convoy groups |
| 0x473b90 .. 0x4756ff | mission | Mission runtime objects: on-screen tests, sound sources, timed bombs, ranking, triggers, doors, crane |
| 0x475700 .. 0x475720 | mission | Three jump thunks into 0x425520 / 0x4258d0 / 0x425780 (mission object builders) |
| 0x475730 .. 0x476500+ | miscar | Small car/ped helpers called by the mission interpreter (continues past my range) |

The player module clearly starts before 0x463100 (0x461xxx/0x462xxx contain the player getters
0x462c30, 0x462c50, 0x462d50, 0x462ef0, 0x462910 crime counters), and miscar continues past 0x476500.

## Key functions

**player** (record base 0x74f148, stride 0x1bc, 4 records; iterate with 0x412a70 first / 0x412a90 next,
which only walks more than player 0 in network games):

- `Player_InitAll` 0x463290: full reset; cheats give 999999999 score and 99 ammo in every slot.
- `Player_ApplyInput` 0x463ec0: decodes the packed control word into control bytes (see layout below).
  Dispatches enter/exit: on foot -> `0x453210`/`0x407000`; in car -> `0x453260`; train -> `Train_Command`.
- `Player_UpdateAll` 0x464880: per-frame tick. Handles busted respawn (halves multiplier, strips armour
  and weapons unless the jail-free flag is set, shows criminal record), counts timers down, expires power-ups,
  and handles multiplayer frags (`Player_UpdateFrags` 0x464c90).
- `Player_Wasted` 0x463100 and `Player_Busted` 0x464820: show FXT keys 4004 "WASTED!" and 4003 "BUSTED!"
  (verified by decoding ENGLISH.FXT) and update lives.
- `Player_EnterCar` 0x463a10, `Player_ExitCar` 0x463c40, `Player_SetOnFoot` 0x463dd0, `Player_BoardTrain`
  0x463d50: switch player state and the camera target.
- `Player_SetViewport` 0x464500 / `Player_GetViewport` 0x4644e0 / `Player_GetCamera` 0x464580.

**police**:
- `Cop_Update` 0x466f10 (1750 lines). This is the state machine for police AI controllers, with the state byte
  at controller+0x1b: 1 patrol, 2-6 routing and driving to path nodes, 10/11 waiting, 0x6e/0x6f cop leaves the car
  and runs, 0x96 roadblock cop, 0xbe/0xbf foot cop returns, 200..0xda pursuit modes. In 0xda the cop
  arrests the player (`Player_Busted`). 0xfe means free/dismissed.
- `Police_SpawnCar` 0x4654f0, `Police_SpawnPatrolCars` 0x465300, `Cop_Release` 0x465a40,
  `Roadblock_TryPlaceAhead` 0x466230 (at wanted level 3 or higher), `Roadblock_Spawn` 0x466450,
  `Cop_SetSpeedByWanted` 0x466e80 (top speed 0x19/0x1e/0x23/0x26).
- `Police_ShowCriminalRecord` 0x464ec0: crime counters 2..9 map to crimeRTA, HAR, HIJ, CAR, GTA, SHO, MUR, BAN.

**powerup**: `PowerUp_Collect` 0x46a560. Types 1-4 are weapons (pistol 20, machine-gun 20, rocket 5,
flame 10 ammo by default, from table 0x4a8c18 {char *fxtKey, int ammo}). 6/7/8 are speed-up (car or foot),
9 bribe, 10 armour (3 hits), 11 multiplier+, 12 jail-free, 13 life+, 14 help sign (`help%d`), 15 life+ with voice.
Values >= 100 in the ammo field mean a temporary weapon that `Player_Wasted` restores.

**train**: `Train_InitAll` 0x46a9a0, `Train_UpdateAll` 0x46a9d0, `Train_Update` 0x46bd60. Track following
uses `Train_BogieNextPiece` 0x46c5d0 (probes neighbouring rail blocks with `0x44b310(1,..)`) and
`Train_BogieComputePos` 0x46cdb0, which uses the curve table at 0x4b22e8 (4 bytes per step: dx, dy, angle;
0x5a steps per curve). `Train_Crash` 0x46d650 and `Train_Command` 0x46a9f0 are the player interface.

**path**: `Path_Find` 0x4716f0(sx,sy,sz, dx,dy,dz, mode, ctrl) runs an incremental best-first search. Only one
controller can own the search at a time (owner in 0x4b3094, -1 when free). `Path_SearchStep` 0x470dc0 has a node
budget and returns 3 when it needs to continue next frame. `Path_ExtractRoute` 0x46ea10 writes the result into the
controller's path slot.

**snd**: `Snd_LoadLevelBank` 0x472300, `Snd_OpenVocalBank` 0x472470, `Snd_Play3D` 0x472a30,
`Snd_PlaySfx` 0x472e00, `Snd_PlayVoice` 0x472c60, `Snd_MusicPlay` 0x4732f0 / `Snd_MusicService` 0x4733a0.

**mission**: `Door_Create` 0x4740f0, `Trigger_Create` 0x4744a0, `Crane_Create` 0x475620 / `Crane_Update`
0x475090. These back the MISSION.INI object kinds DOOR, TRIGGER (and its variants) and CRANE. `Mission_SortPlayerRanks`
0x474a50 handles multiplayer ranking.

## Globals and structures

### Player record: 0x74f148 + i*0x1bc, i = 0..3

| Off | Abs | Type | Meaning |
|---|---|---|---|
| 0x000 | 0x74f148 | u8 | bust state: 0 none, 1 just busted, 2 processing |
| 0x001 | 0x74f149 | u8 | flag (setter 0x463830) |
| 0x002 | 0x74f14a | s16 | player's ped id (Ped_Get 0x44f500) |
| 0x004..0x010 | 0x74f14c | 4 x int | camera box xmin, xmax, ymin, ymax (integer world units; `0x462c30` returns it) |
| 0x018 | 0x74f160 | block | viewport: +0 w/2, +4 h/2, +8 w, +0xc h, +0x10 w<<16, +0x14 h<<16, +0x2c non-4:3 flag |
| 0x048 | 0x74f190 | block | camera (used by 0x43cac0: +0x10/+0x14 target, +0x20/+0x24 position, +0x60 mode) |
| 0x0ac | 0x74f1f4 | block | unknown sub-block |
| 0x0bc | 0x74f204 | int | state: 0 in car, 1 on train, 2 on foot |
| 0x0c0 | 0x74f208 | int | id for state: car index / train index / ped id |
| 0x0d0/0x0d4 | 0x74f218 | int,int | camera-follow kind and id (kind 3/4 are transitional) |
| 0x0e4 | 0x74f22c | char[] | player name (used by frag messages) |
| 0x0f4 | 0x74f23c | int | score (cheat sets 999999999) |
| 0x0f8 | 0x74f240 | u16 | score multiplier (1, or 10 with cheat; halved on bust) |
| 0x0fa | 0x74f242 | u8 | get-out-of-jail flag (keeps weapons on bust) |
| 0x0fc..0x120 | 0x74f244 | 10 x int | zeroed on init (likely crime counters/stats) |
| 0x124/0x127/0x131 | 0x74f26c.. | char[] | formatted HUD strings (%02d, %09d) |
| 0x158 | 0x74f2a0 | u8[4] | ammo per weapon 1-4; 'd'(100) marks a temporary weapon |
| 0x160/0x164 | 0x74f2a8 | int,int | current weapon / saved weapon |
| 0x168..0x16a | 0x74f2b0 | u8[3] | saved values restored when a temporary weapon expires |
| 0x174 | 0x74f2bc.. | | short timers |
| 0x184 | 0x74f2cc | int | unknown (local-player getter/setter) |
| 0x188 | 0x74f2d0 | u8 | mode 0..2 (asserted) |
| 0x190 | 0x74f2d8 | u8 | train-door request pending |
| 0x191..0x196 | 0x74f2d9.. | s8[6] | control bytes: accel/brake, axis2, fire, steer (-8..8), move flag, handbrake/special |
| 0x197 | 0x74f2df | s8 | lives (4 single player, 0xff infinite) |
| 0x198 | 0x74f2e0 | s16 | on-foot aux (-4/0/4) |
| 0x19c | 0x74f2e4 | int | multiplayer frags |
| 0x1a0 | 0x74f2e8 | u8 | frag processed this death |
| 0x1a8 | 0x74f2f0 | s16 | armour hits left |
| 0x1aa | 0x74f2f2 | s16 | speed-up timer (375 frames for foot speed-up) |
| 0x1ac..0x1b0 | 0x74f2f4 | s16 x3 | countdown timers (-1 = off) |
| 0x1b2/0x1b3 | 0x74f2fa | u8,u8 | local-player flags |
| 0x1b8 | 0x74f300 | int | car-alarm timer (300) |

Player-related globals: 0x74f83c view/HUD player, 0x74f844 local (input) player, 0x74f848 initialised flag,
0x74f84c player count / game mode (1 single player; 2 or more enables frags), 0x74f850 packed block target
(x in bits 8-15, y in bits 0-7, z in bits 16-23), 0x502f78 shared message string buffer.

### Control word passed to `Player_ApplyInput`
Bit 0 means a steer value is present in bits 9-12 (4-bit, values above 8 are negative). Bit 1 means an
accelerate/brake value is present in bits 15-16. Bit 7 means a second axis is present in bits 13-14. Bit 2 means
fire, with the value in bit 20. Bit 3 is enter/exit (vehicle/train door; bit 21 qualifies it). Bit 22 is the
`Player_ToggleVehicle` action. Bits 4 and 18 together mean next weapon (0x4619f0); bits 8 and 17 together mean
previous weapon (0x461a50). Bit 5 sets the handbrake/special byte from bit 19. Bit 6 means bits 23 and up carry a value
(`Input_GetHighBits`).

### Tables used heavily from other ranges (layout as seen from this range)
- Cars: 0x4be248, stride 0x2b0, 400 slots (`0x408200`). Fields: +2 driver ped, +4 control state, +0x22 model,
  +0x28 top speed, +0xa2 road direction bits, +0xfc damage (>99 wrecked), +0x116 drive mode, +0x11a alarm/siren,
  +0x119 flag, +0x248 held by script, +0x250/0x254/0x258 position x,y,z, +0x268 heading. Each car's driver ped
  slot is `car + 200`.
- Peds: 0x7284e0, stride 0x100 (`0x44f500`). Fields: +0x49 alive/health, +0x70 state, +0x8c train index,
  +0x90/0x94/0x98 position.
- AI controllers: 0x507ea0, stride 0x98, 129 slots (`0x41ad60`, alloc `0x41ad30`). +0 id, +2 kind (2 police,
  9 dummy), +8 path slot, +0x1b state, +0x1e car id, +0x38 path search progress, +0x40 car pointer,
  +0x48 current path index, +0x5e roadblock, +0x60 foot ped, +0x64..0x66 target block, +0x6c pursuit group.
- Police targets: 0x502dd8, stride 0x2c, 4 entries (`0x414230`): +4 kind (0 car, 1 foot, 2 train), +8 ped,
  +0xa car, +0xc train ped, +0xe wanted active. Wanted level comes from `0x461960(ped)`.
- Pursuit groups: base 0x501ac4, stride 0x3a. +0 lead cop, +2 lead flag, +6 target index, +8 cop list (s16[]),
  +0x30..0x33 flags, +0x33 cop count.
- Police car list: 0x50f2a8 (s16[100]), count 0x5058a2. Initial spawn count is 0x5058a4.
- Roadblocks: 0x505f00, stride 0x5c (+0 id, +2 status, +0xc vertex-set id, +0xe active, +0x10 timer 400,
  +0x12 car list, +0x5a count). Active list 0x50586c[20], count 0x50584a.
- Object delete queue: 0x504f68 (s16[100]), count 0x505ef8.
- Voxel type cache: 0x55fab0, one byte per cell, `[x + y*256 + z*65536]`, z = 0..5. This is the low byte of the
  CMP block `type_map`. Bits 0-3 are direction (1 = -y, 2 = +y, 4 = -x, 8 = +x). Bits 4-6 are the block type
  (2 road, 3 pavement, 5 building; tested as `& 0x70` against 0x20/0x30/0x50). The full block word is read with
  `Map_GetBlockInfo` 0x438900 (tables 0x5c1c1c base, 0x5c2c70 columns, 0x5c0bfc blocks), and slope type is bits 8-13.
- Trig table: 0x511e28, int, indexed by 10-bit angle. 0x512228 is the same table offset by 256 entries (a
  quarter turn). It is built at runtime (BSS).

### Power-ups: 0x74f858, 256 x 0x1c bytes
+0 x, +4 y, +8 z (world), +0xc object id (-1 free), +0x10 visibility (1 crate, 2 revealed), +0x14 type,
+0x18 value (0 = default ammo, <100 ammo, >=100 temporary weapon).

### Trains: 0x7514a8 + t*0x5c8, count 0x75377b
Header: +0 kind (1 = four-carriage train, 2 = single unit; chosen by `0x44b310(3,..)` at the spawn block),
+1 direction state (6/7), +2 state (2 station sequence, 3 run, 4 brake then go to +9, 9 signal wait, 10 leave
station, 0xb/0xc boarded transitions, 0xd crashed), +3 boarded (1 no, 2 yes), +4 speed (x10), +0x11 door
carriage, +0x15/0x16 current/next station, +0x18/0x1a timers, +0x22 max speed (0x3c, or 0x50 when boarded, for
kind 1), +0x23 door frame 1..8. Carriage c is at +0x24 + c*0x168. Inside a carriage there are two bogies at
+0 and +0x6c, each with position ints at +0, block bytes at +0x5c..0x5e, sub-step at +0x5f, track piece at +0x61
(1..12), slope state at +0x64, and curve index at +0x67. The carriage centre xyz is at +0xdc, heading at +0xf4, state at
+0x139 (0xe alive, 0xd wrecked), hit counter at +0x13a, and door objects at +0x14a/+0x15e.
The map spawn list comes from 0x77d368 (`0x47e600`): u8 count, then five u8[20] arrays at +1 (direction 3/6/9/12),
+0x15 x, +0x29 y, +0x3d z, +0x51 station.

### Pathfinder
Start node 0x7537b8, goal 0x75cd3c (x,y,z,flags). The node pool is at 0x75cd58, 16 bytes per node: x,y,z,
dirflags, u16 cost, +8 next in open list, +0xc parent. The node count is 0x7705dc, with a maximum of 0x1374. The mode is 0x7537b0
(2/3 police road following, 4 allows pavement, 5 dummy). Path slots are at 0x7537d0 + slot*0xff, holding 85 xyz byte
triples terminated by 0,0,0. Slots from 50 up hold CMP routes. Owner is 0x4b3094 and the last result is 0x7537b2.

### Mission runtime
- Triggers: 0x771628, 210 x 0x20. +0 state (0 armed, 1 disarmed, 3, 4 off), +4 type, +8/+0xc params,
  +0x10..0x12 block xyz, +0x14/+0x18 params, +0x1c, +0x1d permanent. The creation count is 0x7710f8.
  Car-trigger bindings: 0x773068, 25 x {trigger, car}.
- Doors: 0x773220, 64 x 0x28. +0 animation id (0x402250), +4 state (0 closed, 1 open, 2 opening, 3 closing),
  +8 locked, +0x10 orientation 0..3, +0x14..0x16 block, +0x18/0x1c anim params, +0x20 open mode,
  +0x22/0x24 opener filter, +0x26 face slot, +0x27 permanent. The count is 0x7710f0 and 0x771618 is the unlocked count.
- Cranes: 0x773130, stride 0x3c. +0 state 0..10, +4 object, +8..0x10 x,y,z, +0x14 direction, +0x18 car being
  handled, +0x1c count, +0x20 s32[6] stacked cars. The count is 0x771108.
- Dummy groups: 0x771080, 8 x 10 bytes {count, s16 members[4]}.
- Timed bombs: 0x7715d8, 4 x 0x10. Positional sound sources: 5 slots (flags 0x77161c, data 0x4bdb10).

### Sound (Miles)
Bank tables: LEVEL at 0x7709c8 (131 x 12 bytes) and VOCALCOM at 0x770640 (71 x 12). The level RAW is read whole into
the locked buffer 0x770634 (1 MB limit). Voice samples are read on demand into buffer+1 MB. Handles: 10 loop
channels 0x77060c, 3 one-shots 0x770ff4, 3D one-shot 0x7709ac, frontend 0x771074, voice 0x771008, talk 0x7709b0,
and round-robin 4+4 at 0x7709b4/0x77099c. The digital driver is 0x771000, the stream is 0x770608, and the enabled flag is 0x771078.
sfx volume is 0x4b3114 and music volume is 0x4b3110.

## Data formats verified against game/GTADATA

- **SDT** (AUDIO/*.SDT): an array of 12-byte records `{u32 offset, u32 length, u32 rate}` into the matching .RAW.
  LEVEL001.SDT is 1572 bytes (131 records), VOCALCOM.SDT is 852 bytes (71 records), and the first entry is
  (0, 6828, 14800). This matches the fread counts 0x83 and 0x47 in `Snd_LoadLevelBank` and `Snd_OpenVocalBank`.
  Music is `AUDIO\%d.WAV` (0..5), streamed.
- **CMP route section** (`Route_LoadCmp`): it starts right after the object-position section, at
  `28 + 256*256*4 + column_size + block_size + object_pos_size`, and is exactly `route_size` bytes long. It is a
  sequence of `{u8 n, u8 type, n x {u8 x, u8 y, u8 z}}`. Type 0xFF and 0xFE routes are copied into path slots
  50+ (they are counted separately; NYC has 1 0xFE and 22 0xFF, SANB 36 0xFF, MIAMI 40 0xFF). Any other type
  is a roadblock vertex set indexed by type, with pointer/count stored in 0x5f2b50[type] (8 bytes) and vertices
  pooled at 0x5ce3c8 (300 maximum). NYC has 29 such sets with 3-5 vertices each. I parsed all three maps and the
  sections end exactly at `route_size`.
- **Registry**: HKLM `SOFTWARE\DMA Design\Grand Theft Auto` value `Language` (DWORD). The subkey `...\Controls` has
  values named by the pointer table 0x4b2f90 ("Control 0".."Control 9"); these are DWORD key codes stored at 0x753784.
- **FXT** keys used here: 4003/4004, crimes, crimeRTA.., kill-by, you-kill, pistol, machine-gun, rocket,
  flame, car-speed+, speed+, bribe, armour, multiplier+, jail-free, life+, help%d, crane_screwed. ENGLISH.FXT
  decodes as: subtract 1 from every byte, and also subtract 0x63,0xc6,0x8c,0x18,0x31,0x62,0xc4,0x88 from the first 8 bytes.

## Call-graph links into other ranges

- 0x430a20 (level start) calls `Player_InitAll`, `Train_InitAll` and `Path_Reset`.
- 0x430c00 (per-frame game step) calls `Player_UpdateAll` and `Train_UpdateAll`.
- 0x4148a0 (input/network dispatch) calls `Player_ApplyInput`.
- 0x41aed0 and 0x42e860 (AI controller dispatch) call `Cop_Update` and `Dummy_Update`.
- 0x419880 (police manager) calls `Police_FlushObjectDeleteQueue` and `Police_SpawnCarAtTarget`.
- 0x438200 (map loader) calls `Route_LoadCmp`.
- 0x445800 and 0x4471a0 (MISSION.INI object creation and command interpreter) call `Door_*`, `Trigger_*`,
  `Crane_Create`, `PowerUp_Add`, the `MisCar_*` helpers and the 0x4757xx thunks. 0x479ab0 (mission reset) and
  0x479e00 (mission frame update: `Crane_Update`, `Mission_UpdateTimedBombs`) are also mission-side.
- 0x405460, 0x4054a0 and 0x405600 (sound system init/shutdown) and the 0x404xxx game sound layer call the `Snd_*`
  functions. 0x404e90 is "play voice n".
- WinMain 0x437230 calls `Config_ReadRegistry`, and 0x432b40 calls `Config_MapControlKeys`.
- 0x446fe0 calls `Player_Wasted`.
- Outgoing helpers worth naming centrally: 0x422900 fatal error(code, line, value), 0x47d9a0 FXT lookup,
  0x4814f0/0x481a40/0x481d40 HUD messages (big, ticker, typed), 0x49cb45 sprintf, 0x49d1c5/0x49d510/0x49d67b/
  0x49d707/0x49d11d fopen/fread/fseek/ftell/fclose, 0x49cb27 rand, 0x434160 game random, 0x4899ee atan2 (returns a
  10-bit angle), 0x436c30/0x436e30 object-grid insert/remove (grid 0x5278f8, 128 wide, cell = coord>>23),
  0x44cef0 spawn map object (x,y,z,type,angle), 0x44eb90 delete object, 0x44b310 block predicate (kind,x,y,z),
  0x438900 block info, 0x412a70/0x412a90 player iteration, 0x462d50/0x462ef0 player target position.

## Port first

- (a) Loading and drawing the city: little rendering lives here. Port `Route_LoadCmp` 0x471970 (needed for police
  routes and roadblocks), the 0x55fab0 type cache semantics, and `Snd_LoadLevelBank` 0x472300 for the level
  bank. Defer `Train_CreateFromMap` 0x46b240 until the train list at 0x77d368 is understood.
- (b) Player on foot / in a car: `Player_InitAll` 0x463290, `Player_ApplyInput` 0x463ec0, `Player_EnterCar`
  0x463a10, `Player_ExitCar` 0x463c40, `Player_SetOnFoot` 0x463dd0, `Player_SetViewport` 0x464500,
  `Player_UpdateAll` 0x464880, `Player_Wasted` 0x463100 and `Player_Busted` 0x464820. Then `PowerUp_Collect`
  0x46a560, and for mission start `MisCar_Create` 0x475840, `MisCar_CreatePlayerPed` 0x475a20 and
  `MisCar_PutPlayerIn` 0x475c40.
- (c) Frame loop: `Player_UpdateAll`, `Train_UpdateAll`, `Snd_MusicService` 0x4733a0, and the
  mission updates `Crane_Update`/`Mission_UpdateTimedBombs` (called from 0x479e00). Police (`Cop_Update`) and
  the pathfinder (`Path_Find`) can come later; they are self-contained.

## Open questions

- Player kind 1 (train) versus the camera kinds 3/4. Several player fields are still unnamed (+0xac block, +0x184,
  +0x1ac..0x1b0 timers, the 10 ints at +0xfc).
- Exact bit meanings of the control word for axis 2 (bits 13-14) and bit 22.
- Train kind 2 (single unit) and the predicate kinds of `0x44b310` (1 rail, 3 spawn test, 5 slope, 7 switch, 9 platform).
- Path slot space: dynamic paths are indexed by controller id (up to 128), while CMP routes sit at 50+. The
  buffer size and how collisions are avoided are not verified.
- Trigger type numbers (3, 8..0x21) versus the MISSION.INI keywords (TRIGGER, GUN_TRIG, CARDESTROY_TRIG,
  MOVING_TRIG...). They need a pass over 0x445800.
- What the thunked builders 0x425520/0x4258d0/0x425780 create (they look like a 4-direction object with sprites
  0x12/0x2b-0x2e, possibly TELEPHONE/BOMBSHOP/SPRAY).
