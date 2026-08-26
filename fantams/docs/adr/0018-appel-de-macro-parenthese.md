---
status: accepted
---

# L'appel de macro parenthésé : la seule graphie qui se lise sans contexte

`nom(args)` est un appel de macro. La parenthèse ouvrante est **collée** au nom
et la fermante est le **dernier** caractère de l'instruction. `nom()` appelle une
macro sans argument. La forme nue `nom args` reste licite, et l'assembleur
avertit une fois par macro qu'elle est ambiguë.

## Contexte

Un appel de macro écrit nu est indiscernable d'un label :

```
    fill_screen        ← appel, ou label sans son deux-points ?
```

Rien dans le texte ne tranche. Le préprocesseur, lui, tranche toujours : il a
collecté les macros avant d'expanser. Mais la mise en forme (ADR 0013) n'a pas ce
luxe, et la question a coûté un bug qui détruisait des sources — le beautify
écrivait `fill_screen:` et l'appel disparaissait, sur un fichier qui continuait
de s'assembler.

Trois parades ont été essayées, et **aucune ne couvre le cas complet** :

| Parade | Ce qu'elle rate |
|---|---|
| Prescan textuel des `macro nom` | les `include` |
| Table de macros du préprocesseur | une macro **pas encore écrite** |
| Convention « colonne 1 ⇒ label » | un appel écrit en colonne 1 |

La troisième ligne est celle qui décide. Une mise en forme qu'on invoque au fil de
la frappe s'exécute sur du code incomplet — c'est même sa raison d'être. Aucun
balayage, si complet soit-il, ne peut lire une macro que l'auteur n'a pas encore
tapée. La seule sortie est que l'**appel se déclare lui-même**.

## La forme

**La parenthèse est collée.** `sprite (4),12` garde son sens d'appel nu dont le
premier argument est parenthésé ; le même appel en forme parenthésée s'écrit
`sprite((4),12)`. Tolérer l'espace ferait lire le premier comme un appel à un seul
argument `4` suivi d'un `,12` orphelin — une ambiguïté introduite par la syntaxe
censée en supprimer une.

**La fermante est le dernier caractère.** C'est la seconde moitié de la même
garantie : elle est ce qui rend `((5),6)` lisible sans compter sur une heuristique.

**`nom()` existe.** L'appel sans argument est le cas qui a motivé cet ADR — le seul
dont l'échec est silencieux, puisqu'un `sprite 4,12` mal lu produit au moins un
`4,12` qui ne s'assemble pas. Une syntaxe qui ne le couvrirait pas manquerait sa
cible.

**La définition l'accepte aussi** : `macro nom(p,q)` autant que `macro nom p,q`.
Elle n'a rien à désambiguïser — `macro` est un mot réservé — et c'est donc de la
symétrie, assumée comme telle. La graphie alternative `nom MACRO p,q` garde sa
liste nue : `nom(p,q) MACRO` ne se lit pas, et rendrait la détection du mot-clé en
seconde position dépendante d'un équilibrage de parenthèses.

## La forme nue reste licite, et avertie

La rompre casserait toute source rasm, ce que le projet ne fait pas sans l'écrire.
Mais elle porte le défaut que cet ADR existe pour corriger, d'où un avertissement.

**Une fois par macro, pas par site.** Un appel dans un `REPEAT` est traversé à
chaque tour : « compter les appels » n'a pas de sens. Et un source importé avec
deux cents appels nus produirait deux cents lignes, noyant le signal — le mode
d'échec qu'ADR 0013 redoute explicitement pour les avertissements de mise en
forme.

Cet avertissement est ce qui autorise le beautify à poser les parenthèses lui-même
sur les macros qu'il connaît : la règle qu'il s'impose est de n'éteindre que des
avertissements existants, jamais d'imposer un goût.

## Conséquences

La mise en forme n'a plus besoin de deviner sur un appel parenthésé, quel que soit
son état de connaissance : `nom(` n'est jamais un label, point. Les trois parades
du tableau restent en place et gardent leur utilité pour la forme nue — elles ne
sont plus le dernier recours.

Cet ADR ne dit pas que la forme nue est déconseillée, seulement qu'elle est
ambiguë pour un lecteur sans contexte. Si le projet décide un jour de la
déconseiller, ce sera une autre décision, avec son propre coût de compatibilité.
