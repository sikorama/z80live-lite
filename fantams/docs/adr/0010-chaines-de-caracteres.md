---
status: accepted
---

# Chaînes : `DB` suffit, `STRING` n'existe que pour vérifier

`DB` reste la forme normale d'émission d'une chaîne, et l'arithmétique se
distribue sur les caractères d'un littéral : `db 'HELLO' - 'A'` émet chaque
caractère diminué de 65. Une famille `STRING <terminaison>` couvre les
conventions de fin — `MSB`, `ASCIIZ`, `PASCAL` — et n'a qu'une seule raison
d'être : apporter les vérifications que rasm n'offre pas. Aucune table de
conversion de jeu de caractères.

## Contexte

Le corpus compte 707 lignes de `DB` avec une chaîne sans terminateur, 111 en
terminaison MSB (concentrées sur une source et son fork), 34 en `asciiz`, et
aucun usage avéré du préfixe de longueur — les sept lignes que ma détection
signalait étaient des tables de données où le premier octet n'est pas une
longueur.

La forme majoritaire fonctionne déjà ; on n'y touche pas. `STR`, le nom retenu par
rasm, ne dit pas ce qu'il encode : il devient un alias déprécié de `STRING MSB`,
et son implémentation est différée — elle n'est essentielle pour personne.

## Ce que `STRING` apporte

**La terminaison MSB est incompatible avec tout octet ≥ 0x80.** Si un caractère a
déjà son bit 7 à 1, la chaîne se termine en son milieu, silencieusement. Un
assembleur peut le refuser, et c'est le seul gain de correction — non de confort —
de toute cette famille. Sans cette vérification, `STRING` n'aurait pas lieu
d'être.

`PASCAL` figure dans la table par orthogonalité : il coûte une ligne au même
endroit que les deux autres, tout en n'étant justifié par aucun usage mesuré.

## Deux axes, et pourquoi un seul est retenu

Terminaison et jeu de caractères sont indépendants — « ASCII 7 bits » n'est pas
un format de chaîne mais une contrainte sur les caractères, et la convention
classique du firmware Amstrad les compose : 7 bits plus le bit 7 comme marqueur
de fin.

L'axe jeu de caractères n'est pourtant pas retenu. L'usage courant qu'il devait
servir — soustraire un offset, `'A'` codé 0 — se traite par `db 'HELLO' - 'A'`
sans aucun concept nouveau : ni directive, ni type supplémentaire dans `expr`, et
une source déroulée qui garde une ligne par ligne. Une table de conversion
complète est de la complexité inutile : le besoin qui la dépasse se traite par une
macro ou par un script externe qui produit l'encodage voulu, ce qui ne coûte rien
à fantams.
