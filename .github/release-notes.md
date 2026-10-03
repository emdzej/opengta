OpenGTA needs your own copy of GTA from Rockstar's 2002 re-release (`GTAINSTALLER.zip`): the installed game
(the folder with `GTADATA` and `WINO`) or the unzipped installer (the folder with `data1.cab` and `data2.cab`).
No game data is included. OpenGTA is early work: see the [status](https://opengta.emdzej.pl/guide/status).

- **macOS**: unzip and open `OpenGTA (gasm).app` (ad hoc signed, not notarized: the first time, right-click, Open).
- **Linux** (x86_64, arm64): unpack and run `./opengta.sh` (`--install-desktop` adds a menu entry).
- **Windows** (x86_64): unzip and run `OpenGTA.cmd`.
- **Any system**: `gasm-run opengta-<version>.wasm --asset-dir <your GTA folder>` with gasm 0.6.0 or newer.

The launchers ask for the game folder once and remember it (`--change-data` picks another). Every file has a
`.sha256` next to it. Install guide: https://opengta.emdzej.pl/guide/install

Optional, not in the original: `--param hires=2|3|4` (up to 2560x1920), `--param upscale=xbr`, and skins with
your own replacement art (`--asset-dir skins=<folder> --param skin=<name>`): https://opengta.emdzej.pl/howto/create-a-skin

Or play in the browser: https://opengta.emdzej.pl/play/
