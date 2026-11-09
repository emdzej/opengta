# How GTA works

A theory of operation of Grand Theft Auto (DMA Design, 1997): how the game is built, from the frame loop to
the missions. It comes from porting the Windows executable of Rockstar's 2002 re-release (GTAINSTALLER.zip)
function by function. It is meant for anyone who wants to learn how a top-down open-city game of its time is
put together. The details, with the executable's addresses and layouts, are in the
[reverse-engineering notes](/re/), the [data formats](/formats) and the subsystem pages under
[Internals](/internals/).

Everything here is what the port and its notes establish. Where something is not understood yet, it says
so. The port reproduces the original's behaviour, quirks included, but whole missions have not been played
through against the original yet ([Status](/guide/status)), so some of what follows may still be corrected.

## The shape of the program

The game is one Windows program of about 750 KB, written in C. DMA's own code makes up most of it. The rest
is third-party: SciTech's MGL library for the display and DirectDraw, RAD's Smacker for the intro movie,
the Miles Sound System for sound and music, DirectInput for the keyboard and DirectPlay for network games.
The linker laid the object files out in source order, so the code falls into contiguous modules, one per
subsystem:

| Module | What it does |
|---|---|
| Start-up and frontend | Settings, the intro movie, the menus, the save file, the high scores |
| Game core | The level start, the frame loop, the delayed events, the random numbers |
| Map and style | Loading the city and its graphics, the block queries, map edits |
| Renderer | The camera, the city drawer, the sprite lists, DMA's polygon rasteriser |
| Cars | The car records, kinematic and physical movement, collision, damage |
| Peds and players | Walking, getting in and out of cars, weapons, projectiles, the controls, replays |
| Traffic | Spawning cars around the view, lanes, junctions, traffic lights, path finding |
| Police and services | The wanted level, pursuits, roadblocks, ambulances, fire engines, the helicopter |
| Objects | Moving and animated objects, explosions, fires, power-ups, doors |
| Trains | Track tracing, carriages, stations, riding and driving |
| Missions | The script reader and interpreter, triggers, phones, briefs |
| HUD | Score, lives, the wanted level, the pager, the arrow, the messages |
| Sound | Banks, positional sounds, engine loops, voices, the police scanner, the radio |

The program starts in `WinMain`: it reads its settings and the player file, plays the intro movie, and then
runs the frontend's menu loop. When the player picks a level, the frontend calls the game's session
function, which loads the level, runs the game loop until the level ends and returns a result (mission
over, abandoned, reload, network failure). The frontend shows the results and goes back to its menus.

```text
WinMain
 ├─ settings, player file, intro movie
 └─ frontend loop (one key, one screen step, sleep out 35 ms)
     └─ a level picked → session
         ├─ read the level's section of the mission file
         ├─ load the city map and its style; build the tables
         ├─ level start: entities, traffic pool, objects, trains, script
         ├─ game loop until the level ends
         │    present → read controls → wait for the timer → per-player input
         │    → update the world → camera → draw city, sprites, HUD
         └─ shut down; results screen; back to the menus
```

Memory is static. Cars, pedestrians, objects, players, triggers, script lines, sprite nodes, collision
nodes: each is a fixed array reserved in the program's data and reused level after level. A level start
clears what it needs and leaves the rest, so some values survive from the previous level or the previous
occupant of a slot, and the port keeps that. When a pool is full the game either stops quietly (a sprite
past the three-hundredth is not drawn) or stops with a fatal error (the map's change area overflows).

## Time

The game advances in fixed frames, but it does not have a clock of its own. It borrows one from the sound
library: a Miles timer ticking 70 times a second. Each pass of the game loop waits for the third tick since
the last pass, so the game runs at 70 / 3, about 23.3 frames a second. Everything is counted in those
frames: speeds are pixels per frame, timers are frame counts, a briefing lasts so many frames.

The wait does not catch up. If a frame takes longer than three ticks, the next one simply starts later, so
on a slow machine the whole game slows down rather than skipping. Without sound there is no timer and the
loop runs as fast as the machine allows. F8 turns the limiter off on purpose. The frontend is paced
separately, by sleeping out the rest of 35 ms per pass.

The game is deterministic. Its own random generator is a small lagged additive generator of five words, and
the level start reseeds it with the same five values every time, so every start of a level is identical.
The C library's `rand`, never seeded, serves the sound's pitch variations and the skid marks. The
simulation is integer arithmetic in 16.16 fixed point, with one exception: the car's rigid body uses
floating point, which the port reproduces operation by operation (the original's x87 code computes in
double precision and rounds to single only where it stores).

Determinism is what makes the replays work. The controls are read into one 32-bit word per frame that only
records changes (a new steering value, a key pressed or released), so a replay is just the frames whose
word was not zero. Played back into the same level start, they give the same game.

The port uses the same property for its tests. It counts the 70 Hz ticks from the amount of audio it has
mixed instead of a wall clock, so a headless run with scripted input gives the same frames and the same
sound on every machine, and the test compares hashes of both.

## The data

The data root holds the city maps (`.CMP`), the styles (`.G24`: the tiles, sprites, palettes and object
and car records of one city), the mission file (`MISSION.INI`), the text in each language (`.FXT`), the
fonts (`.FON`), the frontend's pictures (raw 24-bit images), the sound banks (a `.SDT` index into a raw
`.RAW` sample file per city) and the music as WAV tracks. The executable runs from `WINO\` and opens
everything as `..\gtadata\...`.

Each city is a pair: its map, and the style the map's header names. Liberty City, San Andreas and Vice
City use the three styles, and the sound bank follows the style. Many of the game's tables live in neither
file but inside the executable: the vehicle, ped and weapon tables, the slope shapes,
the camera parameters. The port reads those from the user's own copy of the executable at run time, so
none of them are in its source.

The map format shows how much a 1997 PC game had to economise. The city is 256 × 256 squares, each a column
of up to six blocks. A column is a list of indices into a table of 8-byte block descriptors, and identical
columns are shared, so the whole city fits in a few hundred kilobytes. A block descriptor packs its type
(air, water, road, pavement, field, building), its traffic directions, a slope number, the rotation and
flipping of its faces and the five tile numbers (four sides and a lid) into eight bytes. See
[Data formats](/formats).

The language files are encrypted with a trivial rolling key, mainly to stop casual editing. The mission
file is plain text, a grammar of object lines and command lines that the designers wrote by hand (below).

## The world

The world is a grid of blocks 64 pixels on a side, six layers deep, numbered from the top: layer 0 is the
highest, and world height grows downward. Street level is usually the lid of the fifth layer, water the
sixth. Positions are 16.16 fixed point in pixels, angles are 1024 steps to the turn, and sine and cosine
come from a table built at start-up.

A block's type describes the space a walker or a car is in, and the block below provides the ground. Most
queries go through a cache built at map load: one byte per cell holding its type, its directions and
whether it is a slope. The road network is nothing more than these direction bits on road blocks: traffic,
police and path finding all read them. Slopes are ramps one, two or eight blocks long, each block a segment
of the ramp.

The map is shared, read-only data during play, but the game can change it: doors animate block faces,
scripts rewrite tiles and types, explosions leave marks. The first edit to a block copies its column and
descriptor into a reserved change area and points the map at the copies; later edits change the copy. The
area is fixed, and running out of it is fatal.

Moving things are found through a collision grid: 128 × 128 cells of two by two blocks, each a linked list
of nodes pointing at the cars, peds and objects in it. A query looks at the 3 × 3 cells around a position.
The renderer uses the same grid to find the sprites in view.

The map also carries a few lists for the systems that need them: object positions (parked cars, street
furniture), routes for the scripted vehicles and the roadblock layouts, the service locations (police
stations, hospitals, fire stations) and the named areas the HUD and the police scanner announce.

## The gameplay

### Cars

There are 400 car slots, and each has a reserved driver among the ped slots (car n's driver is ped 200 + n),
so a car never has to look for one. A car has a control mode that says who drives it: traffic, the player
or the physics, an AI driver (police, ambulance, fire engine), or a hunter. Only cars inside a player's view
(plus a margin), or held active by a script or an AI, are updated at all.

The central design choice is a hybrid. A traffic car is a kinematic "dummy": it follows the road with
integer pose updates and no forces. The moment it hits something (the map, a heavy object, another car) it
becomes a rigid body. A rigid body that is not the player's and has nearly stopped turns back into a dummy.
Cheap traffic, and physics only where something happens.

The rigid body is two-dimensional, integrated once per frame with semi-implicit Euler. It has only two
tyres, one front and one rear on the car's axis. Each tyre splits its point velocity into along-wheel and
across-wheel parts and pushes back on each with an adhesion factor; the engine drives at the rear; the
footbrake is split between the two by a bias, and the handbrake cuts the rear's lateral grip so the back
slides out. The sum of the sideways forces is the "skid" that leaves marks. There is no gearbox: gear is
reverse, neutral or forward. On a straight road above a modest speed the car is nudged onto the road's
axis, a small driving assist. The handling numbers are one record per model, in the style; a development
tuning file that could override them is read if present but is not in the shipped data.

Every car, dummy or not, moves the same way: propose the next pose, take the car out of the collision grid,
test the proposed hitbox against the map, then against objects and cars, correct the pose, settle it on the
ground, and put it back. Against walls the game bisects back to the last free pose and bounces; against
other cars the damage depends on the difference of mass times speed. Cars fall at a fixed rate when there is
no ground under them, take damage when they land, and sink in water.

Damage runs to 100. At 100 the car explodes, its driver dies, the attacker scores, the crime is reported and
the wreck burns. Damage is also drawn: the car sprite carries patch streams ("deltas") for dents and open
doors, composed into a small cache of sprite variants. Bikes throw their riders, and the tank has a turret
and rockets.

### Pedestrians and the player

There are 620 ped slots: ambient and mission peds, the 400 car drivers and a few special ones. The player
is a ped like any other, steered by the control word instead of an AI. Peds walk, run, turn, jump onto or
over slow cars, fall from heights, and drown. Their animation is one state machine advanced every other
frame.

Ambient peds are spawned just outside the view on the side the player is moving toward, on pavement, a
couple a frame up to a cap, and vanish when no player has seen them for a short while. Every so often one
leads a small group. They keep to the pavement, wait to cross roads and panic at gunfire.

Getting into a car is a short sequence: walk to the driver's door (its offset comes from the car's record,
rotated by the car's heading), open it, pull out any driver, climb in, close it. The car becomes a rigid
body under the player's controls. Getting out needs a slow car and a side that is not against a wall.

Weapons are few (pistol, machine gun, rocket launcher, flamethrower), gated by a reload counter; projectiles
are objects in their own small pool, moved and tested after the other objects.

### Traffic

Traffic cars are never created during play. The level start creates the number of cars the mission asks for
and deletes them at once, which reserves their slots for traffic; the models come from a shuffled list per
city. During play each player has a target number of traffic cars near the view, which falls as the wanted
level rises to leave room for the police. A spawn takes a reusable car, picks a view edge (rotating through
the four and the player's heading), and scans the blocks just outside the view for a road whose direction
points inward and that no screen can see.

A traffic car follows the direction bits. It turns where the road ahead ends, and whether it takes an
optional side road depends on its slot number: a fixed, per-car choice that looks random. It keeps a target
offset across the road for its lane, looks ahead with two boxes that grow while the road is clear, slows or
stops for what is in them, and stops at red lights it has not yet passed. Lights and give-way only apply on
screen. The traffic lights are found at level start by flooding each junction, matching its arms against a
handful of templates and placing a light per arm; one global cycle runs them all, crossing streets half a
cycle apart.

Vehicles that must go somewhere specific (police going home, ambulances, fire engines, mission dummies)
share one path finder. Only one controller can use it at a time, and it expands a few nodes a frame, so a
search spreads over several frames. Its nodes are the turning points of the road network and its cost is
the distance to the goal, which makes it a greedy best-first search rather than a shortest-path one.

### Police and emergency services

Crimes are reported with a kind and a place and add wanted points: a little for hitting a car, more for
stealing one, a lot for shooting. Each city has its own thresholds for the four wanted levels and its own
number of units per level. A pursuit starts after a delay (shorter in a car, immediate if a police car was
involved) and the police scanner announces it, a sentence built word by word from recorded fragments:
an opening, the car model, the area, a direction. The rules that choose the words are not documented yet.

The police are a large state machine per cop: patrol, dispatch, follow a path, chase, close in, get out,
drag the criminal out, arrest, go home. Up to four pursuit groups of up to twenty cops run at once, and the
nearest cop becomes the lead every frame. Higher levels allow shooting, then shooting on sight, and the lead
cop places roadblocks at junctions ahead from layouts stored in the map. An arrest clears the wanted level.

The chase steers greedily toward the criminal, which on its own would leave cops circling. What makes the
police feel relentless is a cheat: cops the player cannot see are moved closer once the criminal has gone
far enough. The same idea, things only happen properly on screen, runs through the whole game.

Ambulances come to the dead: victims are queued, a crew drives from the nearest hospital, a medic walks over
and revives them. Fire engines answer fires from the nearest station, stop short of the fire, and spray a
jet of chained objects until it is out. Gangs hunt a player riding one of their bikes. A scripted helicopter
can land, pick up the player and end the level.

### Objects, explosions and power-ups

Objects are a pool of 3500, each typed by a record in the style: size, sprite, weight, what it breaks into.
The frame update walks four lists rather than the whole pool: moving objects (kicked, sliding, tumbling,
sinking, breaking into their children on landing), timers (bombs), animated objects (fires, smoke) and
attached ones (lights and doors on cars, a fire engine's hose).

Explosions are drawn as four quarter sprites and hurt by distance: close peds die, farther ones catch fire,
loose objects fly, gas tanks chain through a short delay queue, crates open, cars are wrecked. Power-ups are
crates that reveal a weapon, armour, a speed-up, a bribe, an extra life or a multiplier when hit. Doors are
block faces animated through tiles allocated after the style's own; they report "opened" and "closed" to the
script.

### Trains

The elevated trains run on blocks flagged as rail. At level start a tracer walks each loop from block to
block, recording stations and starting points; a loop without a station is a fatal error. A train is four
carriages on two bogies each. The bogies follow precomputed paths through each block (straight, corner, and
three-block curves of 150 steps a quarter turn), and each carriage sits between its bogies. At a station
the doors open, a few passengers get off, the train waits a fixed boarding time, closes and leaves when the
next station is free. The player can ride, take over and drive (braking from a stop reverses the train),
and crash it. San Andreas has two loops but no trains: its starting points lie on road blocks, which the
creation code skips. The port keeps that.

### Missions and scripting

A mission is a section of `MISSION.INI`: a header of six numbers (how many traffic cars to reserve, whether
the police are on, ...), object lines and command lines.

An object line gives a line number, coordinates, a type and parameters: a car, a ped, an object, the player,
a door, a trigger, a crane, a phone, a counter, or a "future" car or ped that a command will create later.
Each becomes a script object holding the entity it created (or a value) and its position. A digit before
the coordinates makes the object persistent, so it survives a reset. Commands never hold entities directly;
they name object lines, and the runtime looks up what the line currently stands for. That indirection lets
a script refer to a car that does not exist yet, or one that has been replaced.

A command line is a label, an opcode and five numbers. There are about 150 opcodes, and most share one shape:
an object line, a success label, a fail label, a parameter and a score. A command that neither succeeds
nor fails is simply run again next frame. That retry is the whole waiting system: "wait until the player is
in this car" is one command that keeps failing to finish until it does. Some commands take a time limit and
fail when it runs out. A label of 0 means the next command, a negative one stops the thread.

The script runs on 32 cooperative processes. The first ones belong to the players and start at command 0;
triggers and kick-start commands start the rest. Each frame the triggers are checked, then every active
process takes one step in order. Each process has an owner, so ending a mission thread ends everything it
started, and a reset stops everything but the protected threads (the kill frenzies, the phones).

```text
frame:  triggers ─▶ fire ─▶ start process at label / open door / respray / ...
        process 0 ─▶ step: command at pc ─▶ success ─▶ score, pc = success label
        process 1 ─▶                     └─ fail    ─▶ pc = fail label
        ...                              └─ neither ─▶ same pc next frame
```

The player's thread usually waits at the phones. Answering a ringing phone kills the other threads, clears
the current job and jumps to the mission's start. A job then chains commands: brief the player on the pager,
create cars and peds with AI presets, wait for the player to reach a place or steal a car, give score, and
end with a result. A finished job raises the score multiplier. Triggers are squares on the map that fire
when the player's car or ped enters them (height is never tested): they open garages, run the respray and
bomb shops, check a car's model, and start threads. Kill frenzies are threads that count the points scored
since they started.

The level ends when the target score is reached, when every player's thread has ended (out of lives), or
through a scripted exit such as the helicopter. Score, multiplier and lives are the progression: a level is
passed by reaching its target score, not by finishing a list of missions.

## Saving

There is no save in the middle of a level. Progress is one small file, `PLAYER_A.DAT`: the settings, a
language override, the top three scores per level, and eight player records with a name, the best score per
level (which also unlocks the levels) and the cutscenes seen. The results screen updates it after a level,
and the menus save it when a screen is left with Enter and reload it on Esc, which undoes the screen's
changes. Certain player names entered on the rename screen turn on cheats.

## Presentation

### The camera

The camera looks straight down from above the player. Its height and zoom ease toward targets with one
rule shared by every field: move by half the velocity, accelerate the velocity toward the target, snap when
the target is passed. Speed raises the camera and lowers the zoom, so a fast car sees more of the city, and
a look-ahead along the heading shows where it is going. A level starts zoomed out and settles in.

### The city

The renderer is a painter's algorithm over the block grid, with no depth buffer. It draws the layers from
the lowest up. For each layer it projects the layer's top plane with a perspective divide (a grid of vertices
built incrementally, so neighbouring corners meet exactly), draws that layer's sprites, then the layer's
blocks. Within a layer it walks the blocks from the screen's edges toward the centre, so the blocks nearer
the camera axis, whose outward faces are seen, overdraw the farther ones. A side face is drawn only if its
upper edge projects outside its lower one: back-face culling for free. Slopes are drawn as textured quads
and a triangle per segment.

All of it is DMA's own software rasteriser: trapezoid fillers for faces and a general polygon filler for
sprites and slopes, stepping 16.16 edges, with texture coordinates that wrap in 8-bit registers inside
256 × 256 pages of tiles, a cache of rotated tiles, and a blend table for the transparent effects. The port
keeps the exact arithmetic, including where a step past a tile's edge reads its neighbour in the page, so
its pixels are the original's. Nothing clears the back buffer: a pixel no block covers keeps an older frame.

Tiles and sprites are 8-bit. Every tile has four palettes (one per face direction or lid remap) and every car
type a dozen remaps, which is how one sprite becomes many coloured cars and peds. The palettes are converted
once to the display's pixel format, and the port draws the 32-bit path. Animated tiles (water) swap their
texture for auxiliary tiles every few frames.

Sprites are collected each frame from the collision grid over the view, sorted into one binary tree per
layer by depth, and drawn between the layers. A ped on the street therefore falls in the layer above the
street's lid, and the walls of the next layer are drawn over it, even walls behind it: one of the visual
quirks the port keeps.

### HUD and menus

The HUD is drawn over the city in the same frame: the pager in the top left with its scrolling messages and
timers, the weapon and ammunition, the wanted level as police heads, the area name in a street sign and
the car's name in a car sign, the rolling score with lives and multiplier, subtitles with a speaker icon,
the big centred messages, an arrow toward the current target, and the pause and quit screens.

The frontend reads one key per pass, so holding a key does nothing. Every screen redraws everything over an
animated logo. After a level a cutscene still with animated layers plays to the briefing speech, then the
results.

### Sound

Sound runs on Miles with a fixed set of channels: ten loops, a few positional one-shots, and single channels
for voices, the scanner and the music stream. Each frame the game scans the world for loop sources (engines,
sirens, skids, horns, fires, trains, cranes, the helicopter), keeps the ten nearest, and matches them to the
ten playing channels, sliding pitch, volume and pan toward their new values, with a crude Doppler term from
the change in distance. Engine pitch comes from a per-model sound function. Volume falls off with distance
and halves under cover.

The radio is a set of stations, a few tracks each, chosen by the car's model. A track starts at a position
derived from the time, so the broadcast seems to have been going already, and a tunnel adds static. The
port reimplements the Miles software mixer the game used, with its arithmetic, rather than a modern one.

### The intro

The intro movie is a Smacker file played before the menus: the publisher's logo and DMA's walking "D" being
run over by a car. The port decodes it with a clean-room decoder of its own.

## What is not understood yet

- Several record fields of the cars, peds, trains and AI controllers have no known purpose.
- A few crime kinds and trigger kinds are unnamed, some do nothing, and two opcodes have no handler.
- How the police scanner chooses its words.
- The tyre constants are known as values, not as the physical reasoning behind them.
- The renderer and sprites have not yet been compared against frames captured from the original.
- Network play: the game's DirectPlay layer is stubbed as a machine without a network, so the network
  rules of the script (score races, kill targets) have only been read, not run.

## Lessons for building such a game

- **Simulate only what is seen.** Cars outside every view are not updated, traffic and peds spawn just off
  screen and vanish when unseen, lights only stop cars on screen, and unseen cops are moved closer. A whole
  city feels alive while only a few screens of it run.
- **Make physics something objects fall into.** Traffic is kinematic until something hits it, and returns
  to kinematic when it settles. The expensive model runs for the few cars where it shows.
- **Let the map carry the rules.** Direction bits on road blocks are the whole road network for traffic,
  police and path finding; block types decide ground, water and walls. Designers shape the AI by painting
  the city.
- **Script by retrying.** A command that has not happened yet simply runs again next frame. Waiting, polling
  and time limits come out of one rule, and many small cooperative threads make missions without an event
  system.
- **Refer to things by name, not by pointer.** Script commands name object lines and look up what they
  currently are, so a mission can talk about a car before it exists or after it has been replaced.
- **Reserve everything up front.** Fixed pools for every kind of entity make memory predictable and cheap;
  decide for each pool what happens when it runs out.
- **Make it deterministic and record only the input.** A reseeded generator and a control word of changes
  give replays almost for free, and the same property lets a port prove it matches.
- **Progress by score.** Passing a level by reaching a target score, with missions as one way to earn it
  among rampage, theft and chaos, is what makes the city a sandbox rather than a corridor of missions.

## Further reading

- [Game core](/game-core): the session, the level start, the frame loop, the delayed events, the random
  numbers, the collision grid, the mission file's grammar.
- [Cars](/cars), [Peds, player, input](/peds), [Traffic](/traffic), [Police](/police),
  [Objects](/objects), [Trains](/trains), [Missions](/missions).
- [City renderer](/render), [Sprites](/sprites), [HUD](/hud), [Frontend](/frontend),
  [Text, fonts, images](/text-fonts), [Sound and music](/audio), [Intro movie](/movie).
- [Data formats](/formats) and the [reverse-engineering notes](/re/), with the addresses.
- [Credits](/credits): the published and community sources the port relied on.
