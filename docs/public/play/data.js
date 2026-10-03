// The player's GTA data: picking it, checking it, and importing it into the site's origin private file
// system (OPFS). Nothing here is game data; everything comes from the user's own copy of the game and
// stays on their device.
//
// A source is { kind: 'folder', name, layout: 'installed' | 'installer', entries: [[path, File], ...] }:
//   installed   the folder the game was installed to: GTADATA/ (MISSION.INI, maps, styles, AUDIO/ ...),
//               WINO/Grand Theft Auto.exe (the original program: OpenGTA reads its tables) and Music/
//   installer   the unzipped GTAINSTALLER.zip: data1.cab + data2.cab, which the game reads itself
//               (src/iscab.c: the InstallShield 5 cabinets as the game folder)
// The game (opengta.wasm) reads them as assets named by their paths (src/platform_gasm.c: mount_game).
import { directoryHandleEntries, fileListEntries } from './vendor/gasm/gasm-host.js';
import { clearNamespace, opfsFileSystem, persist } from '@emdzej/csfs-opfs';

/** OPFS directory the data is imported into. */
export const NAMESPACE = 'opengta-data';
/** Written last: an import without it is incomplete. Hidden, so the game never sees it. */
const MARKER = '.opengta-import.json';
/** The original program of the 2002 release (src/exe.c checks the size and CRC-32 a5ca070e). */
export const EXE = 'WINO/Grand Theft Auto.exe';
export const EXE_SIZE = 774144;
/** vfs_find_game (src/iscab.c) looks for this in the installed folder or inside the cabinets. */
const MARKER_FILE = 'GTADATA/MISSION.INI';
/** The installer's data cabinets (_sys1.cab and _user1.cab hold the setup program: not needed). */
const CABINET = /^data\d+\.(cab|hdr)$/i;

const upper = (s) => s.replace(/[a-z]/g, (c) => c.toUpperCase());
export const mb = (n) => (n >= 1 << 30 ? `${(n / 2 ** 30).toFixed(1)} GB` : `${(n / 1048576).toFixed(n < 10485760 ? 1 : 0)} MB`);

// ---- picking --------------------------------------------------------------------------

/** Folder from showDirectoryPicker() (Chromium). */
export async function folderFromHandle(handle) {
  return checkFolder(handle.name, await directoryHandleEntries(handle));
}

/** Folder from <input type="file" webkitdirectory> (all browsers). */
export function folderFromFileList(files) {
  const name = files[0]?.webkitRelativePath.split('/')[0] || 'folder';
  return checkFolder(name, fileListEntries(files));
}

/** The shortest path whose last `tail.length` components are `tail` (case-insensitive), at most `depth` folders down. */
function locate(entries, tail, depth) {
  const t = upper(tail).split('/');
  return entries
    .map(([p]) => p)
    .filter((p) => {
      const segs = upper(p).split('/');
      return segs.length <= depth + 1 && segs.length >= t.length && t.every((s, i) => segs[segs.length - t.length + i] === s);
    })
    .sort((a, b) => a.length - b.length)[0];
}

/**
 * Keep what the game reads and check it. Accepts the installed game folder (GTADATA/MISSION.INI) or the
 * unzipped installer (data1.cab), the folder itself or the one above it. Returns { source, problems, files, bytes }.
 */
export function checkFolder(name, entries) {
  const problems = [];
  let kept = [], root = '', layout = null;
  const ini = locate(entries, MARKER_FILE, 2);
  const cab = ini ? undefined : locate(entries, 'data1.cab', 1);
  if (ini) {
    layout = 'installed';
    root = ini.slice(0, ini.length - MARKER_FILE.length);
    for (const [path, file] of entries) {
      if (!path.startsWith(root)) continue;
      const rel = path.slice(root.length), top = upper(rel.split('/')[0]);
      if ((top === 'GTADATA' || top === 'MUSIC') && rel.includes('/')) kept.push([rel, file]);
      else if (upper(rel) === upper(EXE)) kept.push([rel, file]);
    }
    const exe = kept.find(([p]) => upper(p) === upper(EXE));
    if (!exe) problems.push(`${EXE} is missing (OpenGTA reads the original program's tables)`);
    else if (exe[1].size !== EXE_SIZE) {
      problems.push(`${EXE} is ${exe[1].size.toLocaleString('en')} bytes, not ${EXE_SIZE.toLocaleString('en')}: not the 2002 release's Windows build`);
    }
  } else if (cab) {
    layout = 'installer';
    root = cab.includes('/') ? cab.slice(0, cab.lastIndexOf('/') + 1) : '';
    for (const [path, file] of entries) {
      if (!path.startsWith(root)) continue;
      const rel = path.slice(root.length);
      if (!rel.includes('/') && CABINET.test(rel)) kept.push([rel, file]);
    }
    const names = new Set(kept.map(([p]) => upper(p)));
    for (const f of ['DATA1.CAB', 'DATA2.CAB']) if (!names.has(f)) problems.push(`${f.toLowerCase()} is missing`);
  } else {
    problems.push('neither GTADATA/MISSION.INI (the installed game) nor data1.cab (the unzipped installer) is in this folder');
  }
  const bytes = kept.reduce((n, [, f]) => n + f.size, 0);
  const shown = root ? `${name}/${root.slice(0, -1)}` : name;
  return { source: { kind: 'folder', layout, name: shown, entries: kept }, problems, files: kept.length, bytes };
}

// ---- OPFS import ----------------------------------------------------------------------

/** The completed import's description ({ kind, layout, name, files, bytes, date }), or null. */
export async function existingImport() {
  try {
    const root = await navigator.storage.getDirectory();
    const dir = await root.getDirectoryHandle(NAMESPACE);
    const f = await (await dir.getFileHandle(MARKER)).getFile();
    return JSON.parse(await f.text());
  } catch {
    return null;
  }
}

export async function removeImport() {
  await clearNamespace(NAMESPACE);
  try { await (await navigator.storage.getDirectory()).removeEntry(NAMESPACE, { recursive: true }); } catch {}
}

/** Ask the browser to keep the data under storage pressure. Firefox may never answer a prompt. */
export async function requestPersist() {
  return persist({ signal: AbortSignal.timeout(10000) }).catch(() => false);
}

export async function storageInfo() {
  const est = await navigator.storage?.estimate?.().catch(() => null);
  const persisted = await navigator.storage?.persisted?.().catch(() => false);
  return { usage: est?.usage, quota: est?.quota, persisted: !!persisted };
}

/**
 * Copy a checked source into OPFS (csfs writes each file through a stream, so a large file never sits
 * in memory). The marker goes last. onProgress({ bytes, total, file, files, done }).
 */
export async function importSource(source, onProgress = () => {}) {
  const items = source.entries;
  const total = items.reduce((n, [, f]) => n + f.size, 0);
  await persist({ signal: AbortSignal.timeout(3000) }).catch(() => false);
  await removeImport();
  const fs = await opfsFileSystem({ namespace: NAMESPACE });
  let bytes = 0, last = 0;
  for (let i = 0; i < items.length; i++) {
    const [path, file] = items[i];
    const counted = file.stream().pipeThrough(new TransformStream({
      transform(chunk, ctrl) {
        bytes += chunk.byteLength;
        const now = performance.now();
        if (now - last > 100) { last = now; onProgress({ bytes, total, file: path, files: items.length, done: i }); }
        ctrl.enqueue(chunk);
      },
    }));
    await fs.write(`/${path}`, counted);
    onProgress({ bytes, total, file: path, files: items.length, done: i + 1 });
  }
  const info = { kind: source.kind, layout: source.layout, name: source.name, files: items.length, bytes: total, date: new Date().toISOString() };
  await fs.write(`/${MARKER}`, new TextEncoder().encode(JSON.stringify(info)));
  return info;
}

// ---- running --------------------------------------------------------------------------

/** gasm Worker-mode asset specs for a source ('opfs' = the imported data). */
export function assetSpecs(source) {
  if (source === 'opfs') return [{ kind: 'opfs', dir: NAMESPACE }];
  return [{ kind: 'files', entries: source.entries }];
}

// ---- skins --------------------------------------------------------------------------------
// A skin (docs/skins.md) is a folder with skin.ini and style<NNN>/..., font/..., pictures/... PNGs: the
// player's own art, drawn by the hires renderer over the original. Skins are imported into OPFS under
// SKINS/<name>/ (one folder per skin) and reach the game as assets "skins/<name>/..." (gasm's asset
// prefix: the same names as gasm-run --asset-dir skins=<folder of skins>). The order and which are on
// are kept in localStorage. Without OPFS a picked skin lasts for the visit (read from the picked files).

/** OPFS directory of the imported skins. */
export const SKINS = 'opengta-skins';
const SKIN_STATE = 'opengta.skins';       // localStorage: [{ name, on }], first = drawn on top
const SKIN_FILE = /\.(png|ini)$/i;        // what the game reads from a skin (CHECKLIST.md, skin.json stay out)
const session = new Map();                // name -> { name, title, author, scale, entries } (no OPFS)

/** The skin name the game accepts (skin=a,b: no commas; a folder name under skins/). */
export const skinName = (s) => s.replace(/[,/\\]/g, '_').replace(/\.\.+/g, '_').trim().slice(0, 60) || 'skin';

function parseIni(text) {
  const info = { title: '', author: '', scale: 1 };
  for (const raw of text.split(/\r?\n/)) {
    const line = raw.trim();
    if (!line || /^[;#[]/.test(line) || !line.includes('=')) continue;
    const k = line.slice(0, line.indexOf('=')).trim(), v = line.slice(line.indexOf('=') + 1).trim();
    if (k === 'name') info.title = v;
    else if (k === 'author') info.author = v;
    else if (k === 'scale') info.scale = Number.parseInt(v, 10) || 1;
  }
  return info;
}

/**
 * A picked folder as a skin: skin.ini at its top (or one folder down). Returns { name, title, author,
 * scale, entries: [[rel, File], ...], files, bytes, problems }.
 */
export async function checkSkin(folderName, entries) {
  const ini = locate(entries, 'skin.ini', 1);
  const problems = [];
  if (!ini) return { name: skinName(folderName), entries: [], files: 0, bytes: 0, problems: ['no skin.ini in this folder (docs/skins.md)'] };
  const root = ini.slice(0, ini.length - 'skin.ini'.length);
  const kept = entries.filter(([p]) => p.startsWith(root) && SKIN_FILE.test(p)).map(([p, f]) => [p.slice(root.length), f]);
  const iniFile = kept.find(([p]) => p === 'skin.ini')?.[1];
  const info = parseIni(iniFile ? await iniFile.text() : '');
  const name = skinName(root ? root.slice(0, -1).split('/').pop() : folderName);
  const images = kept.filter(([p]) => /\.png$/i.test(p)).length;
  if (!images) problems.push('no PNG images in it');
  return { name, ...info, title: info.title || name, entries: kept, files: kept.length, bytes: kept.reduce((n, [, f]) => n + f.size, 0), problems };
}

export const skinFromHandle = async (handle) => checkSkin(handle.name, await directoryHandleEntries(handle));
export const skinFromFileList = (files) => checkSkin(files[0]?.webkitRelativePath.split('/')[0] || 'skin', fileListEntries(files));

function loadState() {
  try { return JSON.parse(localStorage.getItem(SKIN_STATE) ?? '[]').filter((s) => typeof s?.name === 'string'); } catch { return []; }
}
function saveState(list) { localStorage.setItem(SKIN_STATE, JSON.stringify(list.map(({ name, on }) => ({ name, on })))); }

/** The skins available (imported ones and this visit's), in the saved order: [{ name, title, author, scale, on, stored }]. */
export async function listSkins() {
  const found = new Map();
  try {
    const root = await (await navigator.storage.getDirectory()).getDirectoryHandle(SKINS);
    for await (const [name, h] of root.entries()) {
      if (h.kind !== 'directory') continue;
      let info = { title: name, author: '', scale: 1 };
      try { info = { ...info, ...parseIni(await (await (await h.getFileHandle('skin.ini')).getFile()).text()) }; } catch { continue; }
      found.set(name, { name, ...info, title: info.title || name, stored: 'opfs' });
    }
  } catch { /* no skins imported */ }
  for (const s of session.values()) found.set(s.name, { name: s.name, title: s.title, author: s.author, scale: s.scale, stored: 'session' });
  const state = loadState();
  const out = [];
  for (const st of state) if (found.has(st.name)) { out.push({ ...found.get(st.name), on: !!st.on }); found.delete(st.name); }
  for (const s of found.values()) out.push({ ...s, on: true });   // new ones: on, at the bottom
  saveState(out);
  return out;
}

/** Store the order / on state of the list (first = drawn on top). */
export function setSkinOrder(list) { saveState(list); }

/** Import a checked skin into OPFS (replacing one of the same name), or keep it for this visit without OPFS. */
export async function importSkin(skin, onProgress = () => {}) {
  const state = loadState().filter((s) => s.name !== skin.name);
  state.unshift({ name: skin.name, on: true });   // a new skin goes on top
  if (!navigator.storage?.getDirectory) {
    session.set(skin.name, skin);
    saveState(state);
    return;
  }
  await removeSkin(skin.name, false);
  const fs = await opfsFileSystem({ namespace: `${SKINS}/${skin.name}` });
  for (let i = 0; i < skin.entries.length; i++) {
    const [path, file] = skin.entries[i];
    await fs.write(`/${path}`, file.stream());
    onProgress({ done: i + 1, files: skin.entries.length, file: path });
  }
  saveState(state);
}

export async function removeSkin(name, forget = true) {
  session.delete(name);
  try {
    await clearNamespace(`${SKINS}/${name}`);
    await (await (await navigator.storage.getDirectory()).getDirectoryHandle(SKINS)).removeEntry(name, { recursive: true });
  } catch { /* not there */ }
  if (forget) saveState(loadState().filter((s) => s.name !== name));
}

/** The skin= parameter for the skins that are on (the game's order: later ones win, so the top one comes last). */
export function skinParam(list) { return list.filter((s) => s.on).map((s) => s.name).reverse().join(','); }

/** gasm asset specs for the named skins: assets "skins/<name>/...". */
export function skinAssetSpecs(names) {
  return names.map((name) => (session.has(name)
    ? { kind: 'files', entries: session.get(name).entries, prefix: `skins/${name}` }
    : { kind: 'opfs', dir: `${SKINS}/${name}`, prefix: `skins/${name}` }));
}
