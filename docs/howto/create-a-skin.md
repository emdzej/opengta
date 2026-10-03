# Create a skin

A skin is a folder of your own images that OpenGTA draws instead of the original art: block tiles,
sprites (cars, peds, objects), fonts and frontend pictures. It is an addition to the original game,
drawn by the [hires renderer](/hires); the game itself plays exactly the same. This page goes through
making one from start to finish. The reference for every rule is [Skins](/skins).

## How skins work

- **Per asset, with fallback.** A skin replaces only what it has an image for. Every tile, sprite,
  glyph or picture it doesn't have is drawn from the original art, so a skin can be one image or
  thousands, and you can try it at any point.
- **Stacking.** `skin=a,b` loads several skins; where both have an image, the later one (`b`) wins. A
  small skin can sit on top of a large one and change a few things.
- **Any resolution.** Skins work at every hires scale, `hires=1` (640 x 480) to `hires=4`
  (2560 x 1920). Images can be any size: each one is stretched over the tile or the sprite's
  footprint.

## What you need

- Your copy of GTA (the installed folder or the unzipped `GTAINSTALLER.zip`, see
  [Game data](/guide/game-data)). The skin tools read it on your computer; nothing is sent anywhere.
- The `skin_template` tool: it is built with the tests from the source ([Build from source](/howto/build-from-source)):
  ```sh
  cmake -S . -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j
  ./build/skin_template          # prints the usage
  ```
- To try the skin: `gasm-run` and `opengta.wasm` ([Install](/guide/install)), or the
  [browser player](/guide/browser).
- An image editor that saves PNG.

## 1. Make the template

```sh
./build/skin_template --data ~/Games/GTA my-skin
```

`--data` is your game folder (without it: `$OPENGTA_DATA`, else `./game`). The tool writes into
`my-skin/`:

| File | What |
|---|---|
| `CHECKLIST.md` | every asset you can replace, per style, with its size and variants, and a box per file (`[x]` once the file is there) |
| `skin.json` | the same list for scripts |
| `skin.ini` | the skin's name, author and scale, to fill in |
| `style001/`, `style002/`, `style003/` with `side/`, `lid/`, `aux/`, `sprite/`; `font/`, `pictures/` | empty folders for your images |

### Reference images: `--extract`

```sh
./build/skin_template --data ~/Games/GTA --extract my-reference
```

writes the template plus **every original graphic** as a PNG, already named the way a skin names it:
tiles (`side/`, `lid/`, `aux/`, with the `_r<n>` remap and direction variants the maps use), sprites
(`sprite/<n>.png`, the damage and door overlays `<n>_delta<k>.png`, the car paint masks `<n>_mask.png`),
every font glyph (`font/<FONT>/<code>.png`) and the frontend pictures (`pictures/`), about 9,500 files.
`--remaps` adds every ped clothes remap (`<n>_r<r>.png`, tens of thousands of files). Loaded as a skin,
the folder draws exactly like the original, so it is a reference to draw over or to compare with:
open a tile, paint your own over it at any size, save it under the same name in your skin.

These are your copy's own graphics: keep them on your machine. A skin you share must not contain them or
anything traced from them.

Without `--extract`, the template holds **no pixels of the game**, on purpose: numbers, names and sizes only. The game's art
belongs to its owners and can't be redistributed, and an image made from it (an upscaled, traced or
repainted copy) can't be either. A skin you want to share has to be your own art from scratch. The
checklist tells you what to draw and at what size; to see what a tile or sprite looks like, look at it in
the game.

Run the tool again on the same folder at any time: it rewrites `CHECKLIST.md` and `skin.json` with the
files you've added marked done, and never touches your images or an existing `skin.ini`.

## 2. Fill in skin.ini

```ini
; my skin for OpenGTA
name = Clean streets
author = Your Name
scale = 2
```

`name` is shown in the game's log and the browser's skin list (the folder name is the skin's id:
`skin=my-skin`; it can't contain a comma). `scale` is the hires scale you draw for (1 to 4); it is
informative. A folder without `skin.ini` is not a skin: the game skips it with a message.

## 3. Folder layout and file names

```
my-skin/skin.ini
my-skin/style001/lid/<n>.png             lid tile n of style 1 (Liberty City)
my-skin/style001/lid/<n>_r<r>.png        the same lid with remap r (0..3), optional
my-skin/style001/side/<n>.png            side (wall) tile n
my-skin/style001/side/<n>_r<d>.png       the same wall facing direction d (0..3), optional
my-skin/style001/aux/<n>.png             aux tile n: animation frames
my-skin/style001/sprite/<n>.png          sprite n
my-skin/style001/sprite/<n>_r<r>.png     sprite n with remap r (a car colour), optional
my-skin/style001/sprite/<n>_mask.png     sprite n's paint mask, optional
my-skin/style001/sprite/<n>_delta<k>.png sprite n's delta k (damage, a door step), optional
my-skin/font/<FONT>/<code>.png           a glyph of a font
my-skin/pictures/<NAME>.png              a frontend picture
```

- Styles: `style001` Liberty City, `style002` San Andreas, `style003` Vice City. Each city has its own
  numbers: lid 1 of style 1 is not lid 1 of style 2.
- Numbers are plain decimals without leading zeros (`lid/7.png`, not `lid/007.png`), and `.png` is
  lower case.
- Files the game doesn't read (your sources, `.psd`, notes) can stay in the folder; the validator
  warns about them, and the browser player only copies the PNGs and `skin.ini`.

### Finding a tile's number

The checklist lists the lids and sides by how often the maps use them. To find the number of a tile you
see, open the city viewer on that spot and ask the tool about the same block:

```sh
# the viewer at block (105, 119), layer 4 (the arrow keys move the target, Page Up / Down the layer)
gasm-run opengta.wasm --asset-dir ~/Games/GTA --param front=0 --param map=nyc --param x=105 --param y=119 --param z=4
# what is drawn there
./build/skin_template --data ~/Games/GTA --at nyc 105 119
```

```
GTADATA/NYC.CMP column (105, 119), style 1: files under style001/ (z = 0 is the top layer)
z 4:
  lid      7  lid/7.png
```

`--at` lists every block of the column (or only layer z: `--at nyc 105 119 4`): its lid and its four
sides with the file that replaces each, the variant it is drawn with, and the frames if the tile is
animated. Map coordinates are blocks, 0 to 255; z = 0 is the top layer and ground level is usually 4 or 5.

## 4. Draw the tiles

Tiles are square: 64 x 64 in the original. Draw them square at any size; good sizes for the scale:

| hires | street-level block on screen | tile image |
|---|---|---|
| 1 (640 x 480) | about 110 pixels | 128 x 128 |
| 2 | about 220 | 256 x 256 |
| 3 | about 330 | 384 x 384 |
| 4 | about 440 | 512 x 512 |

Larger images are box-filtered down to 128 x scale at load (they look the same and use less memory), so
one 512 x 512 set serves every scale.

### Details per scale: `@1x` .. `@4x`

A 512 x 512 tile box-filtered to 128 x 128 loses its thin lines and small lettering. To draw a scale by
hand, put the version next to the plain file with the scale in its name:

```
my-skin/style001/lid/7.png       the master: used where no level fits
my-skin/style001/lid/7@1x.png    drawn for hires=1 (128 x 128: bolder lines, no small detail)
my-skin/style001/lid/7@4x.png    drawn for hires=4 (512 x 512: every detail)
```

At `hires=N` the game takes `@Nx`, else the nearest larger level (scaled down), else the master, else
the nearest smaller level (scaled up). With the files above, `hires=2` and `hires=3` use `7@4x.png`
scaled down. This works for every kind of file, variants included (`7_r1@2x.png`, `72_mask@4x.png`,
`font/BIG1/65@1x.png`), and you only need levels where a scale looks wrong: the rest stays one master.

- **Lids** are the tops of blocks (roads, pavements, roofs). A block can tint its lid with a remap
  (0 to 3; mostly shading, like a shadowed pavement). Without `lid/<n>_r<r>.png` your plain image is
  shaded the way the remap shades the original, which is usually what you want.
- **Sides** are walls. The original gives each wall direction (0 top, 1 bottom, 2 left, 3 right) its own
  palette, often shaded differently; `side/<n>_r<d>.png` replaces one direction, otherwise the plain
  image is shaded like that direction.
- **Transparency**: on see-through faces (the flat blocks: fences, railings, signs) and slope sides, alpha 0
  is transparent, like the original's colour 0. Solid faces draw every pixel; alpha doesn't matter
  there.
- **Animated tiles** (water, some signs) show aux tiles in turn: one image per frame, under `aux/`, with
  the numbers the checklist (or `--at`) lists. Lid 41 in Liberty City (the water) shows aux 25 to 35:
  replace lid 41 and aux 25 to 35, or the animation flips between your water and the original frames.
  Aux tiles take the remaps and directions of the tile they animate.

## 5. Draw the sprites

Sprites are drawn rotated around their centre at their original footprint (the checklist gives each
one's size, for example `sprite/72.png: 62 x 64`). Your image is stretched over that footprint, so keep
the aspect ratio: for a 62 x 64 car, 124 x 128 at 2x, 248 x 256 at 4x. Up to 2 x scale times the original
size is kept; larger images are box-filtered down at load. Draw it the same way round as the original
graphic (look at the sprite in the game, or in the viewer).

Make everything outside the shape transparent (alpha 0); semi-transparent edges are fine.

### Car colours: paint masks

The game draws one car sprite in up to 12 colours (remaps). Instead of drawing each colour
(`sprite/72_r1.png` ... `sprite/72_r12.png`, also possible), give the sprite a **paint mask**:
`sprite/<n>_mask.png`, the same size as `sprite/<n>.png`, white where the car is paint, black
everywhere else (glass, lights, tyres, chrome), grey for soft edges. For each colour the white pixels
take the colour the game uses for that remap, shaded by your image's brightness; the black ones stay as
you drew them.

An easy way to make one: draw the paint on its own layer, export that layer alone with transparency, and
turn its alpha into the mask. With ImageMagick:

```sh
magick paint-layer.png -alpha extract my-skin/style001/sprite/72_mask.png
```

Draw the paint about as bright as the car's paint looks in the game: brighter paint gives lighter colours
for every remap, darker paint darker ones. Your own colours show when the car is drawn without a remap.

Peds have remaps too (their clothes): the same mask convention works for ped sprites.

### Damage and doors: deltas

Cars change with damage (dents by corner and side), the door animation steps and the siren lights;
the game calls these deltas (the checklist lists a sprite's `deltas 0-10`). You don't have to draw them:
without delta images, the game derives them from the original's deltas, darkening and lightening your
image where the original's damage changes the car and adding the open door outside the outline. To draw
your own, `sprite/<n>_delta<k>.png` is laid over the sprite when it shows delta k: draw it at the size of
the sprite image, transparent everywhere except the damage. It isn't recoloured with the remaps, so keep
it neutral (dents, scratches, broken glass) rather than painted.

## 6. Fonts and pictures

Fonts and the frontend pictures are skinned the same way, by file name; [Fonts and
pictures](/skins#fonts-and-pictures) lists which font is used for what and the details of the drawing:

- `font/<FONT>/<code>.png`: glyph `<code>` of the font `GTADATA/<FONT>.FON` (`BIG2`, `SUB2`, `F_MHEAD`,
  ...; the name in capitals; the game draws the `2` HUD fonts, not the `1` ones). The code is the
  character code in decimal: `65.png` is `A` in the text fonts (they start at 33, `!`); the HUD's digit
  fonts start at 0 and the icon fonts at 1. The checklist
  lists each font's codes and size: a glyph image is stretched over the glyph's width by the font's
  height. A grey glyph is tinted with the colours the game draws that glyph with, so one white-on-
  transparent set can serve text that the game colours.
- `pictures/<NAME>.png`: the frontend picture `GTADATA/<NAME>.RAW` (`F_UPPER`, `F_LOGO0` to
  `F_LOGO7`, `F_LOWER0`, `CUT0` to `CUT5`, `F_PLAY1` to `F_PLAY8`, ...), opaque, stretched over the
  picture at the hires scale. The checklist gives each one's size (640 x 480, 640 x 168, ...).

## 7. Validate

```sh
./build/skin_template --data ~/Games/GTA --validate my-skin
```

It checks every file in the folder against your data and prints errors (the game can't use the file),
warnings (it can, but probably not as you meant) and notes, then a summary:

```
warning skin.ini: unknown key 'colour' (name, author, scale)
error   style001/lid/01.png: leading zeros: the game asks for lid/1.png
error   style001/side/5.PNG: the game asks for ".png" in lower case
error   style001/sprite/72_mask.png: 248 x 256 doesn't match sprite/72.png (256 x 256)
...
style 1: lids 3 (3 of the 124 the maps place), sides 1 (1 of 174), aux 11 of 37, sprites 1 of 1034
17 images; 3 errors, 1 warnings, 0 notes (made for hires=2)
```

It checks the names (style, kind, number and variant in range), that each PNG decodes with the game's
own decoder (8 bits per channel, not interlaced), sizes and aspect ratios, masks and delta images
against their sprite, glyphs and pictures against the game's, and `skin.ini`. The exit status is 1 if
there are errors.

## 8. Try it

With `gasm-run`, give the folder that holds your skin folder as the `skins` assets and name the skin:

```sh
# mission 1 at 2x with your skin (my-skin is in ~/skins)
gasm-run opengta.wasm --asset-dir ~/Games/GTA --asset-dir skins=$HOME/skins \
  --param intro=0 --param mission=1 --param hires=2 --param skin=my-skin
# the city viewer: look around any spot without playing
gasm-run opengta.wasm --asset-dir ~/Games/GTA --asset-dir skins=$HOME/skins \
  --param front=0 --param map=nyc --param x=60 --param y=144 --param hires=2 --param skin=my-skin
# your skin over the sample skin
gasm-run opengta.wasm --asset-dir ~/Games/GTA --asset-dir skins=$HOME/skins \
  --param hires=2 --param skin=sample,my-skin
```

The log names each skin it loaded, and every image it couldn't decode. `--headless N --screenshot shot.png`
saves a frame without opening a window, handy for comparing versions.

In the [browser player](/guide/browser#skins-and-resolution), **Add a skin folder** under
**Resolution and skins** imports the folder (pick `my-skin` itself), and the resolution selector sets the
scale. After changing images, add the folder again: it replaces the stored copy.

### Iterating quickly

- Use the viewer (`front=0`) on the spot you're working on, with `--screenshot` for a quick look, rather
  than playing to it.
- Work one city at a time, and start with what covers the most screen: the face counts in the checklist
  show which lids and sides the maps use most (roads and pavements first).
- Run the validator before you test: a file with a wrong name is simply not used, which is easy to
  mistake for the game not picking up a change.

## A worked example: the sample skin

`assets/skins/sample` in the repository is a complete small skin, made by `tools/make-sample-skin.py`
from code alone (it reads no game file). It replaces Liberty City's road (lid 1), a pavement (lid 8),
the water (lid 41 and its frames aux 25 to 35), a wall (side 138) and the car parked at mission 1's start
(sprite 72, with a paint mask), and leaves everything else to the original art. Its images are 256 x 256
(the car 248 x 256, the car's 62 x 64 times 4). Read the script for a programmatic way to make a skin, and
try it:

```sh
python3 tools/make-sample-skin.py
./build/skin_template --validate assets/skins/sample
gasm-run opengta.wasm --asset-dir ~/Games/GTA --asset-dir skins=assets/skins \
  --param intro=0 --param mission=1 --param hires=2 --param skin=sample
```

## Sharing a skin

- Share the folder (zipped), with `skin.ini` filled in and a `README` and `LICENSE` of your choice
  next to it (the game ignores them). Pick a license for your art, for example a Creative Commons one.
- Include only art you made, or have the rights to: nothing copied, traced, upscaled or recoloured
  from the game's graphics, and no files of the game. Leave out what you haven't replaced: the original
  art comes from each player's own copy.
- `CHECKLIST.md` and `skin.json` describe the asker's game data; they can be left out, or kept as a
  record of what the skin covers.
