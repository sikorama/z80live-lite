---
status: accepted
---

# Le préprocesseur lit les constantes et variables d'assemblage ; `LET` devient le niveau strict

Nous abandonnons le modèle « PP strict », qui réservait le contrôle de flux du
préprocesseur aux seules variables déclarées par `LET`. Une constante `EQU` ou
une variable `=` devient lisible au temps préprocesseur dès lors que son
expression y est résoluble, et `=` devient réaffectable comme chez rasm. `LET`
conserve, et devient le seul porteur de, la sémantique stricte : une valeur non
résoluble au temps préprocesseur y est une erreur et non un report.

## Contexte

`pp.h` documentait la contrainte ainsi : « le contrôle de flux (IF/REPEAT/WHILE)
exige des expressions résolubles au préprocesseur. Une référence à un label
temps-assemblage est une erreur. » Concrètement, `evalPP` (`pp.cpp:298-307`) ne
consulte que trois tables — compteurs de boucle, `ppvars` alimentée uniquement
par `LET`, et arguments de macro — et `pp.cpp` ne parse jamais `EQU` ni
`nom = expr`. Cette détection n'existe que dans `asm.cpp:302`, soit après
l'échec du préprocesseur.

La mesure sur le corpus a tranché. Sur 121 sources rasm qui n'assemblent pas,
41 échouent sur ce seul motif, dont le cas canonique répété 13 fois :

```asm
wantloop equ 1     ; L15
if wantloop        ; L84   -> unknown symbol 'wantloop'
```

Neuf autres échouent en `duplicate symbol` sur la non-réaffectabilité de `=`,
alors que `angle = i - 1` dans un `repeat 256,i` est idiomatique chez rasm. Ces
deux symptômes sont une seule et même décision — un espace de noms à phases
étanches contre un espace unique à évaluation paresseuse — et pèsent ensemble
50 sources sur 121.

L'argument décisif n'est cependant pas ce décompte, mais un renversement du
raisonnement. Le risque de l'évaluation paresseuse est l'ambiguïté : savoir
quelle valeur a réellement été vue, et à quel moment. Or c'est précisément ce
qu'un préprocesseur permet de lever, en produisant une **source déroulée**
inspectable — capacité que rasm n'offre pas. L'ambiguïté n'est donc pas un
argument contre l'assouplissement : elle est la raison d'être de l'outil qui la
rend vérifiable. Nous relâchons la contrainte et nous appuyons sur la source
déroulée, complétée d'avertissements, plutôt que sur un refus en amont.

Le mécanisme existe d'ailleurs déjà côté assembleur : `asm.cpp:125-135` réévalue
les `EQU`/`=` jusqu'à point fixe pour gérer les références avant. Cette décision
étend un comportement en place, elle n'en introduit pas un nouveau. De même,
`findAssign()` (`asm.cpp:91-101`) sait déjà repérer un `=` d'assignation hors
chaîne en écartant `==`, `<=`, `>=` et `!=` : le préprocesseur le réutilise.

## Frontière conservée

Le préprocesseur s'exécute avant que la moindre adresse existe. Une expression
touchant un label ne sera donc **jamais** résoluble au temps préprocesseur, et
reste une erreur — non par choix, mais par construction. Ce point est la seule
part du modèle strict qui survit, et il doit être signalé par un diagnostic
propre le disant explicitement, plutôt que par un `unknown symbol` trompeur.

## Conséquences

Deux niveaux coexistent désormais, et le glossaire les nomme : la **variable**
(`=`, paresseuse, réaffectable, compatible rasm) et la **variable de
préprocesseur** (`LET`, stricte, résolution exigée). `LET` cesse d'être le seul
moyen de piloter le préprocesseur pour devenir le moyen d'exiger une garantie.
Une source qui veut l'ancienne rigueur l'obtient en écrivant `LET`.

En contrepartie, le langage gagne deux façons de faire la même chose, et l'ordre
de lecture acquiert une portée sémantique : la valeur vue par un `IF` est celle
de la dernière affectation rencontrée au-dessus de lui. Cette contrepartie exige
que la source déroulée reste fidèle et lisible — elle devient un livrable de
premier plan, pas un artefact de débogage — et appelle des avertissements aux
points d'ambiguïté réelle : variable lue au temps préprocesseur puis réaffectée
plus loin, ou nom résolu à une valeur différente selon la phase.

Enfin, cette décision ne referme pas à elle seule l'écart avec rasm : elle
adresse 41 % des échecs mesurés. Les directives absentes (`BANK`, `SNASET`,
`ASSERT`, `PRINT`) et les préfixes `{hex}`/`{sizeof}` constituent le reste, et
relèvent d'arbitrages distincts — certains touchant à des fonctionnalités dont
nous ne voulons pas.
