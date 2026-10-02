# Missions: the script runtime

How the original runs the command lines of a MISSION.INI section (loaded by `Mission_Load`, see
[game-core.md](game-core.md)), and how the port mirrors it. Addresses are virtual addresses in `gta.exe`.

## Files

| File | What |
|---|---|
| `src/game/mission_run.c/h` | `Mission_Update` 0x446fe0, the interpreter `Mission_StepProcess` 0x4471a0 with the opcodes it handles inline, the process and script object helpers 0x43cca0-0x43d2b0 and 0x43e280-0x43e870, `Mission_OnBriefDone` 0x445580, `Mission_GetResultText` 0x445670, the counters the frontend's results read |
| `src/game/mission_ops.c/h` | the opcodes that have their own function: `MissionOp_*` 0x43e980-0x4453c0 |
| `src/game/trigger.c/h` | the runtime objects 0x473b90-0x4756ff (triggers, doors, cranes, timed bombs, sound sources, ranking, car triggers) and 0x479020-0x47bb10 (kill scoring, respawn, `Mission_InitCityTables`, `Mission_UpdateTriggers`, `Door_Update`, `Trigger_Update`, the respray cost) |
| `src/game/mission_obj.c/h` | the helpers 0x475700-0x479020: mission cars, block clearing, the PED_ON presets and AI changes, briefs, alarm sound slots, the car list |
| `src/game/dummy.c/h` | the DUMMY cars 0x473440-0x473b8f (state only) |

## The process model

There are 32 processes (script threads). Per process (`Mission`, mission.h): active flag 0x6761c0, pc
0x676620 (a command index), wait counter 0x676280, step 0x6560b8 (1 on entry to a command; the waiting
commands count their timers in it), linked trigger 0x6b3de8 (disarmed when the process stops), owner
0x676348, kind 0x676200 (0 normal, 1 KEEP_THIS_PROC, 2 KF_PROCESS: RESET spares those), the trigger that
started it 0x6762c8 and the result it ended with 0x6b3b30.

- `Mission_Load` starts one process per player at command 0 (owner -1); processes 0-3 belong to the
  players, the rest are free.
- Triggers and `KICKSTART` / `NEXT_KICK` start new ones (`Mission_StartProcess` 0x43cca0: the first free
  from 4, step 1, the pc of a label, owned by a player). The owner chain is followed to its root to find
  the player a process works for (0x676608, "current player") and its ped (0x5f30e0).
- `Mission_Update` (each game frame, from `Game_Update`): `Mission_UpdateTriggers`, then every active
  process gets one `Mission_StepProcess` in index order (0x6b3b70 is the running one), then per player:
  when the player is dead (and not in a wrecked car / the model car), a countdown 0x5fdffc runs:
  `Player_Wasted` (no life left: the player's process stops with result 3) and 0x57 frames later on foot
  (0x64 in a car) `Player_Respawn`. When none of the four players' processes is active any more the
  level end is scheduled once (0x6b3b7e): 1 frame with result 1 for a success of the local player, else
  0x5a frames with its result.
- `Mission_StepProcess`: loads the process's pc and wait counter, dispatches on the command's opcode
  (anything past 0x95 is fatal -0xb6); the handler leaves the next pc in 0x655e58. A reached
  TARGET_SCORE ends every process with result 8. The pc and wait counter are written back to the
  running process (0x6b3b70: END moves it up the owner chain, so its pc goes to the root's slot).

### Branches and score

A command is `{op, a, b, c, d, e}`: a is usually an object line, b the success label, c the fail
label, d a parameter, e a score. Branching (`Mission_BranchSuccess` 0x43e310 / `Mission_BranchFail`
0x43e3b0) through b or c: 0 is the next command, -1 kills the process (`Mission_KillProcess`: disarm
its trigger, clear everything), any other value a label; a negative value also clears the active flag;
the step goes back to 1. Most commands add e (if positive) to the owner player's score with
`Player_AddScore(n, e, 100, 100, 100, 0)` before branching (`Mission_AwardScore` 0x43e450). A command
that does neither stays: the process retries it next frame.

### Script objects at run time

Commands address entities through the line's script object: the handle (a car, ped, object, trigger,
door or crane id, or a counter's value), the parameter, the coordinates. `Mission_GetObjectPos`
0x43e680 gives pixel coordinates: cars, peds and objects where their sprite is, the block-placed kinds
(triggers, doors, DUMMY, FUTURECAR, BLOCK_INFO...) their block's corner (block * 64), TARGET its
coordinates and parameter, the rest the stored values. `Mission_GetObjectHealth` 0x43e870: 100 -
damage of a car (0 for a free slot), a ped's health (0 if carried or in states 5, 0x17, 0x18), else 100.
Type 100 marks a process list (KICKSTART), 0x65 a removed object.

### RESET

`RESET` / `RESET_WITH_BRIEFS` stop every other process from 3 (but kinds 1 and 2 and processes waiting in
an MPHONE), clean up the lists the loader built of non-persistent cars, peds and objects (off-screen
cars and objects are deleted; peds nobody saw recently are removed, the others wander off), make every
non-persistent trigger off (state 4) and close and lock the non-persistent doors, and switch the arrow
and the sound sources off. The car and object list compaction keeps a quirk: an entry already in place
moves past itself, so the lists drift up by one slot per RESET.

## Opcodes

Inline in `Mission_StepProcess` unless an address is given. "award" = award score then branch b;
"fail" = branch c.

| # | Name | Semantics |
|---|---|---|
| 0x04 | SURVIVE | d frames (counted down in the command's own c field, a quirk), then award |
| 0x05 | HUNTON | the car of line a hunts the car of line d (mode c), award |
| 0x06 | HUNTOFF | stops the hunt of line a, award |
| 0x07 | DRIVEON | no handler: the process stays on it |
| 0x08 | DUMMYON | the car of line a becomes a traffic dummy with its driver sitting in it, award |
| 0x0a | END | the process and every owner up the chain stop with result a |
| 0x0c | EXPLODE | an explosion at the block of line a, direction d, scored to the player; award |
| 0x0d | OBTAIN | an object (type = line a's handle, angle d) carried by the player's ped (+0x18 = 4), award |
| 0x0e | THROW | throws the object of line a at the car of line d, award |
| 0x0f | BRIEF | subtitle brief e for d frames (c >= 0: c into 0x693b20, else -c into 0x693b24), branch b |
| 0x11 | DONOWT | no handler: waits forever |
| 0x12 | DISABLE | the trigger of line a off (state 4), unlinks this process's trigger, award |
| 0x13 | ENABLE | re-arms the object of line a (`Mission_EnableObject` 0x43e4f0) and, for non-doors, disarms its trigger (state 1), award |
| 0x14 | DECCOUNT | counter of line a - 1: above 0 fail, else award |
| 0x18 | SETBOMB | car of line a gets bomb kind d, award |
| 0x19 | ESCORT | the car of line a follows the DUMMY of line d (its script line = a), award |
| 0x1a | ARROW | the HUD arrow to line a (a block corner points at its centre); a = -1 switches it off; award |
| 0x1b / 0x1c | LOCK_DOOR / UNLOCK_DOOR | the car of line a's doors held (+0x248), award |
| 0x1d | EXPL_LAST | wrecks the car the player's ped is in or was last in (not on a train), award |
| 0x1e | DROP_ON | object (type = handle) at the block centre of line a, angle d, award |
| 0x20 | KILL_DROP | deletes the object of line a (none: fail), award |
| 0x22 / 0x23 | ARMEDMESS / DISARMMESS | the zone text `bomb_on` / `bomb_off`, award |
| 0x24 | ARROW_OFF | award |
| 0x25 | P_BRIEF | pager brief (kind 0) like BRIEF, branch b |
| 0x27 | WAIT_FOR_PED | until the ped of line d is on the block of line a, award |
| 0x28 | PED_BACK | the ped of line d walks back to the car of line a (dead: fail), award |
| 0x29 | SETUP_REPO | line a's parameter = d, branch b |
| 0x2b | KILL_OBJ | removes the object of line a, award |
| 0x2c | DO_GTA | the car list of line a: step 1 records this pc and arms the crane slot, step 2 waits until the list is complete, branch b |
| 0x2d | MISSION_END | score e, multiplier + 1, branch b |
| 0x2e | EXPLODE_CAR | wrecks the car of line a, award |
| 0x32 | KILL_CAR | removes the car of line a if nobody is in it (clears an ambulance request and the cleanup entry), award |
| 0x33 / 0x34 | ARROWPED / ARROWCAR | the arrow to an object / car, award |
| 0x35 / 0x36 | REMAP_PED / REMAP_CAR | new remap d (no entity: fail), award |
| 0x37 | ARE_BOTH_ONSCREEN | both cars of lines a and d on screen: award, else fail |
| 0x38 | THROW_TO_POINT | the object of line a thrown at line d, award |
| 0x3c | STARTUP | once per level: voice event 0x14 in 10 frames, the trigger of line a to state 3, branch b |
| 0x3d | CHECK_PEDBACK | until the ped of line a is in state 6 (dead: fail), award |
| 0x3e | START_HIRED_ESC | the ped of line a hunts the block of line d (full: retry), award |
| 0x3f | DUMMY_OFF | car +0xc0 = 1 (no car: fail), award |
| 0x40 | POWERUP_ON | a power-up (type = handle, value = param) at line a, branch b |
| 0x41 | PLAIN_EXPL_BUILDING | the explosion with two fires, award |
| 0x42 | CHANGE_BLOCK | the block of line a gets the BLOCK_INFO's packed info, award |
| 0x43 | CHANGE_TYPE | a block face (handle) gets tile (param), award |
| 0x44 | P_BRIEF_TIMED | pager brief with countdown d frames, branch b |
| 0x46 / 0x47 | DOOR_ON / DOOR_OFF | until the door can be unlocked / locked, award |
| 0x48 | WRECK_CURR_TRAIN | crashes the train the player rides (none: fail), award |
| 0x49 / 0x4a | OPEN_DOOR / CLOSE_DOOR | (no door: fail), award |
| 0x4c | BANK_ROBBERY | +1000 wanted points, wanted level 4, a crime report (9) at line a, award |
| 0x4d | DELAY_CRIME | a timed bomb on the car / created ped of line a (who c, frames d), award |
| 0x50 / 0x51 | BANK_ALARM_ON / OFF | a positional alarm source at line d (slot in its param) / off, branch b / award |
| 0x52 | GARAGE_SEND | the ped of line a hunts the block of line d (retry when full), award |
| 0x54 | PLAYER_ARE_BOTH_ONSCREEN | the player (car on screen or ped seen) and the car of line a: award, else fail |
| 0x55 | CHECK_CAR | the player drives the car of line a (and its remap is d, if d >= 0): award, else fail |
| 0x56 | WAIT_FOR_PLAYER | waits while the player is near line a (d blocks); quirk: the test is `x >= bx-r && (x <= bx+r \|\| y >= by-r) && y <= by+r` |
| 0x57 | EXPL_PED | explodes the ped of line a (in a car: wrecks it), award |
| 0x58 | PARKED_ON | creates the car of line a (model = handle, heading = param, remap d) on its block, clears the block, award; no slot: fail |
| 0x59 | KICKSTART | starts a process at label a (0: next command, same kind); a process-list object (type 100, the last free record) holds it; the command's d / e get that object and this process; branch b |
| 0x5a | IS_PED_IN_CAR | the ped of line a in a car (of model d if d >= 0): award, else fail |
| 0x5b | CANCEL_BRIEFING | the alarm loop off and the pager countdown of the brief at label a removed, award |
| 0x5c / 0x5d / 0x5e | FREEZE_TIMED / FREEZE_ENTER / UNFREEZE_ENTER | player +0x18c = a / +0x189 = 1 / 0, award |
| 0x5f | EXPL_NO_FIRE | the explosion only, award |
| 0x60 | KILL_PROCESS | stops every process in the list of the KICKSTART at label a and clears its e, award |
| 0x61 | NEXT_KICK | starts a process at label a into the list of the KICKSTART at label d, branch b |
| 0x62 | KILL_SIDE_PROC | like KILL_PROCESS, keeping e |
| 0x63 | SET_KILLTRIG | the trigger of line a gets field e - 1 of the list of the KICKSTART at label d, branch b |
| 0x64 | POWERUP_OFF | award |
| 0x65 | KILL_SPEC_PROC | stops list field d (1..4) of the KICKSTART at label a (else clears its e), award |
| 0x66 | SCORE_CHECK | score >= a: flag +1 of the player, award; else fail |
| 0x67 / 0x68 | FRENZY_SET / FRENZY_CHECK | stores the score in the FRENZY_CHECK at label a / a points scored since: voice 4, award, else fail |
| 0x69 | IS_GOAL_DEAD | line a dead (and, for d > 0 and a created ped, killed by the ped of line d): award, else fail |
| 0x6a | GENERAL_ONSCREEN | line a in the player's view rectangle: award, else fail |
| 0x6b | GET_CAR_INFO | the car the ped of line a is in becomes the CAR of line d (in a car: fail otherwise); quirk: it's added to the cleanup list only when the object is persistent |
| 0x6c | START_CHOPPER | the helicopter from line a to line d, branch b |
| 0x6d | LOCATE | the player on foot within e pixels of line a: branch b; with d > 0 the step counts d frames down to fail |
| 0x6e / 0x6f | SPEECH_BRIEF / MOBILE_BRIEF | briefs of kinds 4 / 3 with text e, branch b |
| 0x70 | PIXEL_CAR_ON | like PARKED_ON at pixel coordinates, with its driver sitting in it (models 0x29 and 3 get a visible driver, 0x2f bomb kind 3) |
| 0x71 / 0x83 | SET_NO_COLLIDE / SET_COLLIDE | car +0x128 = 99 / 1, award |
| 0x72 | PED_WEAPON | ped weapon d, award |
| 0x73 | FREEUP_CAR | the car is no longer a script car (+0x134 = -1, +0x139 = 1), award |
| 0x74 | MESSAGE_BRIEF | the big message with the FXT key "e" (the number as text), branch b |
| 0x75 | PARKED_PIXELS_ON | like PIXEL_CAR_ON without the driver; no slot: retry |
| 0x76 | PED_POLICE | the ped of line a becomes a cop, award |
| 0x77 | DROP_WANTED_LEVEL | not arrested and +0xfc = 0: wanted points and level cleared, award |
| 0x78 | IS_PED_ARRESTED | ped in state 9: award, else fail |
| 0x79 | HELL_ON | a gang car (model 3) with a driver (remap 0x21) at line a, award; no slot: fail |
| 0x7a / 0x7b | XXXX | no handler |
| 0x7c | SET_PED_SPEED | ped byte +8 = d, award |
| 0x7d | IS_PLAYER_ON_TRAIN | the train into line a's handle (not on a train: fail), award |
| 0x7e | WRECK_A_TRAIN | crashes the train of line a, award |
| 0x7f | INCCOUNT | counter of line a + 1: reaching d awards, else (or d < 0) fail |
| 0x80 | COMPARE | line a's handle == d: award, else fail |
| 0x81 | RESET | see above, award |
| 0x82 | KEEP_THIS_PROC | kind 1, award |
| 0x84 | DEAD_ARRESTED | the ped of line a arrested or dead: award, else fail |
| 0x86 | STOP_FRENZY | the frenzy weapon ends, award |
| 0x8d | KF_PROCESS | the kill frenzy cars with a script line are held, kind 2, award |
| 0x91 | RESET_WITH_BRIEFS | RESET, and the subtitle and pager cleared (the alarm slots stay), award |

### Opcodes with their own handler (mission_ops.c)

"Timed" waits: on entry the step becomes d + 5 (0 when d < 1: wait forever); each frame without success
counts it down, and reaching 5 takes the fail branch.

| # | Name | Handler | Semantics |
|---|---|---|---|
| 0x00 | LOCATE_STOPPED | 0x43e980 | the player's ped within e pixels of line a, standing, not in a car: branch b; timed |
| 0x01 | DESTROY | 0x43ed90 | line a's health 0: award; timed |
| 0x02 | ANSWER | 0x43ef80 | phone a rings (state 2, alarm) for d frames, toggling every 50; walking up answers (b, or c when e = 0), ringing out fails. Quirk: e is only tested on the toggle frames |
| 0x03 | STEAL | 0x43f360 | the player drives car a: voice 0x11, award; timed |
| 0x09 | SENDTO | 0x43f760 | DUMMY car a drives to line d (its controller into a's param), when the shared path search is free |
| 0x0b | MAKEOBJ | 0x43f950 | creates object a (type = handle, angle d); type 0x58 gets +0x12 = 1 |
| 0x10 | MPHONE | 0x43fb20 | c phones on lines a.. ring within 15 blocks (0x7710f4 set); answering phone i kills the other processes, clears the mission and branches to b + i, sets the phone flag and schedules event 0 with player + 0x32 in 250 frames |
| 0x15 | GOTO | 0x43eb50 | what the player controls is on line a's block: award; timed |
| 0x16 | CRANE | 0x440040 | crane a takes the player's car at line d's block; refusals (police car, too long, bomb, no room...) show crane0..5 and fail; delivered: a car-list delivery, or value x (100 - damage) / 100 / (same models + 1), at least 1000, award |
| 0x17 | PARK | 0x4407c0 | park into the garage door a from direction d: the car is taken over and driven in, the driver leaves at the exit point, the door closes, the car is deleted by event (car + 200, 10 frames) |
| 0x1f | PED_ON | 0x441600 | creates the FUTUREPED of line a with its sub-type preset (target = line e, remap d), branch b; nothing created: fail. Quirk: sub-types 0xd and 0x1b-0x28 create nothing |
| 0x21 | CAR_ON | 0x441cd0 | creates the FUTURECAR of line a with its driver seated (models 0x29 / 3 a visible one, 0x2f bomb 3), clears the block; no slot: fail |
| 0x26 | PED_SENDTO | 0x441fd0 | ped d walks to line a |
| 0x2a | DO_REPO | 0x4421a0 | waits for the player to drive car a; after d frames its owner comes back |
| 0x2f / 0x30 / 0x31 | START_MODEL / DO_MODEL / RETURN_CONTROL | 0x4423f0 / 0x4427c0 / 0x442ca0 | the RC car: the player's car is swapped for model 0x2f (the real one kept in a type-100 object, e models); run until the player dies, the model is wrecked (next model or fail) or target d dies (award); the real car back |
| 0x39 | GOTO_DROPOFF | 0x442fc0 | every 8th frame d counts down (0: fail); the ped near line a: award |
| 0x3a | MODEL_HUNT | 0x4431d0 | d parked cars on lines a.. hunt the player; the process stays pruning wrecked hunters, a new one continues |
| 0x3b | MODEL_FUTURE | 0x443440 | a parked model car at line a (quirk: pixel coordinates to a block spawner) |
| 0x45 | CHANGE_PED_TYPE | 0x443690 | live ped a gets AI preset d (target line e), branch b |
| 0x4b | PED_OUT_OF_CAR | 0x443a90 | the driver of car a (or created ped a) leaves its car |
| 0x4e | GET_DRIVER_INFO | 0x443f20 | line d becomes car a's driver (a created PED, +0x8b = 1) |
| 0x4f | KILL_PED | 0x4445d0 | removes created ped a, cancels its ambulance, unlists it |
| 0x53 | DUMMY_DRIVE_ON | 0x43f580 | once car a's driver sits in it, it drives off at speed 5 |
| 0x85 | IS_POWERUP_DONE | 0x4448a0 | the power-up at line a taken: award; still there: fail (now, or when timer d runs out) |
| 0x87 | FRENZY_BRIEF | 0x444180 | brief kind 5 (e), branch b |
| 0x88 | ADD_A_LIFE | 0x4442b0 | Player_AddLife |
| 0x89 / 0x8a | KF_BRIEF_TIMED / KF_CANCEL_BRIEFING | 0x4443d0 / 0x4444b0 | player timer +0x1ae = d x 25, voice 3 / off |
| 0x8b / 0x8c | KF_BRIEF_GENERAL / KF_CANCEL_GENERAL | 0x444b60 / 0x444c40 | timer +0x1b0 = d x 25 / off |
| 0x8e | RESET_KF | 0x444d60 | releases the kill frenzy cars, kills the other kind-2 processes |
| 0x8f | WAIT_FOR_PLAYERS | 0x444f30 | `Mission_AllPlayersBelow(handle a)`: b, else c |
| 0x90 / 0x92 | RED_ARROW / RED_ARROW_OFF | 0x441320 / 0x4414e0 | the local player's red arrow (quirk: a = -1 turns the normal arrow off) |
| 0x93 | IS_A_TRAIN_WRECKED | 0x4450f0 | any train wrecked: b, else c |
| 0x94 | INC_HEADS | 0x445290 | `Player_SetStatByCity(player, a)` |
| 0x95 | IS_PED_STUNNED | 0x4453c0 | ped a in anim 0x2b: b, else c |

## Triggers (trigger.c)

A trigger (0x771628, 210 x 0x20): state +0 (0 fire, 1 armed, 2 delayed, 3 running, 4 off), kind +4,
a +8 (a label, door, colour or car), d +0xc (the watched car / ped or a linked trigger), block x, y, z
+0x10 (bytes), radius b +0x14 (blocks), c +0x18 (arming delay or a frame counter), the process to kill
when it fires +0x1c, persistent +0x1d. `Trigger_Create` 0x4744a0 moves c into d for kinds 10, 0xb, 0xe,
0xf, 0x10, 0x13, 0x15, 0x18, 0x19, 0x1b-0x1e, 0x21.

Each frame (`Trigger_Update` 0x47a3a0, from `Mission_UpdateTriggers` 0x479e00) per player not on a
train: the position of the player's car or ped (or the watched one) is "in" when within ±b blocks in x
and y (z is never tested). Armed and in: fire, or delayed when c > 0 (event 4 resets it to fire after c
frames). Firing runs the kind:

| Kind | Object type | Action |
|---|---|---|
| 0 / 1 / 2 | door triggers | open / close / toggle door a |
| 3 | SPRAY | respray (in a car): emergency cars and the model car refused; a script car gets colour a, a wanted car is cleaned for `Garage_ResprayCost`, repaired, wanted level and points cleared |
| 4 | TRIGGER | starts a process at label a |
| 8 | BOMBSHOP | fits a bomb (kind 1) for BOMBSHOP_COST; model car, tank and emergency cars (but squad cars 4 / 0x20) refused |
| 9 | CARTRIGGER | a car with script line b anywhere: process, state 3 |
| 10 | ONETRIGGER | toggles door a when the player is in car d in the area |
| 0xb | MOVING_TRIG | the player within b of car a: car a hunts the player (mode d) |
| 0xc | MPHONES | after one arming update: a process when no phone rings (0x7710f4) and the player's phone flag is clear |
| 0xd | PHONE_TOGG | leaving the ±3 square (within b) re-arms running trigger a |
| 0xe | DUM_MISSION_TRIG | car d (not a player's) in the area, control != 1: process |
| 0xf / 0x10 | CORRECT_MOD_TRIG / CORRECT_CAR_TRIG | the player's car model / id is d: process, state 3 |
| 0x11 / 0x12 | CLOCK_START / CLOCK_STOP | arms its CLOCK_STOP / kills trigger a and itself |
| 0x13 / 0x14 | MIDPOINT_MULTI / FINAL_MULTI | race checkpoints (the next one armed, texts `1st_over_checkpoint`, `second_checkpoint`) / the finish (`win_race`, `lost_race`) |
| 0x15 | DUM_PED_BLOCK_TRIG | the watched ped (not a player) in the area: process |
| 0x16 | MOVING_TRIG_HIRED | car a drives to the trigger's block |
| 0x17 | | takes a fitted bomb off (`bomb_off`), off |
| 0x18 | CARBOMB_TRIG | car d has a bomb: process, off |
| 0x19 / 0x21 | DAMAGE_TRIG / ALT_DAMAGE_TRIG | remembers the first car in the area; when it's wrecked: process, off (0x21 keeps the +0x1c process) |
| 0x1a / 0x1b | GUN_TRIG / GUN_SCREEN_TRIG | on foot, armed, firing in the area / with ped d on screen: process |
| 0x1c | CARDESTROY_TRIG | car d wrecked in the area: process |
| 0x1d | CARWAIT_TRIG | car d stopped on screen 65 frames (400 in some cases): process |
| 0x1e | PEDCAR_TRIG | ped d in car a: process at label b, off |
| 0x1f | CANNON_START | voice 0xe, off |
| 0x20 | CARSTUCK_TRIG | car d stopped on an air block and on screen > 64 frames: process |
| 5, 6, 7 | | nothing |

Respray cost (`Garage_ResprayCost` 0x47ba30): ((W + value x 1000 x damage / 100) x (100 + multiplier))
/ 100 with W = 0 unwanted, else 1000 x 2^(level - 1), value = car info +0x6e; 500 when both are 0.

Doors (0x773220, 64 x 0x28): animation slot +0, state +4 (0 open, 1 closed, 2 opening, 3 closing),
locked +8, range +0xc, orientation +0x10, block +0x14, tile +0x18, frames +0x1c, condition +0x20 (0 any
car, 1 emergency car / 0x2c / tank, 2 a car id and remap, 3 the viewed car's model, 5 a bomb car), car /
model / line +0x22, remap +0x24, face slot +0x26, persistent +0x27. `Door_Update` 0x479f00 opens a door
when a player is within range in front of it and the condition holds. Cranes (0x773130, 4 x 0x3c): state
+0, object +4, position +8, direction +0x14, the car moved +0x18, stacked count +0x1c, stack[6] +0x20,
car-list slot +0x38. Timed bombs (0x7715d8, 4 x 0x10): id, kind (0 car, 1 ped), who, state, frames.

Quirks kept: `Door_OnClosed` / `Door_OnOpened` are swapped by name; `Door_SetOpenAny` marks a door locked
but counts it unlocked; `Crane_CheckSpace` reads crane 0's stack; the race ranking's distance uses the
trigger x in the y term; `Player_ChooseRespawnPoint`'s second sort writes the wrong array.

## Helpers (mission_obj.c) and DUMMY cars (dummy.c)

PED_ON presets (`Mission_PedCreate_*` 0x4773f0-0x477b50; Ped_Create at z - 2 pixels, graphic 0, +0x8b =
1; "armed" = weapon 1): 0 state 3 action 8; 1 armed, state 3, objective 0x18, target; 4 state 3, speed 1;
5 state 4 action 2 target; 6 state 2 objective 0x24 action 5; 7 state 1 action 5 target; 8 armed state 4
objective 0x1c target; 9 state 3 objective 0x22; 10 state 2 objective 0x23 speed 3; 0xb armed state 4
objective 0x31; 0xc armed state 3 objective 0x30; 0xd state 4 objective 0x39 action 2; the typed codes
0x15-0x1a / 0x29-0x2e armed with objectives 0x18, 0x1a, 0x1b, 0x1c-0x1e, 0x29, 0x2c, 0x2e, 0x2b, 0x2d,
0x2f; the guard state 3 objective 0x38. The AI changes (`Mission_PedSetObj_*`) refuse dead peds and states
0xc, 0x15, 0x17, 0x18; a driving ped leaves its car first.

The car list (GTA_DEMAND, 0x771110: 10 x 16 bytes {line p1, model (-1 any), remap (-1 any), target, ...,
delivered +0xa, the DO_GTA pc +0xc}), the four alarm sound slots 0x773c24, and the DUMMY convoys
(0x771080: 8 groups of a count and four AI controllers). A DUMMY car gets an AI controller of kind 9
(state byte +0x1b): 0xfa start a path search to the destination, 2 wait for it (failed: 0x27), 5 search
again from the nearest road, 1 arrived (the group is released), 100-103 every car of the group hunts
player n. The controllers and path search aren't ported (stubs), so DUMMY cars don't move yet.

## Mission_OnBriefDone 0x445580 (delayed event type 0)

Clears 0x6765e8, then by code: <= 0 a car (-code) whose bomb kind is 3 explodes (if not already
wrecked); 1 ends the game (result 1); 9 and 10 set 0x6765e8; 0x14 voice 0; 0x32-0x35 clear the phone flag
of player code - 0x32; 200-600 delete car code - 200 if it's in use.

## Results

`g_game.result` (0x513230) is set from the local player's process result when the end event fires:
1 success, 2 failed, 3 dead, 4 arrested, 5 time out, 6 time over (multiplayer), 8 target score, 10
cannon, 11 demo. `Mission_GetResultText` appends the FXT text (`m22success`, `m22failed`, `m22dead`,
`m22arrest`, `m22timeout`, `m22timeover` with the best two players, `m22score`, `m22cannon`, `m22demo`,
otherwise `m22incomplete`) and returns the local player's score. The counters: `Mission_GetMissionTotal`
0x43d1d0 (MISSION_TOTAL or 0), `Mission_GetSecretTotal` 0x43d1e0, `Mission_GetCounterRemaining`
0x43d1f0 / `Mission_GetSecretRemaining` 0x43d220 (the counter object's parameter minus its handle),
`Mission_GetTargetScore` 0x43d1c0.

## Port deviations

- Handles of -1 and undefined lines read a record of -1s where the original reads before its tables;
  a process whose pc leaves the command table is fatal.
- `Mission_StartProcess` / KICKSTART copy the owner's ped into 0x656180[process] for every process;
  past the four players that's unused bss in the original; the port derives the ped from the owner.
- With no free process, KICKSTART / NEXT_KICK write the slot before each table in the original; the
  port skips those writes.
