// Importe export_full.json (NDJSON Mongo) -> z80live.sqlite
// Usage: node --experimental-sqlite db/import.mjs [export.json] [out.sqlite]
import { DatabaseSync } from 'node:sqlite';
import { readFileSync } from 'node:fs';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';

const __dirname = dirname(fileURLToPath(import.meta.url));
const EXPORT = resolve(process.argv[2] || join(__dirname, '../../export_full.json'));
const OUT = resolve(process.argv[3] || join(__dirname, 'z80live.sqlite'));

const db = new DatabaseSync(OUT);
// Recréation complète (rebuild depuis le JSON) : on drop d'abord pour prendre en compte
// toute évolution du schéma (CREATE IF NOT EXISTS ne modifierait pas une table existante).
db.exec(`DROP TRIGGER IF EXISTS sources_ai; DROP TRIGGER IF EXISTS sources_ad; DROP TRIGGER IF EXISTS sources_au;
         DROP TABLE IF EXISTS sources_fts; DROP TABLE IF EXISTS sources;`);
db.exec(readFileSync(join(__dirname, 'schema.sql'), 'utf8'));

const insert = db.prepare(`
  INSERT INTO sources (
    id, name, slugname, author, owner, description, category, genre, group_name, code,
    assembler, buildmode, entry_point, start_point, end_point, command, filename, output_type,
    created_at, updated_at, legacy_json
  ) VALUES (
    $id, $name, $slugname, $author, $owner, $description, $category, $genre, $group_name, $code,
    $assembler, $buildmode, $entry_point, $start_point, $end_point, $command, $filename, $output_type,
    $created_at, $updated_at, $legacy_json
  )
`);

const s = (v) => (v === undefined || v === null ? null : String(v));
const n = (v) => (typeof v === 'number' && isFinite(v) ? Math.round(v) : null);

// Dérivation heuristique du genre (amorçage) à partir de la catégorie libre + nom + description.
// Modifiable ensuite par source dans l'UI. Ordre = priorité.
const GENRE_RULES = [
  ['jeu désassemblé', /disassembl|désassemb|ripped|disark|\bgame\b|\bjeu\b|fugitif|knight lore|tarzan|buggyboy/i],
  ['audio', /audio|music|musique|\bzic\b|\bay\b|ym2|\bmod\b|sample|sound|arkos|player.*(ay|ym)|wav2/i],
  ['démo', /\bdemo\b|démo|intro|logon|logo|transition|scroll|starfield|raster|plasma|rupture|delta|sinus/i],
  ['test asm', /\btest\b|essai|osef|quicktest|\bbug\b|examples?\b/i],
  ['tools', /\btool|conversion|compress|décompress|convert|generator|\bgen\b|editor/i],
  ['math', /\bmath\b|\blut\b|fractal|\balgo\b|trigo/i],
  ['graphisme', /sprite|\bgfx\b|graphic|dither|color cycl|\bdots\b|\bballs\b|\bdraw\b|dessine|animation|pixsaur|fade|iso/i],
];
function deriveGenre(cat, name, desc) {
  const hay = [cat, name, desc].filter(Boolean).join(' ');
  for (const [g, re] of GENRE_RULES) if (re.test(hay)) return g;
  return null; // non classé
}
// Métadonnées abandonnées (score, votes, user...) sans le code (déjà en colonne) : zéro perte, sans doublon.
const legacyJson = (d) => { const { code, ...rest } = d; return JSON.stringify(rest); };

const lines = readFileSync(EXPORT, 'utf8').split('\n').filter((l) => l.trim());
let ok = 0, skipped = 0;
const seen = new Set();

db.exec('BEGIN');
for (const line of lines) {
  let d;
  try { d = JSON.parse(line); } catch { skipped++; continue; }
  const id = s(d._id);
  if (!id || seen.has(id)) { skipped++; continue; }
  seen.add(id);
  const bo = (d.buildOptions && typeof d.buildOptions === 'object') ? d.buildOptions : {};
  insert.run({
    id,
    name: s(d.name) || id,
    slugname: s(d.slugname),
    author: s(d.author),
    owner: s(d.owner),
    description: s(d.desc),
    category: s(d.cat),
    genre: deriveGenre(d.cat, d.name, d.desc),
    group_name: s(d.group),
    code: s(d.code) || '',
    assembler: s(bo.assembler),
    buildmode: s(bo.buildmode),
    entry_point: s(bo.entryPoint),
    start_point: s(bo.startPoint),
    end_point: s(bo.endPoint),
    command: s(bo.command),
    filename: s(bo.filename),
    output_type: s(d.outputType),
    created_at: n(d.date),
    updated_at: n(d.timestamp),
    legacy_json: legacyJson(d),
  });
  ok++;
}
db.exec('COMMIT');
db.exec('PRAGMA wal_checkpoint(TRUNCATE); VACUUM;');

// Contrôles
const total = db.prepare('SELECT COUNT(*) c FROM sources').get().c;
const byMode = db.prepare(
  `SELECT COALESCE(buildmode,'(null)') m, COUNT(*) c FROM sources GROUP BY m ORDER BY c DESC`
).all();
console.log(`Importé: ${ok}  | ignorés: ${skipped}  | total en base: ${total}`);
console.log('Par buildmode:');
for (const r of byMode) console.log(`  ${String(r.c).padStart(4)}  ${r.m}`);
db.close();
