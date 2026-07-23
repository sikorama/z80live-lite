// Génère les .sna de toutes les sources assembleur (SNA) qui compilent, soit dans un
// dossier (un fichier .sna par source), soit dans une archive .zip unique.
// Rejoue l'assemblage (rasm+sjasmplus WASM), comme db/classify.mjs, mais conserve le
// binaire produit au lieu de se contenter du statut ok/fail.
//
// Usage:
//   node --experimental-sqlite scripts/export-sna.mjs [outDir]          # dossier de .sna
//   node --experimental-sqlite scripts/export-sna.mjs --zip [outFile]   # archive zip unique
import { DatabaseSync } from 'node:sqlite';
import { deflateRawSync, crc32 } from 'node:zlib';
import { writeFileSync, mkdirSync, rmSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import createRasm from '../wasm/rasm.mjs';
import createSjasm from '../wasm/sjasmplus.mjs';
import { assemble } from '../wasm/assemble.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..');
const DB_PATH = resolve(process.env.DB || join(ROOT, 'db', 'z80live.sqlite'));
const factories = { createRasm, createSjasm };
const SNA = ['sna', 'sna_cpc6128', 'sna_cpc464'];

const args = process.argv.slice(2);
const zipIdx = args.indexOf('--zip');
const asZip = zipIdx !== -1;
if (asZip) args.splice(zipIdx, 1);
const target = resolve(args[0] || join(ROOT, '..', asZip ? 'sna-export.zip' : 'sna-export'));

// Nom de fichier sûr : accents retirés, tout caractère non alphanumérique -> '-'.
function slugify(s) {
  return (s || 'source').normalize('NFD').replace(/[̀-ͯ]/g, '')
    .replace(/[^a-zA-Z0-9._-]+/g, '-').replace(/^-+|-+$/g, '').toLowerCase() || 'source';
}

// Archive ZIP minimale (stockage + compression deflate), sans dépendance externe :
// en-tête local + données par entrée, puis répertoire central, puis EOCD.
function buildZip(entries) {
  const local = [];
  const central = [];
  let offset = 0;
  for (const { name, data } of entries) {
    const nameBuf = Buffer.from(name, 'utf8');
    const compressed = deflateRawSync(data);
    const crc = crc32(data);
    const dosTime = 0, dosDate = 0x21; // date arbitraire fixe (pas de Date.now() dispo ici)

    const localHeader = Buffer.alloc(30);
    localHeader.writeUInt32LE(0x04034b50, 0);
    localHeader.writeUInt16LE(20, 4);           // version needed
    localHeader.writeUInt16LE(0, 6);            // flags
    localHeader.writeUInt16LE(8, 8);            // méthode = deflate
    localHeader.writeUInt16LE(dosTime, 10);
    localHeader.writeUInt16LE(dosDate, 12);
    localHeader.writeUInt32LE(crc >>> 0, 14);
    localHeader.writeUInt32LE(compressed.length, 18);
    localHeader.writeUInt32LE(data.length, 22);
    localHeader.writeUInt16LE(nameBuf.length, 26);
    localHeader.writeUInt16LE(0, 28);
    local.push(localHeader, nameBuf, compressed);

    const centralHeader = Buffer.alloc(46);
    centralHeader.writeUInt32LE(0x02014b50, 0);
    centralHeader.writeUInt16LE(20, 4);         // version made by
    centralHeader.writeUInt16LE(20, 6);         // version needed
    centralHeader.writeUInt16LE(0, 8);          // flags
    centralHeader.writeUInt16LE(8, 10);         // méthode = deflate
    centralHeader.writeUInt16LE(dosTime, 12);
    centralHeader.writeUInt16LE(dosDate, 14);
    centralHeader.writeUInt32LE(crc >>> 0, 16);
    centralHeader.writeUInt32LE(compressed.length, 20);
    centralHeader.writeUInt32LE(data.length, 24);
    centralHeader.writeUInt16LE(nameBuf.length, 28);
    centralHeader.writeUInt16LE(0, 30);         // extra length
    centralHeader.writeUInt16LE(0, 32);         // comment length
    centralHeader.writeUInt16LE(0, 34);         // disk number
    centralHeader.writeUInt16LE(0, 36);         // internal attrs
    centralHeader.writeUInt32LE(0, 38);         // external attrs
    centralHeader.writeUInt32LE(offset, 42);
    central.push(centralHeader, nameBuf);

    offset += localHeader.length + nameBuf.length + compressed.length;
  }
  const centralBuf = Buffer.concat(central);
  const eocd = Buffer.alloc(22);
  eocd.writeUInt32LE(0x06054b50, 0);
  eocd.writeUInt16LE(0, 4);
  eocd.writeUInt16LE(0, 6);
  eocd.writeUInt16LE(entries.length, 8);
  eocd.writeUInt16LE(entries.length, 10);
  eocd.writeUInt32LE(centralBuf.length, 12);
  eocd.writeUInt32LE(offset, 16);
  eocd.writeUInt16LE(0, 20);
  return Buffer.concat([...local, centralBuf, eocd]);
}

const db = new DatabaseSync(DB_PATH);
const rows = db.prepare(
  `SELECT id, name, slugname, code, assembler, buildmode, entry_point, start_point, end_point
   FROM sources WHERE buildmode IN (${SNA.map(() => '?').join(',')})`
).all(...SNA);
db.close();
console.log(`Sources SNA candidates : ${rows.length}`);

const stats = { ok: 0, ok_fallback: 0, fail: 0 };
const outputs = [];
const seen = new Map(); // dédoublonnage des noms de fichiers
let i = 0;
for (const r of rows) {
  const opts = {
    code: r.code, assembler: r.assembler, buildmode: r.buildmode,
    entryPoint: r.entry_point, startPoint: r.start_point, endPoint: r.end_point,
  };
  let res = await assemble(opts, factories);
  if (!res.ok && !r.assembler) {
    // même repli que classify.mjs : pas d'assembleur explicite -> tenter l'autre
    const res2 = await assemble({ ...opts, assembler: 'sjasmplus' }, factories);
    if (res2.ok) { res = res2; stats.ok_fallback++; }
  }
  if (res.ok && res.output) {
    stats.ok++;
    const base = slugify(r.slugname || r.name);
    const n = (seen.get(base) || 0) + 1;
    seen.set(base, n);
    const name = n === 1 ? `${base}.sna` : `${base}-${n}.sna`;
    outputs.push({ name, data: Buffer.from(res.output) });
  } else {
    stats.fail++;
  }
  if (++i % 25 === 0 || i === rows.length) process.stdout.write(`  ${i}/${rows.length}\r`);
}
console.log(`\nOK: ${stats.ok} (dont ${stats.ok_fallback} via fallback sjasmplus) — échecs: ${stats.fail}`);

if (asZip) {
  writeFileSync(target, buildZip(outputs));
  console.log(`Archive zip -> ${target}  (${outputs.length} .sna)`);
} else {
  rmSync(target, { recursive: true, force: true });
  mkdirSync(target, { recursive: true });
  for (const o of outputs) writeFileSync(join(target, o.name), o.data);
  console.log(`Dossier -> ${target}  (${outputs.length} .sna)`);
}
