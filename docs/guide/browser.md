# Playing in the browser

The [player](/play/){target="_self"} runs the same `opengta.wasm` in your browser, on gasm's web runtime.

1. **Choose your GTA**: the installed game folder (with `GTADATA` and `WINO`) or the unzipped installer
   (with `data1.cab` and `data2.cab`). See [Game data](/guide/game-data). The page checks the folder and
   shows what it found.
2. **Import and play** copies the files the game reads into the site's private browser storage (OPFS),
   once: later visits start at once. Or **Play without importing** reads them from the folder you picked,
   which you then pick again next time.

Nothing you pick leaves your computer: the page has no server side, and this site hosts no game data.
The game runs in a Web Worker and reads the files on demand.

It needs a browser with WebAssembly, Web Workers and, to import, OPFS: a recent Chrome, Edge, Firefox
or Safari. OpenGTA draws in 2D, so WebGPU isn't needed. Folder picking uses `showDirectoryPicker` where
the browser has it, and a folder upload field elsewhere.

The keyboard goes to the game raw, as with `gasm-run` ([Controls](/guide/controls)). Holding Esc for a
second stops the game. Player names, scores and options are kept in the browser's storage for this site.

## Skins and resolution

Below the game choice, **Resolution and skins** sets two things OpenGTA adds to the original (they are
kept for the next visit):

- **Resolution**: 640 x 480 (the original) or 2x, 3x, 4x (1280 x 960 up to 2560 x 1920), the
  [hires renderer](/hires) (`hires=N`). The game plays exactly the same; only the picture is drawn
  again at the higher resolution. It costs speed: the browser runs the game at about the speed of
  gasm's Node runner, plus showing the larger frames, so 2x keeps full speed on most computers and 3x
  and 4x often fall below it. The status line under the game shows the frames per second; a mission
  runs at 70 (the game's 23 frames a second plus the sound timer), the menus at 35.
- **Skins**: **Add a skin folder** picks a [skin](/skins) (a folder with `skin.ini`; the PNG images and
  `skin.ini` are copied into the site's browser storage, next to the imported game data). Each skin in
  the list can be switched on and off, moved up and down (where two skins that are on have the same
  image, the higher one wins) and removed. The skins that are on are passed to the game as
  `skin=<lowest>,...,<highest>` with their files as assets `skins/<name>/...`, exactly as
  `gasm-run --asset-dir skins=<folder> --param skin=...` does, so a skin looks the same in the browser and
  on the desktop. Skins work at every resolution, including 640 x 480. A browser without OPFS keeps a
  picked skin for the visit only.

To make a skin, see [Create a skin](/howto/create-a-skin). `hires=` and `skin=` in the address override
the page's choice (see below).

## Launch parameters

[Launch parameters](/guide/parameters) go in the address: `/play/?mission=1`, or
`/play/?front=0&map=miami`. The import is shared, so they work on later visits too.

## Removing the data

The player shows the imported copy and its size, with **Remove imported data**. Clearing this site's
data in the browser's settings removes it as well.
