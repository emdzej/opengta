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

## Launch parameters

[Launch parameters](/guide/parameters) go in the address: `/play/?mission=1`, or
`/play/?front=0&map=miami`. The import is shared, so they work on later visits too.

## Removing the data

The player shows the imported copy and its size, with **Remove imported data**. Clearing this site's
data in the browser's settings removes it as well.
