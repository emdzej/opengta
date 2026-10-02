# Game data

OpenGTA has no game data of its own. It reads the files of **your copy of GTA**, and also the tables
inside the original program (vehicles, peds, weapons, objects, text keys), which it loads from
`WINO/Grand Theft Auto.exe` at run time rather than copying them into its source.

## Which GTA

The Windows version from **Rockstar's 2002 re-release**, distributed as `GTAINSTALLER.zip` (its README
says the game was modified to run on then-current PCs and Windows versions). That release is the
complete game, with all three cities. OpenGTA checks its executable: `WINO/Grand Theft Auto.exe`, 774,144 bytes, CRC-32
`a5ca070e`.

Rockstar no longer offers the download on its own site. This project doesn't host the game and doesn't
link to copies of it elsewhere: use the copy you have.

## Two ways to provide it

Either folder works. OpenGTA finds out which one it got.

### The installed game

The folder the Windows setup installed GTA to:

```
GTADATA/      maps (NYC.CMP, SANB.CMP, MIAMI.CMP), styles (STYLE001.G24 ...), MISSION.INI, fonts,
              text, frontend pictures, AUDIO/
WINO/         Grand Theft Auto.exe (and its DLLs, which OpenGTA doesn't use)
Music/        Track1.wav ... Track10.wav (the radio)
```

### The unzipped installer

Unzip `GTAINSTALLER.zip` and use that folder as it is, no need to run the Windows setup (handy on macOS
and Linux). OpenGTA reads the game from the installer's InstallShield cabinets:

```
data1.cab     the descriptor and most of the game
data2.cab     the rest (Music/Track8.wav is split across the two)
SETUP.EXE, _sys1.cab, _user1.cab, ...   the setup program: not needed
```

The cabinets are read in place on every run. Nothing is extracted to disk.

## What OpenGTA reads

The data files under `GTADATA/` (and `Music/` for the radio), and the executable `WINO/Grand Theft
Auto.exe` for its tables and checks. It never runs the executable or loads its DLLs. The browser player
copies only these into its storage (about 320 MB, most of it the music).

## Saves

The player names, scores and options (the original's `PLAYER_A.DAT`) are kept in gasm's storage, not
in the game folder: `gasm-run` keeps them in its data folder (or `--storage-dir`), the browser in its site
storage. The first run starts from the game's own `PLAYER_A.DAT`.
