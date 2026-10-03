#!/usr/bin/env bash
# Package opengta.wasm with a released gasm-run into a double-clickable bundle (no game data inside).
#   tools/package-gasm.sh <version> <platform> <gasm-run dir> <out dir>
# <platform>: macos-universal, linux-x86_64, linux-arm64 or windows-x86_64. <gasm-run dir> holds gasm-run[.exe]
# and gasm's LICENSE (tools/fetch-gasm-runner.sh <platform> makes one). OPENGTA_WASM=<file> picks the module
# (default build-gasm/opengta.wasm); it is always bundled as opengta.wasm, which is also gasm's storage
# namespace for the saves (the same as the browser player's, "opengta"). Produces in <out dir>:
#   macos-universal  OpenGTA (gasm).app, opengta-gasm-<version>-macos-universal.zip
#   linux-<arch>     opengta-gasm-<version>-linux-<arch>.tar.gz
#   windows-x86_64   opengta-gasm-<version>-windows-x86_64.zip
# each archive with a .sha256 next to it. The macOS app is signed ad hoc when codesign is available.
# The launchers are in tools/gasm-bundle/, the icon is drawn by tools/icon.py (tools/make-icns.sh on macOS).
# No AOT .cwasm: gasm-run --compile only targets the host it runs on, so it can't be made for the other
# platforms (or both halves of the universal app) at packaging time.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
usage="usage: package-gasm.sh <version> <macos-universal|linux-x86_64|linux-arm64|windows-x86_64> <gasm-run dir> <out dir>"
VERSION=${1:?$usage}; PLATFORM=${2:?$usage}; RUNDIR=${3:?$usage}; OUT=${4:?$usage}
WASM=${OPENGTA_WASM:-$ROOT/build-gasm/opengta.wasm}
GASM_VERSION=$(cat "$RUNDIR/VERSION" 2>/dev/null || "$ROOT/tools/fetch-gasm-sdk.sh" --version)
SRC="$ROOT/tools/gasm-bundle"
EXE=""; [[ "$PLATFORM" == windows-* ]] && EXE=.exe
case "$PLATFORM" in macos-universal | linux-x86_64 | linux-arm64 | windows-x86_64) ;; *) echo "$usage" >&2; exit 2 ;; esac
for f in "$WASM" "$RUNDIR/gasm-run$EXE" "$RUNDIR/LICENSE" "$ROOT/LICENSE"; do
  [ -f "$f" ] || { echo "missing $f (tools/fetch-gasm-runner.sh $PLATFORM fetches the runner and its LICENSE)" >&2; exit 1; }
done
mkdir -p "$OUT"; OUT="$(cd "$OUT" && pwd)"
NAME="opengta-gasm-$VERSION-$PLATFORM"

# fill <template> <out>: substitute the versions
fill() { sed -e "s|@VERSION@|$VERSION|g" -e "s|@GASM_VERSION@|$GASM_VERSION|g" "$1" >"$2"; }
crlf() { sed -e 's/\r*$/\r/' "$1" >"$1.tmp" && mv "$1.tmp" "$1"; }
sha() { (cd "$OUT" && if command -v sha256sum >/dev/null; then sha256sum "$1"; else shasum -a 256 "$1"; fi >"$1.sha256"); }

# readme <out file>: README.txt for this platform (~, $USER, %APPDATA% and backslashes are meant literally)
# shellcheck disable=SC2088,SC2016,SC1003
readme() {
  local start data_how change saves logs lic=""
  [ -n "$EXE" ] && lic=.txt
  case "$PLATFORM" in
    macos-*)
      start='Open "OpenGTA (gasm).app". It is signed ad hoc, not notarized: the first time,
right-click it and choose Open (on macOS 15 and later: open it once, then System Settings, Privacy &
Security, Open Anyway), or run: xattr -dr com.apple.quarantine "OpenGTA (gasm).app"'
      data_how='The first time, the app asks for your game data: "Choose your GTA folder..." and pick the
installed game (the folder with GTADATA and WINO) or the folder you unzipped GTAINSTALLER.zip to
(the one with data1.cab and data2.cab; there is no need to run the Windows setup).'
      change='Hold Option while opening the app to choose other data, or delete
  ~/Library/Application Support/OpenGTA/data-location
From Terminal: "OpenGTA (gasm).app/Contents/MacOS/OpenGTA" --help'
      saves='~/Library/Application Support/gasm/opengta/'
      logs='~/Library/Logs/OpenGTA/gasm.log' ;;
    linux-*)
      start='Run ./opengta.sh (from a terminal or your file manager). ./opengta.sh --install-desktop adds
a menu entry. gasm-run needs ALSA (libasound2, package libasound2t64 on newer Debian and Ubuntu) and a
Vulkan capable graphics driver.'
      data_how='Give it your game data once: ./opengta.sh /path/to/GTA (the installed game, the folder with
GTADATA and WINO) or ./opengta.sh /path/to/GTAINSTALLER (the folder you unzipped GTAINSTALLER.zip to,
with data1.cab and data2.cab; there is no need to run the Windows setup). Without an argument it opens
a chooser (zenity or kdialog) if one is installed.'
      change='./opengta.sh --change-data, or give it other data as the argument, or delete
  ${XDG_CONFIG_HOME:-~/.config}/opengta/data-location
./opengta.sh --help lists the options; anything after the data goes to gasm-run.'
      saves='~/.local/share/gasm/opengta/'
      logs='the terminal, or ~/.local/state/opengta/gasm.log when started from a menu' ;;
    windows-*)
      start='Double-click OpenGTA.cmd. (If Windows SmartScreen warns about gasm-run.exe or the script:
More info, Run anyway.)'
      data_how='The first time, it asks for your game data: pick the installed game (the folder with
GTADATA and WINO) or the folder you unzipped GTAINSTALLER.zip to (the one with data1.cab and
data2.cab; there is no need to run the setup). From a command prompt: OpenGTA.cmd C:\Games\GTA'
      change='OpenGTA.cmd --change-data, or delete %APPDATA%\OpenGTA\data-location.
OpenGTA.cmd --help lists the options; anything after the data goes to gasm-run.'
      saves='%APPDATA%\gasm\opengta\'
      logs='the console window, and %LOCALAPPDATA%\OpenGTA\gasm.log' ;;
  esac
  cat >"$1" <<TXT
OpenGTA $VERSION for gasm ($PLATFORM)
https://opengta.emdzej.pl

OpenGTA is a faithful, function-by-function reimplementation of Grand Theft Auto (DMA Design, 1997),
ported from the Windows build of the game. This bundle runs it as a WebAssembly module (opengta.wasm)
in the gasm runtime: gasm-run $GASM_VERSION is included (https://gasm.emdzej.pl). It is the same game
as the browser player.

OpenGTA is early work: see https://opengta.emdzej.pl/guide/status for what works.

You need your own copy of GTA from Rockstar's 2002 re-release (GTAINSTALLER.zip): the installed game
or the unzipped installer. None of the original game's files are included.

START
$start

YOUR GAME DATA
$data_how
The choice is remembered (the folder must still be there next time).

CHANGE THE GAME DATA
$change

CONTROLS (the original's keys)
  Arrows            menus: choose, change; in a level: walk / drive, turn / steer
  Enter             select; in a level: get into / out of a car
  Esc (tap)         back, the quit prompt (hold Esc for a second to quit gasm-run)
  Space             jump, handbrake
  Ctrl              fire
  X / Z             next / previous weapon
Launch options (after the data): --param intro=0 (no intro movie), --param mission=1 (start that
MISSION.INI section, no menus), --window 1280x960, --filter nearest, --mute.
Full guide: https://opengta.emdzej.pl/guide/

SAVES (player names, scores, options)
$saves

LOG
$logs

LICENSES
OpenGTA is GPL-3.0 (LICENSE$lic). gasm-run $GASM_VERSION is MIT (LICENSE-gasm$lic),
https://github.com/emdzej/gasm. Grand Theft Auto is (c) 1997 DMA Design / Rockstar Games; OpenGTA
contains no original code or assets and is not affiliated with them.
TXT
}

case "$PLATFORM" in
macos-*)
  APP="$OUT/OpenGTA (gasm).app"
  rm -rf "$APP"
  mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
  cp "$RUNDIR/gasm-run" "$APP/Contents/MacOS/gasm-run"
  fill "$SRC/launch-macos.sh" "$APP/Contents/MacOS/OpenGTA"
  chmod +x "$APP/Contents/MacOS/OpenGTA" "$APP/Contents/MacOS/gasm-run"
  cp "$WASM" "$APP/Contents/Resources/opengta.wasm"
  cp "$ROOT/LICENSE" "$APP/Contents/Resources/LICENSE"
  cp "$RUNDIR/LICENSE" "$APP/Contents/Resources/LICENSE-gasm"
  "$ROOT/tools/make-icns.sh" "$APP/Contents/Resources/opengta.icns"
  cat >"$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0"><dict>
  <key>CFBundleName</key><string>OpenGTA (gasm)</string>
  <key>CFBundleDisplayName</key><string>OpenGTA (gasm)</string>
  <key>CFBundleIdentifier</key><string>pl.emdzej.opengta.gasm</string>
  <key>CFBundleVersion</key><string>$VERSION</string>
  <key>CFBundleShortVersionString</key><string>$VERSION</string>
  <key>CFBundleGetInfoString</key><string>OpenGTA $VERSION on gasm-run $GASM_VERSION</string>
  <key>CFBundlePackageType</key><string>APPL</string>
  <key>CFBundleExecutable</key><string>OpenGTA</string>
  <key>CFBundleIconFile</key><string>opengta</string>
  <key>LSMinimumSystemVersion</key><string>11.0</string>
  <key>LSApplicationCategoryType</key><string>public.app-category.action-games</string>
  <key>NSHighResolutionCapable</key><true/>
  <key>NSHumanReadableCopyright</key><string>OpenGTA contributors, GPL-3.0; gasm-run MIT. Grand Theft Auto is (c) 1997 DMA Design / Rockstar Games.</string>
</dict></plist>
PLIST
  if command -v codesign >/dev/null; then
    codesign --force --sign - "$APP/Contents/MacOS/gasm-run"
    codesign --force --sign - "$APP"   # ad hoc (not notarized)
    codesign --verify --strict "$APP"
  fi
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  mkdir "$STAGE/$NAME"
  cp -R "$APP" "$STAGE/$NAME/"
  readme "$STAGE/$NAME/README.txt"
  cp "$ROOT/LICENSE" "$STAGE/$NAME/LICENSE"
  cp "$RUNDIR/LICENSE" "$STAGE/$NAME/LICENSE-gasm"
  rm -f "$OUT/$NAME.zip"
  if command -v ditto >/dev/null; then (cd "$STAGE" && ditto -c -k --norsrc --noextattr --noqtn --noacl --keepParent "$NAME" "$OUT/$NAME.zip")
  else (cd "$STAGE" && zip -qry "$OUT/$NAME.zip" "$NAME"); fi
  sha "$NAME.zip"
  echo "$APP"; echo "$OUT/$NAME.zip" ;;
linux-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run" "$D/"
  cp "$WASM" "$D/opengta.wasm"
  fill "$SRC/opengta.sh" "$D/opengta.sh"
  cp "$SRC/opengta-gasm.desktop" "$D/"
  python3 "$ROOT/tools/icon.py" 256 "$D/opengta.png"
  chmod 755 "$D/gasm-run" "$D/opengta.sh"; chmod 644 "$D/opengta.wasm" "$D/opengta-gasm.desktop" "$D/opengta.png"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm"
  if tar --version 2>/dev/null | grep -q GNU; then own=(--owner=0 --group=0 --numeric-owner); else own=(--uid 0 --gid 0 --no-xattrs --no-mac-metadata); fi
  COPYFILE_DISABLE=1 tar "${own[@]}" -C "$STAGE" -czf "$OUT/$NAME.tar.gz" "$NAME"
  sha "$NAME.tar.gz"
  echo "$OUT/$NAME.tar.gz" ;;
windows-*)
  STAGE=$(mktemp -d); trap 'rm -rf "$STAGE"' EXIT
  D="$STAGE/$NAME"; mkdir "$D"
  cp "$RUNDIR/gasm-run.exe" "$D/"
  cp "$WASM" "$D/opengta.wasm"
  fill "$SRC/OpenGTA.cmd" "$D/OpenGTA.cmd"; fill "$SRC/opengta.ps1" "$D/opengta.ps1"
  readme "$D/README.txt"
  cp "$ROOT/LICENSE" "$D/LICENSE.txt"; cp "$RUNDIR/LICENSE" "$D/LICENSE-gasm.txt"
  for f in OpenGTA.cmd opengta.ps1 README.txt LICENSE.txt LICENSE-gasm.txt; do crlf "$D/$f"; done
  rm -f "$OUT/$NAME.zip"
  (cd "$STAGE" && zip -qr "$OUT/$NAME.zip" "$NAME")
  sha "$NAME.zip"
  echo "$OUT/$NAME.zip" ;;
esac
