# Inventory 3: 0x43cef0 to 0x4630e0 (300 functions)

Names are applied in the Ghidra project; the record is `tools/ghidra/names.tsv`.
Addresses are VAs in `gta.exe`. Struct offsets are byte offsets from the start of the record.
Fixed-point: world positions are 16.16 "pixels" (64 pixels per block), so `>>22` gives the block coordinate and `>>16` gives pixels.
Angles are 10-bit (0..0x3ff). 0 points along +y ("down" on screen), 0x100 is +x.

## 1. Modules (in link order)

| Span | Module | Purpose |
|---|---|---|
| 0x43cef0–0x44ab3f | `mission` | MISSION.INI script runtime: object spawners, 150-opcode interpreter, per-opcode handlers, loader. The module probably starts before 0x43cef0, since 0x43cac0/0x43cd30 are mission helpers in range 2. |
| 0x44ab40–0x44b15f | `mission_ini` | Reads the mission.ini section and its tokenizer (ints, words, `(x,y,z)`, `{comments}`). Possibly the same .c file as `mission`. |
| 0x44b160 | `movie` | Plays the intro MOVIE.SMK through SMACKW32 ordinals. |
| 0x44b310 | `map_query` | Block attribute test on the cached block-info dword. |
| 0x44b4a0–0x44b89f | `area` | CMP navigation zones ("area names"), N/S/E/W subdivision, FXT localisation. |
| 0x44b8a0–0x44c2cf | `net` | Multiplayer lock-step input sync, start handshake, chat prefix. Built on the 0x4129xx/0x412axx network layer. |
| 0x44c2d0–0x44ee4f | `obj` | Map objects ("object_pos" from CMP, "object_info" from G24). Covers create/delete, moving/thrown objects, animations and attachment. |
| 0x44ee50–0x4607af | `ped` | Pedestrians: pool, spawning, AI movement and collision, animation, weapons, entering and leaving cars, plus player respawn. Also holds the ground-height helpers that `obj` uses. |
| 0x4607b0–0x4616af | `carphys` | Float rigid-body car dynamics (x87 `fsin`/`fcos`): forces, integrate, bounce, tyre model. |
| 0x4616b0–0x4630e0 | `player` | Player record accessors: score, multiplier, lives, wanted level, weapons and ammo, kill-frenzy weapon, controlled object and camera target, name and colour. The module continues past 0x4630e0 (0x463100.. are player functions that call into `ped`). |

## 2. Key structures and globals

### Mission script (module `mission`)
- **Command table** `0x676660`: 5000 records × 24 bytes (ends 0x693b20). The loader fills unused records with opcode 999.
  - `+0` u16 opcode, indexing the name table at `0x4b0ec0` (0x00 LOCATE_STOPPED … 0x95 IS_PED_STUNNED, 150 names, dumped).
  - `+4` i32 a (usually an object line number)
  - `+8` i32 b (success label)
  - `+0xc` i32 c (fail label)
  - `+0x10` i32 d (timer/param)
  - `+0x14` i32 e (score)
  - Branch rule (`Mission_BranchSuccess/Fail`): 0 means next command, -1 ends the process, anything else is a label resolved via `0x693b30`.
- **Label map** `0x693b30`: short[65536], mapping label to command index.
- **Line→object map** `0x6561c0`: short[65536], mapping script line number to object slot.
- **Script objects** `0x5f30e8`: 1400 × 32 bytes (ends 0x5fdfe8).
  - `+0` i32 type, indexing the type-name table at `0x4b0d98` (CAR=0, PED=1, OBJECT=2, PLAYER=3 … ALT_DAMAGE_TRIG=0x47, 72 names). 99 marks a spawned PED.
  - `+4` runtime handle (car/ped/object/trigger id)
  - `+8` param 2
  - `+0x10/0x14/0x18` x, y, z
  - `+0x1c` u8 persistent flag
- **Processes**: 32 script "threads", indexed by `0x6b3b70`. Indices below the player count belong to the players; spawned processes chain to an owner through `0x676348`. The interpreter resolves the owner into `0x676608` ("current player").
  - Per-process arrays:
    - `0x6761c0` short active
    - `0x676620` short PC
    - `0x676280` short wait counter
    - `0x6560b8` short step state (1 = first entry; handlers count timers from there)
    - `0x6b3de8` short linked handle
    - `0x676200` i32 kind
    - `0x6762c8` i32 trigger that started it
    - `0x6b3b30` short result
  - The current PC is in `0x655e58`. `0x5f30e0` holds the current player's ped id (copied from `0x656180[player]`).
- **Parse scratch**: `0x676610/14/18` x,y,z; `0x5fe004` p1; `0x6765f0` p2.
- Counters: `0x676388` is the MISSION_COUNTER object, `0x693b28` the SECRET_MISSION_COUNTER object, `0x6b3b78` MISSION_TOTAL, `0x6b3b84` TARGET_SCORE.
- Cleanup lists, flushed by RESET: cars `0x6b3b90`/`0x6b3b88`, peds `0x676390`/`0x6b3b80`, objects `0x655e60`/`0x656178`. KF list: `0x5fe010`/`0x676604`.
- Result code `0x513230` drives the end text: 1 success, 2 failed, 3 dead, 4 arrest, 5 timeout, 6 time over (MP), 8 score, 10 cannon, 11 demo.

### Ped pool (module `ped`)
- `0x7284e0`: 620 × 0x100 bytes (`Ped_Get` 0x44f500).
  - Slots 0..199 are ambient/mission peds.
  - Slots 200..599 are the drivers of cars 0..399 (ped id = car id + 200).
  - Slots 600..619 are special peds (`Ped_CreateSpecial`).
- Fields:
  - `+0` id
  - `+6` speed
  - `+8` u8 move speed (4/6)
  - `+0x10` control type
  - `+0x16` graphic type (0 civilian, 1 cop; 0xbd sprite frames per type)
  - `+0x18` anim state; 0 means the slot is free, 0x88 standing, 1..0x10 walking frames
  - `+0x1c` u8 player-controlled
  - `+0x2c/0x30` target point
  - `+0x38/0x3c` walk target
  - `+0x42` short mode
  - `+0x46` u8 firing
  - `+0x49` i8 health (100)
  - `+0x4c` car id (-1 when on foot)
  - `+0x50/+0x52` attach kind/id (1 car, 2 object, 3 ped)
  - `+0x58` carried object
  - `+0x5e` remap
  - `+0x60` weapon (1..4; from the ammo code, probably 1 pistol, 2 machine gun, 3 rocket, 4 flamethrower)
  - `+0x6c` objective (0x19 wander, 0x25 player, 0x15/0x16 group leader/follower, 0x34/0x35 go to car door…)
  - `+0x70` state (1, 2 walk, 4 go-to, 7 in car, 9, 10, 0x0c/0x17 dead/dying, 0x18)
  - `+0x78/+0x7c` sub-modes
  - `+0x86` target ped
  - `+0x89/+0x8a` group index/slot
  - `+0x90` sprite/position block: x, y, z, z2 at +0x90/94/98/9c; angle at +0xa8
- Groups: `0x728498`, 0x1c bytes each (leader short, count byte, members short[]). Active ped count is `0x74f104`; peds are spawned while it is below 200.

### Car pool (range 1, used heavily here)
- `0x4be248`, stride 0x2b0 (`Car_Get 0x408200`).
- Fields seen:
  - `+2` driver ped id
  - `+0x1c` speed
  - `+0x22` car type, mapped through `0x4be178` to the car-info pointer table at `*0x501574`. Car info has door count at +0xac and door offsets at +0xae/+0xb0 and +0xb6/+0xb8.
  - `+0x144` float-physics flag
  - `+0x18c..0x1d4` rigid body (floats). The body starts at +0x190 with x, y, angle (+0x198), velocities, accumulators and mass/inertia (+0x1b0/+0x1b4).
  - `+0x250/254/258` x, y, z (16.16)
  - `+0x268` angle
  - `+0x2ac` handling-params pointer
- Car info physics fields at +0x76..+0x9c: mass, grip, wheelbase.

### Objects (module `obj`)
- `0x6b40d0`: 3500 × 0x88 bytes (`Obj_Get`).
- Fields:
  - `+0` id
  - `+2` speed
  - `+6` heading
  - `+0xa` type (object_info index)
  - `+0xc` state/frame
  - `+0xe` frame timer
  - `+0x14` owner id
  - `+0x18` attach kind (0 obj, 1/5 car, 2 ?, 4 ped)
  - `+0x1a/+0x1c` offset
  - `+0x20` u8 in-anim-list
  - `+0x24` next ptr, `+0x28` prev ptr
  - `+0x2c` sprite/position block (x, y, z, z2, angle at +0x44)
- Lists:
  - `0x6b40bc` moving/thrown objects
  - `0x6b40c0` status-7 objects
  - `0x6b40c4` attached objects
  - `0x728430` animated objects (status 5/9)
- `0x6b40c8` counts smashable objects (limit 200).

### Players (module `player`)
- Base `0x74f148`, stride 0x1bc (local player `0x74f83c`, viewed player `0x74f844`).
- Fields:
  - `+2` ped id
  - `+4` view rect i32[4] (xmin, xmax, ymin, ymax in pixels; `Player_GetViewRect`)
  - `+0xbc/+0xc0` controlled ref (kind, id)
  - `+0xd0/+0xd4` camera-target ref
  - `+0xd8..+0xe0` fixed camera point
  - `+0xe4` name
  - `+0xf4` score
  - `+0xf8` multiplier
  - `+0xfc` short[5] kill counters, `+0x110` short[5] totals
  - `+0x150` wanted points (≤2000)
  - `+0x154` wanted level
  - `+0x158..0x15b` ammo for weapons 1..4 (≤99; 100 = unlimited frenzy weapon)
  - `+0x15c/+0x15d` 5-shot sub-counters for weapons 2 and 4
  - `+0x160` current weapon
  - `+0x164..` saved weapon during a frenzy
  - `+0x170/+0x174` bonus chain
  - `+0x197` lives (≤99)
  - `+0x1ac` frenzy timer
  - `+0x1b4` colour
- Ref kinds: 0 car, 1 table-7 entity via 0x46a9f0 (train?), 2 ped, 3/4 fixed point, 5 via 0x40dc80.

### Shared tables
- `0x511e28`: sine table, int 16.16, 1024 + 256 entries. Cosine is `sin[a+0x100]`; code also reads `0x512228` (= cos(0)), `0x512628` and `0x512a28` directly.
- `0x55fab0`: byte cell map `[z 0..5][y 256][x 256]`. It caches the low byte of each block's `type_map`:
  - bits 0–3 road direction (1 N, 2 S, 4 W, 8 E)
  - bits 4–6 block type (0 air, 1 water, 2 road, 3 pavement, 4 field, 5 building)
  - bit 7 flat
- Full block attributes come from `Map_GetTypeMap 0x438900`/`Map_GetTypeAt 0x4388a0`, as type_map | ext<<16. Slope is bits 8–13.

## 3. Data formats verified here

- **MISSION.INI** (checked against game/GTADATA):
  - `[N]` header, then `name, ?, file.cmp, style,`. The second number goes to the stripped `Dbg_Nop`.
  - A header line of 6 ints follows (100 1 1 1 1 0: p0 → 0x475f10, p1 == 1 → 0x475f60, p3 → 0x478800, p4 → 0x478810).
  - Object lines: `line [1] (x,y,z) TYPE p1 p2`. The optional digit makes the object persistent; without it the object goes on a RESET cleanup list. Coordinates are in blocks, or in pixels for the `*_PIXELS`/CRANE types.
  - A negative number ends the object list.
  - Command lines: `label OPCODE a b c d e`, ended by a negative number.
  - `{…}` is a comment.
- **CMP tail** (verified on NYC/SANB/MIAMI), after the blocks: object_pos, route, location data (108 bytes), nav data.
  - object_pos records are 14 bytes: x, y, z (u16, pixels), type u8, remap u8, rotation u16, pitch u16, roll u16. remap ≥ 0x80 means a parked car with car type `type`.
  - Location data is 36 entries × (x, y, z) bytes. Entries 0..5 are police stations and 6..11 hospitals (used by `Player_RespawnAtStation`: mode 0 = police, mode 1 = hospital, i.e. busted vs. died). The counter in 0x50caa4 is presumably 6.
  - Nav records are 35 bytes: x, y, w, h, sample, name[30].
- **G24 object_info** (from code; not yet cross-checked with a file parser): records of 20 + 2·num_into bytes.
  - width, height, depth (u32; ints below 0x10000 are shifted to 16.16)
  - spr_num u16 (rebased by `0x774f0c`)
  - weight u16 (+0xe)
  - aux u16 (+0x10)
  - status u8 (+0x12)
  - num_into u8 (+0x13)
  - into[] u16 (+0x14), the objects spawned when smashed
  - Status values used: 0 normal, 1, 3 invisible (fixed position, no sprite init), 5/9 animated, 6 car-attached, 7, 8.

## 4. Call-graph links to other ranges
- **Range 2**:
  - `0x430a20` (level start) calls `Mission_Load`, `Obj_InitFromMap` and `Ped_InitAll`.
  - `0x430c00` (world step) calls `Mission_Update`, `Ped_UpdateAll` and `Obj_UpdateAll`.
  - `0x437230` (WinMain) calls `Movie_PlayIntro`, `Area_LoadDirPrefixes` and `Mission_ReadIni`.
  - `0x4148a0` calls `Net_SyncFrameInputs`.
  - `0x438200` (CMP loader) calls `Obj_SetMapObjects`, `Area_SetNavData` and `Area_LocalizeNames`.
  - `0x422900` is the fatal-error reporter, called everywhere.
- **Range 4+**:
  - The style loader `0x47cf10` calls `Obj_LoadInfos`.
  - Triggers/doors are created by `0x4744a0`/`0x4740f0` (kind codes as in `Mission_Load`).
  - Message/FXT: `0x47d9a0` (FXT lookup), `0x481d40`/`0x481a40` (screen message), `0x481540` (floating score), `0x4819b0` (arrow).
  - Sound at position: `0x4047f0`/`0x404840`.
  - Sprites: `0x47c9f0` init, `0x47c960` frame, `0x47c9c0` remap.
  - Spatial grid: `0x436c30` insert, `0x436e30` remove, `0x434300` box query, `0x436b20` end query.
  - `0x434160` rand(0..0x7fff); `0x4899ee` atan2 → 10-bit angle.
  - Player iteration: `0x412a70` first / `0x412a90` next.
- `CarPhys_Step` is called from the car update `0x40a640`. `Ped_EjectDriver`/`Ped_CreateCarDriver` are called from car code at 0x40xxxx–0x41xxxx.

## 5. Porting priorities
- **(a) Loading and drawing the city**:
  1. `Obj_LoadInfos` 0x44ed60 and `Obj_SetMapObjects` 0x44ee20
  2. `Obj_InitFromMap` 0x44c620 and `Obj_CreateStatic` 0x44c870
  3. `Obj_UpdateSprite` 0x44c3a0
  4. `Area_*` 0x44b4a0–0x44b7b0 (area names, HUD)
  5. `Map_TestBlockAttr` 0x44b310
  6. `Mission_ReadIni` 0x44ab90 plus the tokenizer (picks the CMP/style).
- **(b) Player on foot / in a car**:
  1. `Ped_UpdateAll` 0x45cd50, `Ped_Process` 0x45a3b0, `Ped_ComputeStep` 0x456fb0
  2. `Map_GetGroundZ` 0x4544e0 (+ `Map_SlopeDelta`)
  3. `Ped_IsDirClear` 0x455dc0
  4. `Ped_Animate` 0x44fa70 and `Ped_UpdateSprite` 0x44f100
  5. `Ped_SetTurnInput` 0x45f670
  6. `Ped_EnterExitKey` 0x45f5e0 → `Ped_TryEnterCar` 0x45def0 / `Ped_FinishEnterCar` 0x45ddc0 / `Ped_PlayerExitCar` 0x45d3d0
  7. `Ped_FireWeapon` 0x4532f0
  8. `CarPhys_*` 0x4607b0–0x4615e0, in particular `CarPhys_Step` 0x460be0
  9. Player accessors 0x4616b0+ (controlled ref, view rect, camera target)
- **(c) Frame loop**: `Mission_Update` 0x446fe0 → `Mission_StepProcess` 0x4471a0, `Obj_UpdateAll` 0x44d790, `Ped_UpdateAll` 0x45cd50, `Net_SyncFrameInputs` 0x44b930 (MP only). These are the in-range pieces of `0x430c00`.

## 6. Open questions
- Exact semantics of ref kind 1 and kind 5 (0x46a9f0 table 7, 0x40dc80). Kind 1 is possibly trains.
- Mission header ints 0, 1, 3, 4, and the second value on the `[N]` name line.
- Weapon 3 vs 4 identity (sound ids 0x24/0x23; projectile spawners 0x489150/0x488fd0).
- Many ped/obj state numbers are only partially decoded (anim states 0x11–0x88, objectives). The 0x44fa70 and 0x45a3b0 switch bodies still need a dedicated pass.
- `Net_unk_0044b900/910/920` wrap 0x412a00/0x412980/0x412960 (network layer, range 2).
- The opcode handlers were named from the interpreter switch (case to handler); their bodies were only skimmed. Inline opcodes (e.g. 0x04 SURVIVE, 0x0a END, 0x0c EXPLODE, 0x0e THROW, 0x81 RESET…) live inside `Mission_StepProcess`.
