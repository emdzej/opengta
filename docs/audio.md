# Sound and music

The port of GTA's three sound modules and the timer, over a reimplementation of the Miles Sound System
calls they make. Code: `src/audio/` (`audio.h` is the API).

| File | Original | What |
|---|---|---|
| `snd.c` | 0x471bc0–0x473430 | Miles handles, banks, one-shot / voice / talk players, the cutscene WAV stream |
| `sound.c` | 0x402640–0x405760, 0x414620–0x4146a0 | game-level sound: loops sorted by distance, 3D one-shots, voices, police scanner, mode switch |
| `music.c` | 0x40c300–0x40d620 | `Music/TrackN.wav` streams, radio stations |
| `timer.c` | 0x47dc00–0x47dcb0 | the 70 Hz AIL timer that paces `Game_Run` |
| `mss.c` | MSS32.DLL V3.6B | samples, streams, master volume, ms clock, timers, the software mixer |

The functions keep the original's names, so the game core calls them the way the exe does. What the
sound code reads from the rest of the game goes through two small structs: `SndHost` (listener position
= `Player_GetViewTargetPos` 0x462d50, `Map_IsCovered` 0x438800, `Game_IsFrozen` 0x4309d0, the CRT's
`rand` 0x49cb27 and `time` 0x49cb97) and `SndWorld` (a per-frame snapshot of the car fields, trains, cranes,
helicopter and group leader that `Snd_GatherLoops` 0x402690 and `Music_UpdateRadio` 0x40cde0 read; each
field is named with its offset in the original). `Snd_GatherLoops` writes one car field back
(+0x14a, the bus air-brake peak speed).

## Data

All under `GTADATA/AUDIO/` unless noted.

- **.SDT**: an array of 12-byte records `{u32 offset, u32 length, u32 rate}` into the matching **.RAW**
  (raw PCM, no header). The loaders read a fixed number of records with `fread`: 131 for a level bank,
  71 for VOCALCOM. A shorter .SDT leaves the remaining table entries as they were.
- **LEVEL000** (16 records): the frontend's sounds, **16-bit signed**; entries 0–2 are stereo (32000 /
  24000 Hz loops), the rest mono. Played on 16-bit handles only.
- **LEVEL001..003** (131 records each): the in-game banks, **8-bit unsigned mono**, rates 4000–22050.
  The bank number is the **style number** (city): `Snd_InitGame` loads LEVEL001, then `Game_Init`
  calls `Snd_Reset(Style_GetNumber())`. NYC.CMP, SANB.CMP and MIAMI.CMP have style 1, 2, 3; the CMP
  header's "sample" byte is 0 in all three and is not used. The RAW is read whole into a 1 MB area
  (larger: fatal error -23).
- **VOCALCOM** (71 records): voices, 8-bit unsigned mono, 11025–32000 Hz. The .RAW stays open; each
  voice is read on demand into the buffer after the level data (65000 bytes there).
- **MISBRIEF** (10 records, 8-bit): not referenced by the Windows exe.
- **0.WAV..5.WAV**: cutscene / briefing speech, RIFF PCM 22050 Hz 16-bit stereo, streamed by
  `Snd_MusicPlay` 0x4732f0.
- **`Music/Track1.wav` .. `Track10.wav`** (the exe names them `..//music//trackN.wav`, table 0x4ab2e8,
  0x108 bytes each): RIFF PCM 22050 Hz 16-bit stereo, 5–58 MB, streamed in 64 KB reads through
  `vfs_read_at`.

Bank sample numbers the code uses (LEVEL001..003): 0x1c emitter loop B, 0x1d..0x20 tuning noise,
0x27 a one-shot that always gets a channel at full distance volume, 0x28, 0x29 bus air brake, 0x2c pause,
0x2d + n engine n, 0x35 damaged engine, 0x37 train, 0x3a + horn/10 horns, 0x40 / 0x42 / 0x43 sirens,
0x41 skid, 0x44 crane, 0x45 an extra engine loop, 0x46 emitter loop A, 0x47 alarm, 0x48 helicopter,
0x49 group leader, 0x4a radio static, 0x4b fire, 0x4c/0x4d, 0x4e..0x7d police scanner words (0x4f..0x51
intros, 0x53..0x60 car models, 0x61..0x63, 100..119 area names, 0x79..0x7d directions, 0x4e end),
0x7e..0x82 car radio samples. LEVEL000: 0..2 menu loops, 0xf the exit jingle.

## Channels

Handles per mode (`AIL_allocate_sample_handle`):

| Mode | Handles | Format |
|---|---|---|
| game (`Snd_StartupGame` 0x471be0, 19 Miles channels) | 10 loops 0x77060c, 3 positional one-shots 0x770ff4, "3D" 0x7709ac, menu/scanner 0x771074, voice 0x771008, talk 0x7709b0 | 8-bit unsigned mono |
| frontend (`Snd_StartupFrontend` 0x471f10, 12 channels) | 4 stereo 0x77099c, 4 mono 0x7709b4 (round robin) | 16-bit signed |

Each mode also opens one stream (music or cutscene WAV), which takes a channel of its own.

- **Loops** (`Snd_UpdateGame` 0x405510 every game frame): the "wanted" list 0x4bdc88 is cleared,
  `Snd_GatherLoops` fills it with the 10 nearest sources (sorted farthest first; inserting drops entry 0):
  per car within 16 blocks fire, sirens, engine (rate from the speed by the car's sound function: gear
  table 0x4b30dc, boat, bus, tank formulas), skids, horns (with a pattern table 0x4ab091), a convertible's
  radio sample, the tunnel static of the player's radio, a damaged engine; trains, cranes, emitter
  slots, an alarm, the nearest of emitter set B, the helicopter, the group leader. Volumes are
  `(0x3ffffff - Manhattan distance) >> 19` (0..127) scaled per kind, halved under cover; pan is
  `(0x3ffffff - dx) >> 20`. `Snd_UpdateLoopChannels` 0x404430 matches the list to the 10 playing
  channels by (source id, sample): rate slews by 500 per frame (3000 for "fast" car types) towards the
  target plus a Doppler term `(old - new distance) / 3000`, volume and pan by 5; unmatched channels
  stop, new entries take a free channel (looping forever).
- **One-shots** `Snd_Play3D` 0x4046b0: a free positional channel, or the one playing the farthest sound
  if the new one is nearer.
- **Voices** on the voice channel; ped voices are rate limited per kind.
- **Police scanner** `Snd_PoliceRadio` 0x404880 replaces a 100-entry queue of words; `Snd_UpdateGame`
  plays the next one on the menu channel whenever it is idle, at 6000 Hz whatever the sample's rate.
- **Music**: radio mode gives tracks 0..8 random start offsets (`time()` squared modulo the length);
  station = car_info +0xa8, 3 tracks per station (table 0x4ab270), `Music_NextStation` 0x40d140 rotates;
  on foot: track 8 (Track9.wav). Tracks loop; a stream that fails restarts. A newly started track is at
  volume 0 for 26 frames.

## Miles calls replaced

Imports of `MSS32.DLL` (V3.6B) and what `mss.c` does for each.

| Import | Use in the exe | Here |
|---|---|---|
| `AIL_startup`, `AIL_shutdown` | per sound mode | `mss_startup` / `mss_shutdown` |
| `AIL_set_preference` | 0 (DIG_MIXER_CHANNELS) = 19 in game, 12 frontend, 1 probe; 15 (DIG_USE_WAVEOUT) = 0, or 1 to reopen when the DirectSound device is "Emulated" | the channel count of `mss_startup`; device choice moot |
| `AIL_waveOutOpen`, `AIL_waveOutClose`, `AIL_digital_configuration` | 22050 Hz, 2 channels, 8 bits; the configuration string is compared with "Emulated" | output format of `mss_render` |
| `AIL_mem_alloc_lock`, `AIL_mem_free_lock` | the 0x10fde8-byte bank buffer | `calloc` / `free` |
| `AIL_allocate_sample_handle`, `AIL_release_sample_handle`, `AIL_init_sample` | handles above | a pool of 32 voices, at most `channels` allocated; init = 11025 Hz, mono 8, volume 100, pan 64, 1 loop |
| `AIL_set_sample_type` | (0, 0) mono 8 unsigned; (3, 1) stereo 16 signed; (1, 1) mono 16 signed | same |
| `AIL_set_sample_address`, `_playback_rate`, `_volume` (0..127), `_pan` (0..127), `_loop_count` (0 = forever) | | same |
| `AIL_start_sample`, `AIL_end_sample`, `AIL_sample_status` (4 = playing) | | same; start rewinds |
| `AIL_set_digital_master_volume` | 108, or music level * 0.8 when the sfx level is 0 | scales every voice: `v = master * volume / 127` |
| `AIL_ms_count` | the 1 s wait in `Snd_Shutdown`, stream start time | ms of audio rendered |
| `AIL_open_stream`, `AIL_close_stream`, `AIL_start_stream`, `AIL_pause_stream`, `AIL_service_stream` | music, cutscene WAVs | RIFF parser, chunked reads through the file layer; service is a no-op (reads happen while mixing) |
| `AIL_stream_info` | length (bytes), sample rate, format | same |
| `AIL_set_stream_volume`, `AIL_set_stream_loop_count`, `AIL_set_stream_position` (bytes, clamped, not aligned), `AIL_stream_position`, `AIL_stream_status` (-1 = NULL or read error) | | same |
| `AIL_register_timer`, `AIL_set_timer_frequency`, `AIL_start_timer`, `AIL_stop_timer`, `AIL_release_timer_handle` | the 70 Hz frame timer | callbacks on the rendered-audio clock |

### The mixer

`mss.c` follows MSS32.DLL's own software mixer (its waveOut path; with DirectSound, MSS hands each voice
to a DirectSound buffer with the same gains: volume `20 log10(v / 127)` dB, pan `20 log10(x / 63)` dB):

- effective volume `v = master * volume / 127`, clamped to 0..127; pan gains from a table
  `T[p] = min(2p, 128)`: left `T[127 - pan]`, right `T[pan]` (full on both sides at 64);
- 8-bit data: per voice a 256-entry table `((s - 128) * 2 * (v + 1)) * T >> 7` (`v + 1` only if v > 0);
  16-bit: `s * (T * v / 127) >> 7`;
- rate: step `rate * 65536 / 22050`; within 655 / 65536 (1%) of 1 no resampling at all (MSS's default
  DIG_RESAMPLING_TOLERANCE); otherwise point sampling, read then advance, the fraction starting at 0.5;
- a 32-bit sum, clipped to 16 bits, and the output is the high byte (8-bit), as the original's device
  format. `MSS_OUTPUT_8BIT` = 0 gives the 16-bit sum instead.

**Rate**: the original mixes at 22050 Hz (its `AIL_waveOutOpen` format), so `APP_AUDIO_RATE` 22050 is
right and should stay: the music, the cutscene WAVs and most voices are 22050 Hz and play unresampled.

## Timing

`Game_Run` 0x4148a0 starts the 70 Hz timer (`AIL_set_timer_frequency(0x46)`) and calls
`Timer_WaitTicks(3)` every frame: **the game runs at 70 / 3 = 23.33 frames per second** (42.86 ms, 945
frames of 22050 Hz audio), as long as a frame takes less. The count restarts after each wait, so a slow
frame isn't caught up: frame times are whole 1/70 s ticks, at least 3. The timer exists only with sound
on (launch option 0x502f6c) and option 0x5031d0 clear; without it the game loop is unthrottled. The
frontend is paced by WinMain's 35 ms `Sleep` instead (`APP_FRAME_HZ`). Here `Timer_WaitTicks` doesn't
block: it returns true when the frame may run, the ticks coming from audio rendered.

## Quirks kept

- `Snd_ShotStart` sets the volume to the sfx volume, so positional one-shots (`Snd_Play3D`) are only
  panned, not attenuated; `Snd_VoicePlay` does the same to positional voices.
- `Snd_ShotSetSample` tests the channel number (< 0x4e) instead of the sample, so every one-shot gets the
  random pitch offset (± rate / 8, alternating sign).
- `Snd_GearRate` indexes its table by `speed / 10 + speed % 10 + 1`.
- Loop channel volumes ignore the sfx volume; only the one-shots, voices and frontend sounds use it.
- `Snd_SetMasterVolume` with sfx level 0 disables the sfx for good and sets the master from the music.
- `Snd_RestoreVolumes` (resume after pause) restores volumes nothing saved: all 0.
- The frontend's random pan is 64..95 (never left).
- `Snd_GatherLoops` reuses one local entry: unset fields carry over between sources (the static's pan
  goes to the damaged-engine entry); a car with an unknown sound function inserts a "no sample" entry
  that still takes a place among the 10.
- `Music_PlayTrack` only makes the offset even, so a 16-bit stereo track starting at an offset of 2 mod 4
  plays with its channels swapped (MSS doesn't align stream positions).
- Sequential mode reads track n's position from catalog entry n + 1; station 9 reads the three variables
  after the station table as track numbers.
- A missing track keeps the previous track's length in the catalog (Track8.wav is missing when the
  game was installed with unshield).

## Deviations

- The MSS mixer restarts its resampling phase at each mix buffer; here the phase carries over, so the
  output doesn't depend on how rendering is split.
- `Snd_ShutdownFrontend` / `Snd_Shutdown` don't block for the exit jingle and the following second:
  the sounds play out on their own and `Snd_FrontendDraining` tells a caller how long the original
  would have waited.
- Fatal errors (missing bank, RAW over 1 MB) are logged and the call fails instead of exiting.
- `Timer_WaitTicks` returns instead of busy-waiting.
