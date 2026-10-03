#!/usr/bin/env node
// Headless Chrome test of the browser play page (docs/public/play/), through the page's own code: the
// folder is handed to its <input webkitdirectory> (CDP DOM.setFileInputFiles, as if picked), checked,
// imported into OPFS and run in gasm's Worker mode; then the imported copy is run again (a later visit),
// and the installed folder and the unzipped installer are run without importing (the File/Blob provider).
// Every run must print the hash line gasm-run --headless prints for the same frames and input, except
// for one thing the page can't see: gasm-run (and gasm's Node runner) call the guest's gasm_exit before
// printing, and OpenGTA's exit inside a level presents a last frame (Game_Run's end, Gfx_Present). The
// Worker API reports no stats after exit, so the page's line is the one before gasm_exit. The test checks
// that link too: the page against gasm's own GasmHost run in Node without the exit (same package, same
// input), and that one plus gasm_exit against gasm-run.
// Skins and hires: the sample skin (assets/skins/sample) is added through the page's skin picker (into
// OPFS) and 2x chosen in its resolution selector; that run must hash like gasm-run with the same
// parameters and assets (--asset-dir skins=assets/skins --param skin=sample --param hires=2).
//
//   docs/scripts/copy-wasm.sh && (cd docs && scripts/vendor-web.sh && pnpm build)
//   node tools/web-play-test.mjs [game folder] [installer folder] [out dir]
// Defaults: ./game, ./installer (skipped if missing), /tmp/opengta-play. Needs Chrome (CHROME=<binary>) and
// gasm-run (GASM_RUN=<binary>, default .deps/gasm-runner-macos-universal/gasm-run from
// tools/fetch-gasm-runner.sh). Serves docs/.vitepress/dist on 127.0.0.1:8793. Never opens a window.
import { spawn, execFileSync } from 'node:child_process';
import { existsSync, mkdirSync, mkdtempSync, rmSync, writeFileSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, resolve } from 'node:path';
import { fileURLToPath, pathToFileURL } from 'node:url';

const repo = fileURLToPath(new URL('..', import.meta.url));
const GAME = resolve(process.argv[2] ?? join(repo, 'game'));
const INSTALLER = resolve(process.argv[3] ?? join(repo, 'installer'));
const OUT = resolve(process.argv[4] ?? '/tmp/opengta-play');
const DIST = join(repo, 'docs/.vitepress/dist');
const WASM = join(DIST, 'play/opengta.wasm');   // the module the page runs: gasm-run gets the same one
const PORT = 8793, DEBUG = 9336;
const CHROME = process.env.CHROME ?? '/Applications/Google Chrome.app/Contents/MacOS/Google Chrome';
const RUN = process.env.GASM_RUN ?? join(repo, '.deps/gasm-runner-macos-universal/gasm-run');
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));
mkdirSync(OUT, { recursive: true });
if (!existsSync(WASM)) { console.error('build the site first (docs/scripts/copy-wasm.sh, scripts/vendor-web.sh, pnpm build)'); process.exit(1); }
if (!existsSync(CHROME)) { console.error(`no Chrome at ${CHROME} (CHROME=<binary>)`); process.exit(1); }

const SKINS = join(repo, 'assets/skins');   // the sample skin (tools/make-sample-skin.py)
const CASES = [
  { name: 'menu', frames: 60, input: '' },
  { name: 'mission1', frames: 400, input: '60:KEY(Enter),100:KEY(Enter),140:KEY(Enter)' },
];

// Reference hashes from the native runner (same module, same data, same input).
function native(c, dir) {
  if (!existsSync(RUN)) return null;
  const args = [WASM, '--asset-dir', dir, '--headless', String(c.frames), '--param', 'intro=0'];   // the scripts drive the menus
  for (const [prefix, d] of Object.entries(c.assets ?? {})) args.push('--asset-dir', `${prefix}=${d}`);
  for (const [k, v] of Object.entries(c.params ?? {})) args.push('--param', `${k}=${v}`);
  if (c.input) args.push('--input', c.input);
  return execFileSync(RUN, args, { stdio: ['ignore', 'pipe', 'ignore'] }).toString().trim().split('\n').join(' ');
}

// gasm's GasmHost in Node on a folder (as headless.mjs builds it), N frames of the script; the hash line
// before and after gasm_exit.
const gasm = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/gasm-host.js')).href);
const { InputScript } = await import(pathToFileURL(join(repo, 'docs/node_modules/@emdzej/gasm-host/input-script.mjs')).href);
async function nodeRef(c, dir) {
  const { readdirSync, lstatSync, openSync, readSync, closeSync, readFileSync } = await import('node:fs');
  const table = new gasm.AssetTable(() => {});
  const fds = [];
  const walk = (abs, segs) => {
    for (const d of readdirSync(abs, { withFileTypes: true })) {
      if (d.name.startsWith('.') || d.isSymbolicLink()) continue;
      const path = join(abs, d.name), s = [...segs, d.name];
      if (d.isDirectory()) walk(path, s);
      else if (d.isFile()) {
        const size = lstatSync(path).size;
        let fd = null;
        table.add(s.join('/'), { size: () => size, readAt: (off, dst) => {
          fd ??= (fds.push(openSync(path, 'r')), fds.at(-1));
          let done = 0;
          while (done < dst.length) { const k = readSync(fd, dst, done, dst.length - done, off + done); if (!k) break; done += k; }
          return done;
        } }, { fromDir: true });
      }
    }
  };
  walk(dir, []);
  for (const [prefix, d] of Object.entries(c.assets ?? {})) walk(d, [prefix]);
  table.finish();
  const script = new InputScript(c.input);
  const host = new gasm.GasmHost({ assets: table, params: { intro: '0', ...c.params }, storage: new gasm.MemoryStorage(), virtualTime: true, onLog: () => {},
    getPad: (p) => (p !== 0 ? 0 : script.pad(host.frameIndex)) });
  host.hashing = true;
  await host.load(await WebAssembly.compile(readFileSync(WASM)));
  const st = {};
  for (let f = 0; f < c.frames; f++) {
    host.text = script.textAt(f);
    host.input = script.raw(f, st, [host.gfx.width(), host.gfx.height()], host.inputMode);
    host.frame();
  }
  const hex = (h) => (h >>> 0).toString(16).padStart(8, '0');
  const line = () => `frames=${host.frameIndex} presented=${host.framesPresented} size=${host.width}x${host.height} ` +
    `video_fnv32=${hex(host.videoHash)} audio_fnv32=${hex(host.audioHash)} audio_frames=${host.audioFrames}`;
  const before = line();
  host.exit();
  const after = line();
  for (const fd of fds) closeSync(fd);
  return { before, after };
}

const PROFILE = mkdtempSync(join(tmpdir(), 'opengta-play-chrome-'));
// A static server for the site (in-process: python's http.server drops connections when the machine is
// busy, and the page loads its modules in parallel).
const { createServer } = await import('node:http');
const { readFile } = await import('node:fs/promises');
const TYPES = { '.html': 'text/html', '.js': 'text/javascript', '.mjs': 'text/javascript', '.css': 'text/css', '.json': 'application/json',
  '.wasm': 'application/wasm', '.svg': 'image/svg+xml', '.png': 'image/png', '.woff2': 'font/woff2' };
const server = createServer(async (req, res) => {
  let path = decodeURIComponent(new URL(req.url, 'http://x').pathname);
  if (path.endsWith('/')) path += 'index.html';
  const file = join(DIST, path);
  if (!file.startsWith(DIST)) { res.writeHead(403).end(); return; }
  try {
    const body = await readFile(file);
    res.writeHead(200, { 'content-type': TYPES[path.slice(path.lastIndexOf('.'))] ?? 'application/octet-stream' }).end(body);
  } catch { res.writeHead(404).end(); }
});
server.kill = () => server.close();
await new Promise((r) => server.listen(PORT, '127.0.0.1', r));
const chromeArgs = ['--headless=new', `--remote-debugging-port=${DEBUG}`, `--user-data-dir=${PROFILE}`,
  '--autoplay-policy=no-user-gesture-required', 'about:blank'];
if (process.env.CI) chromeArgs.unshift('--no-sandbox');
const chrome = spawn(CHROME, chromeArgs, { stdio: 'ignore' });

async function cdp() {
  let target;
  for (let i = 0; i < 50 && !target; i++) {
    await sleep(200);
    target = await fetch(`http://127.0.0.1:${DEBUG}/json`).then((r) => r.json()).then((t) => t.find((x) => x.type === 'page')).catch(() => null);
  }
  const ws = new WebSocket(target.webSocketDebuggerUrl);
  await new Promise((r) => (ws.onopen = r));
  let id = 0; const pending = new Map(); const logs = [];
  ws.onmessage = (e) => {
    const m = JSON.parse(e.data);
    if (m.id && pending.has(m.id)) { pending.get(m.id)(m); pending.delete(m.id); }
    if (m.method === 'Runtime.consoleAPICalled') logs.push(m.params.args.map((a) => a.value ?? a.description).join(' '));
    if (m.method === 'Runtime.exceptionThrown') logs.push(`EXCEPTION ${m.params.exceptionDetails.exception?.description ?? m.params.exceptionDetails.text}`);
  };
  const send = (method, params = {}) => new Promise((r) => { const i = ++id; pending.set(i, r); ws.send(JSON.stringify({ id: i, method, params })); });
  await send('Runtime.enable'); await send('Page.enable'); await send('DOM.enable');
  const evaluate = async (expr) => (await send('Runtime.evaluate', { expression: expr, returnByValue: true, awaitPromise: true })).result.result?.value;
  const until = async (expr, ms) => {
    const t0 = Date.now();
    while (Date.now() - t0 < ms) { const v = await evaluate(expr); if (v) return v; await sleep(200); }
    throw new Error(`timeout waiting for ${expr}\n  ${logs.slice(-8).join('\n  ')}`);
  };
  const setFiles = async (selector, files) => {
    const doc = await send('DOM.getDocument');
    const node = await send('DOM.querySelector', { nodeId: doc.result.root.nodeId, selector });
    const r = await send('DOM.setFileInputFiles', { nodeId: node.result.nodeId, files });
    if (r.error) throw new Error(`setFileInputFiles: ${r.error.message}`);
  };
  const open = async (url) => {
    await evaluate('delete globalThis.__opengtaChecked; delete globalThis.__opengtaResult; 0');
    await send('Page.navigate', { url });
    await until('document.readyState === "complete" && !!globalThis.__opengtaReady', 60000);
  };
  return { send, evaluate, until, setFiles, open, logs };
}

async function saveFrame(page, file) {
  const url = await page.evaluate('globalThis.opengtaPlay.framePng()');
  if (url) writeFileSync(file, Buffer.from(url.split(',')[1], 'base64'));
}

async function result(page, ms = 600000) {
  const t0 = Date.now();
  for (;;) {
    const r = await page.evaluate('globalThis.__opengtaResult');
    if (r) return { hash: r, secs: (Date.now() - t0) / 1000 };
    const err = await page.evaluate('document.getElementById("message").hidden ? "" : document.getElementById("message").textContent');
    if (/could not|stopped|failed/i.test(err)) throw new Error(`page: ${err}\n  ${page.logs.slice(-8).join('\n  ')}`);
    if (Date.now() - t0 > ms) throw new Error('timed out');
    await sleep(200);
  }
}

let failed = false;
const report = (ok, label, detail) => { failed ||= !ok; console.log(`${ok ? 'PASS' : 'FAIL'}  ${label}${detail ? `\n      ${detail}` : ''}`); };
const base = `http://127.0.0.1:${PORT}/play/`;
const hasInstaller = existsSync(join(INSTALLER, 'data1.cab'));
try {
  const page = await cdp();
  await page.send('Emulation.setDeviceMetricsOverride', { width: 1280, height: 1000, deviceScaleFactor: 1, mobile: false });
  await page.open(base);
  await sleep(2000); // let a fresh profile settle
  const shot = await page.send('Page.captureScreenshot', { format: 'png' });   // the page as Chrome renders it
  writeFileSync(join(OUT, 'page.png'), Buffer.from(shot.result.data, 'base64'));

  let imported = false;
  for (const c of CASES) {
    const nat = native(c, GAME);
    const ref = await nodeRef(c, GAME);
    if (!nat) console.log(`      no gasm-run at ${RUN}: the Node reference only`);
    else {
      console.log(`      gasm-run ${c.name}: ${nat}`);
      report(ref.after === nat, `${c.name}: gasm's Node host + gasm_exit = gasm-run`, ref.after === nat ? '' : ref.after);
    }
    if (ref.before !== ref.after) console.log(`      before gasm_exit (what the page sees): ${ref.before}`);
    const want = ref.before;
    if (hasInstaller && nat) {
      const natCab = native(c, INSTALLER);
      report(natCab === nat, `${c.name}: gasm-run, installer = game folder`, natCab === nat ? '' : natCab);
    }
    const url = `${base}?intro=0&hashframes=${c.frames}${c.input ? `&input=${encodeURIComponent(c.input)}` : ''}`;
    const check = (label, got) => report(!want || got.hash === want, `${c.name}: ${label}`,
      `${got.hash}  (${got.secs.toFixed(1)} s)${want && got.hash !== want ? `\n      native: ${want}` : ''}`);

    // 1. without importing: the installed folder via the File/Blob provider (the path that needs no OPFS)
    await page.open(url);
    await page.setFiles('#folder-input', [GAME]);
    const checked = await page.until('globalThis.__opengtaChecked', 30000);
    report(checked.problems.length === 0 && checked.layout === 'installed', `${c.name}: game folder check`,
      `${checked.layout}, ${checked.files} files, ${(checked.bytes / 1048576).toFixed(0)} MB ${checked.problems.join('; ')}`);
    await page.evaluate('document.getElementById("play-direct").click()');
    check('game folder without importing (File/Blob worker)', await result(page));
    await saveFrame(page, join(OUT, `${c.name}-direct.png`));

    // 2. the unzipped installer without importing: the cabinets read in the worker
    if (hasInstaller) {
      await page.open(url);
      await page.setFiles('#folder-input', [INSTALLER]);
      const ci = await page.until('globalThis.__opengtaChecked', 30000);
      report(ci.problems.length === 0 && ci.layout === 'installer', `${c.name}: installer folder check`,
        `${ci.layout}, ${ci.files} files, ${(ci.bytes / 1048576).toFixed(0)} MB ${ci.problems.join('; ')}`);
      await page.evaluate('document.getElementById("play-direct").click()');
      check('installer without importing (cabinets, File/Blob worker)', await result(page));
    }

    // 3. first visit: import the folder into OPFS (once), play from OPFS
    if (!imported) {
      await page.open(url);
      if (await page.evaluate('!document.getElementById("imported").hidden')) await page.evaluate('document.getElementById("remove").click()');
      await page.until('document.getElementById("imported").hidden', 10000);
      await page.setFiles('#folder-input', [GAME]);
      await page.until('globalThis.__opengtaChecked', 30000);
      const t0 = Date.now();
      await page.evaluate('document.getElementById("import").click()');
      const info = await page.until('globalThis.__opengtaImported', 600000);
      console.log(`      imported ${info.files} files, ${(info.bytes / 1048576).toFixed(0)} MB into OPFS in ${((Date.now() - t0) / 1000).toFixed(1)} s`);
      check('import + OPFS worker', await result(page));
      await saveFrame(page, join(OUT, `${c.name}-opfs.png`));
      imported = true;
    }

    // 4. a later visit: the import is detected and played
    await page.open(url);
    report(await page.evaluate('!document.getElementById("imported").hidden'), `${c.name}: existing import detected`);
    await page.evaluate('document.getElementById("play-opfs").click()');
    check('OPFS worker (later visit)', await result(page));
  }

  // 5. skins and hires: the sample skin through the skin picker, 2x through the selector, the imported data
  if (existsSync(join(SKINS, 'sample/skin.ini'))) {
    const c = { name: 'mission1 skin=sample hires=2', frames: 400, input: CASES[1].input,
      assets: { skins: SKINS }, params: { hires: '2', skin: 'sample' } };
    const nat = native(c, GAME);
    const ref = await nodeRef(c, GAME);
    if (nat) {
      console.log(`      gasm-run ${c.name}: ${nat}`);
      report(ref.after === nat, `${c.name}: gasm's Node host + gasm_exit = gasm-run`, ref.after === nat ? '' : ref.after);
    }
    await page.open(`${base}?intro=0&hashframes=${c.frames}&input=${encodeURIComponent(c.input)}`);
    if (await page.evaluate('(globalThis.__opengtaSkins ?? []).some((k) => k.name === "sample")')) {
      await page.evaluate('globalThis.opengtaPlay.data.removeSkin("sample").then(() => globalThis.opengtaPlay.refreshSkins()).then(() => 0)');
    }
    await page.setFiles('#skin-input', [join(SKINS, 'sample')]);
    await page.until('globalThis.__opengtaSkinAdded === "sample"', 30000);
    const listed = await page.evaluate('JSON.stringify(globalThis.__opengtaSkins)');
    report(/"name":"sample","title":"OpenGTA sample skin","on":true/.test(listed), `${c.name}: skin picker lists the sample, on`, listed);
    await page.evaluate('(() => { const h = document.getElementById("hires"); h.value = "2"; h.dispatchEvent(new Event("change")); return 0; })()');
    await page.evaluate('document.getElementById("play-opfs").click()');
    const got = await result(page);
    const want = ref.before;
    report(got.hash === want, `${c.name}: OPFS game data + OPFS skin, page selector`,
      `${got.hash}  (${got.secs.toFixed(1)} s)${got.hash !== want ? `\n      native: ${want}` : ''}`);
    report(/size=1280x960/.test(got.hash), `${c.name}: presents 1280 x 960`);
    await saveFrame(page, join(OUT, 'mission1-skin-hires2.png'));
    // a later visit keeps the choice: the selector and the skin list come back from the browser's storage
    await page.open(base);
    report(await page.evaluate('document.getElementById("hires").value === "2" && globalThis.__opengtaSkins.some((k) => k.name === "sample" && k.on)'),
      `${c.name}: choice kept for the next visit`);
    await page.evaluate('(() => { const h = document.getElementById("hires"); h.value = "1"; h.dispatchEvent(new Event("change")); return 0; })()');
    await page.evaluate('globalThis.opengtaPlay.data.removeSkin("sample").then(() => globalThis.opengtaPlay.refreshSkins()).then(() => 0)');
    report(await page.evaluate('!globalThis.__opengtaSkins.length'), `${c.name}: skin removed`);
  } else console.log(`      no ${SKINS}/sample: the skin run is skipped (python3 tools/make-sample-skin.py)`);

  // Real time with the keyboard: the start menu, then Enter goes to the player select.
  await page.open(base);
  await page.evaluate('document.getElementById("play-opfs").click()');
  await page.until('/frames\\/s/.test(document.getElementById("status").textContent)', 20000);
  await sleep(2500);
  await saveFrame(page, join(OUT, 'live-menu.png'));
  const before = await page.evaluate('globalThis.opengtaPlay.framePng()');
  const key = (type) => page.send('Input.dispatchKeyEvent', { type, code: 'Enter', key: 'Enter', windowsVirtualKeyCode: 13 });
  await key('keyDown'); await sleep(200); await key('keyUp');
  await sleep(2500);
  await saveFrame(page, join(OUT, 'live-after-enter.png'));
  const moved = (await page.evaluate('globalThis.opengtaPlay.framePng()')) !== before;
  const fps = await page.evaluate('document.getElementById("status").textContent');
  report(/^\d+ frames\/s$/.test(fps) && moved, 'real time, Enter leaves the start menu', `${fps}; live-menu.png, live-after-enter.png`);
  const shot2 = await page.send('Page.captureScreenshot', { format: 'png' });
  writeFileSync(join(OUT, 'page-playing.png'), Buffer.from(shot2.result.data, 'base64'));
  await page.evaluate('document.getElementById("stop").click()');
  await sleep(500);
  const bad = page.logs.filter((l) => l.startsWith('EXCEPTION'));
  report(!bad.length, 'no uncaught exceptions', bad.slice(0, 3).join('\n      '));
  console.log(`screenshots in ${OUT}`);
} catch (e) {
  failed = true;
  console.error(`FAIL  ${e.message}`);
} finally {
  const exited = new Promise((r) => chrome.once('exit', r));
  chrome.kill(); server.kill();
  await Promise.race([exited, sleep(5000)]);
  try { rmSync(PROFILE, { recursive: true, force: true, maxRetries: 5, retryDelay: 200 }); } catch {}
}
process.exit(failed ? 1 : 0);
