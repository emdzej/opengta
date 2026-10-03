# Cars

The car module of the original (0x405790-0x40c0c0), the car info records of the style (0x40c100),
the float rigid-body model (`carphys`, 0x4607b0-0x4616af), the collision module (0x40e350-0x412310)
and the grid's box queries (0x4348c0-0x435a90), and how the port mirrors them. Addresses are virtual
addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `src/game/car.c/h` | the car table and record, `Cars_Init`, `Car_Init`, the spawners, `Cars_UpdateAll`, `Car_Update`, player controls, the kinematic (dummy) move, wrecks, skid marks, doors, sirens, damage, accessors, `Car_GetCamTarget`, the sound view |
| `src/game/carphys.c/h` | the rigid body (`Phys_*`) and `CarPhys_*`, the x87 rules |
| `src/game/carcoll.c/h` | shape against the map (walls, buildings, slopes), the grid hit list, the car responses (`Car_CollideMap`, `Car_CollideObjects`, `Car_CollideCar`, `Car_UpdateGround`) and the box queries |
| `src/game/carinfo.c/h` | `CarInfo_Setup` |
| `tests/car_test.c` | driving, a wall, a crash, a slope against mission 1; frames in `out/car/` |

## The car record (0x4be248, 400 x 0x2b0)

Car n's driver slot is ped 200 + n; a player in a car is its driver instead (the player's own ped).
The struct in `car.h` names every field the module reads or writes; unknown ones are `uNN`. The
important ones:

| Off | Field | Meaning |
|---|---|---|
| +0x00 | id | own index |
| +0x02 | driver | ped id, -1 none |
| +0x04 | control | 0 dummy (traffic), 1 physics / player, 2 / 3 / 9 / 10 AI driver (sentinels), 0x32 hunter |
| +0x06 | active | updated this frame (in a player's view, or held active by a mission, an AI, a drive mode) |
| +0x08 | status | -1 free; bikes: 1, 7 fallen, 8 |
| +0x14 / +0xac | front / rear | wheel points (16.16) |
| +0x1c | speed | pixels per frame; physics cars: the distance actually moved |
| +0x22 | model | car info model; 0x25 tank, 0x2f the invisible hunter model, 4 / 5 emergency |
| +0x28, +0x2a | max / min speed | car info +0x0a / +0x0c (60 for model 4) |
| +0x2c, +0x36 | width, -length | car info +0 and minus +2 (the length is stored negative) |
| +0x34 | vtype | car info +0x6a: 0 bus, 1 front of a bus, 3 bike, 4 car, 8 train, 9 tram, 13 boat, 14 tank |
| +0x38 / +0x1dc | box / box_saved | the current hitbox and the pending one of the next pose (`CollBox`, 0x44 bytes) |
| +0x80, +0x88 | enter_delay, engine | a new driver starts the engine 4 frames later; the player controls work only with it on |
| +0x8a / +0x8c | half width / length | pixels |
| +0x90 / +0xb4 | front / rear heading | the wheel headings of dummies; +0x96 the turn in progress |
| +0x9c / +0xa0 | bomb, timer | 1 armed on entry, 2 counting (`Mission_ShowBombTimer` at 0x7c), 3 game over on entry, 4 explodes past damage 10, 5 arms above 3/4 top speed ("click"), 6 explodes below 1/2 |
| +0xa2 | road_dirs | direction bits of the heading (2 north / 0, 8 / 0x100, 1 / 0x200, 4 / 0x300) |
| +0xc6 | sinking | water: 1 splash, 8 sinking, 9 |
| +0xc8 / +0xd2 / +0xd6 | doors | animation step of door 1 (deltas 6..9), door 2 and the rear door (11..14) |
| +0xfc | damage | 0..100; 100 explodes (`Car_UpdateWreck`), 0x65 burnt out |
| +0xfe | burning | fire objects on the car |
| +0x108 | z_offset | height of the sprite above the ground for the depth key (0x20000, 0x30000, tanks 0x40000) |
| +0x110 | falling | frames in the air |
| +0x11a / +0x11c | horn / siren | sound state, light deltas 15 / 16; 99 = a parked car's alarm; tanks keep their turret objects here |
| +0x128 | owner_status | 1 normal, 2 / 3 / 4 transient, 99 mission-locked (no damage) |
| +0x13c | player | the driver's player / the last attacker |
| +0x144 | physics | the rigid body drives the car |
| +0x145 / +0x146 / +0x147 | brake, handbrake, gear | from the player's control bytes 1, 2, 4 (gear -1 reverse, 0, 1 forward) |
| +0x148 / +0x149 | map_hit / obj_hit | the collision retry states (below) |
| +0x14c | thrust | car info +0x80, cut by damage past 25: (125 - damage) * 0.01 * +0x80 |
| +0x150 | skid | the lateral tyre force of the step (21 with the handbrake on at speed); > 20 leaves marks |
| +0x188 / +0x18c | thrust_in, steer | this frame's drive force; the front wheel angle in radians |
| +0x190 | body | the rigid body (0x48 bytes) |
| +0x220..+0x22c | next pose | x, y, z, heading the step proposes; collision corrects it, Car_Update commits it |
| +0x230..+0x240 | impulse | pending force and its point, state 1 -> 2 -> applied |
| +0x250 | sprite | x +0x250, y +0x254, z +0x258, depth key +0x25c, angle +0x268, deltas +0x26c |
| +0x2ac | info | the car info record |

## Car info (CarInfo_Setup 0x40c100)

Records of 0xae + 8 x doors bytes (doors a s16 at +0xac). `CarInfo_Setup` keeps a pointer per record
(0x501574, at most 256), adds the sprite group base of the vtype to the sprite number (+6), converts
the 16.16 physics values to floats in place (+0x7c mass, +0x80 thrust, +0x84 / +0x88 tyre adhesion x
/ y, +0x8c handbrake friction, +0x90 footbrake friction, +0x94 front brake bias, +0x9e back end slide,
+0xa2 handbrake slide) and gives model 4 a top speed of 0x32. Other fields: +0x0e acceleration, +0x10
braking, +0x16.. the 12 remaps (hls; `Car_HasDoor` 0x405990 in fact tests them), +0x6a vtype, +0x6b
model, +0x76 / +0x77 centre of mass (s8), +0x78 moment of inertia (int), +0x98 turn ratio (degrees a
frame), +0x9a / +0x9c drive / steering wheel offsets (pixels along the car), +0xa6 bit 0 convertible
(the driver is drawn), bit 1 an animated delta (11) while moving, +0xa7.. sound, +0xae the door
records (the first one's offsets at car +0x24 / +0x26).

## Creation

`Car_Init` 0x4067c0 fills slot n from the car info record (no memset: fields it doesn't set keep the
slot's old values, like the original), sets the sprite, the remap from a 0 / 7..12 cycle, inserts
the car into the collision grid, builds the box, resets the rigid body (`CarPhys_Reset`), puts it on
the ground (`Car_UpdateGround`) and copies the pending box to the current one. Slots are taken by the
spawners' search (first free slot whose driver ped is unused and that no player drives or views).
`Car_SpawnEx` 0x4078d0 checks the space with the box of slot `Cars_GetCount` (the next unused slot,
not the new car: a quirk kept), `Car_SpawnExOnGround` 0x407ab0 snaps z to the ground, `Car_SpawnOnRoad`
0x407310 and `Car_SpawnModel47AtBlock` 0x4076f0 face the road direction of the block. Parked cars of
the CMP and MISSION.INI come through `Car_SpawnExOnGround`: they start with control 0 at rest.

## The frame (Cars_UpdateAll 0x40adc0, Car_Update 0x40a640)

For every used slot: visibility (any player's view rectangle plus its margin), the horn, the engine
delay; then for active cars the bombs, the tram doors, bike frames, the control mode's driver
(traffic dummies: `Car_DummyFollowRoad`; AI: `Sentinel_DriveCar`; hunters), the siren lights (a
delta every 8 frames), and `Car_DummyDrive` + `Car_Update`. Cars on a railway lose 5 damage a frame
(not in the third city). Cars in the water drop their driver's player back on foot and are deleted.
Then the traffic generator per player.

`Car_Update`: a kinematic car moves with `Car_DummyMove`; a physics car gets the player's controls,
the pending impulse, and `CarPhys_Step`; a non-player physics car at rest (all of v, a, force below
0.1) goes back to kinematic. Then `Car_UpdateWreck`, and if the pose changes: out of the grid, the
speed of a physics car = the distance moved, the box bounds, `Car_CollideMap`, `Car_CollideObjects`,
`Car_UpdateGround`, commit the pose to the sprite and the body, rebuild the box, scare the peds on a
pavement, back into the grid.

## The rigid body (carphys)

State (car +0x190): centre of mass x, y (pixels, float), angle (radians, = -heading x 2pi/1024),
velocity vx, vy, angular velocity w, the geometric centre cx, cy, mass, inertia, centre of mass offset,
the accelerations and the force / torque accumulators.

- `Phys_AddForceAtPoint`: the force accumulates, the torque about the centre of mass is
  (p - c) x F with p a world point (mode 0), a body point rotated around cx, cy (mode 1) or the centre.
- `Phys_PointVelocity`: the velocity of a body point as its world position after one step of v and w
  minus now.
- `Phys_Integrate`: v += a, w += aw, x += v, angle += w (semi-implicit Euler, one step per frame),
  then cx, cy = x, y - R(angle) com; the accumulators are cleared.

`CarPhys_Step` 0x460be0 each frame:

1. The front wheel angle is kept within 0.873 rad of straight (snapped to the limit when cos < 0.642).
2. Front tyre at (0, +0x9c): its velocity is split along / across the wheel (c, s = cos, sin of the
   wheel angle), each part times an adhesion (x along: +0x84, plus the footbrake share +0x94 x +0x90
   when braking; y across: +0x88), negated, rotated back and applied at the wheel. The across part is
   summed into the skid value.
3. Rear tyre at (0, +0x9a) the same along the body (angle + pi/2), with the drive force added along
   it, and adhesions x k1 (1.5; 3.0 for model 9): footbrake (1 - bias) x friction, the handbrake +0x8c;
   with the handbrake alone the across grip uses k2 = 0.6 (3.0 for model 9) instead of k1: the back
   slides.
4. a = F / mass, aw = torque / inertia, integrate.
5. A player car without steering input returns the wheel toward straight by 0.139 rad a frame; going
   straight on a road above speed 6 it aligns with the axis within 0x20 heading units (0.0278 rad a
   frame), snapping exactly when the heading is on it.
6. The next pose: centre x 65536 truncated, heading = -angle x 1024/2pi + 0.5 truncated (& 0x3ff; kept
   when a map hit is being resolved), the wheel points, the pending box; skid marks.

`Car_ApplyPlayerControls` 0x40a2e0: brake / handbrake / gear from the control bytes; with accelerate
the drive force is the thrust forward, -0.5 x thrust in reverse (-0.15 for bikes), nothing in the air;
steering adds the turn ratio (degrees) to the wheel angle per frame. The tank turns its turret with
special + steer and fires its rocket.

### Precision

The original is x87 code from MSVC. The CRT start-up sets 53-bit precision (`_controlfp(_PC_53,
_MCW_PC)` at 0x49e925) and nothing changes it later (MGL's `_control87(0x9001f, 0xffff)` at 0x48cbbb
only masks exceptions; DirectDraw doesn't touch the FPU), so arithmetic is double precision with
values kept in registers between instructions and rounded to float where stored. The port computes in
double in the original's operand order and converts to float exactly at the original's stores (the
stores that keep intermediate values in a register, unrounded, are reproduced: e.g. the lateral tyre
force, or Phys_PointVelocity's x but not y). `__ftol` 0x49cb00 is truncation through a 64-bit integer.
Fused multiply-adds are switched off in these files (clang and GCC pragmas). Not exact: `fsin` / `fcos`
return 64-bit mantissas that the next multiply rounds once; the port uses libm's double sin / cos, a
double rounding that can differ in the last bit of a double (and rarely the stored float). libm's
results may also differ between hosts (the native tests vs the wasm build); within one build the
simulation is deterministic.

## Collision

The pending box (of the next pose) is tested; the module's state is in globals (`g_cc`): the bounds
of the box (min / max x, y, ground z and their layers), the kind of the hit (0x501d10: 2 building, 3
slope, 4 wall, 9 / 10 / 11 / 12 combinations; grid: 5 ped, 6 car, 7 object, 8 heavy object), the
contact point (`g_coll_contact`, pixels).

- **Edges**: walls are block sides, vertical (x = X, y from Y to Y + 64) or horizontal. A box side
  crosses an edge when its corners lie on different sides of the line (bit 15 of the 16-bit
  differences) and the crossing point, interpolated in 16-bit integers, lies on the edge.
- `Coll_MapSolid` 0x40f060: a corner inside a building block (type 5, at the lowest ground layer of
  the box) is a hit by that corner (4 for several); else the outer sides of the building blocks.
- `Coll_MapWalls` 0x40e7e0: the left and top faces of flat blocks whose tile is a fence tile of the
  level (`Door_IsFaceSlotUsed`, the 0x773c38 table `Mission_InitCityTables` fills); the city's special
  tile (0x4abe6c[style]; 0xb4 also in style 2) on such a face is an 8 x 8 post.
- `Coll_MapSlopes` 0x40f4c0: the sides of slope blocks toward non-slope neighbours, one pixel outside
  the block, except where the slope meets the level (its low end; with the box spanning two layers
  also the sides of the partial slopes). Bikes may also leave 2-block and 1-block slopes sideways.
- `Coll_MapAll` 0x40ffe0 runs the three and combines the kinds.
- `Car_CollideMap` 0x411280: on a new hit the car becomes a physics body, its speed and drive are cut,
  `Car_BisectMove` 0x405ed0 finds the last free pose (up to 8 halvings in whole pixels / heading units;
  after 8 the car stays where it was), then the bounce: near an axis (heading within 8 of a multiple of
  0x100) or several corners in a building, the impulse is the body's offset from the contact point / 2,
  4 or 8 (by speed + drive), or both velocity components bounce; otherwise the corner that hit and the
  heading's quadrant pick the component that bounces (|v| < 1 stops, else x 0.625, reversed) and an
  impulse of |drive| + |v|. Damage deltas by the corner (pair), a crash sound, bikes throw their rider
  above speed 12. While the car keeps hitting, the next frames retry with only the x part of the move,
  then only y (`Car_BoxMoveX/Y`, state 1 -> 2 -> 3 -> 1); when it frees itself the damage is
  speed / 4.
- `Car_CollideObjects` 0x410dc0: the grid hit list (`Coll_GatherHits` over the 3 x 3 cells; filter
  `Coll_ShouldCollide`: cars, objects of the solid types, kind 10; never peds), `Coll_ProcessHits`
  (objects are kicked or collected, heavy ones bounce the car, cars: `Car_CollideCar` and
  `Coll_CarCarImpulse`), then for cars and heavy objects the bisection and the car becomes a body
  (light cars, mass < 15, lose half their velocity).
- `Car_CollideCar` 0x411a20: crash sound, the hit directions, bikes may fall, the impact
  (`Car_ImpactSpeed`: the difference of mass x integer speed components, / 4) damages each car by
  impact / (2 x its mass) (at least 10 from a tank), delta of the side that was hit, speed halved,
  crimes reported for player cars, car triggers.
- `Coll_CarCarImpulse` 0x410560: the car hit stays on its pose and gets an impulse at the contact
  point: ((its offset from the mover's motion) x min(mass, 20) + drive) / 42, plus half the mover's
  force; it becomes a physics body.
- `Car_UpdateGround` 0x412310: the ground under the next position; a car more than half a block above
  it falls (z 8 pixels a frame, 8 damage a frame on flat ground, damage capped at 50), lands (damage
  doubled on flat ground, all deltas past 50, debris, a sound) or splashes into water; a car below the
  ground bounces back; slow cars against a step are pushed out (`Car_PushFromWalls`). The depth key is
  the lowest corner's ground minus the z offset (a layer up over a slope).

### The box queries (0x4348c0-0x435a90)

`Coll_EntityVsBox` builds an entity's box (peds 2 x 2, objects their size or 6 x 6, cars their pending
box, kinds 8 / 10 fixed) after a z window of a block and a circle prefilter, then `Coll_BoxVsBox`.
`Coll_QueryBox` walks the nine points 100 pixels apart around the box (skipping a cell just walked),
`Coll_QueryCarBox` the nine points around (x, y) against a car's pending box, testing the neighbour
cells' counts (the points and the cells don't match: an entity can be listed twice),
`Coll_QueryBoxFirst` stops at the first hit. Car hits record the direction of the hit (+0xfa).

## Wrecks, damage, doors, sirens

`Car_UpdateWreck` 0x408af0 at damage 100: the explosion (`Expl_Create`, and at both ends for buses,
trains and model 0x2c), the driver dies (thrown out of convertibles), the attacker scores (bonus by
model), crime reported, a fire object, the burnt-out sprite of the vtype (the "wcar" group: cars
alternate two frames, bikes fall), damage 0x65, speed / 4. Damage: `Car_AddDamage` 0x40a200 caps at
100, locked cars take none; past 25 the thrust drops. Doors animate one delta per frame
(`Car_Open/CloseDoor1/2/RearDoorStep` 0x40b8c0-0x40bb40; the ped side of entering calls them).
Sirens alternate deltas 15 / 16 every 8 frames.

## Camera and sound

`Car_GetCamTarget` 0x408220: x, y, z minus the z offset, width, -length, speed x 3, angle (player.c's
`player_view_target` builds the same record). `car_fill_snd` fills audio.h's `SndCar` from the car
fields the sound module reads (+0x80, +0x88, +0x110, +0x11a, +0x136, +0x138, +0x146, +0x147, +0x14a,
+0x150 and the car info's sound bytes).

## Port deviations

- `CarInfo_Setup` runs from `Cars_Init` on a copy of the style's car section (the original converts
  the loaded section inside `Style_Load`); same values.
- Edge lists hold 64 edges (the original's stack arrays hold 18 and overflow beyond).
- Out-of-range reads of the type cache (negative layers, the map edge) give 0 where the original reads
  neighbouring memory.
- sin / cos precision as above.
- Traffic driving (`Car_DummyFollowRoad`, `Car_DummyDrive`, `Car_DummyKeepLane`, `Car_SetHorn`) is in
  `src/game/traffic.c` ([Traffic](/traffic)). Not ported here (stubs): the AI drivers, hunters, explosions, the objects attached to tanks, the peds'
  side (`Ped_EjectDriver`, `Ped_PanicNear`), power-ups.
