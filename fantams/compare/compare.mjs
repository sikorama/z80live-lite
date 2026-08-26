// compare.mjs - Compare la sortie de fantams (natif) à la référence rasm (wasm) sur le corpus
// de sources réelles de la base (celles qui compilent actuellement avec rasm).
//
// Usage: node compare/compare.mjs [--limit N] [--id <id>] [--verbose]
//
// Pour chaque source :
//   1. assemble avec rasm (wasm, via wasm/assemble.mjs) -> SNA de référence
//   2. assemble avec fantams (binaire natif ./fantams)   -> SNA candidat
//   3. compare le dump RAM (banques 0..7, 128K) octet à octet
//
// Rapport : sources OK / sources où fantams échoue à l'assemblage (hors périmètre,
// probablement directive non supportée) / sources où ça diverge (bug à corriger).

import { DatabaseSync } from 'node:sqlite';
import { createHash } from 'node:crypto';
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

// --- Textes distincts ---------------------------------------------------
// La base contient des copies : 13 groupes de textes strictement identiques
// couvrant 33 sources, dont un programme en 7 exemplaires. Le cardinal brut
// n'est donc PAS une population valide pour décider quoi implémenter.
//
// La clé est le CONTENU, pas la filiation : seules 3 sources sur 425 ont un
// fork_parent renseigné — l'import depuis l'ancien système ne l'a pas conservé,
// donc dédupliquer par lignée reviendrait à ne rien dédupliquer.
//
// Limite assumée : seules les copies EXACTES (aux espaces près) sont
// regroupées. Deux variantes d'un même programme séparées par une ligne restent
// comptées deux fois, donc « distincts » est une borne HAUTE de la diversité.
//
// Les deux chiffres répondent à deux questions différentes — le brut dit combien
// d'utilisateurs sont débloqués, le distinct combien de constructions
// syntaxiques restent à écrire — et ils ne classent pas les causes pareil.
const contentKey = new Map();
for (const r of rows)
  contentKey.set(r.id, createHash('sha1')
    .update(String(r.code || '').replace(/\r/g, '').replace(/[ \t]+/g, ' ').trim())
    .digest('hex'));
const distinct = (list) => new Set(list.map((x) => contentKey.get(x.id))).size;

// --- Classement des échecs par cause --------------------------------------
// Provisoire : normalise le TEXTE de l'erreur. À remplacer par le code stable
// que le cœur émettra (E_UNKNOWN_SYMBOL, ...) — reparser une chaîne destinée à
// un humain est précisément ce que l'API structurée doit supprimer.
function classify(err) {
  return String(err || '(vide)')
    .replace(/^[^:]*:\d+:\s*/, '')
    .replace(/'[^']*'/g, "'X'")
    .replace(/\b\d+\b/g, 'N')
    .trim() || '(vide)';
}
if (opt.id) rows = rows.filter((r) => r.id === opt.id);
if (rows.length > opt.limit) rows = rows.slice(0, opt.limit);

const factories = { createRasm };
const tmpDir = mkdtempSync(join(tmpdir(), 'fantams-cmp-'));

function runFantamsNative(wrapped) {
  const asmPath = join(tmpDir, 'in.asm');
  const snaPath = join(tmpDir, 'out.sna');
  writeFileSync(asmPath, wrapped);
  try {
    execFileSync(FANTAMS_BIN, [asmPath, '-o', snaPath],
      // maxBuffer relevé : un débordement du tampon par défaut (1 Mo) faisait
      // remonter une erreur VIDE, indiscernable d'un crash. Un assembleur
      // bavard ne doit pas se traduire par un diagnostic muet.
      { stdio: ['ignore', 'pipe', 'pipe'], maxBuffer: 64 * 1024 * 1024 });
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

// Rend TOUJOURS 128K (banques 0..7), quelle que soit la forme du fichier : c'est
// ce qui rend deux snapshots d'étendues différentes comparables sans traitement
// particulier. Un source qui n'écrit que dans les 64K de base laisse la moitié
// haute à zéro des deux côtés, et la comparaison est inchangée.
//
// Trois formes possibles : dump plat de 64K, dump plat de 128K (ce que fantams
// produit dès qu'une banque 4..7 est écrite), ou chunks "MEMx" compressés RLE
// (ce que rasm produit). Ne PAS reconnaître l'une d'elles rendrait un tampon de
// zéros, et la comparaison déclarerait conformes des octets jamais lus.
const RAM_SIZE = 131072;

function ramDump(sna) {
  const out = Buffer.alloc(RAM_SIZE);
  const flat = sna.length - 256;
  if (flat === 65536 || flat === RAM_SIZE) {
    sna.copy(out, 0, 256, 256 + flat);
    return out;
  }
  let off = 256, found = false;
  while (off + 8 <= sna.length) {
    const id = sna.toString('ascii', off, off + 4);
    const size = sna.readUInt32LE(off + 4);
    const data = sna.subarray(off + 8, off + 8 + size);
    const m = /^MEM(\d)$/.exec(id);
    if (m) {
      const base = Number(m[1]) * 65536;
      if (base < RAM_SIZE) { decodeRLE(data).copy(out, base); found = true; }
    }
    off += 8 + size;
  }
  if (!found) throw new Error(`snapshot illisible : ni dump plat 64K/128K, ni chunk MEMx (${sna.length} octets)`);
  return out;
}

function firstDiff(a, b) {
  const n = Math.min(a.length, b.length);
  for (let i = 0; i < n; i++) if (a[i] !== b[i]) return i;
  if (a.length !== b.length) return n;
  return -1;
}

const results = { match: [], fantamsFail: [], mismatch: [], skipped: [] };

for (const r of rows) {
  const opts = {
    code: r.code, assembler: 'rasm', buildmode: r.buildmode,
    entryPoint: r.entry_point, startPoint: r.start_point, endPoint: r.end_point,
  };
  const ref = await assemble(opts, factories);
  // Les sources non comparables sont COMPTABILISÉES, pas ignorées en silence :
  // sans ça le dénominateur est inconnu et le taux de compatibilité ne veut
  // rien dire (constaté : 215 lignes lues pour 207 sources réparties).
  if (!ref.ok) { results.skipped.push({ id: r.id, name: r.name, reason: 'rasm-failed' }); continue; }
  if (ref.ext !== 'sna') { results.skipped.push({ id: r.id, name: r.name, reason: 'rasm-not-sna' }); continue; }

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

const comparable = results.match.length + results.mismatch.length + results.fantamsFail.length;
const pct = (n, d) => (d ? ((100 * n) / d).toFixed(1) : '0.0');
console.log(`\n== Résultat ==`);
console.log(`  lues        : ${rows.length}`);
console.log(`  écartées    : ${results.skipped.length}  (rasm n'a pas produit de .sna comparable)`);
console.log(`  comparables : ${comparable}   <- dénominateur`);
console.log(`\n                    brut          textes distincts`);
const line = (label, list) =>
  console.log(`  ${label.padEnd(11)} ${String(list.length).padStart(4)} (${pct(list.length, comparable).padStart(5)}%)` +
              `   ${String(distinct(list)).padStart(4)} (${pct(distinct(list), distinct([...results.match, ...results.mismatch, ...results.fantamsFail])).padStart(5)}%)`);
line('match', results.match);
line('mismatch', results.mismatch);
line('échecs', results.fantamsFail);

if (results.fantamsFail.length) {
  // Classement par cause : c'est ce chiffre qui dit quoi implémenter ensuite.
  // La colonne « distincts » est celle qui compte — le brut sur-pondère les
  // causes concentrées sur une source très forkée.
  const byCause = new Map();
  for (const f of results.fantamsFail) {
    const k = classify(f.firstError);
    if (!byCause.has(k)) byCause.set(k, []);
    byCause.get(k).push(f);
  }
  const causes = [...byCause.entries()].sort((a, b) => distinct(b[1]) - distinct(a[1]) || b[1].length - a[1].length);
  console.log('\n-- Causes d\'échec (brut / distincts) --');
  for (const [cause, list] of causes)
    console.log(`  ${String(list.length).padStart(4)} /${String(distinct(list)).padStart(4)}   ${cause}`);
}

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
