---
status: accepted
---

# Les couches et le canon : ce qui est forcé, ce qui est choisi

Deux choses seulement forcent une réécriture dans le préprocesseur : produire du
texte source à ré-analyser, et exiger une valeur avant qu'aucune adresse existe.
Tout le reste est une politique. Parmi ces choix libres, la **fidélité de la
source déroulée** décide : ce qui devient plusieurs instructions est canonisé par
le préprocesseur, ce qui reste une orthographe peut rester à l'assembleur.

## Contexte

La question s'est posée à propos du sucre et des syntaxes héritées : `push hl,de`
est déplié par le préprocesseur, `defb` est accepté par l'assembleur — pourquoi
pas l'inverse ? Et faut-il retirer les alias de l'assembleur pour obtenir une
couche d'assemblage épurée ?

Une première réponse invoquait les adresses : une réécriture qui change le nombre
d'octets émis devrait vivre avant elles. **Elle est fausse.** `push hl,de` rend
deux octets, et ce compte est déterminé par la ligne source ; aucune adresse n'est
en jeu. La preuve est dans le code : `emitDB` (`asm.cpp:332`) émet déjà N octets à
partir d'une ligne, dans l'assembleur, sans difficulté. Émettre deux opcodes
depuis une ligne, c'est deux appels à `emit`.

Il ne faut donc pas justifier un placement par une contrainte qu'on n'a pas
vérifiée. Une fois celle-ci écartée, le périmètre de ce qui est réellement forcé
se réduit à deux cas : produire du **texte source à ré-analyser** — macros,
boucles, includes — et exiger une **valeur avant toute adresse** — le contrôle de
flux, frontière déjà posée par l'ADR 0003. `push hl,de`, `ld de,hl` et `defb`
n'entrent dans aucun des deux. Ce sont tous des choix libres, et `push hl,de` est
au préprocesseur par commodité d'écriture textuelle, non par nécessité.

## La source déroulée juge le placement

Le critère de remplacement ne peut donc pas être technique : il repose sur une
valeur que le projet a déjà érigée en livrable. La source déroulée doit être un
programme Z80 à une instruction par ligne, où l'auteur lit exactement ce qui sera
assemblé.

Une réécriture **un-vers-plusieurs** est donc canonisée par le préprocesseur,
pour que `-E` reste fidèle. Une réécriture **un-pour-un** — une orthographe — ne
met pas cette fidélité en jeu et peut rester à l'assembleur. En une phrase :
l'assembleur tolère des orthographes, jamais des structures.

Ce critère valide les deux placements existants. Le code avait raison, sa
justification était fausse.

## Le canon

Une ligne qui porte un opcode n'en porte qu'un, et cet opcode est du Z80 standard
sous son orthographe canonique. Un label n'est pas un opcode et n'entre pas dans
le compte : `boucle: ld a,1` est canonique.

Le canon est une **structure**, jamais un goût. L'indentation, la casse et le
détachement des labels n'en relèvent pas : ce sont des styles, donc des options.

## La canonisation est la dernière étape

Elle n'intervient qu'en aval de toute substitution. `emit` (`pp.cpp:551-559`)
applique `splitStatements` puis `expandPushPop` sur le résultat de
`substituteVars`, lui-même postérieur aux `{...}`. C'est ce qui répond au cas
d'un argument de macro **valant** `ld hl,de` et utilisé en `{x}` : la ligne émise
est `ld hl,de`, et la canonisation la voit. Aucune seconde passe n'est nécessaire.

Cette propriété est garantie par une contrainte sur la table de canonisation :
une règle de canonisation ne produit que du Z80 canonique — jamais de directive
de préprocesseur, jamais de substitution, jamais de sucre. Un sucre qui ne s'y
plie pas n'entre pas dans la table. Une canonisation itérative jusqu'à point fixe
a été écartée : elle peut ne pas converger, et elle rendrait la source déroulée
dépendante d'un ordre d'application illisible.

## L'invariant des outils de forme

**Aucune source ne doit être assemblable seulement après passage par un outil de
mise en forme.** Un outil cosmétique devenu obligatoire est une étape de
compilation déguisée, et la garantie qui le rendait sûr — ne rien changer au
programme — disparaît. Tout ce qu'un outil de forme canonise, la chaîne
d'assemblage doit déjà l'accepter.

C'est ce qui interdit de retirer les alias de l'assembleur pour les confier au
beautify, et c'est ce qui rend l'arbitrage entre tolérance et pureté inutile.

## `--normalize`

Normaliser, c'est canoniser sans dérouler : les orthographes non canoniques
— fermetures de bloc, alias de directives — et les opcodes composés, `push hl,de`
comme plusieurs opcodes sur une ligne. Le fichier de l'auteur est réécrit, ses
macros et ses boucles sont conservées.

Il change donc le nombre de lignes, ce qui le distingue du beautify, et il n'est
pas une option de celui-ci : c'est un outil indépendant, composable avec lui.

`--normalize` et `-E` sont **incompatibles** : `-E` fait déjà normalize *plus* le
déroulage. Deux sorties différentes, comme le refus existant de `-E` avec
`--beautify`.

Sa promesse est vérifiable, et c'est ce qui en fait un outil : il est idempotent,
et `assemble(normalize(src))` rend les mêmes octets que `assemble(src)`.

Une ligne portant un `{...}` dans un corps de macro non déroulé échappe au canon :
sa forme dépend du site d'appel, et il peut y en avoir plusieurs. Ce n'est pas
signalé. La promesse a été **rétrécie** — plus d'orthographe obsolète, un opcode
par ligne — plutôt que trahie, et `-E` répond en une commande à ce que ces lignes
deviennent.

La conversion d'un format d'assembleur vers un autre est un outil distinct, et
elle ne peut pas être portée par le préprocesseur, qui déroule.

## Le beautify

Indentation remplacée par quatre espaces (`beautify.cpp:10`, à rendre
paramétrable plus tard), deux-points ajouté au label seul sur sa ligne, et
**labels détachés de l'instruction par défaut**, avec `--no-detach-labels` pour
s'en dispenser.

Le détachement est un style, non un canon, mais il est le défaut pour une raison
d'alignement : l'indentation fixe aligne tous les opcodes, un label de longueur
variable ne les aligne pas. Une transformation par défaut doit rester
indiscutable, et le critère est « quelqu'un peut-il légitimement ne pas le
vouloir ? » — d'où l'option.

Il rompt la bijection sur les lignes que l'ADR 0013 disait porteuse. Vérification
faite, elle ne l'était pas : l'assembleur travaille sur `pp::Result::lines` et
leur provenance, jamais sur un texte mis en forme, et le seul consommateur du
numéro de ligne était le curseur de l'éditeur — qui corrige le décalage en
comptant les labels collés au-dessus de lui. L'ADR 0013 est amendé en ce sens.

## Le mode strict

Puisque le placement des choix libres ne protège pas la pureté de la couche
d'assemblage, c'est un mode qui le fait. Le laxisme reste le défaut, compatible ;
la garantie s'obtient en la demandant. C'est le geste de `LET` face à `=` (ADR
0003) : le mode strict est le `LET` du vocabulaire.

Il porte sur le **pipeline**, pas sur une couche. L'utilisateur qui le demande
demande « ma source est-elle du Z80 canonique ? », pas « la couche interne n° 3
a-t-elle vu du canon ? ».

Il est en revanche porté **entièrement par le préprocesseur**, et non réparti
entre les deux étages comme prévu d'abord. La raison est une conséquence de la
décision précédente : puisque la canonisation des orthographes a lieu dans
`emit()`, l'assembleur ne voit **jamais** `defb` à travers le pipeline — il reçoit
déjà `db`. Un refus des alias porté par l'assembleur serait inatteignable pour
qui passe par la chaîne complète, c'est-à-dire pour tout le monde. Le
préprocesseur est le seul étage qui voie la source **telle qu'elle est écrite**,
donc le seul qui puisse refuser une orthographe.

L'assembleur garde donc sa tolérance sans drapeau : elle ne concerne que le
chemin direct, celui qu'exerce `asm_test`. Lui ajouter un mode strict aujourd'hui
créerait un second garde-fou inatteignable, et l'un suffit.

Les sources du projet — exemples et cas de référence — sont assemblées en mode
strict. Un mode que le projet ne lance pas sur ses propres sources ne défend
rien : il passera dès le premier jour, puisqu'elles sont déjà canoniques, et il
échouera le jour où quelqu'un y écrira `defb`.

## Conséquences

La **politique des alias** reste différée : l'état actuel — l'assembleur accepte
neuf orthographes pour trois directives (`asm.cpp:410-412`) — est la réponse
laxiste par défaut, et le mode strict rend son maintien sans coût sur la pureté.
La renverser plus tard serait un changement mécanique confiné à ces lignes et à
leurs tests, avec un prix connu : `asm_test.cpp` exerce l'assembleur **sans**
préprocesseur, donc tout test écrit en `defb` tomberait.

Une facilité comme `ld de,hl` est un-vers-plusieurs : elle va au préprocesseur,
dans la table de canonisation, à côté de `expandPushPop`.

Les trois propriétés à tester pour toute transformation de forme sont
l'idempotence, la préservation du nombre de lignes — pour le beautify seul, pas
pour `--normalize` — et l'égalité des octets assemblés, déjà exercée en
`beautify_test.cpp`.
