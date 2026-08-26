---
status: accepted
---

# Arithmétique des expressions : un seul type, aucun mode

`expr` calcule en `double` du début à la fin, et ne convertit en entier qu'au
moment d'émettre. Il n'existe pas d'arithmétique entière contextuelle : une même
expression donne la même valeur quel que soit l'endroit où elle est écrite. Les
besoins entiers se servent par des opérateurs et des fonctions explicites.

## Ce qui est ajouté

**`div`** — division entière. `7 div 2` vaut 3, sans dépendre d'une règle
d'arrondi. Le symbole `//` n'est **pas** retenu pour cet usage : il est réservé au
commentaire de ligne, que quatre sources du corpus utilisent déjà et que tout
lecteur attend par réflexe.

**Les alias textuels des opérateurs bit à bit** — `and` `or` `xor` `not` `mod`
`shl` `shr`, en regard de `&` `|` `^` `~` `%` `<<` `>>` qui existent tous déjà.
Ils sont exigés par la compatibilité rasm et mesurés dans le corpus.

Il n'y a **pas d'alias textuel pour `&&`, `||` et `!`**. Les opérateurs textuels
désignent les formes bit à bit ; donner à `and` un sens dépendant de ses opérandes
violerait la règle d'absence d'effet contextuel, et ce serait le pire endroit pour
en introduire un, puisque `and` bit à bit et `and` logique diffèrent sur les mêmes
entrées.

**Les fonctions d'arrondi** — `floor`, `int`, `ceil`, `round`. `floor` seul
compte 99 usages dans le corpus, de très loin le plus gros manque isolé mesuré.
Il est utilisé si souvent précisément parce que la division entière n'existait
pas : qui veut `y/8` sur une adresse doit écrire `floor(y / 8)` pour obtenir une
troncature au lieu d'un arrondi.

**Le confort d'écriture** — `min`, `max`, puissance `**`. Rien dans le corpus ne
les réclame ; ils sont retenus au titre du pouvoir d'expression, non d'un besoin
constaté. `min` et `max` héritent de la règle ci-dessous sur le mélange des types.

La puissance s'écrit `**` : `^` est déjà le ou exclusif. Elle est plus liante que
les unaires — `-2**2` vaut -4 — et associative à droite : `2**3**2` vaut `2**9`.

**Le ternaire `? :` est retiré de cette décision.** Il y figurait, n'a jamais été
implémenté, et il ne le sera pas. Deux raisons, dont la seconde suffit seule :
`:` est déjà le séparateur d'instructions du préprocesseur autant que le suffixe
d'un label, si bien que `1 ? 2 : 3` est coupé en deux avant même d'atteindre
l'évaluateur ; et un `if` dit la même chose plus clairement, dans un langage où
les blocs conditionnels existent déjà au temps préprocesseur.

## Ce qui ne change pas

L'opérateur `/` reste une division flottante. Le faire basculer en entier
changerait silencieusement la sortie de sources qui compilent aujourd'hui —
84 correspondent à rasm octet pour octet — ce qui est le pire type de régression :
invisible à la relecture.

## Mélange flottant et opérateurs entiers

Les opérateurs bit à bit convertissent chaque opérande en entier avant de
calculer. `(7/2) & 0xFF` vaut donc 4 et non 3, et `1.5 << 1` vaut 4 et non 3. La
conversion est silencieuse aujourd'hui ; elle **émet désormais un
avertissement** quand un opérateur entier reçoit un opérande non entier.

C'est la démarche de l'ADR 0003 : on ne restreint pas, on rend visible. Une erreur
casserait des sources qui fonctionnent. Et l'avertissement restera rare, puisque
la manipulation d'adresses — `label & 0x3FFF` — ne fait intervenir que des
entiers : il ne parlera que lorsqu'un flottant s'est glissé là où personne ne
l'attendait.

## Note pour plus tard

`and`, `or`, `xor` et `not` sont aussi des mnémoniques Z80. Il n'y a pas de
conflit : `parser.cpp` sépare le mnémonique des opérandes avant que `expr` ne voie
quoi que ce soit, et `ld a, b and 3` livre bien `b and 3` à l'évaluateur. C'est
écrit ici parce que c'est typiquement le genre de chose qu'on « corrige » par
erreur des mois plus tard.

Les chaînes de caractères restent hors du langage d'expressions. `strlen` et
`char` permettraient de construire les variantes de chaînes en macros
utilisateur plutôt qu'en directives, ce qui serait plus conforme à la discipline
du projet — mais cela demanderait d'introduire un second type dans `expr`, et
produirait une source déroulée d'un `db` par caractère, dégradant l'artefact sur
lequel repose l'ADR 0003. La porte reste ouverte, non franchie : on pousse
l'arithmétique jusqu'au bout d'abord.
