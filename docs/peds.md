# Peds, the player on foot, input

How the original reads the controls, turns them into the player's held inputs, and moves, animates
and spawns pedestrians; how the port mirrors it (`src/game/input.c`, `replay.c`, `player.c`,
`ped*.c`, `proj.c`, `weapon.c`). Addresses are virtual addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `input.c/h` | `Input_Init` 0x432b40, `Input_ReadControls` 0x432e00, `Input_ActionPressed` 0x4331f0 / `Released` 0x4333c0, `Config_MapControlKeys` 0x46e960, the key event queue (`Input_GetKey` 0x414a80, `Input_FlushKeys` 0x414a70) |
| `replay.c/h` | `Replay_SetFileName` 0x432c50, `Replay_Begin` 0x432c90, `Replay_EndSave` 0x432d80, `Replay_IsPassthroughKey` 0x433730, `Replay_TickFrame` 0x433790, `Replay_IsPlaying` 0x4337a0 |
| `player.c/h` | the player record and the player module 0x4616b0-0x464c90: `Player_ApplyInput` 0x463ec0, `Player_ToggleVehicle` 0x4642b0, `Player_UpdateAll` 0x464880, weapons and ammo, wanted level, `Player_Wasted` / `Player_Busted`, entering / leaving vehicles, the *ByPed accessors |
| `ped.c/h`, `ped_internal.h` | the ped table, `Ped_InitAll`, `Ped_Reset`, `Ped_UpdateAll` 0x45cd50, `Ped_UpdateSprite` 0x44f100, `Ped_SetTurnInput` 0x45f670, `Ped_SetDestination` 0x45f780, `Ped_EnterExitKey` 0x45f5e0 and the small accessors |
| `ped_move.c` | `Ped_Process` 0x45a3b0 and its collision responses 0x458ca0-0x45a3af, `Ped_UpdateRiding` 0x45cbb0 |
| `ped_step.c` | `Ped_ComputeStep` 0x456fb0 and the steering 0x455040-0x456fb0, `Map_SlopeDelta` 0x454c60 |
| `ped_anim.c` | `Ped_Animate` 0x44fa70 |
| `ped_car.c` | getting into and out of cars, `Ped_EjectDriver`, `Ped_CreateCarDriver`, respawning (`Player_RespawnAtStation` 0x4601a0) |
| `ped_spawn.c` | `Ped_SpawnAmbient` 0x4537b0, the creators 0x453e90-0x4544df, `Ped_PanicNear`, the visibility tests, `Ped_FireWeapon` 0x4532f0, groups |
| `proj.c`, `weapon.c` | projectiles 0x4879b0-0x488e1f, `Weapon_Fire*` 0x488e20-0x4892ff |

## Input

### Key events

The original takes keyboard events from SciTech MGL's queue (`Input_GetKey` 0x414a80): the PC set-1
scan code of a key down, + 0x80 for a key up; the cursor block (0x47-0x53 when the key has no
character: arrows, Home, End, Page Up / Down, Insert, Delete) gets + 0x100, other extended keys
(keypad Enter, right Ctrl) read as their plain codes. So Up is 0x148 and its release 0x1c8.

The port has a queue of the same codes (`input_post_key`). The host gives held keys
(`plat_keys`, platform codes: set-1 + 0x100 for every extended key); `input_feed_held` turns the
changes since its last call into press / release events (in code order) and folds the extended
keys outside the cursor block to their plain codes, as MGL reports them. The app calls it once per
host frame (70 Hz) before `game_run_step`:

```c
/* src/app.c, APP_GAME, before game_run_step(): */
uint8_t held[KEY_COUNT];
if (plat_keys(held)) input_feed_held(held);
```

`Replay_Begin` (from `Game_Init`) flushes the queue at every level start; the held state seen by
`input_feed_held` survives, so a key held across the start produces its release later, as with MGL.

### Bindings

Ten actions. `Config_ReadRegistry` 0x46e820 (from WinMain) reads `HKLM\SOFTWARE\DMA Design\Grand
Theft Auto\Controls\Control 0..9` (DWORDs; missing keys are fatal: the installer's "GTA Settings"
creates them). The values are DirectInput key codes; `Config_MapControlKeys` 0x46e960 converts
them (below 0x80 as is; 0x80-0xff + 0x80, so DirectInput's 0xc8 Up becomes 0x148; 0x100-0x11f
joystick buttons + 0x2f1; 0x140-0x145 joystick axes + 0x2a9; anything else 0) and stores entry i at
the action given by the table 0x4a8c58 (read from the exe: 0, 1, 2, 3, 4, 6, 5, 8, 9, 7).

The port has no registry; it uses the values GTA Settings writes by default (its first preset):

| Control n | DirectInput | Key | Action |
|---|---|---|---|
| 0 | 0xcb | Left | 0 steer left |
| 1 | 0xcd | Right | 1 steer right |
| 2 | 0xc8 | Up | 2 accelerate / walk |
| 3 | 0xd0 | Down | 3 brake / back |
| 4 | 0x39 | Space | 4 handbrake / jump |
| 5 | 0x1c | Enter | 6 enter / exit |
| 6 | 0x1d | Ctrl | 5 fire |
| 7 | 0x2d | X | 8 next weapon |
| 8 | 0x2c | Z | 9 previous weapon |
| 9 | 0x0f | Tab | 7 special (horn) |

`Input_Init` 0x432b40 marks whether any action is bound to the keyboard (below 0x3e9) or the
joystick. The port has no joystick: bindings to it never fire.

### The control word

`Input_ReadControls` 0x432e00 returns one 32-bit word per frame (stored at 0x5031a8 per player). It
holds changes, not the held state: a frame in which nothing changes gives 0.

| Bits | Meaning |
|---|---|
| 0, 9-12 | steering changed; the new value, 4-bit two's complement (-7 left, 0, 7 right: left and right add -7 / +7, so both held cancel) |
| 1, 15-16 | accelerate changed; 0 or 3 |
| 7, 13-14 | brake changed; 0 or 3 |
| 2, 20 | Space changed; held |
| 3, 21 | fire changed; held |
| 22 | enter / exit pressed (there is no release event) |
| 5, 19 | Tab changed; held |
| 4, 18 | next weapon changed; held |
| 8, 17 | previous weapon changed; held |
| 6, 23-31 | a key event for `Game_HandleKey`: the code (+ 0x80 released) |

Reading the queue: each event is matched against the ten bindings; a bound key's press is its
action pressed, its release (only if the press was seen) the action released. A key the HUD wants
(`HUD_WantsKey` 0x482c80: chat, menus) or that isn't bound becomes the key event in the high bits,
and that ends the frame's reading: the events after it stay queued for the next frame. Repeats are
ignored (an action already held isn't pressed again).

### Replays

Every game records: the non-zero words go to the buffer 0x5ce750 as 8-byte records {u32 frame, u32
word}, at most 0x4000; the frame is `Replay_TickFrame`'s counter (Game_Update). `Replay_EndSave`
writes the buffer to `..\gtadata\replay.rep` when the game ends (unless nothing was recorded, or the
option 0x502f44 appended each record as it went, truncating the file at the start). With the option
0x5031f0 the level instead plays the file back: until the next record's frame only the passthrough
keys (Alt, F6, keypad +, Esc, F12, R and their releases) are read from the keyboard; at its frame
the record is the word (and if it has no key event, the frame's key may still be a passthrough
one). With 0x5031f4 (the frontend's attract mode) any key abandons. After the last record a single
player game goes back to recording, a network game stops. The port saves through
`plat_save_user_file("replay.rep")` and plays back the user file, else `GTADATA/replay.rep`.

## The player's held controls (Player_ApplyInput 0x463ec0)

`Game_Run` calls it with the viewed player set, for a non-zero word. It keeps the held values in the
player record:

| Field | From |
|---|---|
| +0x194 `ctl[3]` | steering (-7..7), in a car or on foot |
| +0x191 `ctl[0]`, +0x195 `ctl[4]` | accelerate: the value with `ctl[4]` = 1; brake: the value with `ctl[4]` = -1 (0xff). On a train they are train commands 3 / 4; on foot accelerating also calls `Ped_RespawnBesideCar` |
| +0x193 `ctl[2]` | Space held |
| firing | fire held / released: `Ped_StartFiring` / `Ped_StopFiring` on what the player controls (in a car: its driver, with `Car_OnDriverEnter`; on a train: boarding / leaving) |
| enter / exit | `Player_ToggleVehicle` 0x4642b0 unless +0x189 is set or the game is frozen or paused |
| weapons | `Player_NextWeapon` / `Player_PrevWeapon` on the press |
| +0x196 `ctl[5]` | Tab held: the horn in a car, a sound on foot |

The *ByPed accessors (0x4645c0-0x464750) give these bytes to the ped code by ped id.

## The ped record

620 records of 0x100 bytes at 0x7284e0. Slots 0..199 ambient and mission peds, 200..599 the drivers
of cars 0..399, 600..619 special peds. The fields the port names (ped.h):

| Offset | Name | Meaning |
|---|---|---|
| 0x00 | id | |
| 0x02 | turn | angle change per frame |
| 0x04 | accel | player's accelerate input (3 forward, < 0 back) |
| 0x06 | speed | step per frame in pixels; -2 backing |
| 0x08 | move_speed | 4 walk, 6 with the speed-up power-up: the player's top speed |
| 0x0a | anim_tick | `Ped_Animate` advances every other call |
| 0x0e | u0e | frame counter (dead peds vanish after 1000) |
| 0x10 | control | -1 free, 0 ambient, 8 + n player n |
| 0x12 | u12 | the side a ped steers round things (0x80 / -0x80); the turn in action 0x12 |
| 0x16 | graphic | 0 civilian, 1 cop, ... (0xbd sprites each) |
| 0x18 | anim | animation state; 0 = slot free (below) |
| 0x1a | idle_count | frames in the current pose |
| 0x1c | player_ctl | player-controlled |
| 0x2c, 0x30 | target_x, target_y | |
| 0x38, 0x3c | walk_x, walk_y | walk target, 0 = none |
| 0x40 | u40 | the other ped of a car door animation |
| 0x42 | mode | what the walk target is (0xb a car door, 0x3c a detour, 0x3e across the road) |
| 0x46 | firing | fire held |
| 0x49 | health | 100; 0 dead |
| 0x4c | car | the car it drives / walks to (-1) |
| 0x50, 0x52 | attach_kind, attach_id | riding: 1 car, 2 object, 3 ped |
| 0x54, 0x56 | u54, u56 | riding offset across / along |
| 0x58 | carried | carried object (-1) |
| 0x5e | remap | |
| 0x60 | weapon | 0 none, 1 pistol, 2 machine gun, 3 rocket launcher, 4 flamethrower |
| 0x6c | objective | 0x19 wander, 0x25 player, ... |
| 0x70 | state | 2 normal, 1 walk to target, 3 waiting to cross, 4 go to, 7 in a car, 9, 10 falling, 0x17 / 0xc dead, 0x18 in water, 0x15 / 0x16 on rails |
| 0x78, 0x7c, 0x80 | u78, u7c, u80 | sub-modes; 0x7c the action (2 normal, 9 / 10 side step right / left, 0x12, 0x13 in / at a car, 0x14 punching, 0x15..0x1c crossing), 0x80 the saved action |
| 0x86 | target_ped | |
| 0x89, 0x8a | group, group_slot | |
| 0x8b | u8b | mission ped |
| 0x8c | train | |
| 0x90 | spr | the sprite: x, y, z, z key (+0x9c), angle (+0xa8) |
| 0xee | uee | frames without moving |
| 0xf0 | prev_angle | the angle at the start of the frame |
| 0xf4 | uf4 | going round a car |

## The frame (Ped_UpdateAll 0x45cd50)

For every slot in use, in order (the port's `ped_update_all` comments each step):

1. the start angle kept (+0xf0). A player ped gets objective 0x25; on foot in the normal action the
   player controls what its camera follows.
2. Player controls (not while getting in / out of a car, falling, dead or on rails): the steering
   becomes the player's turn input +0x198 (`Player_SetFootAux198` 0x463990: +4 left, -4 right, kept
   while turning the same way), which `Ped_SetTurnInput` 0x45f670 multiplies by 4 each frame and
   clamps to ±0x30: a held key turns 16 units the first frame, then 48 (17° at 23 frames a second).
   Accelerate starts the walk at speed 4 and adds one a frame (`Ped_Process` caps it at the move
   speed); brake backs at -2; neither stops the ped. The original clears ped 0's turn instead of the
   player's when the player can't turn (an absolute address): kept.
3. A ped in walk frame 1 at speed 0 stands (0x88); fire held fires; ambient peds stand 16 frames at
   most; a carried object costs health.
4. Health 0 kills: blood (object 0x3f under the ped, its angle the ped id: the original reuses
   `Map_GetGroundZ`'s pushed arguments), anim 0x2d, state 0x17.
5. The turn is applied (a standing ped animates while turning); ambient peds out of every player's
   view (`Ped_IsVisibleRecent`) vanish; dead ones after 1000 frames; then `Ped_Process` (riders:
   `Ped_UpdateRiding`).

After the loop up to two ambient peds are spawned while fewer than 200 are active.

## Movement (Ped_Process 0x45a3b0)

One step of a ped on foot. In the port the locals keep the original's roles: the new position
(nx, ny), `go` (it may move), `blocked` (1 a wall, 2 a slope stopped it), `car_hit`.

1. Dead peds fall over (0x2d); off-screen peds waiting to cross skip the frame; the player's speed
   is capped at its move speed (and -2 back), ambient peds at 4.
2. Punching (action 0x14): the ped in front is knocked down or pushed back (the 10 / 11 / 12 count
   0x74f0fc decides).
3. A player walking to a car door gives up when the car drives off; ambient peds with a walk target
   steer to it (`Ped_SteerToTarget`).
4. Rails (block attribute 1) turn a ped back after 21 frames; the ground under it: buildings push it
   out (`Ped_StepOutOfBuilding`: the first of west, north, east, south that isn't a building, 1
   pixel), pavement may start a crossing (`Ped_CheckCrossing`), a road sends ambient peds back to
   where they are (state 1).
5. The next position: the player (and peds in special actions) move `speed` pixels along the
   heading, `nx = x + sin(a) * speed, ny = y + cos(a) * speed` (16.16 sin table, a 0..1023 with 0
   along +y); ambient peds use `Ped_ComputeStep`. On a slope ambient peds walk along it
   (`Ped_SlopeAdjust`).
6. Cars at the ped's feet run it over (sound 0x11, blood, anim 0x2d) unless slow: then a player can
   jump onto the roof (anim 0x73) of a car (not a bus / truck kind 0..2, 0xe) or slides off (0x92).
7. Walls: a box (4 x 4 at the new position; 6 x 6 ahead for peds walking to a target) against
   buildings under its corners and a drop at the new position (`Ped_CheckWallHit`: ambient peds turn
   back from an edge unless there is walkable ground near, `Map_FindWalkableZ`), the map's walls
   (`Coll_MapWalls`) and slopes (`Coll_MapSlopes`): a hit keeps the old position.
8. Other peds ahead (a 6 x 6 box 8 pixels ahead): the player pushes them aside (`Ped_AvoidPed`, a
   scream); cars and trains ahead make peds go round (`Ped_AvoidCar`), objects aside
   (`Ped_AvoidObject`; power-ups are collected by a player); the player running (speed > 2) with
   Space jumps over a car's bonnet (anim 0x73).
9. Ambient peds keep to the pavement: a side step back when the next 0x30 pixels aren't pavement, or
   a detour a block to the side where there is pavement (four patterns in turn, 0x72844d).
10. The ground under the new position (`Map_GetGroundZ`): a step of more than a quarter block up or
    down stops the ped; water drowns ambient peds (splash object 0x36); more than a quarter block
    below falls (state 10, z + 2 pixels a frame, health - 2 per frame).
11. Stuck for 8 frames (4 for a player with a walk target) turns the ped 0x140; then the position is
    committed (the grid node moves with it) unless something stopped it, in which case it turns
    (`Ped_TurnAtObstacle` with a target, else a quarter or more).
12. The depth key: over slopes from the ground under the box's corners; dead peds lie under live
    ones, ordered by id. Then `Ped_Animate`.

## Animation (Ped_Animate 0x44fa70)

A state machine on `anim` (+0x18), advanced every other call (+0x0a). The sprite of a state comes
from the table 0x4b1e90 (one short per state, read from the exe) plus the ped's graphic type * 0xbd
and the ped sprite group base, with weapon variants (`Ped_UpdateSprite` 0x44f100: pistol +0x63,
machine gun +0x88, rocket launcher +0x9a, flamethrower +0x76 when a player fires on the move; the
standing poses 0x59 / 0x5a take +0xf / +0x3f, +0x51, +0x2d). The groups of states (each commented
in `ped_anim.c`):

| Anims | What |
|---|---|
| 1..8 | walk cycle (footstep sound on 3 and 8 for players); at speed > 2 it switches to the run cycle |
| 9..0x10 | run cycle (footsteps 0xc, 0x10) |
| 0x88 | standing |
| 0x11..0x1d, 0x1e..0x22 | walking to the driver's door, opening it, pulling a driver out (+0x40 the victim), climbing in, closing it; the door steps are the car's (`Car_OpenDoor1Step` / `Car_CloseDoor1Step`), the shared step counter 0x74f0f4 |
| 0x23..0x26 | getting out |
| 0x27..0x2b | stumbling, knocked down (0x2b lies 20 frames) |
| 0x2c, 0x2d | dead: `Score_PedKilled`, state 0x17, an ambulance call for ambient peds |
| 0x30, 0x31 | in water |
| 0x39.., 0x51..0x59 | convertibles and riding open vehicles |
| 0x47..0x50 | side steps and quarter turns (2 x 2 car test) |
| 0x5d..0x61 | falling and landing (a fall on hard ground kills) |
| 0x64..0x87 | the passenger side, the rear door, over the bonnet (0x73) |
| 0x89..0x91 | shot (knock-back six pixels, then dead) |
| 0x92.., 0xa9..0xaf | thrown out of a car, punched |
| 0xe9..0xf0 | electrocuted |

## Getting into and out of cars (ped_car.c)

The enter / exit key (`Player_ToggleVehicle` 0x4642b0 -> `Ped_EnterExitKey` 0x45f5e0): on foot
`Ped_TryEnterCar` 0x45def0 looks for a car near the ped (a train door when standing at one), and sends
the ped to the driver's door (`Ped_SendToCarDoor1` 0x45f4a0: the car info's door offset rotated by
the car's heading, state 4 / mode 0xb); `Ped_ComputeStep` walks it there and `Ped_SitInCar` /
`Ped_FinishEnterCar` hand it to the car (`Player_EnterCar`: the player controls the car, the camera
follows it, `CarPhys_Begin`). In a car `Ped_PlayerExitCar` 0x45d3d0 needs a slow car, picks the door
side not blocked by walls (`Ped_CheckWallHit`) and starts the get-out animation; `Ped_EjectDriver`
0x44f510 puts a car's driver on the road at once (wrecks, cops). Respawning: `Player_RespawnAtStation`
0x4601a0 (busted: the nearest police station of the CMP's locations, wasted: the nearest hospital;
the nearest one is never tried and the hospital count is used for both: kept).

## Ambient peds (Ped_SpawnAmbient 0x4537b0)

Up to two attempts a frame while fewer than 200 peds are active: a free slot of 0..199, a point just
outside the player's view on the side it walks toward (in turn when standing), snapped to a block
and offset by 16..48 pixels (cycling), on pavement (ground type 3), not on a slope, not in any
player's view (+ 64 pixels) and with nobody there; it walks at speed 1 with the next ambient colour.
Every 100th spawn leads a group of six followers (objective 0x15 / 0x16, `Ped_LeaveGroup`). Peds
out of every view vanish (`Ped_IsVisibleRecent`: in a view rectangle, or seen within 30 frames).

## Weapons and projectiles (weapon.c, proj.c)

`Ped_FireWeapon` 0x4532f0 (fire held) fires the ped's weapon when its reload counter (+0x64) allows:
`Weapon_FireBullet` 0x488e20 (pistol and machine gun), `Weapon_FireFlame` 0x488fd0,
`Weapon_FireRocket` 0x489150; players use ammo (`Player_UseAmmo`: the machine gun and flamethrower one
unit per 5 shots), peds nearby panic (`Ped_PanicNear`). Projectiles are objects of the object table
(types 0x4a bullet, 0x1f rocket, 0x4b flame) listed at 0x785318 (at most 40); `Proj_UpdateAll`
0x487a70 moves them at the end of `Obj_UpdateAll` and resolves hits on peds, cars, objects and the
map; rockets explode (`Proj_Detonate`).

## Port notes and deviations

- The registry bindings are GTA Settings' defaults (no registry); no joystick.
- Out-of-range reads of the block type cache (the original indexes the flat 6 x 256 x 256 array
  without checks) read 0 outside the array; inside it the original's row wrap is kept.
- `Player_*ByPed` for a ped no player has read player 0 instead of the record before the table;
  likewise other reads the original makes before a table with an id of -1 (ped, car, group) read
  ped / car 0 or are skipped (each is commented where it happens).
- `Ped_UpdateAll` clears ped 0's turn where the player can't turn (the original's absolute address).
- `Map_GetLidBelow` 0x4387b0 and `Map_TestBlockAttr` 0x44b310 are `src/game/mapq.c`'s (the level's
  map queries).
- `Player_AwardBonus` 0x462000 (player.c) scores the events with their chains: the same kind in a row
  (13 frames) doubles the base per event, the 7th of kind 7 in a row adds 12300 times the multiplier
  with a big message.
