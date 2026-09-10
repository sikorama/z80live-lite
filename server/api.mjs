// z80live — API REST minimale (node:http + node:sqlite), zéro dépendance.
// Sert les sources depuis le fichier SQLite portable. Compilation = côté client (WASM).
// Usage: node --experimental-sqlite server/api.mjs
//   env: PORT (3000), DB (../db/z80live.sqlite), Z80_WRITE_TOKEN (si défini, requis pour écrire)
import { createServer } from 'node:http';
import { DatabaseSync } from 'node:sqlite';
import { randomUUID } from 'node:crypto';
import { fileURLToPath } from 'node:url';
import { dirname, join, resolve, normalize, extname } from 'node:path';
import { readFile } from 'node:fs/promises';
import { existsSync, readFileSync } from 'node:fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 3000);
const DB_PATH = resolve(process.env.DB || join(__dirname, '../db/z80live.sqlite'));
const WRITE_TOKEN = process.env.Z80_WRITE_TOKEN || null; // null => écriture ouverte

const db = new DatabaseSync(DB_PATH);
db.exec('PRAGMA foreign_keys = ON;');
// Bootstrap idempotent : rejoue db/schema.sql (CREATE TABLE/INDEX IF NOT
// EXISTS partout) a chaque demarrage. Un seul texte canonique — celui que
// db/import.mjs applique aussi — plutot qu'une copie a la main ici qui
// pourrait diverger : une base deja en service ne perd rien (IF NOT EXISTS),
// une base neuve (comme dans les tests) n'a pas besoin d'une etape d'import
// separee pour exister.
db.exec(readFileSync(join(__dirname, '../db/schema.sql'), 'utf8'));

// Colonnes exposées en liste (léger, sans le code). Profil/Fichier de lien/Format n'en font
// plus partie (docs/adr/0002) : ce sont des réglages de Cible/Membre, pas de Source — voir
// `q.get` plus bas, seul endroit qui les rejoint encore, pour que l'appelant (App.svelte,
// `selectSource()`) n'ait RIEN à changer : la forme de la réponse ne bouge pas, seul son
// stockage a changé de table.
const LIST_COLS = `id, name, slugname, author, owner, description, category, genre, group_name,
  assembler, buildmode, entry_point, start_point, end_point, command, filename, is_include,
  build_status, compilable, fork_parent, created_at, updated_at`;
const FULL_COLS = `${LIST_COLS}, code`;
const LIST_COLS_S = LIST_COLS.replace(/\b(\w+)\b/g, 's.$1'); // colonnes qualifiées pour les jointures FTS
const FULL_COLS_S = FULL_COLS.replace(/\b(\w+)\b/g, 's.$1');

const q = {
  list: db.prepare(`SELECT ${LIST_COLS} FROM sources ORDER BY updated_at DESC LIMIT $limit OFFSET $offset`),
  listMode: db.prepare(`SELECT ${LIST_COLS} FROM sources WHERE buildmode = $mode ORDER BY updated_at DESC LIMIT $limit OFFSET $offset`),
  // `filename` n'est pas dans l'index FTS (db/schema.sql) : chercher « toolbox.asm » ne
  // trouvait que les sources qui l'INCLUENT, jamais la librairie elle-même. Les
  // correspondances de nom/nom de fichier passent devant — c'est ce que veut qui tape un
  // nom de fichier — puis viennent les correspondances de code, par rang FTS.
  search: db.prepare(`SELECT ${LIST_COLS} FROM (
        SELECT ${LIST_COLS_S}, 0 AS pri, 0.0 AS rk FROM sources s
         WHERE s.name LIKE $like OR s.filename LIKE $like
        UNION ALL
        SELECT ${LIST_COLS_S}, 1 AS pri, f.rank AS rk FROM sources s
         JOIN sources_fts f ON f.rowid = s.rowid
         WHERE sources_fts MATCH $q
           AND s.name NOT LIKE $like AND (s.filename IS NULL OR s.filename NOT LIKE $like)
      ) ORDER BY pri, rk LIMIT $limit OFFSET $offset`),
  // Repli quand MATCH lève (opérateurs FTS mal formés dans la requête de l'utilisateur).
  searchLike: db.prepare(`SELECT ${LIST_COLS} FROM sources
      WHERE name LIKE $like OR filename LIKE $like OR author LIKE $like OR description LIKE $like
      ORDER BY updated_at DESC LIMIT $limit OFFSET $offset`),
  // Profil/format/fichier de lien : rejoints depuis la Cible/le Membre IMPLICITE de cette
  // Source (`default_target_id`), avec les memes noms que les anciennes colonnes — c'est
  // volontaire (docs/adr/0002) : le contrat rendu a l'appelant ne change pas, seul l'endroit
  // qui les porte a change. NULL des deux cotes si la Source n'en a pas encore (jamais
  // sauvegardee avec ces reglages).
  get: db.prepare(`SELECT ${FULL_COLS_S}, t.profile AS profile, t.format AS container, tm.ld_filename AS ld_filename
      FROM sources s
      LEFT JOIN targets t ON t.id = s.default_target_id
      LEFT JOIN target_members tm ON tm.target_id = s.default_target_id AND tm.source_id = s.id
      WHERE s.id = $id`),
  includes: db.prepare(`SELECT id, name, filename, code FROM sources WHERE is_include = 1 ORDER BY name`),
  count: db.prepare(`SELECT COUNT(*) c FROM sources`),
  del: db.prepare(`DELETE FROM sources WHERE id = $id`),
};

// Formats de Cible actifs (ADR 0002, Q2) : dsk/cdt/cro viendront quand
// fantams les livrera reellement, pas avant (deviner leur forme n'aide pas).
const TARGET_FORMATS = new Set(['sna', 'cpr']);

const qp = {
  list: db.prepare(`SELECT p.id, p.name, p.created_at, p.updated_at,
      (SELECT COUNT(*) FROM targets t WHERE t.project_id = p.id) AS target_count
      FROM projects p ORDER BY p.updated_at DESC`),
  get: db.prepare(`SELECT id, name, created_at, updated_at FROM projects WHERE id = $id`),
  del: db.prepare(`DELETE FROM projects WHERE id = $id`),
};

const qt = {
  listForProject: db.prepare(`SELECT id, project_id, name, format, profile, created_at, updated_at
      FROM targets WHERE project_id = $project_id ORDER BY created_at`),
  get: db.prepare(`SELECT id, project_id, name, format, profile, created_at, updated_at
      FROM targets WHERE id = $id`),
  members: db.prepare(`SELECT m.source_id, m.bank, m.ld_filename, s.name AS source_name
      FROM target_members m JOIN sources s ON s.id = m.source_id
      WHERE m.target_id = $target_id ORDER BY m.bank, s.name`),
};

function getTargetFull(id) {
  const t = qt.get.get({ id });
  if (!t) return null;
  return { ...t, members: qt.members.all({ target_id: id }) };
}

function getProjectFull(id) {
  const p = qp.get.get({ id });
  if (!p) return null;
  const targets = qt.listForProject.all({ project_id: id })
    .map((t) => ({ ...t, members: qt.members.all({ target_id: t.id }) }));
  return { ...p, targets };
}

// Champs modifiables par l'API — colonnes REELLES de `sources`. Profil/Fichier de
// lien/Format n'y sont plus (docs/adr/0002) : voir TARGET_FIELDS et applyTargetFields().
const WRITABLE = ['name', 'slugname', 'author', 'owner', 'description', 'category', 'genre',
  'group_name', 'code', 'assembler', 'buildmode', 'entry_point', 'start_point',
  'end_point', 'command', 'filename', 'output_type', 'is_include', 'build_status', 'compilable'];

// L'appelant (App.svelte) envoie encore ces trois clefs dans le corps de la requete, comme
// avant docs/adr/0002 : c'est INTENTIONNEL, pas une dette — seule leur DESTINATION a change.
const TARGET_FIELDS = ['profile', 'ld_filename', 'container'];

// La Cible implicite d'une Source seule (ADR 0002, Q1 : "une Source seule EST un Projet a
// un membre") : creee a la premiere ecriture de profil/format/fichier de lien, jamais avant
// (une Source qui ne configure jamais ces reglages n'accumule pas un Projet vide derriere elle).
function ensureDefaultTarget(sourceId, sourceName) {
  const row = db.prepare(`SELECT default_target_id FROM sources WHERE id = $id`).get({ id: sourceId });
  if (row?.default_target_id) return row.default_target_id;
  const now = Date.now();
  const projectId = randomUUID(), targetId = randomUUID();
  db.prepare(`INSERT INTO projects (id, name, created_at, updated_at) VALUES ($id, $name, $now, $now)`)
    .run({ id: projectId, name: sourceName || 'untitled', now });
  db.prepare(`INSERT INTO targets (id, project_id, name, format, profile, created_at, updated_at)
      VALUES ($id, $project_id, $name, 'sna', NULL, $now, $now)`)
    .run({ id: targetId, project_id: projectId, name: sourceName || 'untitled', now });
  db.prepare(`INSERT INTO target_members (target_id, source_id, bank, ld_filename)
      VALUES ($target_id, $source_id, NULL, NULL)`).run({ target_id: targetId, source_id: sourceId });
  db.prepare(`UPDATE sources SET default_target_id = $target_id WHERE id = $source_id`)
    .run({ target_id: targetId, source_id: sourceId });
  return targetId;
}

// Route profil/format/fichier de lien vers la Cible/le Membre implicite (ADR 0002). Ne cree
// RIEN si aucun des trois n'est present dans `data` : une simple mise a jour de nom ou de
// description ne doit pas fabriquer une Cible.
function applyTargetFields(sourceId, sourceName, data) {
  if (!TARGET_FIELDS.some((k) => k in data)) return;
  const targetId = ensureDefaultTarget(sourceId, sourceName);
  const sets = [], params = { id: targetId, now: Date.now() };
  if ('profile' in data) { sets.push('profile = $profile'); params.profile = data.profile || null; }
  if ('container' in data) {
    sets.push('format = $format');
    params.format = TARGET_FORMATS.has(data.container) ? data.container : 'sna';
  }
  if (sets.length) {
    sets.push('updated_at = $now');
    db.prepare(`UPDATE targets SET ${sets.join(', ')} WHERE id = $id`).run(params);
  }
  if ('ld_filename' in data) {
    db.prepare(`UPDATE target_members SET ld_filename = $ld WHERE target_id = $tid AND source_id = $sid`)
      .run({ ld: data.ld_filename || null, tid: targetId, sid: sourceId });
  }
}

function insertSource(data, { fork_parent = null } = {}) {
  const id = randomUUID();
  const now = Date.now();
  const row = { id, fork_parent, created_at: now, updated_at: now };
  for (const k of WRITABLE) row[k] = data[k] ?? null;
  row.name = row.name || 'untitled';
  row.code = row.code || '';
  const cols = ['id', ...WRITABLE, 'fork_parent', 'created_at', 'updated_at'];
  db.prepare(`INSERT INTO sources (${cols.join(',')}) VALUES (${cols.map((c) => '$' + c).join(',')})`).run(row);
  if (!row.is_include) applyTargetFields(id, row.name, data);
  return q.get.get({ id });
}

function updateSource(id, data) {
  const existing = q.get.get({ id });
  if (!existing) return null;
  const sets = [], params = { id, updated_at: Date.now() };
  for (const k of WRITABLE) {
    if (k in data) { sets.push(`${k} = $${k}`); params[k] = data[k]; }
  }
  sets.push('updated_at = $updated_at');
  db.prepare(`UPDATE sources SET ${sets.join(', ')} WHERE id = $id`).run(params);
  if (!existing.is_include) applyTargetFields(id, data.name ?? existing.name, data);
  return q.get.get({ id });
}

// ---- helpers HTTP ----
const json = (res, code, body) => {
  const b = JSON.stringify(body);
  res.writeHead(code, { 'Content-Type': 'application/json; charset=utf-8', 'Access-Control-Allow-Origin': '*',
    'Access-Control-Allow-Methods': 'GET,POST,PUT,DELETE,OPTIONS', 'Access-Control-Allow-Headers': 'Content-Type,Authorization' });
  res.end(b);
};
const readBody = (req) => new Promise((ok, ko) => {
  let d = ''; req.on('data', (c) => { d += c; if (d.length > 8e6) req.destroy(); });
  req.on('end', () => { try { ok(d ? JSON.parse(d) : {}); } catch (e) { ko(e); } });
});
const readBinary = (req) => new Promise((ok) => {
  const chunks = []; req.on('data', (c) => chunks.push(c)); req.on('end', () => ok(Buffer.concat(chunks)));
});

// Store transitoire des builds (en mémoire) : sert le binaire à une URL avec extension
// (l'émulateur détecte SNA/DSK par l'extension ; un blob: URL n'en a pas).
const scratch = new Map(); // name -> Buffer
const SCRATCH_MAX = 24;
const canWrite = (req) => !WRITE_TOKEN || req.headers.authorization === `Bearer ${WRITE_TOKEN}`;

// Racine statique servie en même origine que l'API : la SPA compilée (app/dist) si présente,
// sinon la démo brute (z80next/). Permet le dev de la démo sans build.
const SPA_DIR = resolve(__dirname, '..', 'app', 'dist');
const HAS_SPA = existsSync(join(SPA_DIR, 'index.html'));
const STATIC_ROOT = HAS_SPA ? SPA_DIR : resolve(__dirname, '..');
const INDEX = HAS_SPA ? 'index.html' : join('demo', 'index.html');
const MIME = {
  '.html': 'text/html; charset=utf-8', '.mjs': 'text/javascript; charset=utf-8',
  '.js': 'text/javascript; charset=utf-8', '.wasm': 'application/wasm',
  '.css': 'text/css; charset=utf-8', '.json': 'application/json; charset=utf-8',
  '.sna': 'application/octet-stream', '.dsk': 'application/octet-stream',
  '.png': 'image/png', '.webp': 'image/webp', '.ico': 'image/x-icon',
};
async function serveStatic(req, res, pathname) {
  const rel = normalize(decodeURIComponent(pathname)).replace(/^(\.\.[/\\])+/, '');
  let filePath = join(STATIC_ROOT, rel);
  if (!filePath.startsWith(STATIC_ROOT)) return json(res, 403, { error: 'forbidden' });
  if (pathname === '/' || pathname === '') filePath = join(STATIC_ROOT, INDEX);
  else if (pathname.endsWith('/')) filePath = join(filePath, 'index.html'); // index de dossier
  try {
    const data = await readFile(filePath);
    res.writeHead(200, { 'Content-Type': MIME[extname(filePath)] || 'application/octet-stream' });
    res.end(data);
  } catch {
    json(res, 404, { error: 'not found', path: pathname });
  }
}

const server = createServer(async (req, res) => {
  const url = new URL(req.url, `http://${req.headers.host}`);
  const p = url.pathname.replace(/\/+$/, '');
  const m = req.method;
  try {
    if (m === 'OPTIONS') return json(res, 204, {});

    // GET /api/sources
    if (m === 'GET' && p === '/api/sources') {
      const limit = Math.min(Number(url.searchParams.get('limit')) || 50, 500);
      const offset = Number(url.searchParams.get('offset')) || 0;
      const search = url.searchParams.get('q');
      const mode = url.searchParams.get('buildmode');
      let rows;
      if (search) {
        const like = `%${search}%`;
        try { rows = q.search.all({ q: search, like, limit, offset }); }
        catch { rows = q.searchLike.all({ like, limit, offset }); }
      }
      else if (mode) rows = q.listMode.all({ mode, limit, offset });
      else rows = q.list.all({ limit, offset });
      return json(res, 200, { total: q.count.get().c, count: rows.length, offset, items: rows });
    }

    // GET /api/sources/:id
    let mo = p.match(/^\/api\/sources\/([^/]+)$/);
    if (m === 'GET' && mo) {
      const row = q.get.get({ id: decodeURIComponent(mo[1]) });
      return row ? json(res, 200, row) : json(res, 404, { error: 'not found' });
    }

    // POST /api/sources  (create)
    if (m === 'POST' && p === '/api/sources') {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      return json(res, 201, insertSource(await readBody(req)));
    }

    // POST /api/sources/:id/fork
    mo = p.match(/^\/api\/sources\/([^/]+)\/fork$/);
    if (m === 'POST' && mo) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const parent = q.get.get({ id: decodeURIComponent(mo[1]) });
      if (!parent) return json(res, 404, { error: 'parent not found' });
      const overrides = await readBody(req);
      const data = { ...parent, name: overrides.name || `${parent.name} (fork)`, ...overrides };
      return json(res, 201, insertSource(data, { fork_parent: parent.id }));
    }

    // PUT /api/sources/:id  (update)
    mo = p.match(/^\/api\/sources\/([^/]+)$/);
    if (m === 'PUT' && mo) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const row = updateSource(decodeURIComponent(mo[1]), await readBody(req));
      return row ? json(res, 200, row) : json(res, 404, { error: 'not found' });
    }

    // DELETE /api/sources/:id
    if (m === 'DELETE' && mo) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const r = q.del.run({ id: decodeURIComponent(mo[1]) });
      return json(res, r.changes ? 200 : 404, { deleted: r.changes });
    }

    // GET /api/projects
    if (m === 'GET' && p === '/api/projects') return json(res, 200, { items: qp.list.all() });

    // POST /api/projects  { name }
    if (m === 'POST' && p === '/api/projects') {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const body = await readBody(req);
      const id = randomUUID(), now = Date.now();
      db.prepare(`INSERT INTO projects (id, name, created_at, updated_at) VALUES ($id, $name, $now, $now)`)
        .run({ id, name: body.name || 'untitled project', now });
      return json(res, 201, getProjectFull(id));
    }

    // GET /api/projects/:id  (avec ses Cibles et leurs Membres)
    let mp = p.match(/^\/api\/projects\/([^/]+)$/);
    if (m === 'GET' && mp) {
      const proj = getProjectFull(decodeURIComponent(mp[1]));
      return proj ? json(res, 200, proj) : json(res, 404, { error: 'not found' });
    }

    // PUT /api/projects/:id  { name }
    if (m === 'PUT' && mp) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const id = decodeURIComponent(mp[1]);
      if (!qp.get.get({ id })) return json(res, 404, { error: 'not found' });
      const body = await readBody(req);
      if ('name' in body) db.prepare(`UPDATE projects SET name = $name, updated_at = $now WHERE id = $id`)
        .run({ id, name: body.name, now: Date.now() });
      return json(res, 200, getProjectFull(id));
    }

    // DELETE /api/projects/:id
    if (m === 'DELETE' && mp) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const r = qp.del.run({ id: decodeURIComponent(mp[1]) });
      return json(res, r.changes ? 200 : 404, { deleted: r.changes });
    }

    // POST /api/projects/:id/targets  { name, format, profile? } — une Cible
    // (ADR 0002 : un Projet peut en porter plusieurs, y compris du meme Format).
    let mt = p.match(/^\/api\/projects\/([^/]+)\/targets$/);
    if (m === 'POST' && mt) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const project_id = decodeURIComponent(mt[1]);
      if (!qp.get.get({ id: project_id })) return json(res, 404, { error: 'project not found' });
      const body = await readBody(req);
      if (!TARGET_FORMATS.has(body.format))
        return json(res, 400, { error: `format inconnu : '${body.format}' (au choix : ${[...TARGET_FORMATS].join(', ')})` });
      const id = randomUUID(), now = Date.now();
      db.prepare(`INSERT INTO targets (id, project_id, name, format, profile, created_at, updated_at)
          VALUES ($id, $project_id, $name, $format, $profile, $now, $now)`)
        .run({ id, project_id, name: body.name || 'untitled target', format: body.format, profile: body.profile || null, now });
      db.prepare(`UPDATE projects SET updated_at = $now WHERE id = $id`).run({ id: project_id, now });
      return json(res, 201, getTargetFull(id));
    }

    // POST /api/projects/:pid/targets/:tid/members { source_id, bank?, ld_filename? }
    // Affecte un point d'entree a une Cible (ADR 0002). Le role qu'exige le
    // Format de la Cible est verifie ICI, pas dans le schema : `bank` est
    // OBLIGATOIRE et unique pour un Conteneur CPR (deux banques ne peuvent
    // pas partager un numero), absent pour une Base SNA, qui ne compte
    // qu'UN SEUL Membre (Q4).
    let mm = p.match(/^\/api\/projects\/([^/]+)\/targets\/([^/]+)\/members$/);
    if (m === 'POST' && mm) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const project_id = decodeURIComponent(mm[1]), target_id = decodeURIComponent(mm[2]);
      const target = qt.get.get({ id: target_id });
      if (!target || target.project_id !== project_id) return json(res, 404, { error: 'target not found' });
      const body = await readBody(req);
      if (!body.source_id || !q.get.get({ id: body.source_id }))
        return json(res, 400, { error: 'source_id manquant ou introuvable' });
      let bank = null;
      if (target.format === 'cpr') {
        bank = Number(body.bank);
        if (!Number.isInteger(bank) || bank < 0 || bank > 31)
          return json(res, 400, { error: 'une Cible cpr exige "bank" entre 0 et 31' });
        const clash = qt.members.all({ target_id }).find((mb) => mb.bank === bank && mb.source_id !== body.source_id);
        if (clash) return json(res, 400, { error: `la banque ${bank} est deja prise par '${clash.source_name}'` });
      } else {
        if (body.bank !== undefined && body.bank !== null)
          return json(res, 400, { error: `une Cible ${target.format} ne prend pas de "bank"` });
        const existing = qt.members.all({ target_id });
        if (existing.length >= 1 && existing[0].source_id !== body.source_id)
          return json(res, 400, { error: `une Cible ${target.format} ne compte qu'un seul Membre` });
      }
      db.prepare(`INSERT INTO target_members (target_id, source_id, bank, ld_filename)
          VALUES ($target_id, $source_id, $bank, $ld_filename)
          ON CONFLICT(target_id, source_id) DO UPDATE SET bank = excluded.bank, ld_filename = excluded.ld_filename`)
        .run({ target_id, source_id: body.source_id, bank, ld_filename: body.ld_filename || null });
      db.prepare(`UPDATE targets SET updated_at = $now WHERE id = $id`).run({ id: target_id, now: Date.now() });
      return json(res, 201, getTargetFull(target_id));
    }

    // DELETE /api/projects/:pid/targets/:tid/members/:source_id
    let md = p.match(/^\/api\/projects\/([^/]+)\/targets\/([^/]+)\/members\/([^/]+)$/);
    if (m === 'DELETE' && md) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const [, pid, tid, sid] = md.map(decodeURIComponent);
      const target = qt.get.get({ id: tid });
      if (!target || target.project_id !== pid) return json(res, 404, { error: 'target not found' });
      db.prepare(`DELETE FROM target_members WHERE target_id = $tid AND source_id = $sid`).run({ tid, sid });
      db.prepare(`UPDATE targets SET updated_at = $now WHERE id = $id`).run({ id: tid, now: Date.now() });
      return json(res, 200, getTargetFull(tid));
    }

    // DELETE /api/projects/:pid/targets/:tid  (ses Membres partent avec elle)
    let mtd = p.match(/^\/api\/projects\/([^/]+)\/targets\/([^/]+)$/);
    if (m === 'DELETE' && mtd) {
      if (!canWrite(req)) return json(res, 401, { error: 'write token required' });
      const project_id = decodeURIComponent(mtd[1]), target_id = decodeURIComponent(mtd[2]);
      const target = qt.get.get({ id: target_id });
      if (!target || target.project_id !== project_id) return json(res, 404, { error: 'target not found' });
      db.prepare(`DELETE FROM targets WHERE id = $id`).run({ id: target_id });
      db.prepare(`UPDATE projects SET updated_at = $now WHERE id = $id`).run({ id: project_id, now: Date.now() });
      return json(res, 200, getProjectFull(project_id));
    }

    // POST /api/scratch?ext=sna  (corps binaire) -> { url } avec extension pour l'émulateur
    if (m === 'POST' && p === '/api/scratch') {
      const ext = (url.searchParams.get('ext') || 'sna').replace(/[^a-z0-9]/gi, '') || 'sna';
      const name = randomUUID() + '.' + ext;
      scratch.set(name, await readBinary(req));
      while (scratch.size > SCRATCH_MAX) scratch.delete(scratch.keys().next().value); // évince le plus ancien
      return json(res, 201, { url: '/api/scratch/' + name });
    }
    // GET /api/scratch/:name  -> binaire (consommé par l'émulateur via ?file=)
    let sc = p.match(/^\/api\/scratch\/([\w.-]+)$/);
    if (m === 'GET' && sc) {
      const buf = scratch.get(sc[1]);
      if (!buf) return json(res, 404, { error: 'expired' });
      res.writeHead(200, { 'Content-Type': 'application/octet-stream', 'Access-Control-Allow-Origin': '*',
        'Content-Disposition': `attachment; filename="${sc[1]}"` });
      return res.end(buf);
    }

    // GET /api/includes  (fichiers librairie is_include=1, avec leur code : injectés dans le FS wasm à l'assemblage)
    if (m === 'GET' && p === '/api/includes') return json(res, 200, { items: q.includes.all() });

    if (m === 'GET' && p === '/api/health') return json(res, 200, { status: 'ok', db: DB_PATH, sources: q.count.get().c });

    // Fichiers statiques (démo, wasm, émulateur) pour toute route GET hors /api.
    if (m === 'GET' && !p.startsWith('/api/')) return serveStatic(req, res, url.pathname);

    json(res, 404, { error: 'route not found' });
  } catch (e) {
    json(res, 500, { error: String(e && e.message || e) });
  }
});

server.listen(PORT, () => {
  console.log(`z80live API sur http://localhost:${PORT}  (db: ${DB_PATH}, écriture: ${WRITE_TOKEN ? 'token requis' : 'ouverte'})`);
});
