# Installing

OpenGTA ships as one WebAssembly module, `opengta.wasm`, plus ready-to-run **bundles** of that module with
gasm's native runner `gasm-run` for macOS, Linux and Windows. None of them contains any of the game: you
need your own copy of GTA from Rockstar's 2002 re-release (`GTAINSTALLER.zip`), see
[Game data](/guide/game-data). OpenGTA is early work: read [Status](/guide/status) first.

## Download a bundle

Each [release](https://github.com/emdzej/opengta/releases) has:

| System | File | Start |
|---|---|---|
| macOS 11 or later (Apple Silicon and Intel) | `opengta-gasm-<version>-macos-universal.zip` | `OpenGTA (gasm).app` |
| Linux x86_64 / arm64 | `opengta-gasm-<version>-linux-<arch>.tar.gz` | `./opengta.sh` |
| Windows x86_64 | `opengta-gasm-<version>-windows-x86_64.zip` | `OpenGTA.cmd` |
| any system with gasm-run 0.6.0 or newer | `opengta-<version>.wasm` | `gasm-run opengta-<version>.wasm --asset-dir <your GTA>` |

Every file has a `.sha256` next to it. To check a download:

```sh
shasum -a 256 -c opengta-gasm-0.1.0-macos-universal.zip.sha256     # macOS
sha256sum -c opengta-gasm-0.1.0-linux-x86_64.tar.gz.sha256          # Linux
```

On Windows: `Get-FileHash opengta-gasm-0.1.0-windows-x86_64.zip` in PowerShell, and compare with the
`.sha256` file.

Unpack the archive anywhere. Each bundle has a `README.txt` with the same instructions as this page.

## First run: choose your game data

The first time, the launcher asks where your GTA is. Either folder works:

- **the installed game**: the folder the Windows setup installed GTA to, with `GTADATA` and `WINO` in it
  (and `WINO/Grand Theft Auto.exe`, which must be the re-release's, 774,144 bytes);
- **the unzipped installer**: the folder you unzipped `GTAINSTALLER.zip` to, with `data1.cab` and
  `data2.cab`. There is no need to run its setup, so this is the easy way on macOS and Linux.

The launcher checks the folder and says what is missing if it is neither. The zip itself doesn't work:
unzip it first. Your choice is remembered, and the next start goes straight to the game.

| | macOS | Linux | Windows |
|---|---|---|---|
| How it asks | a dialog, then a folder chooser | zenity or kdialog, if installed; otherwise pass the folder: `./opengta.sh /path/to/GTA` | a dialog, then a folder chooser |
| Saved in | `~/Library/Application Support/OpenGTA/data-location` | `~/.config/opengta/data-location` | `%APPDATA%\OpenGTA\data-location` |
| Choose again | hold <kbd>Option</kbd> while opening the app | `./opengta.sh --change-data` | `OpenGTA.cmd --change-data` |
| Log | `~/Library/Logs/OpenGTA/gasm.log` | the terminal, or `~/.local/state/opengta/gasm.log` when started from a menu | the console window and `%LOCALAPPDATA%\OpenGTA\gasm.log` |
| Saves | `~/Library/Application Support/gasm/opengta/` | `~/.local/share/gasm/opengta/` | `%APPDATA%\gasm\opengta\` |

The saves (player names, scores, options) are gasm's storage for `opengta`, the same namespace the
[browser player](/guide/browser) uses in the browser.

## Launcher options

All three launchers take the same options:

```sh
opengta.sh [options] [DATA] [gasm-run options...]
```

| Option | What |
|---|---|
| `DATA` | the game folder for this run; it is saved for the next ones |
| `--change-data` | ask for the game data even if a location is saved |
| `--forget-data` | delete the saved location and exit |
| `--dry-run` | print the `gasm-run` command instead of running it |
| `--install-desktop` | (Linux) add a menu entry, `~/.local/share/applications/opengta-gasm.desktop` |
| `--help` | the options and where things are kept |

Anything after the data goes to `gasm-run`: [launch parameters](/guide/parameters) like
`--param intro=0` or `--param mission=1`, and the runner's own options such as `--window 1280x960`,
`--filter nearest` or `--mute` (see [Running on gasm](/guide/running)). On macOS, from Terminal:

```sh
"OpenGTA (gasm).app/Contents/MacOS/OpenGTA" --param intro=0
open "OpenGTA (gasm).app" --args --change-data
```

`OPENGTA_DATA=<folder>` uses that folder for one run without saving it, and makes a missing or wrong
folder an error (exit code 2) instead of a dialog: that is how the bundles are tested.

## macOS: Gatekeeper

The app is signed ad hoc, not notarized by Apple, so the first start is blocked as from an "unidentified
developer". Right-click (or Control-click) `OpenGTA (gasm).app` and choose **Open**, then **Open** again.
On macOS 15 and later, open it once, then go to **System Settings > Privacy & Security** and click
**Open Anyway**. Or remove the quarantine flag that the download added:

```sh
xattr -dr com.apple.quarantine "OpenGTA (gasm).app"
```

## Linux: libraries

`gasm-run` needs ALSA for sound (`libasound2`; the package is `libasound2t64` on Debian 13, Ubuntu 24.04
and newer) and a Vulkan capable graphics driver for the window. For the folder chooser, install
`zenity` (GNOME) or `kdialog` (KDE), or pass the folder as an argument once.

## Windows: SmartScreen

`gasm-run.exe` and the launcher are not signed. If Windows SmartScreen says it protected your PC, click
**More info**, then **Run anyway**. If Windows marks the downloaded zip as coming from the internet, you
can also unblock it before unpacking: right-click the zip, **Properties**, tick **Unblock**. `OpenGTA.cmd`
runs `opengta.ps1` with PowerShell (built into Windows); a window stays open after an error so you can read
it.

## In the browser

Nothing to install: open the [player](/play/){target="_self"}. See [Playing in the browser](/guide/browser).

## From source

See [Build from source](/howto/build-from-source).
