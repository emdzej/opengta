# AGENTS.md

Guidance for coding agents working in this repository. Humans: see [README.md](README.md).

## What this is

OpenGTA is a faithful, from-scratch reimplementation of **Grand Theft Auto** (DMA Design, 1997), the
Windows build (`WINO/Grand Theft Auto.exe`, 774,144 bytes) of Rockstar's 2002 re-release (`GTAINSTALLER.zip`), in C11.
It targets [gasm](https://github.com/emdzej/gasm) (`opengta.wasm`: native runners on macOS, Linux and
Windows, and the browser). A native build exists for tools and headless tests only. Sibling projects with
the same conventions: `../openrf` (Return Fire), `../openballance` (Ballance).

It is not an emulator or a wrapper: every subsystem is ported function by function from the original
executable. **Fidelity is the product.** Reproduce the original's behaviour, including its quirks and bugs.
If something looks wrong, check the disassembly; if the original does it, keep it and add a comment. Only
fix deviations *of the port* from the original.

**State:** early. Ported: file layer (directories, the installer's InstallShield cabinets), runtime exe
tables (`src/exe.c`), every function named in Ghidra (`docs/re/`), map + style, camera, city renderer and
DMA's rasteriser (32 bpp), sprites, text/fonts/frontend images, the frontend state machine, sound (Miles
mixer port), game core (`Game_Run`, MISSION.INI loading, entity tables, collision grid), peds/player/input,
cars and their physics, the mission interpreter, the HUD, traffic and AI drivers, police and emergency
services, objects/explosions/fires/power-ups, trains, the intro movie (Smacker). Next: play missions
through and compare with the original, the remaining stubs (`src/game/stubs.c`), fire engines, London.

## Hard rules

1. **Never commit original data or anything derived from it.** `game/` (the installed game),
   `installer/` (the unzipped GTAINSTALLER.zip), `directors/` (the Director's Cut DOSBox bundle: DOS exes + London data), `gta.exe`, `re/` (decompiler dumps, names work files) and
   `out/` (decoded assets, renders) are git-ignored: keep it that way.
2. **No data copied from the executable in the source tree.** Tables the game needs from the exe (vehicle,
   ped, weapon and object tables, anything that Carnage3D keeps in JSON) are **loaded at runtime** from
   `WINO/Grand Theft Auto.exe` (check size + CRC-32 `a5ca070e`). Small algorithmic constants and
   instruction operands are fine; tables and strings are not.
3. **Cite the original.** Every ported function names its source: `/* Map_Load 0x438200 */`. Keep names
   consistent with the Ghidra project (rename there as you learn; `tools/ghidra/names.tsv` is the record).
4. **Don't paste decompiler output** into source or docs. Write the port and the docs in your own words;
   describe layouts, formulas and addresses.
5. Prior art (Carnage3D, MIT; DMA's cds.doc) may inform format understanding; the exe is the authority and
   no code is copied.

## Layout

| Path | What |
|---|---|
| `src/` | Portable core. File layer `vfs.c` (from OpenRF/OpenBallance: directories, ISO images), `iscab.c` (InstallShield 5 cabinets: runs straight from the installer), `inflate.c`. Backends: `vfs_host.c` (POSIX, native tests), `platform_gasm.c` (the only file that includes `gasm.h`) |
| `src/exe.c` | The original exe read at runtime (size + CRC check, bytes by virtual address) |
| `src/map.c`, `src/style.c` | `Map_Load` 0x438200 (exe convention: z = 0 is the top layer), `Style_Load` 0x47cf10, tile tables, palette conversion, tile animation |
| `src/render/` | Camera (`camera.c`), city renderer (`city.c`), DMA's rasteriser `Poly_*` (`poly.c`, 32 bpp only) |
| `src/game/` | Simulation (`gmath.c`: `Math_InitTables`; the rest to come) |
| `src/text.c`, `src/font.c`, `src/front/`, `src/savedata.c`, `src/surface.h` | FXT text, FON fonts and text drawing, frontend images, PLAYER_A.DAT, the 32 bpp surface + blitters |
| `tests/` | Headless tests (`*_test.c`, one executable each) against the real data |
| `tools/` | `gtafmt.py` (reference decoders/renderers, stdlib only), `fetch-gasm-sdk.sh`, `fetch-gasm-runner.sh`, `ghidra/` scripts; gasm bundles: `package-gasm.sh`, `smoke-gasm-bundle.sh`, `gasm-bundle/` (launchers), `icon.py`, `make-icns.sh`; `screenshots.sh`, `web-play-test.mjs` |
| `.github/workflows/` | `ci.yml` (build + tests, GCC, `opengta.wasm`, package and smoke the gasm bundles), `release.yml` (wasm + four bundles on tag), `pages.yml` (site) |
| `docs/` | Reverse-engineering notes (`docs/re/`: inventory per address range, formats) |

## Game data

Development data lives in `./game` (the installed folder: `GTADATA/`, `WINO/`, `Music/`) and
`./installer` (the unzipped `~/Downloads/GTAINSTALLER.zip`: `data1.cab`, `data2.cab`, ...). Either is a
valid data root: `OPENGTA_DATA=<dir>` (default `game`). `Music/Track8.wav` is split across the two cabinets
(unshield 1.6.2 fails on it; our reader doesn't). The original runs from `WINO\` and opens
`..\gtadata\...`: paths are normalised to the data root, case-insensitively.

**All data access goes through `src/vfs.h`**: no `fopen` in the core.

## Build and test

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
(cd build && ctest --output-on-failure)        # or run ./build/<name>_test
OPENGTA_DATA=installer ./build/cab_test         # the cabinet layer against ./game
cmake -S . -B build-gcc -DCMAKE_C_COMPILER=gcc-16 -DCMAKE_C_FLAGS=-Werror && cmake --build build-gcc -j   # what CI's Linux job checks
python3 tools/gtafmt.py map game/GTADATA/NYC.CMP out/nyc.png 4
```

gasm module: gasm **0.6.0** (the pin is `GASM_VERSION` in `tools/fetch-gasm-sdk.sh`; it fetches wasi-sdk and the
C SDK into `.deps/`, `tools/fetch-gasm-runner.sh macos-universal` the released `gasm-run`):

```sh
tools/fetch-gasm-sdk.sh && RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
cmake -S . -B build-gasm -DOPENGTA_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j
# menus, Enter x3 -> mission 1 (intro=0: no intro movie, front=0 skips the menus)
$RUN build-gasm/opengta.wasm --asset-dir installer --headless 200 --param intro=0 \
  --input "60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)" --screenshot /tmp/g.png
node ../gasm/runners/web/headless.mjs build-gasm/opengta.wasm --asset-dir installer --headless 200 --param intro=0 \
  --input "60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)"     # must print the same hash line
```

Both runners must print identical `video_fnv32`/`audio_fnv32` lines (determinism), with `--asset-dir game`
too. A change that alters the hashes must be explained by the change.

Unattended runs: always `--headless` (never open a window, never use macOS `screencapture`).

## Reverse engineering

- Ghidra project `gta`, program `gta.exe` (a copy of `WINO/Grand Theft Auto.exe`), via ghidra-cli:
  `ghidra decompile 0xADDR --project gta --program gta.exe`, plus `x-ref`, `disasm`, `memory`, `function rename`.
  ghidra-cli needs `java_home` set in its config (JDK 21); without it the launcher waits on stdin forever.
- Full dump for grepping: `ghidra script run tools/ghidra/DumpAllNamed.java --project gta --program gta.exe -- "$PWD/re/all.c"`.
- Names: `tools/ghidra/names.tsv` (`addr name module confidence description`) is applied to the project
  with `tools/ghidra/apply-names.sh`; regenerate the dump afterwards.
- The binary is stripped. MSVC runtime from about 0x49cb00. Entry 0x49dc30, WinMain 0x437230.

## Docs site

VitePress in `docs/` (pnpm, `cd docs && pnpm install && pnpm build`), published to https://opengta.emdzej.pl by
`.github/workflows/pages.yml` (CNAME in `docs/public/CNAME`). Colour theme in `docs/.vitepress/theme/gta.css`,
sampled from our frontend renders (the menu font's yellow-to-amber gradient and drop shadow, the rust). **No
emojis.** Say "Rockstar's 2002 re-release (GTAINSTALLER.zip)", never freeware/free; don't host or link copies of the
game. Don't overstate the state (`docs/guide/status.md`). The "Built for gasm" badge is gasm's official one,
hotlinked from `https://gasm.emdzej.pl/badge/built-for-gasm-{light,dark,flat}.svg` (rules:
https://gasm.emdzej.pl/dev/badge; don't recolor, stretch or crop): light/dark by theme on the site, dark on the
play page, flat in the README. The notes (`docs/*.md`, `docs/re/`) are pages as they are: add new ones to the
sidebar in `docs/.vitepress/config.ts`. Dead links fail the build. Screenshots come from `tools/screenshots.sh`
(headless gasm-run renders of our port; never game files, never `screencapture`).

## Browser player

`docs/public/play/` (`/play/`) runs `opengta.wasm` in gasm's Worker mode: the user picks the installed GTA folder
or the unzipped installer (`showDirectoryPicker`, else `<input webkitdirectory>`); `data.js` checks it
(`GTADATA/MISSION.INI` + `WINO/Grand Theft Auto.exe` 774,144 bytes, or `data1.cab` + `data2.cab`), keeps what
the game reads and imports it into OPFS `opengta-data/` with csfs (marker `.opengta-import.json` written last);
"Play without importing" uses the File/Blob provider. Keys go to the game raw, gamepads as pads; holding Esc
stops. Frames are scaled with gasm's `GlPresenter` (sharp), else a 2D canvas. Storage namespace `opengta`
(IndexedDB). Query params: the launch params, `hashframes=N` with optional `input=<--input script>` (virtual
time, in-memory storage, prints the headless hash line into `globalThis.__opengtaResult`), `autoplay`. **Never put
game data in `docs/public/`.** `vendor/` is generated from the npm packages pinned in `docs/package.json`
(`docs/scripts/vendor-web.sh`; keep `@emdzej/gasm-host` equal to `GASM_VERSION`); bump the pins, never edit the
copies.

```sh
docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
node tools/web-play-test.mjs        # headless Chrome: folder/installer direct, OPFS import, later visit; hashes
(cd docs/.vitepress/dist && python3 -m http.server 8080)   # then open http://localhost:8080/play/
```

`web-play-test.mjs` hands `./game` and `./installer` to the page's file input (CDP `DOM.setFileInputFiles`), so it
exercises the real check/import/play code; every case must print PASS. gasm-run calls `gasm_exit` before printing
its hash line, and OpenGTA presents a last frame when it exits inside a level, which the Worker can't report: the
test compares the page with gasm's GasmHost in Node before the exit, and that host plus the exit with gasm-run.

## gasm bundles

`tools/fetch-gasm-runner.sh <platform>` downloads the released `gasm-run` (version = `GASM_VERSION` from
`tools/fetch-gasm-sdk.sh --version`) plus gasm's MIT `LICENSE` into `.deps/gasm-runner-<platform>/` and prints
the directory. `tools/package-gasm.sh <version> <platform> <runner dir> <out>` (`OPENGTA_WASM=` picks the module,
always bundled as `opengta.wasm` = gasm storage namespace `opengta`, as in the browser) builds
`opengta-gasm-<version>-<platform>.{zip,tar.gz}` + `.sha256` for `macos-universal` (`OpenGTA (gasm).app`, id
`pl.emdzej.opengta.gasm`, ad hoc signed), `linux-x86_64`, `linux-arm64` (`opengta.sh`, `.desktop`, png) and
`windows-x86_64` (`OpenGTA.cmd` + `opengta.ps1`). No native app, never game data. The icon is our own mark
(the favicon's design) drawn by `tools/icon.py` (stdlib), `.icns` by `tools/make-icns.sh`. Launchers
(`tools/gasm-bundle/`): the installed folder (`GTADATA/MISSION.INI` + `WINO/Grand Theft Auto.exe` 774,144 bytes)
or the unzipped installer (`data1.cab` + `data2.cab`) -> `--asset-dir`; anything else (the zip, half an
installer, a wrong exe) is refused with the reason; saved `data-location`; `--change-data`, `--forget-data`,
`--help`, `--dry-run`, `--install-desktop` (Linux); extra args go to gasm-run; Option at launch re-asks
(macOS); log `~/Library/Logs/OpenGTA/gasm.log` (macOS), `~/.local/state/opengta/gasm.log` (Linux, no
terminal), `%LOCALAPPDATA%\OpenGTA\gasm.log` (Windows).

**Test hooks (never open a dialog in an unattended run):** `OPENGTA_DATA=<folder>` uses the data without saving
it (and turns missing or wrong data into exit 2 instead of a dialog), `--dry-run` / `OPENGTA_DRY_RUN=1` prints
the gasm-run command, `OPENGTA_NO_PAUSE=1` (Windows). Point `HOME` (and `XDG_*`, `APPDATA`) at a scratch dir.
`tools/smoke-gasm-bundle.sh <archive>` checks a macOS or Linux bundle that way without data;
`OPENGTA_SMOKE_DATA=game` (and `=installer`) adds a headless menus run through the launcher whose hash line
must equal a direct gasm-run of the same module (`SMOKE_GASM_RUN=` for another runner). Linux bundles can be
smoke-tested in Apple `container` (`debian:trixie-slim`, `apt-get install libasound2t64`, `--arch amd64
--rosetta` for x86_64). The Windows launcher is only exercised by CI (`gasm-smoke-windows` in release.yml).

## Releases

Plain semver tags, **no `v` prefix**: bump `project(OpenGTA VERSION ...)` in `CMakeLists.txt` (the workflow
refuses a tag that differs), then `git tag 0.1.0 && git push origin 0.1.0` (the release job creates the GitHub
Release itself). `release.yml` builds `opengta-<version>.wasm`, packages the gasm bundles and smoke-tests each
on its own OS (macOS, Linux x86_64 and arm64, Windows); the `release` job needs all of them and attaches every
zip / tar.gz / wasm + `.sha256` with `.github/release-notes.md`. `workflow_dispatch` builds the same files as
artifacts only (version `0.0.0-snapshot.<sha>`). CI (`ci.yml`) packages all bundles and smoke-tests macOS and
Linux x86_64 on every push and PR. Releases ship only the module and the bundles: no native app.

## Expansions (later)

GTA London 1969 and 1961 (in `directors/`, the Director's Cut DOSBox bundle) run on the same engine, as
modified DOS builds (`GTADOS/gta24_uk.exe`, `Gta24_61.exe`), with data in the same formats (CMP 331, G24 336,
MISSION.INI grammar) under `GTADATA/UK`. The Windows exe stays the authority for the base game; London will be
variant switches on the same port, verified against the DOS London exes. Keep the core data-driven (file
names, counts and tables from the data or the exe, not hard-coded per city) so that stays a switch, not a fork.

## Commits

Imperative subject, body explaining why when non-obvious. Never commit `game/`, `installer/`, `gta.exe`,
`re/`, `out/`, `build*/`, `.deps/`.
