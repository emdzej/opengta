# Controls

OpenGTA reads the keyboard as the original reads it: raw scan codes, the original's keys, no remapping.
What works depends on how far the port is ([Status](/guide/status)).

## Frontend

As in the original, one key press per frame drives the menus; holding a key doesn't repeat.

| Key | In the menus |
|---|---|
| <kbd>Up</kbd> <kbd>Down</kbd> | choose an entry, a mission |
| <kbd>Left</kbd> <kbd>Right</kbd> | change a setting, the player slot, the city |
| <kbd>Enter</kbd> | select, start the mission |
| <kbd>Esc</kbd> | back; on the start menu: the credits, then quit |
| <kbd>Space</kbd> | city select and results: the level's cutscene (once seen) |
| <kbd>Del</kbd> | player select: rename the player |
| <kbd>R</kbd> | player select: reset the player |
| letters, digits | rename: type the name |

The frontend doesn't read gamepads (the original's menus only take keys).

## In a level

Walking and driving come with the ped and car ports. The ported key handler (`Game_HandleKey`) already
takes the original's function keys, though most of what they control isn't ported yet:

| Key | Original function |
|---|---|
| <kbd>Esc</kbd> | the quit prompt (with the HUD port) |
| <kbd>F5</kbd> | next radio station |
| <kbd>F6</kbd> | freeze the game |
| <kbd>F8</kbd> | frame limiter on / off |

## City viewer

With `front=0` ([Launch parameters](/guide/parameters)):

| Key | Gamepad | What |
|---|---|---|
| Arrow keys | D-pad | move the camera target |
| <kbd>Shift</kbd> + arrows | | move faster |
| <kbd>Page Up</kbd> / <kbd>Page Down</kbd> | L / R | target layer up / down |
| <kbd>Esc</kbd> | | to the frontend |

## Runner keys

In `gasm-run` and the browser player, holding <kbd>Esc</kbd> for a second stops the game; a tap goes to
the game. In the browser's fullscreen, the browser takes Esc to leave fullscreen first.
