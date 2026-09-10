// Migration docs/adr/0002 : retire `profile`/`ld_filename`/`container` de `sources`,
// vers une Cible + un Membre IMPLICITE par Source (Q1 : "une Source seule EST un
// Projet a un membre"). Rejouable : une Source deja migree (default_target_id non
// nul) est laissee telle quelle.
// Usage: node --experimental-sqlite db/migrate-0002-target-fields.mjs
import { DatabaseSync } from 'node:sqlite';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve } from 'node:path';
import { readFileSync } from 'node:fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const DB_PATH = resolve(process.env.DB || join(__dirname, 'z80live.sqlite'));

const db = new DatabaseSync(DB_PATH);
db.exec('PRAGMA foreign_keys = ON;');

// `targets`/`target_members` : deja dans schema.sql, mais une base plus ancienne que
// cette session ne les a peut-etre pas encore — meme bootstrap idempotent que le serveur.
db.exec(readFileSync(join(__dirname, 'schema.sql'), 'utf8'));

const cols = db.prepare(`PRAGMA table_info(sources)`).all().map((c) => c.name);
if (!cols.includes('default_target_id')) {
  db.exec(`ALTER TABLE sources ADD COLUMN default_target_id TEXT REFERENCES targets(id) ON DELETE SET NULL`);
  console.log('colonne default_target_id ajoutee');
}
if (!cols.includes('profile')) {
  console.log('rien a migrer : profile/ld_filename/container deja absents de sources');
  process.exit(0);
}

const rows = db.prepare(
  `SELECT id, name, profile, ld_filename, container, is_include, default_target_id FROM sources`
).all();

let migrated = 0, skippedLib = 0, skippedDone = 0;
const now = Date.now();
for (const r of rows) {
  if (r.is_include) { skippedLib++; continue; }          // une lib n'a pas de Cible a elle
  if (r.default_target_id) { skippedDone++; continue; }   // deja migree (script rejouable)
  const projectId = randomUUID(), targetId = randomUUID();
  const format = r.container === 'cpr' ? 'cpr' : 'sna';   // seul 'sna' etait actif avant cette session
  db.prepare(`INSERT INTO projects (id, name, created_at, updated_at) VALUES ($id, $name, $now, $now)`)
    .run({ id: projectId, name: r.name || 'untitled', now });
  db.prepare(`INSERT INTO targets (id, project_id, name, format, profile, created_at, updated_at)
      VALUES ($id, $project_id, $name, $format, $profile, $now, $now)`)
    .run({ id: targetId, project_id: projectId, name: r.name || 'untitled', format, profile: r.profile || null, now });
  db.prepare(`INSERT INTO target_members (target_id, source_id, bank, ld_filename)
      VALUES ($target_id, $source_id, NULL, $ld)`)
    .run({ target_id: targetId, source_id: r.id, ld: r.ld_filename || null });
  db.prepare(`UPDATE sources SET default_target_id = $target_id WHERE id = $source_id`)
    .run({ target_id: targetId, source_id: r.id });
  migrated++;
}
console.log(`${migrated} sources migrees vers un Projet/Cible/Membre implicite`
  + (skippedLib ? `, ${skippedLib} librairies ignorees` : '')
  + (skippedDone ? `, ${skippedDone} deja migrees` : ''));

db.exec(`ALTER TABLE sources DROP COLUMN profile;`);
db.exec(`ALTER TABLE sources DROP COLUMN ld_filename;`);
db.exec(`ALTER TABLE sources DROP COLUMN container;`);
console.log('colonnes profile/ld_filename/container retirees de sources (docs/adr/0002)');
