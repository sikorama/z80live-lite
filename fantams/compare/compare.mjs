// compare.mjs - Compare la sortie de fantams (natif) à la référence rasm (wasm) sur le corpus
// de sources réelles de la base (celles qui compilent actuellement avec rasm).
//
// Usage: node compare/compare.mjs [--limit N] [--id <id>] [--verbose]
//
// Pour chaque source :
//   1. assemble avec rasm (wasm, via wasm/assemble.mjs) -> SNA de référence
//   2. assemble avec fantams (binaire natif ./fantams)   -> SNA candidat
//   3. compare le dump RAM (offset 0x100..0x100+65536) octet à octet
//
// Rapport : sources OK / sources où fantams échoue à l'assemblage (hors périmètre,
// probablement directive non supportée) / sources où ça diverge (bug à corriger).

import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { execFileSync } from 'node:child_process';
import { writeFileSync, readFileSync, mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';

import createRasm from '../../wasm/rasm.mjs';
import { assemble, wrapFantams, parseDirectives, resolveAssembler } from '../../wasm/assemble.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..', '..');
const DB_PATH = process.env.DB || join(ROOT, 'db', 'z80live.sqlite');
const FANTAMS_BIN = join(ROOT, 'fantams', 'fantams');

const argv = process.argv.slice(2);
const opt = { limit: Infinity, id: null, verbose: argv.includes('--verbose') };
for (let i = 0; i < argv.length; i++) {
  if (argv[i] === '--limit') opt.limit = parseInt(argv[++i], 10);
  if (argv[i] === '--id') opt.id = argv[++i];
}

const db = new DatabaseSync(DB_PATH);
let rows = db.prepare(
  `SELECT id, name, code, assembler, buildmode, entry_point, start_point, end_point
   FROM sources
   WHERE (assembler IS NULL OR assembler = 'rasm')
     AND build_status = 'ok'
     AND buildmode LIKE 'sna%'
   ORDER BY id`
).all();
if (opt.id) rows = rows.filter((r) => r.id === opt.id);
if (rows.length > opt.limit) rows = rows.slice(0, opt.limit);

const factories = { createRasm };
const tmpDir = mkdtempSync(join(tmpdir(), 'fantams-cmp-'));

function runFantamsNative(wrapped) {
  const asmPath = join(tmpDir, 'in.asm');
  const snaPath = join(tmpDir, 'out.sna');
  writeFileSync(asmPath, wrapped);
  try {
    execFileSync(FANTAMS_BIN, [asmPath, '-o', snaPath], { stdio: ['ignore', 'pipe', 'pipe'] });
  } catch (e) {
    return { ok: false, error: (e.stderr || e.message || '').toString() };
  }
  try {
    return { ok: true, data: readFileSync(snaPath) };
  } catch (e) {
    return { ok: false, error: 'pas de sortie: ' + e.message };
  }
}

// RLE (rasm/CPCEMU) : 0xE5 <count> <value> -> `count` répétitions de `value` ;
// 0xE5 0x00 (count=0) est un cas spécial : un seul octet 0xE5 littéral (2 octets,
// pas de 3e octet de valeur — vérifié empiriquement contre la sortie rasm réelle).
function decodeRLE(buf) {
  const out = Buffer.alloc(65536);
  let o = 0;
  for (let i = 0; i < buf.length && o < out.length; ) {
    const b = buf[i];
    if (b === 0xE5 && i + 1 < buf.length) {
      const count = buf[i + 1];
      if (count === 0) { out[o++] = 0xE5; i += 2; continue; }
      const value = buf[i + 2];
      out.fill(value, o, Math.min(o + count, out.length));
      o += count; i += 3;
    } else { out[o++] = b; i += 1; }
  }
  return out;
}

function ramDump(sna) {
  // en-tête SNA = 256 octets, puis soit un dump RAM 64K brut (V2/V3 non compressé),
  // soit des chunks "MEMx" (V3 compressé RLE, ex: peu de mémoire utilisée). On ne
  // reconstruit que MEM0 (les 64K de base) : fantams ne gère pas le multi-bank.
  if (sna.length === 256 + 65536) return sna.subarray(256, 256 + 65536);
  let off = 256;
  while (off + 8 <= sna.length) {
    const id = sna.toString('ascii', off, off + 4);
    const size = sna.readUInt32LE(off + 4);
    const data = sna.subarray(off + 8, off + 8 + size);
    if (id === 'MEM0') return decodeRLE(data);
    off += 8 + size;
  }
  return Buffer.alloc(65536); // pas de MEM0 trouvé : ne devrait pas arriver
}

function firstDiff(a, b) {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  if (a.length !== b.length) return n;
  return -1;
}

const results = { match: [], fantamsFail: [], mismatch: [] };

for (const r of rows) {
  const opts = {
    code: r.code, assembler: 'rasm', buildmode: r.buildmode,
    entryPoint: r.entry_point, startPoint: r.start_point, endPoint: r.end_point,
  };
  const ref = await assemble(opts, factories);
  if (!ref.ok) { continue; } // ne devrait pas arriver (build_status=ok) mais sécurité
  if (ref.ext !== 'sna') { continue; } // rasm n'a pas produit un .sna (ex: BUILDSNA en commentaire mal détecté) -> non comparable

  const fOpts = { ...opts, ...parseDirectives(r.code) };
  const wrapped = wrapFantams(r.code, fOpts);
  const cand = runFantamsNative(wrapped);

  if (!cand.ok) {
    // ne garder que les lignes d'erreur réelles (les avertissements précèdent en stderr) ;
    // la 1re est la cause racine, les suivantes sont souvent des erreurs en cascade.
    const errLines = cand.error.trim().split('\n').filter((l) => !/warning/.test(l));
    results.fantamsFail.push({ id: r.id, name: r.name, firstError: errLines[0] || '', error: errLines.join('\n') });
    continue;
  }

  const refRam = ramDump(Buffer.from(ref.output));
  const candRam = ramDump(cand.data);
  const diffIdx = firstDiff(refRam, candRam);
  if (diffIdx === -1) {
    results.match.push({ id: r.id, name: r.name });
  } else {
    results.mismatch.push({
      id: r.id, name: r.name, diffAt: diffIdx, addr: '0x' + diffIdx.toString(16),
      refByte: refRam[diffIdx], candByte: candRam[diffIdx],
      refLen: ref.output.length, candLen: cand.data.length,
    });
  }
}

rmSync(tmpDir, { recursive: true, force: true });

console.log(`\n== Résultat (${rows.length} sources testées) ==`);
console.log(`  match     : ${results.match.length}`);
console.log(`  mismatch  : ${results.mismatch.length}`);
console.log(`  fantamsFail: ${results.fantamsFail.length}`);

if (results.mismatch.length) {
  console.log('\n-- Divergences (RAM dump) --');
  for (const m of results.mismatch) {
    console.log(`  [${m.id}] ${m.name}  @${m.addr}  ref=0x${m.refByte.toString(16).padStart(2,'0')} cand=0x${m.candByte.toString(16).padStart(2,'0')}  (refLen=${m.refLen} candLen=${m.candLen})`);
  }
}

if (opt.verbose && results.fantamsFail.length) {
  console.log('\n-- Échecs fantams (hors périmètre probable) --');
  for (const f of results.fantamsFail) console.log(`  [${f.id}] ${f.name}: ${f.error.split('\n')[0]}`);
}

writeFileSync(join(__dirname, 'last-report.json'), JSON.stringify(results, null, 2));
console.log(`\nRapport complet: ${join(__dirname, 'last-report.json')}`);
