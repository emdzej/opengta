# Make a release

Releases ship the gasm module and the gasm bundles only, no native app, and never any game data.

## Tag it

Tags are plain semantic versions, **without a `v` prefix**. Set the version in `CMakeLists.txt` first
(`project(OpenGTA VERSION 0.1.0 ...)`), commit, then:

```sh
git tag 0.1.0
git push origin 0.1.0
```

The release workflow (`.github/workflows/release.yml`) refuses a tag that differs from the CMake version.

## What the workflow does

| Job | What |
|---|---|
| `wasm` | builds `opengta-<version>.wasm` + `.sha256` (wasi-sdk and the gasm C SDK from `tools/fetch-gasm-sdk.sh`) |
| `gasm-macos` | packages `opengta-gasm-<version>-macos-universal.zip` (`OpenGTA (gasm).app`, id `pl.emdzej.opengta.gasm`, signed ad hoc) and smoke-tests it |
| `gasm-package` | packages the `linux-x86_64`, `linux-arm64` (`.tar.gz`) and `windows-x86_64` (`.zip`) bundles |
| `gasm-smoke-linux` | smoke-tests the Linux bundles on x86_64 and on `ubuntu-24.04-arm` |
| `gasm-smoke-windows` | checks the Windows bundle: `gasm-run.exe` without data, `OpenGTA.cmd --help` and `--dry-run` |
| `release` | needs all of the above; creates the GitHub Release with every archive, the `.wasm` and their `.sha256` files, and `.github/release-notes.md` as the notes (followed by GitHub's generated changelog) |

Run it by hand from the Actions tab (*Release > Run workflow*) for a snapshot: the same files, version
`0.0.0-snapshot.<sha>`, kept as workflow artifacts and not published. CI (`ci.yml`) packages the bundles on
every push and pull request too, and smoke-tests the macOS and Linux x86_64 ones.

The gasm version is pinned in one place, `GASM_VERSION` in `tools/fetch-gasm-sdk.sh`: the module's SDK and
the bundled `gasm-run` both come from it. Keep `@emdzej/gasm-host` in `docs/package.json` in step.

## The bundles

`tools/fetch-gasm-runner.sh <platform>` downloads the released `gasm-run` and gasm's MIT `LICENSE` into
`.deps/gasm-runner-<platform>/`; `tools/package-gasm.sh` puts it together with the module, the launcher,
a `README.txt`, the licences and the icon:

```sh
tools/package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <runner dir> <out dir>
```

`OPENGTA_WASM=<file>` picks the module (default `build-gasm/opengta.wasm`); it is always bundled as
`opengta.wasm`, which names gasm's storage for the saves. The launchers are in `tools/gasm-bundle/`
(`launch-macos.sh`, `opengta.sh` + `opengta-gasm.desktop`, `OpenGTA.cmd` + `opengta.ps1`). The icon is
OpenGTA's own mark (the site's favicon), drawn at any size by `tools/icon.py` (standard library only);
`tools/make-icns.sh` makes the macOS `.icns` with `iconutil`.

## Test a bundle locally

```sh
tools/fetch-gasm-sdk.sh    # then build build-gasm/opengta.wasm (Build from source)
tools/package-gasm.sh 0.0.0-dev macos-universal "$(tools/fetch-gasm-runner.sh macos-universal)" dist
tools/smoke-gasm-bundle.sh dist/opengta-gasm-0.0.0-dev-macos-universal.zip
OPENGTA_SMOKE_DATA=game tools/smoke-gasm-bundle.sh dist/opengta-gasm-0.0.0-dev-macos-universal.zip
OPENGTA_SMOKE_DATA=installer tools/smoke-gasm-bundle.sh dist/opengta-gasm-0.0.0-dev-macos-universal.zip
```

Without data the smoke test checks the files (and on macOS the signature, bundle id, icon and the universal
runner), that `gasm-run` stops with OpenGTA's "game data not found" message and exit code 1, and the
launcher's `--help`, `--dry-run` and `--forget-data` against fake folders: an installed game, an installer,
and folders it must refuse (half an installer, a wrong-size exe, no `MISSION.INI`, the zip itself). It
uses a scratch `HOME` and `OPENGTA_DATA`, so it never opens a dialog or touches your saved location. With
`OPENGTA_SMOKE_DATA` it also runs the menus headless through the launcher (`--headless 400 --param intro=0`)
and checks that the hash line equals a direct `gasm-run` run of the same module (`SMOKE_GASM_RUN=<path>`
compares with another runner).

Linux bundles can be tested on a Mac in Apple's `container`:

```sh
container run --rm -v "$PWD":/w -w /w debian:trixie-slim bash -c \
  'apt-get update -qq && apt-get install -y -qq libasound2t64 &&
   OPENGTA_SMOKE_DATA=game tools/smoke-gasm-bundle.sh dist/opengta-gasm-0.0.0-dev-linux-arm64.tar.gz'
# x86_64: add --arch amd64 --rosetta and use the linux-x86_64 bundle
```

The Windows launcher is exercised by the release workflow's `gasm-smoke-windows` job.
