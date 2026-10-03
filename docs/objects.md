# Objects, explosions, fires, power-ups

The object module of the original (0x44c2d0-0x44ee4f), the explosions (0x425170-0x426320), the fire
module with its fire engines (0x42e600-0x430400), the power-ups (0x46a0a0-0x46a99f), the block animations
(0x402240-0x402610) and the map edits (`Map_Set*` 0x437b50-0x438020, `Map_IsFaceSolid` 0x438650,
`Map_IsCovered` 0x438800), and how the port mirrors them. Addresses are virtual addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `src/game/obj.c/h` | the object table and record, the object_info records, level start, `Obj_Create`, `Obj_CreateAttached`, `Obj_CreateAnimated`, `Obj_Kick`, `Obj_UpdateAll` and its four lists, `Obj_Delete` and the unlinkers, `Obj_IsOnScreen`, `Obj_OnCarWrecked` |
| `src/game/expl.c/h` | the 25 explosion slots, `Expl_Create`, `Expl_CarExplode`, `Expl_AtFaceIfSolid`, `Expl_UpdateAll`, `Expl_DamageArea`, the delayed explosions |
| `src/game/fire.c/h` | the 4 recorded fires: `Fire_Init`, `Fire_Register`, `Fire_IsNearActive`, `Fire_Extinguish`, `Fire_FindNearestUnattended`, `Fire_HasObjects`, `Fire_ClearObjects`; the fire engines `FireEngine_*` |
| `src/game/powerup.c/h` | the 256 power-ups: `PowerUp_Add`, `_Reveal`, `_RemoveAt`, `_ExistsAt`, `_Collect` |
| `src/game/blockanim.c/h` | the 64 animated faces (doors) |
| `src/map.c/h`, `src/game/mapedit.c/h` | the copy-on-write map edits, `Map_IsFaceSolid`, `Map_IsCovered` (`map_covered`); mapedit.c is the game's side (the fatal error) |
| `tests/obj_test.c` | map edits, a kick, a rocket at a parked car, a blast, a power-up, a door, against mission 1; frames in `out/obj/` |
| `tests/fire_test.c` | a fire, the engine dispatched, driving there, spraying and heading home; frames in `out/fire/` |

The explosions on block faces with debris and fires (0x425520, 0x425780, 0x4258d0) are in
`mission_obj.c`, where the mission thunks 0x475700-0x475720 that call them were ported.

## The object record (0x6b40d0, 3500 x 0x88)

| Off | Field | Meaning |
|---|---|---|
| +0x00 | id | own index |
| +0x02 | speed | pixels a frame (moving list), projectiles too |
| +0x04 | life | frames in a state (tumbling, sinking), counters of the 0x40 / 0x4d and projectile types |
| +0x06 | heading | direction of motion (0..1023), separate from the sprite angle |
| +0x08 | weight | object_info +0xe; 3 never moves; `2 * speed >> weight` on a kick |
| +0x0a | type | object_info index |
| +0x0c | state | the frame state: 0 free; see `Obj_UpdateSprite` and the moving states below |
| +0x0e | frame_timer | frames since the last animation step |
| +0x10 | u10 | animation cycles done (status 7: timer steps) |
| +0x12 | u12 | 1: cycles don't count (`Obj_SetFlagE2`: a recorded fire burns until extinguished) |
| +0x14 | owner | the entity a fire burns on / an attached object rides (-1) |
| +0x16 | param | creation parameter (0x40 / 0x4d: a ped) |
| +0x18 | attach_kind | attached: 0 object, 1 / 5 car (5 also turns the sprite), 2 / 3 train carriage, 4 ped; fires: 0 object, 1 car, else ped |
| +0x1a / +0x1c | off_fwd / off_side | attached offset in pixels along / across the owner's heading |
| +0x1e | u1e | projectiles and bombs: the ped that fired / planted it (its player is blamed) |
| +0x20 | in_anim_list | |
| +0x24 / +0x28 | next / prev | list links |
| +0x2c | spr | the sprite (x, y, z, depth key, angle +0x44) |

`Obj_UpdateSprite` 0x44c3a0: the frame of state s is spr_num + s - 1 for 1-8, then three runs of six
(9-14, 15-20, 21-26) map to spr_num + 1..6, and 27-31 to spr_num + 8..12.

object_info (style, `Obj_LoadInfos` 0x44ed60): w, h, depth (16.16 after loading), spr_num, weight,
aux (+0x10), status (+0x12), num_into, into[] (the objects it breaks into). The animation code reuses
the integer parts of w / h / depth as counts (frames per cycle, frames per frame, cycles).

## Obj_UpdateAll 0x44d790

Four lists, then `Proj_UpdateAll` (projectiles are objects too, proj.c):

- **Moving** (0x6b40bc): what `Obj_Kick` set going (statuses 0, 8, 9 only; the bomb 0x16 explodes
  instead, the signs 0x21 / 0x22 stay). An object moves speed pixels along its heading (at most 48)
  unless a corner of its box is over a building (state 7, speed 0); tumbling ones (states 8-25) knock
  peds down (anim 0x2c), a ped walking into a light one (weight 0) stops it. States: 2-6 slide, 7 bounce,
  8-13 tumble, 14-19 in the air, 20-25 tumble again, 26-31 sink (status 8 floats at 26). Each frame it
  spins 0x40 (less in the air) and steps through a fixed table of state changes. Over an edge (the
  ground 8 pixels lower) it falls 5 pixels a frame and nearby peds panic. Landing on water sinks it;
  elsewhere it lands (status 0: state 7, or 2 when it breaks), and an object with `into` entries
  breaks into them, each kicked on with speed 3 in a random direction. Speed drops by 2 a frame; at 0
  it settles on the ground, leaves the list and kicks a ped standing on it.
- **Status 7** (0x6b40c0): timers. Every h frames a step; after depth steps the object is deleted and
  explodes (a pixel up), blamed on its ped's player.
- **Animated** (0x728430, statuses 5 and 9): fires (0x12, 0x13, 0x2e) make peds panic, leave smoke
  (type 10) on frame 1, follow their owner (an object, a car, a ped; a car parked at the map's corner
  puts it out) or, without one, set a ped walking into them on fire (object 0x2e, health - 10) and add
  5 damage to a car on them. Smoke (10, 0x33) rises a pixel a frame and drifts to +x / -y. Type 0x3f
  spawns a 0x40 on its first frame in state 5; 0x40 / 0x4d live until their ped dies (then 1000
  frames). Status 9 cycles 1..aux, a frame every 3; the others a frame every h over w frames, and with
  a depth for depth cycles (unless u12): then they go (a fire 0x12 leaving a dying fire 0x13 and adding
  to its car's burning count), unlinked only when aux is 1.
- **Attached** (0x6b40c4): back onto the owner at the offset (cars 2 pixels under, peds 1 over). The
  car lights 0x35 cycle 1-11, the object lights 0x30 / 0x31 1-8. `Obj_ListRotate` (used by the tank)
  moves the second entry to the head.

## Explosions (0x50f7e8, 25 x 0x64)

A slot is a sprite (kind 0xc in the collision grid; the renderer draws the owner as the sprite) plus a
frame (+0x5c) and a tick (+0x5e). `Expl_Create` takes the first group of four consecutive free slots
(groups 0..21), plays sample 0x27, applies the blast, and places four quarters of a 64 x 64 picture
around (x, y): frames 1, 13, 25, 37 of the explosion sprite group (0x774ef4). The picture floats
16..31 pixels (random) above z, its depth key a layer higher. Every 2 frames a slot steps; the first
quarter leaves smoke (0x33) on frame 10; frames 12 / 24 / 36 / 48 free the quarters.
`Expl_CarExplode` explodes at the car and the four corners of its box (8 pixels under the corners'
ground).

`Expl_DamageArea` 0x425ca0, over everything of the 3 x 3 cells touching the layer of z:

- peds (alive, not dead / in a car, at most a layer below) within 60 pixels are thrown (speed 16, away
  by quadrant); within 35 they die (anim 0x5a), further out they catch fire (object 0x2e, sample 0x18,
  health - 10). The owner scores (`Score_PedKilled` cause 2) and the police hear of it (crime 8). In
  single player a ped seen lately counts for the player.
- objects without an owner at rest within 75 pixels are kicked away (speed 10); a gas tank (0x45) within
  25 goes off 5 frames later (the 25 delayed explosions at 0x50f610). Crates (0x54) open.
- cars within 64 pixels are wrecked (damage 100) and blamed on the owner (crime 7); the tank (0x25) only
  takes 4 damage.

`Expl_AtFaceIfSolid` 0x425960 (rockets against walls): an explosion on a side face of a block when the
face is there (`Map_IsFaceSolid`), plus two fires beside it where `World_AnyThingAt` finds none.

## Fires (0x511988, 4 x 0x24)

{object, x, y, z (the road block next to it, pixels), engine (its car id), objects[10] (the water jet;
the last one counts the spraying frames), extra}. `Fire_Register` (from
the creators of fire objects) records a type 0x12 fire when the record, fire and engine counts allow,
no recorded fire with an engine is within 40 blocks and it doesn't burn on the cars 0xb / 0xd: within 2
blocks of a recorded fire it becomes that fire's `extra` (and burns on: u12); otherwise the nearest
road (`Map_FindNearestRoad`) on its layer within 4 blocks gets a fire engine (`FireEngine_Dispatch`).
No engine to send: the record is dropped again.

## Fire engines (0x42e870-0x430400)

A fire engine is a sentinel of kind 6 ([Police](/police)) driving car model 0x2a. `FireEngine_Dispatch`
0x42ec70 takes a free record (`Sentinel_FindFree`) and spawns the engine (`FireEngine_Spawn` 0x42e920)
at the nearest (Manhattan) of the 4 fire stations of the CMP's locations, else at each in turn:
`Car_SpawnOnRoad` with a driver (control type 6), car control 9, cruise speed 6; the record listed in
engines[] (0x511a1c, 3) with 2500 frames to stay out (0x511978), and the hose (object 0x32) attached 1
left and 6 back, turning with the car (its id also at car +0x11c, so the siren code leaves it alone).
With 3 out and no free engines[] slot an engine heading home is sent instead (quirk: only sentinels
0..2 are looked at, each against its own engines[] slot). `FireEngine_SetDestination` 0x42ea90 puts
the siren on and starts a route search (mode 5) to the road block next to the fire.

`Sentinel_DriveCar` drives it along the route and calls `FireEngine_Update` 0x42f460 every frame, a
state machine on the record's +0x1b:

| State | |
|---|---|
| 5, 2 | waiting for the path search (another controller holds it), the search running |
| 1 | driving to the fire; stops (-> 10) within 4 blocks (Chebyshev, another layer counting 500 more) unless still moving and the next block is closer, or at the end of the route |
| 10, 0x14 | braking to a stop; the hose stops turning with the car |
| 0x64 | the hose turns 3 a frame toward the fire object (`FireEngine_AimHoseAtObject` 0x42f240) |
| 0x6e | the jet: n = (d - 52) / 32 + 1 objects (d the hose-to-fire distance in pixels, at most 320 and n below 9): 0x30, each 0x1e ahead of the previous (the first 0x24 ahead of the hose), and a 0x31 end, shortened by the rest of the division; the fire object is deleted (its record keeps the position) |
| 0x82 | spraying: 262 frames while the fire object's record stays where it was |
| 0x97, 0xa0 | the jet deleted; the hose turns back to the car (`FireEngine_AimHoseAtCar` 0x42f2d0), turns with it again, the fire record is dropped (`Fire_Extinguish`) |
| 0x1e | the nearest unattended fire (`FireEngine_ArriveCheck` 0x42ebe0: more than 2 blocks away it drives there) or home (`FireEngine_ReturnToBase` 0x42ee40: siren off, a route to the nearest station) |
| 0x23, 0x28 | the search home running, driving home; an unattended fire on the way is taken; at the end of the route 0xff |
| 0x27, 0xaa | giving the fire up (its object and record go) -> 0xff; a wall in the jet's way -> 0x97 |
| 0xff | dismissed: `Sentinel_DriveCar` removes it (`FireEngine_Remove` 0x42e870: hose, car, record) once off screen |

A burnt-out engine (damage 100) or one out for 2500 frames gives its fire up; burnt out, its hose is
deleted and the fire dropped every frame. `tests/fire_test.c` lights a fire 8 blocks from a station in
NYC: the engine stops 4 blocks from it after about 530 frames, sprays a jet of 9 objects and heads
home at frame 910.

Port notes: engines[] is filled at `engine_count`, which `FireEngine_Remove` decrements whatever slot
it frees, so a spawn after an engine left from a lower slot overwrites a live one (which then keeps
no timer and is never removed from the list): kept. Object -1 (the hose after a burn-out) and the
request of the victim slot -1 (`FireEngine_ReturnToBase` writes one, 0x50caae, unused memory) are
read as a blank object and not written.

## Power-ups (0x74f858, 256 x 0x1c)

{x, y, z, object, visible (1 crate, 2 revealed), type, value}. `PowerUp_Add` puts a crate (0x54; help
signs, type 14, as 0x5f at once). `PowerUp_Reveal` (rockets, cars, punches, blasts) replaces the crate
with the power-up's object (types 1-4: 0x4e-0x51, 6: 0x61, 9: 0x65, 10: 0x60, 11: 100, 12: 0x62, 13 /
15: 99; the others none: the record is cleared) and a broken crate (0x55). Touching the object (ped and
car collision code) calls `PowerUp_Collect`: weapons (default ammo from the exe table 0x4a8c18 when the
value is 0, the value if below 100, else a temporary weapon for value - 100 frames; refused at 99
ammo or with a temporary weapon), speed-up (in a car `Car_DampThrust`, on foot fast walking for 0x177
frames), bribe (wanted level cleared), armour (3 hits; refused when full), multiplier + 1, get out of
jail free (refused when held), extra life (15 with a voice), help text "help<value>". The local player
sees the item's text; a sample plays (2 weapons, 3 odd types, 4 even).

## Block animations (0x4bbc70, 64 x 0x74)

{which (1 lid, 0 side), tile, active, tick, speed, index, event type, event arg, n, frames[10] of
{kind, tile}}. `BlockAnim_Create` gives a door's face a fresh tile number past the style's tiles
(`Style_AllocSide` / `Style_AllocLid`: the style's counts grow) and points the face at it
(`Map_SetBlockFace`), showing the old tile. `StartForward` / `StartReverse` fill the frames with
ascending / descending tiles of a kind (2 = aux tiles); `BlockAnim_Tick` steps every `speed` frames
(`Style_SetTileFrame` sets what the tile shows) and dispatches the event (2 / 3: door opened / closed)
on reaching the last frame.

## Map edits (0x437b50-0x438020)

The loaded map is shared data; an edit first copies the block's column (7 - h shorts) into the column
change area (0x2000 bytes after data_end + 0x2000) and the block (8 bytes) into the block change area
(0x2000 bytes at data_end), repoints the column entry and the base table, then changes the copy; later
edits of the same block change the copy in place. Overflow is fatal (-0x15, "Map change overflow").
`Map_SetBlockType` / `_SetBlockKind` / `_OrBlockFlags` update the type cache; `Map_SetBlockFace` doesn't.

## Quirks kept

- `Obj_Kick` pushes onto the moving list without setting `prev`; when an object settles, the list is
  unlinked through `prev` (NULL), so the head becomes the next object and the ones before it stop
  being moved.
- `Obj_Kick` compares the angle with itself plus / minus 0x20 to turn it by 0x40: never true.
- Moving objects give `Map_SlopeDelta` the block kind (0..7) instead of a type map: no slope speeds
  them up.
- `Obj_CreateAttached` tests the attach kind, not the type, against the blended types.
- `Obj_CreateAnimated` initialises the sprite with the owner as its angle (then sets 0); callers that
  pass a heading as the last argument set the attach kind to it.
- `Obj_DeleteByOwner` deletes free slots whose stale owner matches (the smashable count drops again).
- `Fire_IsNearActive` ignores fires whose engine id is 0; `Fire_Register` compares against every record,
  the free ones (at block 0) too.
- `Expl_AtFaceIfSolid` explodes without testing the face outside blocks 1..255 / 0..255, and gives
  `World_AnyThingAt` block coordinates for the first fire.
- In `Expl_Create` the quarters copy z from the first slot, even when the first was outside the world.
- `Map_SetBlockKind` ORs the new type bits in on a block's first edit (it reads the source block after
  clearing the copy) and replaces them on later edits.
- A blast in single player credits every later kill of the same blast to the player once a ped seen
  lately died.
- Status 4 objects turn their heading on landing without wrapping it (see the deviations).

## Port deviations

- Out-of-range headings (status 4 landings) index the sine table wrapped; the original reads the tan
  table before it or memory after it.
- `PowerUp_Reveal` on a record without an object doesn't delete object -1 (the original indexes before
  the table).
- The block animation frame list stops at the end of the table (the original writes on into the next
  record, and past the last one).
- `Map_IsCovered` answers false outside the map (unchecked in the original).
