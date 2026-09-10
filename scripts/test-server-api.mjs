// Test d'integration : server/api.mjs, contre une vraie base temporaire.
//   node --experimental-sqlite scripts/test-server-api.mjs
//
// Le serveur est lance comme un VRAI PROCESSUS (pas importe) : api.mjs ecoute
// des le chargement du module (`server.listen` en bas de fichier, hors de
// toute fonction), l'importer directement demarrerait un serveur qu'on ne
// controle pas. Une base neuve, vide : le bootstrap idempotent du serveur
// (schema.sql complet, pas seulement projects/project_banks) doit suffire —
// aucune etape d'import separee a executer avant de tester.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import { join, dirname } from 'node:path';
import { fileURLToPath } from 'node:url';
import { createApiStore } from '../client/store.mjs';

const __dirname = dirname(fileURLToPath(import.meta.url));
const ROOT = join(__dirname, '..');
const PORT = 3999;
const BASE = `http://127.0.0.1:${PORT}`;

const dir = mkdtempSync(join(tmpdir(), 'z80live-test-'));
const dbPath = join(dir, 'test.sqlite');

const proc = spawn(process.execPath, ['--experimental-sqlite', 'server/api.mjs'], {
  cwd: ROOT,
  env: { ...process.env, PORT: String(PORT), DB: dbPath },
  stdio: ['ignore', 'pipe', 'pipe'],
});
let serverLog = '';
proc.stdout.on('data', (d) => { serverLog += d; });
proc.stderr.on('data', (d) => { serverLog += d; });

async function waitReady(tries = 100) {
  for (let i = 0; i < tries; i++) {
    try { const r = await fetch(BASE + '/api/health'); if (r.ok) return true; } catch {}
    await new Promise((r) => setTimeout(r, 50));
  }
  return false;
}

let pass = 0, total = 0;
async function check(desc, fn) {
  total++;
  try {
    const ok = await fn();
    if (ok) pass++; else console.log(`[FAIL] ${desc}`);
  } catch (e) { console.log(`[FAIL] ${desc} — ${e?.message || e}`); }
}
const api = (method, path, body) => fetch(BASE + path, {
  method, headers: { 'Content-Type': 'application/json' },
  body: body === undefined ? undefined : JSON.stringify(body),
});

async function main() {
  if (!(await waitReady())) {
    console.error('le serveur de test ne repond pas :\n' + serverLog);
    process.exitCode = 1; return cleanup();
  }

  // --- POST /api/projects/:id/targets : la premiere Cible d'un Projet -----
  const proj = await (await api('POST', '/api/projects', { name: 'p-targets' })).json();

  await check('creer une Cible sna', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets`, { name: 'demo', format: 'sna' });
    const j = await r.json();
    return r.ok && j.id && j.project_id === proj.id && j.name === 'demo' && j.format === 'sna'
      && Array.isArray(j.members) && j.members.length === 0;
  });

  await check('creer une Cible cpr avec profil', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets`, { name: 'cart', format: 'cpr', profile: 'cpcplus' });
    const j = await r.json();
    return r.ok && j.format === 'cpr' && j.profile === 'cpcplus';
  });

  await check('un Format inconnu est refuse', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets`, { name: 'x', format: 'dsk' });
    return r.status === 400;
  });

  await check('un Projet inconnu est refuse', async () => {
    const r = await api('POST', '/api/projects/pas-un-id/targets', { name: 'x', format: 'sna' });
    return r.status === 404;
  });

  await check('deux Cibles du meme Format sur le meme Projet (Q3, ADR 0002)', async () => {
    const r1 = await api('POST', `/api/projects/${proj.id}/targets`, { name: 'demo2', format: 'sna' });
    return r1.ok;
  });

  // --- POST .../targets/:tid/members : affecter un point d'entree a une Cible
  const src1 = await (await api('POST', '/api/sources', { name: 's1', code: '' })).json();
  const src2 = await (await api('POST', '/api/sources', { name: 's2', code: '' })).json();
  const cprTarget = await (await api('POST', `/api/projects/${proj.id}/targets`, { name: 'cart-members', format: 'cpr' })).json();
  const snaTarget = await (await api('POST', `/api/projects/${proj.id}/targets`, { name: 'sna-members', format: 'sna' })).json();

  await check('ajouter un Membre a une Cible CPR (banque + fichier de lien)', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${cprTarget.id}/members`,
      { source_id: src1.id, bank: 3, ld_filename: 'b3.ld' });
    const j = await r.json();
    const m = j.members?.find((x) => x.source_id === src1.id);
    return r.ok && m && m.bank === 3 && m.ld_filename === 'b3.ld' && m.source_name === 's1';
  });

  await check('une Cible CPR exige une banque 0..31', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${cprTarget.id}/members`, { source_id: src2.id });
    return r.status === 400;
  });

  await check('deux Membres, la meme banque : refuse', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${cprTarget.id}/members`, { source_id: src2.id, bank: 3 });
    return r.status === 400;
  });

  await check('une Source inconnue est refusee', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${cprTarget.id}/members`, { source_id: 'pas-une-source', bank: 4 });
    return r.status === 400;
  });

  await check('une Cible SNA accepte un premier Membre, SANS banque', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${snaTarget.id}/members`, { source_id: src1.id });
    const j = await r.json();
    return r.ok && j.members.length === 1 && j.members[0].bank === null;
  });

  await check('une Cible SNA refuse un second Membre (Q4, ADR 0002)', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/${snaTarget.id}/members`, { source_id: src2.id });
    return r.status === 400;
  });

  await check('une Cible inconnue est refusee', async () => {
    const r = await api('POST', `/api/projects/${proj.id}/targets/pas-une-cible/members`, { source_id: src1.id, bank: 5 });
    return r.status === 404;
  });

  // --- DELETE .../members/:source_id et DELETE .../targets/:tid -----------
  await check('supprimer un Membre', async () => {
    const r = await api('DELETE', `/api/projects/${proj.id}/targets/${cprTarget.id}/members/${src1.id}`);
    const j = await r.json();
    return r.ok && j.members.length === 0;
  });

  await check('supprimer une Cible (et ses Membres avec elle)', async () => {
    const r = await api('DELETE', `/api/projects/${proj.id}/targets/${snaTarget.id}`);
    if (!r.ok) return false;
    const proj2 = await (await api('GET', `/api/projects/${proj.id}`)).json();
    return !proj2.targets.some((t) => t.id === snaTarget.id);
  });

  // --- client/store.mjs : le meme seam, vu par l'app (createApiStore) -----
  const store = createApiStore(BASE);
  const sproj = await store.createProject({ name: 'store-project' });
  const ssrc = await store.create({ name: 'store-src', code: '' });

  await check('store.createTarget', async () => {
    const t = await store.createTarget(sproj.id, { name: 'demo', format: 'sna' });
    return t.id && t.format === 'sna';
  });

  const st = await store.createTarget(sproj.id, { name: 'cart', format: 'cpr' });

  await check('store.setTargetMember', async () => {
    const t = await store.setTargetMember(sproj.id, st.id, { source_id: ssrc.id, bank: 7 });
    return t.members.length === 1 && t.members[0].bank === 7;
  });

  await check('store.getProject voit la Cible et son Membre', async () => {
    const p2 = await store.getProject(sproj.id);
    const t = p2.targets.find((x) => x.id === st.id);
    return t && t.members.length === 1 && t.members[0].source_id === ssrc.id;
  });

  await check('store.removeTargetMember', async () => {
    const t = await store.removeTargetMember(sproj.id, st.id, ssrc.id);
    return t.members.length === 0;
  });

  await check('store.removeTarget', async () => {
    await store.removeTarget(sproj.id, st.id);
    const p2 = await store.getProject(sproj.id);
    return !p2.targets.some((x) => x.id === st.id);
  });

  // --- Cible implicite d'une Source seule (ADR 0002, Q1) : profile/ld_filename/
  // container ne sont plus des colonnes de `sources`, ils sont routes vers une
  // Cible+Membre crees a la premiere ecriture (server/api.mjs, applyTargetFields).
  await check('creer une Source avec profile/container/ld_filename : rendus tels quels', async () => {
    const s = await store.create({ name: 'implicit-src', code: '', profile: 'cpcplus', container: 'cpr', ld_filename: 'x.ld' });
    const got = await store.get(s.id);
    return got.profile === 'cpcplus' && got.container === 'cpr' && got.ld_filename === 'x.ld';
  });

  await check('modifier profile seul ne touche pas ld_filename, et reutilise la MEME Cible implicite', async () => {
    const s = await store.create({ name: 'implicit-src2', code: '', profile: 'cpc6128', container: 'sna', ld_filename: 'y.ld' });
    const before = await store.get(s.id);
    await store.update(s.id, { profile: 'cpcplus' });
    const after = await store.get(s.id);
    // Une seconde ecriture ne doit pas fabriquer un second Projet : au plus 1 au total pour ces deux Sources.
    const projs = await store.listProjects();
    const named = projs.filter((p) => p.name === 'implicit-src2');
    return after.profile === 'cpcplus' && after.container === 'sna' && after.ld_filename === 'y.ld'
      && before.ld_filename === 'y.ld' && named.length === 1;
  });

  await check('une librairie (is_include) ne fabrique jamais de Cible implicite', async () => {
    const s = await store.create({ name: 'implicit-lib', code: '', is_include: 1, filename: 'implicit-lib.asm',
      profile: null, container: null, ld_filename: null });
    const got = await store.get(s.id);
    const projs = await store.listProjects();
    return got.profile === null && !projs.some((p) => p.name === 'implicit-lib');
  });

  await check('changer seulement le nom ne fabrique pas de Cible', async () => {
    const s = await store.create({ name: 'no-target-yet', code: '' });
    await store.update(s.id, { name: 'renamed-no-target' });
    const got = await store.get(s.id);
    return got.profile === null && got.container === null && got.ld_filename === null;
  });

  console.log(`\n${pass}/${total} reussis`);
  process.exitCode = pass === total ? 0 : 1;
  cleanup();
}

function cleanup() {
  proc.kill();
  try { rmSync(dir, { recursive: true, force: true }); } catch {}
}

main();
