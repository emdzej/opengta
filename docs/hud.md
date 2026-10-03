# The in-game HUD

What the original draws over the city, where, with which fonts and palettes, and how long things
stay; and where the port lives. Addresses are virtual addresses in `gta.exe`.

| Port | Original |
|---|---|
| `src/hud/hud.c` | the hud module 0x481030-0x486830 except the pager and the text renderers |
| `src/hud/pager.c` | the pager: 0x482140-0x482fb0, `HUD_DrawPager` 0x485c30, `Pager_Update` 0x486430 |
| `src/hud/area.c` | area names, the area module 0x44b4a0-0x44b7a0 (no other owner yet) |
| `src/font.c` | the HUD fonts (the font half of `HUD_LoadFonts`) and the string renderers `HUD_DrawText` 0x483df0, `HUD_DrawCenteredLine` 0x4837a0, `HUD_DrawTextClipped` 0x4854a0, `HUD_DrawTextMultiline` 0x485910 ([text-fonts.md](text-fonts.md)) |

Test: `tests/hud_test.c` (renders to `out/hud/*.png`).

## When it runs

- `HUD_Init` 0x483010 in `Game_Init` (before the mission loads): everything off, the FXT strings
  `paused`, `target_score`, `missions-comp`, `secrets_found` fetched.
- `HUD_LoadFonts` 0x481030 from `Gfx_SelectMode` 0x414cc0, after `Game_Init`, with res 2 when the
  mode is taller than 400 lines (so 640x480 uses `BIG2.FON` etc.), 1 otherwise. It does nothing if
  res hasn't changed, so the HUD sprites are set up only for the first level of a resolution.
  `Gfx_SetVideoMode` (and `Gfx_Present`) store the screen size with `HUD_SetViewSize` 0x486830
  (0x785170 width, 0x785174 height).
- `HUD_Update` 0x485d70 near the end of `Game_Update` (only while the game runs; while paused by a
  frame count only `HUD_TickBigMessage` 0x486640 runs).
- `HUD_Draw` 0x483390 at the end of `Game_Render`, after `Render_DrawCity` (which draws the sprites
  between the layers), into the same back buffer. Nothing is drawn with the option 0x502f70.
- `HUD_HandleKey` 0x482d00 gets every key first (`Game_HandleKey` 0x430dc0); `HUD_WantsKey`
  0x482c80 tells `Input_ReadControls` which keys not to treat as controls.
- `HUD_FreeFonts` 0x4832c0 in `Game_Shutdown`.

## The back buffer and the palettes

The game view is drawn into the 32 bpp DirectDraw back surface: X8R8G8B8, a word reads
`0x00RRGGBB`. The port's back buffer is the same (`src/app.c`, `poly_set_screen_rows`), and
`Gfx_Present` converts it to the platform's R, G, B, A bytes. The HUD reaches it as the original
does: the row table 0x503228 (`poly_rows`), the pitch in pixels 0x503220, linear pixel offsets.
`hud_draw` wraps it in a `Surface` (width = the view width 0x5c0c00) for font.c's renderers.

The frontend's `Surface` holds R, G, B, A bytes (`0xAABBGGRR` read as a word). The HUD's surface does
not: it views the X8R8G8B8 back buffer. The two still work together because no blitter converts
colours. They store CLUT words as they are, and the HUD's CLUTs come from the style after
`Style_ConvertPalettes` with `PIXFMT_32` (0x00RRGGBB). So `surface_rgb` / `surface_from_xrgb` must
never be used for HUD colours.

Palettes (the current CLUT 0x78c10c):

- `Tile_SelectAux(n)` 0x437730: font palette n of the style, from the table built by
  `Tile_BuildAuxTable` 0x4376f0 at 0x5c2c4c. Entry n is logical palette `font_base + n` through the
  palette index, where `font_base` 0x7752f8 = (tile + sprite + new car CLUT) / 1024.
  - 0 is the plain text colour.
  - 1 the highlight (zone texts, armour count, unselected video modes).
  - 3 the pager text.
  - 4-7 the player colours. `Player_GetColourIndex` 0x461710 reads them from the table 0x4b21dc
    (4, 5, 6, 7 for players 0-3).
- `Tile_SelectSprite(0)` 0x4385b0: the palette of sprite 0, for fonts the clipped renderer doesn't know.
- Sprites drawn with `Sprite_DrawScreen` 0x47bbc0 use their own palette, unremapped.
- The arrows use palette `font_base + 1`, remapped to a player colour (`Tile_SelectSpriteRemap`:
  `font_base + colour`).

The original's CLUTs are paged (colour e of a palette is 64 words after colour e - 1). font.c's
renderers take linear 256-colour palettes, so `hud_draw` copies the 8 font palettes and the sprite 0
palette out of the style every frame (`font_set_hud_cluts`). That also follows a re-conversion after a
video mode change. `Poly_DrawRect` (the score popups) still gets the paged pointer through `poly_clut`.

## Sprites

HUD sprite n is sprite number `arrow base 0x774efa (0) + 0x785120 + 0x785124 + n`. At res 2,
0x785120 = 0x18 and 0x785124 = 1, so the hi-res set starts 0x19 further on. The exceptions are the
arrows and the roof marker (frame `base + 0x785120`) and the street sign (`base + 0x785120 + 1`, and
`+ 2` for its second half at res 2).

| n | What |
|---|---|
| 0 (frame base + 0x785120) | arrow (pointer to a target, other players, the roof marker) |
| 1 (+ 2 at res 2) | street sign (area names) |
| 2 | car sign (car names) |
| 3, 4 | pager, its light |
| 5-8 | weapons 1-4 |
| 10-15 | subtitle icons: kind 1, 0, 2, 4, 3, 5 |
| 0x10, 0x11 | cop head, flashing frame |
| 0x12 / 0x13 | armour (blinking pair) |
| 0x14 / 0x15 | get out of jail free |
| 0x16 / 0x17 | speed-up |

Blinking items alternate on a counter that counts down from 5: on for 3 frames of 5
(`counter == 0` or `counter > 2`). The frenzy weapon is hidden in the off frames.

## Fonts

From `HUD_LoadFonts`; the files and sizes are in [text-fonts.md](text-fonts.md).

| Font | Use |
|---|---|
| BIG | big messages |
| SUB | zone texts, subtitles, ammo, timers, quit prompt, pause screen, video menu, car names |
| STREET | area names in the street sign |
| PAGER | the pager |
| SCORE | the score digits; frags in network games |
| MISSMUL | lives and multiplier (glyphs 0-9 and 10, the "x") |
| EXPSCOR | score popups |

Text is selected with `Font_SelectRaw` except where a string has wide characters (Japanese only, not
shipped; the kanji colour overrides at 0x513705 are not ported).

## Layout (res 2 numbers for 640 x 480)

Coordinates are from the top left. `vw`, `vh` are the view size (0x5c0c00, 0x5bfab0) and `sw` the
screen width (0x785170). In single player all three are 640 / 480. `res` is 1 or 2.

### Top left: pager

`HUD_DrawPager` draws:

- the pager sprite at (0, 0);
- the text in a window at (11, 6) * res, 58 * res wide;
- the light (sprite 4) at (11, 19) * res while it is lit.

### Top left, under the pager while it shows: weapon and items

`HUD_DrawWeaponInfo` 0x484190 starts at y = the pager sprite height while the pager shows, else 0.

1. The weapon icon at (0, y). The ammo (`"%02.2d"`) is drawn in its corner, at (icon w - dx,
   icon h - dy + y). dx, dy are, times res: (13, 9) for the pistol, (12, 10) for the machine gun,
   (13, 7) for the rocket launcher, (16, 10) for the flame thrower. A temporary (frenzy) weapon has
   ammo 'd' (100) and shows its timer / 25 instead, blinking.
2. One row lower (+ icon h + 1): the item icons, side by side, 1 pixel apart.
   - Jail card.
   - Armour, with its count at (icon w - 5 res, icon h - 5 res) in aux 1.
   - Speed-up.
3. Below the tallest item: the two mission timers (+0x1ae, +0x1b0 of the player, in frames; shown /
   25 when not -1), one sub font line each.

### Top centre: wanted level, then the zone texts

`HUD_DrawWanted` 0x485680 draws one cop head per wanted level (1-4; anything else is fatal -0x4a):

- They are centred on the screen width at y = 0, w + 1 apart.
- Each head switches between sprites 0x10 and 0x11 every second frame. Each has its own counter
  (0x77e7e4, initially 2) and phase (0x77e7bc).

`HUD_DrawZoneTexts` 0x484de0 draws the three slots from the last to the first, stacked down from
y = 10 res (while heads show) or 0, each advancing by its height + 2:

| Type | Look |
|---|---|
| 2: area name | Street sign centred (at res 2 its two halves side by side), the text in the street font 2 res lower, aux 0 |
| 200: car name | Car sign centred, the text in the sub font 3 res lower, clipped to the sign's width - 6 res |
| others | The sub font, centred, aux 1 |

### Top right: score, lives, multiplier

`HUD_DrawScore` 0x484750 draws per player, at row `score height * player`:

- **Score digits.** The 9 digits are right-aligned to the view width, each `width('0')` apart.
  - Leading zeros are not drawn. A digit rolling in still is.
  - The digits roll. `Player_UpdateScoreDigits` 0x462ab0 compares the shown digits (+0x127) with
    `"%09d"` of the score (+0x131). Each digit that differs, or is still moving, adds 2 to its
    counter (9 shorts at +0x13c). Past 15 the counter resets and the digit steps up by one (9 wraps
    to 0).
  - While rolling, the digit is drawn shifted down by `counter * h / 16` and cut at the bottom; the
    next digit's bottom rows come in above it.
- **Lives.** Two digits (`"%02d"`, `"::"` when infinite) and the "x" glyph, on the score's top row, to
  the left of the shown digits. They use the colour of player 1 (aux 5).
- **Multiplier.** Two digits (the tens only if not 0) and "x", 6 res rows lower (4 res with more
  players), in the player's colour. Player 2's colour (aux 6) is used when the player's +0x01 byte is
  set.
- With more than one player the frags are shown in the score font instead of the lives.

Quirk: the lives' tens digit is placed by the multiplier's width.

### Bottom left: subtitles

`HUD_DrawSubtitle` 0x4857a0 draws:

- the icon of the kind at (0, vh - icon h);
- the text word-wrapped (`Text_WordWrap`) to `sw - icon w - 2`, starting at x = icon w + 2. Its last
  line ends at the bottom: the block starts at `vh - (h - 1) * lines`, from the row table entry
  before (0x503224), so it is one row higher. Lines are h - 1 apart.

### Centre

- **Big messages** (`HUD_DrawBigMessage` 0x483890): 1-3 lines in the big font, each centred. The
  block starts at `(vh - lines * (h + 1)) / 4` (a quarter down) and the lines are h + 1 apart.
- **Quit prompt**: `quit1`..`quit3` in the sub font, centred, starting at `(vh - 3h - 3) / 2`,
  lines h + 1 apart.
- **Pause screen** (`HUD_DrawPauseInfo` 0x483e90): `paused` at `(vh - 3 (h + 1)) / 2`, and 2 (h + 1)
  lower one of these, each for 40 frames in turn:
  - `target_score` with the mission's target (`"%s %d"`);
  - `missions-comp` with passed / total (`"%s %d/%d"`);
  - `secrets_found` with found / total.

  It is not drawn while the quit prompt or the video menu is open.
- **Video menu** (`HUD_DrawVideoMenu` 0x485a20): the mode names in up to 3 columns (one per colour
  depth). A column is as wide as the first name + 16, the block is centred, rows are sub height + 2
  apart. The selected mode uses aux 0, the others aux 1.

### In the world

- **Arrow** (`HUD_UpdateArrow` 0x486220). It sits on the line from the target to the player's focus
  (`Player_GetViewFocusPos` 0x462e10), pointing along it.
  - Its distance from the focus is `camera height / 4 + size + dist`. The size is 0x20 for the arrow,
    0x26 for the red arrow and `0x26 + 6 * player` for the other players' arrows.
  - While the target is inside the view rectangle, dist moves 8 pixels a frame towards
    `max(|dx|, |dy|) - size` (past it, it is set). Outside, dist shrinks by 8 down to 0.
  - It hides while the target is exactly at the focus.

  Target types (0x784744):

  | Type | Target |
  |---|---|
  | 0 | car |
  | 1 | point |
  | 2 | none |
  | 3 | ped (`HUD_ArrowToObj` sets it, but the position comes from `Ped_GetPosRect`) |
  | 4 | player |

  A queued target (0x784238) would take over when the target is reached, but only `HUD_Init` writes
  it (type 2), so that never happens.
- **Red arrow**: the same, with its own record (0x785108) and the colour of player 2.
- **Roof marker**: the arrow sprite at the focus, turned half a turn from the player's heading. It
  shows while `Map_IsCovered` 0x438800 finds a lid above the focus.
- **Score popups** (`HUD_AddScorePopup` 0x481540 / `HUD_DrawScorePopups` 0x481620): 32 slots. The
  value is drawn with the EXPSCOR digits on `Poly_DrawRect` at the projected world position, in the
  player's colour.
  - The first 4 frames at natural size.
  - Then it grows 6 pixels a frame until past 99. Each digit's rectangle starts at `i * w` but ends at
    `(i + 1) * (w + grow)`.

## Timers (frames of the 23.3 Hz game)

| What | How long |
|---|---|
| Zone text | type 1: 90 frames, 3: 270, any other: 45. The same type and text again only restarts the timer |
| Big message | 5 frames per byte of the text |
| Subtitle | `(4 / speed) * (len + 25) / 2` frames, speed = 0x4b3610 (1, 2 or 4 for the pager speeds 1-3; 0 would divide by zero) |
| Pager scroll | 0x4b3614 pixels a frame (1, 1, 2, 3) |
| Pager light | toggles every 5 frames |
| Countdown | a second every 25 frames |
| Pause line | changes every 40 frames |

`HUD_SetPagerSpeed` 0x482140 takes 0..3; other values are fatal -0x4a. The frontend passes the
PLAYER_A.DAT setting 1..3. The initial values (before any call) are read from the exe's data at
0x4b3610 / 0x4b3614.

## Zone texts (3 slots of 0x88 bytes at 0x784080)

The record:

| Offset | Field |
|---|---|
| +0 | timer |
| +4 | active |
| +8 | type |
| +0xa | width |
| +0xc | resolution the width was measured at |
| +0xd | text (up to 0x78 bytes; longer is fatal -0x82) |
| +0x86 | wide flag |

The functions:

- `HUD_ShowZoneText` takes the first free slot. With none free, the text is dropped.
- `HUD_ClearZoneText` frees all slots of a type.
- `HUD_ShowCarName` 0x481cb0 shows FXT `car%d` of the car's model as type 200.
- `HUD_Update`:
  - Looks up the area at the render camera's block. `Area_GetName` gets `camera x >> 6` and
    `camera y >> 6`, truncated to bytes.
  - On a change (or after `HUD_RefreshZone`, F9) `HUD_Draw` replaces the type 2 text with it.
  - Ticks the timers.

Other types in use:

| Type | Text |
|---|---|
| 1 | video mode name, F12 reload |
| 4, 5 | debug readout (key C with the debug keys: the view target and the car's speed and damage) |
| 0x58 | wanted points (option 0x5031c4) |
| 0xb4 | the demo countdown |
| 0xca | F8 speed limit |

## Area names (area module)

- Nav zones are the CMP's 35-byte records {x, y, w, h, sample, name[30]}.
- `Area_LocalizeNames` 0x44b4a0 replaces each name with the FXT text `%03darea%03d` (style, sample),
  e.g. `001area001` "Liberty City".
- `Area_GetName` takes the first zone containing the block, skipping sample 1 in style 1 and sample
  11 in style 3. It prints `"%s%s"` of a compass prefix and the name, and returns `zone * 256 +
  direction`, or -1 with "unknown area".
- `Area_SubDirection` 0x44b6d0 finds the part of the zone the block is in:
  - Rows give 2 (top), 3 (middle), 1 (bottom); columns 8 (left), 12 (middle), 4 (right).
  - A side of up to 6 blocks isn't divided (0). Up to 12 it is halved (no middle).
  - The thirds are `w / 3` and `2w / 3`.
- The table 0x4b1dec maps the sum to the prefix index 0-9.
- `Area_LoadDirPrefixes` 0x44b510 loads the prefixes: the FXT texts of the 9 keys at 0x4b1e00, each
  with a space appended. Index 0 is empty. WinMain calls it; the port calls it from `HUD_Init` (once).

## Big messages

`HUD_ShowBigMessage` 0x481320 (`Lo` priority 0, `Hi` 1; e.g. 4003 "BUSTED!", 4004 "WASTED!"):

- A text over 40 bytes is fatal -0x82.
- A lower priority than the message showing is ignored.
- The texts of 2501, 2500 and 2504 play voice 2, 1 and 5.
- The text splits at the first space and again at the next one (so up to 3 lines; the third keeps
  any further spaces).

## Subtitles

`HUD_ShowSubtitle` 0x481d40. Kinds 0-2 are "talk" and 3-5 not.

- **Replacing.** While one shows, a new one replaces it only if it is talk and the current one isn't.
  Otherwise the new one becomes the pending subtitle (the last wins), shown when the current one
  ends.
- **F10.** Talk subtitles are saved for F10 (`HUD_RestoreSubtitle`).
- **Skipping.** `HUD_SkipSubtitle` 0x4820a0 ends a talk subtitle (timer 1) and drops a pending talk
  one.
- **Attract mode.** While 0x5031f4 is set (the game started from the start menu), talk subtitles are
  not shown at all.

`HUD_Brief` 0x4821c0 shows FXT text number n for a player (only the local one):

| Kind | Shown as |
|---|---|
| 0 | pager message |
| 1, 3, 4 | subtitle kind 0, 2, 1, with the babble sound (`Snd_PlayTalk`) |
| 2 | pager countdown |
| 5 | subtitle 5, silent |

## Pager (16 slots of 0x4e0 bytes at 0x77f280)

The slot record:

| Offset | Field |
|---|---|
| +0 | text (0x4b4 bytes) |
| +0x4b4 | length |
| +0x4b8 | width in pixels |
| +0x4bc | scroll |
| +0x4c0, +0x4c4 | chat max / min length |
| +0x4c8 | sequence |
| +0x4cc | chat target |
| +0x4d0 | state |
| +0x4d4 | countdown id |
| +0x4d8 | countdown value |
| +0x4da | countdown tick |
| +0x4dc | countdown scroll limit |

States: 0 queued / showing, 1 free, 3 local chat line, 4 remote chat line, 5 countdown.

- **New message** (`Pager_AddMessage` 0x4824a0):
  - The text is mapped (`Text_ToUtf8Mapped`) and prefixed with `pager_cols` spaces (0x784860 =
    res * 58 / width('n'): it scrolls in from the right).
  - It is dropped if a queued slot already contains it (`strstr`).
  - It is appended (after a space) to the last added message if that is still queued.
  - Otherwise it goes into a free slot, with sample 28.
  - The lengths count the caller's bytes, not the mapped copy's (accented letters get cut off).
- **Next slot.** A local chat line comes first, else the oldest (by sequence) queued message or
  countdown.
- **Scrolling** (`Pager_Update`). The shown slot scrolls by the speed until its width. Then it is
  freed and the next one shown; with none left, the pager hides. A local chat line stops one pixel
  short.
- **Countdowns** (`Pager_AddCountdown` 0x4827c0: text, seconds, id).
  - The digits live after the text's length and are rewritten every 25 frames, with the alarm loop
    sound.
  - Scrolling stops 58 pixels before the end (58, not 58 * res).
  - At 0 the slot becomes an ordinary message. `Pager_RemoveCountdown` 0x482710 frees it by id.
- **F7** (`Pager_Resume` 0x482fb0) shows the last shown slot again from the start.
- **Chat** (`HUD_ChatBegin` 0x4828a0 from `Net_BuildChatPrefix`, `HUD_ChatKey` 0x482b10).
  - The line is the prefix and a blinking `_` cursor.
  - Keys go through the scan code table 0x4b3618 (0x3a entries) while the processed player's chat
    slot (+0x184) is set.
  - Backspace stops at the prefix. Enter drops the cursor and lets it scroll out like a message.
  - The network side (sending the line, `Net_BuildChatPrefix` with other players) is the stubbed
    DirectPlay layer: in a single-player game F1..F4 do nothing, as in the original.

## Keys (`HUD_HandleKey`)

Chat characters first (see above).

With the video menu open (+0x1b2 of the processed player):

- up / down move the selection;
- left / right move it by a column, keeping the row;
- Enter selects the mode (`Gfx_SelectMode`, `Style_ConvertPalettes`), shows its name as a type 1 zone
  text and requests a redraw.

With the quit prompt (+0x1b3): Enter abandons the game (`Game_RequestAbandon`).

`HUD_ToggleQuitPrompt` 0x4822a0 (Esc) and `HUD_ToggleVideoMenu` 0x482330 (F11, or Esc in the menu)
open only when the other isn't open and no chat line is.

## Port notes and deviations

- **Other modules' functions here.** The area module (`src/hud/area.c`: the area names and
  `Area_GetSample` 0x44b7b0, the police radio's zone sample) lives with the HUD; `Map_IsCovered` is
  `src/map.c`'s (a wrapper on the level's map in hud.c). `Player_UpdateScoreDigits` is player.c's.
- **Position records.** `Car_GetCamTarget`, `Ped_GetPosRect` and `Player_GetControlledPos` are the
  car, ped and player modules' (with `Ref_GetKind1PosRect` for a ridden train and `Heli_GetPos`);
  `Player_GetViewFocusPos` and `HUD_GetTargetPos` copy their static records by value.
- **The video menu's mode list** (`src/game/gfx.c`, `Gfx_GetModeLists` 0x414c20): the original lists
  the modes SciTech MGL enumerates (DirectDraw and packed drivers, 4:3 or 16:10 up to 1600 wide) in
  three columns by depth, 15, 16 and 32 bits, each named "w x h x bits"; gasm offers one surface, so
  the list is "640x480x32", alone in the third column. Enter on it runs `Gfx_SelectMode` 0x414cc0
  (`Gfx_SetVideoMode`: the viewport and `HUD_SetViewSize`; then `HUD_LoadFonts` with res 2, the mode
  being wider than 400). The original takes its default mode from the 15- and 16-bit lists only and
  stops (fatal -0x128) without a 640 x 480 there; the port's default is the 32-bit mode.
- **Language.** With no language selected (a host that skips WinMain's `Text_InitLanguage`),
  `hud_text` selects English instead of failing on the first FXT lookup.
- **Sprite infos.** Before each world sprite draw the sprite's info is looked up again from its frame:
  the original keeps the pointer from `HUD_LoadFonts`, which goes stale when the next level loads
  another style at the same resolution.
- **Bounds the original doesn't check.** Some indexes are never checked in the original; the port
  stops instead:
  - Pager slot -1: `Pager_Update` with no current slot, `Pager_AddCountdown` with nothing added.
  - Writes past text buffers: the area name buffer is 64 bytes, and the score popup text has 12
    bytes where the original has 8 before the next field.
  - Division by zero in the subtitle time (fatal).
- **Sprintf with the text as format.** `HUD_Brief` sprintfs the FXT text with itself as the format;
  the port copies it. No text a brief uses contains `%`.
- **Not ported:** the kanji (Japanese) font paths and the network chat transport. 0x5031f4 (attract
  mode) is a HUD-local false until the frontend wires it.
