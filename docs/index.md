---
layout: home

hero:
  name: OpenGTA
  text: Grand Theft Auto, ported function by function
  tagline: A faithful reimplementation of GTA 1 (DMA Design, 1997) in C11, ported from the Windows version of Rockstar's 2002 re-release. It runs the game from your own copy, as one WebAssembly module for the gasm runtime, natively and in the browser. Early work in progress.
  image:
    src: /screenshots/menu.jpg
    alt: The start menu, as OpenGTA draws it
  actions:
    - theme: brand
      text: Get started
      link: /guide/
    - theme: alt
      text: Play in the browser
      link: /play/
      target: _self
    - theme: alt
      text: How it works
      link: /internals/

features:
  - title: Faithful
    details: Not an emulator and not a remake. Every subsystem is ported from the original executable, function by function, quirks and bugs included. Each ported function names its address in the original.
  - title: Your own data
    details: Reads the installed game folder (GTADATA, WINO) or the unzipped installer, straight from its InstallShield cabinets. Even the original's tables are read from its executable at run time. Nothing from the game is in OpenGTA.
    link: /guide/game-data
    linkText: Game data
  - title: Runs on gasm
    details: One opengta.wasm for macOS, Linux, Windows and the browser on the gasm WebAssembly game runtime. Runs are deterministic, so the native and browser runners print the same frame hashes.
    link: /guide/running
    linkText: Running on gasm
  - title: In the browser
    details: Choose your GTA folder or the unzipped installer once and run it in the browser. Your data stays on your device.
    link: /play/
    linkText: Play
    target: _self
  - title: Early, and honest about it
    details: The data layer, the city renderer, sprites, text and fonts, the frontend menus, sound and music, and the level start are ported. Peds, cars, the mission interpreter and the HUD are in progress.
    link: /guide/status
    linkText: Status
  - title: Reverse engineered in the open
    details: Every function of the original program has a name, and the notes on formats, the renderer and the game core are on this site.
    link: /re/
    linkText: Reverse-engineering notes
---

<div class="vp-doc" style="max-width: 1152px; margin: 48px auto 0; padding: 0 24px;">

## Built for gasm

<p><a class="gasm-badge" href="https://gasm.emdzej.pl"><img class="gasm-badge-light" src="https://gasm.emdzej.pl/badge/built-for-gasm-light.svg" alt="Built for gasm" width="120" height="44"><img class="gasm-badge-dark" src="https://gasm.emdzej.pl/badge/built-for-gasm-dark.svg" alt="Built for gasm" width="120" height="44"></a></p>

OpenGTA is one WebAssembly module, `opengta.wasm`, running on [gasm](https://gasm.emdzej.pl), a portable
game runtime: the same module natively (`gasm-run`) and in the browser. See
[Running on gasm](/guide/running), or [play in the browser](/play/){target="_self"}.

## Status

Early. The frontend runs as in the original: the start menu, options, player select and the city and
mission select, with their sounds and music, and the settings saved like `PLAYER_A.DAT`. Choosing a mission
starts the level through the ported level start and frame loop: the city is drawn by DMA's rasteriser,
with the player and parked cars as sprites. Walking, driving, traffic, the missions themselves and the
HUD are not playable yet. Details in [Status](/guide/status).

## Screenshots

All of these are OpenGTA's own output: `opengta.wasm` run headless on the game data by
`tools/screenshots.sh`.

<div class="shots">
  <figure><img src="/screenshots/cities.jpg" alt="The city and mission select over the map of the USA"><figcaption>City and mission select</figcaption></figure>
  <figure><img src="/screenshots/mission1.jpg" alt="The start of the first Liberty City mission: the player next to a parked car"><figcaption>Liberty City, mission 1: the level start</figcaption></figure>
  <figure><img src="/screenshots/mission-miami.jpg" alt="The start of the first Vice City mission"><figcaption>Vice City</figcaption></figure>
  <figure><img src="/screenshots/players.jpg" alt="The player select screen with a portrait"><figcaption>Player select</figcaption></figure>
  <figure><img src="/screenshots/city-nyc.jpg" alt="Liberty City in the city viewer: buildings and an elevated railway"><figcaption>The city renderer: Liberty City</figcaption></figure>
  <figure><img src="/screenshots/city-sanb.jpg" alt="San Andreas in the city viewer: an AUTO garage"><figcaption>San Andreas</figcaption></figure>
</div>

</div>
