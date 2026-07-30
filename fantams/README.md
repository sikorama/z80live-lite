# fantams — assembleur Z80 léger (source WASM)

Initiallement un fork simplifié de rasm, compilé vers WebAssembly pour tourner côté client (comme `wasm/rasm.*` et `wasm/sjasmplus.*`).
A évolué vers un périmètre éloigné, puisque l'assembleur est en 2 passes, et focalise sur l'export SNA. Il est doté d'un **préprocesseur complet** (macros, `REPEAT`/`WHILE`, `IF`, `STRUCT`, scope auto-local, includes)

## Structure

| Lib | Rôle |
|---|---|
| `z80` (`z80.cpp`) | encodeur d'instructions Z80 |
| `pp` + `expr` (`pp.cpp`, `expr.cpp`) | préprocesseur texte→texte + évaluateur d'expressions |
| `parser` (`parser.cpp`) | ligne texte → instruction |
| `asm` (`asm.cpp`) | assembleur 2 passes (ORG, symboles, refs avant) |
| `sna` (`sna.cpp`) | export snapshot CPC `.sna` |

Outils : `rasmlite` (`asm_main.cpp`, `.asm → .bin/.sna`) et `ppdump`
(`pp_main.cpp`, équivalent `-E` : export de la source préprocessée).

## Tests natifs

```bash
make test        # 226 tests (z80 · pp · parser · asm · sna)
```

## Build WASM

Passe par l'image `emscripten/emsdk` (podman/docker) — pas besoin d'emcc local :

```bash
./build-wasm.sh          # -> ../wasm/rasmlite.mjs + rasmlite.wasm
node test-wasm.mjs       # test d'intégration via wasm/assemble.mjs
```

Flags notables : 
 * `-fexceptions` - sans lui, tout `throw` devient `abort()` en WASM,
 * `-sSTACK_SIZE=8388608` (parseur récursif), 

## Intégration app

`wasm/assemble.mjs` expose l'assembleur via `assembler: 'rasmlite'` (ou la directive
`;z80: assembler=rasmlite` en tête de source). `wrapRasmLite` injecte un en-tête
`org`/`run` (syntaxe lite, **pas** `BUILDSNA`) si absent. Sortie `.sna` byte-identique
au binaire natif.

> Piste différée (jugée trop complexe pour l'instant) : binder `pp::preprocess`
> seul pour **afficher** la source expansée dans l'éditeur (pas de preview live).
