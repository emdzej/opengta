# Reverse engineering notes

The executable (`WINO/Grand Theft Auto.exe`, 774,144 bytes, CRC-32 `a5ca070e`) is stripped. Every
function from 0x401000 to the C runtime (about 0x49cb00) now has a name in the Ghidra project `gta`; the
record is `tools/ghidra/names.tsv` (address, name, module, confidence, description), applied with
`tools/ghidra/apply-names.sh`. MSVC linked the objects in source order, so modules are contiguous address
ranges. The first inventory, one document per range:

| Doc | Range | Modules |
|---|---|---|
| [inventory-1](inventory-1.md) | 0x401000–0x417d90 | delayed events, ambulance, block animation, sound (3D, engines, voices, scanner), cars, car info, music, police wanted level, heli, collision, net wrappers, tuning, sprite overlays, `Game_Run`, MGL video/input glue, traffic AI |
| [inventory-2](inventory-2.md) | 0x418390–0x43ce90 | traffic generation, emergency services ("sentinels"), errors, explosions, frontend, file I/O, fires, maths tables, fonts, game core, gangs, controls/replays, collision grid, `WinMain`, map, city renderer, camera |
| [inventory-3](inventory-3.md) | 0x43cef0–0x4630e0 | mission script runtime and interpreter, mission.ini reader, movie, map queries, areas, net sync, objects, peds, car rigid-body physics, players |
| [inventory-4](inventory-4.md) | 0x463100–0x476500 | players, police AI, power-ups, trains, registry config, path finding, routes, Miles sound, dummies, mission objects |
| [inventory-5](inventory-5.md) | 0x476550–0x48a310 | mission helpers, triggers, sprites and projection, style loader, FXT text, timer, traffic lights, rails, draw lists, HUD, DirectPlay, projectiles, blitters, maths, Win32 events |
| [inventory-6](inventory-6.md) | 0x48a320–0x49cac6 | SciTech MGL (third party), DirectPlay thunks, DMA's polygon rasteriser (`Poly_*`), iostream, CRT |

## Top level

- `entry` 0x49dc30 → `WinMain` 0x437230: registry config, intro movie, frontend loop paced at 35 ms per
  frame (`Sleep(35 - elapsed)`), keyboard scan codes mapped to frontend input bits.
- `Game_Run` 0x4148a0: the in-game loop (no sleep): present → read controls → per-player keys →
  `Game_Frame` 0x430b20 → `Game_Update` 0x430c00 → `Game_Render` 0x430d40.
- `Game_Init` 0x430a20 starts a level: `Map_Load` 0x438200 (CMP), `Style_Load` 0x47cf10 (G24),
  `Mission_Load` 0x445800 (MISSION.INI), objects, peds, players, trains, paths.
- `Game_Update` 0x430c00: delayed events, mission update, cars (`Cars_UpdateAll` 0x40adc0), peds,
  objects, emergency services (0x419880, not decompilable: read the disassembly), players.

## Shared conventions

- World coordinates are 16.16 fixed point, 64 units per block: block = coord >> 22.
- Angles are 0..1023 (0 along +y); sine table 0x511e28 (int[0x500], 16.16), cosine = sine + 256 entries.
  These are bss: computed at start-up by `Math_InitTables` 0x430400.
- Map layers: z = 0 is the top layer, z = 5 the lowest.
- Entity tables: cars 0x4be248 (400 × 0x2b0; car n's driver is ped 200 + n), peds 0x7284e0 (620 × 0x100),
  objects 0x6b40d0 (3500 × 0x88), players 0x74f148 (4 × 0x1bc), power-ups 0x74f858 (256), trains 0x7514a8
  (0x5c8 each), AI controllers 0x507ea0 (129 × 0x98).
- Cached block types 0x55fab0: u8 `[6][256][256]`, the low byte of each block's type map (bit 7 replaced
  by "is slope"); all AI and road queries read it.
