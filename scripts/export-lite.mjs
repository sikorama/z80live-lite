// Construit la version LITE : un dossier statique auto-suffisant (dist-lite/),
// distribuable/hébergeable n'importe où, base SQLite embarquée en asset (lecture seule).
// Usage: node --experimental-sqlite scripts/export-lite.mjs [dist-lite]
import { DatabaseSync } from 'node:sqlite';
import { gzipSync } from 'node:zlib';
import { readFileSync, writeFileSync, rmSync, mkdirSync, cpSync, existsSync, readdirSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = resolve(__dirname, '..');
const OUT = resolve(process.argv[2] || join(ROOT, '..', 'dist-lite'));
const SRC_DB = join(ROOT, 'db', 'z80live.sqlite');
const TMP_DB = join(ROOT, 'db', '_lite.sqlite');

console.log('Export LITE ->', OUT);
rmSync(OUT, { recursive: true, force: true });
mkdirSync(join(OUT, 'db'), { recursive: true });

// 1) Base allégée : table `sources` uniquement (pas de FTS — sql.js CDN n'a pas fts5 —
//    ni de legacy_json). La recherche lite se fait en LIKE côté client.
rmSync(TMP_DB, { force: true });
const src = new DatabaseSync(SRC_DB);
const lite = new DatabaseSync(TMP_DB);
const cols = ['id','name','slugname','author','owner','description','category','genre','group_name','code',
  'assembler','buildmode','entry_point','start_point','end_point','command','filename','output_type',
  'build_status','compilable','fork_parent','created_at','updated_at'];
lite.exec(`CREATE TABLE sources (${cols.map(c => c + ' TEXT').join(', ')});`);
lite.exec('CREATE INDEX idx_bm ON sources(buildmode);');
const rows = src.prepare(`SELECT ${cols.join(',')} FROM sources`).all();
const ins = lite.prepare(`INSERT INTO sources (${cols.join(',')}) VALUES (${cols.map(c => '$' + c).join(',')})`);
lite.exec('BEGIN');
for (const r of rows) ins.run(Object.fromEntries(cols.map(c => ['$' + c, r[c] ?? null])));
lite.exec('COMMIT');
lite.exec('VACUUM;');
src.close(); lite.close();

// 2) Gzip de la base (l'asm compresse très bien) — décompressée dans le navigateur.
const raw = readFileSync(TMP_DB);
const gz = gzipSync(raw, { level: 9 });
writeFileSync(join(OUT, 'db', 'z80live.sqlite.gz'), gz);
rmSync(TMP_DB, { force: true });
console.log(`  base lite: ${rows.length} sources, ${(raw.length/1e6).toFixed(1)} Mo -> gz ${(gz.length/1e6).toFixed(1)} Mo`);

// 3) SPA compilée : on copie tout app/dist (index + JS/CSS + wasm/emu/vendor/sw depuis public/).
const SPA = join(ROOT, 'app', 'dist');
if (!existsSync(join(SPA, 'index.html'))) {
  console.error('\n⚠ app/dist introuvable. Lance d’abord :  (cd app && npm run build)\n');
  process.exit(1);
}
for (const entry of readdirSync(SPA)) cpSync(join(SPA, entry), join(OUT, entry), { recursive: true });

console.log('Terminé. Servir en statique : cd', OUT, '&& npx serve  (ou tout hébergeur statique).');
console.log('(openStore bascule en mode local : pas d’API -> base SQLite embarquée.)');
