// Test d'intégration : fantams WASM via wasm/assemble.mjs (Node).
//   node test-wasm.mjs
import createFantams from '../wasm/fantams.mjs';
import { assemble, beautifySource, fantamsVersion, listFiles } from '../wasm/assemble.mjs';

const SNA_MAGIC = 'MV - SNA';

async function run(name, code, opts = {}) {
  const r = await assemble({ code, assembler: 'fantams', ...opts }, { createFantams });
  const magic = r.output ? Buffer.from(r.output.slice(0, 8)).toString('latin1') : '';
  const okSna = r.ok && magic === SNA_MAGIC;
  console.log(`[${okSna ? 'OK' : 'FAIL'}] ${name} — ${r.output?.length ?? 0} o, ext=${r.ext}, magic="${magic}"`);
  if (!r.ok || !okSna) {
    console.log('   preprocessed:', JSON.stringify(r.preprocessed));
    console.log('   log:', r.log.join(' | '));
    if (r.error) console.log('   error:', r.error);
  }
  return okSna;
}

let pass = 0, total = 0;
const check = async (...a) => { total++; if (await run(...a)) pass++; };

// 1) source minimale : header org/run injecté
await check('minimal', 'ld a,#ff\nret\n', { entryPoint: '#8000' });

// 2) exemple démo (macros, REPEAT, IF, scope) — org/run déjà présents
const demo = `LET COUNT = 4
        org 0x8000
        run start
MACRO WAIT n
@wloop: dec {n}
       jr nz,@wloop
ENDM
start:
        ld b,{COUNT}
        WAIT b
        WAIT b
        ret
`;
await check('démo macro/repeat', demo);

// 3) erreur attendue : mnémonique invalide -> ok=false
total++;
{
  const r = await assemble({ code: 'zorglub xyz\n', assembler: 'fantams' }, { createFantams });
  if (!r.ok) { pass++; console.log('[OK] erreur détectée —', r.log.join(' | ') || r.error); }
  else console.log('[FAIL] erreur non détectée (ok=true)');
}

// 4) base (ADR 0012) : hors des octets assembles, la memoire vient de la base
{
  // base synthetique : v2, dump plat de 64K rempli de 0xAA, ROM basse activee
  const h = Buffer.alloc(256); h.write('MV - SNA', 0, 'latin1');
  h[0x10] = 2; h[0x21] = 0xF0; h[0x22] = 0xBF; h[0x40] = 0x84; h[0x6B] = 64; h[0x6D] = 2;
  const mem = Buffer.alloc(65536, 0xAA); mem[0xBD00] = 0x55;
  const fakeBase = new Uint8Array(Buffer.concat([h, mem]));
  const resolveBase = async (id) => (id === 'test-6128' ? fakeBase : null);

  total++;
  const r = await assemble({ code: '  org #8000\n  call #BB5A\n  ret\n', assembler: 'fantams',
                             base: 'test-6128', resolveBase }, { createFantams });
  const out = r.output;
  const good = r.ok && out && out.length === 65792
    && out[0x40] === 0x84                    // en-tete de la base
    && out[0x21] === 0xF0 && out[0x22] === 0xBF   // SP de la base
    && out[0x23] === 0x00 && out[0x24] === 0x80   // PC patche
    && out[256 + 0x4000] === 0xAA            // hors coverage : la base
    && out[256 + 0xBD00] === 0x55            // vecteur firmware preserve
    && out[256 + 0x8000] === 0xCD;           // le code assemble gagne
  if (good) { pass++; console.log('[OK] base posee — log:', r.log.filter((l) => l.startsWith('base')).join('')); }
  else console.log('[FAIL] base —', r.log.join(' | '), r.error ?? '');

  // 5) aucun repli silencieux quand la base est introuvable
  total++;
  const r2 = await assemble({ code: '  ret\n', assembler: 'fantams', base: 'inconnue', resolveBase },
                            { createFantams });
  if (!r2.ok && !r2.output) { pass++; console.log('[OK] base introuvable refusee —', r2.log.join(' | ')); }
  else console.log('[FAIL] base introuvable : un artefact a ete produit quand meme');

  // 6) base=none : pas de base, et surtout pas d'erreur
  total++;
  const r3 = await assemble({ code: '  org #8000\n  ret\n', assembler: 'fantams', base: 'none', resolveBase },
                            { createFantams });
  if (r3.ok && r3.output && r3.output[256 + 0x4000] === 0x00) { pass++; console.log('[OK] base=none -> memoire a zero'); }
  else console.log('[FAIL] base=none —', r3.log.join(' | '));
}

// 7) mise en forme (ADR 0013) : le bouton « Mettre en forme » de l'editeur.
{
  const src = 'start\nld a,1\n\tjp start\n';
  total++;
  const r = await beautifySource(src, { createFantams });
  const want = 'start:\n    ld a,1\n    jp start\n';
  if (r.ok && r.code === want) { pass++; console.log('[OK] mise en forme'); }
  else console.log('[FAIL] mise en forme —', JSON.stringify(r.code), r.log?.join(' | '), r.error ?? '');

  // Le refus de deviner : un appel de macro n'est pas un label. Et les mots-cles
  // du preprocesseur ne recoivent jamais de deux-points.
  //
  // Deux choses ont change pendant les etages A/B/C1, et l'attente d'ici datait
  // d'avant :
  //   - « nom MACRO params » est une graphie HERITEE, que fantams reecrit en
  //     « MACRO nom params » (beautify_test.cpp : « ce n'est pas un label, et
  //     elle est reecrite ») ;
  //   - un appel de macro est indente comme du code, faute de quoi il aurait la
  //     colonne 1, qui est la place d'un label — deviner dans l'autre sens.
  // La propriete que ce cas garde, elle, n'a pas bouge : AUCUN deux-points n'est
  // ajoute. On l'affirme separement, pour que la regle survive au prochain
  // changement de mise en page.
  total++;
  const src2 = 'cls MACRO c\nld a,{c}\nMEND\nsprite 4,12\n';
  const r2 = await beautifySource(src2, { createFantams });
  const want2 = '    MACRO cls c\n        ld a,{c}\n    MEND\n    sprite 4,12\n';
  const aucunDeuxPoints = r2.ok && !r2.code.includes(':');
  if (r2.ok && r2.code === want2 && aucunDeuxPoints) { pass++; console.log('[OK] mise en forme : refus de deviner'); }
  else console.log('[FAIL] refus de deviner —', JSON.stringify(r2.code), aucunDeuxPoints ? '' : '(un ":" a ete ajoute)');

  // Idempotence de bout en bout.
  total++;
  const r3 = await beautifySource(want, { createFantams });
  if (r3.ok && r3.code === want) { pass++; console.log('[OK] mise en forme idempotente'); }
  else console.log('[FAIL] idempotence —', JSON.stringify(r3.code));

  // La source deroulee sort mise en forme : plus une seule ligne en colonne 1
  // hors label, plus une seule tabulation d'indentation.
  total++;
  const r4 = await assemble({ code: 'start\nld a,1\n\tjp start\n', assembler: 'fantams', entryPoint: '#8000' },
                            { createFantams });
  const pre = r4.preprocessed || '';
  const bad = pre.split('\n').filter((l) => /^\t/.test(l) || /^(ld|jp|nop|ret|call)\b/i.test(l));
  if (r4.ok && pre.includes('start:') && bad.length === 0) { pass++; console.log('[OK] source deroulee mise en forme'); }
  else console.log('[FAIL] source deroulee —', JSON.stringify(pre), bad);
}

// 9) profil (--target) : cpc6128 et cpcplus sont tous deux des builtins valides.
for (const profile of ['cpc6128', 'cpcplus']) {
  total++;
  const r = await assemble({ code: 'ld a,#ff\nret\n', assembler: 'fantams', entryPoint: '#8000', profile },
                            { createFantams });
  const magic = r.output ? Buffer.from(r.output.slice(0, 8)).toString('latin1') : '';
  if (r.ok && magic === SNA_MAGIC) { pass++; console.log(`[OK] profil ${profile}`); }
  else console.log(`[FAIL] profil ${profile} —`, r.log.join(' | '), r.error ?? '');
}
// Un profil qui n'existe pas doit remonter comme erreur : la seule façon de le
// constater est que --target soit réellement transmis au CLI.
{
  total++;
  const r = await assemble({ code: 'ld a,#ff\nret\n', assembler: 'fantams', entryPoint: '#8000', profile: 'doesnotexist' },
                            { createFantams });
  if (!r.ok) { pass++; console.log('[OK] profil inconnu refusé —', r.log.join(' | ') || r.error); }
  else console.log('[FAIL] profil inconnu accepté (ok=true) — --target ne semble pas transmis');
}

// 10) script de lien (-T) : une source SANS org (le placement est delegue au linker,
// ADR 0030 fantams), le .ld fourni comme un include ('ld_filename', CONTEXT.md).
{
  const code = '        SECTION main, "ro"\n        run    start\nstart:\n        ld     a,#ff\n        ret\n';
  const ld = 'TARGET cpc6128\nMEMORY_MAP {\n    CONFIG linear {\n        w1 { SECTION main }\n    }\n}\n';
  total++;
  const r = await assemble(
    { code, assembler: 'fantams', ldFile: 'min.ld', includes: [{ filename: 'min.ld', code: ld }] },
    { createFantams });
  const magic = r.output ? Buffer.from(r.output.slice(0, 8)).toString('latin1') : '';
  if (r.ok && magic === SNA_MAGIC) { pass++; console.log('[OK] script de lien (-T)'); }
  else console.log('[FAIL] script de lien (-T) —', r.log.join(' | '), r.error ?? '');

  // Garde de non-regression : SANS `ldFile`, l'en-tete org/run habituel reste injecte (la
  // branche « le linker place tout » de wrapFantams ne doit s'appliquer qu'avec -T).
  total++;
  const r2 = await assemble({ code, assembler: 'fantams', entryPoint: '#8000' }, { createFantams });
  if (r2.ok && /^\s*org\b/im.test(r2.preprocessed || '')) { pass++; console.log('[OK] sans -T : org toujours injecte'); }
  else console.log('[FAIL] sans -T : org non injecte —', JSON.stringify(r2.preprocessed));
}

// 11) conteneur : l'extension de sortie suit `container` (par defaut 'sna', seul actif).
{
  total++;
  const r = await assemble({ code: 'ld a,#ff\nret\n', assembler: 'fantams', entryPoint: '#8000', container: 'sna' },
                            { createFantams });
  if (r.ok && r.ext === 'sna') { pass++; console.log('[OK] conteneur sna explicite'); }
  else console.log('[FAIL] conteneur sna explicite —', r.log.join(' | '), r.error ?? '');

  // Un conteneur pas encore implémenté côté fantams (docs/spec-etage-c1.md) ne doit PAS
  // silencieusement retomber sur .sna : la seule façon de le constater est que la sortie
  // demandée (-o out.dsk) suive vraiment `container`.
  total++;
  const r2 = await assemble({ code: 'ld a,#ff\nret\n', assembler: 'fantams', entryPoint: '#8000', container: 'dsk' },
                            { createFantams });
  if (!r2.ok) { pass++; console.log('[OK] conteneur dsk (pas encore livré) — aucun repli silencieux sur .sna'); }
  else console.log('[FAIL] conteneur dsk aurait dû échouer (pas encore implémenté) ou a produit un .sna malgré tout');
}

// 12) conteneur cpr : une banque physique par appel (fantams/cpr.h), accumulee via
// `prevCpr` — c'est le meme geste que le Projet CPR d'App.svelte fera banque par banque.
{
  const RIFF_AMS = 'RIFF';
  const cartAsm = (n) => `        SECTION cart, "ro"\ncart:\n        db ${n}, ${255 - n}\n`;
  const ldFor = (window, axis) => `TARGET cpcplus\nMEMORY_MAP { CONFIG ${axis} { ${window} { SECTION cart } } }\n`;

  // banque 0 (cart_rom, RMR2, 0..7) : premier appel, aucun conteneur existant.
  total++;
  const r0 = await assemble({
    code: cartAsm(0), assembler: 'fantams', profile: 'cpcplus', container: 'cpr', cprBank: 0,
    ldFile: 'b0.ld', includes: [{ filename: 'b0.ld', code: ldFor('w0', 'cart_rom.w0<0>') }],
  }, { createFantams });
  const magic0 = r0.output ? Buffer.from(r0.output.slice(0, 4)).toString('latin1') : '';
  if (r0.ok && r0.ext === 'cpr' && magic0 === RIFF_AMS && r0.output.length === 8 + 4 + 8 + 16384) {
    pass++; console.log('[OK] cpr banque 0 (nouveau conteneur)');
  } else console.log('[FAIL] cpr banque 0 —', r0.log.join(' | '), r0.error ?? '', r0.output?.length);

  // banque 19 (cart_rom_hi, Upper ROM, 8..31) : AJOUTEE au conteneur precedent.
  total++;
  const r1 = await assemble({
    code: cartAsm(19), assembler: 'fantams', profile: 'cpcplus', container: 'cpr', cprBank: 19,
    ldFile: 'b19.ld', includes: [{ filename: 'b19.ld', code: ldFor('w3', 'cart_rom_hi.on<19>') }],
    prevCpr: r0.output,
  }, { createFantams });
  const magic1 = r1.output ? Buffer.from(r1.output.slice(0, 4)).toString('latin1') : '';
  const wantLen = 8 + 4 + 2 * (8 + 16384);
  if (r1.ok && r1.ext === 'cpr' && magic1 === RIFF_AMS && r1.output.length === wantLen) {
    pass++; console.log('[OK] cpr banque 19 ajoutee (2 chunks, banque 0 conservee)');
  } else console.log('[FAIL] cpr banque 19 —', r1.log.join(' | '), r1.error ?? '', r1.output?.length, 'attendu', wantLen);

  // --cpr-bank absent ou hors bornes : refuse, sans repli silencieux sur sna.
  total++;
  const r2 = await assemble({ code: cartAsm(0), assembler: 'fantams', profile: 'cpcplus', container: 'cpr' },
                             { createFantams });
  if (!r2.ok) { pass++; console.log('[OK] cpr sans --cpr-bank refuse —', r2.error); }
  else console.log('[FAIL] cpr sans --cpr-bank aurait du echouer');

  total++;
  const r3 = await assemble({ code: cartAsm(0), assembler: 'fantams', profile: 'cpcplus', container: 'cpr', cprBank: 99 },
                             { createFantams });
  if (!r3.ok) { pass++; console.log('[OK] cpr-bank hors 0..31 refuse —', r3.error); }
  else console.log('[FAIL] cpr-bank 99 aurait du echouer');
}

// 13) listFiles() : la Fermeture (ADR 0002) via le pont WASM — le meme
// pp::Result::files() que la CLI (--list-files), mais depuis JS, includes
// injectes dans le FS virtuel comme pour assemble().
{
  total++;
  const r = await listFiles('  nop\n  INCLUDE "lib.asm"\n', { createFantams },
    [{ filename: 'lib.asm', code: '  inc a\n' }]);
  // "lib.asm" telle quelle : la Fermeture enregistre le chemin LITTERAL de
  // l'INCLUDE, pas le chemin (prefixe '/') sous lequel includePath() l'ecrit
  // dans le FS virtuel — ils coincident par construction du CWD MEMFS ("/"),
  // pas par une reecriture du chemin.
  const want = ['/in.asm', 'lib.asm'];
  if (r.ok && JSON.stringify(r.files) === JSON.stringify(want)) {
    pass++; console.log('[OK] listFiles : principal + include');
  } else console.log('[FAIL] listFiles : principal + include —', JSON.stringify(r), r.log?.join(' | '));

  // Un avertissement de preprocesseur ne doit JAMAIS polluer la liste — c'est
  // tout l'objet de ne pas reutiliser runModule (qui melange stdout/stderr).
  total++;
  const r2 = await listFiles('zorglub\n  nop\n', { createFantams });
  if (r2.ok && r2.files.every((f) => f === '/in.asm')) {
    pass++; console.log('[OK] listFiles : un avertissement ne pollue pas la liste');
  } else console.log('[FAIL] listFiles : avertissement mele a la liste —', JSON.stringify(r2));

  // Sans aucun include, juste le fichier principal.
  total++;
  const r3 = await listFiles('  ld a,1\n  ret\n', { createFantams });
  if (r3.ok && JSON.stringify(r3.files) === JSON.stringify(['/in.asm'])) {
    pass++; console.log('[OK] listFiles : sans include');
  } else console.log('[FAIL] listFiles : sans include —', JSON.stringify(r3));

  // Un include manquant : refuse, liste vide — jamais une Fermeture partielle.
  total++;
  const r4 = await listFiles('  INCLUDE "absent.asm"\n', { createFantams });
  if (!r4.ok && r4.files.length === 0) {
    pass++; console.log('[OK] listFiles : include manquant refuse, liste vide');
  } else console.log('[FAIL] listFiles : include manquant —', JSON.stringify(r4));
}

// 8) la version : l'artefact sait dire qui il est.
// On verifie la FORME, jamais le contenu — aucun ordre entre versions n'est
// defini, et la chaine est faite pour etre lue par un humain. Ce que ce cas
// attrape, c'est un artefact d'avant l'option : il ne rend rien d'utile.
{
  total++;
  const v = await fantamsVersion({ createFantams });
  if (v.ok && /^fantams \d{4}-\d{2}-\d{2} \(compile \d{4}-\d{2}-\d{2}\)$/.test(v.version)) {
    pass++; console.log(`[OK] version — ${v.version}`);
  } else console.log('[FAIL] version —', JSON.stringify(v));
}

console.log(`\n${pass}/${total} réussis`);
process.exit(pass === total ? 0 : 1);
