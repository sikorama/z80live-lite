# fantams

Assembleur Z80 deux passes ciblant l'export de snapshots CPC, doté d'un
préprocesseur dont la source déroulée est un livrable de premier plan. Le
vocabulaire ci-dessous existe parce que « symbole » et « variable » recouvrent,
dans le monde des assembleurs Z80, des objets qui n'ont ni la même phase de
résolution ni les mêmes règles de réaffectation — confusion qui a un coût direct
sur la conception du préprocesseur.

## Langage

### Phases

**Temps préprocesseur** :
La phase qui transforme le texte source en source déroulée. Elle ne connaît
aucune adresse.
_Éviter_ : temps PP, phase 0, pré-passe

**Temps d'assemblage** :
La phase qui transforme la source déroulée en octets et en adresses. Elle
comprend les deux passes de calcul d'adresses puis d'encodage.
_Éviter_ : passe 1 / passe 2 (qui désignent ses sous-étapes, pas la phase)

**Source déroulée** :
Le texte plat produit par le préprocesseur : macros expansées, boucles
déroulées, includes insérés, scopes renommés. Elle est réassemblable à
l'identique et lisible par un humain.
_Éviter_ : source expansée, source préprocessée, sortie du PP

### Noms et valeurs

**Symbole** :
Terme générique couvrant label, constante et variable. À n'employer que
lorsque la distinction est réellement indifférente.

**Label** :
Nom lié à une adresse par sa position dans le code émis. Résolu au temps
d'assemblage uniquement, jamais au temps préprocesseur.
_Éviter_ : étiquette, symbole d'adresse

**Constante** :
Nom lié à une valeur par `EQU`, non réaffectable.
_Éviter_ : define, symbole EQU

**Variable** :
Nom lié à une valeur par `=`, réaffectable, résolu paresseusement. Sa valeur
au temps préprocesseur est celle de sa dernière affectation rencontrée dans
l'ordre de lecture.
_Éviter_ : variable rasm, assignation

**Variable de préprocesseur** :
Nom lié à une valeur par `LET`, réaffectable, dont la résolution au temps
préprocesseur est **exigée** : une valeur non résoluble à cette phase est une
erreur, pas un report.
_Éviter_ : variable PP, variable stricte

**Résoluble au temps préprocesseur** :
Se dit d'une expression dont tous les noms sont des variables de
préprocesseur, des variables, des constantes ou des arguments de macro
eux-mêmes résolubles à cette phase. Une expression touchant un label ne l'est
jamais.

### Portée

**Scope auto-local** :
Règle rendant unique, à chaque expansion, tout label défini dans un corps de
macro ou une itération de boucle. `@@export` y fait exception.
_Éviter_ : scope local, renommage

**Module** :
Espace de noms de labels, dont la syntaxe est une divergence assumée vis-à-vis
de rasm.

### Mémoire

**Banque** :
Bloc physique de 16 K de la mémoire de la machine, numéroté à partir de 0. Les
banques 0 à 3 forment les 64 K de base, les suivantes l'extension du 6128.
_Éviter_ : page, bloc, configuration

**Slot** :
L'une des quatre fenêtres de 16 K de l'espace d'adressage du Z80 — 0x0000,
0x4000, 0x8000, 0xC000 — dans laquelle une banque peut être rendue visible.
_Éviter_ : fenêtre, zone

**Adresse logique** :
L'adresse 16 bits à laquelle le Z80 verra un octet à l'exécution. C'est la
valeur que prennent les labels. L'assembleur ne peut pas la déduire : seul le
programmeur sait comment la mémoire sera paginée.
_Éviter_ : adresse d'assemblage, adresse virtuelle

**Emplacement de rangement** :
Le couple banque et offset où un octet est écrit dans l'image produite.
Indépendant de l'adresse logique.
_Éviter_ : adresse physique, adresse de sortie

**Espace d'adressage** :
Une des collections de 16 K que l'assembleur sait remplir : une banque RAM ou
une ROM. Les deux ne se confondent pas — une ROM est en lecture seule et
sélectionnée autrement.

**Image** :
La collection complète des espaces d'adressage remplis, telle que le backend la
reçoit.
_Éviter_ : dump, mémoire

**Configuration RAM** :
L'une des huit combinaisons de pagination du gate array, sélectionnée par
`&7Fxx`. Sans rapport avec une banque, malgré la graphie `C0`–`C7` qui les
désigne conventionnellement sur CPC.
_Éviter_ : banque (précisément la confusion à ne pas faire)

### Export

**Découpage** :
La règle qui partitionne l'image en morceaux : un par bloc `ORG`, un seul
englobant tout, ou un par banque de 16 K ou de 64 K.
_Éviter_ : split, segmentation

**Morceau** :
Une portion de l'image destinée à devenir un artefact, portant sa banque, son
adresse logique, son point d'entrée et ses octets. Son nom est dérivé de son
emplacement, jamais déclaré dans le source.
_Éviter_ : bloc, segment, chunk

**Encapsulation** :
Ce qu'on ajoute à un morceau pour le rendre chargeable — aujourd'hui l'en-tête
AMSDOS de 128 octets, ou rien.
_Éviter_ : header, wrapping

**Conteneur** :
Ce qui rassemble les morceaux encapsulés en livraison : système de fichiers,
image DSK, cartouche CPR, arborescence CRO. Le SNA n'en est pas un — il prend
l'image entière.
_Éviter_ : format (trop large), archive

**Artefact** :
Un fichier produit par un backend. Un backend en rend un ensemble, pas un seul.
_Éviter_ : sortie, output, binaire

### Diagnostics

**Listing** :
La correspondance, ligne de source par ligne de source, entre banque, adresse
logique et octets produits. C'est ce qui rend consultable ce que l'assembleur a
réellement décidé.
_Éviter_ : dump, trace

### Architecture

**Cœur** :
Les six modules sans état ni entrées-sorties (`z80`, `expr`, `parser`, `pp`,
`asm`, `sna`). Il ne connaît aucun hôte.
_Éviter_ : lib, moteur, backend

**Hôte** :
Un environnement qui fait tourner fantams : CLI natif, navigateur via WASM,
serveur, intégration continue, MCP.
_Éviter_ : client, frontend

**Adaptateur** :
La couche fine qui relie un hôte au cœur : elle fournit le `FileProvider`,
traduit les options et sérialise le résultat. Elle ne contient aucune règle du
langage.
_Éviter_ : wrapper, binding, glue

**Backend** :
Producteur d'un format de sortie à partir de l'image mémoire assemblée (`sna`,
plus tard `dsk`). Sélectionné à la compilation, jamais chargé dynamiquement.
_Éviter_ : plugin, exporter, writer

### Compatibilité

**Corpus** :
L'ensemble des sources réelles écrites pour rasm, conservé hors de fantams,
servant à *mesurer* la compatibilité. Il contient des forks et des sources dont
la correction n'est pas garantie : c'est un instrument de mesure, pas une
référence de correction.
_Éviter_ : base de test, suite de tests (qui désignent les tests unitaires)

**Texte distinct** :
Une source du corpus après regroupement des copies de même contenu. C'est
l'unité de mesure de la diversité syntaxique, et donc du travail
d'implémentation restant. Le regroupement se fait sur le texte et non sur la
filiation, celle-ci n'étant pratiquement pas renseignée dans la base ; il ne
capture que les copies exactes, ce qui en fait une borne haute.

**Cas de référence** :
Une source minimale et autonome, versionnée dans fantams, exerçant une seule
construction du langage. Contrairement au corpus, sa sortie attendue fait
autorité : c'est ce qui *défend* la compatibilité contre les régressions.
_Éviter_ : fixture, exemple, échantillon

**Portage** :
Le travail d'édition nécessaire pour qu'une source écrite pour rasm assemble
sous fantams. Se mesure en lignes éditées, pas en réussite ou échec.
