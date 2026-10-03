# Trains and railways

The train module of the original (0x46a9a0-0x46e81f), the railway tracer (the rail part of
0x47dcf0-0x480e10; the traffic lights in the same range are [traffic.md](traffic.md)'s), three
train-only helpers in the car module (0x407c30, 0x407f00, 0x408090) and how the port mirrors them.
Addresses are virtual addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `src/game/rail.c/h` | `Rail_Init` 0x47fa90, `Rail_TraceTrack` 0x47fbe0, `Rail_GetInfo` 0x47e600, `Rail_ToggleCrossing` 0x47e610, the getters 0x480bc0-0x480d90 |
| `src/game/train.c/h` | the train table, `Train_InitAll`, `Train_CreateFromMap`, `Train_CreateCarriage`, `Train_BuildCurveTables`, `Train_Update` and the bogie functions, stations and doors, passengers, the player interface (`Train_Command`), crashes; `Train_SpawnCarriages` 0x407c30, `Car_OnHit` 0x407f00, `Coll_ProjectileHit` 0x408090 |
| `tests/train_test.c` | the tracer on the three cities; mission 1: the trains running, stations, doors, passengers, boarding, riding, driving, a crash; determinism; frames in `out/train/` |

## The railway in the map

A block is railway when bit 7 of its type map ext byte is set (`Map_TestBlockAttr` 1). The ext
field (bits 0-2 of the same byte, `Map_TestBlockAttr` 6) marks what lies on a railway:

| ext | Meaning | Who reads it |
|---|---|---|
| 2 | level crossing (the rail cells across a road with lights) | tracer, `Lights_FindRailCrossing` (attr 8) |
| 3 | switch: the ends of a single-track section | tracer |
| 4, 5 | the two ends of a curve (`Map_TestBlockAttr` 7 returns the value) | the bogies |
| 6 | station | tracer |
| 7 | station where a train starts | tracer |

In the shipped maps there are no crossings and no switches. Liberty City has one loop of 662
blocks (elevated, on layer 2, down to 3 and 4 in places) with five stations, three of them starts;
Vice City one loop of 792 blocks, three stations, two starts; San Andreas two loops of 718 blocks
on road blocks, seven stations and six starts, whose trains are never created (below). A curve is
three blocks: the ext-4 or ext-5 block before the corner, the corner, the other mark after it.

### Rail_Init 0x47fa90 and Rail_TraceTrack 0x47fbe0

Lights_Init calls `Rail_Init` last (after its own junction scan, which fills the junction crossing
table that the tracer reads). Rows outer, columns, layers inner: a railway block that isn't in the
visited list (0x775588, 880 blocks of {x, y, z}; fatal -0x86 past them) starts a trace, which
also ends that column's layers. A trace starts in direction 3 and records each block it steps on;
it ends when the first block comes round again (0x14, a track) or when no neighbour answers (0x1e,
which stops the whole scan: no trains). A loop without a station is fatal (-0x72, "No Station
found on Track").

Directions, here and for the bogies: 1-3 +x, 4-6 -x, 7-9 -y, 10-12 +y, each as (a layer up, a
layer down, level); z = 0 is the top layer. From a block the tracer probes nine neighbours in a
fixed order (`rail_probes`): the sides at layers +1, -1 and level, then ahead at -1, +1 and level;
a later hit overrides an earlier one, so the block straight ahead wins over a slope, and a slope over
a turn. The direction stays the reverse one when nothing answers.

After each step the new block's ext field decides:

- 2: three states over the crossing cells met. The first opens a record at 0x77cf58 (block only),
  the second completes it with the light of the junction crossing at its (x, y) (0x77d2b8, filled by
  Lights_FindRailCrossing; 0 when none) and counts it (0x77d46a), the third opens and counts a
  record with the previous record's light, then the states start over.
- 3: the same three states over the switch cells (0x77d0c8, {x, y, z, switch}), where the second
  looks the block up among the unique switches (0x77d010, {x, y, z, held}), adding it if new.
- 6 / 7: a station record (0x77d180, 7 bytes {x, y, z, loops so far, direction, occupied, track
  id}); 7 also adds a train start to the summary block (at most 6, fatal -0xa4).

The summary block 0x77d368 (`Rail_GetInfo`) is a count and five arrays of 20 bytes: direction, x, y,
z, station index. The count is the starts found if the last trace returned 0x14, else 0. The
station and crossing counts are cleared by Lights_Init, not by Rail_Init.

The getters: `Rail_FindStation` 0x480bc0 returns (in AL, which the decompiler drops) whether a
station record is at (x, y); `Rail_FindSwitch` 0x480c20 the first switch cell there (0xff none);
`Rail_IsSwitchSet` / `Rail_ToggleSwitch` the held flag of a cell's switch; `Rail_GetStationFlag` /
`Rail_ToggleStation` a station's occupied flag; `Rail_NextStation` 0x480d90 the next station of the
same track (by track id; the last one wraps to the track's first); `Rail_ToggleCrossing` 0x47e610
flips the last crossing record at (x, y)'s light and the next one between phases 0 and 3.

The station, crossing and junction crossing tables and their counts live in lights.c's
`LightsState` (Lights_Query 0x3c / 0x3d reads them too); the rest is `RailState` in rail.h.

## The train record (0x7514a8, 6 x 0x5c8)

`Train` in train.h; the count is 0x75377b (the summary's count, created or not).

| Off | Field | Meaning |
|---|---|---|
| +0x00 | kind | 1 four carriages; 2 one unit (never created) |
| +0x01 | dir | 6 forward, 7 reversed |
| +0x02 | state | 2 station sequence, 3 run, 4 brake (then +9), 9 wait for a switch, 10 wait for the next station, 0xb ridden (stopped), 0xc the rider left |
| +0x03 | boarded | 1 no, 2 the player rides it |
| +0x04 | speed | 0..0x3c (0x50 ridden); the bogies move speed / 10 steps a frame |
| +0x06 | rider | ped id of the last ped that boarded (`Train_Command` 9) |
| +0x09 | next_state | after braking |
| +0x0a..+0x0f | last crossing, station, switch | blocks (x, y) the leading bogie passed |
| +0x10 | wait_switch | the switch cell it waits for |
| +0x11 / +0x12 | front / rear carriage | 3 / 0 for start directions 3 and 12, 0 / 3 for 6 and 9 |
| +0x13 / +0x14 | front / rear bogie | of those carriages |
| +0x15 / +0x16 | station / next station | |
| +0x18 / +0x1a | passengers / timer | to get off; then the doors' 100-frame countdown |
| +0x1c / +0x1d / +0x1e | ped tick / sub / toggle | the station sequence |
| +0x21 | doors_open | |
| +0x22 | max_speed | |
| +0x23 | door_frame | 8 shut, 1..7 opening, 7 open: the door objects' state |
| +0x24 | car[4] | carriages, 0x168 each |
| +0x5c4 | cross_count | single units: crossings passed |

A carriage is two bogies (+0, +0x6c), the block bytes +0xd8 (see the quirks), the body sprite
+0xdc (collision kind 10, id train * 4 + carriage, heading +0xf4), the id +0x138, the state +0x139
(0xe running, 0xd wrecked), hits +0x13a (10), +0x13b, two doors +0x13c / +0x150 ({x, y, z, open
side, object id, object}: left at heading + 0x100, right at - 0x100), +0x164 (2: the body faces
against the bogies' line, heading + 0x200) and crashed +0x165.

A bogie is a sprite (one end of the carriage, collision kind 8, id (train * 4 + carriage) * 2 +
bogie) and its place: block +0x5c, sub-step +0x5f, piece (the direction to the next block) +0x61,
the direction it came in by +0x62, the block's steps +0x63, slope state +0x64, slope height +0x65,
on a slope +0x66, curve row +0x67, inside a curve +0x68, a block count +0x69, the id +0x6a.

## The curve table (0x4b22e8)

Nine rows of 0x5a steps {u8 along, u8 across, s16 angle}: row 0 straight (along 0..63, across
31, 0x40 steps), 1 / 2 a corner (0x31 steps, from the middle of one edge to the middle of the next),
3-6 the ends of a curve (0x46 steps), 7 / 8 its middle (10 steps). The positions are initialised
data and are read from the exe; `Train_BuildCurveTables` 0x46bc30 (once per run) computes the
angles, i * 256 / 48 for the corners and i * 256 / 150 for the curves (`__ftol`: truncated; the
exe's doubles 1/48, 1/150, 256), and the mirrored rows. A curve's three blocks are 70 + 10 + 70 =
150 steps for a quarter turn.

Four words at 0x4b22e0 give the axis headings the rows are turned by: {-y, +y, -x, +x} = 0x200,
0x400, 0x300, 0x100. `Train_Reverse` swaps them (0x400, 0x200, 0x100, 0x300) for every train at
once, and nothing resets them.

## Level start

`Train_InitAll` 0x46a9a0 (with the trains option, after `Lights_Init`): the tables once, then
`Train_CreateFromMap` 0x46b240, one train per start. A start on a road block (`Map_TestBlockAttr` 3:
block types 2, 6, 7) would be a single unit, but the code jumps over everything for kind 2
(0x46b2f7): the record only gets its kind and Train_Update skips it. So San Andreas has no trains.
A train starts at its station (taking it), state 2 at step 0, speed 0, direction 6, not ridden, doors
shut, top speed 0x3c.

`Train_CreateCarriage` 0x46b510 lays the four carriages 0x7e pixels apart along the track (carriage
c from (c - 2) * 0x7e off the start block's edge), the bogies 0x14 pixels in from the carriage's
ends, in the middle of the block across, 4 pixels over the layer's bottom. The sub-step is the
bogie's position in its block in the direction of travel. Frames: train sprite 0 for the bodies (1
for the front carriage), 2 for the bogies, 4 / 3 for the front / rear end bogies. Each carriage
gets two door objects (type 0xe, `Obj_CreateAttached` kind 2, 0x11 pixels either side, state 8).
The direction switches in this function decompile with shifted case labels; the port follows the
disassembly.

## Train_Update 0x46bd60

Per four-carriage train (`Train_UpdateAll` 0x46a9d0 returns 0x14 once started, which Game_Update
checks):

1. `Train_CheckCarriageHits` 0x46d4f0: each bogie's box (0x17 x 0x3f) against other trains' bodies;
   near one (`Train_FindDoorNear` 0x46ab00) both carriages crash.
2. Outside the station sequence the doors close a frame step at a time.
3. The state machine. Station sequence (+0x1d): 5 find the platform sides (`Train_FindPlatformDoors`
   0x46e1e0: the sides with pavement a block out, both bogies on the same piece), door frame 1; 1
   open (to 7, the door sound 0x29 every frame); 3 pick 0..15 passengers and get them off
   (`Train_UnloadPassengers` 0x46e300: every 5th call per carriage, 5 times in 6, a ped with anim
   0x41 at a random open side) for at most 100 frames; 2 a 100-frame countdown during which peds may
   board, then `Train_ClearDoorAreas` 0x46e640; 4 close (sound); 0 leave: the next station, state 10.
   State 10 waits until the next station is free, frees the current one and takes it. State 3
   accelerates by 1 to the top speed, 4 brakes by 1 and then takes +9, 9 waits for the switch, 0xc
   turns round if reversed and starts the station sequence at 5.
4. Each running carriage's bogies: out of the grid; once the sub-step reaches the block's steps,
   into the next block (`Train_BogieStep` 0x46c460), the next piece (`Train_BogieNextPiece`
   0x46c5d0, the tracer's probes, which also set the corner rows), the curve marks
   (`Train_BogieCheckCurve` 0x46d210: they override the corner rows; elsewhere blocks are straight);
   the leading bogie of an unridden train toggles crossings, brakes for a station it hasn't just
   left (then the station sequence) and for a held switch (`Train_CheckSwitch` 0x46da30); then the
   height (`Train_BogieSlope` 0x46d7a0: 8-block slopes, 8 pixels a block). The position follows the
   curve row at the sub-step, turned to the axis it came in by (`Train_BogieComputePos` 0x46cdb0).
   Back into the grid, speed / 10 steps on, `Train_BogieCollide` 0x46d320. Then the body between the
   bogies (`Train_CarriageUpdateCentre` 0x46d050).
5. `Train_UpdateDoorSprites` 0x46dcc0: the open sides' door objects show the door frame.

`Train_BogieCollide` calls `Coll_ProjectileHit` 0x408090 (only the trains use it): while the train
moves, a 0x17-pixel box at the bogie hits another bogie (7), a car (its vtype; `Car_OnHit` 0x407f00
pushes it and adds 20 damage), an object (kicked) or a ped. Cars of vtype 1, 2, 8 and 9 cost the
carriage a hit (the 11th crashes it), vtype 4 halves the train's speed, another train's bogie crashes
both carriages.

`Train_Crash` 0x46d650 wrecks a carriage once: state 0xd, out of the grid, doors deleted, the train
brakes into state 0xb and counts as not ridden, a rider dies (`Player_TrainCrashKick` 0x463b70, the
ped's +0x49 = 0), every other carriage crashes, and each carriage becomes a wreck car (model 0xb,
`Train_SpawnCarriages` 0x407c30: the first free car slot, the train's speed / 10, damage 100, crash
sound 10). `Train_AnyWrecked` 0x46e7d0 backs IS_A_TRAIN_WRECKED.

## The player on a train

Ped_TryEnterCar walks the ped to a door of a train standing still (speed 0, `Train_Command` 5),
records it as the rider (9) and makes the player kind 1 on that train (`Player_BoardTrain`). The
train keeps its timetable with the player on board. The fire key on a train (Player_ApplyInput:
`Train_Command` 1, `Train_ToggleBoarded` 0x46dbe0) takes it over: it stops (at once at a station,
else after braking) in state 0xb with top speed 0x50 and no longer stops at stations. The
accelerate and brake keys are `Train_Command` 3 / 4 (`Train_DoorLeft` 0x46dae0 / `Train_DoorRight`
0x46db60): in direction 6, accelerate goes and brake stops into 0xb; brake from 0xb turns the train
round (`Train_Reverse` 0x46df90: direction 7, every bogie turned: sub-step, piece, curve and slope
mirrored), after which the keys swap meanings. Leaving (`Train_Command` 2) puts it back on the
timetable (0xc: turned round if reversed, doors open where it stands). The exit key
(Ped_PlayerExitCar) asks `Train_CheckPlatformSides` 0x46ae40 whether a side of carriage 1 has
pavement, and `Train_LoadPassengers` 0x46e450 sends the ped off that side. `Train_Command` 7
(`Train_GetBoardInfo` 0x46e6c0) gives the camera the front carriage's position.

## Sound

`Snd_GatherLoops` reads, per train, the speed (+4: no sound at 0), carriage 0's x for the pan
(+0x100) and carriage 3's position (+0x538) for the distance: loop sample 0x37 at rate
(speed + 60) * 100 (`train_fill_snd` fills `SndTrain`).

## Quirks kept

- Single-unit trains (kind 2) are never created: San Andreas' six starts give records with only the
  kind set, still counted by Train_GetCount.
- The train records are bss: fields the creation doesn't set (+9, +0x16, +0x1e, ...) keep the last
  level's values until set.
- The axis headings swapped by Train_Reverse are shared by all trains and never reset.
- The ridden train doesn't stop at stations; the leading bogie checks crossings, stations and
  switches only for an unridden train.
- Train_CreateCarriage stores the low byte of each centre coordinate >> 6 as the "block" (0 for
  whole pixels), so the first Train_CarriageUpdateCentre always re-links the body in the grid.
- Train_UnloadPassengers widens the step-off distance to 0x1b for the rest of the carriages once a
  carriage heads along x.
- Train_ClearDoorAreas uses door positions nothing ever writes (0, 0).
- Train_GetDoorOffsets takes the carriage from the whole low byte of its argument; its only caller
  (Ped_PlayerExitCar) passes 0.
- Train_FindDoorNear for a single unit takes the whole carriage id as the train index.
- Rail_NextStation reads the record after the last station before testing the count; Lights_Query
  0x3d scans the crossings with the station count.
- Train_BogieCollide ignores collisions with its own train's bogies unless +0x1f is set, which
  nothing does.

## Port deviations

- Obj_Delete of a door id of -1 (no object was free) is skipped (the original deletes the record
  before the table).
- The tables that are full in the original without a check (stations, switches past 44) stop with a
  fatal error; indices past the train or carriage tables return NULL or are folded into range; the
  curve table has a zero tail where the original would read on into the next table.
- Train_GetCarriage returns the body sprite as `const int32_t *` (the original's dword view) for the
  object module.
- The curve angles are computed in double precision (MSVC's default 53-bit x87 precision control
  assumed).

## Open questions

- The camera on a train: Ref_GetKind1PosRect 0x45fb60 (ped module) copies the board info into the
  position record (speed / 10); the port's player module doesn't have kind 1 yet.
- What +0x05, +0x08, +0x17 and +0x1f / +0x20 were meant for.
