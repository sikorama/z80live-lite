// Test d'intégration : fantams WASM via wasm/assemble.mjs (Node).
//   node test-wasm.mjs
import createFantams from '../wasm/fantams.mjs';
import { assemble } from '../wasm/assemble.mjs';

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

console.log(`\n${pass}/${total} réussis`);
process.exit(pass === total ? 0 : 1);
