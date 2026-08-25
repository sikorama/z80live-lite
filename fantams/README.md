# fantams — assembleur Z80 léger (source WASM)

Initiallement un fork simplifié de rasm, compilé vers WebAssembly pour tourner côté client (comme `wasm/rasm.*` et `wasm/sjasmplus.*`).
A évolué vers un périmètre éloigné, puisque l'assembleur est en 2 passes, et focalise sur l'export SNA. Il est doté d'un **préprocesseur complet** (macros, `REPEAT`/`WHILE`, `IF`, `STRUCT`, scope auto-local, includes)

## Structure

| Lib | Rôle |
|---|---|
| `z80` (`z80.cpp`) | encodeur d'instructions Z80 |
| `keywords` (`keywords.cpp`) | « ce mot est-il un label ? », indexé par phase |
| `pp` + `expr` (`pp.cpp`, `expr.cpp`) | préprocesseur texte→texte + évaluateur d'expressions |
| `parser` (`parser.cpp`) | ligne texte → instruction |
| `asm` (`asm.cpp`) | assembleur 2 passes (ORG, symboles, refs avant) |
| `beautify` (`beautify.cpp`) | mise en forme du source, texte→texte |
| `sna` (`sna.cpp`) | export snapshot CPC `.sna` |

Outils : `fantams` (`asm_main.cpp`, `.asm → .bin/.sna`) et `ppdump`
(`pp_main.cpp`, équivalent `-E` : export de la source préprocessée).

## Mise en forme du source

Deux règles, pas une de plus — celles-là même que l'assembleur signale déjà :
un label écrit sans son deux-points, **seul sur sa ligne**, le reçoit ; une
ligne de code sans label voit son indentation remplacée par quatre espaces.
Rien n'est paramétrable, et un appel de macro possible (`sprite 4,12`) est
laissé intact plutôt que deviné.

```bash
fantams src.asm --beautify -o src.fmt.asm   # le source, mis en forme
fantams src.asm -E -o src.pp.asm            # la source déroulée, mise en forme
```

La source déroulée sort mise en forme par définition : c'est un livrable, pas un
artefact de débogage. Dans z80live, le bouton **¶** (Alt+Maj+F) réécrit le
tampon de l'éditeur ; l'annulation est celle de l'éditeur.

Conception et alternatives écartées : `docs/adr/0013-la-mise-en-forme-preserve-les-lignes-et-refuse-de-deviner.md`.

## Base : faire tourner du code qui appelle le firmware

Un snapshot produit par un assembleur ne contient que le code assemblé — tout le
reste vaut zéro. Or un `CALL &BB5A` a besoin des vecteurs d'indirection au-delà
de `&B000`, des variables système, des vecteurs de RST, et d'un état matériel où
la ROM basse est activée : autant de choses que la ROM installe **au boot** et
qu'aucun assemblage ne produit.

`--base` pose l'assemblage sur un tel état, capturé une fois pour toutes :

```bash
fantams src.asm -o out.sna --base bases/cpc6128-en.sna
```

Chaque adresse que le source a **réellement** écrite l'emporte ; toutes les
autres gardent l'octet de la base — y compris quand le source y a écrit un zéro,
que la *coverage* distingue d'une adresse jamais touchée. L'en-tête de la base
fait foi (`SP`, gate array, `I`, `IM`, CRTC, palette) et seul `PC` est patché.

Contraintes, et les refus qui vont avec :

| Situation | Comportement |
|---|---|
| Base en version 3 (chunks `MEM0`) | refus, en nommant le remplaçant : réenregistrer en v2 |
| Base de 128 Ko | refus : une base ne peuple que les 64 K de base |
| Base absente ou illisible | refus — jamais de repli sur des zéros, qui produirait un `.sna` qui démarre et plante au premier appel firmware |
| `--base` avec une sortie autre que `.sna` | refus : la base est une option du backend `sna` |

Produire une base : démarrer l'émulateur, attendre le `Ready`, enregistrer un
snapshot **version 2** (en-tête de 256 octets + dump plat de 64 Ko). C'est la
**ROM** qui doit correspondre, pas seulement le modèle — voir
`app/public/bases/README.md` pour le catalogue côté z80live.

Le chevauchement de deux écritures **du source** produit un avertissement qui
nomme les deux lignes en conflit (une seule ligne par plage contiguë). Écraser la
base n'en produit jamais aucun : c'est l'usage normal.

Conception et alternatives écartées : `docs/adr/0012-base-snapshot-fusionnee-par-la-coverage.md`.

## Tests natifs

```bash
make test        # 382 tests (z80 · expr · pp · parser · asm · beautify · sna)
```

## Build WASM

Passe par l'image `emscripten/emsdk` (podman/docker) — pas besoin d'emcc local :

```bash
./build-wasm.sh          # -> ../wasm/fantams.mjs + fantams.wasm
node test-wasm.mjs       # test d'intégration via wasm/assemble.mjs
```

Flags notables : 
 * `-fexceptions` - sans lui, tout `throw` devient `abort()` en WASM,
 * `-sSTACK_SIZE=8388608` (parseur récursif), 

## Intégration app

`wasm/assemble.mjs` expose l'assembleur via `assembler: 'fantams'` (ou la directive
`;z80: assembler=fantams` en tête de source). `wrapFantams` injecte un en-tête
`org`/`run` (syntaxe lite, **pas** `BUILDSNA`) si absent. Sortie `.sna` byte-identique
au binaire natif.

La base voyage par `; z80: base=<id>` ou par l'option `base` de l'appel : l'hôte
résout l'**identifiant de catalogue** en fichier, l'écrit dans le FS wasm et
passe `--base <chemin>`. Le cœur ne voit jamais un id ni une URL. `base=none`
annule une base héritée d'une couche inférieure (la valeur vide, elle, est
indistinguable de l'absence dans `buildDirectiveLine`).

### Priorité entre les sources d'options

De la plus forte à la plus faible :

1. **option explicite** passée à l'invocation (CLI, appel d'API) ;
2. **directive `;z80:`** en tête de source ;
3. **fiche en base** (colonnes `assembler`, `buildmode`, `entry_point`) ;
4. **défaut du code** (`fantams`, `org #8000`…).

La directive fait donc autorité sur le **stocké**, non sur l'**explicite** —
sinon toucher un réglage de l'éditeur resterait sans effet.

Où chaque niveau existe réellement :

- la **CLI** est le niveau explicite : elle ne lit aucune directive `;z80:`, ses
  options s'appliquent telles quelles ;
- dans l'hôte JS (`assemble()`), les options passées par l'appelant sont du
  **stocké** : la directive les écrase (`{ ...raw, ...parseDirectives(code) }`),
  ce qui est voulu — `scripts/export-sna.mjs` alimente ces options depuis les
  colonnes de la fiche ;
- dans l'éditeur z80live, les contrôles ne passent pas par-dessus : ils
  **réécrivent la directive** (`upsertDirectives`) avant d'assembler, de sorte
  que la seule autorité visible reste le source.

Une contradiction entre deux couches se règle en faveur de la plus forte, avec un
avertissement (base écartée parce que la sortie n'est pas un snapshot, par
exemple). Une contradiction **au sein d'un même appel** est une erreur : là,
l'appelant se contredit lui-même.

> Piste différée (jugée trop complexe pour l'instant) : binder `pp::preprocess`
> seul pour **afficher** la source expansée dans l'éditeur (pas de preview live).
