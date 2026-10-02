#!/usr/bin/env bash
# Put opengta.wasm into the site's play page (docs/public/play/opengta.wasm, git-ignored).
# CI runs it after building the module; locally it copies build-gasm/opengta.wasm.
#   docs/scripts/copy-wasm.sh [path/to/opengta.wasm]
# The module is the engine only: the game data always comes from the player's own copy of GTA.
set -euo pipefail
root="$(cd "$(dirname "$0")/../.." && pwd)"
src="${1:-$root/build-gasm/opengta.wasm}"
[ -f "$src" ] || { echo "no $src: build opengta.wasm first (docs/howto/build-from-source.md)" >&2; exit 1; }
magic="$(head -c 4 "$src" | od -An -tx1 | tr -d ' \n')"
[ "$magic" = 0061736d ] || { echo "$src is not a WebAssembly module" >&2; exit 1; }
cp "$src" "$root/docs/public/play/opengta.wasm"
echo "copied $(wc -c < "$src" | tr -d ' ') bytes -> docs/public/play/opengta.wasm"
