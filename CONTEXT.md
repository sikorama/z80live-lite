# z80live

L'application (SPA Svelte + assembleurs WASM) qui édite des sources Z80, les
construit et les fait tourner dans un émulateur. Le vocabulaire ci-dessous
couvre la portée d'un build et sa persistance ; le vocabulaire de sortie d'un
build (Artefact, Conteneur, Profil, Morceau...) appartient à
[fantams](./fantams/CONTEXT.md) et n'est pas redéfini ici.

## Langage

**Projet** :
La portée d'un build, TOUJOURS — plus une notion provisoire (tranché
2026-09-10,
[docs/adr/0002-le-projet-porte-des-cibles-dexport-et-des-membres.md](./docs/adr/0002-le-projet-porte-des-cibles-dexport-et-des-membres.md)).
Regroupe des Sources *point d'entrée*, déclarées à la main, et porte une ou
plusieurs Cibles d'export. Une Source seule (le cas d'aujourd'hui) est un
Projet à un seul point d'entrée, créé implicitement — il n'existe qu'une
seule façon de construire, pas deux chemins parallèles à maintenir.
_Éviter_ : build, configuration (trop vague)

**Source point d'entrée** :
Une Source explicitement ajoutée à un Projet — celle qui peut ensuite
recevoir un rôle dans une Cible (une banque, la Source d'une Cible SNA...).
Distincte d'une Source `lib` (`is_include`), jamais ajoutée directement : elle
n'apparaît qu'au travers de la Fermeture d'un point d'entrée qui la référence.
_Éviter_ : membre (réservé à « Membre de Cible », un cran plus bas), fichier
(trop large)

**Cible d'export** :
Ce qu'un Projet produit : un Artefact conforme à un Format de sortie de
fantams (Conteneur comme CPR, ou Base comme SNA — vocabulaire de
[fantams](./fantams/CONTEXT.md)). Porte les réglages partagés par tous ses
Membres (Profil : un Conteneur cible une seule machine). Un Projet à un seul
point d'entrée porte implicitement une Cible (celle que la Source décrivait
déjà par son ancien `container`). Un Projet peut porter plusieurs Cibles, y
compris plusieurs du même Format (deux CPR différents d'un même Projet sont
deux Cibles distinctes, pas un seul slot par Format).
_Éviter_ : export, build (déjà pris), format (trop large, c'est celui de
fantams)

**Membre (de Cible)** :
Un point d'entrée du Projet, affecté à UNE Cible avec le rôle que ce Format y
demande (le numéro de banque pour un Conteneur CPR ; rien de plus pour une
Cible SNA, qui n'en compte qu'un seul). Porte le Fichier de lien : il est
propre au placement DANS cette Cible précise, pas à la Source elle-même — la
même Source peut être Membre de deux Cibles avec deux Fichiers de lien
différents (ex. `boot.asm` en banque 0 d'un cartouche "démo" ET d'un
cartouche "complet", chacun avec son propre script de placement).
_Éviter_ : banque (c'est son rôle dans une Cible CPR seulement, pas son nom
général)

**Fermeture** :
L'ensemble des fichiers qu'une Source point d'entrée touche une fois
préprocessée — elle-même plus, récursivement, toute Source `lib` qu'elle
inclut. JAMAIS stockée : calculée à la demande (`pp::Result::files()` —
enregistrée à l'ouverture de chaque fichier, pas dérivée des lignes qui
survivent : un fichier tout en directives, sans une ligne qui en réchappe,
appartient quand même à la Fermeture), pour ne jamais pouvoir mentir sur
l'état réel d'un fichier `lib` partagé. C'est elle qui rend un Projet
exportable en autonome, et qui peuplera sa vue en arborescence.
_Éviter_ : dépendances (trop générique), includes (c'est le mécanisme, pas
l'ensemble qu'il produit)

**Directive de build** :
La ligne de commentaire en tête de Source (`;z80: assembler=... buildmode=...
entry=...`) qui reflète en texte un sous-ensemble des réglages stockés en
base, pour qu'une Source exportée seule reste assemblable. Le Profil et le
Fichier de lien n'y sont **délibérément pas** reflétés — voir
[docs/adr/0001-profil-et-fichier-de-lien-restent-hors-directive.md](./docs/adr/0001-profil-et-fichier-de-lien-restent-hors-directive.md).
_Éviter_ : métadonnées, en-tête, header

**Fichier de lien** :
Le script `.ld` de fantams (optionnel), choisi parmi les fichiers `includes[]`
déjà injectés dans le système de fichiers WASM plutôt que via un canal
d'upload dédié. Consommé par fantams via `-T`. Défini par le Membre (tranché
2026-09-10) : propre au placement d'une Source DANS une Cible précise, pas à
la Source elle-même ni au Projet dans son ensemble.
_Éviter_ : linker script (anglicisme à réserver au code), script de placement
