# Quick start — z80next

Guide pratique pour lancer le projet en local, le déployer, et exporter en masse les `.sna`
des sources compilables.

## Prérequis

- Node ≥ 22.5 (utilise `node:sqlite`, expérimental — compatible aussi Bun `bun:sqlite`).
- La base `db/z80live.sqlite` présente (générée via `npm run import`, ou déjà fournie dans le repo).

Vérifier :

```bash
node --version                # >= v22.5
ls db/z80live.sqlite          # doit exister
```

## Lancer en local (mode dev)

```bash
npm run import                             # (ré)génère db/z80live.sqlite depuis ../export_full.json
(cd app && npm install && npm run build)   # compile la SPA -> app/dist
npm run serve                              # API + SPA sur http://localhost:3000
```

`npm run import` n'est nécessaire que si `db/z80live.sqlite` n'existe pas encore ou doit être
régénérée. Le serveur sert `app/dist` s'il existe (SPA buildée), sinon `demo/` (démo brute).

## Déploiement

Le projet propose deux modes de déploiement, selon le besoin :

### 1. Mode complet (édition + API) — sur un serveur/VPS

C'est un serveur Node classique, sans dépendance (`node:http` + `node:sqlite`) : un simple
process à garder en vie derrière un reverse proxy (nginx/caddy) ou un supervisor (systemd, pm2).

```bash
(cd app && npm install && npm run build)   # build la SPA une fois pour toutes
PORT=8080 DB=/chemin/persistant/z80live.sqlite Z80_WRITE_TOKEN=un-secret \
  node --experimental-sqlite server/api.mjs
```

Variables d'env :

| Variable          | Défaut                       | Rôle                                                    |
|-------------------|-------------------------------|----------------------------------------------------------|
| `PORT`            | `3000`                        | port d'écoute HTTP                                        |
| `DB`              | `db/z80live.sqlite`            | chemin du fichier SQLite (le mettre sur un volume persistant) |
| `Z80_WRITE_TOKEN` | *(non défini = écriture libre)* | si défini, écriture (POST/PUT/DELETE) protégée par `Authorization: Bearer <token>` |

Points d'attention en prod :

- `DB` doit pointer vers un chemin persistant (volume monté), sinon les sources créées/forkées
  sont perdues au redéploiement.
- Définir `Z80_WRITE_TOKEN` dès que l'instance est exposée publiquement en écriture ouverte.
- Le process n'a pas de daemon intégré : le superviser (systemd `Restart=always`, pm2, docker
  `restart: unless-stopped`…) pour qu'il redémarre après un crash ou un reboot.
- Rebuild `app/dist` (`cd app && npm run build`) à chaque changement du front avant de redéployer.

Exemple d'unité systemd minimale :

```ini
# /etc/systemd/system/z80live.service
[Unit]
Description=z80live API
After=network.target

[Service]
WorkingDirectory=/opt/z80next
Environment=PORT=8080
Environment=DB=/var/lib/z80live/z80live.sqlite
Environment=Z80_WRITE_TOKEN=un-secret
ExecStart=/usr/bin/node --experimental-sqlite server/api.mjs
Restart=always

[Install]
WantedBy=multi-user.target
```

### 2. Mode lite (statique, lecture seule) — sur un CDN/hébergement statique

Pas de serveur du tout : un instantané de la base est embarqué en SQLite gzippé et lu en local
via sql.js dans le navigateur. Pratique pour un hébergement statique (Netlify, Pages, S3+CDN…).

```bash
(cd app && npm run build)      # SPA à jour
npm run export:lite            # génère ../dist-lite/ (~7,4 Mo)
```

Puis déposer le contenu de `../dist-lite/` sur n'importe quel hébergeur statique — ou tester
en local :

```bash
cd ../dist-lite && npx serve
```

Limite du mode lite : lecture seule (pas de sauvegarde de sources, édition/exécution éphémères).

## Export en masse des .sna

Génère les `.sna` de toutes les sources SNA qui compilent (rejoue rasm/sjasmplus en WASM côté
Node, même logique que `db/classify.mjs`), dans un dossier ou dans une archive zip unique.
Ne nécessite ni build de la SPA, ni serveur lancé — juste `db/z80live.sqlite`.

### Dans un dossier

Un fichier `.sna` par source, dans `../sna-export/` (à côté du repo) :

```bash
npm run export:sna
```

```
Sources SNA candidates : 336
  25/336 ... 336/336
OK: 192 (dont 2 via fallback sjasmplus) — échecs: 144
Dossier -> /chemin/vers/sna-export  (192 .sna)
```

Destination personnalisée :

```bash
node --experimental-sqlite scripts/export-sna.mjs mon-dossier
```

### En une seule archive zip

```bash
npm run export:sna:zip
```

Produit `../sna-export.zip` (une entrée par source compilée). Destination personnalisée :

```bash
node --experimental-sqlite scripts/export-sna.mjs --zip mon-archive.zip
```

### Comprendre le résultat

- Seules les sources dont le `buildmode` est `sna` / `sna_cpc464` / `sna_cpc6128` sont traitées
  (336 sources dans la base actuelle).
- Chaque source est réassemblée à la volée (rasm en WASM, avec repli sjasmplus si aucun
  assembleur n'est déclaré sur la source) — pas de résultat mis en cache, donc fiable même si
  la base a changé depuis le dernier `npm run classify`.
- Les échecs (~144 sur 336 actuellement) viennent essentiellement de sources à dépendances
  externes manquantes (`incbin`/`include` vers des fichiers non embarqués) ou d'erreurs de
  syntaxe ; ils sont simplement exclus du résultat, sans faire échouer l'export global.
- Nom de fichier : `slugname` (ou `name`) de la source, nettoyé des caractères non alphanumériques ;
  suffixé `-2`, `-3`… en cas de collision de nom.

## Aller plus loin

- `npm run classify` — met à jour en base le statut `build_status`/`compilable` par source
  (utile pour trier/filtrer côté UI), sans écrire de `.sna`.
- Voir `README.md` pour l'architecture complète du projet.
