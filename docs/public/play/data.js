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
