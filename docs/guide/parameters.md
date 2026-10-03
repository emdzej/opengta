# Launch parameters

Launch parameters are gasm's `--param name=value` (in the browser: the page's address,
`/play/?mission=1`). They are development switches for now; without any, OpenGTA starts at the frontend
as the original does.

| Parameter | Values | What |
|---|---|---|
| `intro` | `0` | No intro movie (it plays before the menus, as in the original; any key but Alt skips it). |
| `mission` | a `MISSION.INI` section | Skip the menus and start that section's level, e.g. `1` and `2` (Liberty City), `102`, `103` (San Andreas), `202`, `203` (Vice City). The game quits when the level ends. |
| `front` | `0` | Skip the menus and open the **city viewer**: the city drawn by the ported renderer around a camera target you move, without the game. |
| `map` | `nyc`, `sanb`, `miami` | The viewer's city (default `nyc`). |
| `x`, `y` | 0-255 | The viewer's target block (default: NYC mission 1's start, 105, 119; 128, 128 in the other cities). |
| `z` | 0-6 | The viewer target's layer, the original's convention: 0 is the top (default 4, the usual ground). |
| `hires` | `2`, `3`, `4` | Not in the original, opt-in: the in-game view (and the viewer) drawn again at 640N x 480N by the [hires renderer](/hires), sharper geometry and filtered textures, the same game; `skin=` adds [skins](/skins) (`skin=sample`, with `--asset-dir skins=assets/skins`). |

```sh
gasm-run opengta.wasm --asset-dir GTA --param mission=202
gasm-run opengta.wasm --asset-dir GTA --param front=0 --param map=sanb --param x=60 --param y=60 --param z=2
```

The original's own command-line switches (`Game_SetOptions`, 43 of them: debug keys, demo mode, the
frame limiter and so on) are set as WinMain sets them; see [Game core](/game-core).
