// Test d'intégration : fantams WASM via wasm/assemble.mjs (Node).
//   node test-wasm.mjs
import createFantams from '../wasm/fantams.mjs';
import { assemble, beautifySource, fantamsVersion } from '../wasm/assemble.mjs';

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
  total++;
  const src2 = 'cls MACRO c\nld a,{c}\nMEND\nsprite 4,12\n';
  const r2 = await beautifySource(src2, { createFantams });
  const want2 = 'cls MACRO c\n    ld a,{c}\n    MEND\nsprite 4,12\n';
  if (r2.ok && r2.code === want2) { pass++; console.log('[OK] mise en forme : refus de deviner'); }
  else console.log('[FAIL] refus de deviner —', JSON.stringify(r2.code));

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
