---
status: accepted
---

# Les accolades ne sont jamais un préfixe de format

Nous refusons les notations rasm `{sizeof}`, `{hex}`, `{bin}`, `{char}` et
`{int}`. `sizeof` devient une fonction — `sizeof(nom)` — et les formats
d'affichage de `PRINT` s'écrivent en mot-clé nu devant l'expression :
`print "addr=", hex init_music`.

## Contexte

rasm surcharge les accolades. Dans une même source du corpus, on trouve à la
fois la substitution de variable `{mode}` et le préfixe de format `{hex}` :

```asm
ld bc,{sizeof}state                               ; opérateur : rend une valeur
print 'debut :',{int}init_music,'/',{hex}init_music   ; format : même valeur, deux rendus
```

Une syntaxe, deux rôles grammaticaux distincts — et rien dans la ligne ne dit
lequel s'applique.

**Le comportement de fantams était déjà le bon.** Les échecs
`unknown symbol 'hex'` du corpus ne signalaient pas une fonctionnalité
manquante : le préprocesseur appliquait correctement sa propre grammaire, où
`{X}` veut dire « évalue X ». `hex` n'existe pas, donc erreur. Il n'y avait rien
à corriger — seulement une divergence à assumer et à diagnostiquer clairement.

## Pourquoi `sizeof` peut être une fonction et `hex` non

`sizeof` rend un **nombre** : elle entre dans le langage d'expressions sans rien
y ajouter. La connaissance vit dans le préprocesseur, seul à connaître les
`STRUCT` — après leur abaissement il ne reste que des `EQU` — donc c'est lui qui
réécrit `sizeof(nom)` en littéral.

`hex` rend une **représentation**, pas une valeur : `{int}x` et `{hex}x`
désignent le même nombre affiché autrement. En faire une fonction supposerait
qu'une expression puisse valoir une chaîne, ce que l'ADR 0008 diffère
explicitement. Le format appartient donc à la syntaxe de `PRINT`, pas à celle
des expressions.

`PRINT` n'étant pas implémenté, rien ne contraignait ce choix : le mot-clé nu ne
casse aucune source existante, et il laisse les accolades à leur sens unique à
l'intérieur même de `print`. L'ensemble des mots-clés est fermé — `hex`, `bin`,
`char`, `int` — et ils ne sont reconnus qu'en position d'argument de `PRINT`, si
bien qu'un symbole nommé `hex` ailleurs reste intact.

## Conséquences

Contrairement à `BANK`, `SNASET` ou `STR`, les notations à accolades ne sont
**pas** acceptées avec un avertissement de dépréciation : elles sont une erreur.
Accepter `{hex}` obligerait à réintroduire dans la grammaire l'ambiguïté même
qu'on refuse. Le diagnostic nomme en revanche la forme de remplacement, source
par source.

Environ treize sources du corpus passent donc de « manque à combler » à
« divergence assumée » : elles devront être éditées. C'est un abaissement
délibéré du plafond de compatibilité, décidé en connaissance du chiffre.

Si un jour des opérateurs rendant des chaînes entrent dans le langage
d'expressions, `hex()` deviendra naturellement exprimable et cette décision
pourra être revue. La porte reste ouverte, non franchie.

## Amendement — l'invariant reformulé

Cet ADR s'intitulait « les accolades n'ont qu'un rôle : `{X}` évalue X et
substitue ». La décision est intacte — `{hex}` et `{sizeof}` restent refusés —
mais sa formulation est devenue fausse, et il valait mieux la corriger que la
laisser affirmer un invariant que le langage ne tient plus.

`{arg}`, sur un argument de macro, n'évalue rien : il substitue le **texte brut**
de l'argument, quand la forme nue en substitue la **valeur**, calculée au site
d'appel. La différence est observable — un corps de macro qui réaffecte une
variable entre deux usages voit `5, 5, 5` par la forme nue et `5, 4, 3` par les
accolades — donc elle est porteuse, et une formulation qui la nie ne peut pas
tenir.

Ce que cet ADR refusait n'était pas la multiplicité des sources de substitution,
c'était le **préfixe de format** : `{int}x` et `{hex}x` désignent le même nombre
rendu autrement, et rien dans la ligne ne dit qu'on a changé de registre
grammatical. C'est ce refus qui porte le titre désormais, et il ne souffre
toujours aucune exception.

Conséquence à porter dans le code : les commentaires de `pp.cpp` et de
`expandSizeof` qui citent « un seul rôle grammatical » doivent citer le nouvel
énoncé.
