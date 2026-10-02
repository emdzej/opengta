# Troubleshooting

## The game doesn't find the data

`gasm-run` logs what it got. OpenGTA accepts:

- the installed game: `--asset-dir` must be the folder that contains `GTADATA/` and `WINO/` (not
  `GTADATA` itself, not its parent);
- the unzipped installer: the folder that contains `data1.cab` and `data2.cab` side by side.

With the installer it logs `game data from gasm assets: data1.cab (InstallShield 5 cabinets, 195 files)`.

## "WINO/Grand Theft Auto.exe: not found" or "unsupported version"

OpenGTA reads the original's tables from `WINO/Grand Theft Auto.exe` and checks its size (774,144 bytes)
and CRC-32 (`a5ca070e`). Without it the game doesn't start; another build of the executable (patched, or
from a retail CD) fails the check. Use the files of the 2002 re-release, or its installer.

## The browser player rejects the folder

It checks for `GTADATA/MISSION.INI` and `WINO/Grand Theft Auto.exe` of the right size, or for both
`data1.cab` and `data2.cab`. Pick the folder itself or the one directly above it. If the import fails for
lack of space, free some disk space or use **Play without importing**.

## "The imported data is in use"

Only one tab can read the imported copy at a time. Close the other OpenGTA tab.

## A mission starts but nothing moves

That is the current state: the level start is ported, walking, driving and the mission scripts aren't.
See [Status](/guide/status).

## Reporting a problem

Open an issue at [github.com/emdzej/opengta](https://github.com/emdzej/opengta/issues) with the runner's
log, the gasm version and how you provided the data. Never attach game files.
