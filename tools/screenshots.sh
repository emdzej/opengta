#!/usr/bin/env bash
# Regenerate the documentation screenshots (docs/public/screenshots/*.jpg) from opengta.wasm, run headless
# by the released gasm-run on your own game data. They are renders of our port, nothing else: never put
# the game's own files in docs/public/. Never opens a window (and never uses macOS screencapture).
#
#   tools/screenshots.sh [game data] [opengta.wasm]
# Defaults: ./game (the installed folder; the unzipped installer works too), build-gasm/opengta.wasm
# (build it first: AGENTS.md "gasm module"). Needs sips (macOS) to convert to JPEG.
set -euo pipefail
cd "$(dirname "$0")/.."
DATA=${1:-game}
WASM=${2:-build-gasm/opengta.wasm}
OUT=docs/public/screenshots
[ -f "$WASM" ] || { echo "no $WASM: build opengta.wasm first" >&2; exit 1; }
command -v sips >/dev/null || { echo "sips not found (macOS); convert $OUT/*.png yourself" >&2; exit 1; }
RUN="$(tools/fetch-gasm-runner.sh macos-universal)/gasm-run"
mkdir -p "$OUT"
tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT
cp "$WASM" "$tmp/opengta.wasm"   # a snapshot: other builds may replace build-gasm/opengta.wasm meanwhile

shot() {   # name frames [gasm-run options...]
  local name=$1 frames=$2; shift 2
  "$RUN" "$tmp/opengta.wasm" --asset-dir "$DATA" --headless "$frames" --screenshot "$tmp/$name.png" --param intro=0 "$@" \
    2>/dev/null | tr '\n' ' '
  sips -s format jpeg -s formatOptions 80 "$tmp/$name.png" --out "$OUT/$name.jpg" >/dev/null
  echo "-> $OUT/$name.jpg ($(($(wc -c < "$OUT/$name.jpg") / 1024)) KB)"
}

# The frontend (one key event per frame, as WinMain reads them).
shot menu       60
shot players   110 --input "60:KEY(Enter)"
shot cities    150 --input "60:KEY(Enter),100:KEY(Enter)"
shot options   200 --input "60:KEY(ArrowDown),70:KEY(ArrowDown),80:KEY(ArrowDown),100:KEY(Enter)"
shot credits   200 --input "60:KEY(Escape)"
# Mission 1 from the menus: Play, the player, Liberty City (the game then runs at 70 calls a second, a game
# frame every third), and the first San Andreas and Vice City sections straight from mission=.
shot mission1  400 --input "60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)"
shot intro     120 --param intro=1
shot driving  1150 --param mission=1 --input "100-160:KEY(ArrowRight),170-175:KEY(Enter),400-430:KEY(ArrowUp),430-480:KEY(ArrowRight+ArrowUp),480-1100:KEY(ArrowUp)"
shot mission-sanb   300 --param mission=102
shot mission-miami  300 --param mission=202
# The city viewer (front=0) in the three cities.
shot city-nyc    20 --param front=0 --param map=nyc   --param x=150 --param y=60 --param z=2
shot city-sanb   20 --param front=0 --param map=sanb  --param x=60  --param y=60 --param z=2
shot city-miami  20 --param front=0 --param map=miami --param x=60  --param y=60 --param z=2
