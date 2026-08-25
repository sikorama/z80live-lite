---
status: accepted
---

# Les arguments de macro : la forme nue vaut la valeur, les accolades valent le texte

Un nom de paramètre écrit nu dans un corps de macro est substitué par la
**valeur** de son argument, évaluée au site d'appel. `{param}` est substitué par
le **texte brut** de l'argument. Les deux formes restent permises partout : ce
sont deux sémantiques distinctes, pas deux graphies d'une même chose.

## Contexte

Le préprocesseur lisait déjà les constantes et les variables (ADR 0003), si bien
que `repeat high` fonctionne avec `high = 25` écrit nu. Dans un corps de macro,
en revanche, un paramètre nu ne fonctionnait pas : il fallait écrire `{param}`.

L'asymétrie vient de deux chemins distincts. `repeat <expr>` est une expression
**évaluée** par `evalPP`, dont le résolveur consulte quatre tables — compteurs,
`ppvars`, arguments, `asmvars`. Un corps de macro, lui, est **émis** : la
substitution textuelle des lignes émises est faite par `substituteVars`
(`pp.cpp:444`), qui ne consulte que les compteurs et `ppvars`. Ni les arguments,
ni `asmvars`.

Pour `asmvars` l'omission est justifiée et invisible : `db high` n'a pas besoin
de substitution, l'assembleur connaît le symbole. Pour un argument de macro elle
est pénalisante : l'argument n'existe pas côté assembleur, donc s'il n'est pas
substitué au préprocesseur il n'existe nulle part. `{}` (`pp.cpp:367`) était le
seul chemin. Cette règle n'était écrite nulle part, et sa violation ne produisait
aucun diagnostic — seulement un `unknown symbol` plus loin, ou du silence si un
vrai symbole portait le nom du paramètre.

## Pourquoi la forme nue évalue au lieu de substituer

La première formulation retenue était textuelle : substituer nu le texte de
l'argument, et avertir quand ce texte n'est pas « atomique ». Elle ne tient pas.
Un argument reçu comme `1+1` dans un corps faisant `db n*2` donne `db 1+1*2`,
soit 3 au lieu de 4 — un bug silencieux de précédence. Et le test d'atomicité
avertit sur `(3)`, qui est pourtant parfaitement sûr, les parenthèses étant
précisément ce qui neutralise le piège.

Évaluer supprime le problème au lieu de l'annoncer : `(3)` vaut 3, `1+1` vaut 2,
et `db n*2` donne 4. L'atomicité n'a plus à être définie.

## Deux sémantiques, et elles sont observables

La différence n'est pas cosmétique. Avec `n` variable d'assemblage :

```asm
macro m x
  repeat 3
    db x        ; nu : 5, 5, 5
    n = n - 1
  rend
endm
```

La forme nue capture **5** au point d'appel. `{x}` émet le texte `n`, que
l'assembleur résout séquentiellement au point d'émission : 5, 4, 3. C'est un
appel par valeur contre un appel par nom, et c'est la propriété à enseigner —
beaucoup plus simple que « atomique et numérique ».

C'est aussi ce qui a fait amender l'ADR 0011 : `{X}` n'évalue pas toujours.

## Évaluation avide, au site d'appel

`Env.args` porte désormais, pour chaque paramètre, le **texte brut** et une
**valeur numérique optionnelle**, calculée dans `expandMacro` avec
l'environnement de l'**appelant**. La forme nue lit la valeur, `{}` lit le texte.

Ce choix ferme une question restée ouverte : `evalPP` évaluait un argument avec
un résolveur **vide** (`pp.cpp:326-327`), si bien que `MAC high` avec un corps
faisant `repeat n` échouait, alors que `repeat high` marche hors macro. Avec
l'évaluation avide, rien dans le corps n'a besoin du contexte de l'appelant : un
argument arrive déjà résolu ou ne le sera jamais. Le résolveur vide devient la
bonne réponse, et la branche `env.args` doit lire la valeur précalculée au lieu
de réévaluer le texte.

Le corollaire est que la portée lexicale des macros n'est pas ouverte : c'est
l'argument qui est résolu avant l'entrée, pas la macro qui voit son appelant.

## Quand la valeur n'existe pas

Trois familles, et elles n'ont pas le même sort.

Un **symbole connu mais dépendant d'un label** — `n equ buffer+2` — n'est pas
résoluble au temps préprocesseur. La forme nue se replie sur le texte, que
l'assembleur résoudra très bien, et **avertit** : le repli change le moment de
la résolution, donc l'auteur peut être surpris. L'avertissement est ancré sur la
ligne du **corps**, où s'écrit la correction, et **nomme le site d'appel**, d'où
vient le fait. La clé de déduplication devient ainsi `(ligne du corps, site
d'appel)` : un avertissement par problème distinct, et non un par déroulage de
boucle.

Un **fragment non numérique par nature** — registre, `(ix+2)`, chaîne de plus
d'un octet, qui n'a pas de valeur (ADR 0010) — se replie sur le texte **en
silence**. Sans cela `macro savereg r` / `push r` deviendrait impossible sans
accolades, alors que c'est un idiome majoritaire.

Un **texte malformé** échoue là où il aurait échoué de toute façon.

## Conséquences

Les accolades restent permises sans condition, y compris sur un argument
numérique où la forme nue suffirait. La nature d'un argument dépend du **site
d'appel**, pas du corps : réserver `{}` aux cas où le nu ne suffit pas rendrait
une macro correcte cassable par un appelant qui change `3` en `hl`.

Un nom de paramètre ne peut pas porter un nom du vocabulaire réservé : cf. ADR
0015, dont la règle couvre cette position.

Un paramètre et un compteur d'un `REPEAT` **du corps** ne peuvent pas porter le
même nom. `expandMacro` construit un environnement vierge, donc les compteurs de
l'appelant ne sont pas hérités et il n'y a pas de collision à l'entrée ; mais un
`repeat 3,x` dans le corps d'une `macro m x` en crée une. La règle lexicale
classique — le plus interne l'emporte — est ici un bug muet, le corps se lisant
comme si `x` était le paramètre. C'est une erreur.

Enfin, la syntaxe de déclaration et d'appel des macros — forme parenthésée
`macro m(a,b)`, sort du `(void)` de rasm — n'est pas tranchée ici. Elle ne touche
pas la substitution d'un paramètre dans un corps, et relève d'une décision
distincte.
