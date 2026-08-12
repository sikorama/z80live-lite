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
import { existsSync } from 'node:fs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const PORT = Number(process.env.PORT || 3000);
const DB_PATH = resolve(process.env.DB || join(__dirname, '../db/z80live.sqlite'));
const WRITE_TOKEN = process.env.Z80_WRITE_TOKEN || null; // null => écriture ouverte

const db = new DatabaseSync(DB_PATH);
db.exec('PRAGMA foreign_keys = ON;');

// Colonnes exposées en liste (léger, sans le code).
const LIST_COLS = `id, name, slugname, author, owner, description, category, genre, group_name,
  assembler, buildmode, entry_point, start_point, end_point, command, filename, is_include,
  build_status, compilable, fork_parent, created_at, updated_at`;
const FULL_COLS = `${LIST_COLS}, code`;
const LIST_COLS_S = LIST_COLS.replace(/\b(\w+)\b/g, 's.$1'); // colonnes qualifiées pour les jointures FTS

const q = {
  list: db.prepare(`SELECT ${LIST_COLS} FROM sources ORDER BY updated_at DESC LIMIT $limit OFFSET $offset`),
  listMode: db.prepare(`SELECT ${LIST_COLS} FROM sources WHERE buildmode = $mode ORDER BY updated_at DESC LIMIT $limit OFFSET $offset`),
  search: db.prepare(`SELECT ${LIST_COLS_S} FROM sources s
      JOIN sources_fts f ON f.rowid = s.rowid
      WHERE sources_fts MATCH $q ORDER BY rank LIMIT $limit OFFSET $offset`),
  get: db.prepare(`SELECT ${FULL_COLS} FROM sources WHERE id = $id`),
  includes: db.prepare(`SELECT id, name, filename, code FROM sources WHERE is_include = 1 ORDER BY name`),
  count: db.prepare(`SELECT COUNT(*) c FROM sources`),
  del: db.prepare(`DELETE FROM sources WHERE id = $id`),
};

// Champs modifiables par l'API.
const WRITABLE = ['name', 'slugname', 'author', 'owner', 'description', 'category', 'genre',
  'group_name', 'code', 'assembler', 'buildmode', 'entry_point', 'start_point',
  'end_point', 'command', 'filename', 'output_type', 'is_include', 'build_status', 'compilable'];

function insertSource(data, { fork_parent = null } = {}) {
  const id = randomUUID();
  const now = Date.now();
  const row = { id, fork_parent, created_at: now, updated_at: now };
  for (const k of WRITABLE) row[k] = data[k] ?? null;
  row.name = row.name || 'untitled';
  row.code = row.code || '';
  const cols = ['id', ...WRITABLE, 'fork_parent', 'created_at', 'updated_at'];
  db.prepare(`INSERT INTO sources (${cols.join(',')}) VALUES (${cols.map((c) => '$' + c).join(',')})`).run(row);
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
      if (search) rows = q.search.all({ q: search, limit, offset });
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
