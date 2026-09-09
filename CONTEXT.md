# z80live

L'application (SPA Svelte + assembleurs WASM) qui édite des sources Z80, les
construit et les fait tourner dans un émulateur. Le vocabulaire ci-dessous
couvre la portée d'un build et sa persistance ; le vocabulaire de sortie d'un
build (Artefact, Conteneur, Profil, Morceau...) appartient à
[fantams](./fantams/CONTEXT.md) et n'est pas redéfini ici.

## Langage

**Projet** :
La portée d'un build : aujourd'hui, une Source unique (hors fichiers de
`includes[]`, qui n'en portent pas). Le Projet n'a pas encore d'existence
propre en base — c'est une notion provisoire, en attendant qu'un Projet
puisse un jour regrouper plusieurs Sources. Les réglages qui sont
conceptuellement des réglages de Projet (Profil, Fichier de lien) sont pour
l'instant stockés sur la Source elle-même.
_Éviter_ : build, configuration (trop vague)

**Directive de build** :
La ligne de commentaire en tête de Source (`;z80: assembler=... buildmode=...
entry=...`) qui reflète en texte un sous-ensemble des réglages stockés en
base, pour qu'une Source exportée seule reste assemblable. Le Profil et le
Fichier de lien n'y sont **délibérément pas** reflétés — voir
[docs/adr/0001-profil-et-fichier-de-lien-restent-hors-directive.md](./docs/adr/0001-profil-et-fichier-de-lien-restent-hors-directive.md).
_Éviter_ : métadonnées, en-tête, header

**Fichier de lien** :
Le script `.ld` de fantams (défini par le Projet, optionnel), choisi parmi les
fichiers `includes[]` déjà injectés dans le système de fichiers WASM plutôt
que via un canal d'upload dédié. Consommé par fantams via `-T`.
_Éviter_ : linker script (anglicisme à réserver au code), script de placement
