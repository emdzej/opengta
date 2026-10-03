#!/usr/bin/env bash
# Build the macOS app icon (.icns) of OpenGTA (gasm).app: the site's mark (docs/public/favicon.svg), drawn
# at every iconset size by tools/icon.py. Used by tools/package-gasm.sh. Needs macOS iconutil.
#   tools/make-icns.sh <out.icns>
set -euo pipefail
OUT=${1:?usage: make-icns.sh <out.icns>}
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP=$(mktemp -d); trap 'rm -rf "$TMP"' EXIT
SET="$TMP/opengta.iconset"; mkdir -p "$SET"
for s in 16 32 128 256 512; do
  python3 "$ROOT/tools/icon.py" "$s" "$SET/icon_${s}x${s}.png"
  python3 "$ROOT/tools/icon.py" $((s * 2)) "$SET/icon_${s}x${s}@2x.png"
done
iconutil -c icns "$SET" -o "$OUT"
