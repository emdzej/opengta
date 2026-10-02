# Build from source

OpenGTA is C11 with CMake. The product is `opengta.wasm`, the gasm module; a native build exists for the
tools and the headless tests.

```sh
git clone https://github.com/emdzej/opengta && cd opengta
```

## opengta.wasm

`tools/fetch-gasm-sdk.sh` downloads wasi-sdk (clang and the C library for wasm32) and gasm's C SDK
(`gasm.h` and a CMake toolchain) into `.deps/`. The gasm version is pinned there (`GASM_VERSION`,
currently 0.6.0, the minimum).

```sh
tools/fetch-gasm-sdk.sh
cmake -S . -B build-gasm -DOPENGTA_PLATFORM=gasm -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_TOOLCHAIN_FILE=.deps/gasm-c-sdk/cmake/gasm-toolchain.cmake -DWASI_SDK_PREFIX="$PWD/.deps/wasi-sdk"
cmake --build build-gasm -j                    # -> build-gasm/opengta.wasm
```

Run it with the released runner, fetched into `.deps/` at the pinned version:

```sh
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"   # or linux-x86_64, linux-arm64, windows-x86_64
$RUN build-gasm/opengta.wasm --asset-dir game
```

## Native build (tools and tests)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
```

This builds the portable core and one test program per `tests/*_test.c`. See [Run the tests](/howto/tests).

## Game data in the checkout

The tests and scripts look for the data in the repository: `./game` (the installed folder) and
`./installer` (the unzipped `GTAINSTALLER.zip`). Both are git-ignored, as are `out/` (decoded assets and
renders) and `re/` (decompiler dumps). Never commit them, and never copy data from the executable into
the source: tables are read from it at run time.

## The site

This site is VitePress in `docs/` (Node 22, pnpm):

```sh
cd docs && pnpm install
scripts/vendor-web.sh        # the play page's gasm and csfs runtime, from the pinned npm packages
scripts/copy-wasm.sh         # build-gasm/opengta.wasm -> public/play/
pnpm dev                     # http://localhost:5173 (the player: /play/index.html)
pnpm build                   # -> docs/.vitepress/dist
```

`tools/screenshots.sh` regenerates the screenshots from `opengta.wasm` ([Headless runs](/howto/headless)).
