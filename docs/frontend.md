# Frontend

The menus of the original and what WinMain does around a game. Addresses are virtual addresses in
`WINO/Grand Theft Auto.exe`. Drawing primitives (fonts, pictures, menu items) are in
[text-fonts.md](text-fonts.md).

| Port | Original |
|---|---|
| `src/front/front.c` | `WinMain` 0x437230 (the frontend part), `Front_Step` 0x426a50, `Front_SetScreen` 0x427030, `Front_LoadTexts` 0x426320, `Front_LoadSettings` 0x42b4a0 (the file half is `src/savedata.c`), `Front_Enter` / `Front_Leave` 0x42b690 / 0x42b7a0, `Front_BuildChapterList` 0x427630, `Front_ApplyVolumes` 0x4280b0, `Front_AdvanceMission` 0x42b1e0, `Front_SelectMission` 0x4268d0, the getters 0x426990-0x4269c0, `Front_DrawEnterPrompt` 0x426fd0, the screens handled inside `Front_Step` (cutscene, error, CD, credits, SVGA) |
| `src/front/front_screens.c` | `Front_ScreenMain` 0x4277c0, `Front_DrawHighScores` 0x427fc0, `Front_ScreenLoading` 0x428bf0, `Front_ScreenStart` 0x428ee0, `Front_ScreenOptions` 0x429180, `Front_ScreenMultiOptions` 0x429650, `Front_ScreenPlayerSelect` 0x429a20, `Front_DrawPlayerScores` 0x429df0, `Front_DrawPlayerPortrait` 0x429f60, `Front_ScreenRename` 0x42a160, `Front_ScreenResults` 0x42a750, `Front_ScreenResetPlayer` 0x42b260 |
| `src/front/front_net.c` | `Front_ScreenCommsError` 0x4276e0, `Front_ScreenConnectionList` 0x428150, `Front_ScreenSessionList` 0x4282c0, `Front_ScreenEnterName` 0x428480, `Front_ScreenLobbyHost` / `Join` 0x428680 / 0x428950, and stubs for the DirectPlay wrappers they call |

Test: `tests/frontend_test.c` drives the menus with key presses and writes `out/frontend/*.png`.

## The loop

`WinMain` creates the mutex, starts the subsystems and sets the launch switches (`Game_SetOptions`
0x4146d0, 43 arguments; demo 0x5031bc and the "network started" switch 0x502f5c among them). Then:

1. `Front_LoadSettings`: `PLAYER_A.DAT`, the language override, `Front_LoadTexts`, the chapter list,
   `Gfx_ValidateModeIndex` on the saved mode, `Front_ApplyVolumes`.
2. `Player_SetNameColour(0, "Player", 0)` (0x4b0c7c).
3. `Movie_PlayIntro` 0x44b160, then `Front_Enter(4)`.
4. The frontend loop. Each pass reads **one** key event with `Input_GetKey` 0x414a80, turns it into
   input bits, calls `Front_Step(bits)`, then sleeps whatever is left of 35 ms. It ends when
   `Front_Step` returns non-zero; `Front_Leave` follows.
5. Return 4: quit. Otherwise the game runs: code 1 first calls `Net_EndGame` and `Net_InitPlayers`,
   code 3 (joined a network game) reads `MISSION.INI` again, code 2 (host) neither. `Game_Run` is
   repeated while it returns 3 (restart).
6. `Front_Enter` with 0x11 (Game_Run returned 4, comms failure), 0x14 (5, SVGA error), 7 (the start
   menu, when 0x5031f4 is set; it and 0x5031f0 are cleared) or 1 (the cutscene, which turns into the
   results screen when there is none to show). Back to 4.

The port makes the loop a step: `front_frame` is one pass (the app's 35 ms frame, `APP_FRAME_HZ`) and
returns `FrontStep {code, section, level, player}`; `front_game_over(f, result)` is step 6.

`Movie_PlayIntro` is a stub. The original flushes the keys, opens `..\gtadata\movie.smk` with
SMACKW32, switches to the movie mode (`Gfx_SetVideoMode(-2)`), shows every frame doubled to 640 x 480
with the movie's palette, and stops at the last frame or at a key other than Alt (0x38).

### Input bits

Built by WinMain from one event (`Input_GetKey`: scan code, +0x100 for the cursor/keypad codes
0x47-0x53 when the event has no character, +0x80 for a key release):

| Bit | Key |
|---|---|
| 0x01 / 0x02 / 0x04 / 0x08 | Up 0x148 / Down 0x150 / Left 0x14b / Right 0x14d |
| 0x10 | Enter 0x1c (the keypad Enter too: its code is also 0x1c) |
| 0x20 | Esc 0x01 |
| 0x40 | a character key: code < 0x54 with an entry in the table 0x4a8b78 (digits, `qwertyuiop[]`, `asdfghjkl`, `zxcvbnm`, lower case); the character goes in bits 16-23 |
| 0x80 | Backspace 0x0e, Delete 0x53 / 0x153 |
| 0x100 | a Shift is down: 0x2a / 0x36 set bit 0 / 1 of a state, 0xaa / 0xb6 clear it (the state starts at 0 each time the frontend is entered) |
| 0x200 | Space 0x39 |

Repeats are not events, so holding a key does nothing. Shift only matters to the text entry screens,
which upper-case a lower-case letter (`islower`).

## Screens

`Front_Step` dispatches on the screen id 0x5101d0 and returns 0x5110b4. `Front_SetScreen` sets
0x511078; when a step changed the screen, the shared menu selection 0x5110a0 goes back to 1. Every
screen redraws everything and flips (`thunk_Gfx_Flip` 0x42da50). The backdrop is
`Front_DrawBackground(animate, lower)`: the animated logo (or `F_UPPER` for `animate` 0) over
`F_LOWER0` (the map, `F_LOWER1`, only on the main screen).

Sounds are frontend bank samples (`Snd_PlaySampleN`): 0 / 1 left / right, 3 player change, 7 / 8 up /
down, 2 Enter, 4 back, 0xc / 0xd typing / deleting, 0xe sound volume, 5 / 6 radio mode, 9-11 text
speed.

| Id | Screen | Keys |
|---|---|---|
| 4 | CD check. `Front_SetScreen(4)` sets the CD flag 0x511108, so it moves to 7 at once (the "cd-title" page is dead) | |
| 7 | Start: Play, Gather Network, Join Network, Options (the network items when `Net_IsActive`, never in the demo; without them Play, Options). Two Rockstar logos at (20, 390) and (556, 390), "Esc: Quit" | Up/Down; Enter: Play -> 0xb (mode 0), Gather -> 0xb (mode 1), Join -> 0xb (mode 2), Options -> 9; Esc -> 10 |
| 9 | Options: Sound 0-7 (+0x00, "Off" at 0), Music 0-7 (+0x01), Text speed Slow/Normal/Fast (+0x02, wraps), Music Mode Radio/Constant (+0x03, not in the demo), Transparency Effects (+0x07) | Left/Right change, Enter saves the file, Esc re-reads it; both -> 7 |
| 0xb | Player select: portrait, name plate with blinking `<` `>`, the slot's best score per unlocked level, "Del: Rename", "R: Reset" | Left/Right slot (+0x400; in French, German and Italian only slots 1, 5, 6, 7), Enter -> 8 (join: the connection list), Esc -> 7 (saves), Del -> 0xc (saves), `r` -> 0xe (single player) |
| 0xc | Rename: up to 14 characters in F_MHEAD with a blinking `<` | Enter: the name (empty: the default) and the cheat names, saves; Esc re-reads; -> 0xb |
| 0xe | Reset player: Cancel / Reset | Reset: default name, best = 0, -1 x 5, cutscenes unseen, selections 0, saves |
| 8 | Main (city and chapter select) over the map: the unlocked cities with their logo animations, the selected city's missions "1: name", the selection marker, the level's high scores ("Public Enemies") | Left/Right city, Up/Down mission, Enter: `Mission_SetIniSection` + `Mission_ReadIni`, start code 1, -> 6 (network: -> 0x12, a race -> the connection list); Space: the level's cutscene if seen; Esc -> 0xb |
| 6 | Loading: high scores, "<city> Chapter <n> :", the mission name, "Loading...". Returns the start code on its first frame and hands the options to the game (`HUD_SetPagerSpeed`, effects 0x5031e4, `Music_SetSequential`) | |
| 1 | Cutscene still (after a completed mission, or Space): `CUTn` with up to 4 animated glyph layers and the story lines in CUTTEXT at row 480 - 2h | Enter/Esc/Space, or the end of the voice: -> 8 (from main) or 0xd |
| 0xd | Results: score, high scores, the player's bests, the crime counts, the result text, the portrait, "Missions Passed : a / b", "Secrets Found : a / b"; network: final scores or race results and the outcome | Enter -> 8 (and the selection moves on), Esc -> 7 (likewise), Space: the cutscene again |
| 10 | Credits: the list 0x4af130 scrolls up 2 pixels per frame, clipped to rows 192-479 | Any of Enter/Esc/Space, or the end of the list: returns 4 (quit) |
| 0x12 | Multiplayer options: end on score (10000-999999999, steps of 10000, +0x0c) or kills (1-1000, +0x10); type +0x08 | Enter: the connection list (saves), Esc -> 8 (re-reads) |
| 0 | Connection list ("Choose a connection" / "No connections available"), up to 8 | Enter: host -> 0x10, join -> 0xf; Esc -> 0xb |
| 0xf | Session list (join) | Enter joins -> 5 |
| 0x10 | Session name (host), +0x401, 15 characters | Enter hosts -> 3 |
| 3 / 5 | Lobby (host / join): session title, "Status: ...", Enter: Play when the game can start | |
| 0x11 / 0x13 | Comms failure / version mismatch | -> 7 |
| 0x14 | SVGA error | -> 7 |
| 2 | Error: "Error" and the message 0x510760 (nothing sets this screen in this build) | -> 7 |

### Chapter tables

Two tables of 3 cities, 0x2c bytes each: single player at 0x4af4a0 (2 missions per city, sections 1,
2 / 102, 103 / 202, 203, levels 0..5) and network at 0x4af418 (4 per city, sections 1001-1004 /
1101-1104 / 1201-1204, levels 0..11, race flags):

| Offset | Field |
|---|---|
| +0x00 | city name (text `city%d`, written by `Front_LoadTexts`) |
| +0x04 | mission count |
| +0x05 | level index per mission (s8 x 5) |
| +0x0a | MISSION.INI section per mission (s16 x 4) |
| +0x12 | race flag per mission (network) |
| +0x18 | mission names (text `mission%d` of the section, x 4) |
| +0x28 | highest selectable mission, written by `Front_BuildChapterList` |

0x511084 points at the table in use, 0x5101b8 is the last selectable city. `Front_BuildChapterList`:
single player, per city the highest mission whose level has `best != -1`; network, all three cities.
The demo allows the first mission of the first city only.

Selection state lives in the player record: `chapter[net]` / `mission[net]` (net = 0x5110fc: 0 single
player, 1 network). The game mode 0x511104 is 0 single player, 1 gather (host), 2 join.

### After a game

`Front_SetScreen(1)` shows the cutscene only for a completed single-player mission (0x513230 == 1):
the level of the selection becomes 0x511114 and its `seen` flag is set. It loads the level's fonts
(`..\gtadata\%s.fon` of the names 0x4af540, count 0x4a73dc), the still `..\gtadata\cutN` (0x4af524,
only if another level's is loaded: 0x4af410) and starts the voice. Otherwise it goes to 0xd.

The cutscene's clock is the voice position / 100 (`Snd_GetCutsceneVoiceStatus` 0x404e60) or, without
a voice (-2), a frame counter 0..191 that loops; -1 (voice ended) leaves. Per level, 5 rows of 193
bytes at 0x4a73f8: rows 0-3 give per tick the glyph of each font layer ('0' none; drawn at the
{x, y} of 0x4a8a98), row 4 the story line (0 none, else text key 0x4af5fc[level * 4 + n]).

`Front_SetScreen(0xd)` takes the score from `Mission_GetResultText` 0x445670 and, once per game (flag
0x511100, cleared by screens 5, 6, 7):

- raises the level's best (`best[level]` +0x10 of the record) and flags it to blink;
- enters the score in the level's high scores (+0x18 + level * 0x3c, three entries ascending): above
  every entry it beats, the lower entries move down, the lowest drops out; the new entry blinks;
- after a completed mission, unlocks level + 1 (-1 -> 0) for levels below 5.

A second visit (Space -> cutscene -> results) runs the network outcome code instead, harmlessly. Then
the chapter list is rebuilt, the video mode (+0x04) taken from `Gfx_GetModeIndex`, and the file
saved. `Front_AdvanceMission` (on leaving the results) moves the selection to the next mission, or the
next city's first, after a completed mission below level 5.

Network outcome 0x5110e0: 3 abandoned (0x513230 == 7), else by score / kills the players with the
best value (at least 1) win, or for a race rank 0; 0 the local player won, 1 lost, 2 nobody won.

### Settings bytes

Changed by the screens (layout in [text-fonts.md](text-fonts.md#player_adat)): +0x00 sfx, +0x01 music,
+0x02 text (pager) speed, +0x03 music mode, +0x07 effects (options); +0x04 video mode (after a game);
+0x08 / +0x0c / +0x10 network target (multiplayer options, `Front_SelectMission`); +0x14 language
(cheats); +0x18 high scores and the records' `best` (results, cheats, reset); +0x28..+0x34 selection;
+0x38 `seen`; +0x400 current slot; +0x401 session name.

Saved ("wb") on: options Enter, player select Esc / Del, rename Enter, reset, the results, the main
screen's network race path, multiplayer options Enter. Re-read ("rb", undoing the screen's changes)
on: options Esc, rename Esc, multiplayer options Esc.

### Cheat names

The rename screen compares the new name with 16 strings (pointers 0x4af660..0x4af69c) stored with a
filler character between letters: the name must equal every second byte. Outside the demo, the first
match plays sample 7 (else 2) and:

| Strings | Effect |
|---|---|
| 0-2 | every level of the current table unlocked |
| 3-4 | 0x502f35 = 1 |
| 5-6 | 0x5031ec = 1 |
| 7-8 | 0x503198 = 1 |
| 9 | 0x503194 = 1 (debug keys, free camera) |
| 10 | every cutscene seen |
| 11 / 12 | language 99 (SPECIAL.FXT) / -1, from the next start |
| 13 | 0x502f74 = 1 |
| 14 | 0x50318c = 1 |
| 15 | 0x5031e5 = 1 |

The switches are `Game_SetOptions` globals; the port collects them in `Front.cheats` for the game.

## Interfaces of the port

- `FrontHooks`: sound (`Snd_PlaySampleN`, `Snd_PlaySfx`, the cutscene voice, volumes,
  `Audio_EnterFrontend`, `Audio_Update`), `Mission_SetIniSection` + `Mission_ReadIni`,
  `Player_SetNameColour`, the loading screen's options, `Player_GetColourName`, `Gfx_GetModeIndex`,
  `Gfx_ValidateModeIndex`. NULL hooks behave like the original with sound off.
- `FrontGameResult`: what the results screen reads from the game (scores, frags, ranks, race times,
  names, crime counters `Player_GetTotalKills`, mission and secret counters, the result text), the
  exit reason 0x513230 and `Game_Run`'s return value.
- The network layer is stubbed in `front_net.c` as DirectPlay on a machine without service
  providers: the enumeration succeeds and finds nothing.

## Deviations

- One key event per frame, as the original, but only presses: our platform has no key-release
  events, so a release does not use up a frame and Shift is released when `plat_keys` says it is up.
- `Input_GetKey` adds 0x100 to the keypad codes when the key produces no character (Num Lock off);
  the port treats every keypad key as Num Lock off.
- `clock()` (the logo animation, 83 ms per frame) is a frame count times 35 ms.
- Fatal errors (missing text, bad volumes in the file, unwritable `PLAYER_A.DAT`) are reported in
  `Front.error` instead of stopping; a missing text becomes "".
- The FXT format strings go through a small typed formatter (`front_fmt`) rather than `sprintf`: the
  same output for the shipped texts, no undefined behaviour for a text whose conversions do not
  match.
