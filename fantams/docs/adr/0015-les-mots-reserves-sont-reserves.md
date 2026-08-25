---
status: accepted
---

# Les mots réservés sont réservés

Aucun identifiant utilisateur ne porte un nom du vocabulaire du langage ou de la
machine. La règle vaut dans les quatre positions — paramètre de macro, index de
boucle, nom de symbole, label — et le deux-points n'en affranchit pas. Les noms
de registres et de conditions entrent dans le vocabulaire réservé.

## Contexte

La règle n'existait ni dans son périmètre, ni dans ses positions.

`isReservedWord` (`keywords.h:40`) couvre les mnémoniques et les directives, par
phase. Les **registres et conditions** vivaient dans un ensemble privé de
`pp.cpp:414`, invisible du reste du projet, alors qu'ils décrivent le vocabulaire
de la machine — exactement la duplication que l'en-tête de `keywords.h` dit avoir
supprimée pour les labels. Conséquence : `hl equ 5` et `c = 7` passaient.

Et un mot réservé **suivi de `:`** pouvait être accepté comme label : `peelLabel`
rend le label sans consulter la liste dès qu'il y a deux-points. La liste ne
servait qu'à la forme sans `:`. L'assembleur seul — celui qu'exerce `asm_test`,
sans préprocesseur — définissait donc bien un label nommé `call` sur `call: nop`.

Le cas qui a forcé la décision est celui du bug muet. Un compteur ou un
paramètre nommé `c` est substitué dans `db c` et **ne l'est pas** dans `ld a,c`,
où la protection des noms de registres (`pp.cpp:414`) le laisse lire comme un
registre. Deux lectures du même nom sur deux lignes voisines, et rien dans la
ligne ne dit laquelle s'applique. Le code s'assemble et fait autre chose.

Le cas est déjà présent aujourd'hui avec un nom très répandu : `I` **est** un
registre du Z80, et `repeat 3,i` est l'index le plus courant qui soit.

## Pourquoi une règle unique plutôt qu'une gradation

Une gradation était défendable : erreur sur les registres et conditions, où le
bug est muet, avertissement sur les mnémoniques et les directives, où
`macro m call` est laid mais sans effet — un paramètre n'apparaît jamais en tête
d'instruction, `operandStart` écartant le premier mot.

Elle a été écartée pour une raison qui n'est pas technique : une phrase vraie
partout coûte moins cher à retenir que trois règles justes par position. C'est
tout le bénéfice de la décision, et il disparaît au premier « sauf que ».

Le même raisonnement condamne l'exception qu'on aurait pu accorder à `END`, dont
l'homonymie avec un label est massive : `end:` devient illégal comme le reste.
L'argument de fréquence se retourne d'ailleurs — plus `end:` est répandu, plus la
collision entre un label et un mot de fermeture de bloc est probable, donc plus
le refus est utile. Le diagnostic nomme le remplacement, comme le font déjà
`BANK` et `STR`.

## Conséquences

L'ensemble des registres et conditions quitte `pp.cpp` pour `keywords.h`, où il
rejoint le vocabulaire réservé. Il n'est pas indexé par phase : un registre est
un mot de la machine à toutes les phases.

Le deux-points n'affranchit plus, mais par **deux mécanismes distincts**, et il
est utile de savoir lequel s'applique.

Pour un mnémonique, une directive ou un mot-clé du préprocesseur, le
préprocesseur ne lisait déjà pas `call:` comme un label : `splitStatements`
(`pp.cpp:172`) traite ce deux-points comme un **séparateur d'instructions**, ce
qui est licite et idiomatique — `ei: ret` vaut `ei` puis `ret`. Aucun label nommé
`call` ne pouvait donc naître par ce chemin ; ce qui manquait était le
diagnostic, qui parlait de style là où il y a une règle. Il le dit désormais.

Pour un registre ou une condition, le chemin est l'autre : `isReservedWord`
répond à la question « ce premier mot est-il un label ? » et n'inclut
volontairement **pas** les mots de la machine, si bien que `hl: nop` est bien
épluché comme un label — puis refusé à la définition, avec le message exact. Les
y ajouter aurait renvoyé `hl:` vers le séparateur d'instructions et dégradé le
diagnostic en « unknown directive 'hl' ».

Le refus lui-même vit donc, pour les symboles et les labels, dans l'assembleur
— seul étage qui les définisse — et pour les paramètres de macro et les index de
boucle dans le préprocesseur, seul étage qui les connaisse. Un fait, un étage :
aucune position n'est diagnostiquée deux fois.

C'est un abaissement du plafond de compatibilité, et il n'est pas chiffré : le
corpus n'était pas à portée au moment de la décision. `end:`, `call:` et tout
symbole nommé d'après un registre devront être édités. Le nombre de textes
distincts concernés reste à mesurer, et cette mesure est due — elle ne remet pas
la règle en cause, elle en donne le prix.

En contrepartie, la règle rend **décidables** plusieurs choses qui ne l'étaient
pas. La normalisation des orthographes de fermeture par le beautify (ADR 0017)
n'est sûre que parce qu'aucun identifiant utilisateur ne peut plus s'appeler
`wend` : le mot en tête de ligne est nécessairement le mot-clé. Sans cette règle,
cette normalisation serait indécidable.
