---
status: accepted
---

# Arrondi à l'écart de zéro, divergence délibérée avec rasm

`toInt()` arrondit les demis **à l'écart de zéro** : `3.5 → 4` et `-3.5 → -4`.
rasm arrondit vers `+∞` (`3.5 → 4`, `-3.5 → -3`) et fantams reproduisait ce
comportement. C'est une divergence assumée, et le seul endroit du projet où nous
nous écartons sciemment de la sortie de rasm. Une égalité effectivement atteinte
**émet un avertissement**.

## Contexte

`expr.cpp` implémentait `int64_t toInt(double v) { return floor(v + 0.5); }`,
vérifié empiriquement contre rasm (`db 7/2 → 4`, `db -7/2 → -3`). L'asymétrie
autour de zéro qui en résulte est contre-intuitive : les positifs s'éloignent de
zéro, les négatifs s'en rapprochent.

`floor(v + 0.5)` et `ceil(v - 0.5)` sont le même mécanisme dans deux directions —
l'un pousse tout vers `+∞`, l'autre tout vers `-∞`. **Aucun des deux n'est
symétrique** : passer à half-down aurait retourné l'asymétrie sans la supprimer.
Seul l'arrondi à l'écart de zéro la fait disparaître, et c'est aussi celui qu'on
apprend à l'école.

## Ce que la mesure a établi

Deux binaires — l'actuel et une variante — rejoués sur les 84 sources aujourd'hui
identiques à rasm au bit près : **zéro divergence**. Un troisième binaire,
instrumenté pour tracer chaque valeur tombant exactement sur `.5`, n'a émis
**aucune trace** sur ces 84 sources.

Ce n'est pas faute d'arithmétique flottante : 28 des 84 contiennent une division
et 6 utilisent `SIN`/`COS`. Mais les expressions réelles se répartissent en deux
camps qui n'atteignent jamais l'égalité — des divisions exactes (`ld d,222/2`,
`ld h,tabint/256`, `equ ({_cycles}-1)/4`, où l'auteur choisit un diviseur qui
tombe juste) et des résultats trigonométriques irrationnels, qui n'atteignent un
`.5` binaire exact qu'avec une probabilité nulle.

Autrement dit, l'égalité est un artefact de littéraux écrits à la main, pas
quelque chose que le code de démo produit naturellement.

## Conséquences

Le corpus n'offre et n'offrira **aucune protection contre les régressions** sur ce
point : ajouter des sources réelles n'y changerait rien, puisque le cas ne se
produit pas. La protection ne peut venir que des tests unitaires. Deux assertions
d'`expr_test.cpp` — `arrondi positif .5 -> +1` et `arrondi négatif .5 -> vers 0` —
sont à réécrire délibérément : elles décrivent une intention, pas un constat, et
ce sont les seules du projet à échouer sous la nouvelle règle.

La portée réelle du changement est étroite. `div` couvrant désormais la division
entière, la règle de départage ne gouverne plus que les résultats véritablement
fractionnaires — sorties de `sin`, de `cos`, de `floor`. Le cas `7/2`, qui motivait
l'essentiel de la gêne, se traite maintenant par `7 div 2`.
