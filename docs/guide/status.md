# Status

OpenGTA is **early but complete in scope**: every part of the single-player game is reimplemented, and the
whole game runs, from the intro movie through the menus to the three cities with their traffic, police,
trains and missions. What it still needs is playing: whole missions haven't been played through and
compared with the original yet, so expect differences and bugs.

## Ported

| Part | State |
|---|---|
| Data layer | The installed folder or the installer's InstallShield 5 cabinets (`data1.cab`, `data2.cab`, read directly, including the file split across the two cabinets); paths as the original opens them (`..\gtadata\...`), case-insensitive. The original executable is read at run time for its tables (size and CRC-32 checked). |
| Maps and styles | `Map_Load`, `Style_Load`: the CMP city maps and G24 styles, tile animation, palettes. [Data formats](/formats) |
| City renderer | The camera and DMA's software rasteriser (the 32 bpp path), integer for integer. [City renderer](/render) |
| Sprites | Sprite info, draw lists, the sprite and rectangle rasterisers, damage deltas. [Sprites](/sprites) |
| Text and fonts | FXT text, FON fonts, the frontend's pictures, `PLAYER_A.DAT`. [Text, fonts, images](/text-fonts) |
| Frontend | WinMain's menu loop: start menu, options, player select and rename, the city and mission select with its high scores, results, cutscene stills, credits. The network screens show, but networking (DirectPlay) is a stub. [Frontend](/frontend) |
| Sound | The game's three sound modules over a port of the Miles software mixer it used: sound banks, 3D one-shots, engine loops, voices, the police scanner, music and radio, the 70 Hz timer that paces the game. [Sound and music](/audio) |
| Game core | The 43 launch switches, the level start (`MISSION.INI` with all its object types), the car, ped, object and player tables, the collision grid, routes, the frame loop `Game_Run` with its timing. [Game core](/game-core) |
| Player and peds | The keyboard controls and the control word, replays, walking, running, turning, wall collision, getting into and out of cars, ambient pedestrians, weapons and projectiles. [Peds, player, input](/peds) |
| Cars | Car creation and the per-frame update, the player's driving, the rigid-body physics (the original's floating point, operation by operation), collision with the map, objects and other cars, damage and dents. [Cars](/cars) |
| Missions | The `MISSION.INI` interpreter with all its commands, phones, triggers, doors, cranes, timed bombs, garages, mission cars and briefs. [Missions](/missions) |
| Intro movie | `MOVIE.SMK` through a clean-room Smacker decoder, picture and sound, before the menus (`intro=0` skips it). [Intro movie](/movie) |
| Traffic | Cars spawning around the view and driving: lanes, junctions, traffic lights, path finding for AI drivers. [Traffic](/traffic) |
| Police and emergency services | The wanted level and crime reports, police pursuits and roadblocks, arrests, ambulances, fire engines, the gang, the scripted helicopter. [Police](/police) |
| Objects | Moving and animated objects, explosions, fires and the fire engines that put them out, power-ups, animated doors, map edits. [Objects](/objects) |
| Trains | The elevated trains: track following, stations, doors, passengers, riding and driving, crashes. [Trains](/trains) |
| High resolution and skins (additions) | Not in the original, opt-in: `hires=2..4` renders the game, HUD, menus and intro at up to 2560x1920 with the game unchanged underneath; `upscale=xbr` upscales the original art; `skin=` layers replacement art (tiles, sprites, fonts, pictures), with a template generator and validator. [High resolution](/hires), [Creating a skin](/howto/create-a-skin) |
| HUD | Score, multiplier and lives, the wanted level, weapon and ammo, the pager, subtitles, the arrow, area and car-name signs, the big messages, the pause and quit screens. [HUD](/hud) |

## Not yet

- Whole missions haven't been played through and checked against the original yet.
- Networked games: the DirectPlay layer is stubbed as a machine without a service provider (no
  session can start), in the frontend (`src/front/front_net.c`) and the game core (`src/game/stubs.c`).
- The 8, 15 and 16 bpp render paths (the port draws the 32 bpp one).

Only the network layer is left out; it does what the original does when no network game runs.

## Releases

Releases ship `opengta-<version>.wasm` and ready-to-run gasm bundles for macOS, Linux and Windows
([Installing](/guide/install)). The browser player runs the module built from `main`.

## Versions

- **The game**: the Windows executable of the 2002 re-release (774,144 bytes, CRC-32 `a5ca070e`), and its
  data. The 1997 DOS version, other Windows builds and the London 1969 and 1961 expansions are not
  supported; London is planned as variant switches on the same port.
- **gasm**: 0.6.0 or newer (raw keyboard, 64-bit asset reads, the window title).
