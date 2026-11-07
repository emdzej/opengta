# Credits and licences

**OpenGTA** is released under the [GNU General Public License v3.0](https://github.com/emdzej/opengta/blob/main/LICENSE).
It contains no code, art, sound or data from the original game: everything is read from your own copy at run
time.

**Grand Theft Auto** is © 1997 DMA Design / Rockstar Games. OpenGTA is not affiliated with them.

## Published and community sources

- **DMA Design's *CityScape Data Structure* (`cds.doc` v12.10)** — DMA's own description of the map and style
  formats. The exe stays the authority.
- **Carnage3D** — the open-source GTA 1 engine (MIT), consulted as prior art. No code was copied.
- **[MultimediaWiki: Smacker](https://wiki.multimedia.cx/index.php/Smacker)** — the basis for the clean-room
  intro-movie decoder.
- **[Scale2x](https://www.scale2x.it)** (Andrea Mazzoleni) and **xBR** (Hyllian) — the upscalers behind
  `upscale=`, implemented from their public descriptions.
- **RFC 1950 / 1951** — the inflate used for InstallShield cabinets and skin PNGs.
- The GTA 1 box art (DMA Design / BMG, 1997) gave this site its colour palette.

## Reference implementations

- The DOS version and the Director's Cut builds explain leftovers and dead code in the Windows exe.

## Tools

[Ghidra](https://ghidra-sre.org), [ghidra-cli](https://github.com/akiselev/ghidra-cli), [wasi-sdk](https://github.com/WebAssembly/wasi-sdk), [VitePress](https://vitepress.dev). The file layer comes from OpenRF and the inflate and cabinet reader from OpenBallance.

## Runtime

[gasm](https://gasm.emdzej.pl) (MIT): `gasm-run`, the C SDK, and on this site `@emdzej/gasm-host`. OpenGTA is part of
[ENCORE](https://encore.emdzej.pl), with its sibling reimplementations.

If we have missed someone whose work this project relied on, please
[open an issue](https://github.com/emdzej/opengta/issues) — gaps in these credits are bugs.
