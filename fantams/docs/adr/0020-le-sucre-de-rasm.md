---
status: accepted
---

# Le sucre de rasm : ce qu'on adopte, ce qu'on refuse

L'ADR 0017 a posé le **critère** — un-vers-plusieurs au préprocesseur, un-pour-un
à l'assembleur. Cet ADR-ci enregistre ce que ce critère décide quand on
l'applique au catalogue complet de rasm, et les deux lignes que le critère ne
couvrait pas : la répétition est du **déroulage**, et on avertit quand le texte
**ment**.

## Contexte

Le catalogue a été relevé sur pièces, pas de mémoire : rasm 3.2.5 compilé depuis
`rasm.c`, et chaque forme soumise aux deux assembleurs avec comparaison des
octets. C'est ce qui a permis d'écarter trois croyances qui auraient orienté la
décision de travers.

D'abord, la répétition n'est **pas** une règle générale chez rasm. Elle est
écrite à la main dans dix gestionnaires d'opcode — `NOP`, `HALT`, `LDI`, `LDD`,
`INI`, `IND`, `OUTI`, `OUTD`, `RLCA`, `RRCA` — et `ldir 2`, `cpi 4`, `exx 2`,
`di 2` sont des erreurs. Elle ne vit pas non plus dans son préprocesseur : elle
est dans l'assembleur, donc `nop fin-debut` y marche sur des labels **arrière**.

Ensuite, `ex af,af'` est la forme **canonique**, avec l'apostrophe — c'est la
notation de Zilog, et l'apostrophe désigne le registre fantôme. C'est
`ex af,af` qui est la tolérance, pas l'inverse.

Enfin, `rst z,#38` n'est pas ce qu'il paraît. Voir plus bas : c'est ce qui le
fait refuser.

La comparaison a aussi trouvé un bug qui n'attendait personne : `inc hl,de`
rendait **un** octet, le second opérande étant silencieusement jeté. Ni le sens
de rasm, ni une erreur.

## Ce qui est adopté

**Canonisation** (préprocesseur, un-vers-plusieurs, `-E` montre les deux lignes) :

| Écrit | Déplié en |
|---|---|
| `ld de,hl` — toute paire parmi BC/DE/HL/IX/IY | `ld d,h` · `ld e,l` |
| `ld hl,(ix+d)` · `ld (ix+d),hl` | deux lignes indexées, la moitié haute en `d+1` |

L'ordre est haut puis bas, comme rasm. Aucun recouvrement n'est possible : les
deux paires étant disjointes, la moitié haute écrite n'est jamais la moitié
basse encore à lire.

`ld hl,ix` n'existe pas, et c'est une contrainte de la **machine** : le préfixe
DD fait de `h` la moitié de IX, si bien qu'aucune instruction ne nomme H et IXH
à la fois. Deux paires d'index s'excluent pour la même raison. rasm les refuse
aussi.

Le décalage de la moitié haute — l'octet bas est à l'adresse basse — est
précisément ce qui rend `ld hl,(ix+d)` utile : c'est le détail qu'on écrit à
l'envers une fois sur deux à la main.

**Orthographes** (l'assembleur tolère, `--normalize` réécrit, `--strict` refuse,
le beautify ne touche pas) :

`ex hl,de` → `ex de,hl` · `ex hl,(sp)` → `ex (sp),hl` · `ex ix,(sp)` →
`ex (sp),ix` · `ex af,af` → `ex af,af'` · `jp hl` → `jp (hl)` (idem `ix`, `iy`)

Les trois premières ne sont qu'un échange de l'ordre des opérandes : l'instruction
est symétrique, la notation ne l'est pas.

`jp hl` porte à trois les graphies d'un seul octet, avec `jp (hl)` et `ld pc,hl`.
C'est **délibéré**, et il faut le dire ici pour que ça ne se lise pas comme une
dérive dans six mois : la raison d'être de ce travail est qu'une source écrite
pour l'un ou l'autre assembleur se colle et fonctionne. Le prix est une ligne de
table par graphie.

**Déroulage** (préprocesseur ; `-E` déroule, `--normalize` laisse, `--strict`
refuse) : `<mnémonique sans opérande> n`.

## La répétition est du déroulage, pas de la canonisation

C'est la décision la plus structurante, et le vocabulaire la tranchait déjà.
CONTEXT.md définit le déroulage comme ce qui « multiplie les lignes par
**répétition** ou par expansion d'une définition ». `ldi 4` multiplie les lignes
par répétition. Donc :

- `-E` le déroule **entièrement**, sans plafond. `nop 1024` rend mille
  vingt-quatre lignes. La promesse de la source déroulée est « vous lisez
  exactement ce qui sera assemblé » ; une ligne qui resterait une ligne la
  romprait à la taille exacte où le lecteur en a le plus besoin, et un plafond
  serait un mensonge silencieux.
- `--normalize`, qui canonise **sans** dérouler, le laisse intact. Sa promesse
  rétrécie — un opcode par ligne, plus d'orthographe obsolète — est tenue :
  `nop 32` porte bien un seul opcode sur sa ligne.
- Le compteur est une **valeur de préprocesseur**, exactement comme celui de
  `repeat n` dont cette écriture est le raccourci. Une variable convient, une
  expression aussi, une expression touchant un label **jamais** (CONTEXT.md).

Ce dernier point est une divergence assumée avec rasm, qui accepte
`nop fin-debut` sur des labels arrière puisque sa répétition vit dans
l'assembleur. Elle est **forcée** : dès lors que la répétition est du déroulage,
le compteur est une valeur de préprocesseur, et l'alternative serait de la
déplacer dans l'assembleur en abandonnant la fidélité de `-E`. Et l'idiome est
déjà mieux servi : `ds fin-debut` marche, connaît les adresses, et dit « réserver
cette place » plutôt que « exécuter tant de non-opérations ». Le diagnostic le
nomme — sans promettre qu'il marche vers l'**avant**, `ds` déplaçant lui-même
l'adresse qu'on lui demande de calculer.

La règle porte sur **tout** mnémonique sans opérande, plus large que les dix de
rasm. Elle admet donc `ldir 3` et `di 3`. C'est accepté sans liste noire : une
règle à exceptions coûte plus cher à retenir qu'elle ne fait gagner (l'argument
de l'ADR 0015 sur `END`), et « pourquoi `ldi 4` et pas `cpi 4` ? » n'a pas de
réponse sinon « parce que ». `ldir 3` n'est d'ailleurs pas *faux* : ce sont trois
`ldir`, ce qu'il dit.

`RET` et `IM` n'en sont pas : ils **prennent** un opérande, et un compteur y
serait ambigu.

## On avertit quand le texte ment

`ex af,af` est la seule tolérance du projet à être **signalée**. Les huit autres
— `defb`, `push hl,de`, `ld pc,hl`, `ex hl,de`… — sont muettes, et une exception
sans raison dite se lit comme un accident.

La raison est celle-ci : `ex af,af` est la seule dont la lecture littérale désigne
une **autre opération**. Écrit tel quel, il dit « échanger AF avec lui-même »,
c'est-à-dire un `nop`. Toutes les autres tolérances sont non ambiguës, seulement
démodées. D'où la ligne, qui se généralise aux cas futurs : **on avertit quand le
texte ment, pas quand il se démode.**

L'avertissement est porté par le **préprocesseur**, seul étage à voir la source
telle qu'elle est écrite — même argument que l'ADR 0017 pour `--strict`, et pour
la même raison : la canonisation ayant lieu dans `emit()`, l'assembleur ne voit
jamais `ex af,af` à travers la chaîne complète.

## Ce qui est refusé, et pourquoi

**`ld hl,sp`.** rasm le rend en `ld hl,0 : add hl,sp` — quatre octets, et le
**carry est écrasé**. Toutes les autres facilités de la table rendent exactement
ce que l'auteur aurait tapé ; celle-ci invente une arithmétique. `--strict` ne
peut pas protéger quelqu'un d'un drapeau qu'il ignore avoir perdu.

**`rst cc,n`.** rasm rend deux octets, `28 FF`, où le `FF` est **à la fois** le
déplacement du `jr z` et l'opcode `rst #38` sur lequel ce `jr` retombe. C'est
ingénieux, et ça ne marche que pour `#38` (`rst z,#20` est une erreur chez rasm).
Mais les deux instructions **partagent un octet** : aucune paire de lignes Z80
canoniques ne la reproduit. Or la table de canonisation est contrainte à ne
produire que du Z80 canonique (ADR 0017) — un sucre que `-E` ne saurait écrire
sans mentir sur le programme n'y entre pas.

**Les décalages 16 bits** — `rlc hl`, `rr de`, `sla bc`, et `srl8 rr`. Ce ne sont
pas des orthographes mais des **routines** : `rlc hl` est quatre opérations `cb`,
et ni leur nombre ni l'ordre dans lequel les drapeaux sont touchés ne se lisent
sur la ligne. Une routine mérite le nom que son auteur lui a choisi, donc une
macro. C'est un refus, pas un report : un report est une promesse de revoir que
personne ne revoit, et il laisserait la table arbitrairement incomplète.

**`inc hl,de` et `dec hl,de`.** La liste multi-registres reste réservée à
`push`/`pop`, et la justification n'est pas arbitraire : ceux-là sont par nature
une **séquence** — personne n'écrit `push hl,de` en pensant à un seul push de
32 bits, et la liste épouse l'idiome de sauvegarde/restauration que l'auteur a
déjà en tête. `inc hl,de` n'a pas cet idiome derrière lui, et il se confond avec
`add hl,de`, qui existe et fait tout autre chose. Le refus est **explicite** :
c'est lui qui corrige le bug du silence.

## Ce qui ne bouge pas

**`add a,b` contre `add b`.** fantams accepte déjà les deux — et même
`and a,b`, `or a,b`, `xor a,b`, que rasm **refuse**. Aucun canon n'est élu :
l'ADR 0017 dit que le canon est une structure — un opcode par ligne, une
orthographe standard — et les deux formes le sont. Trancher ici serait la
première fois que le projet légifère sur un **goût**, et aucun bug n'est à
prévenir. `--normalize` n'y touche pas, `--strict` accepte les deux.

**`nop:nop:nop`.** Marche déjà, et l'ADR 0015 en avait décidé : `splitStatements`
lit ce deux-points comme un séparateur d'instructions, et un mnémonique n'est
jamais épluché comme un label. Le mécanisme est exactement le critère qu'on
aurait voulu inventer. Seul `--strict` le refuse, comme toute ligne à plusieurs
instructions, et c'est le dessein.

## Conséquences

`expandSugar()` réunit la canonisation un-vers-plusieurs en **un** point, que
`emit()` et `normalize()` partagent : deux tables divergentes feraient diverger
`-E` et `--normalize`. `kw::canonicalOrthography()` joue le même rôle pour les
orthographes à plusieurs mots, et c'est par elle que l'assembleur les tolère —
il n'a pas à savoir combien il y en a.

Le diagnostic de `--strict` nomme la **ligne entière** quand le mot de tête ne
change pas. Le critère précédent — « est-ce un `ld pc,rr` ? » — nommait le seul
premier mot pour `ex hl,de` et produisait « `'ex'` is a non-canonical spelling —
write `'ex'` ».

Un label ne se répète pas : `pause: nop 2` définit un label et deux `nop`. Et un
compteur nul émet le label quand même — `fin: nop 0` nomme une adresse.

Les trois propriétés de l'ADR 0017 restent vérifiées sur le sucre nouveau :
idempotence de `--normalize`, préservation du nombre de lignes par le beautify,
et égalité des octets assemblés. S'y ajoute une propriété propre à ce travail,
vérifiée sur 89 formes : **les octets rendus sont ceux de rasm**, pour toute
forme que les deux acceptent.
