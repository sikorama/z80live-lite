// Module d'assemblage isomorphe (Node + navigateur).
// Fait tourner rasm/sjasmplus en WASM et génère le header/footer selon les buildOptions.
// Les factories WASM sont injectées pour rester agnostique de l'environnement.
//
//   import { assemble } from './assemble.mjs';
//   import createRasm from './rasm.mjs';
//   import createSjasm from './sjasmplus.mjs';
//   const r = await assemble({ code, assembler:'rasm', buildmode:'sna', entryPoint:'#8000' },
//                            { createRasm, createSjasm });
//   // r = { ok, assembler, ext, output: Uint8Array|null, log: string[] }

const OUT = '/out';
const BIN_EXT = ['sna', 'dsk', 'tap', 'bin', 'cpr']; // extensions binaires produites
const DEFAULT_ORG = '#8000'; // défaut CPC courant (au lieu de #1000)

const hasRasmHeader = (c) => /\bbuildsna\b/i.test(c);
const hasSjHeader = (c) => /\b(device|savecpcsna|savesna|savetap|savebin)\b/i.test(c);
// fantams : en-tête minimal (org/run), pas de BUILDSNA.
const hasLiteOrg = (c) => /^\s*(?:[\w.$]+\s*:?\s+)?org\b/im.test(c);
const hasLiteRun = (c) => /^\s*run\b/im.test(c);

const cpcModel = (bm) => (bm === 'sna_cpc464' ? 'AMSTRADCPC464' : 'AMSTRADCPC6128');

// ---- Directives d'en-tête dans la source : `;z80: assembler=rasm buildmode=sna entry=#8000` ----
// La source devient auto-descriptive ; ces directives font autorité sur les buildOptions stockés.
const DIR_KEYS = { assembler: 'assembler', buildmode: 'buildmode', entry: 'entryPoint',
  start: 'startPoint', end: 'endPoint', command: 'command', base: 'base' };

// Chemin virtuel de la BASE dans le FS wasm (ADR 0012). L'identifiant de
// catalogue (`base=cpc6128-en`) est resolu ICI, par l'hote : fantams ne recoit
// qu'un chemin, jamais un id ni une URL.
const BASE_PATH = '/base.sna';

export function parseDirectives(code = '') {
  const out = {};
  for (const line of code.split('\n')) {
    const m = /^\s*;+\s*z80:\s*(.*)$/i.exec(line);
    if (m) {
      for (const kv of m[1].trim().split(/\s+/)) {
        const i = kv.indexOf('=');
        if (i > 0) { const k = DIR_KEYS[kv.slice(0, i).toLowerCase()]; if (k) out[k] = kv.slice(i + 1); }
      }
      continue;
    }
    if (line.trim() === '' || /^\s*;/.test(line)) continue; // on scanne le bloc de commentaires de tête
    break; // 1re ligne de code non-commentaire -> fin des directives
  }
  return out;
}

export function buildDirectiveLine(opts = {}) {
  const parts = [];
  for (const [short, full] of Object.entries(DIR_KEYS)) if (opts[full]) parts.push(`${short}=${opts[full]}`);
  return '; z80: ' + parts.join(' ');
}

// Insère/met à jour la ligne `;z80:` en tête (l'éditeur l'utilise pour maintenir la config).
export function upsertDirectives(code = '', opts = {}) {
  const line = buildDirectiveLine(opts);
  const lines = code.split('\n');
  const idx = lines.findIndex((l) => /^\s*;+\s*z80:/i.test(l));
  if (idx >= 0) lines[idx] = line; else lines.unshift(line);
  return lines.join('\n');
}

// Détecte le 1er `org <addr>` de la source pour un défaut d'entrée sensé.
function detectOrg(code) {
  const m = /^\s*(?:[\w.$]+\s*:?\s+)?org\s+(\$?#?%?[0-9a-fx]+)/im.exec(code);
  return m ? m[1] : null;
}

// Defaut = fantams. Une source qui n'a jamais choisi (« auto », 325 sur 425)
// passe donc par notre assembleur. Choix delibere : a ce stade fantams couvre
// ~52 % du corpus rasm au bit pres, et rendre les echecs VISIBLES dans
// l'editeur est justement ce qu'on veut — sinon il faut ouvrir chaque source,
// changer le reglage a la main et relancer pour savoir ou ca coince.
//
// Les sources qui epinglent explicitement rasm (76) ou sjasmplus (17) ne sont
// pas concernees. Pour revenir en arriere sur une source : reglage de l'editeur
// ou directive « ; z80: assembler=rasm » en tete. Globalement : cette ligne.
export function resolveAssembler(opts = {}) {
  return opts.assembler || 'fantams';
}

export function wrapRasm(code, opts = {}) {
  if (hasRasmHeader(code)) return code; // la source gère son propre BUILDSNA
  const start = opts.startPoint || opts.entryPoint || detectOrg(code) || DEFAULT_ORG;
  const entry = opts.entryPoint;
  const run = !entry || entry === 'none' || !String(entry).startsWith('#') ? 'RUN $' : `RUN ${entry}`;
  return `BUILDSNA V2 : BANKSET 0 : ORG ${start} : ${run}\n${code}`;
}

// fantams : préprocesseur intégré + assembleur 2 passes ; l'en-tête utilise
// la syntaxe lite (`org`/`run`), pas les directives rasm (`BUILDSNA`). Le CLI
// écrit un .sna quand le -o se termine par .sna (PC = adresse RUN).
export function wrapFantams(code, opts = {}) {
  // Un script de lien (-T) place les sections lui-meme (ADR 0030 fantams) : une source
  // banquee n'a par construction AUCUN `org`, et lui en injecter un casserait ce que le
  // linker doit calculer. On rend le texte tel quel.
  if (opts.ldFile) return code;
  const start = opts.startPoint || opts.entryPoint || detectOrg(code) || DEFAULT_ORG;
  const entry = opts.entryPoint;
  const run = !entry || entry === 'none' || !String(entry).startsWith('#') ? null : entry;
  const head = [];
  if (!hasLiteOrg(code)) head.push(`org ${start}`);
  if (!hasLiteRun(code) && run) head.push(`run ${run}`);
  return head.length ? head.join('\n') + '\n' + code : code;
}

export function wrapSjasm(code, opts = {}, outPath = OUT + '.sna') {
  if (hasSjHeader(code)) return code; // la source gère DEVICE/SAVE elle-même
  const start = opts.startPoint || opts.entryPoint || detectOrg(code) || DEFAULT_ORG;
  const entry = opts.entryPoint || start;
  return `  DEVICE ${cpcModel(opts.buildmode)}\n  org ${start}\n${code}\n  SAVECPCSNA "${outPath}", ${entry}\n`;
}

// Nettoie les accents comme le faisait le serveur (rasm -utf8 gère l'UTF-8, mais on reste prudent).
const stripAccents = (s) => s.normalize('NFD').replace(/[̀-ͯ]/g, '');

// Nombre de lignes injectées par le wrapper avant le code utilisateur (en-tête invisible dans
// l'éditeur). Sert à recaler les numéros de ligne rapportés par l'assembleur sur la source affichée.
function headerLineCount(wrapped, code) {
  const idx = wrapped.indexOf(code);
  if (idx < 0) return 0;
  return wrapped.slice(0, idx).split('\n').length - 1;
}

// Récupère le binaire produit : chemin attendu, sinon 1er fichier binaire présent dans MEMFS /.
// `exclude` : les binaires qu'on a nous-mêmes injectés (la base). Sans cette
// exclusion, un assemblage qui échoue sans rien produire renverrait la BASE
// comme s'il s'agissait du snapshot assemblé — un build rouge qui s'exécute.
function readOutput(FS, expected, exclude = []) {
  try { return { data: FS.readFile(expected), ext: expected.split('.').pop() }; } catch {}
  try {
    for (const name of FS.readdir('/')) {
      if (exclude.includes('/' + name)) continue;
      const ext = name.split('.').pop().toLowerCase();
      if (BIN_EXT.includes(ext)) {
        try { return { data: FS.readFile('/' + name), ext }; } catch {}
      }
    }
  } catch {}
  return { data: null, ext: null };
}

// Nom/chemin virtuel sous lequel une lib (is_include=1) est injectée dans le FS wasm :
// `filename` fait autorité (ex. 'lib/toolbox.asm'), sinon slug(name)+'.asm' par défaut.
// Exportée : c'est aussi la clé qui permet à l'hôte de retrouver la SOURCE derrière le
// chemin qu'un assembleur cite dans un diagnostic.
export function includePath(inc) {
  let p = String(inc.filename || inc.name || 'lib').trim().replace(/^\/+/, '');
  if (!/\.[A-Za-z0-9]+$/.test(p)) p += '.asm';
  return '/' + p;
}

// Copie les sources marquées "include" (librairies, sans point d'entrée) dans le FS virtuel
// wasm avant l'assemblage, pour que les directives INCLUDE/READ/INCBIN du fichier principal résolvent.
function writeIncludes(FS, includes = []) {
  for (const inc of includes) {
    const path = includePath(inc);
    const dir = path.slice(0, path.lastIndexOf('/'));
    if (dir) { try { FS.mkdirTree(dir); } catch {} }
    try { FS.writeFile(path, inc.code || ''); } catch {}
  }
}

// `dump` (optionnel) : { args, path } — une SECONDE invocation dans la MÊME
// instance WASM, dont le fichier produit est renvoyé en texte. Sert à obtenir la
// source déroulée (fantams -E) sans réinstancier le module, l'instanciation
// étant de loin la partie coûteuse.
async function runModule(factory, args, sourceText, expectedOut, includes, dump, extraFiles = []) {
  const log = [];
  let error = null;
  let Module;
  try {
    Module = await factory({ print: (s) => log.push(s), printErr: (s) => log.push(s), noExitRuntime: true });
  } catch (e) {
    return { log, data: null, ext: null, exitCode: -1, error: 'init WASM: ' + (e?.message || e) };
  }
  writeIncludes(Module.FS, includes);
  for (const f of extraFiles) Module.FS.writeFile(f.path, f.data);   // binaire : Uint8Array
  Module.FS.writeFile('/in.asm', sourceText);
  let exitCode = 0;
  try {
    Module.callMain(args);
  } catch (e) {
    // ExitStatus (exit() normal) porte .status ; un trap WASM (RuntimeError) non.
    if (typeof e?.status === 'number') exitCode = e.status;
    else { exitCode = -1; error = e?.message || String(e); }
  }
  const { data, ext } = readOutput(Module.FS, expectedOut, extraFiles.map((f) => f.path));

  let dumped = null;
  if (dump) {
    try {
      Module.callMain(dump.args);
    } catch (e) {
      if (typeof e?.status !== 'number') dumped = null; // trap : on renonce, sans masquer l'assemblage
    }
    try { dumped = new TextDecoder().decode(Module.FS.readFile(dump.path)); } catch { dumped = null; }
  }

  return { log, data, ext, exitCode, error, dumped };
}

// ---- La version de fantams ----
// « fantams --version » rend deux dates accolees : celle, portee dans l'arbre
// des sources, de ce qui a ete VOULU comme livraison, et celle, prise a
// __DATE__, de la production de CET artefact-la. C'est l'ECART entre les deux
// qui repond a « cet artefact est-il a jour ».
//
// L'option vit sur la surface argv, comme toute autre invocation : cet
// adaptateur n'a rien a exporter de plus pour la servir.
//
// La chaine est rendue TELLE QUELLE. Rien ne la decoupe, rien ne la compare,
// aucun ordre entre versions n'est defini — un changement de sa forme ne doit
// rien casser. L'hote l'affiche, un humain la lit.
export async function fantamsVersion(factories) {
  const out = [];
  try {
    const Module = await factories.createFantams(
      { print: (s) => out.push(s), printErr: (s) => out.push(s), noExitRuntime: true });
    try { Module.callMain(['--version']); } catch (e) { if (typeof e?.status !== 'number') throw e; }
  } catch (e) {
    return { ok: false, version: null, error: e?.message || String(e) };
  }
  const version = out.join('\n').trim();
  // Un artefact d'avant l'option repond « file not found: --version » : on ne
  // l'interprete pas, on constate seulement qu'il n'a rien rendu d'utile.
  return version ? { ok: true, version, error: null }
                 : { ok: false, version: null, error: 'aucune version rendue' };
}

// ---- Mise en forme du source (ADR 0013) ----
// Appelle « fantams --beautify » : rien n'est deroule, le tampon de l'editeur
// garde ses macros, ses includes et sa ligne « ; z80: ».
//
// Les includes sont ecrits dans le FS virtuel comme pour assemble() : fantams y
// fait tourner son preprocesseur pour en tirer la table des macros, seule facon
// de savoir qu'un « fill_screen » nu est un APPEL et non un label. Sans eux, une
// macro de bibliotheque redeviendrait indiscernable d'un label.
//
// Deux differences deliberees avec assemble() : aucun en-tete n'est injecte (on
// rend le texte de l'AUTEUR, pas celui qu'on fabrique autour), et les accents ne
// sont PAS retires — stripAccents protege des assembleurs tiers, mais l'appliquer
// ici reecrirait les commentaires de l'auteur sous couvert de mise en forme.
//
// Rend { ok, code, log, error }. Sur echec, `code` est null : rien n'est
// remplace a moitie.
export async function beautifySource(code = '', factories, includes = []) {
  const r = await runModule(factories.createFantams,
    ['/in.asm', '--beautify', '-o', '/out.fmt'], code, '/out.fmt', includes);
  let text = null;
  try { text = r.data ? new TextDecoder().decode(r.data) : null; } catch { text = null; }
  const ok = r.exitCode === 0 && text !== null;
  return { ok, code: ok ? text : null, log: r.log, error: r.error };
}

// ---- La Fermeture (z80live CONTEXT.md, ADR 0002) ----
// `fantams --list-files` imprime le fichier principal, plus récursivement
// chaque INCLUDE touché, sur stdout — une erreur de préprocesseur va sur
// stderr. Les deux sont capturés SÉPARÉMENT (à la différence de `runModule`,
// qui les mélange dans un seul `log`) : un avertissement ne doit jamais
// pouvoir se glisser dans la Fermeture.
export async function listFiles(code = '', factories, includes = []) {
  const out = [], errLog = [];
  let Module;
  try {
    Module = await factories.createFantams(
      { print: (s) => out.push(s), printErr: (s) => errLog.push(s), noExitRuntime: true });
  } catch (e) {
    return { ok: false, files: [], log: [], error: 'init WASM: ' + (e?.message || e) };
  }
  writeIncludes(Module.FS, includes);
  Module.FS.writeFile('/in.asm', code);
  let exitCode = 0, error = null;
  try {
    Module.callMain(['/in.asm', '--list-files']);
  } catch (e) {
    if (typeof e?.status === 'number') exitCode = e.status;
    else { exitCode = -1; error = e?.message || String(e); }
  }
  const ok = exitCode === 0;
  return { ok, files: ok ? out.filter(Boolean) : [], log: errLog, error };
}

export async function assemble(source, factories) {
  const raw = source || {};
  const code = stripAccents(raw.code || '');
  // Les directives `;z80:` en tête de source font autorité sur les opts fournis.
  const opts = { ...raw, ...parseDirectives(code) };
  let baseLog = null;
  const assembler = resolveAssembler(opts);
  const includes = raw.includes || [];

  // `base=none` annule une base heritee d'une couche inferieure : la valeur vide
  // etant indistinguable de l'absence dans buildDirectiveLine, il faut un mot.
  const baseId = opts.base && opts.base !== 'none' ? String(opts.base) : null;
  const baseFiles = [];
  const baseArgs = [];
  if (baseId && assembler !== 'fantams') {
    // La base vient d'une couche inferieure, l'assembleur est fixe par-dessus :
    // on l'ecarte, mais en le disant (ADR 0012).
    baseLog = `base « ${baseId} » ecartee : ${assembler} ne sait pas la poser`;
  } else if (baseId) {
    const bytes = raw.resolveBase ? await raw.resolveBase(baseId) : null;
    if (!bytes) {
      // Aucun repli silencieux sur des zeros : ca produirait un .sna qui demarre
      // et plante au premier appel firmware.
      return { ok: false, assembler, ext: null, output: null,
               log: [`base « ${baseId} » introuvable — aucun repli sur des zeros`],
               error: 'base introuvable', preprocessed: code, lineOffset: 0 };
    }
    baseFiles.push({ path: BASE_PATH, data: bytes });
    baseArgs.push('--base', BASE_PATH);
  }

  if (assembler === 'sjasmplus') {
    const wrapped = wrapSjasm(code, opts, OUT + '.sna');
    const r = await runModule(factories.createSjasm, ['--nologo', '/in.asm'], wrapped, OUT + '.sna', includes);
    const errs = /Errors:\s*(\d+)/.exec(r.log.join('\n'));
    const ok = !!r.data && (!errs || errs[1] === '0');
    return { ok, assembler, ext: r.ext, output: ok ? r.data : null,
             log: baseLog ? [baseLog, ...r.log] : r.log, error: r.error,
             preprocessed: wrapped, lineOffset: headerLineCount(wrapped, code) };
  }

  if (assembler === 'fantams') {
    // Conteneur (CONTEXT.md/fantams) : 'sna' et 'cpr' sont livres (dsk/cdt restent hors
    // perimetre, docs/spec-etage-c1.md). fantams n'ecrit qu'un blob brut sous le nom qu'on
    // lui donne, sans valider le format visé : lui laisser passer un ".dsk" rendrait un
    // fichier qui SE FAIT PASSER pour une image DSK sans en etre une. On refuse tot.
    const container = opts.container || 'sna';
    // `.cpr` : UNE banque physique (0..31) par appel (fantams/cpr.h — chaque banque est
    // liee SEPAREMENT). `opts.cprBank` la nomme ; rien d'autre ne le sait (le script -T
    // choisit l'axe/etat, l'image liee ne porte pas l'id physique — voir cpr.cpp).
    const cprBank = Number(opts.cprBank);
    if (container === 'cpr' && !(Number.isInteger(cprBank) && cprBank >= 0 && cprBank <= 31)) {
      return { ok: false, assembler, ext: null, output: null,
               log: ['cpr : --cpr-bank manquant ou hors de 0..31'],
               error: 'cpr-bank invalide', preprocessed: code, lineOffset: 0 };
    }
    if (container !== 'sna' && container !== 'cpr') {
      return { ok: false, assembler, ext: null, output: null,
               log: [`conteneur "${container}" pas encore livre par fantams (sna/cpr actifs)`],
               error: 'conteneur non supporte', preprocessed: code, lineOffset: 0 };
    }
    const wrapped = wrapFantams(code, opts);
    const outPath = OUT + '.' + container;
    const args = ['/in.asm', '-o', outPath, ...baseArgs];
    // Profil (--target) et script de lien (-T) : reglages de Projet (CONTEXT.md), jamais
    // reflete dans la directive ;z80: (docs/adr/0001-...). Le .ld lui-meme est deja ecrit
    // dans le FS wasm comme n'importe quel include ; on ne fait ici que le DESIGNER a fantams.
    if (opts.profile) args.push('--target', opts.profile);
    if (opts.ldFile) args.push('-T', includePath({ filename: opts.ldFile }));
    if (container === 'cpr') args.push('--cpr-bank', String(cprBank));
    // `opts.prevCpr` : le .cpr deja accumule par les banques precedentes (un Projet CPR,
    // App.svelte) — pre-seme au chemin de sortie, fantams (asm_main.cpp) le relit, y AJOUTE
    // cette banque et le reecrit. Absent (premier appel), fantams part d'un conteneur vide.
    const extraFiles = container === 'cpr' && opts.prevCpr
      ? [...baseFiles, { path: outPath, data: opts.prevCpr }]
      : baseFiles;
    // fantams a un vrai preprocesseur : `preprocessed` porte la SOURCE DEROULEE
    // (macros expansees, boucles deroulees, includes inseres), pas la source
    // d'entree. Pour rasm et sjasmplus, faute d'equivalent, elle reste l'entree.
    const r = await runModule(
      factories.createFantams, args, wrapped, outPath, includes,
      { args: ['/in.asm', '-E', '-o', '/out.pp'], path: '/out.pp' }, extraFiles);
    const ok = r.exitCode === 0 && !!r.data;
    return { ok, assembler, ext: r.ext, output: ok ? r.data : null,
             log: baseLog ? [baseLog, ...r.log] : r.log, error: r.error,
             preprocessed: r.dumped ?? wrapped, lineOffset: headerLineCount(wrapped, code) };
  }

  // rasm (+ uz80 traité comme rasm en attendant)
  const wrapped = wrapRasm(code, opts);
  const r = await runModule(factories.createRasm, ['/in.asm', '-oa', '-eo', '-utf8', '-o', OUT], wrapped, OUT + '.sna', includes);
  const ok = r.exitCode === 0 && !!r.data;
  return { ok, assembler, ext: r.ext, output: ok ? r.data : null,
           log: baseLog ? [baseLog, ...r.log] : r.log, error: r.error,
           preprocessed: wrapped, lineOffset: headerLineCount(wrapped, code) };
}
