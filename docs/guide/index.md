# Introduction

OpenGTA is a from-scratch reimplementation of **Grand Theft Auto** (DMA Design, 1997), the first GTA, in
portable C11. It is ported function by function from the Windows build of the game, `WINO/Grand Theft
Auto.exe` of Rockstar's 2002 re-release, which is the complete game: Liberty City, San Andreas and Vice
City with all their missions.

It is not an emulator, a wrapper or a remake. Each subsystem of the original program is read in the
disassembly and written again in C, keeping its behaviour, including its quirks and bugs. Fidelity is
the point: where the port differs from the original, the port is wrong.

## What you need

- **Your own copy of GTA**: the installed game folder, or the unzipped installer. OpenGTA contains no
  code, graphics, sound or text from the game, and it reads even the original's internal tables from the
  executable at run time. See [Game data](/guide/game-data).
- **gasm**: OpenGTA is one WebAssembly module, `opengta.wasm`, for the [gasm](https://gasm.emdzej.pl)
  game runtime. Run it with the native runner `gasm-run` on macOS, Linux or Windows
  ([Running on gasm](/guide/running)), or in the browser on this site ([Play](/play/){target="_self"}).

## How far it is

Early: the menus work and a mission starts, but you can't play it yet. Read [Status](/guide/status)
before you expect a game.

## Where next

- [Game data](/guide/game-data): where GTA comes from and which files OpenGTA reads.
- [Running on gasm](/guide/running) and [Launch parameters](/guide/parameters).
- [Playing in the browser](/guide/browser).
- [Controls](/guide/controls) and [Troubleshooting](/guide/troubleshooting).
- [Build from source](/howto/build-from-source), if you want the latest module or to help.
