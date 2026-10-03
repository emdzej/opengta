// OpenGTA in the browser: opengta.wasm in gasm's Worker mode. The worker runs the game and reads the data on
// demand, either from the site's OPFS copy (FileSystemSyncAccessHandle) or straight from the picked files
// (FileReaderSync). The page keeps input, display and audio. OpenGTA draws in 2D (video_present, 640 x
// 480): the page scales the frames up with gasm's WebGL 2 presenter ('sharp' filter), or a 2D canvas.
//
// Test and debug query parameters:
//   front, map, x, y, z, mission   passed to the game (docs/guide/parameters.md)
//   hashframes=N   run N frames on virtual time as fast as possible, then print the same
//                  "frames=... video_fnv32=... audio_fnv32=..." line as gasm-run --headless
//                  (globalThis.__opengtaResult); in-memory storage, like gasm-run's headless runs
//   batch=N        with hashframes: frames per worker batch (default 250)
//   input=SCRIPT   with hashframes: scripted input, gasm-run's --input syntax ("60:KEY(Enter),...")
//   autoplay       start the imported data right away
import { BrowserInput, ProcExit, Resampler } from './vendor/gasm/gasm-host.js';
import { GasmWorker } from './vendor/gasm/gasm-worker.js';
import { GlPresenter } from './vendor/gasm/gasm-present.js';
import { InputScript } from './vendor/gasm/input-script.js';
import * as data from './data.js';

const $ = (id) => document.getElementById(id);
const query = new URLSearchParams(location.search);
const GAME_PARAMS = ['intro', 'front', 'map', 'x', 'y', 'z', 'mission'];
const HASH_FRAMES = Number(query.get('hashframes') || 0);
const BATCH = Number(query.get('batch') || 250);   // hash runs: frames per worker batch
const STORAGE = 'opengta';                // gasm:storage namespace (IndexedDB): PLAYER_A.DAT
const hasOpfs = !!navigator.storage?.getDirectory;

function message(text, kind = 'info') {
  const m = $('message');
  m.hidden = !text;
  m.textContent = text ?? '';
  m.className = `message ${kind}`;
  if (text) console.log(`[play] ${text}`);
}
const show = (id, on) => { $(id).hidden = !on; };

// ---- keyboard and gamepads ------------------------------------------------------------------
// The game reads the raw keyboard itself (input_mode KEYS_RAW; src/platform_gasm.c maps gasm's keys to the
// DirectInput scan codes the original reads), so the page only collects it. Gamepads are virtual pads 0-3,
// in connection order (the city viewer reads pad 1).
const canvas = $('screen');
const typing = (e) => e.target instanceof HTMLInputElement || e.target instanceof HTMLTextAreaElement;
const keyboard = new BrowserInput(canvas, { ignore: (e) => !running || typing(e) }).attach();
// While playing, keys shouldn't scroll the page or open the browser's menus (Alt, F10); browser shortcuts
// (Cmd/Ctrl + key) still work. A tap of Escape goes to the game; holding it for a second stops the game,
// like gasm-run.
const browserKey = (e) => e.metaKey || (e.ctrlKey && !/^(Control|Shift)/.test(e.code));
let escDown = 0;
addEventListener('keydown', (e) => {
  if (!running || typing(e)) return;
  if (e.code === 'Escape') { if (!e.repeat) escDown = performance.now(); return; }
  if (!browserKey(e)) e.preventDefault();
});
addEventListener('keyup', (e) => { if (e.code === 'Escape') escDown = 0; });
addEventListener('blur', () => { escDown = 0; });

// W3C "standard" gamepad mapping -> gasm button bit (A east, B south, X north, Y west, like gasm's app.js).
const PAD = { 1: 0, 0: 1, 3: 2, 2: 3, 4: 4, 5: 5, 6: 4, 7: 5, 8: 6, 9: 7, 12: 8, 13: 9, 14: 10, 15: 11 };
function readPads() {
  const pads = [0, 0, 0, 0];
  let n = 0;
  for (const gp of navigator.getGamepads?.() ?? []) {
    if (!gp || n > 3) continue;
    let m = 0;
    for (const [btn, bit] of Object.entries(PAD)) if (gp.buttons[btn]?.pressed) m |= 1 << bit;
    const [x = 0, y = 0] = gp.axes;
    if (x < -0.5) m |= 1 << 10; if (x > 0.5) m |= 1 << 11;
    if (y < -0.5) m |= 1 << 8; if (y > 0.5) m |= 1 << 9;
    pads[n++] |= m;
  }
  return pads;
}

// ---- audio: the gasm web player's AudioWorklet queue ------------------------------------------
const WORKLET = `
class GasmOut extends AudioWorkletProcessor {
  constructor() {
    super();
    this.q = []; this.off = 0; this.len = 0; this.primed = false;
    this.target = Math.round(sampleRate * 0.06) * 2; this.max = Math.round(sampleRate * 0.2) * 2;
    this.port.onmessage = (e) => {
      this.q.push(e.data); this.len += e.data.length;
      while (this.len > this.max && this.q.length > 1) { this.len -= this.q[0].length - this.off; this.q.shift(); this.off = 0; }
    };
  }
  process(_, [out]) {
    const L = out[0], R = out[1] ?? out[0];
    if (!this.primed && this.len >= this.target) this.primed = true;
    for (let i = 0; i < L.length; i++) {
      if (!this.primed || this.len < 2) { this.primed = false; L[i] = R[i] = 0; continue; }
      const b = this.q[0];
      L[i] = b[this.off]; R[i] = b[this.off + 1];
      this.off += 2; this.len -= 2;
      if (this.off >= b.length) { this.q.shift(); this.off = 0; }
    }
    return true;
  }
}
registerProcessor('gasm-out', GasmOut);`;

let audioCtx = null, audioNode = null, gain = null, resampler = null, audioReady = null;
let muted = localStorage.getItem('opengta.muted') === '1';
/** Call from a click handler (browsers only start audio on a user gesture). */
function startAudio() {
  if (!audioCtx) {
    try {
      audioCtx = new AudioContext({ latencyHint: 'interactive' });
    } catch (e) { message(`No sound: ${e.message}`, 'warn'); return; }
    audioReady = audioCtx.audioWorklet.addModule(URL.createObjectURL(new Blob([WORKLET], { type: 'text/javascript' }))).then(() => {
      audioNode = new AudioWorkletNode(audioCtx, 'gasm-out', { outputChannelCount: [2] });
      gain = new GainNode(audioCtx, { gain: muted ? 0 : 1 });
      audioNode.connect(gain).connect(audioCtx.destination);
      resampler = new Resampler(audioCtx.sampleRate);
    }).catch((e) => { message(`No sound: ${e.message}`, 'warn'); audioCtx = null; });
  }
  audioCtx?.resume();
}
function onAudio(samples, rate, channels) {
  if (!audioNode) return;
  const out = resampler.process(samples, rate, channels);
  audioNode.port.postMessage(out.slice());
}
function setMuted(m) {
  muted = m;
  localStorage.setItem('opengta.muted', m ? '1' : '0');
  if (gain) gain.gain.value = m ? 0 : 1;
  $('sound').textContent = m ? 'Sound: off' : 'Sound: on';
}
$('sound').onclick = () => { startAudio(); setMuted(!muted); };
setMuted(muted);

// ---- display ------------------------------------------------------------------------------
// WebGL 2 with gasm's 'sharp' filter (even pixels at any size, letterboxed), else a 2D canvas scaled by CSS.
const presenter = GlPresenter.create(canvas);
const ctx2d = presenter ? null : canvas.getContext('2d');
if (!presenter) canvas.classList.add('fit2d');
let lastFrame = null;
const displaySize = () => [Math.max(1, Math.round(canvas.clientWidth * (devicePixelRatio || 1))) || 960,
                           Math.max(1, Math.round(canvas.clientHeight * (devicePixelRatio || 1))) || 720];
function present(f) {
  lastFrame = f;
  if (presenter) presenter.draw(f.rgba, f.width, f.height, displaySize(), { filter: 'sharp', aspect: f.aspect });
  else {
    if (canvas.width !== f.width || canvas.height !== f.height) { canvas.width = f.width; canvas.height = f.height; }
    ctx2d.putImageData(new ImageData(f.rgba, f.width, f.height), 0, 0);
  }
}
// Redraw at the new size (fullscreen, window resize) without waiting for the next frame.
new ResizeObserver(() => { if (lastFrame && presenter) present(lastFrame); }).observe(canvas);
/** The last game frame as a PNG data URL, at the game's own size (tests). */
async function framePng() {
  if (!lastFrame) return null;
  const c = new OffscreenCanvas(lastFrame.width, lastFrame.height);
  c.getContext('2d').putImageData(new ImageData(new Uint8ClampedArray(lastFrame.rgba), lastFrame.width, lastFrame.height), 0, 0);
  const blob = await c.convertToBlob({ type: 'image/png' });
  return new Promise((r) => { const fr = new FileReader(); fr.onload = () => r(fr.result); fr.readAsDataURL(blob); });
}
$('fullscreen').onclick = () => {
  if (document.fullscreenElement) document.exitFullscreen();
  else ($('stage').requestFullscreen ?? $('stage').webkitRequestFullscreen)?.call($('stage'));
};
canvas.ondblclick = () => $('fullscreen').onclick();

// ---- running the game ---------------------------------------------------------------------
let worker = null, running = false, inflight = false, rafId = 0, wasmBytes = null, lastLog = '';
let acc = 0, last = 0, fpsN = 0, fpsT = 0;

async function loadWasm() {
  if (!wasmBytes) {
    const r = await fetch(new URL('opengta.wasm', import.meta.url));
    if (!r.ok) throw new Error(`opengta.wasm: HTTP ${r.status}`);
    wasmBytes = await r.arrayBuffer();
  }
  return wasmBytes.slice(0); // start() transfers its copy to the worker
}

function onLog(msg) {
  console.log(msg);
  if (/^\[guest\]/.test(msg)) lastLog = msg.replace(/^\[guest\]\s*/, '');
}

async function stopGame() {
  cancelAnimationFrame(rafId);
  running = false; inflight = false; escDown = 0;
  const w = worker; worker = null;
  await w?.exit();          // flushes the saves, releases the OPFS handles
}

function stopped(e) {
  running = false;
  if (e instanceof ProcExit) message('The game has ended. Press Play to start again.');
  else message(`The game stopped: ${e.message}${lastLog ? ` (${lastLog})` : ''}`, 'error');
  stopGame();
  endGameView();
}

function endGameView() {
  cancelAnimationFrame(rafId);
  show('game', false);
  show('setup', true);
  refreshImported();
}

/** Start the game from 'opfs' (the imported data) or a checked source (read in place). */
async function play(source) {
  startAudio();                        // still inside the click's user gesture
  message('');
  await stopGame();
  lastLog = '';
  lastFrame = null;
  show('game', true);
  show('setup', HASH_FRAMES > 0);      // keep the page visible in test runs
  $('status').textContent = 'Starting...';
  const params = Object.fromEntries(GAME_PARAMS.filter((k) => query.has(k)).map((k) => [k, query.get(k)]));
  try {
    await audioReady;
    worker = await GasmWorker.start({
      wasm: await loadWasm(), assets: data.assetSpecs(source), params,
      storage: HASH_FRAMES > 0 ? null : STORAGE,     // hash runs: in memory, like gasm-run --headless
      hashing: HASH_FRAMES > 0, virtualTime: HASH_FRAMES > 0, onLog, onAudio,
      onTitle: (t) => { document.title = `${t ?? 'Grand Theft Auto'} | OpenGTA`; },
    });
  } catch (e) {
    worker = null;
    const busy = /NoModificationAllowed|InvalidState|lock|access handle/i.test(`${e.name} ${e.message}`);
    if (busy) message('The imported data is in use by another OpenGTA tab or window. Close it and try again.', 'error');
    else if (e instanceof ProcExit) message(`The game could not start${lastLog ? `: ${lastLog}` : ''}.`, 'error');
    else message(`The game could not start: ${e.message}`, 'error');
    endGameView();
    return;
  }
  document.title = 'Grand Theft Auto | OpenGTA';
  if (HASH_FRAMES > 0) return hashRun(HASH_FRAMES);
  canvas.focus?.();
  acc = 0; last = performance.now(); running = true;
  rafId = requestAnimationFrame(tick);
}

function tick(now) {
  rafId = requestAnimationFrame(tick);
  if (!running || !worker) return;
  if (escDown && now - escDown >= 1000) {   // Escape held: stop (the tap already went to the game)
    $('stop').onclick();
    return;
  }
  // The rate changes with the game's state (frontend 1000/35 Hz, a level 70 Hz): the worker reports it.
  const period = 1000 / worker.frameRate;
  acc += Math.min(now - last, 100);    // clamp after tab switches
  last = now;
  // Fixed timestep; when catching up (at most 4 frames), only the last one is shown.
  const due = Math.min(4, Math.floor(acc / period));
  if (!inflight && due > 0) {          // one batch in flight: the worker sets the pace
    const pads = readPads();
    inflight = true;
    acc -= due * period;
    const steps = Array.from({ length: due }, (_, k) => ({ pads, input: keyboard.frame(k === 0) }));
    worker.frames(steps, true).then((r) => {
      inflight = false;
      fpsN += due;
      if (r.frame) present(r.frame);
      keyboard.setMode(worker?.inputMode ?? 0);
    }, stopped);
  }
  if (acc > period * 4) acc = 0;       // fell far behind: resync
  if (now - fpsT >= 1000) { $('status').textContent = `${fpsN} frames/s`; fpsN = 0; fpsT = now; }
}

const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
/** gasm-run --headless N [--input SCRIPT]: the same frames, the same input, the same hash line. */
async function hashRun(n) {
  let script;
  try { script = new InputScript(query.get('input') ?? ''); } catch (e) { message(`input: ${e.message}`, 'error'); return; }
  const st = {};
  let done = 0;
  try {
    while (done < n) {
      const k = Math.min(BATCH, n - done);
      const steps = Array.from({ length: k }, (_, i) => {
        const f = done + i;
        return { pads: [script.pad(f), 0, 0, 0], text: script.textAt(f), input: script.raw(f, st, [640, 480], worker.inputMode) };
      });
      const r = await worker.frames(steps, done + k === n);
      if (r.frame) present(r.frame);
      done += k;
    }
  } catch (e) { if (!(e instanceof ProcExit)) { message(`hash run failed: ${e.message}`, 'error'); throw e; } }
  const s = worker.stats;
  const out = `frames=${s.frames} presented=${s.presented} size=${s.width}x${s.height} ` +
              `video_fnv32=${hex(s.videoHash)} audio_fnv32=${hex(s.audioHash)} audio_frames=${s.audioFrames}`;
  $('status').textContent = out;
  console.log(out);
  globalThis.__opengtaResult = out;
}

$('stop').onclick = async () => { await stopGame(); endGameView(); };
addEventListener('pagehide', () => { worker?.exit(); });

// ---- choosing and importing the data --------------------------------------------------------
let checked = null; // result of data.folderFrom...

const describe = (layout) => (layout === 'installer' ? 'installer folder' : 'game folder');

function showCheck(result) {
  checked = result;
  const list = $('check-list');
  list.textContent = '';
  const item = (text, ok) => {
    const li = document.createElement('li');
    li.className = ok ? 'ok' : 'bad';
    li.textContent = text;
    list.append(li);
  };
  const src = result.source;
  item(`folder "${src.name}": ${result.files} files, ${mbText(result.bytes)}`, true);
  if (result.problems.length) {
    for (const p of result.problems) item(p, false);
    item('This does not look like GTA: choose the installed game folder or the unzipped GTAINSTALLER.zip.', false);
  } else if (src.layout === 'installer') {
    item('data1.cab and data2.cab found: the unzipped installer (the game is read from its cabinets)', true);
  } else {
    item(`GTADATA/MISSION.INI and ${data.EXE} (${data.EXE_SIZE.toLocaleString('en')} bytes) found: an installed GTA`, true);
  }
  show('check', true);
  show('check-actions', !result.problems.length);
  $('import').hidden = !hasOpfs;
  globalThis.__opengtaChecked = { problems: result.problems, files: result.files, bytes: result.bytes, layout: src.layout };
}
const mbText = data.mb;

$('pick-folder').onclick = async () => {
  message('');
  if (!window.showDirectoryPicker) return $('folder-input').click(); // Firefox, Safari
  let handle;
  try { handle = await showDirectoryPicker({ id: 'opengta-data', mode: 'read' }); } catch (e) {
    if (e.name !== 'AbortError') message(e.message, 'error');
    return;
  }
  message('Reading the folder...');
  try { showCheck(await data.folderFromHandle(handle)); message(''); } catch (e) { message(`Could not read the folder: ${e.message}`, 'error'); }
};
$('folder-input').onchange = (e) => {
  if (e.target.files.length) showCheck(data.folderFromFileList(e.target.files));
  e.target.value = '';
};

$('import').onclick = async () => {
  if (!checked) return;
  startAudio();
  const source = checked.source;
  show('check', false);
  show('progress', true);
  $('pick-folder').disabled = true;
  const t0 = performance.now();
  try {
    await stopGame();
    const info = await data.importSource(source, (p) => {
      $('progress-bar').value = p.total ? p.bytes / p.total : 1;
      $('progress-text').textContent = `Copying ${p.file}: ${mbText(p.bytes)} of ${mbText(p.total)} (${p.done} of ${p.files} files)`;
    });
    $('progress-text').textContent = `Imported ${info.files} files, ${mbText(info.bytes)} in ${((performance.now() - t0) / 1000).toFixed(0)} s.`;
    globalThis.__opengtaImported = info;
    await refreshImported();
    show('progress', false);
    await play('opfs');
  } catch (e) {
    show('progress', false);
    const full = /quota/i.test(`${e.name} ${e.message}`);
    message(full ? `Not enough browser storage for the game data (${mbText(checked.bytes)}). Free some space, or play without importing.`
                 : `Import failed: ${e.message}. You can still play without importing.`, 'error');
    show('check', true);
    await data.removeImport().catch(() => {});
    refreshImported();
  } finally {
    $('pick-folder').disabled = false;
  }
};
$('play-direct').onclick = () => { if (checked) play(checked.source); };
$('play-opfs').onclick = () => play('opfs');
$('remove').onclick = async () => {
  await stopGame();
  show('game', false);
  try { await data.removeImport(); message('The imported game data was removed from this browser.'); } catch (e) { message(`Could not remove it: ${e.message}`, 'error'); }
  refreshImported();
};
$('persist').onclick = async () => {
  const ok = await data.requestPersist();
  message(ok ? 'The browser will keep the imported data.' : 'The browser did not agree to keep the data; it stays until space runs low.', ok ? 'info' : 'warn');
  refreshImported();
};

async function refreshImported() {
  const info = hasOpfs ? await data.existingImport() : null;
  show('imported', !!info);
  $('choose-title').textContent = info ? 'Your GTA' : 'Choose your GTA';
  if (info) {
    $('imported-desc').textContent = `${describe(info.layout)} "${info.name}", ${info.files} files, ${mbText(info.bytes)}`;
    const s = await data.storageInfo();
    $('storage').textContent = s.usage !== undefined
      ? `This site uses ${mbText(s.usage)} of the ${mbText(s.quota ?? 0)} the browser allows${s.persisted ? '; kept even when space runs low' : ''}.`
      : '';
    show('persist-row', !s.persisted && !!navigator.storage?.persist);
  }
  return info;
}

if (!hasOpfs) message('This browser has no private file storage (OPFS), so the game data can\'t be imported. Play without importing works.', 'warn');
if (typeof Worker === 'undefined' || typeof WebAssembly === 'undefined') message('This browser can\'t run OpenGTA (it needs WebAssembly and Web Workers).', 'error');
const ready = refreshImported();
ready.then((info) => {
  globalThis.__opengtaReady = true;
  if (info && query.has('autoplay')) play('opfs');
});
globalThis.opengtaPlay = { play, stopGame, refreshImported, framePng, data };
