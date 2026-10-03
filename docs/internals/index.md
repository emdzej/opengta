# Internals

How OpenGTA is put together, and what has been learned about the original along the way. The pages in
this section are the project's working notes, kept next to the code: they describe the original's
layouts, formulas and addresses, and where the port lives. Addresses are virtual addresses in
`WINO/Grand Theft Auto.exe`; names follow the Ghidra project (`tools/ghidra/names.tsv`).

## Shape of the port

The original is a Win32 program around DirectDraw (through SciTech MGL), DirectInput, DirectPlay and the
Miles Sound System. OpenGTA keeps the game's own code paths and replaces only those libraries:

- **Portable core** (`src/`): the ported game, frontend, renderer and sound, in C11. It never touches files
  or the platform directly. Data goes through the file layer `src/vfs.h` (no `fopen` in the core), the
  platform through `src/platform.h` (present a frame, keys, pads, launch parameters, saves, log).
- **File layer**: `vfs.c` (folders and images, shared with OpenRF and OpenBallance), `iscab.c`
  (InstallShield 5 cabinets: the installer works as the game folder), `inflate.c`. Paths are normalised
  the way the original opens them, from `WINO\` as `..\gtadata\...`, case-insensitive.
- **The original executable** (`src/exe.c`): read at run time, size- and CRC-checked, for the tables the
  game needs (vehicles, peds, weapons, objects, strings), so none of them are copied into the source.
- **Platform backend**: `src/platform_gasm.c`, the only file that includes `gasm.h`. It mounts the gasm
  assets, maps gasm's raw keys to the DirectInput scan codes the original reads, presents the 640 x 480
  frame and feeds the mixer's output to gasm's audio. `src/vfs_host.c` is the POSIX backend for the native
  tests.
- **The app** (`src/app.c`): a per-frame state machine instead of the original's blocking loops: the
  frontend (one pass of WinMain's menu loop per frame, at 1000/35 Hz), the game (`Game_Run`, one tick of
  the 70 Hz sound timer per frame, a game frame every third, 23.33 a second, as in the original) and the
  development city viewer.

Everything is integer arithmetic where the original's is, rand and timers included, so a run is
deterministic: the same input gives the same frames and sound, which the gasm runners check with
hashes ([Headless runs](/howto/headless)).

## Source layout

| Path | What |
|---|---|
| `src/vfs.c`, `src/iscab.c`, `src/inflate.c` | file layer, InstallShield cabinets, inflate |
| `src/exe.c` | the original program read at run time |
| `src/map.c`, `src/style.c` | `Map_Load`, `Style_Load`, tile tables, palettes, tile animation |
| `src/render/` | camera, city renderer, DMA's rasteriser (`Poly_*`), sprites and draw lists |
| `src/text.c`, `src/font.c`, `src/front/`, `src/savedata.c` | FXT text, fonts, frontend pictures and screens, `PLAYER_A.DAT` |
| `src/audio/` | the sound modules, music and radio, the timer, the Miles mixer |
| `src/game/` | the game core (`Game_Run`, entity tables, collision, routes, maths), peds, player and input, cars and their physics, the mission interpreter, traffic, police and emergency services, objects, trains; the stubbed network layer (`stubs.c`) |
| `src/hud/` | the in-game HUD and the pager |
| `tests/` | headless tests against the real data |
| `tools/` | `gtafmt.py` (reference decoders), the gasm SDK and runner fetchers, Ghidra scripts and names |

## Pages

- [Game core](/game-core): level start, frame loop, entity tables, `MISSION.INI`.
- [Frontend](/frontend): WinMain's menu loop and every screen.
- [HUD](/hud): score, lives, wanted level, weapon, pager, arrows, zone names.
- [Peds, player, input](/peds): the control word, walking, entering cars, weapons, projectiles.
- [Cars](/cars): car tables, the rigid-body physics, collision, damage.
- [Missions](/missions): the `MISSION.INI` interpreter, triggers, doors, cranes.
- [City renderer](/render): camera, block drawing, DMA's rasteriser.
- [Sprites](/sprites): sprite info, draw trees, deltas, the sprite rasteriser.
- [Text, fonts, images](/text-fonts): FXT, FON, the frontend's pictures, `PLAYER_A.DAT`.
- [Sound and music](/audio): the sound modules and the Miles mixer.
- [Data formats](/formats): CMP maps, G24 styles and the rest.
- [Reverse-engineering notes](/re/): the function inventory of the whole executable.
