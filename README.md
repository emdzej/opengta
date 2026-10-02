# OpenGTA

[![Built for gasm](https://gasm.emdzej.pl/badge/built-for-gasm-flat.svg)](https://gasm.emdzej.pl)

A from-scratch, portable reimplementation of **Grand Theft Auto** (DMA Design, 1997) in C11, ported
function by function from the Windows version of the game. It runs as `opengta.wasm`, a module for the
[gasm](https://gasm.emdzej.pl) WebAssembly game runtime (macOS, Linux, Windows, browser). It contains no
original code, assets or data: it reads the data files, and the tables of the original game program,
from your own copy of the game.

**Status: early.** The frontend, the city renderer, sound, the HUD and the mission script run as ported
from the original; you can walk, get into a car and drive. Traffic, the police and moving objects are
next, so most missions can't be completed yet. Details: [opengta.emdzej.pl/guide/status](https://opengta.emdzej.pl/guide/status).

## Game data

OpenGTA uses the Windows version of Rockstar's 2002 re-release (`GTAINSTALLER.zip`). It reads either the installed game
folder (with `GTADATA/` and `WINO/`) or the unzipped installer (`data1.cab`, `data2.cab`, ...): no need to run
the Windows setup.

## Build

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENGTA_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j                 # -> build-gasm/opengta.wasm
gasm-run build-gasm/opengta.wasm --asset-dir <GTA folder or unzipped installer>
```

Native build (tools and headless tests): `cmake -S . -B build && cmake --build build -j`.

## License

OpenGTA is free software under the [GNU GPL v3](LICENSE). Grand Theft Auto is © 1997 DMA Design / Rockstar
Games; OpenGTA contains none of its code or assets and is not affiliated with them.
