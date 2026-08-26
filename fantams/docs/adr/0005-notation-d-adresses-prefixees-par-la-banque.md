---
status: accepted
---

# Adresses préfixées par la banque : `Bn:adresse`

Une adresse peut être préfixée du numéro de la banque de 16 K où loger les octets :
`org b4:0x4000`. Le préfixe désigne l'**emplacement de rangement**, le nombre qui
suit reste l'**adresse logique** — celle que prennent les labels. L'offset dans la
banque est `adresse & 0x3FFF`. La directive `BANK` n'est pas reprise ; elle est
acceptée en compatibilité rasm avec un avertissement de dépréciation.

## Contexte

Le Z80 adresse sur 16 bits, alors qu'un CPC 6128 possède 128 K. Où un octet est
rangé et à quelle adresse le processeur le verra sont donc deux informations
indépendantes, et seul le programmeur connaît la seconde à l'avance : elle dépend
de la configuration du gate array au moment de l'exécution.

rasm sépare ces deux informations sur deux lignes — `BANK n` puis `ORG adresse` —
qui peuvent être distantes de plusieurs milliers de lignes. Le corpus le montre :
dans `land3d`, les `bank`/`org` se suivent, mais les blocs qu'ils ouvrent font
2000 lignes chacun, et rien à l'intérieur ne rappelle dans quelle banque on écrit.

Nous accolons donc les deux informations : `org b4:0x4000` remplace exactement le
couple `bank 4` / `org #4000`.

## Pourquoi l'adresse logique n'est pas dérivée du numéro de banque

Une banque pourrait sembler avoir un slot naturel, la banque *n* étant vue en
`(n mod 4) * 0x4000`. Le corpus réfute cette idée sans appel : **les sept sources
qui utilisent `BANK` écrivent toutes `org #4000`**, pour les banques 4, 5, 6 et 7
indifféremment. Les configurations RAM standard du gate array (`&7FC4` à `&7FC7`)
paginent en effet toute banque supplémentaire dans le slot 1. Un offset dérivé du
numéro de banque donnerait des labels faux dans trois cas sur quatre.

L'adresse logique doit donc être écrite, jamais déduite.

## Le masquage, et ce qu'il autorise

L'offset dans la banque vaut `adresse & 0x3FFF`. Sur tout le corpus, cette règle et
un offset relatif à la banque donnent le même résultat, puisque `0x4000 & 0x3FFF`
est nul. Elles ne divergent que sur `org b4:0x8000`, qu'aucune source n'écrit :
le masquage y voit « banque 4 rangée depuis son début, labels en 0x8000 », lecture
légitime pour qui pagine la banque 4 en slot 2, là où un offset relatif borné
l'interdirait sans raison.

Le masquage a un coût : `b4:0x0000`, `b4:0x4000`, `b4:0x8000` et `b4:0xC000`
rangent au même endroit. Une faute de frappe sur le slot ne produit pas d'erreur
de syntaxe — elle se manifestera comme un recouvrement d'octets, que
l'assembleur peut détecter par ailleurs.

## Portée de la notation

Le préfixe qualifie une adresse dans les directives de placement, pas dans les
expressions. `ld hl, B4:label` n'est pas accepté : la valeur produite tiendrait
sur 16 bits et n'aurait plus de place pour la banque, et le `:` entre en collision
avec la syntaxe des labels comme avec le séparateur d'instructions du
préprocesseur. L'arithmétique explicite reste la façon d'exprimer un décalage —
`ld hl, 0x8000 + (label & 0x3fff)` — et elle a le mérite de rendre visible une
hypothèse de pagination que rien ne peut vérifier.

La banque est rémanente d'un `ORG` au suivant, mais un `ORG` sans préfixe héritant
d'une banque non nulle **émet un avertissement**. Sans rémanence, écrire un long
bloc en banque 4 obligerait à répéter le préfixe ; sans avertissement, un préfixe
oublié déplacerait silencieusement le bloc dans les 64 K de base — panne à
l'exécution, sans diagnostic. Le listing porte banque et adresse pour chaque ligne,
ce qui rend l'héritage consultable. Le préprocesseur, lui, n'interprète pas
`ORG` : la source déroulée ne peut pas porter cette garantie.

Le second paramètre d'`ORG` conserve sa sémantique rasm — délier complètement
rangement et adresse logique, comme `org 0x2000,0x3000`. Il est **implémenté**, et
sa sémantique a été mesurée contre rasm plutôt que devinée : le **premier**
paramètre est l'adresse **logique**, celle que prennent les labels et pour laquelle
le code est assemblé ; le **second** est l'adresse de **rangement**, où les octets
sont réellement écrits. Un bloc `org #A600,#100` est donc du code stocké en `#100`
et destiné à tourner en `#A600` — c'est exactement ce que fait la seule source du
corpus qui l'utilise : elle recopie le bloc avant de l'appeler.

Trois conséquences sont alignées sur rasm, mesure à l'appui :

- **`RUN` prend l'adresse logique.** `run label` doit valoir ce que vaut `label`,
  sinon `run label` et `run #A600` donneraient deux `PC` différents pour la même
  adresse et plus rien n'est prévisible. Le `PC` tombe donc sur de la mémoire pas
  encore chargée : c'est **averti**, pour la même raison que la banque rémanente
  l'est — sans diagnostic, c'est une panne à l'exécution sans explication.
- **`ALIGN` aligne le logique.** C'est l'adresse où le code tournera après recopie ;
  aligner le rangement alignerait une position dans un tampon qui sera déplacé,
  donc désaligné à l'arrivée. Corollaire assumé : le rangement, lui, ne l'est pas.
- **Le déplacement n'est pas rémanent** : un `ORG` sans second paramètre le remet à
  zéro. Asymétrique avec la banque, qui est rémanente *et* avertit — parce que
  c'est l'héritage silencieux qui est risqué, pas la remise à zéro, laquelle remet
  le bloc là où son `ORG` le dit.

`loadAddress` et l'étendue du binaire décrivent le **rangement** : le `.bin` brut
cesse donc de se charger à l'adresse des labels. C'est correct, et surprenant. Le
diagnostic de chevauchement nomme lui aussi le rangement — c'est là que les octets
s'écrasent réellement.

### Où se porte le préfixe de banque

Le préfixe qualifie le **rangement**, il se porte donc sur le **paramètre de
rangement** : le dernier. `org 0x4000,b4:0x3000`. En forme à un paramètre,
l'unique adresse fait les deux offices et le préfixe s'y porte comme avant :
`org b4:0x4000`.

Le préfixe sur le premier paramètre d'une forme à deux est **refusé**, avec un
message nommant le remplaçant. Sans cette règle, le rangement serait décrit de part
et d'autre de l'adresse logique — banque en position 1, offset en position 3, le
logique au milieu. Aucune source n'écrit `b<n>:` aujourd'hui, cette notation étant
une invention de fantams remplaçant `BANK` : placer le préfixe sur le second
paramètre ne casse donc rien, c'est de la syntaxe neuve.

L'ordre rasm est conservé. L'inverser — rangement d'abord — aurait mis le préfixe
sur le bon paramètre, mais aurait réinterprété la source existante à l'envers, en
silence.

## Graphie

`B` pour *banque*, et non `C`. Sur un CPC, `C0` à `C7` désignent conventionnellement
les huit configurations RAM du gate array, qui n'ont rien à voir avec des index de
banques. Le projet amspirit-lite emploie la graphie `C` pour des banques tout en
réservant le mot « mode » aux configurations ; nous préférons éviter la collision
plutôt que la documenter.

`R:` est réservé pour les ROMs de cartouches CPR, sans être implémenté : la
réservation empêche qu'un jour `R4` devienne un label valide qu'il faudrait
casser. La notation des ROMs firmware ne concerne que l'émulateur et n'a pas à
exister ici.

## Conséquences

`asm.cpp` conflait jusqu'ici les deux notions : `image_` est un tableau plat de
65 536 octets et `emit()` écrit en `image_[pc_ & 0xFFFF]`. Il n'existe aucun
endroit où loger une banque — le modèle mémoire est à remplacer, pas à étendre. De
même, `sna::build()` exige une image de 64 K exactement, et `compare.mjs` ne
reconstruit que le chunk `MEM0`, avec le commentaire « fantams ne gère pas le
multi-bank » : sans mise à jour, tout code écrit en banque 4 serait déclaré
identique à la référence sans jamais avoir été lu.

Enfin, la notation diverge de celle d'amspirit-lite au-delà de la graphie. Le
modèle plat y calcule `banque * 0x4000 + offset`, ce qui rend `C0:0x8000` et
`C2:0x0000` équivalents ; ici ils désignent deux banques différentes. Les deux
lectures coïncident tant que l'offset reste sous 0x4000 et divergent ensuite. Un
débogueur n'a qu'un emplacement à désigner ; un assembleur doit porter deux
informations, et leur somme les perd.
