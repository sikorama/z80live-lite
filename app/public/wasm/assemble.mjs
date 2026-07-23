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
const BIN_EXT = ['sna', 'dsk', 'tap', 'bin']; // extensions binaires produites
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
  start: 'startPoint', end: 'endPoint', command: 'command' };

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

// Défaut réel côté serveur historique = rasm (pas sjasmplus).
export function resolveAssembler(opts = {}) {
  return opts.assembler || 'rasm';
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

// Récupère le binaire produit : chemin attendu, sinon 1er fichier binaire présent dans MEMFS /.
function readOutput(FS, expected) {
  try { return { data: FS.readFile(expected), ext: expected.split('.').pop() }; } catch {}
  try {
    for (const name of FS.readdir('/')) {
      const ext = name.split('.').pop().toLowerCase();
      if (BIN_EXT.includes(ext)) {
        try { return { data: FS.readFile('/' + name), ext }; } catch {}
      }
    }
  } catch {}
  return { data: null, ext: null };
}

async function runModule(factory, args, sourceText, expectedOut) {
  const log = [];
  let error = null;
  let Module;
  try {
    Module = await factory({ print: (s) => log.push(s), printErr: (s) => log.push(s), noExitRuntime: true });
  } catch (e) {
    return { log, data: null, ext: null, exitCode: -1, error: 'init WASM: ' + (e?.message || e) };
  }
  Module.FS.writeFile('/in.asm', sourceText);
  let exitCode = 0;
  try {
    Module.callMain(args);
  } catch (e) {
    // ExitStatus (exit() normal) porte .status ; un trap WASM (RuntimeError) non.
    if (typeof e?.status === 'number') exitCode = e.status;
    else { exitCode = -1; error = e?.message || String(e); }
  }
  const { data, ext } = readOutput(Module.FS, expectedOut);
  return { log, data, ext, exitCode, error };
}

export async function assemble(source, factories) {
  const raw = source || {};
  const code = stripAccents(raw.code || '');
  // Les directives `;z80:` en tête de source font autorité sur les opts fournis.
  const opts = { ...raw, ...parseDirectives(code) };
  const assembler = resolveAssembler(opts);

  if (assembler === 'sjasmplus') {
    const wrapped = wrapSjasm(code, opts, OUT + '.sna');
    const r = await runModule(factories.createSjasm, ['--nologo', '/in.asm'], wrapped, OUT + '.sna');
    const errs = /Errors:\s*(\d+)/.exec(r.log.join('\n'));
    const ok = !!r.data && (!errs || errs[1] === '0');
    return { ok, assembler, ext: r.ext, output: ok ? r.data : null, log: r.log, error: r.error, preprocessed: wrapped };
  }

  if (assembler === 'fantams') {
    const wrapped = wrapFantams(code, opts);
    const r = await runModule(factories.createFantams, ['/in.asm', '-o', OUT + '.sna'], wrapped, OUT + '.sna');
    const ok = r.exitCode === 0 && !!r.data;
    return { ok, assembler, ext: r.ext, output: ok ? r.data : null, log: r.log, error: r.error, preprocessed: wrapped };
  }

  // rasm (+ uz80 traité comme rasm en attendant)
  const wrapped = wrapRasm(code, opts);
  const r = await runModule(factories.createRasm, ['/in.asm', '-oa', '-eo', '-utf8', '-o', OUT], wrapped, OUT + '.sna');
  const ok = r.exitCode === 0 && !!r.data;
  return { ok, assembler, ext: r.ext, output: ok ? r.data : null, log: r.log, error: r.error, preprocessed: wrapped };
}
