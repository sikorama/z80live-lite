---
status: accepted
---

# Chaînes : deux délimiteurs pour un seul objet, et `DB` suffit

`'texte'` et `"texte"` désignent **le même objet** : une chaîne. Les deux
délimiteurs sont interchangeables partout, et rien ne distingue `'A'` de `"A"`.
Un littéral n'a de valeur en expression que s'il fait exactement un octet.
`DB` reste la forme normale d'émission, et une queue arithmétique s'y applique à
chaque caractère — la **chaîne décalée**, `db 'hello'-'a'`. Aucune table de
conversion de jeu de caractères : `CHARSET` est refusé.

## Contexte

Le corpus compte 265 chaînes en `'…'` et 589 en `"…"` sur des lignes de données,
296 littéraux d'un caractère, et 233 chaînes en `'…'` hors données — dont 178
`PRINT`. **21 textes distincts emploient les deux délimiteurs**, ce qui suffit à
dire que la distinction n'existe pas dans l'esprit des auteurs.

Ce n'est pas rasm qui manquait de symétrie, c'est fantams. Mesuré : rasm émet
`3e 78` pour `ld a,"x"`, `42` pour `db "A"+1`, `61 22 62` pour `db 'a"b'` et
`61 27 62` pour `db "a'b"`. Fantams, lui, réservait `"` aux chaînes de `DB` et
`'` aux caractères d'expression — deux trous symétriques, pas un.

**Cette ADR corrigeait une version antérieure d'elle-même qui affirmait un fait
faux.** Elle donnait `db 'HELLO' - 'A'` comme distribuant l'arithmétique sur les
caractères « comme rasm ». Vérification faite contre rasm : il émet **un seul
octet, `0xBF`**, soit `(0 - 65) & 0xFF` — le littéral multi-caractères y vaut
zéro, silencieusement, et il n'y a aucune distribution. `ld hl,'ab'`, `'abc'`,
`'abcd'` valent zéro de la même façon. Toute la justification bâtie sur cette
phrase était sans objet.

## Ce qui est décidé

**Les délimiteurs sont synonymes.** Un littéral court jusqu'à la prochaine
occurrence de *son propre* délimiteur ; l'autre y est un caractère ordinaire,
sans échappement. `db ''` et `db ""` émettent zéro octet. Un littéral non
terminé est une erreur. La règle vit une seule fois, dans `kw::readLiteral`, et
`DB`, `PRINT`, `ASSERT`, `INCLUDE` et `expr` l'appellent tous — la duplication
en cinq exemplaires était exactement la cause des deux trous.

**Le choix du délimiteur n'est pas avertissable.** Les avertissements de
fantams nomment chacun une ambiguïté réelle : un label sans `:` est indiscernable
d'un appel de macro. « Préférez `"` » n'en nomme aucune — aucun des 265 littéraux
`'…'` du corpus ne contient de `"`. Ce serait imposer un goût, ce que le beautify
se refuse déjà à faire (ADR 0013), et il n'y a donc pas non plus de règle de
beautify correspondante.

**Un littéral en expression doit faire un octet.** `ld hl,'ab'` est une erreur,
tout comme `'abc'` et `''`. Le tolérer supposerait une valeur à lui donner : ou
bien celle de rasm — un zéro silencieux, c'est-à-dire du faux émis sans le dire —
ou bien une convention d'endianness que rien dans le source n'énonce. Un
avertissement ne rattraperait pas ça : il laisserait passer des octets qu'aucune
règle ne définit.

**La chaîne décalée est un contexte, pas un type.** `db` accepte une *suite*
d'octets ; `ld hl,` réclame *une* valeur. La différence est dans le contexte, pas
dans le littéral — et c'est pourquoi `db 'hello'-'a'` a un sens là où
`ld hl,'ab'` n'en a pas : il n'y a rien d'ambigu à résoudre, `-'a'` s'applique à
chacun des cinq octets.

La forme est volontairement étroite : le littéral en **tête** de l'élément, le
reste étant une queue appliquée à chaque octet. Donc `db 'hello'-'a'+1` est
licite, `db 1+'hello'` non ; les parenthèses annoncent une expression, donc
`db ('hello')-'a'` non ; un seul littéral par élément, donc `db 'ab'-'cd'` non.
Tous les opérateurs binaires sont admis — restreindre à `+` et `-` serait une
règle de plus à retenir sans rien acheter. Seuls `DB`/`DEFB`/`DM`/`DEFM`
distribuent, plus la future famille `STRING` ; `DW` et `PRINT` refusent.

C'est une **divergence assumée** vis-à-vis de rasm, où la même ligne produit un
octet au lieu de cinq. Elle ne casse aucune source : la forme n'apparaît nulle
part dans le corpus, et pour cause — elle y produirait du faux.

## Pourquoi pas de mesure d'usage à l'appui

L'argument « zéro occurrence dans le corpus, donc pas de besoin » ne vaut rien
ici, et il faut le dire pour ne pas le refaire : on ne mesure pas la demande
pour une écriture que l'implémentation de référence sabote en silence. L'absence
d'usage n'est une preuve d'absence de besoin que si l'usage était possible.

Ce qui *justifie* la chaîne décalée est ailleurs : sans elle, décaler une chaîne
s'écrit `db 'h'-'A','e'-'A','l'-'A',…`, illisible à cinq caractères et
inécrivable à trente ; et les macros de fantams ne manipulent pas de chaînes,
donc la porte de sortie habituelle est fermée. Il ne reste que le script externe,
pour un besoin d'une ligne.

## `CHARSET` est refusé

Mesuré : 28 lignes réparties sur 11 textes distincts (~3 % du corpus). Et ces
lignes ne font pas ce qu'on croyait — elles n'appliquent pas un offset, elles
posent une **permutation arbitraire** :

```
charset '.=()+-*/lpf',0     → '.'→0, '='→1, '('→2, ')'→3, '+'→4 …
CHARSET '0','9','0'+7       → remappe la plage '0'..'9'
charset                     → réinitialise
```

L'ordre est celui dans lequel le programmeur a rangé ses glyphes en ROM. Aucune
arithmétique ne le décrit, et la chaîne décalée ne le remplace donc pas — ces
deux sujets sont indépendants, contrairement à ce que cette ADR affirmait.

Le refus tient à deux raisons, dont la seconde est décisive. Un encodage de fonte
est une **transformation d'asset**, de la même famille qu'une image convertie en
tuiles, que personne n'attend d'un assembleur. Et surtout, `charset` **ne se lit
nulle part** : il change les octets émis par toutes les lignes suivantes, et
`db 'hello'` reste écrit `db 'hello'` dans la source déroulée alors que les
octets émis ne sont pas ceux que le texte dit. La source déroulée mentirait — or
elle est un livrable de premier plan, réassemblable et lisible par un humain.

C'est ce qui le sépare des autres constructions à portée avant. `ORG` porte son
effet sur chaque ligne du listing. `MODULE` et les variables sont *résolus* dans
la source déroulée, qui montre les labels renommés et les valeurs substituées.
`charset` serait le seul dont l'effet reste invisible — y compris dans une
variante à portée bornée, ce qui vaut refus d'avance.

Le diagnostic nomme le remplaçant, comme ceux de `BANK`, `SNASET` et `TICKER` :
produire les `db` par un script.

Prix assumé : 11 textes sur 377 demanderont du portage.

## `STRING` reste à faire, et pour une seule raison

Une famille `STRING <terminaison>` couvre les conventions de fin — `MSB`,
`ASCIIZ`, `PASCAL` — et n'a qu'une raison d'être : apporter les vérifications
que rasm n'offre pas. **La terminaison MSB est incompatible avec tout octet
≥ 0x80** : si un caractère a déjà son bit 7 à 1, la chaîne se termine en son
milieu, silencieusement. Un assembleur peut le refuser, et c'est le seul gain de
correction — non de confort — de toute cette famille.

Le corpus compte 707 lignes de `DB` avec une chaîne sans terminateur, 111 en
terminaison MSB (concentrées sur une source et son fork), 34 en `asciiz`, et
aucun usage avéré du préfixe de longueur. La forme majoritaire fonctionne déjà.
`STR`, le nom retenu par rasm, ne dit pas ce qu'il encode : il devient un alias
déprécié de `STRING MSB`, et son implémentation est différée.

`PASCAL` figure dans la table par orthogonalité : il coûte une ligne au même
endroit que les deux autres, tout en n'étant justifié par aucun usage mesuré.
