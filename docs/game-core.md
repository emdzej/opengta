# Game core: level start, frame loop, entity tables, MISSION.INI

How the original starts a level and runs its frames, and how the port mirrors it (`src/game/`).
Addresses are virtual addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `game.c/h` | `Game_SetOptions` 0x4146d0 (the 43 launch switches), `Game_Init` 0x430a20, `Game_Frame` 0x430b20, `Game_Update` 0x430c00, `Game_Render` 0x430d40, `Game_Present` 0x430da0, `Game_HandleKey` 0x430dc0, the requests 0x430980-0x430a00, `Game_Run` 0x4148a0 as begin / step / end, the map name and style requests (0x438190 / 0x4381c0, 0x47ced0 / 0x47cee0), `Error_Fatal` |
| `event.c/h` | frame counter and the delayed-event queue 0x401000-0x401280 |
| `fileio.c/h` | the shared sequential input file `TextFile_*` 0x42e270-0x42e540 |
| `mission_ini.c`, `mission.h` | `Mission_ReadIni` 0x44ab90 and the section tokeniser 0x44ace0-0x44b130 |
| `mission_load.c` | `Mission_Load` 0x445800, the object-line creators `Mission_Spawn*` 0x43d2c0-0x43e1b0 and `MisCar_*` 0x475840-0x475fd0, `Mission_ClearBlock` 0x4770a0 |
| `car.c/h`, `ped.c/h`, `obj.c/h`, `player.c/h` | the four entity records, their tables and level-start initialisation |
| `coll.c/h` | the collision grid 0x434180-0x437000 (init, insert / remove, hitbox, block and car queries), `Map_GetGroundZ` 0x4544e0 |
| `route.c/h` | `Route_LoadCmp` 0x471970, the service locations, `Area_SetNavData` 0x44b590 |
| `gmath.c/h` | `Math_Random` 0x434160 / `Math_Rand` 0x489abe and its reset 0x434170, MSVC `rand` 0x49cb27 |
| `stubs.c/h` | the DirectPlay network layer the game core calls (`Net_ResetSync`, `Net_SyncFrameInputs`, `Net_BuildChatPrefix`...), as the original behaves without a network game; the level-start call counters of the tests |
| `tune.c/h` | the car tuning file `Tune_LoadFile` 0x412d20 / `Tune_SetCarParam` 0x412e90 (`config.ini`, absent in the data) |
| `gfx.c/h` | the display mode list and `Gfx_SelectMode` 0x414cc0 (one 640 x 480 x 32 mode) |
| `mapq.c/h` | `Map_GetLidBelow` 0x4387b0, `Map_TestBlockAttr` 0x44b310 |

## The session (Game_Run 0x4148a0)

WinMain sets the switches (`Game_SetOptions` with 1,0,1,1,0,1,1,1,1,1,0,0,1,0,1,0,0,0,1,0,0,0,1,1,0,...,
1 at the 33rd, 0 for the rest), clears the map name, selects the MISSION.INI section (0 = a built-in
empty mission on `level001.cmp`) and, when the frontend picks a level, calls Game_Run:

1. frame limiter on (0x502f34), style request cleared;
2. `Mission_ReadIni`: finds `[N]` in `..\gtadata\mission.ini`, reads the name, a number, the CMP name
   (requested with priority 1) and the style (priority 2, 0 means none) and keeps the rest of the section
   (up to the next `[`, at most 0x37001 bytes) for Mission_Load;
3. `Map_Load` 0x438200 (map.c) and the parts of it that belong to other modules: object positions
   (`Obj_SetMapObjects`), routes and locations (`Route_LoadCmp`), nav zones (`Area_SetNavData`), the
   per-player debug camera stop / camera mode 0 / 320x200 viewport for non-local players, and a style
   request with priority 1 from the CMP header (the INI's 0 never overrides it);
4. `Style_LoadRequested` 0x47d390 (style.c), whose `Sprite_LoadInfo` calls `Coll_Init` 0x436ad0 and
   which then runs `Obj_LoadInfos` 0x44ed60 and `CarInfo_Setup` 0x40c100;
5. `Game_Init` (below), then `Tune_LoadFile("config.ini")`;
6. the network start (`Net_ResetSync`; on failure the result is 4), the video mode (which sets the local
   player's viewport, `Player_SetViewport` 0x464500), `Style_ConvertPalettes`, `Timer_Start` (a 70 Hz
   Miles timer, only with sound on);
7. the loop, until 0x51322c (quit) is set;
8. present, timer stop, `Game_Shutdown` (replay save, HUD fonts, sounds), network end, frees; the
   result is the quit code: 1 mission over, 2 abandoned, 3 reload (F12), 4 network failure.

The port splits it: `game_run_begin` (1-6), `game_run_step` (one iteration of 7), `game_run_end` (8).

### The tuning file (Tune_LoadFile 0x412d20)

`..\gtadata\config.ini`, optional (the game data has none: a development leftover), read whole into a
0x1000-byte buffer (longer is fatal). Each `[car <model> <parameter>] <value>` overwrites a field of the
model's car info record (`Tune_SetCarParam` 0x412e90): centre of mass x / y (bytes +0x76 / +0x77),
moment of inertia (int +0x78), turn ratio, drive and steering wheel offset (shorts +0x98..+0x9c) take
the integer; mass, gear 1, tyre adhesion x / y, handbrake and footbrake friction, front brake bias,
back end and handbrake slide value (+0x7c..+0x94, +0x9e, +0xa2) the value / 65536 as a float (the
parameter names are the exe's strings). An unknown parameter or an entry not starting with "car" is
fatal (-0x89); a model without a record is skipped. The key and the text share one block of memory
(0x501d88, 0x50 bytes, then the text 0x501dd8), and a key with blanks before its `]` is copied with
the whole rest of the text over the start of the buffer: the port keeps the layout, so such a file
parses as in the original.

### The network layer

The game core's network calls (`src/game/stubs.c`) are the DirectPlay layer of the original, which
gasm doesn't have; the frontend's screens find no connection (`src/front/front_net.c`), so a network
game never starts. Each does what the original does then: `Net_ResetSync` 0x44bd30 returns 1 (start),
`Net_unk_0044b900` and `Net_BuildChatPrefix` 0x44c1b0 (F1..F4) do nothing, `Net_SyncFrameInputs`
0x44b930 only counts the frame in its sequence byte (0x6b400c), and `Front_GetMultiTarget` 0x4269c0
answers single player (-1), which turns the network rules of the scripts off (score / kills targets,
races) and lets the police send patrol cars.

### One loop iteration

1. (timing option) start time;
2. `Game_Present`: flip the frame drawn by the previous iteration;
3. `Input_ReadControls` 0x432e00 into the local player's control word (0x5031a8[player]);
4. `Net_SyncFrameInputs` 0x44b930 (lock-step exchange in network games);
5. `Timer_WaitTicks(3)`: busy-wait for the third 70 Hz tick since the last frame, i.e. about 23.3 frames
   a second, unless F8 turned the limiter off (or there is no timer: sound off);
6. per player: viewed player = it, `Player_ApplyInput` with its control word, the key in bits 23+ (bit 6
   set) to `Game_HandleKey`, `Camera_DebugMove`;
7. `Game_Frame`, `Game_Render`;
8. sound update (in-game or frontend mode).

`game_run_step(elapsed_us)` keeps that order: it presents and reads the controls, then returns
`GAME_STEP_WAIT` (without doing the rest) until the caller's clock has advanced the 70 Hz counter by 3
ticks; the next call continues at 6. So one call is at most one iteration and the number and order of
presents is the original's.

## Level start (Game_Init 0x430a20)

In this order (switch names as in `GameOptions`):

| Step | Function |
|---|---|
| players not ready | `Player_ClearInitFlag` 0x463280 |
| sound mode, events, replay | `Audio_EnterGame`, `Event_Init` 0x401000, `Replay_Begin` |
| redraw / pause cleared, HUD | `HUD_Init` 0x483010 |
| RNG reseeded | `thunk_Math_RandomReset` 0x434170: the five words of 0x4b3824 |
| peds (switch) | `Ped_InitAll` 0x44f370 |
| cars (switch) | `Cars_Init` 0x4070a0 (includes `Traffic_InitModelTables` 0x418f80: 300 `Math_Random` calls), `Heli_Init` |
| objects (switch) | `Obj_InitFromMap` 0x44c620 (CMP objects and parked cars, then `PowerUp_InitAll`) |
| trains (switch) | `Lights_Init` |
| | `Sentinel_InitAll` 0x41abd0 (also counts the service locations), `Path_Reset` |
| trains (switch) | `Train_InitAll` |
| | `Proj_Reset`, `Fire_Init`, `Expl_Init`, `BlockAnim_Reset`, `Hunt_Init` |
| mission | `Mission_Load` 0x445800, `Mission_FreeIni` |
| | `Snd_Reset(style)`; phase 1, step mode 0, quit 0, result 0 |
| players | `Player_InitAll` 0x463290 (after the mission bound the players to their peds) |
| cameras | `Camera_InitAll(-1000)` (snap, zoomed out by the bias -204), `Camera_Update(-1000)` |

Because the RNG is reseeded first, every start of a level is identical (the test checks it). With the
shipped data the RNG is used only by `Traffic_InitModelTables` during the start (fires created by
`Obj_Create` would use it too).

## Frame (Game_Frame 0x430b20, Game_Update 0x430c00, Game_Render 0x430d40)

`Game_Frame`: with the debug keys, a debug-camera move while paused requests a redraw; the sprite draw
trees are emptied (with sprites on). Then:

- not paused (pause frames 0x513234 = 0): phase 0 (keypad +, one tick) runs `Game_Update`, the cameras,
  `Render_ComputeVisibleRect` and freezes (phase 2); phase 1 (running) runs `Game_Update` then the
  cameras and the visible rect; phase 2 (frozen, F6) only does the cameras when a redraw was requested;
- paused by a frame count: count down, `HUD_TickBigMessage`, `Style_UpdateAnims`;
- always: `Render_CopyCamera`.

`Game_Update`: the demo countdown (10000 frames; the HUD shows (10000 - frame) / 25), `Style_UpdateAnims`,
`Event_Tick` (frame counter + 1), cars + heli, peds, trains and lights (both must return 20, else fatal
-0xc1 / -0xc0) + junction override timers, objects, emergency services, explosions, block animations,
`Mission_Update`, `HUD_Update`, `Replay_TickFrame`, `Player_UpdateAll`.

`Game_Render`: (sprites on) `Render_QueueVisibleEntities` 0x437000 over the local player's view
rectangle, `Render_DrawCity` 0x4389f0 (which draws the sprite levels between its layers), `HUD_Draw`. In
the debug step mode (Alt) only every 5th frame is drawn and presented (`Game_Present`).

`Game_HandleKey` (scan codes; +0x80 = released): Esc quit prompt (or the video menu), F1-F4 chat, F5
radio, F6 freeze (single player), F7 pager, F8 frame limiter (single player; text keys
`speed-limit-on/off`), F9 zone, F10 subtitle, F11 video menu; with the debug keys: `[ ] K L` and the
cursor keys drive the debug camera (released keys stop it), C debug text, keypad * 99 of every weapon
(also with the frontend cheat 0x503198), Alt step mode, keypad + single tick, F12 reload (quit 3), Home
camera snap. The HUD sees every key first. Unknown keys return 0, every known key 1 whether it did
something or not.

## Delayed events (0x401000-0x401280)

Ten nodes {due frame, type, arg, next, prev} in a list sorted by due frame and ended by a sentinel due
at 0xffffffff. `Event_Schedule(delay, type, arg)` inserts before the first node due at or after
`frame + delay`, so same-frame events run in reverse order of scheduling; an empty pool is fatal (-0x4d).
`Event_Tick` dispatches every event due exactly now (the node returns to the pool first) and then
advances the frame counter 0x4bbb80. Types: 0 mission briefing done (0x445580), 1 game end
(`Game_RequestEnd`), 2 / 3 door closed / opened, 4 trigger reset, 5 nothing. `Event_ScheduleExit`
keeps a pending game-end event unless that one's argument is negative and the new one positive (then it
moves to the new time with the new argument).

## Random numbers

- `Math_Rand` 0x489abe: five words at 0x4b3810; s0 = (s1 + s2 + s3) & 0x7fff, then s4..s1 shift up and s0
  is returned. `Math_Random` 0x434160 also stores it at 0x523fe8. Seeds: 0x4b3824 (the same five values as
  the initial state), copied back at every level start.
- MSVC `rand` 0x49cb27 (named `Crt_rand` now): seed 0x4b8e9c = 1, never `srand`ed; sound and skid marks.

## Entity tables

All four are static arrays in the original and in the port, with structs that mirror the records
(`_Static_assert` on every named field; on 64-bit hosts the fields after a pointer move, the checks for
those only run with 32-bit pointers, like `Sprite`'s). Unknown fields that the initialisers set are
named `uNN` after their offset. The embedded sprite is `Sprite` (src/render/sprite.h, 0x5c bytes).

| Table | Address | Record | Header |
|---|---|---|---|
| cars | 0x4be248 | 400 x 0x2b0 (sprite at +0x250, car info pointer +0x2ac) | `car.h` |
| peds | 0x7284e0 | 620 x 0x100 (sprite at +0x90); 200..599 are the drivers of cars 0..399 | `ped.h` |
| objects | 0x6b40d0 | 3500 x 0x88 (sprite at +0x2c, list links +0x24 / +0x28) | `obj.h` |
| players | 0x74f148 | 4 x 0x1bc (view rect +4, viewport +0x18, camera +0x48 as in camera.h) | `player.h` |

Level start of each:

- `Cars_Init`: model -> car info record table 0x4be178 (100 models), every slot free (status -1, +0x139
  = 0), traffic model rows 0x504ce0 copied from 0x4ac108 and shuffled (swap each entry with a random one
  of its row).
- `Ped_InitAll`: every slot `Ped_Reset` (all fields but the remap), control -1, anim 0, +0x12 alternating
  0x80 / -0x80; slots 200..600 (600 included: the first special slot, with car 400) are drivers sitting
  in their car (state 7, health 100); each sprite at z 0x13f0000 with the first ped sprite and the ped
  palette; remaps from the 22-entry cycle 0x4b21b0 through 0x4b20b0.
- `Obj_InitFromMap`: the CMP object positions in order into slots 0.. (type 2 outside style 1 becomes two
  objects, 3 and 2); records with remap >= 0x80 are parked cars (every other plain car, vtype 4, gets its
  alarm, siren state 99); the rest of the table is cleared. `Obj_CreateStatic`: types 0x1a-0x1c half a
  block lower, invisible types (status 3) only get a position, status 1 starts in state 7, the animated
  (5, 9) and status-7 types go into their lists, all but types 0x10, 0x41, 0x42, 0x47-0x49 into the grid,
  and over a slope the depth key goes one layer up (type 0x57: 0x450000).
- `Player_InitAll` (after Mission_Load): temporary weapons given back, armour / speed-up off, ammo empty
  (99 each with the weapons cheat), score 0 (999999999 with the score cheat), multiplier 1 (10), HUD
  strings, 4 lives (infinite with more players or the option 0x502f35); the camera follows what the
  player controls; a player in a car switches it to physics control.

## The collision grid (0x434180-0x437000)

128 x 128 cells of 2 x 2 blocks (`coord >> 23`), each a list of 16-byte nodes {kind, id, in use, owner,
next} taken from a fixed pool per kind (one node per entity slot: peds 620, objects 3500, cars 400,
...). `Coll_Insert` puts the node at the head of its cell and records the position in the sprite (+0x20 /
+0x24); `Coll_Remove` finds it again from there (and does nothing if the cell's head node isn't in use).
Kinds 1, 3, 6, 8 and 10 count in the cell counter 0x537908, which the 3 x 3 queries test before walking a
cell. Queries fill a 149-entry result list (newest first), locked until `Coll_Unlock`.

`Coll_BuildBox` 0x434300: corners centre ± hh·(sin a, cos a) ± hw·(sin a', cos a') with a' = a + 0x100
(exact sums for 0, 0x100, 0x200, 0x300), and the ground z under each corner (`Map_GetGroundZ` at z -
half a block, minus 1).

`Map_GetGroundZ` 0x4544e0: from layer z >> 22 downward the first layer whose cached type isn't air; layers
past 4 count as 4; flat ground is the bottom face of the layer (z * 0x400000 + 0x3f0000); a slope adds the
height of its type at the position (types 1-8 two-block, 9-0x28 eight-block, 0x29-0x2c one-block slopes).

## CMP sections kept raw by map.c

- **Routes** (`Route_LoadCmp`): `{u8 n, u8 type, n x (x, y, z)}`; types 0xfe / 0xff are copied into path
  slots 50.. (0x7537d0, 0xff bytes = 85 triples each, ended by 0, 0, 0; an 85-node route ends in the next
  slot); other types are roadblock vertex sets (0x5f2b50[type], vertices pooled at 0x5ce3c8, at most 300).
  At most 100 0xfe / 0xff routes. NYC: 1 + 22 routes, 99 vertices; SANB 36, 96; MIAMI 40, 147.
- **Locations** (the 0x6c bytes after the routes): 6 kinds x 6 x (x, y, z) blocks: police stations,
  hospitals, two unused, fire stations, unused. A kind's count is its entries with x != 0. The first
  police station is the respawn block 0x74f850, the first hospital 0x505898, the first four fire stations
  0x511960.
- **Nav zones**: 35-byte records {x, y, w, h, sample, name[30]}.

## MISSION.INI

A section starts at a line `[N]`. Then, read by the fgetc tokenisers of fileio:

    name, number, file.cmp, style,

(name and CMP up to ',', at most 0x51 and 0xd characters; numbers unsigned) and the section text up to the
next `[`, read by the string tokeniser of mission_ini, where `{...}` after a blank is a comment:

    traffic police - var505efa emergency -          six numbers
    line [digit] (x, y, z) TYPE p1 p2 [more]         object lines, until a negative number
    label OPCODE a b c d e                           command lines, until a negative number

- Object lines: the optional digit makes the object persistent (not removed by RESET); 2 also lists it
  for the kill frenzy (0x5fe010). Coordinates are blocks, or pixels for PED, OBJECT, CRANE and
  PARKED_PIXELS. TYPE is one of the 72 names at 0x4b0d98 (CAR, PED, OBJECT, PLAYER, ... ALT_DAMAGE_TRIG);
  the line number maps to the script object (0x6561c0) that holds type, handle (the created entity or a
  parameter), parameter and coordinates (1400 records at 0x5f30e8). Some types read more values; the
  last value of a line must be a number (Ini_ReadIntChecked, fatal -0xb1 otherwise).
- PLAYER binds the next player (`Player_First` / `Player_Next`; one in single player): its ped is the
  driver slot (car + 200) of the car of line p1, standing on block (x, y, z) facing p2.
- Command lines: OPCODE is one of the 150 names at 0x4b0ec0 (the first match: XXXX is there twice); the
  label maps to the command index (0x693b30); records are 24 bytes {u16 opcode, a, b, c, d, e} at
  0x676660, 5000 of them, the unused ones opcode 999.
- After the commands the player processes start at command 0; with the first header value the traffic
  pool is primed (`MisCar_SpawnBatch`), with the second = 1 the police start.

All 17 sections of the shipped MISSION.INI load (tests/level_test.c).

### Quirks kept

- `TextFile_SkipTo` restarts the match after a mismatching character without testing it against the
  first character of the string.
- A `{` right at a token's start (no blank before it) is not a comment; `Ini_ReadOptDigit` steps over the
  first character unconditionally.
- `Mission_ClearBlock` takes pixel coordinates but most object types pass blocks, so before a parked car
  or a player it clears near the map's corner instead of its block (nothing there at level start).
- PED sub-type 2 (a driver for a car) stores p2 as its handle and gives that ped id the remap: the
  creator's result is dropped (disassembly at 0x43d68b).
- `MisCar_SpawnBatch` binds drivers from the old car count up to the new count plus the old count.
- `Ped_InitAll` treats slot 600 as a driver (of car 400).
- Object types 0x40 / 0x4d created by `Obj_Create` turn their shared object_info record animated for good.
- `Player_InitAll` and `Player_PrevWeapon` address ammo as +0x157 + weapon: weapon 0 means the top byte
  of the wanted level.
- `Coll_GetFiresInBlock` returns the first entry it added (whose next is NULL), not the list head.

### Port deviations

- `Error_Fatal` reports code, module line and argument (no message text) and exits.
- Where the original indexes outside a table with bad data (negative grid cells, line numbers past 0xffff,
  more than 5000 commands or 1400 objects, -1 handles), the port stops with a fatal error or reads a
  record of -1s instead. Valid data behaves the same.
- The shared input file is read whole through the file layer at open.
- `Player_First` / `Player_Next` are single player (the network slots aren't ported).
- `Map_Load` is map.c's; the parts of it that belong to other modules run right after it in
  `game_map_load`. `Coll_Init` and `Obj_LoadInfos` (called from inside `Style_Load`) run right after
  `style_load` in `style_load_requested`.
- Car creation is the real `Car_Init` 0x4067c0 ([Cars](/cars)); `Traffic_PrimeCarPool` is ported too
  ([Traffic](/traffic)): mission 1 primes 100 traffic slots.
