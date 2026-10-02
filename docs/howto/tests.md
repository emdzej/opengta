# Run the tests

The tests are headless C programs, one per `tests/*_test.c`, run against the real game data. Without the
data they skip, which is all CI can do (the data isn't redistributable).

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
(cd build && ctest --output-on-failure)          # or run ./build/<name>_test
```

The data root is `OPENGTA_DATA` (default `game`, the installed folder); the installer works the same way:

```sh
OPENGTA_DATA=installer ./build/cab_test           # the cabinets, compared with ./game
```

| Test | What |
|---|---|
| `cab_test` | the installer's cabinets: every file byte-compared with the installed game, including `Music/Track8.wav`, split across the two cabinets |
| `exe_test` | the original program found, checked and read by virtual address |
| `text_test` | the FXT language files: decryption, keys, the character mapping |
| `front_test` | the frontend's static layers: backdrops, menu text in every font, pictures, a cutscene still, `PLAYER_A.DAT` |
| `frontend_test` | the frontend state machine driven by scripted key presses, through every screen (writes `out/frontend/*.png`) |
| `render_test` | the city renderer in the three cities, coverage and determinism |
| `sprite_test` | sprite info, draw order, the delta cache; sprite sheets and cars, peds and objects in NYC |
| `audio_test` | the sound modules and the mixer: a scripted sequence rendered to `out/audio/test.wav`, checked by structure and determinism |
| `level_test` | the level start for every `MISSION.INI` section, then frames of the in-game loop |

Tests that render write PNGs into `out/` (git-ignored): look at them.

The gasm module is checked by running it headless on both runners and comparing hashes; see
[Headless runs and hash checks](/howto/headless).
