#!/usr/bin/env bash
# Check a gasm bundle from tools/package-gasm.sh without game data (CI runs it on each bundle's own OS):
#   tools/smoke-gasm-bundle.sh <opengta-gasm-...-macos-universal.zip | ...-linux-<arch>.tar.gz>
# Unpacks it, checks the files, runs the bundled gasm-run without data (it must stop with OpenGTA's "game
# data not found" message and exit code 1, not crash), and the launcher's --help and --dry-run paths against
# fake data (an installed folder with a 774,144-byte WINO/Grand Theft Auto.exe, an unzipped installer folder,
# and folders that must be refused; no dialogs: OPENGTA_DATA, --dry-run, a scratch HOME).
# With OPENGTA_SMOKE_DATA=<installed GTA folder | unzipped installer folder> it also runs the game through the
# launcher (headless, the menus without the intro movie) and checks that its hash line equals a direct
# gasm-run on the same module and parameters (the bundled gasm-run, or SMOKE_GASM_RUN=<another gasm-run>).
set -euo pipefail
ARCHIVE=$(cd "$(dirname "$1")" && pwd)/$(basename "$1")
T=$(mktemp -d); trap 'rm -rf "$T"' EXIT
fail() { echo "FAIL: $*" >&2; exit 1; }
case "$ARCHIVE" in
  *.zip) (cd "$T" && unzip -q "$ARCHIVE") ;;
  *.tar.gz) tar xzf "$ARCHIVE" -C "$T" ;;
  *) fail "unknown archive $ARCHIVE" ;;
esac
DIR=$(find "$T" -mindepth 1 -maxdepth 1 -type d -name 'opengta-gasm-*' | head -n 1)
[ -n "$DIR" ] || fail "no opengta-gasm-* folder in the archive"
for f in README.txt LICENSE LICENSE-gasm; do [ -s "$DIR/$f" ] || fail "missing $f"; done
if [ -d "$DIR/OpenGTA (gasm).app" ]; then
  APP="$DIR/OpenGTA (gasm).app"
  RUN="$APP/Contents/MacOS/gasm-run"; WASM="$APP/Contents/Resources/opengta.wasm"; L="$APP/Contents/MacOS/OpenGTA"
  codesign --verify --strict "$APP" || fail "code signature"
  [ "$(/usr/libexec/PlistBuddy -c 'Print CFBundleIdentifier' "$APP/Contents/Info.plist")" = pl.emdzej.opengta.gasm ] ||
    fail "bundle id"
  [ -s "$APP/Contents/Resources/opengta.icns" ] || fail "icon"
  lipo -info "$RUN"
else
  RUN="$DIR/gasm-run"; WASM="$DIR/opengta.wasm"; L="$DIR/opengta.sh"
  [ -s "$DIR/opengta.png" ] || fail "icon"
  [ -s "$DIR/opengta-gasm.desktop" ] || fail "desktop entry"
fi
if [ ! -x "$RUN" ] || [ ! -s "$WASM" ] || [ ! -x "$L" ]; then fail "gasm-run, opengta.wasm or the launcher missing"; fi
grep -q "gasm-run [0-9]" "$DIR/README.txt" || fail "README does not name the gasm version"
grep -q "GTAINSTALLER.zip" "$DIR/README.txt" || fail "README does not name the re-release"
if grep -qi "freeware" "$DIR/README.txt"; then fail "README calls the game freeware"; fi

# 1. No data: a clean error from the game, not a crash.
set +e
out=$(cd "$T" && "$RUN" "$WASM" --headless 5 --mute 2>&1); rc=$?
set -e
echo "$out"
[ "$rc" = 1 ] || fail "gasm-run without data: exit $rc, expected 1"
echo "$out" | grep -q "game data not found" || fail "no 'game data not found' message"

# 2. Launcher: help, and the command it builds (no dialogs, scratch HOME so nothing real is touched).
REAL_HOME=$HOME
export HOME="$T/home" XDG_CONFIG_HOME="$T/home/.config" XDG_STATE_HOME="$T/home/.local/state"
mkdir -p "$HOME"
"$L" --help | grep -q "OpenGTA" || fail "--help"
# fakes: the installed game (any letter case, a space in the path), the unzipped installer, and wrong ones
G="$T/My GTA"; mkdir -p "$G/gtadata" "$G/Wino"
: >"$G/gtadata/mission.ini"
dd if=/dev/zero of="$G/Wino/grand theft auto.EXE" bs=774144 count=1 2>/dev/null
mkdir -p "$T/installer" "$T/half" "$T/empty" "$T/wrongexe/GTADATA" "$T/wrongexe/WINO" "$T/noini/GTADATA" "$T/noini/WINO"
: >"$T/installer/Data1.CAB"; : >"$T/installer/data2.cab"; : >"$T/half/data1.cab"
: >"$T/wrongexe/GTADATA/MISSION.INI"; printf 'MZ' >"$T/wrongexe/WINO/Grand Theft Auto.exe"
cp "$G/Wino/grand theft auto.EXE" "$T/noini/WINO/Grand Theft Auto.exe"
: >"$T/GTAINSTALLER.zip"
OPENGTA_DATA="$G" "$L" --dry-run --param intro=0 | tee "$T/cmd"
{ grep -q -- "--asset-dir" "$T/cmd" && grep -q "My" "$T/cmd" && grep -q "intro=0" "$T/cmd"; } || fail "dry run with an installed folder"
OPENGTA_DATA="$T/installer" "$L" --dry-run | grep -q -- "--asset-dir" || fail "dry run with an installer folder"
for bad in half empty wrongexe noini GTAINSTALLER.zip missing; do
  set +e
  OPENGTA_DATA="$T/$bad" "$L" --dry-run >/dev/null 2>"$T/err"; rc=$?
  set -e
  [ "$rc" = 2 ] || fail "$bad accepted (exit $rc, expected 2)"
  echo "refused $bad: $(tr '\n' ' ' <"$T/err")"
done
grep -q "774,144" <(OPENGTA_DATA="$T/wrongexe" "$L" --dry-run 2>&1 || true) || fail "no exe size message"
# the data as an argument (saved, but not by a dry run), then --forget-data
"$L" --dry-run "$T/installer" --mute | grep -q -- "--mute" || fail "dry run with the data as an argument"
if "$L" --dry-run </dev/null >/dev/null 2>&1; then fail "no data, dry run: should exit non-zero"; fi
LOCS=("$XDG_CONFIG_HOME/opengta/data-location" "$HOME/Library/Application Support/OpenGTA/data-location")
for f in "${LOCS[@]}"; do [ ! -e "$f" ] || fail "a dry run saved the data location"; done
"$L" --forget-data | grep -q "forgot" || fail "--forget-data"

# 3. Optional: the real game with the user's data, through the launcher, against a direct gasm-run.
if [ -n "${OPENGTA_SMOKE_DATA:-}" ]; then
  DATA=$(cd "$OPENGTA_SMOKE_DATA" && pwd)
  REF=${SMOKE_GASM_RUN:-$RUN}
  case "$REF" in /*) ;; *) REF="$PWD/$REF" ;; esac
  ARGS=(--headless 400 --param intro=0 --mute)
  OPENGTA_DATA="$DATA" "$L" "${ARGS[@]}" >"$T/launch.out" 2>&1 || fail "the launcher's run failed: $(tail -n 3 "$T/launch.out")"
  # the launcher logs when there is no terminal (always on macOS)
  cat "$T/launch.out" "$HOME/Library/Logs/OpenGTA/gasm.log" "$XDG_STATE_HOME/opengta/gasm.log" 2>/dev/null >"$T/launch.all" || true
  HOME=$REAL_HOME "$REF" "$WASM" --asset-dir "$DATA" "${ARGS[@]}" >"$T/direct.out" 2>&1 || fail "the direct run failed"
  grep -q "game data from" "$T/launch.all" || fail "the game did not find the data"
  a=$(grep -E "^video_fnv32=" "$T/launch.all" | tail -n 1); b=$(grep -E "^video_fnv32=" "$T/direct.out" | tail -n 1)
  echo "launcher: $a"; echo "direct:   $b"
  grep -q "frames=400 " "$T/launch.all" || fail "the game did not run 400 frames"
  if [ -z "$a" ] || [ "$a" != "$b" ]; then fail "the launcher's hashes differ from gasm-run's"; fi
fi
echo "PASS $(basename "$ARCHIVE")"
