# Text, fonts, frontend images and PLAYER_A.DAT

How the original handles game text, bitmap fonts, the frontend's pictures and the settings file, and
where the port lives. Addresses are virtual addresses in `WINO/Grand Theft Auto.exe`. Everything was
checked against the 2002 release data in `game/GTADATA`.

| Port | Original |
|---|---|
| `src/text.c` | text module 0x47d680-0x47dbd0, UTF-8 helpers 0x486670-0x486760 |
| `src/font.c` | font module 0x4304a0-0x430970, frontend renderers 0x42b800-0x42d1c0, HUD renderers 0x4837a0-0x485910 |
| `src/surface.h` | the 32 bpp blitters of the blit module 0x4894a1-0x4897d3 |
| `src/front/images.c` | frontend images 0x42d490-0x42dc40, pictures of `Front_Enter` 0x42b690 |
| `src/front/front_text.c` | `Front_LoadFonts` 0x42d2e0, menu items and headings 0x42c340-0x42cf70, `Font_DrawCreditLine` 0x42c030 |
| `src/savedata.c` | `Front_LoadSettings` 0x42b4a0 (the file; the rest is in `src/front/front.c`, [frontend.md](frontend.md)) |

Tests: `tests/text_test.c` (all five FXT files), `tests/front_test.c` (renders to `out/front/*.png`).

## Surface and pixel format

`Surface` (`src/surface.h`) is `{uint32_t *px; int w, h, stride;}`. Each pixel is the bytes R, G, B, A in
memory, so a little-endian word reads `0xAABBGGRR`. This is what `plat_present` takes. The stride is
counted in pixels.

The original draws into a DirectDraw surface and converts every palette once into that surface's
format. Each channel gets `(c >> precision) << position`, with the shifts taken from the DirectDraw
masks by `Gfx_SetVideoMode` 0x414db0:

| Channel | Precision shift | Position shift |
|---|---|---|
| red | 0x775318 | 0x7750c8 |
| green | 0x7752e0 | 0x775528 |
| blue | 0x775520 | 0x7752dc |

Our format is fixed and keeps the full 8 bits per channel. Alpha is set to 0xff.

The game view can run at 16 or 32 bpp (0x50321c holds the bytes per pixel). The frontend always runs
at 640x480x16, in mode 0x110 (RGB555) or 0x111 (RGB565). On the original, frontend colours are
therefore quantised to 5 or 6 bits per channel.

There is also an 8-bit path, VESA mode 0x101, which uses the `.RAT` pictures and the `F8_CITY` fonts.
It is tested everywhere (`0x511590 == 0x101`), but `Gfx_SetVideoMode` never sets that mode. It is a
leftover of the DOS version and is dead code in this build.

Blitter destinations are linear offsets into the surface, matching the original's frame-buffer
pointers. Running past the end of a row lands on the next row, as in the original. A write outside
the buffer is dropped (the original would corrupt memory).

### Blitters

The source is 8-bit, colour 0 is transparent, and pixels go through a CLUT. In the original the CLUT
pointer 0x78c10c points into a paged style CLUT, where colour `i` sits at byte `i * 256`. Ours is a
linear array of 256 display colours.

| Function | Address | What it does |
|---|---|---|
| `Blit_Tile32` | 0x4894a1 | `w x h` block of a 256-byte-wide image |
| `Blit_Sprite32` | 0x4895bb | Packed `w x h` source (stride `w`), destination pitch 0x503220 |
| `Blit_SpriteClip32` | 0x489610 | Same, skipping `l` source columns on the left and `r` on the right; the first visible column lands at the destination |
| `Blit_ApplyDelta` | 0x48958a | Applies `{u16 skip, u8 n, n bytes}` records to a sprite page |
| `Blit_Tile` / `Blit_SpriteClip` / `Blit_Sprite` | 0x48974b / 0x489789 / 0x4897d3 | Dispatch on bytes per pixel. We always take the 32 bpp branch. The 16 bpp variants (0x489516, 0x489683, 0x4896d8) are not ported |
| `Blit_Copy8to16_640` / `..Clip_640` | 0x4898ac / 0x489904 | Frontend glyph blitters: the font palette 0x51156c, pitch fixed at 640. Ported as the 32 bpp sprite blitters with the font palette. `Blit_Copy8_640` / `..Clip_640` (0x489814 / 0x489851) are the dead 8-bit path |

Both the row loop (`loop` with ECX = h) and the column loop (EBX decremented and then tested) would
run 2^32 times for a zero size. The callers never pass one, and the port draws nothing in that case.

## FXT (language files)

Files: `ENGLISH.FXT`, `FRENCH.FXT`, `GERMAN.FXT`, `ITALIAN.FXT` and `SPECIAL.FXT` (English with
uncensored lines). `JAPANESE.FXT` is named in the exe but not shipped. The file names are at 0x4b34f8,
0x4b34e0, 0x4b34c8, 0x4b34b0, 0x4b3498 and 0x4b3480.

### Language selection

The language number is 0 English, 1 French, 2 German, 3 Italian, 4 Japanese, or 99 special.

- `Text_InitLanguage` 0x47d7f0 runs from WinMain. It reads the registry value `Language` (as a digit,
  through `Config_GetLanguageChar` 0x46e9c0), clears the loaded flag 0x775550 without freeing the
  buffer, and selects the file.
- `Text_SetLanguage` 0x47d680 only selects the file name (0x77555c) and the language (0x775554). It does
  not touch the loaded flag, so an already-loaded file stays in use. The port keeps this.
- Only Japanese sets the wide flag 0x775558. For the other languages the copy loop leaves the
  terminator, 0, in it.
- Any other number is fatal error -74.
- `PLAYER_A.DAT` +0x14 can override the language (see below).

### Decryption

`Text_Get` 0x47d9a0 loads the file on first use with `File_LoadAlloc` 0x42dce0. The buffer has the exact
file size and no terminator. Every byte is decrypted as `b -= k; k += m; m *= 2`, all mod 256, starting
with `k = 0x64` and `m = 0x63`:

- The key k runs 0x64, 0xc7, 0x8d, 0x19, 0x31, 0x61, 0xc1, 0x81.
- After eight bytes m has become 0, so every remaining byte is just decremented by 1.

The decrypted file must end in `[]`, otherwise fatal error -86 ("invalid format in text file").

### Contents

The decrypted text is a sequence of `[identifier]text\0` entries. Identifiers are digits (`1001`, `2500`,
`4003`, ...) or names (`car0`, `quit1`, ...). Every shipped file ends in `[]\x1a\0[]`.

Text is UTF-8. French, German and Italian use 2-byte sequences (é = c3 a9). English and special are
pure ASCII.

### Lookup

`Text_Get` scans linearly from the start of the file on every call:

1. Skip to the next `[`.
2. If `]` follows immediately, the table has ended. The key is missing: fatal error -85, "text identifier
   [%s] not found in language file".
3. Otherwise compare the identifier with the key, character by character, up to the `]`.
4. On an exact match (the key is exhausted at the `]`), return a pointer just after the `]`.
5. Otherwise resume step 1 from the character where the comparison stopped.

Consequences:

- A prefix or an extension of a key never matches.
- The empty key never matches.
- A `[` inside a text body would open an identifier.

The port returns NULL with `text_error()` where the original stops the program.

Known keys: 4003 "BUSTED!", 4004 "WASTED!", 2500 "MISSION COMPLETE!". French gives "ARRESTATION!" /
"REFROIDI!", German "VERHAFTET!" / "GETÖTET!", Italian "PRESO!" / "FATTO FUORI!".

### Character helpers

- `Text_MapChar` 0x47dab0 maps a character to the font's glyph code:
  - In every language, `a`-`z` becomes upper case.
  - In French and German, codes 0x80..0x100 go through the u16 table at 0x4b3280, indexed by code. This
    maps accented lower case to upper case (é to É). Code 0x100 reads past the table, a quirk the port
    keeps.
  - In Italian the range is 0x80..0xff.
  - In Japanese nothing is mapped.
- `Text_IsForeign` 0x47da90 is true unless the language is English or special.
- `Text_HasWideChars` 0x47dbd0 is true if a string contains any code >= 0x100.
- UTF-8 handling:
  - `Text_Utf8Next` 0x486670, `Text_Utf8Peek` 0x4866c0 and `Text_Utf8Skip` 0x486700 decode one code.
    A byte < 0x80 is the code itself. A larger lead byte starts 3 bytes if bit 5 is set (4+6+6 payload
    bits), otherwise 2 bytes (5+6). There is no validation.
  - `Text_Utf8Back` 0x486730 steps back 1 byte if the previous byte is ASCII. If the previous byte is a
    continuation byte, it steps back 2 when the byte before that has bit 6 set, otherwise 3.
  - `Text_ToUtf8Mapped` 0x486760 re-encodes a string through `Text_MapChar`.

## FON (bitmap fonts)

### File layout

Headerless, all little-endian bytes:

| Offset | Size | Field |
|---|---|---|
| 0 | u8 | glyph count n |
| 1 | u8 | height h (the same for all glyphs) |
| 2 | per glyph | u8 width w, then `w * h` bytes of 8-bit pixels, row by row, 0 = transparent |
| end - 768 | 768 | palette: 256 × R, G, B (8 bits per channel) |

Every shipped `.FON` file ends with the palette. Fonts used with a style palette, the HUD ones, are
loaded without reading it.

### `Font_Load` 0x4304d0

`Font_Load(&font, path, first, with_palette)` builds a 0x80c-byte record:

| Offset | Field |
|---|---|
| +0 | u8 count |
| +1 | u8 height |
| +2 | u16 first: the character code of glyph 0 |
| +4 | int kanji: 1 for the Japanese pseudo font |
| +8 | pointer to the 256-entry palette converted to the display format, or NULL |
| +0xc + 8i | u8 width of glyph i |
| +0x10 + 8i | pointer to the pixels of glyph i |

Errors are fatal: -202 on open, -203 on read.

### Glyph lookup

Glyph lookup uses the current font, 0x513228, selected by `Font_Select` 0x4307c0 or `Font_SelectRaw`
0x430840. In Japanese mode, `Font_Select` swaps text fonts (first code 0x21) for the kanji font (not
ported).

`Font_GlyphWidth` 0x430850:

- A space (0x20) uses the width of `n`, except in fonts whose first code is 1.
- A code >= 0x80 is first remapped through the u16 table at 0x4b0980, indexed by code. The table sits
  right after the string `..\gtadata\cuttext.fon` at 0x4b0974. It maps Latin-1 letters to glyph codes
  0x80..0xac, and everything else to 0.
- The index is `code - first`. An index outside 0..count-1 becomes glyph 0.

`Font_GlyphPixels` 0x4308d0 works the same way but without the space substitution. `Font_Height`
0x430930 returns the height of the current font.

### Fonts in use

**Frontend**, loaded by `Front_LoadFonts` 0x42d2e0, all with their palette:

| File | Handle | First code | Glyphs × height | Use |
|---|---|---|---|---|
| `CUTTEXT` | 0x511578 | 0x21 | 140 × 30 | Cutscene text |
| `F_MHEAD` | 0x511570 | 0x21 | 140 × 40 | Headings, main menu |
| `F_MMISS` | 0x51157c | 0x21 | 140 × 15 | Small text |
| `F_MTEXT` | 0x511568 | 0x21 | 140 × 30 | Menu text |
| `F_KEY` | 0x511574 | 1 | 13 × 72 | Animation frames of the selection marker |
| `F_CITY1..4` | 0x511154.. | 1 | 12-13 × 72 (F_CITY4: 64) | City logo animations |

`F8_CITYn` replaces `F_CITYn` on the dead 8-bit path.

**HUD**, loaded by `HUD_LoadFonts` 0x481030 with res 1 or 2, without palette:

| Files | First code | Glyphs × height (res 1 / res 2) |
|---|---|---|
| `BIG%d` | 0x21 | 118 × 35 / 140 × 65 |
| `SUB%d` | 0x21 | 140 × 12 / 21 |
| `STREET%d` | 0x21 | 140 × 12 / 21 |
| `PAGER%d` | 0x21 | 118 × 12 / 20 |
| `EXPSCOR%d` | 0 | 10 digits, 11 / 20 |
| `SCORE%d` | 0 | 10 digits, 11 / 20 |
| `MISSMUL%d` | 0 | 11 × 7 / 12 |

`HUD_LoadFonts` also leaves the pager font selected and computes the pager line length 0x784860 as
`res * 58 / width('n')`, which is 8 at res 1.

The `CUT*.FON` files, `C_ALLEY0.FON` and the `*8.FON` files are not loaded by these two functions.

### Frontend string renderers

They draw with the font's own palette, through the 640-wide blitters.

- **`Font_DrawString`** 0x42bad0 and **`Font_DrawStringAlt`** 0x42bc70 (the Alt version skips the
  Japanese substitution):
  - Each code advances x by its glyph width.
  - Spaces are not drawn.
  - Nothing is clipped.
- **`Font_StringWidth`** 0x42c0e0 and **`Font_StringWidthAlt`** 0x42c120 return the sum of the widths.
- **`Font_DrawCentered`** 0x42c160 draws at `x + max(0, (box_w - width) / 2)`.
- **`Font_DrawStringClipped`** 0x42be10 takes `(text, dst, skip, max_w, h, row0)` and draws the part of
  the string between pixels `skip` and `skip + max_w`:
  1. Walk to the glyph containing pixel `skip`.
  2. Draw its visible right part at dst.
  3. Draw whole glyphs while they fit.
  4. Draw the visible left part of the glyph that crosses `max_w`.

  Only glyph rows `row0 .. row0 + h - 1` are drawn. A negative skip makes the original step back before
  the string; the port draws nothing.
- **`Font_DrawCreditLine`** 0x42c030 clips a line vertically to the clip rectangle's rows: rows below
  `y1` are cut, rows above `y0` are skipped. It then calls the above with skip 0 and a window as wide as
  the pitch.
- **`Text_WordWrap`** 0x42b800 copies the text into the static buffer 0x511168 (0x400 bytes) while
  summing widths, and counts lines:
  - `\n` and `@` both break the line (`@` is written as `\n`).
  - When a code makes the line wider than the limit and the line has had a space, that space becomes
    `\n` and copying resumes after it. The rest of the line is copied a second time.
  - Otherwise `\n` is inserted before the code.
  - The overflow guard counts every byte written, including re-copied ones. It is fatal error -289
    ("String is too long for text wrap") past 0x3ff. The port truncates instead.
- **`Font_DrawWrapped`** 0x42d150, through `Font_DrawWrappedLines` 0x42d1c0, draws the wrapped lines,
  `height` rows apart.

### Menu layout helpers

| Function | Address | Font | Text x | Text y | Marker |
|---|---|---|---|---|---|
| `Front_DrawMenuItem` | 0x42c340 | heading | 196 | `216 + h*(n-1)` | at (116, y-16) |
| `Front_DrawMenuItemB` | 0x42c6b0 | menu text | 196 | `224 + h*(n-1)` | at (116, y-24) |
| `Front_DrawMenuItemC` | 0x42ca20 | menu text, no substitution | 196 | `224 + h*(n-1)` | at (116, y-24) |
| `Front_DrawTitle` | 0x42cd90 | heading | `320 - width/2` | given | none |
| `Front_DrawTitleAlt` | 0x42cf70 | menu text | `320 - width/2` | given | none |

The marker is a one-character string `frame + 1` in `F_KEY`. Each item variant has its own frame
counter (0x511584, 0x511588, 0x51158c), which steps through 0..12 on every draw of a selected item.

### HUD string renderers

They draw with style palettes. The palette in use is the original's current CLUT 0x78c10c:

- `Tile_SelectAux(n)` 0x437730 picks entry `n` of the table that `Tile_BuildAuxTable` 0x4376f0 builds at
  0x5c2c4c. That table holds the style's font palettes: logical palettes `font_base + n`, where
  `font_base` = (tile + sprite + new-car CLUT sizes) / 1024 is stored at 0x7752f8.
- `Tile_SelectSprite(0)` 0x4385b0 picks the palette of sprite 0.

The port takes these palettes from the style owner through `font_set_hud_cluts`.

| Function | Address | What it does |
|---|---|---|
| `HUD_DrawText` | 0x483df0 | Palette aux 0 or 1. Skips spaces and zero-width glyphs |
| `HUD_DrawCenteredLine` | 0x4837a0 | Sub font, centred on the view width 0x5c0c00, aux 0 |
| `HUD_DrawTextClipped` | 0x4854a0 | The `Font_DrawStringClipped` algorithm for the pager. Palette: street or sub font aux 0, pager font aux 3, any other font the sprite 0 palette |
| `HUD_DrawTextMultiline` | 0x485910 | `\n` moves down `height - 1` rows. A zero-width glyph does not advance |

## RAW and RAT pictures

Both are headerless. The caller supplies the size.

- **`.RAW`** is 24-bit, R, G, B per pixel, row by row, top first.
- **`.RAT`** is 8-bit indices into a separate palette. It is only used by the dead 8-bit path.

`Gfx_LoadRawImage` 0x42d5f0 builds the file name with `%s.raw` (0x4b098c) or `%s.rat` (0x4b0994) and
treats the formats differently:

- **RAW**: read with `fgetc`, three bytes per pixel, each converted to the display format.
- **RAT**: one `fread`, then, if the invert argument is set, every byte becomes `255 - v`. Only cutscene
  stills set it.

### Pictures in use

| Picture | Size | Loaded by |
|---|---|---|
| `F_UPPER` | 640 × 168 | `Front_LoadImages` 0x42da60 |
| `F_LOGO0..7` | 640 × 168 | `Front_LoadImages` |
| `F_LOWER0`, `F_LOWER1` | 640 × 312 | `Front_LoadImages` |
| `CUT0..5` | 640 × 480 | `Front_LoadCutsceneBg` 0x42d810 (names at 0x4af524) |
| `F_PLAY1..8` | 102 × 141 | `Front_Enter` 0x42b690 (path table 0x4a7390 of {path, name} pairs) |
| `F_PLAYN` | 180 × 50 | `Front_Enter` (name plate) |
| `F_RSTAR`, `F_RSTARN` | 64 × 59 | `Front_Enter` |

All file sizes match exactly. For example, 322,560 = 640 × 168 × 3, and the `.RAT` files are a third of
their `.RAW`.

### Relation between RAT and RAW

Each `.RAT` is the `.RAW` quantised to a 256-colour palette:

- `F_PAL.RAW` (768 bytes, RGB) for the `F_*` pictures.
- `CUTn.ACT` (768 bytes) for the `CUTn` stills.

The files are stored without inversion. Sampled against the RAW, the mean error is about 18-37 per
pixel for the `F_*` pictures and 4-9 for the cuts. With `255 - v` it is about 400.

The Windows exe references none of `F_PAL.RAW`, `CUTn.ACT`, `F_BMG.*`, `F_DMA.*` or
`F_RSTAR white.RAW`. They are leftovers. `gfx_load_rat_image` / `gfx_load_palette` exist only to look
at them.

### Drawing

- **`Gfx_BlitImage`** 0x42d490 is an opaque copy inside the clip rectangle. The rectangle is set by
  `Gfx_SetClipRect` 0x42d590 and stored at 0x511920 (x0, y0, x1, y1); `Front_LoadImages` resets it to
  0, 0, 0x27f, 0x1df. The copy happens only if `x < x1` and `x + w >= x0`. Quirks:
  - An image starting left of x0 is copied to column 0, not to x0, and its right edge is not clipped.
  - An image reaching x1 stops at column `x1 - 1`, so with the usual rectangle column 639 is never
    drawn.
  - Only rows `y0 <= row < y1` are drawn, so row 479 is never drawn either.
- **`Front_DrawBackground(animate, lower)`** 0x42d8e0 draws rows 0-167 and 168-479:
  - Rows 0-167 show the current logo frame (`animate`) or `F_UPPER`.
  - Rows 168-479 show `F_LOWER<lower>`.
  - The first call sets the due time to `clock() + 0x53`, i.e. 83 ms.
  - With `animate`, once `clock()` passes the due time the frame (0x511950) advances through 0..7 and
    the next frame is due 83 ms later.
  - The port takes the clock as a parameter.
- **`Front_ClearOrDrawBg(draw)`** 0x42d830 either clears every row or copies the 640 × 480 background
  0x511930.

## PLAYER_A.DAT

0x414 bytes, kept at 0x510298. `Front_LoadSettings` 0x42b4a0 opens `..\gtadata\player_a.dat` (pointer
0x4af400) and freads 0x414 bytes. If that fails it builds the defaults and writes the file back; a
failed write is fatal (-202 / -204). The file is saved again after a game (`Front_SetScreen` 0x427030)
and from the options screen (0x429180).

In the port, the user's file (`plat_load_user_file("PLAYER_A.DAT")`) comes first, then the shipped
`GTADATA/PLAYER_A.DAT`. A short read leaves its bytes in the buffer, with the defaults written over
them, as in the original. Saving always goes to the user file.

### Settings

| Offset | Type | Field | Default |
|---|---|---|---|
| +0x00 | s8 | sfx volume 0..7 | 4 |
| +0x01 | s8 | music volume 0..7 | 6 |
| +0x02 | s8 | pager speed 1..3 (`HUD_SetPagerSpeed` 0x482140) | 2 |
| +0x03 | s8 | music sequential / radio mode | the demo flag 0x5031bc |
| +0x04 | s8 | display mode index (`Gfx_ValidateModeIndex` 0x414c70) | 0 |
| +0x05 | s8 | (unknown) | 0 |
| +0x06 | s8 | (unknown) | not set |
| +0x07 | s8 | effects, copied to 0x5031e4 (sprite rasteriser option) | 1 |
| +0x08 | s8 | network game target: 0 score, 1 kills | 0 |
| +0x0c | s32 | score target | 100000 |
| +0x10 | s32 | kill target | 10 |
| +0x14 | s8 | language override. Not -1 means `Text_SetLanguage(v)` after loading; the rename screen's cheat names set 99 (special) or -1 | -1 |
| +0x18 | 6 × 3 × {s32 score; char name[16]} | high scores per level, ascending ([2] is the best) | scores 1, 2, 3 (0x4a73d0) for BILLY, PAUL, STEVE (pointers 0x4af404) |
| +0x180 | 8 × 0x50 | player records | |
| +0x400 | u8 | current player | 1 |
| +0x401 | char[19] | network name | "GTA Game" (0x4b080c) |

### Player record

0x50 bytes per record.

| Offset | Type | Field | Default |
|---|---|---|---|
| +0x00 | char[16] | name | from 0x4a7394 + 8i: Ulrika, Travis, Katie, Mikki, Divine, Bubba, Troy, Kivlov |
| +0x10 | s32[6] | best score per level, -1 = locked | 0, -1, -1, -1, -1, -1 |
| +0x28 | s32[2] | selected chapter: [0] single player, [1] network (index 0x5110fc) | 0 |
| +0x30 | s32[2] | selected mission in that chapter | 0 |
| +0x38 | s32[6] | level intro cutscene already shown | 0 |

Completing level `i < 5` unlocks level `i + 1`: its `best` value goes from -1 to 0.

Names are copied with `strcpy`, so the bytes after the terminator keep old contents. The shipped file
shows the remains of longer names there.

### Shipped file

sfx 4, music 3, pager 3, mode 6, language -1, player 1 (Travis, best 12640). Level 0 high scores are
210, 290 and 12640 (Travis); the other levels hold the defaults.

## Not ported here

- The kanji font (`Font_InitKanji` 0x4304a0, `Kanji_*` 0x433d50).
- The HUD sprite setup in `HUD_LoadFonts`.

The frontend's screens that use these routines, with `Front_DrawEnterPrompt` 0x426fd0,
`Front_DrawPlayerPortrait` 0x429f60, `Front_DrawHighScores` 0x427fc0 and `Front_LoadTexts` 0x426320,
are in [frontend.md](frontend.md).
