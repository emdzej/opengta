# Reverse-engineering workflow

The authority is the original program, `WINO/Grand Theft Auto.exe` of the 2002 release (774,144 bytes,
CRC-32 `a5ca070e`). It is stripped; every function from 0x401000 up to the C runtime (about 0x49cb00) now
has a name. Entry 0x49dc30, WinMain 0x437230.

## Rules

- **Fidelity.** Reproduce the original's behaviour, quirks and bugs
  included. If something looks wrong, check the disassembly; if the original does it, keep it and say so in
  a comment. Only fix deviations of the port from the original.
- **Cite the original.** Every ported function names its source: `/* Map_Load 0x438200 */`.
- **Own words.** Don't paste decompiler output into the source or the docs: describe layouts, formulas and
  addresses.
- **No data from the executable in the source.** Tables (vehicles, peds, weapons, objects, strings) are read
  from the exe at run time (`src/exe.c`). Small constants and instruction operands are fine.
- Prior art (Carnage3D, DMA's `cds.doc`) can inform the understanding of the formats; no code is copied,
  and the exe decides.

## Ghidra

The Ghidra project is `gta`, with the program `gta.exe` (a copy of `WINO/Grand Theft Auto.exe`), driven
from the shell with ghidra-cli (the `ghidra` command):

```sh
ghidra decompile 0x438200 --project gta --program gta.exe
ghidra x-ref 0x438200 --project gta --program gta.exe
ghidra disasm 0x438200 --project gta --program gta.exe
```

plus `memory` and `function rename`. ghidra-cli needs `java_home` set in its configuration (JDK 21):
without it the launcher waits on stdin.

A full dump for grepping goes to `re/` (git-ignored):

```sh
ghidra script run tools/ghidra/DumpAllNamed.java --project gta --program gta.exe -- "$PWD/re/all.c"
```

## Names

`tools/ghidra/names.tsv` is the record of every name: address, name, module, confidence, description.
Rename in the table as you learn, then apply it to the project and regenerate the dump:

```sh
tools/ghidra/apply-names.sh          # ApplyNames.java, then re/all.c and re/funcs.txt
```

Keep the names in the source consistent with the table. MSVC linked the objects in source order, so a
module is a contiguous address range; the [inventory](/re/) documents the ranges one by one.

## Checking formats

`tools/gtafmt.py` (Python 3, standard library only) has reference decoders and renderers for the data
formats, to check an understanding against the data before the C port:

```sh
python3 tools/gtafmt.py map game/GTADATA/NYC.CMP out/nyc.png 4
python3 tools/gtafmt.py tiles game/GTADATA/STYLE001.G24 out/tiles.png
```

## Then

Port the function, add or extend a test in `tests/` against the real data, update the matching page
(formats, renderer, frontend...) and check the gasm hashes ([Headless runs](/howto/headless)).
