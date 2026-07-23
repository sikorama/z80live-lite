# z80next — architecture serverless de z80live

Refonte de z80live vers une architecture simple et portable :

- **Compilation côté client** : les assembleurs (rasm, sjasmplus) tournent en **WebAssembly** dans le navigateur. Plus de serveur de build (`z80live_node` supprimé).
- **Émulation côté client** : tiny8bit / RVMPlayer / crocods en WASM, alimentés directement par le `.sna` via blob (aucune URL serveur).
- **Serveur = simple base de données** : un fichier **SQLite portable** + une API CRUD tout-JS. Pas de MongoDB, pas de Meteor.
- **Front** : **SPA Vite + Svelte 5** (`app/`), client only, build statique. Éditeur CodeMirror 6 (mode Z80).

Périmètre fonctionnel retenu : **édition / fork en ligne**. Abandonnés : comptes/login, notes/votes, groupes/permissions. Lecture publique, écriture ouverte (protégeable via `Z80_WRITE_TOKEN`).

> Guide pratique (dev local, déploiement, export en masse des `.sna`) : **[QUICKSTART.md](QUICKSTART.md)**.

## Structure

```
z80next/
├── db/          schema.sql · import.mjs · classify.mjs · z80live.sqlite (généré, ~39 Mo)
├── server/      api.mjs  — REST CRUD/fork + sert la SPA (node:http + node:sqlite, 0 dépendance)
├── wasm/        rasm.mjs+.wasm · sjasmplus.mjs+.wasm · assemble.mjs (module isomorphe)
├── client/      store.mjs  — couche d'accès unifiée (API ou SQLite locale/sql.js)
├── emu/         tiny8bit/  — émulateur CPC (WASM)
├── emu-sw.js    Service Worker : sert le .sna assemblé à /build/<name>.sna (sans serveur)
├── vendor/      sqljs/     — SQLite WASM pour la lecture navigateur (mode lite)
├── app/         SPA Vite + Svelte (UI) — build -> app/dist/
└── scripts/     export-lite.mjs  — génère le bundle statique distribuable
```

## Utilisation (mode complet : édition)

Requiert Node ≥ 22.5 (`node:sqlite`). Compatible Bun (`bun:sqlite`).

```bash
npm run import              # (ré)génère db/z80live.sqlite depuis ../export_full.json
(cd app && npm install && npm run build)   # compile la SPA -> app/dist
npm run serve               # API + SPA sur http://localhost:3000
```

Le serveur sert `app/dist` si présent (SPA), sinon `demo/` (démo brute, sans build).

Variables d'env : `PORT`, `DB` (chemin du .sqlite), `Z80_WRITE_TOKEN` (si défini, écriture via `Authorization: Bearer <token>`).

## API

| Méthode | Route | Rôle |
|---|---|---|
| GET | `/api/sources?q=&buildmode=&limit=&offset=` | liste / recherche FTS (sans le code) |
| GET | `/api/sources/:id` | source complète (avec code) |
| POST | `/api/sources` | créer |
| PUT | `/api/sources/:id` | modifier |
| POST | `/api/sources/:id/fork` | forker (lignée via `fork_parent`) |
| DELETE | `/api/sources/:id` | supprimer |
| GET | `/api/health` | état |

## Démo — la boucle complète (éditer → assembler → exécuter)

```bash
npm run serve            # API + fichiers statiques (démo, wasm, émulateur) sur une seule origine
# puis ouvrir http://localhost:3000/  dans un navigateur
```

`demo/index.html` relie toute la chaîne **sans serveur de build** : sélection d'une source (API) →
éditeur → assemblage `rasm`/`sjasmplus` en WASM (`wasm/assemble.mjs`) → le `.sna` (Uint8Array) est
passé en **blob** à l'émulateur tiny8bit (`emu/tiny8bit/cpc.html?file=<blob>`), même origine.

- `wasm/rasm.mjs` + `rasm.wasm` — rasm en WASM (sortie byte-correcte ; écart mémoire mineur documenté).
- `wasm/sjasmplus.mjs` + `sjasmplus.wasm` — sjasmplus en WASM (sortie identique au natif).
- `wasm/assemble.mjs` — module isomorphe : header/footer selon buildOptions, défaut rasm, fallback sjasmplus.
- `emu/tiny8bit/` — émulateur CPC (floooh/tiny8bit), args `file`/`input`/`joystick`/`type`.

> ⚠️ La validation *visuelle* de l'émulateur se fait dans un navigateur (non testable en headless).
> Note dev : les assembleurs sont buildés avec `-sSTACK_SIZE=8388608` (8 Mo). Le défaut Emscripten
> (64 Ko) provoquait un débordement de pile du parseur récursif de rasm → trap `table index out of
> bounds` (y compris navigateur). `assemble.mjs` remonte l'erreur via `res.error`.

## Version lite (statique, distribuable, lecture seule)

La base SQLite peut être **embarquée dans le client** et lue en local (sql.js) : plus aucun serveur.
La version complète (avec API) sert d'instance d'édition ; on en **exporte** un instantané statique.

```bash
(cd app && npm run build)   # SPA à jour
npm run export:lite         # génère ../dist-lite/  (~7,4 Mo : SPA + assembleurs + émulateur + base gzippée)
cd ../dist-lite && npx serve   # ou tout hébergeur statique / CDN
```

- **La même SPA** sert les deux modes : `client/store.mjs#openStore` détecte l'API ; à défaut,
  elle charge `db/z80live.sqlite.gz` (base allégée sans FTS ni `legacy_json`, gzippée ~4,8 Mo) via **sql.js**.
  La build sql.js du CDN n'a pas FTS5 → la recherche lite utilise un repli **LIKE**.
- **Émulateur sans serveur** : `emu-sw.js` (Service Worker) sert le `.sna` assemblé à `/build/<name>.sna`
  (URL avec extension → détection de type par l'émulateur). Fonctionne aussi en mode complet.
- Lecture seule : pas de sauvegarde de sources (édition/exécution éphémères uniquement).

## Export en masse des .sna

Génère les `.sna` de toutes les sources SNA qui compilent (rejoue rasm/sjasmplus en WASM côté Node,
même logique que `db/classify.mjs`), dans un dossier ou dans une archive zip unique.
Guide pas à pas (et déploiement) : [QUICKSTART.md](QUICKSTART.md).

```bash
npm run export:sna              # -> ../sna-export/ (un .sna par source)
npm run export:sna:zip          # -> ../sna-export.zip (archive unique)
# ou avec une destination explicite :
node --experimental-sqlite scripts/export-sna.mjs mon-dossier
node --experimental-sqlite scripts/export-sna.mjs --zip mon-archive.zip
```

Les sources qui échouent à l'assemblage (dépendances externes `incbin`/`include`, erreurs de
syntaxe) sont ignorées ; le résumé (`OK`/`échecs`) s'affiche en fin d'exécution.

## Reste à faire

- Coloration dédiée de la ligne de directives `;z80:` dans l'éditeur.
- Corriger l'écart mémoire de rasm (dépendance à un malloc zéroé) pour un déterminisme byte-parfait.
- Build sjasmplus **avec Lua** (`USE_LUA=1`) pour ~6 sources qui scriptent en Lua.
- Fallback `.sna` pré-compilé pour les ~15 sources à dépendances externes (`incbin`/`include`).
- Prise en charge des DSK (repoussée).
