---
status: accepted
---

# Tout bloc se ferme par `end` ou par le nom de son ouvreur, et les index de boucle commencent à 0

`end` ferme le bloc ouvert le plus interne, quel qu'il soit. Chaque bloc a en
outre une fermeture explicite — `endmacro`, `endmodule`, `endrepeat`,
`endwhile`, `endstruct`, `endfor`, `endif` — et la correspondance est
**vérifiée**. Les formes courtes héritées de rasm restent tolérées en silence.
L'index de `repeat` vaut 0 à la première itération, et une nouvelle boucle `for`
porte des bornes écrites.

## Contexte des fermetures

`findMatching` (`pp.cpp:623`) compte la profondeur sur **une seule paire** de
mots-clés : elle est appelée avec `{"REPEAT"}, {"REND"}`. Elle ne sait donc pas
quel bloc est ouvert, seulement combien d'ouvreurs d'un type donné restent à
fermer. Deux conséquences : un `end` polyvalent est impossible — le premier `end`
d'un `REPEAT` imbriqué serait pris pour celui de la `MACRO` qui l'entoure — et un
bloc fermé par la fermeture d'un **autre** bloc produit un diagnostic trompeur,
« REPEAT without REND », au lieu de nommer la faute.

`ENDM` et `ENDS` portent par ailleurs une ambiguïté de lecture : « end macro » ou
« end module » ? « end struct » ou « end s… » ?

## La vérification supprime l'ambiguïté, elle ne la contourne pas

`findMatching` est remaniée pour compter sur **l'union des ouvreurs**. Elle
connaît alors le type du bloc en cours, ce qui rend possibles deux choses d'un
coup : `end` ferme le plus interne, et une fermeture nommée qui ne correspond pas
est une **erreur qui le dit**.

C'est ce qui règle le sort d'`ENDM`. Son ambiguïté n'était un problème que tant
qu'il fallait la lever par le seul mot ; dès qu'on sait quel bloc est ouvert,
`endm` fermant une macro est vérifié, donc sans risque. Il est conservé, en
silence, comme `mend`, `ends`, `rend` et `wend`.

Déprécier ces formes courtes aurait produit un avertissement sur presque toute
source rasm existante, pour des mots — `rend`, `wend` — qui ne sont ambigus avec
rien. Un diagnostic qui parle de code correct est un diagnostic qu'on désactive.
La table complète est donc : un mot canonique par bloc, plus `end`, plus les
formes courtes tolérées sans un mot.

`endwhile` est ajouté bien que `wend` ne soit ambigu avec rien : la valeur de la
table n'est pas de désambiguïser au cas par cas, c'est d'être énonçable sans
exception — tout bloc `X` se ferme par `endX`, ou par `end`.

`end`, `endmacro`, `endrepeat`, `endwhile` et `endfor` entrent au vocabulaire
réservé, avec les conséquences de l'ADR 0015 : `end:` devient illégal.

Le beautify ne normalise **pas** `end` vers la forme explicite. Ce serait
l'introduire puis l'effacer, et cela exigerait de rendre le beautify conscient de
l'imbrication — donc de lui retirer sa propriété la plus utile, celle de mettre
en forme chaque ligne indépendamment, y compris dans un source déséquilibré.

## L'index de `repeat` passe à 0

`repeat count[,var]` était 1-based, délibérément, par compatibilité rasm. Il
passe à 0.

Cette divergence est la seule du projet qui **ne peut pas** être détectée. Un
`{hex}` produit une erreur : la source ne s'assemble pas, l'auteur est prévenu,
et l'ADR 0011 a pu assumer un nombre de sources à éditer. Un index qui démarre à
0 au lieu de 1 produit une source qui s'assemble parfaitement et des données
décalées d'un cran — une table, un déroulage d'adresses, un sprite.

Elle est néanmoins acceptée, à une condition qui la rend bruyante : **tout**
usage de la forme `repeat n,i` déclenche un avertissement de dépréciation. Le
signalement ne repose donc pas sur une détection impossible, mais sur le fait que
la construction elle-même est ce qu'on déprécie — aucun faux positif n'est
possible, et aucune source importée ne peut produire de données décalées sans
qu'un avertissement l'ait dit. L'avertissement nomme le remède disponible : une
source écrite pour rasm doit lire `i+1`, et la forme recommandée est `for`.

## `for`, où les bornes sont écrites

Le grief contre le 1-based de rasm n'est pas « 1 plutôt que 0 », c'est qu'il faut
le **savoir** au lieu de le lire. Une nouvelle construction ne doit donc pas
reproduire la faute en la déplaçant.

`for x = 0 to n` inclut la borne haute, `for x = 0 until n` l'exclut. Deux mots,
deux sens, et aucun à mémoriser. `until n` fait exactement `n` tours, ce qui en
fait le remplacement direct de `repeat n,x`.

La forme `for x in 0..n` a été écartée : le point est un caractère d'identifiant
dans ce projet — c'est ce qui permet les labels locaux `.loop` — si bien que
`0..count` se lexe en `0` puis un identifiant `..count`, et que `0...count`, dont
la borne est le label local `.count`, est ambigu au découpage comme à la lecture.
Un séparateur qui est un mot-clé n'a pas ce problème.

`for` se ferme par `endfor` ou `end`.

## Conséquences

Le remaniement de `findMatching` et de `classify` est un préalable à `end` : les
deux ne se livrent pas séparément.

`endr` reste à trancher, et n'est pas retenu ici. Contrairement à ce que son
allure suggère, ce n'est pas une forme de compatibilité : les fermetures acceptées
jusqu'ici étaient `ENDM`/`MEND`, `REND`, `WEND`, `ENDMODULE` et
`ENDSTRUCT`/`ENDS`. L'ajouter en ferait une **troisième** graphie pour un même
bloc à côté de `rend` et de `endrepeat`, sans compter `end` — soit quatre façons
de fermer un `repeat`.

L'avertissement de dépréciation de `repeat n,i` mentionnera `for` dès que `for`
existera ; jusque-là il nomme `i+1`. Les deux ne sont pas livrés dans le même
lot, et la formulation de l'avertissement en dépend.

Une option de normalisation du beautify (ADR 0017) peut ramener les formes
courtes à leur forme canonique. Elle reste facultative : le préprocesseur accepte
les deux, et aucune source n'est assemblable seulement après mise en forme.
