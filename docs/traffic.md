# Traffic, traffic lights and path finding

How the original fills the streets with cars and drives them, how its traffic lights work, and how
its AI controllers find routes; and where the port lives. Addresses are virtual addresses in
`gta.exe`.

| Port | Original |
|---|---|
| `src/game/traffic.c/h` | the traffic AI 0x415a50-0x418390 (`Car_DummyDrive`, lanes, junctions, the throttle), the generator 0x418390-0x418f80, and the car-module pieces the AI uses: the probes 0x407160-0x4072a0, `Car_SetHorn` 0x406e90, `Car_StartLaneChange` 0x408330, `Car_DummyFollowRoad` 0x408440 |
| `src/game/lights.c/h` | the traffic lights (the lights half of 0x47dcf0-0x480e10; `Rail_*` is the trains' `rail.c`) and the junction override table of the emergency services (0x419400, 0x41e140) |
| `src/game/path.c/h` | the path finder 0x46e9e0-0x471940 and `Map_FindNearestRoad` 0x41a490 |
| `src/game/ai.c/h` | the AI controller records 0x507ea0 (`Sentinel_Get` and the resets) |
| `src/game/dummy.c/h` | the mission DUMMY controllers 0x473440-0x473b8f ([missions.md](missions.md)) |

Test: `tests/traffic_test.c` (mission 1; frames in `out/traffic/`).

## The traffic pool

Traffic cars are not created on the fly. `Mission_Load` primes a pool with the first number of the
MISSION.INI header (`MisCar_SpawnBatch` 0x475f10 -> `Traffic_PrimeCarPool` 0x418f00): with sound
effects suspended it creates that many cars at block (1, 1, 1), each with its driver, and deletes them
at once. A deleted car keeps its model, sprite and car info, and its +0x139 stays 1, so the normal
spawners (which want +0x139 = 0) never take the slot again: it belongs to the traffic. The models come
from the city's row of `Traffic_InitModelTables` 0x418f80: three rows of 100 model numbers read from the
exe (0x4ac108), each shuffled with the game RNG at `Cars_Init`, taken in turn by a cycle (0x504f40)
that wraps after 99. Mission 1 primes 100 cars.

## The generator (Traffic_SpawnAroundView 0x4183d0)

`Cars_UpdateAll` counts, per player, the dummies (control 0) within 0x140 pixels of its view, and at
the end of the frame calls the generator for each player with that count. Nothing happens when the
count reaches the quota of the player's wanted level: 7 cars, 6 at level 1, 4 at level 2, 3 at level
3, 2 at level 4. Otherwise `Traffic_FindRecyclable` 0x417b70 looks for a pool car from where its last
search stopped (0x504f38; past car 399 it returns none and starts again at 0 next time): not an
emergency model nor 0xb / 0xc, and either free and left alone (no script, not burning, not followed by
a camera; a free car with damage 100 and control 0 also qualifies) or a parked, unscripted dummy with
a driver that no view shows; its driver must not be dead (state 0xc) nor seen lately
(`Ped_IsVisibleRecent`). A car marked at +0x140 rejoins the pool 10 frames after the frame stored there.

The car then goes to an edge of the view. Each call advances the player's edge cycle (0x504f3c, five
steps): 0 tries the top edge first, then bottom, left, right; 1 bottom first; 2 left; 3 right; 4 the
player's own direction. Each edge scanner walks the row or column one block outside the view rectangle
(from 0x40 pixels in from the corner, block by block, until 0x40 pixels before the other corner) and
takes the first road block of the ground layer whose direction bits contain the lane direction and
none across it (top: lanes going down, +y; bottom: up; left: right; right: left); sprites taller than
0x40 pixels also need road on all eight neighbours (`Traffic_IsRoad3x3` 0x418ab0). The player's
direction is the camera target's heading (`Traffic_AngleToDir` 0x418390). With police due at the
wanted level (`Police_CopsForWanted`), the edge in the player's direction is skipped at first; it is
tried as a last resort at low wanted levels (the cut-offs differ per step of the cycle).

`Traffic_RespawnCar` 0x417d90 refuses a block whose car-sized square (half the larger sprite side
around the block centre) overlaps any view, or where an object stands (`Coll_AnyObjectAt` on the box
grown by 4). Otherwise it reclaims the driver (the slot's own ped for a free car; riders of bikes and
convertibles leave the grid; a parked car leaves the grid and its ped is removed), puts the car at the
block centre on the ground facing the lane (direction 1 heading 0x200, 2 heading 0, 4 0x300, 8 0x100),
repairs it, resets control, owner status and the AI fields, gives bikes and convertibles a visible
driver (`Ped_CreateCarDriver`) and cars a new remap, rebuilds box and body, and grounds it. Finally it
walks up to ten blocks back along the road and remembers the last traffic-light junction there (+0xa8)
so the car doesn't stop for a light it has already passed.

## Driving (control 0)

Cars only move while active: in a view (with its margin), or held active by a mission, an AI, a drive
mode. Off-screen traffic stands still until it is recycled. Each frame `Cars_UpdateAll` runs, for an
active dummy, `Car_DummyFollowRoad`, then `Car_DummyDrive` and `Car_Update`, whose kinematic move
(`Car_DummyMove`, [cars.md](cars.md)) calls `Car_DummyKeepLane`.

`Car_DummyFollowRoad` 0x408440 looks at the point half the length / 2 ahead (+ 8 for buses, + the
speed):

- a step it can't take (more than 0x14 pixels above the slope's continuation), a building (it slows),
  or air for a control-8 car below speed 0x14: no decision this frame;
- off the plain road (the car's own block isn't type 2) the lane mode becomes 3 with a road on the
  left, else 4 with one on the right (lane step 8); a two-way block (the opposite direction bit) also
  gives mode 3, a block that isn't road 4;
- when the block ahead doesn't continue the car's direction, the car turns (+0x96 = -0x20 / +0x20 a
  frame) toward the one other direction it offers. A car whose id has its direction bit set also takes
  a side road offered by the block ahead when the block beside it is not a road and the corner beyond
  it is (the original tests the car's id: `id & dirs`, a cheap per-car choice). The turn is dropped
  when the car's direction continues on that side. A turning car slows to its cruise speed (+0x104);
- lane changes (owner status 2 / 3, below) slow the car.

`Car_DummyDrive` 0x416610 (also for physics cars, with a driver):

- AI drivers (control 2 / 9 / 10, or +0xc0 set) only get the give-way rule: a lid tile 'Q', 'R' or
  'M' under the point ahead or under the car, while on a screen, makes them wait (+0xdc = 2).
- Dummies (not while the siren is on, not a fallen bike) probe a box half a length ahead (at most
  0x40): another car (`Car_CheckAhead`, the pending-box query; an inactive car there is removed if no
  view shows it) or a live ped, a heavy object, kinds 8 / 10 (`Car_IsPathClear`) stop the
  acceleration and cost a speed step (a blocked tram rings). Then a second probe +0x7c blocks further
  (it grows 1..3 while the way is free and shrinks while it's blocked): blocked, the car brakes, and
  at speed it sometimes hoots (+0x136 = 0x28, brake) or a ped screams.
- The stop point (the speed ahead, + 0x20 for cars longer than 0x40) on a junction block
  (`Map_TestBlockAttr` 2) of a junction other than +0xa8 is checked once: red or amber
  (`Lights_Query` 0x34 = 0 / 1) makes the car stop (+0xdc = 1). A give-way lid ahead of a car standing
  on one makes it give way (+0xdc = 2). Both only while the car is on a screen. A stopping car brakes
  at most at speed 8 (the input is minus the car info braking).
- A free car accelerates (the car info acceleration +0x0e) up to its top speed plus the bonus +0x8e,
  or the cruise speed while turning; at the top speed it hoots and brakes.
- A waiting car below speed 4 stops. +0xdc = 1 waits in `Car_DummyCheckLights` 0x417980 for green
  (state 3), then sets off at speed 1 and remembers the junction. +0xdc = 2 checks the crossing lanes
  for moving cars (`Car_DummyTurnLeft` 0x4173d0 when its id picks the side road and the block beside
  continues there, else `Car_DummyTurnRight` 0x417640) and goes on when they are free.
- Any input left runs `Car_DummyThrottle` 0x4170e0: the car info speeds, then, with the driver seated
  (anim 0 / 0x7f / 0x80), the engine on, not braking, kinematic and below 99 damage, every 4th to 8th
  frame (+0x102) the input accumulates in +0x1e and the speed changes by +0x1e / 16, stopping at 0 when
  it changes sign; a dummy can't exceed its cruise + 0xf over the bonus. `Car_UpdateWheelspin` 0x415b10
  makes a car pulling away against the brake spin its wheels (smoke, tyre marks, the tail wagging) and
  bikes wheelie and fall. Badly damaged cars (above 0x62) roll to a stop.

Lanes: `Car_DummyKeepLane` 0x415fe0 keeps the car's position across its road (the pixel within the
block, x for vertical roads, y for horizontal) within the lane offset +0x10e (0x1f normally) plus or
minus the step +0xc4; outside it the lane mode becomes 1 or 2 (which side by direction), and while the
car moves `Car_LaneCheckLeft` / `Right` 0x4162f0 / 0x416480 shift the front wheel point by the step
toward a point 0x34 pixels to that side, if that block allows the direction (or the car is
overtaking), braking for a car there. The player's horn (`Car_SetHorn` 0x406e90) makes the first car
within five blocks ahead start a lane change (`Car_StartLaneChange` 0x408330: lane 0x37 or 7, owner
status 2, which `Cars_UpdateAll` speeds up); it returns to lane 0x1f once there (status 3, slowing to a
stop). Emergency models switch their siren instead of hooting.

### Quirks kept

- `Car_DummyCheckLights` checks for crossing traffic by reading the type cache at block (0, 0) of the
  car's layer and probing positions relative to the map's corner rather than the car; the result
  doesn't change what follows.
- The side-road choice tests the car's id against its direction bits.
- `Car_IsBlockedByCar` walks the hit list after unlocking it.
- `Car_DummyFollowRoad` computes a ground height it never uses.

## Traffic lights

`Lights_Init` 0x47dcf0 runs at level start (with the trains / lights switch). It scans every block
column, row by row, for a block whose type map extension says "junction" (lights field 1). The first such
block that isn't on a railway and isn't known yet seeds a junction: `Lights_FloodJunction` 0x47e7c0
visits the neighbours in the order +x, +y, -x, -y; every new junction block starts an arm, which
`Lights_TraceArm` 0x47f240 follows in a straight line until it leaves the junction, recording each block
in the cell table (with the light the arm will get), the first road block, where the road's direction
bits change and the first block past the road. The list of arm directions is matched against 11
templates read from the exe (0x4b3520, 21 bytes: kind, 10 arm directions, 10 orientations), which give
the junction a kind and each arm an orientation (the side of the junction its light faces); some
recorded blocks move back by one depending on the kind. Kinds 10 and 11 are level crossings: the other
half is looked for up to 9 blocks on and flooded too. Each arm gets one light at its stop block: the
light sprite over the road near the block's edge, turned to face its traffic (orientation 0 -> 0x200,
1 -> 0, 2 -> 0x300, 3 -> 0x100, the table 0x4b3608), and a pole sprite; both go into the collision grid
(kinds 7 and 0xe), which is how the renderer finds them. At most 88 lights; the lights of a junction
share its number. NYC has 24 junctions with 74 lights.

The light record (0x776ae8, 0x124 bytes): +0 mode (0 automatic, 3 forced), +1 orientation, +2 phase,
+3 kind, +4 the length of phases 0 and 3 (12 ticks), +5..+7 the stop block, +8 the junction, +9 the
tick in the phase, +0xc the countdown of a forced state, +0x10 the light sprite, +0x6c the pole.

`Lights_Update` 0x47e420 runs every frame and does its work every 8th: the global cycle and every light
move on. The phase table (0x4b3514, from the exe) has six (colour, ticks) entries: green for 12 ticks,
amber 5, red 5, red 12, red 5, flashing amber 5. Lights of orientations 0 / 1 start at phase 0 and 2 / 3
at phase 3, so crossing streets alternate: over a cycle green shows 96 frames, amber 40, red 176 and
the flashing amber 40. A light put back to automatic takes the global cycle's phase and tick and stays in
step. A forced light (mode 3, `Lights_Command` 0x39: red, amber, ... by the argument) holds for 0x32
ticks (400 frames), then its junction returns to automatic. A light left in mode 1 or 2 makes the
update return 0x16, which `Game_Update` treats as fatal. The sprite frames (`Lights_UpdateSprite`
0x480e10): base + 0 red, + 1 green, + 2 amber, + 3 unlit (the flashing alternates 2 and 3 every 8
frames), + 4 the pole.

`Lights_Query` 0x47df00 answers for the junction block at (x, y) (the first cell of that column):
0x34 the colour (0 red, 1 amber, 2 flashing amber, 3 green; peds and cars stop on 0 and 1, waiting cars
go on 3), 0x3a the junction number, 0x3b 1 when every light of the junction is red, 0x32 / 0x33 / 0x35
mode, orientation, kind, 0x3c / 0x3d the rail tables. A block that isn't part of a junction gives 0x17
(0x3a is fatal there, -0xa9). `Lights_Command` 0x47e2a0: 0x32 the mode of the selected light, 0x36 of
the junction, 0x38 adds to it, 0x39 forces it.

The emergency services hold junctions through the override table 0x505f00 (88 records of 0x5c: +0 id,
+2 the traffic-light object, +4 the controller holding it (0xff free), +6 a timer, +8 the saved mode,
+0xa / +0xb the object's block, +0xc its saved sprite angle). `Junction_InitOverrides` 0x419400 (from
`Sentinel_InitAll`) binds each traffic-light object (type 0x10) to its junction's record, keeping the
object's angle and setting it to 0; `Junction_UpdateOverrideTimers` 0x41e140 counts the timers down
every frame and frees a record whose timer runs out. `Sentinel_OverrideLights` 0x41e1c0 (the
emergency services' side) takes a junction ahead of its car for 60 frames and forces its lights.

Quirks kept: a junction no template matches becomes kind 5 with the previous junction's orientations;
a kind 5 junction whose seed had one arm becomes kind 6 with the next template's orientations; query
0x3b always answers 1 for light 0; the automatic reset of a crossing writes the selected light's phase;
`Lights_Update`'s 0x16 result sticks across lights; `Lights_TraceArm` adds cells without a limit check
(only `Lights_AddJunction` checks).

## AI controllers

The cars of the emergency services and of the mission dummies are driven by AI controller records
("sentinels" in the original's error text): 129 records of 0x98 bytes at 0x507ea0 (`Sentinel_Get`
0x41ad60 hands out 0..0x80, `Sentinel_FindFree` 0x41ad30 the first free one of 0..49,
`Sentinel_ClearTable` 0x41adb0 resets 0..127, `Sentinel_Reset` 0x41aae0 one). A car with control 2 / 3
/ 9 / 10 names its record at +0xd8. Known fields: +2 kind (1 ambulance, 2 police, 6 fire engine, 9
mission dummy; 0 free), +8 path slot, +0xa group, +0x1b / +0x1c state and sub state, +0x1e the car,
+0x38 the progress of a paused path search, +0x40 the car pointer, +0x48 the route node, +0x58..+0x5c
the light override, +0x5e the junction, +0x64..+0x66 the destination block, +0x6c the pursuit group.
The controllers drive with `Sentinel_DriveCar` 0x41aed0 (the emergency services module), which follows
the route node by node and calls the kind's logic (`Dummy_Update` for dummies, docs/missions.md).

## Path finding (0x46e9e0-0x471940)

Emergency vehicles, cops returning to their station, fire engines and mission dummies get their routes
from one shared search. Only one controller owns it at a time (0x4b3094, -1 when idle): `Path_Find`
0x4716f0 answers -1 to any other, and the owner calls it again every frame until the result isn't 3.
Each call expands at most 4 nodes (5 on the first call of a search); the controller keeps the paused
count at +0x38, and a non-zero value there means "continue". The route goes into the path slot of the
controller's own number (0x7537d0 + n x 0xff; the CMP routes start at slot 50): up to 85 block
triples, start first, ended by 0, 0, 0; the nodes are the turning points of the route, not every
block. Results: 1 found (at once when start = goal: an empty route), 2 node pool full (the best
partial route is stored), 0 failed (no direction bits under the start or the goal, z above 5, or nothing
left to expand), 3 not finished.

The search is best-first over 16-byte nodes {x, y, z, arrival direction, cost, next, parent} from a
pool of 5000 (0x75cd58; the search gives up past 4980). Open and expanded nodes share one list sorted
by cost and headed by the goal node; a node is open while its direction byte is set. A new node must
lie at least 2 blocks (Chebyshev) from its parent (1 if it is the goal; 4 in mode 5 when the parent's
block can't be left that way); a copy of a block already earlier in the list is dropped. The result is
extracted by walking the parents back from the goal (`Path_ExtractRoute` 0x46ea10); more than 85 nodes
is fatal (-0xa7).

The road modes (2 cops home, 3 emergency vehicles and cops to a target, 4 the same with pavements, no
caller, 5 fire engines and dummies) cost a node by its Chebyshev distance to the goal, a greedy search
(`Path_ExpandNode` 0x46f0a0): from a node it runs along the direction bits and makes nodes where a turn
is possible (the right turn, or the left one when the left lane doesn't continue, after a few straight
blocks in modes 2 / 5), where short diagonal look-aheads find the next turn, at ramps (a jump node a
block further, `Path_IsUpRamp` / `Path_IsDownRamp`) and at the goal. Modes 3 and 4 also try a straight
line to a goal within 10 blocks on its layer (`Path_TryStraightLine` 0x46eec0) and scan sideways for a
lane back when heading away; mode 3 adds one block past the goal when the block along the goal's
direction is plain road. Far from the goal (8 blocks or more) a node doesn't expand back the way it
came. Modes 0 and 1 (`Path_ExpandDir` 0x4710d0) are a plain grid walk with accumulated costs (pavement
or field 1, road 6, diagonals + 3, moving away + 6); only a degenerate all-zero call uses them.

`Map_FindNearestRoad` 0x41a490 moves a block (bytes +2..+4 of its argument: x, y, z) onto a road. A
block with direction bits steps to the neighbour with the same bits; the original maps bit 1 to x - 1,
2 to x + 1, 4 to y + 1 and 8 to y - 1 (its jump table 0x41a72c, the code at 0x41a50f / 0x41a53f /
0x41a568 / 0x41a590), not the -y / +y / -x / +x the bits mean everywhere else: this settles the open
question of inventory-2, it is a quirk of the original and the port keeps it. Otherwise a square spiral
of up to 64 legs (never coordinate 0) finds the nearest block with direction bits, preferring one on
the start's column, then its row.

Quirks kept: `Path_Find` doesn't clear the controller's +0x38 after a result 1 / 2, so a controller
whose search ever paused resumes the finished search next time unless its caller zeroes it; the goal
check after an insert reads the next (stale) pool entry; the right-hand lane scan links its nodes with
the left-hand direction; the right-side look-ahead accepts any slope ahead, the left side doesn't; the
ramp jump node's cost comes from the last look-ahead block; `Path_ExpandDir`'s slope checks can never
match; the road-run look-ahead never runs (its flag is always 1; inside it x is compared with the stored
y); `Path_TryStraightLine` fails when the goal is exactly the 11th step; an 85-node route's end marker
spills into the next slot.

## Mission dummies

`Dummy_StartDrive` (docs/missions.md) gives a car a kind-9 controller and control 10; its first
`Dummy_Update` asks `Path_Find` (mode 5) for a route from the car's block (or the nearest road) to the
destination and waits for it. The test checks that a dummy gets its route; driving along it is
`Sentinel_DriveCar`'s, which the emergency services port provides.

## Port deviations

- `Car_SetHorn`'s unknown siren states are fatal as in the original (-0x4a); the model-row lookup of
  `Traffic_PrimeCarPool` gives model 0 for a style number outside 1..3 (the original reads outside
  the table).
- The AI controller's car pointer (+0x40) is not a pointer in the port: the car comes from +0x1e.
- Lights: the light table is cleared at `Lights_Init` (the original keeps the previous level's bss);
  two spare records for the group walks that read past the last light; more than 16 arms or a junction
  number past the override table is fatal (the original writes past its arrays); the debug-file
  logging (option 0x502f54) is left out, here and in the path finder (`rdiag`).
- Path finder: reads past layer 5 give 0 instead of the memory after the cache; writes past the slot
  array and a missing controller record are fatal; the over-long-route fatal fires before the 86th node
  is written.
