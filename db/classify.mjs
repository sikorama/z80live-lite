// Rejoue l'assemblage (rasm+sjasmplus WASM) sur les sources SNA et remplit
// build_status / compilable en base. Identifie les sources non compilables côté client.
// Usage: node --experimental-sqlite db/classify.mjs
import { DatabaseSync } from 'node:sqlite';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import createRasm from '../wasm/rasm.mjs';
import createSjasm from '../wasm/sjasmplus.mjs';
import { assemble } from '../wasm/assemble.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH = resolve(process.env.DB || join(__dirname, 'z80live.sqlite'));
const factories = { createRasm, createSjasm };
const SNA = ['sna', 'sna_cpc6128', 'sna_cpc464'];

const db = new DatabaseSync(DB_PATH);
const rows = db.prepare(
  `SELECT id, name, code, assembler, buildmode, entry_point, start_point, end_point
   FROM sources WHERE buildmode IN (${SNA.map(() => '?').join(',')})`
).all(...SNA);
console.log(`Sources SNA à classifier: ${rows.length}`);

const EXTDEP = /(cannot open|can't open|not found|no such file|include|incbin|read error)/i;
const upd = db.prepare('UPDATE sources SET build_status = $st, compilable = $c WHERE id = $id');

const stats = { ok: 0, ok_fallback: 0, fail: 0, extdep: 0 };
let i = 0;
for (const r of rows) {
  const opts = {
    code: r.code, assembler: r.assembler, buildmode: r.buildmode,
    entryPoint: r.entry_point, startPoint: r.start_point, endPoint: r.end_point,
  };
  let res = await assemble(opts, factories);
  let status;
  if (res.ok) { status = 'ok'; stats.ok++; }
  else {
    // fallback : si pas d'assembleur explicite, tenter l'autre
    if (!r.assembler) {
      const other = { ...opts, assembler: 'sjasmplus' };
      const res2 = await assemble(other, factories);
      if (res2.ok) { status = 'ok'; stats.ok++; stats.ok_fallback++; res = res2; }
    }
    if (!status) {
      const logtxt = res.log.join('\n');
      if (EXTDEP.test(logtxt)) { status = 'external-dep'; stats.extdep++; }
      else { status = 'fail'; stats.fail++; }
    }
  }
  upd.run({ st: status, c: status === 'ok' ? 1 : 0, id: r.id });
  if (++i % 50 === 0) process.stdout.write(`  ${i}/${rows.length}\r`);
}

console.log(`\n== Résultat ==`);
console.log(`  OK          : ${stats.ok}  (dont ${stats.ok_fallback} via fallback sjasmplus)`);
console.log(`  external-dep: ${stats.extdep}  (incbin/include manquant — non compilable côté client)`);
console.log(`  fail        : ${stats.fail}`);
console.log(`  Taux de succès: ${(100 * stats.ok / rows.length).toFixed(1)}%`);
db.close();
