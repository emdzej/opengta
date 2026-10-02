#!/usr/bin/env bash
# Applies tools/ghidra/names.tsv to the Ghidra project and regenerates the grep dump re/all.c.
#   tools/ghidra/apply-names.sh [names.tsv]
set -euo pipefail
cd "$(dirname "$0")/../.."
NAMES=$(cd "$(dirname "${1:-tools/ghidra/names.tsv}")" && pwd)/$(basename "${1:-tools/ghidra/names.tsv}")
ghidra script run tools/ghidra/ApplyNames.java --project gta --program gta.exe -- "$NAMES"
mkdir -p re
ghidra script run tools/ghidra/DumpAllNamed.java --project gta --program gta.exe -- "$PWD/re/all.c"
grep -o '^// ==== [^ ]* @ [0-9a-f]*' re/all.c | awk '{print $5, $3}' | sort > re/funcs.txt
echo "re/all.c: $(wc -l < re/all.c) lines, $(wc -l < re/funcs.txt) functions"
