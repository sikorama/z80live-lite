# Le Projet devient permanent ; Cible d'export et Membre remplacent `container`/`ld_filename` de la Source

`CONTEXT.md` annonçait déjà ce moment : « Projet » était glosé comme
« la portée d'un build : aujourd'hui, une Source unique... en attendant qu'un
Projet puisse un jour regrouper plusieurs Sources ». Le déclencheur concret
est le premier vrai besoin multi-Sources : une cartouche CPR (fantams/cpr.h)
n'existe qu'en liant CHAQUE banque physique séparément, puis en les agrégeant
— aucune Source seule ne peut produire ça. En même temps, la même Source
(ex. `boot.asm`) doit pouvoir servir de banque 0 à deux cartouches différents
(une « démo » à 4 banques, un « complet » à 21), avec deux scripts de
placement différents pour le même code.

**Décidé** : le Projet n'est plus une notion provisoire. C'est TOUJOURS la
portée d'un build ; une Source seule en est un cas particulier (un Projet à
un point d'entrée, créé implicitement — pas un second chemin de build à
maintenir). Un Projet regroupe des **Sources point d'entrée** (déclarées à la
main) et porte une ou plusieurs **Cibles d'export**, chacune produisant un
Artefact fantams (un Conteneur comme CPR, une Base comme SNA). Chaque point
d'entrée affecté à une Cible en devient un **Membre**, avec le rôle que le
Format de cette Cible y demande (un numéro de banque pour CPR ; rien de plus
pour SNA, qui n'en compte qu'un).

Conséquence directe sur les réglages aujourd'hui stockés sur `sources` :
- `container` disparaît comme champ — c'est le Format de la Cible qui le dit.
- `ld_filename` migre sur le **Membre** : c'est un réglage de PLACEMENT dans
  UNE Cible précise (l'exemple `boot.asm` ci-dessus le montre), jamais un fait
  intrinsèque à la Source.
- `profile` migre sur la **Cible** : partagé par tous ses Membres — un
  Conteneur cible une seule machine, il ne peut pas y avoir une banque
  `cpc6128` dans une cartouche `cpcplus`.
- `assembler` reste sur la Source (propriété du code, pas de son usage), mais
  devient sans objet à terme : seul fantams sera supporté.

Une quatrième notion, la **Fermeture**, complète le modèle sans ajouter de
table : l'ensemble des fichiers qu'un point d'entrée touche une fois
préprocessé (lui-même, plus récursivement toute Source `lib` qu'il inclut).
Elle n'est **jamais stockée** — fantams sait déjà, en préprocessant, quel
fichier il ouvre à chaque INCLUDE ; `pp::Result::files()` (ADR mis en oeuvre
le 2026-09-10) l'enregistre à l'ouverture plutôt que de le dériver des
lignes qui survivent : un fichier tout en directives (des INCLUDE, une
déclaration MACRO jamais appelée ici...) n'en laisse aucune, et appartient
quand même à la Fermeture. Toujours dans l'esprit de `--dump-profile` :
rendre une donnée déjà connue du préprocesseur, pas fabriquer un nouveau
moteur d'analyse. La stocker aurait pu mentir dès qu'une Source `lib` partagée change
sans que le Projet lui-même change. C'est elle qui rendra un Projet
exportable en autonome et qui peuplera sa vue en arborescence (remplaçant la
liste plate actuelle) — ces deux usages restent à construire.

## Options considérées

- **Rôle de Membre générique** (`role` texte/JSON interprété par le code)
  plutôt que des tables typées par Format (`bank INTEGER` pour CPR, etc.) —
  écarté : le nombre de Formats reste petit (`sna`/`cpr` maintenant, `dsk` et
  du binaire brut/AMSDOS plus tard, `cdt` à faible intérêt, `cro` plus tard),
  la duplication de structure reste bon marché, et SQL type et contraint ce
  qu'un `role` opaque déplacerait vers du code applicatif à faire confiance.
- **Fermeture mise en cache** en base — écartée pour la raison ci-dessus
  (mentir dès qu'un `lib` partagé change ailleurs).
- **Un seul slot de Cible par Format** — écarté : rien n'empêche de vouloir
  deux variantes du même Format à partir des mêmes Sources (l'exemple
  démo/complet). Une Cible est un objet nommé, pas un slot.

## Conséquences

- **Fait (2026-09-10)** : `project_banks` refondu en `targets` + `target_members`
  (`server/api.mjs`, `db/schema.sql`), testé (`scripts/test-server-api.mjs`).
- **Fait (2026-09-10)** : migration des 424 Sources existantes (hors 2
  librairies, qui n'ont pas de Cible) vers un Projet/Cible/Membre implicite
  chacune (`db/migrate-0002-target-fields.mjs`, rejouable), puis retrait de
  `profile`/`ld_filename`/`container` de `sources`. La Cible implicite d'une
  Source neuve se crée à la première écriture de l'un de ces trois réglages
  (`server/api.mjs`, `ensureDefaultTarget`/`applyTargetFields`) — jamais
  avant, pour qu'une Source qui ne les configure jamais n'accumule pas un
  Projet vide derrière elle. `sources.default_target_id` porte ce lien ;
  `App.svelte`/`client/store.mjs` n'ont pas eu à changer, le contrat API
  (`profile`/`container`/`ld_filename` dans le corps et la réponse JSON) est
  resté identique — seul son stockage a bougé.
- `wasm/assemble.mjs` lit `container`/`profile`/`ld_filename` depuis les
  réglages passés par l'appelant, jamais depuis `sources` directement — non
  affecté par cette migration.
