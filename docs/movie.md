# The intro movie

`GTADATA/MOVIE.SMK` is a Smacker video (RAD Game Tools). The original plays it before the first
frontend screen with RAD's `SMACKW32.DLL` from `Movie_PlayIntro` 0x44b160. The port decodes it with a
clean-room decoder of its own, written from the public description of the format on MultimediaWiki's
"Smacker" page and from looking at the file. No other decoder's code was used.

| Port | Original |
|---|---|
| `src/movie/smacker.c` | what `SMACKW32.DLL` does: the container, the Huffman trees, palette, audio and video decoding |
| `src/movie/intro.c` | `Movie_PlayIntro` 0x44b160, as one step per frontend frame |
| `src/front/front.c` | WinMain's call of it and the move on to `Front_Enter(4)` (the CD screen) |

Test: `tests/movie_test.c` decodes every frame (hash per frame in `out/movie/frames.txt`), writes some
frames (`out/movie/frame*.png`), the soundtrack (`out/movie/audio.wav`), and runs the player with the
Miles mixer the way the frontend does (`out/movie/intro_step*.png`, `out/movie/intro_mix.wav`).

## MOVIE.SMK

| | |
|---|---|
| Signature | `SMK2` |
| Size | 320 x 200, 415 frames, no ring frame, flags 0 |
| Frame rate | field -4000: 40 ms per frame, 25 fps, 16.6 s |
| Audio | track 0 only: 44100 Hz, 16-bit, mono, Huffman DPCM. The first frame holds 91,728 bytes (1.04 s), every other frame 3,528 bytes (40 ms). 732,060 samples in all, exactly 16.600 s |
| Palette | one palette chunk, in frame 0 |
| Content | the BMG Interactive logo, then DMA Design's walking "D" man, run over by a car |

## The file, as implemented

All numbers are little-endian.

**Header** (0x68 bytes): signature (`SMK2` or `SMK4`), width, height, frame count, frame rate, flags,
the largest unpacked audio chunk of each of 7 tracks, the size of the tree section, the allocation
sizes of the four video trees (MMap, MClr, Full, Type), the format of each of 7 audio tracks, one unused
word. The frame rate: positive = milliseconds per frame, negative = hundredths of a millisecond per frame,
0 = 10 fps. Flags: bit 0 a ring frame follows the last one (one more physical frame), bits 1 and 2 the
picture is to be shown at double height (interlaced / doubled). The audio format word: bit 31
compressed, bit 30 present, bit 29 16-bit, bit 28 stereo, bits 26-27 the codec (0 = Huffman DPCM;
others are Bink audio, not decoded), bits 0-23 the rate.

After the header: a 32-bit size for every physical frame (bit 0 = key frame, bit 1 unknown; both
masked off for the length), then a type byte per frame (bit 0 = a palette chunk, bit 1 + t = a chunk
for audio track t), then the tree section, then the frames back to back.

**Bit streams.** Trees, video and compressed audio are bit streams read from the lowest bit of each
byte up.

**Byte trees.** A presence bit; then the tree in pre-order: bit 1 = an inner node (its 0 subtree
follows, then its 1 subtree), bit 0 = a leaf followed by its 8-bit value; then one closing bit. An
absent tree decodes to 0 without reading anything.

**Value trees** (the four video trees). A presence bit; a byte tree for low bytes and one for high bytes;
three 16-bit escape values; the tree, whose leaves are a low byte and a high byte each decoded with
the byte trees; a closing bit. A leaf whose value equals one of the escape values does not stand for
that value: it stands for the 1st, 2nd or 3rd most recently decoded value of this tree. After every
decode, if the value differs from the most recent one, it becomes the most recent and the other two
move down. The three recent values start at 0 for every frame. Check against MOVIE.SMK: every tree
has exactly three nodes fewer than its header allocation size / 4 (the three escape slots), and the
four trees take 44,652 of the 44,925 bytes of the section (the rest is padding).

**Frame.** In order: the palette chunk (if the type says so), one chunk per audio track flagged in the
type, and the video data, which is the rest of the frame.

**Palette chunk.** First byte: the chunk length / 4, that byte included. Then blocks fill the 256 new
entries in order, from the previous palette (all black before the first):

- `1nnnnnnn`: n + 1 entries stay as in the previous palette;
- `01nnnnnn ssssssss`: n + 1 entries copied from the previous palette starting at entry s;
- otherwise three bytes, red, green, blue, 6 bits each, scaled to 8 bits as round(v * 255 / 63).

**Audio chunk.** A 32-bit length (the length field included); for a compressed track the unpacked
size (32 bits) follows. Uncompressed tracks are the PCM bytes. Huffman DPCM: a presence bit (0 =
silence), a stereo bit, a 16-bit bit; one byte tree per byte of a sample frame (left low, left high,
right low, right high, as many as the format has); the first sample frame written out raw (16-bit: the
right channel first, each high byte first); then for every further sample and channel one delta per
byte from its tree (low first), added to the previous sample of that channel as a 16-bit (or 8-bit)
number. 8-bit samples are unsigned, 16-bit signed.

**Video.** The picture (rounded up to whole 4 x 4 blocks) is decoded block by block, left to right, top
to bottom. A value from the Type tree gives the block kind (bits 0-1), a run length (bits 2-7: index
0-58 = 1-59 blocks, 59-63 = 128, 256, 512, 1024, 2048) and a byte (bits 8-15). The run applies to that
many blocks, cut at the end of the picture:

- 0, two colours: a value from MClr (high byte for set bits, low byte for clear ones) and a 16-bit map
  from MMap, one bit per pixel from the lowest, row by row;
- 1, full: per row two values from Full, the first for pixels 2 and 3, the second for 0 and 1 (low byte
  left). In `SMK4` files one or two bits before the run select plain, doubled (2 x 2 cells) or halved
  (odd rows copied from the row above) blocks;
- 2, skip: the block keeps the previous frame's pixels;
- 3, solid: the whole block in the type value's byte.

## The intro player

What `Movie_PlayIntro` does (the DLL is imported by ordinal; the calls are named after RAD's API):

1. `Input_FlushKeys`; `Snd_ProbeDevice` 0x472180 opens Miles; if it worked, `SmackSoundUseMSS` with the
   Miles driver (0x771000), so the soundtrack plays through Miles.
2. `SmackOpen("..\gtadata\movie.smk", all tracks, ...)`; on failure `Snd_CloseDevice` and return.
3. `Gfx_SetVideoMode(-2)`: the 8-bit palettised mode (MGL mode 0x13) with a display DC and a memory DC;
   if the mode can't be set (0x4ac06c) the movie is closed unplayed.
4. Frames are unpacked into the memory DC at (0, 20).
5. Per frame: the movie's palette to the display (`Gfx_SetPalette` 0x414d40) if it changed, decode,
   each dirty rectangle stretched x2 to the display, next frame, wait for its time, one
   `Input_GetKey` 0x414a80. It goes on while there was no event or the event was 0x38 (Alt pressed),
   and frames remain.
6. The palette is zeroed, the display cleared, the memory DC copied 1:1 to the display, the black
   palette set; `SmackClose`, `Snd_CloseDevice`. WinMain goes on to `Front_Enter(4)`.

So the movie fills (0, 40)-(639, 439) of the 640 x 480 screen, each pixel 2 x 2, and the rest of the
screen is palette index 0 (black in MOVIE.SMK). A quirk kept: `Input_GetKey` returns key releases too
(scan code + 0x80), so only *pressing* Alt is ignored: letting it go (0xb8), or letting go of any key that
was held when the movie started, ends the movie like any key press.

### In the port

`front_init` starts the movie (`movie_intro_start`) instead of entering the CD screen; while it plays,
each `front_frame` is one step of the loop above (`movie_intro_step`) and draws the 640 x 480 frame;
when it ends, `front_frame` does the rest of WinMain (the key queue and Shift state cleared,
`Front_Enter(4)`), and the frontend runs from the next frame. The frontend reports key presses and the
held keys; the releases the original's loop sees are the held keys that went up since the last frame.
`movie_intro_set_enabled` switches the movie on (off by default, for callers that expect the CD screen
right after `front_init`); the app turns it on.

**Timing.** The original waits in `SmackWait` until each frame's time (40 ms; Smacker keeps it in step
with the sound). The app calls the frontend at its fixed 35 ms rate (WinMain's frontend loop, 28.57 Hz).
Step k is at time 35k ms; frame n is shown on the first step whose time has reached 40n ms, and the key
check after a frame is made on the step where the next frame is due. Seven frames take eight steps, so
one step in eight shows the previous frame again; the 415 frames take 475 steps (16.625 s) and the
476th ends the movie. A key ends it at the next frame's time.

**Sound.** SMACKW32 feeds the decoded track to a Miles sample as the frames go by. The port decodes the
whole track (`smk_frame_audio` for every frame, 1.46 MB) when the movie starts and plays it as one
Miles sample (`src/audio/mss.c`: 16-bit signed, mono, 44100 Hz, volume 127, centred) started with
frame 0, on the one channel `Snd_ProbeDevice` opens. The samples and their times are the same as a
stream's; the Miles mixer resamples it to its 22050 Hz output. The sample volume Smacker's Miles
driver uses is an assumption (full volume). Only the first audio track is played (MOVIE.SMK has one).
