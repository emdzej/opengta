# Headless runs and hash checks

gasm runners run a module without a window with `--headless N`: N frames, as fast as possible, on
virtual time, then they print a hash of every presented frame and of the sound:

```
frames=200 presented=163 size=640x480
video_fnv32=... audio_fnv32=... audio_frames=...
```

OpenGTA is deterministic, so the same module, data, parameters and input give the same hashes on every
runner. That's the check for the gasm build. Never open a window in unattended runs, and never capture the
desktop: `--screenshot` writes the last frame.

## The menu-to-mission run

```sh
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
# the start menu
$RUN build-gasm/opengta.wasm --asset-dir game --headless 60 --screenshot /tmp/menu.png
# Enter x3: Play, the player, Liberty City mission 1
$RUN build-gasm/opengta.wasm --asset-dir installer --headless 200 \
  --input "60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)" --screenshot /tmp/g.png
```

`--input` scripts input by frame: `KEY(Enter)` holds a raw key for that frame (`KEY(ShiftLeft+ArrowUp)`,
ranges like `60-90:KEY(ArrowLeft)`), `UP`, `A` and so on press pad buttons.

The same run on gasm's Node runner (from a gasm checkout, or `npx -p @emdzej/gasm-host gasm-headless`)
must print the same two lines:

```sh
node ../gasm/runners/web/headless.mjs build-gasm/opengta.wasm --asset-dir installer --headless 200 \
  --input "60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)"
```

So must `--asset-dir game` and `--asset-dir installer`: the cabinets give the game the same bytes as the
installed files. A change that alters the hashes has to be explained by the change.

## The browser player

`tools/web-play-test.mjs` runs the [player](/guide/browser) in headless Chrome through its own page code
(picking the folder, checking it, importing into OPFS, running in the Worker) and compares its hash lines
with `gasm-run` for the same frames and input. One detail: the runners call the guest's `gasm_exit`
before printing, and OpenGTA presents a last frame when it exits inside a level; the page's Worker
reports the line before that exit, so the test also runs gasm's host in Node without the exit to compare
the page with. The page takes `hashframes=N` (and `input=` with the
`--input` syntax) in its address for that.

```sh
docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
node tools/web-play-test.mjs          # [data folder] [out dir]; PASS / FAIL per case, PNGs in /tmp/opengta-play
```

## Screenshots

`tools/screenshots.sh [data] [opengta.wasm]` makes the site's screenshots (`docs/public/screenshots/`):
headless runs of the menus, mission starts and the city viewer, converted to JPEG. They are OpenGTA's own
renders.
