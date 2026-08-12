// Couche d'accès aux sources, agnostique du backend.
// Deux implémentations, même interface { mode, count, list, get } :
//   - createApiStore(base)  -> parle à l'API REST (version complète, avec écriture ailleurs)
//   - createLocalStore(db)  -> lit une base SQLite en mémoire via sql.js (version lite, statique)
// L'UI ne dépend que de cette interface.

const LIST_COLS = `id, name, slugname, author, owner, description, category, genre, group_name,
  assembler, buildmode, entry_point, start_point, end_point, command, filename, is_include,
  build_status, compilable, fork_parent, created_at, updated_at`;
const LIST_COLS_S = LIST_COLS.replace(/\b(\w+)\b/g, 's.$1');

// ---- Backend API (version complète, lecture + écriture) ----
export function createApiStore(base = '..', { token } = {}) {
  const j = (p) => fetch(base + p).then((r) => r.json());
  const send = (method, p, body) => fetch(base + p, {
    method,
    headers: { 'Content-Type': 'application/json', ...(token ? { Authorization: 'Bearer ' + token } : {}) },
    body: body === undefined ? undefined : JSON.stringify(body),
  }).then((r) => r.json());
  return {
    mode: 'api',
    canWrite: true,
    async count() { return (await j('/api/health')).sources; },
    async list({ q, buildmode, limit = 50, offset = 0 } = {}) {
      const p = new URLSearchParams();
      if (q) p.set('q', q);
      if (buildmode) p.set('buildmode', buildmode);
      p.set('limit', limit); p.set('offset', offset);
      return (await j('/api/sources?' + p)).items;
    },
    get(id) { return j('/api/sources/' + encodeURIComponent(id)); },
    async listIncludes() { return (await j('/api/includes')).items; },
    create(data) { return send('POST', '/api/sources', data); },
    update(id, data) { return send('PUT', '/api/sources/' + encodeURIComponent(id), data); },
    fork(id, overrides = {}) { return send('POST', '/api/sources/' + encodeURIComponent(id) + '/fork', overrides); },
    remove(id) { return send('DELETE', '/api/sources/' + encodeURIComponent(id)); },
  };
}

// ---- Backend local SQLite via sql.js (version lite) ----
// `db` = instance sql.js Database déjà ouverte. `hasFts` : la build sql.js supporte-t-elle FTS5.
export function createLocalStore(db, { hasFts = true } = {}) {
  const rows = (sql, params) => {
    const st = db.prepare(sql);
    try { st.bind(params || {}); const out = []; while (st.step()) out.push(st.getAsObject()); return out; }
    finally { st.free(); }
  };
  const one = (sql, params) => rows(sql, params)[0] || null;
  // Repli sans FTS : LIKE sur nom/auteur/description.
  const likeSearch = (q, limit, offset) => rows(
    `SELECT ${LIST_COLS} FROM sources
     WHERE name LIKE $q OR author LIKE $q OR description LIKE $q
     ORDER BY updated_at DESC LIMIT $l OFFSET $o`,
    { $q: `%${q}%`, $l: limit, $o: offset });
  return {
    mode: 'local',
    canWrite: false,
    count() { return one(`SELECT COUNT(*) c FROM sources`).c; },
    list({ q, buildmode, limit = 50, offset = 0 } = {}) {
      if (q) {
        if (!hasFts) return likeSearch(q, limit, offset);
        try {
          return rows(
            `SELECT ${LIST_COLS_S} FROM sources s JOIN sources_fts f ON f.rowid = s.rowid
             WHERE sources_fts MATCH $q ORDER BY rank LIMIT $l OFFSET $o`,
            { $q: q, $l: limit, $o: offset });
        } catch { return likeSearch(q, limit, offset); }
      }
      if (buildmode) return rows(
        `SELECT ${LIST_COLS} FROM sources WHERE buildmode = $m ORDER BY updated_at DESC LIMIT $l OFFSET $o`,
        { $m: buildmode, $l: limit, $o: offset });
      return rows(`SELECT ${LIST_COLS} FROM sources ORDER BY updated_at DESC LIMIT $l OFFSET $o`,
        { $l: limit, $o: offset });
    },
    get(id) { return one(`SELECT ${LIST_COLS}, code FROM sources WHERE id = $id`, { $id: id }); },
    listIncludes() { return rows(`SELECT id, name, filename, code FROM sources WHERE is_include = 1 ORDER BY name`); },
  };
}

// Ouvre le bon store : API si dispo, sinon base locale.
//   openStore({ base, loadLocalDb })  où loadLocalDb() -> sql.js Database (fourni par l'hôte).
export async function openStore({ base = '..', loadLocalDb } = {}) {
  try {
    const r = await fetch(base + '/api/health', { signal: AbortSignal.timeout?.(1500) });
    if (r.ok) return createApiStore(base);
  } catch { /* pas d'API -> mode lite */ }
  if (!loadLocalDb) throw new Error('Aucune API et pas de base locale fournie');
  const { db, hasFts } = await loadLocalDb();
  return createLocalStore(db, { hasFts });
}
