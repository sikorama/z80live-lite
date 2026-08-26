# La mise en forme préserve les lignes et refuse de deviner

Le beautify est une passe texte vers texte, indépendante du préprocesseur et de
l'assembleur, tenue par trois règles :

1. **Bijection sur les lignes.** La ligne *n* de l'entrée devient la ligne *n* de
   la sortie. Jamais de scission, jamais de jointure.
2. **Refus de deviner.** Le deux-points n'est ajouté qu'à un label **seul sur sa
   ligne**. Un premier mot suivi de quoi que ce soit est laissé intact.
3. **Rien de paramétrable.** Quatre espaces, pas de largeur configurable, pas de
   directive dans le source, pas d'option d'invocation pour choisir les règles.

## Contexte

L'assembleur signale deux écarts de mise en forme : un label écrit sans son
deux-points, et une instruction laissée en colonne 1. Signaler sans savoir
corriger fait porter à l'auteur un travail purement mécanique — d'où cette
passe. Mais une passe qui réécrit du source est une passe qui peut le détruire,
et chacune des trois règles ferme une porte précise.

**La bijection** protège la provenance. Un diagnostic se recale dans l'éditeur
par un décalage de lignes ; une source déroulée porte le fichier et la ligne
d'origine de chacune de ses lignes. Une mise en forme qui scinde `start: ld a,1`
en deux lignes décale tout ce qui suit et rompt ce lien — au moment même où
l'auteur en a le plus besoin, puisqu'il lit une vue mise en forme pour
comprendre une erreur. Le prix est assumé : la sortie n'est pas canonique. Deux
sources qui ne diffèrent que par le regroupement label-instruction restent
différentes après mise en forme.

**Le refus de deviner** protège les appels de macro. Sur une source non
déroulée, `sprite 4,12` en colonne 1 est un appel de macro, mais rien dans le
texte ne le distingue d'un label suivi d'une directive inconnue : les macros ne
sont connues qu'après leur collecte, et un `include` peut les apporter. Ajouter
un deux-points à `sprite` produirait un source qui ne s'assemble plus. La règle
« label seul sur sa ligne » traverse ce cas sans avoir à trancher, parce que la
forme que les avertissements visent — un nom sur sa ligne, l'instruction en
dessous — est précisément celle où le reste de la ligne est vide. Conséquence
directe : le beautify **n'éteint pas tous** les avertissements. Celui de
`sprite 4,12` survit, et c'est le bon résultat — là, l'assembleur a raison de
douter.

**L'étendue de la règle 2.** L'indentation couvre les instructions **et les
directives**, alors que l'avertissement de l'assembleur ne parle que des
instructions. Ce n'est pas une règle de plus, c'est la même règle appliquée à
tout ce qui est du code : une sortie où `ld a,1` serait indenté mais `org
#8000` resterait en colonne 1 se lirait comme un bug de la mise en forme. Le
choix inverse aurait été d'élargir l'avertissement aux directives, mais il
ferait crier tous les en-têtes rasm (`BUILDSNA`, `BANKSET`, `ORG`, `RUN` en
colonne 1) et noierait le signal que l'avertissement existe pour porter.

**Le rien-de-paramétrable** est l'ADR 0004 appliqué à l'entrée plutôt qu'à la
sortie. Chaque règle ajoutée est une règle que quelqu'un voudra désactiver, et
chaque paramètre exige un endroit où le mettre — un cran de plus vers la
directive de mise en forme dans le source. Les deux règles retenues ont
l'avantage rare d'être déjà justifiées par un avertissement existant. La casse
des mnémoniques ne l'est pas : rien ne dit aujourd'hui si `ld` ou `LD` est
correct, et le beautify n'est pas l'endroit où en décider.

## Conséquences

Le beautify vit dans son propre module du cœur, sans état ni entrées-sorties,
et prend un texte pour rendre un texte. Il ne dépend pas de l'assemblage : les
deux seules décisions qu'il prend se lisent dans le parseur, sans adresse ni
octet. Il tourne donc dès que le préprocesseur aboutit — y compris quand
l'assemblage échoue, cas où la source déroulée est le plus utile.

Il s'applique aux deux entrées, avec la phase pour seule différence — et la
phase désigne **qui va lire le texte**, non d'où il sort :

- la source d'origine, celle du tampon de l'éditeur, est le texte que le
  **préprocesseur** va lire : ses mots-clés y sont vivants. Au mauvais cran,
  `MEND` seul sur sa ligne n'est pas réservé, donc lu comme un label seul, donc
  pourvu d'un deux-points — et le source est détruit. Le cas a été trouvé à
  l'essai, pas au tableau ;
- la source déroulée est le texte que l'**assembleur** va lire : les mots-clés du
  préprocesseur n'y sont plus. Les y traiter comme réservés ferait indenter un
  label nommé `read` au lieu de lui donner son deux-points.

Cela exige un vocabulaire de mots réservés indexé par phase, là où trois copies
de la même liste et de la même fonction d'épluchage de label coexistaient
(`parser.cpp`, `asm.cpp`, `pp.cpp`) — refactor que cette décision rend
nécessaire plutôt qu'optionnel. Les trois listes formaient une inclusion
stricte : ce ne sont pas des copies qui ont dérivé, ce sont trois phases, et
c'est ce que le paramètre nomme.

Deux points d'entrée l'exposent, parce que les deux entrées ne se ressemblent
pas : `--beautify` met en forme un source sans le préprocesser ni l'assembler —
c'est ce que le bouton de l'éditeur appelle —, tandis que `-E` met en forme la
source déroulée qu'il produit déjà. Un `-E` qui ne déroulerait rien serait un
contresens, d'où le drapeau séparé plutôt qu'une option de `-E`.

Trois propriétés sont tenues par les tests, dans cet ordre de gravité : les
octets assemblés sont inchangés ; la mise en forme est idempotente ; les deux
avertissements s'éteignent, aux exceptions de la règle 2 près — un test vérifie
justement qu'un avertissement **survit** sur `sprite 4,12`.

La source déroulée étant mise en forme par définition, le préprocesseur perd son
propre avis sur le sujet : ses sous-lignes, jusqu'ici indentées d'une
tabulation, le sont de quatre espaces comme le reste.

Cette décision ne couvre pas l'alignement des commentaires, la casse des
mnémoniques ni l'espacement des opérandes. Les ajouter plus tard reste possible,
mais chacun devra d'abord exister comme avertissement — c'est ce qui distingue
une correction d'un goût.

## Amendement — la préservation des lignes ne survit qu'en option

Le titre de cet ADR est désormais inexact pour le beautify lui-même. Sa règle 3
(ADR 0017) détache « label: instruction » en deux lignes, par défaut, et rompt
donc la bijection.

Ce qui a rendu la décision possible est que la justification écrite ici était
fausse. Cet ADR affirmait que la bijection protégeait « la provenance et le
recalage des diagnostics ». Elle ne protège ni l'un ni l'autre : l'assembleur
consomme `pp::Result::lines`, chaque ligne portant son fichier et son numéro
d'origine (`asm_main.cpp`), et le beautify n'est **jamais** dans le chemin
d'assemblage. En mode `--beautify` il produit un fichier que l'auteur réassemble
ensuite ; en mode `-E` il met en forme un texte qu'on écrit et qu'on ne relit pas.

Le seul consommateur réel était le curseur de l'éditeur, qui retrouvait sa ligne
par son numéro. Il corrige maintenant le décalage en comptant les labels collés
au-dessus de lui.

La préservation des lignes reste vraie, et testée, sous `--no-detach-labels`.

## Amendement — refuser de deviner n'est pas refuser de regarder

La règle 2 telle qu'elle est énoncée plus haut — « le deux-points n'est ajouté
qu'à un label **seul sur sa ligne** » — détruisait du source. Elle repose sur
cette phrase, qui est fausse :

> la forme que les avertissements visent — un nom sur sa ligne, l'instruction en
> dessous — est précisément celle où le reste de la ligne est vide.

Cette forme n'est pas propre au label. Un **appel de macro sans argument** est
lui aussi un nom seul sur sa ligne. L'ADR n'avait examiné que `sprite 4,12`, en
avait tiré la règle « seul sur sa ligne », et n'avait pas remarqué qu'une macro
peut n'avoir aucun argument. Sur ce source :

```
macro fill_screen
    ld hl,#c000
endm
    fill_screen
```

le beautify écrivait `fill_screen:`, changeant l'appel en label — c'est-à-dire
en rien. Le source s'assemblait toujours, et ne faisait plus rien. C'est
exactement le mode de défaillance que la règle 2 existait pour empêcher.

Symétriquement, la règle était trop timide dans l'autre sens : `pcoltab dw
coltab` était laissé intact alors qu'il est **décidable** — `dw` est un mot
réservé, et aucun appel de macro ne commence par un mot réservé.

**La correction ne change pas le principe, elle change ce qui compte comme une
devinette.** Lire le texte à la recherche d'une information qu'il porte n'est
pas deviner. Le beautify collecte donc, avant de mettre en forme, les noms de
macro définis dans le texte — sous les deux graphies que le préprocesseur
accepte, `macro nom` et `nom macro`. Puis, pour un premier mot écrit sans
deux-points :

| Situation | Décision | Fondement |
|---|---|---|
| Le nom est une macro connue | intact | preuve tirée du texte |
| `nom EQU v`, `nom = v`, `nom MACRO p` | intact | formes canoniques, exclues aussi par `asm.cpp` |
| Nom **seul**, en colonne 1 | reçoit son `:` | convention que l'assembleur fait déjà respecter |
| Nom **seul**, indenté | intact | ce peut être une macro venue d'un `include` |
| Nom + **mot réservé** | reçoit son `:`, puis règle 3 | aucun appel de macro ne commence par `dw`, `ld`… |
| Nom + autre chose | intact | `sprite 4,12` reste indécidable |

Deux conséquences assumées.

**Le prescan ne suit pas les `include`.** C'est la limite que cet ADR invoquait
pour ne rien faire du tout. Elle ne justifie pas l'inaction : elle justifie de ne
pas s'y fier seule. La règle de colonne la couvre — l'assembleur avertit déjà que
« only labels/symbols should start in column 1 » (`asm.cpp`), si bien qu'un nom
indenté et seul n'est pas un label selon la convention du projet lui-même.

**Un label indenté ne reçoit plus son deux-points.** C'est une régression réelle,
et le prix consenti. Elle prolonge une conséquence que cet ADR acceptait déjà —
le beautify n'éteint pas *tous* les avertissements. L'échange n'est pas
symétrique : un avertissement laissé debout coûte une correction à la main, un
appel de macro détruit coûte une séance de débogage sur du code qui s'assemble
sans rien faire.

Le nombre de règles ne change pas. C'est toujours trois.

## Amendement — règle 4 : la colonne 1 aux labels, un cran par bloc

Le beautify indente le corps de chaque bloc — `repeat`, `while`, `for`, `if`,
`macro`, `struct` — d'un cran de plus par niveau, et place tout **label** en
colonne 1 quelle que soit sa profondeur. `--no-indent-blocks` l'éteint, au même
titre et pour la même raison que `--no-detach-labels` : c'est un style, pas un
canon.

Ce n'est pas une règle de goût de plus glissée dans une passe qui en refusait.
Les deux moitiés se justifient séparément, et la seconde est structurelle.

**L'indentation des blocs** est le seul cas où la largeur d'indentation porte une
information plutôt qu'une préférence. Trois `repeat` imbriqués écrasés au même
cran ne se lisent pas ; c'est un défaut de lisibilité que l'assembleur ne
signale pas et ne peut pas signaler, ce qui la met hors du critère « chaque
règle doit d'abord exister comme avertissement » posé plus haut. Cet ADR
maintient ce critère pour ce qui relève du goût — casse des mnémoniques,
colonne des commentaires — et l'écarte ici parce que la profondeur d'un bloc est
un fait du programme, pas une opinion sur son apparence.

**La colonne 1 pour les labels** est ce qui rend la règle 1 fondée. L'amendement
précédent s'appuie sur la convention « seul un label commence en colonne 1 »
pour décider qu'un nom indenté et seul n'est pas un label. Tant que le beautify
se contentait de l'observer, il pariait sur une discipline de l'auteur. En la
faisant respecter, il la transforme en invariante de sa propre sortie : après un
passage, tout label est en colonne 1, donc un nom indenté n'en est pas un. La
règle 1 et la règle 4 se tiennent l'une l'autre.

Conséquence à connaître : un label dans un corps de macro ou de boucle sort du
bloc visuellement. `fslp0:` s'écrit en colonne 1 pendant que le corps qui
l'entoure est indenté. C'est voulu — c'est le prix de l'invariante — et c'est
aussi ce que fait l'assembleur lui-même, dont le message dit « only
labels/symbols should start in column 1 ».

**Une définition est du code.** `v = v - 1` dans un `repeat` s'indente avec le
corps. Le mot « symbols » de l'avertissement vise l'endroit où l'on déclare
quelque chose de global ; dans une boucle, une réaffectation est une étape de
calcul, et la lire comme du code est ce qui la montre comme faisant partie du
corps.

**Les espaces de fin de ligne sont retirés partout.** Ils ne changent aucun
octet, aucune règle ne peut les vouloir, et les laisser aurait fait dépendre le
résultat de la façon dont l'auteur a terminé sa ligne.

Ce que la règle 4 ne touche pas : l'alignement des commentaires, qui reste celui
de l'auteur, et les lignes vides.

## Amendement — la mise en forme pose les parenthèses d'appel

Le beautify écrit `fill_screen()` là où il lit `fill_screen`, quand il sait que
`fill_screen` est une macro. Il connaît les macros de deux sources : le balayage
textuel du tampon, et la table du préprocesseur — qui a lu les `include`.

C'est une réécriture de code, pas une mise en forme, et elle a donc besoin du
critère que cet ADR s'impose : **chaque règle doit d'abord exister comme
avertissement**. Il existe depuis l'ADR 0018 (« appelée sans parenthèses »), et
c'est lui, non le confort, qui l'autorise ici. Le nombre d'octets ne bouge pas :
c'est la même expansion.

**Le préprocesseur tourne, mais on ne garde que sa table de macros.** Ses erreurs
sont ignorées. La propriété écrite plus haut — la mise en forme tourne même sur un
source qui ne s'assemble pas — n'est pas négociable : c'est au milieu d'une saisie,
sur du code à moitié écrit, qu'on presse le raccourci. Un outil qui se retire dès
que le code est cassé se retire quand il sert. Quand le préprocesseur échoue, on
retombe sur le balayage textuel, qui est sûr.

**Ce qu'il ne fait pas** : inventer des parenthèses sur un nom qu'il ne connaît
pas. `sprite 4,12` reste intact tant qu'aucune définition ne l'explique.

## Note — `--normalize` n'est pas de la mise en forme

Le reste de cet ADR vaut pour le **beautify**.

`--normalize` (ADR 0017) est un outil **distinct**, pas une option du beautify :
il canonise sans dérouler, et il change délibérément le nombre de lignes —
`push hl,de` devient deux lignes, `ld a,1:inc a` aussi. Il ne contredit pas la
préservation des lignes énoncée ici, il n'y est pas soumis.

Ce que les deux partagent, en revanche, est le refus de deviner et l'égalité des
octets assemblés.
