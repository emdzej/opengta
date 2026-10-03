# Police, emergency services, gangs

The wanted level and the crime reports (0x40d640-0x40dc7f, 0x4131d0-0x41429f), the police module
(0x464e20-0x46a09f), the emergency services ("sentinels", 0x419000-0x4227a0), the ambulance crews
(0x401340-0x401a7f), the scripted helicopter (0x40dc80-0x40e19f) and the Hells Angels gang and the
hunting cars (0x431500-0x4325ff), and how the port mirrors them. Addresses are virtual addresses in
`gta.exe`.

## Files

| File | What |
|---|---|
| `src/game/wanted.c/h` | criminal records, `Police_ReportCrime`, the per-city thresholds, the scanner reports, the pursuit groups (`Police_StartPursuit`, `Police_UpdatePursuits`, `Police_EndPursuit`) |
| `src/game/police.c/h` | the criminal record screen, patrol cars, `Police_SpawnCar`, pursuit membership, roadblocks, `Cop_Update` |
| `src/game/sentinel.c/h` | `Emergency_UpdateAll` (from the disassembly), `Sentinel_DriveCar`, the steering and look-ahead probes, warps, the ambulance call queue, the police dispatch list, the chasers, wreck removal |
| `src/game/ambulance.c/h` | the ambulance crews: dispatch, the medic, reviving, back to the hospital |
| `src/game/heli.c/h` | the mission-end helicopter |
| `src/game/gang.c/h` | `Gang_*` and the hunt table, `Hunt_UpdateCar` |
| `tests/police_test.c` | mission 1: crimes, the wanted level, a pursuit, a roadblock, an arrest, an ambulance; frames in `out/police/` |

The controller table itself (0x507ea0, `Sentinel_Get` / `FindFree` / `ClearTable` / `Reset`) is
`ai.c`'s (struct `AiCtl`); `sentinel.h`'s `Sentinel` is the same 0x98 bytes with the field names
this part of the port uses. The junction override records (0x505f00) are `lights.h`'s `JunctionOvr`,
`Map_FindNearestRoad` and `Path_Find` are `path.h`'s.

## The wanted level

A crime is reported with `Police_ReportCrime(kind, id, crime, x, y, z)`: kind 0 a car (its driver
must be a player, the car under physics control and not model 0x2f), kinds 1 / 2 a player's ped.
Position 0 means "where the reporter is". The record (`Criminal`, 0x2c bytes at 0x502dd8, four of them)
is the one of that ped, else the first free one.

| Crime | Wanted points | Score bonus kind | Scanner sample | Counter |
|---|---|---|---|---|
| 2 car hit by a player's car (only above speed 11 or below -5) | 2 | 0x18 | 0xe | 2 |
| 3 | 50 | - | 0x18 | 3 |
| 4 | 10 | 0x1c | 0x1c | 4 |
| 5 car stolen | 15 | by model, 0x1c..0x24 | 0x1c | 5 |
| 6 | 50 | 0x25 | 0x20 | 6 |
| 7 | 1 | - | 0x22 | 7 |
| 8 shooting | 100 | - | 0x23 | 8 |
| 9 bank robbery | 100 | - | 0x2a | 9 |

The counters are the player's crime counters (`Player_IncKills`: shorts at +0xfc this life, +0x110
total; `Police_ShowCriminalRecord` prints and clears them). Points are capped at 2000
(`Player_AddWantedPoints`). After the crime the level is recomputed for the city (the MISSION.INI
section: 1 / 2 NYC, 0x66 / 0x67 San Andreas, everything else Vice City):

| City | level 1 | level 2 | level 3 | level 4 | units at levels 1..4 |
|---|---|---|---|---|---|
| NYC (`Police_SetWantedCity1`) | 151 | 251 | 351 | 501 | 1, 2, 3, 4 |
| San Andreas (`..City2`) | 101 | 201 | 251 | 376 | 1, 2, 4, 6 |
| Vice City (`..City3`) | 101 | 176 | 251 | 351 | 1 at every level |

With the no-patrols switch (0x503184) the level is always 0. Level 0 clears the record (and ends its
pursuit). `Police_UpdatePursuits` recomputes the level of every running pursuit's criminal every
frame, so wanted points added directly (missions, `Player_AddWantedPoints`) take effect there too.

A new record's pursuit is pending with a countdown: 200 frames on foot, 100 in a car, 1 when the car
last touched a police car (control 3), which also gives a player without a wanted level the points of
level 1. Any non-zero level sets the countdown to 1. Each crime also queues a police scanner report
(three slots per record, one per area, compass part and crime) said by `Police_TickRadioReports`
after the same delay (`Snd_PoliceRadio` with the crime sample, the zone part and the nav zone's sample
from `Area_GetSample`).

### Quirks kept

- The "at least 3 / 6 units" each crime writes into +0x24 is overwritten by the city's level right
  after: dead code.
- A car crime first looks for a matching delayed report in the mission's timed table 0x7715d8 indexed
  by the player number (a mission's "bomb" set on the car): it then only starts that one counting.
- `Police_EndPursuit` clears the sightings of the criminal record with the pursuit's number.
- `Player_IncKills` compares a short with 0x7fff, which is always true: the counters wrap.

## Pursuits

Four groups (`Pursuit`, 0x3a bytes at 0x501ac0): the criminal record, up to 20 cop controllers, the
lead (the cop nearest the target), the shoot (+0x36, wanted level 3) and shoot-on-sight (+0x35, level 4
and more) flags. `Police_StartPursuit` 0x40da90 (named `Police_SpawnRoadblock` in the first names; it
spawns nothing) only looks at criminal record 0: when its countdown runs out it takes the first group
that isn't running (if it is free) and queues the record's number of units on the dispatch list
(0x5058a8), each the nearest patrolling police car (state 199) or none. `Emergency_UpdateAll` then sends
them; `Police_UpdatePursuits` keeps the flags and the lead and sends the farthest cop home while the
group has more cops than the record wants.

## Emergency_UpdateAll (0x419880)

The decompiler fails on it; the port follows the disassembly. Every frame, in this order:

1. `Police_StartPursuit`, `Police_UpdatePursuits`, `Police_FlushObjectDeleteQueue`;
2. the ambulance call queue (0x505048): a ped request without a crew gets one (`Ambu_AssignVictim`,
   only with the ambulance switch 0x5031cc and while no path search runs); once a crew is assigned
   the entry is dropped; car entries (id >= 0x26c, wrecks) are removed once off screen and not burning
   (`Car_TryRemoveWreck`); the queue is compacted;
3. the police dispatch list: an entry without a car takes the nearest patrol car or a new one from the
   respawn block (`Police_SpawnCarAtTarget`); its controller goes to state 200 (chase) with the
   pursuit, the target's nearest road block as destination, the distance at +0x46, and joins the
   group; done entries are compacted out;
4. the pursuits: a running group with fewer cops than its record wants (not for a criminal on a
   train) gets more, found or spawned the same way;
5. the roadblocks (0x50586c): their timer counts down (back to 400 while one of their cars is on a
   screen); while it runs and a player is wanted (level > 2) they stay; else their unseen cops (state
   0x96, control 3) are released (`Cop_Release`; the scan then restarts at index 1, as the original
   does), the others leave the list, and an empty roadblock is freed;
6. `Police_TickRadioReports`, then (tail call) `Police_UpdateChasers`.

## Cop_Update (0x466f10)

The state machine of a police controller (+0x1b; returns 1 when `Sentinel_DriveCar` should drive on):

| State | What it does |
|---|---|
| 1 | patrol: siren off, `Cop_JoinNearestPursuit`, door closed |
| 2 | head for the station: released off screen, else a path to the respawn block (`Cop_PathToTarget`), then 3 |
| 3 | released off screen, else 0xfe |
| 4 | back to the patrol route: on screen a path to its start (then 6), off screen a warp to the first node (then 1); not a patrol car: 2 |
| 5 | like 2 with sub state 3 |
| 6 | driving to the route start; off screen a warp, then 1 |
| 10 / 0xb | lights overridden near the destination; when the turn ends 4 (patrol car) or 2 |
| 0x32, 0x33, 199 | idle (199: picked for a pursuit, waiting for the dispatch) |
| 0x6e / 0x6f | the crew gets out (`Ped_DriverLeaveCar`), placed by the door, then the sub state |
| 0x96 | a cop at a roadblock: weapon (2 at wanted level 4), watches the criminal within 5 blocks, then leaves the roadblock, replaces the group's farthest cop, 0xf8 |
| 0xbe / 0xbf | a cop on foot walks back and gets in (at once when unseen), then the sub state |
| 200 | dispatched: siren; chase directly (0xcb) when close or the group has a lead, else `Path_Find` mode 3 (0xc9 pending, 0xca found, 0xff failed) |
| 0xc9 | waiting for the search |
| 0xca | following the path; within 15 blocks 0xcb |
| 0xcb | chasing: lead election, roadblocks ahead (the lead only), within 5 blocks 0xd1, a warp to a road off screen |
| 0xd0 | the cop back in the car, then 4 |
| 0xd1 | close: both stopped on the same layer: out (0x6e, sub 0xd3); within 3 and the target slow: 0xd2; farther than 4: 0xcb |
| 0xd2 | alongside: stop and get out (0x6e, sub 0xd3) or chase on |
| 0xd3 | on foot: the target in a car 0xf8 (objective 0x37); on foot 0xe6 (no shooting) or 0xf8 (shooting) |
| 0xd4 | running to the target car's door (a bike is stopped); at the door 0xd5 |
| 0xd5 | pulling him out; then the camera on the ped, criminal +0xe = 4, 0xda |
| 0xd6..0xd8 | a criminal on foot gives up; the cop walks him to the car, 0xda |
| 0xd9 | the cop back in, `Pursuit_RecallCops` |
| 0xda | the arrest: single player `Player_Busted(0)`, wanted level and points cleared, the chasers updated, the group recalled; network games: the criminal dies |
| 0xe6 | on foot after a criminal on foot (shooting if he is armed) |
| 0xf0 | then 0xfc, which has no case: the next step is the fatal default (kept) |
| 0xf8 | on foot at the criminal: to his door (0xd4) or shoot; criminal +0xe 3 / 5 recalls the group, 4 sends the cop back (0xd0) |
| 0xf9 | the car stopped dead |
| 0xfe | going home: leaves group and roadblock, waits for the cop on foot, then repair + `Cop_ResetToPatrol` or released |
| 0xff | no path: a warp to the next route node, then 1 |
| other | fatal -0xf5 |

Criminal +0xe takes the values -1 none, 0 pending, 1 chased, 3 cops recalled, 4 arrested, 5 his car
wrecked, 6 shoot on sight.

### Roadblocks

They live in the junction override records (`JunctionOvr`, 0x505f00): a junction's traffic-light
object angle (+0xc) names the CMP roadblock vertex set (`g_roadblock_sets`); +0xe is 1 while a
roadblock stands, +0x10 its timer (400 frames), +0x12 its cars (36 shorts), +0x5a their count; the list
0x50586c (at most 0x14 used) holds the junctions with one. `Roadblock_TryPlaceAhead` (wanted level 3
and more, the lead cop only) scans ahead of the target car for a road and a set; `Roadblock_Spawn`
puts police cars with their cops and barrier objects across the road. A roadblock cop's controller has
+0x5e = the junction.

## Sentinel_DriveCar and the steering

A sentinel (`Sentinel`, 0x98 bytes) drives the car whose +0xd8 names it; `Cars_UpdateAll` calls
`Sentinel_DriveCar` 0x41aed0 for control modes 2, 9, 10, and 3 while the car is owned. Kinds (+0x02):
1 ambulance, 2 police, 5 route follower, 6 fire engine, 9 mission dummy (`dummy.c`). One step:

1. the controller holding the path search (0x4b3094) only continues it (`Sentinel_RecallRoute`);
2. state 0xff (dismissed): off screen and not burning the record goes (an ambulance `Ambu_Remove`, a
   police car state 0xfe, a fire engine `FireEngine_Remove`), else the car brakes to a stop;
3. the look-ahead (0x20 pixels ambulances, 0x24 police, the car's half length otherwise) gives the
   block ahead; a police car of a pursuit measures its distance to the criminal (+0x46);
4. on a route (+0x4a = 0): the current node (path slot +0x44, node number in the car's +0x119) into
   +0xc..+0xe, the distance to it (+0x14, its minimum +0x16) and to the destination +0x64;
5. the kind's handler (`Ambu_Update`, `Cop_Update`, `FireEngine_Update`, `Dummy_Update`): 0 ends the
   step;
6. off a route (+0x4a > 0, a chase): `Sentinel_Steer` 0x41f290 turns into side roads toward the
   criminal, lines up beside him and boxes him in, or follows the lane braking for what is ahead
   (`Sentinel_CheckAhead`, `Sentinel_CheckObstacle`, `Sentinel_IsLaneClear`); a pursuer far from its
   destination warps after the criminal while unseen;
7. on a route: the node reached advances; the turns toward the next node with lane and stopped-car
   checks, overtaking (`Sentinel_FindOvertakeLane`, `Map_ScanLaneLength`), the stuck recovery
   (`Sentinel_HandleStuck`, `Car_ReverseLane`), warps back onto the road (`Sentinel_WarpCar`,
   `Sentinel_WarpToNearestRoad`), traffic cleared ahead of dummies, the speed.

Emergency vehicles near a junction hold its lights (`Sentinel_OverrideLights`: the junction override
record, timer 0x3c, the saved mode). Unjamming two cars swaps their places (`Car_SwapPositions`).

Quirks kept (commented in `sentinel.c`): whether a next route node exists is read from the path slot
of the controller's own number, the node itself from +0x44; the junction block comparison of
`Sentinel_OverrideLights` is on signed bytes (blocks past 127 never match); a removal from the call
queue skips the entry swapped into the hole; in `Sentinel_Steer` the "lined up" test for heading +y
(`ry > 8 && ry < -8`) never holds, and the four heading cases differ in small ways (which field is
tested, braking, the turn sign); with chase mode 1 the target is in pixels but compared with the car's
16.16 position; the probes query collisions one block behind the block they test.

## Ambulances

Peds that die (`Ped_Animate`) are queued with `Ambulance_RequestForPed`: the request table 0x50cab0
(10 bytes per ped, then per car + 0x26c for wrecks: block on the nearest road, state, crew) and the
call queue 0x505048. `Emergency_UpdateAll` gives each request a crew (`Ambu_AssignVictim`): the first
crew already out (at most nine victims) whose last victim is within 10 blocks, else a new one
(`Ambu_Dispatch`, at most 10 crews: model 5 on the road at the hospital nearest the victim, control 2,
top speed 15, the driver dressed by `Ped_GetCopLook`). The crew's states (`Ambu_Update`):

| State | |
|---|---|
| 200 | wait for the path search, then plan the route to the victim (1) |
| 1 | driving; at the victim's block (or within 5 and stopped) with no route left: stop (0x50) |
| 0x50 | the rear door opens (0x5a); nobody near a screen: 0x6c |
| 0x5a | the medic gets out at the rear wheels (`Ped_CreateSpecial`) |
| 0x5b, 0x5c | he walks behind the car, then to the victim (100) |
| 100 | he follows the victim until touching it (0x66) or arriving (0x65) |
| 0x65 | 75 frames later the victim is revived: health 100, walking (state 2, objective 0x19) |
| 0x66, 0x67, 0x6a, 0x6b | the medic walks back behind the car and into it |
| 0x6c, 0x6d | the medic gone, a still dead victim's body removed, the rear door closes; then 2 |
| 2 | the next victim (0x50 within 6 blocks, else 200); none: back to the hospital (3) |
| 3, 4 | `Ambu_SendToHospital` (path mode 3, then 2); at the hospital the crew is removed |
| 0xfe | told to leave: once the medic is off screen he and the record go (the car stays) |

A stolen ambulance makes the crew leave (0xfe); a victim that recovers, isn't lying for the medic or
has been dead over 950 frames ends the call; a dead medic sends the crew back to the hospital.

Quirk kept: a new crew records itself only in its own victim list, not in the request (+8 stays -1).

## The helicopter

`Heli_Spawn` (the CHOPPER command; one at a time, a second is fatal -0xf8) puts it at a point with its
landing point; its shadow is an object of type 0x5c 17 pixels ahead, a block below; the sprite is the
car info sprite of model 88; it is entity kind 0x1e in the grid. `Heli_Update`: turn to the landing
point (4 a frame), fly (speed to 15), slow over the block, descend 2 pixels a frame to 66 pixels above
the ground, wait 40 frames, take the player (his ped leaves the grid, the camera follows the heli:
target kind 5), climb to z = 2, fly to the exit point (CHOPPER_ENDPOINT, speed to 39), freeze the
camera there (kind 4) and end the level 30 frames later (`Event_ScheduleExit(30, 1)`).

Quirk kept: `Heli_TurnTowards` masks the heading only when it turns again, so a heading of -4..-1 or
0x400..0x403 is used once; below the sine table lies the tan table, which the port reads in that case.

## The gang and the hunters

A mission's gang cars (`Gang_AddCar`, ten) hunt a player who rides one of them (a bike, model 3) for 30
frames (`Hunt_AddCarTarget` mode 6). While he is on a bike `Gang_Update` (from the mission's trigger
update) also gives every gang car an extra `Hunt_UpdateCar` step: the gang drives twice per frame.

The hunt table (`Hunt`, 0x1c bytes at 0x5132a0, 20 entries) drives cars with control 0x32
(`Hunt_UpdateCar` from `Cars_UpdateAll`): to a block (modes 4 / 10) or after a ped (other modes, its
car's block when it drives). A hunter chasing a car accelerates (bikes to the top speed, model 0x2f 3 a
frame, others to 10 above the target's speed), otherwise it slows to 7; off the road and unseen it
warps onto the nearest road; within 2 blocks it turns toward the target; else at each junction it
takes the exit (left, right, ahead) whose direction is nearest the target's. A car that hasn't moved
since the last step stops unless it can push the car in its way off screen.

## How the chase plays out

`Police_UpdatePursuits` makes the nearest cop the group's lead every frame, so dispatched cops go
straight to the chase (0xcb) and are steered greedily toward the criminal (`Sentinel_Steer`) rather
than along a `Path_Find` route. On the one-way grid around a criminal who stands still they can
circle for a long time (mission 1's start block: within about 8 blocks, never closer). What brings
them is the criminal moving: once he is 12 blocks from where a cop was sent, the unseen cop is warped
there (`Sentinel_WarpCar`) and sent on. `tests/police_test.c` therefore moves the player between two
blocks 13 apart; at wanted level 2 (no shooting) a cop then gets out and arrests him.

The helicopter takes the player by pointing his camera at it (kind 5). A player on foot whose ped is
walking (state 2, action 2) has `Ped_UpdateAll` copy the camera target into the controlled kind
(`Player_ControlViewTarget`), and kind 5 is fatal in `Player_UpdateAll` (-0x4a); the original does
the same, so in the shipped missions the player is presumably not in that state when the heli lands
(open question). The test boards it from a car.

## Port deviations

- The original reads outside its tables in a few places (criminal -1, ped -1, a cache layer below 0,
  the player record of a ped that is no player); the port guards them (documented at each place).
- `Car_SteerTowards` 0x40bc70 is car.c's, `Area_GetSample` 0x44b7b0 the area module's
  (`src/hud/area.c`), `Player_IncKills` / `Player_SetViewFixed4` / `Player_AwardBonus` player.c's,
  `Ref_GetKind1PosRect` 0x45fb60 ped.c's. The fire engines (kind 6) are fire.c's: see
  [Objects](/objects#fire-engines-0x42e870-0x430400).
- The dispatch list stores car / ped ids where the original stores record pointers.
