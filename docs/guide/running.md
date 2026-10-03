# Running on gasm

OpenGTA is a gasm game: `opengta.wasm` runs on gasm's native runner `gasm-run`, version **0.6.0** or
newer.

## Get the runner and the module

- **gasm-run**: download it for your platform from the
  [gasm releases](https://github.com/emdzej/gasm/releases) (macOS universal, Linux x86-64 and arm64,
  Windows x86-64). From a checkout, `tools/fetch-gasm-runner.sh macos-universal` fetches the pinned
  version into `.deps/`.
- **opengta.wasm**: from a release (`opengta-<version>.wasm`, or a ready-to-run gasm bundle: see
  [Installing](/guide/install)), [build it from source](/howto/build-from-source), or take the module the
  browser player runs, [opengta.wasm](https://opengta.emdzej.pl/play/opengta.wasm){target="_self"},
  built from `main` by the site's workflow. It is the engine only: no game data.

## Run it

Point `--asset-dir` at your GTA, either form ([Game data](/guide/game-data)):

```sh
# the installed game (the folder with GTADATA and WINO)
gasm-run opengta.wasm --asset-dir "/path/to/GTA"

# the unzipped GTAINSTALLER.zip (the folder with data1.cab and data2.cab)
gasm-run opengta.wasm --asset-dir "/path/to/GTAINSTALLER"
```

The game opens at the start menu in a 960 x 720 window, scaled up from the original's 640 x 480. Useful
`gasm-run` options:

| Option | What |
|---|---|
| `--param name=value` | a [launch parameter](/guide/parameters), e.g. `--param mission=1` |
| `--window 1280x960` | another window size |
| `--filter nearest` | plain pixel doubling instead of the default `sharp` scaling (also `xbr`, `fsr`, `crt`) |
| `--integer-scale` | whole multiples only |
| `--storage-dir saves` | keep the saves (player names, scores, options) in a folder of your choice |
| `--mute` | no sound |

Holding Esc for a second quits `gasm-run`; a tap goes to the game. See [Controls](/guide/controls).

## Headless

`gasm-run` also runs the game without a window, as fast as it can, and prints hashes of every frame and
of the sound, which is how OpenGTA checks that the native and the browser runners agree. See
[Headless runs and hash checks](/howto/headless).
