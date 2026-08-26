---
status: accepted
---

# La table des symboles est un CSV, une ligne par nom

`--sym` écrit un fichier CSV d'une ligne par label et par constante, avec le type,
l'adresse logique, la banque et l'adresse de rangement, le fichier et la ligne
**d'origine**. Il est destiné à un désassembleur ou à un émulateur.

## Contexte

`Output::symbols` était un `std::map<std::string, int64_t>` : un nom, une valeur,
rien d'autre. Ni banque — jamais stockée par symbole, `bankOf()` la dérivant à
l'émission — ni provenance. L'option `-s` en imprimait le contenu sur stderr, pour
un humain qui regarde son terminal.

Un désassembleur a besoin d'autre chose : savoir quel nom correspond à `0x8042`,
si ce nom est une adresse ou un nombre, et où l'auteur l'a écrit pour pouvoir y
ouvrir un éditeur. Ce sont trois informations que la table ne portait pas.

## Ce n'est pas un listing

Le glossaire réserve **listing** à la correspondance ligne de source par ligne de
source entre banque, adresse et octets. Deux artefacts différents portent deux
noms différents : celui-ci est la **table des symboles**, une ligne par *nom*, et
sans un seul octet. D'où l'extension `.sym`, et non `.lst` — ni `.map`, qui
évoque le source-map, donc encore le listing.

## Le format

```
name,type,value,bank,store,file,line
main,label,0x8000,2,0x8000,jeu.asm,9
@retry__1,label,0x8002,2,0x8002,macros.asm,3
@retry__2,label,0x8009,2,0x8009,macros.asm,3
far,label,0x4000,4,0x100,jeu.asm,15
SCREEN,const,0xC000,-,-,jeu.asm,2
```

CSV, avec une **vraie ligne d'en-tête**, non commentée : les noms de colonnes
*sont* le numéro de version — un lecteur qui voit apparaître une colonne le sait —
et `csv.reader` n'a pas de préambule à sauter. Le champ fichier est guillemeté
selon la RFC 4180 **seulement s'il en a besoin** : un chemin contenant une virgule
est rare mais légal, et sans ce guillemetage il produirait une ligne à huit champs
au milieu d'un fichier à sept, que le consommateur décalerait sans rien signaler.
C'est le seul mode d'échec silencieux que ce format pouvait avoir.

`value` est l'adresse **logique** : ce que le nom vaut dans une expression, donc
ce qu'un désassembleur doit substituer. `bank` et `store` décrivent le
**rangement**. Les deux ne divergent que dans un bloc `org <logique>,<rangement>`
(ADR 0005) ; l'ordre des colonnes reprend celui des paramètres d'`ORG`.

Le tri est **banque, puis rangement, puis nom**. La question d'un désassembleur
est « quel symbole est en `0x8042` », donc une recherche par adresse : un fichier
trié par adresse se lit en une passe sans construire d'index. Le départage par nom
rend le tri **total**, sans quoi la sortie cesserait d'être reproductible d'un
assemblage à l'autre — deux symboles à la même adresse est le cas courant
(`screen:` puis `.start:`).

## Ce que la table refuse de faire

**Pas de variables.** Une variable (`=`) est réaffectée en cours de route ; sa
valeur n'est celle d'aucun point précis du programme, et un désassembleur n'en
ferait rien. Les constantes (`EQU`), elles, restent : `SCREEN equ 0xC000` est
exactement ce qu'on veut substituer dans un `ld hl,#C000`. Les variables de
préprocesseur (`LET`) n'existent plus au temps d'assemblage.

**Pas de dé-manglage.** Les noms sortent tels que l'assembleur les connaît :
qualifiés (`top.inner`) et manglés (`@retry__2`). Le consommateur veut le nom qui
correspond *effectivement* à l'adresse ; une table qui afficherait trois fois
`@retry` à trois adresses serait inutilisable. Corollaire assumé : trois
expansions d'une macro donnent trois noms et **une seule** ligne d'origine. C'est
le nom qui distingue, pas le numéro de ligne.

**Pas de numéro de ligne dans la source déroulée.** Il avait été envisagé, puis
écarté : il n'apporte rien qu'on ne puisse obtenir en travaillant directement sur
la sortie du préprocesseur. Il aurait en outre couplé le `.sym` aux drapeaux de
mise en forme — `--no-detach-labels` change le nombre de lignes que `-E` écrit
(ADR 0013) — donc à un artefact que l'auteur n'a pas forcément demandé.

**Pas de format de compatibilité tiers.** Le `.sym` de rasm (`START #2000 B0 L`)
ou celui d'un émulateur donné perdent le type, la provenance, ou les deux. Le jour
où un émulateur précis est visé, il sera visé nommément ; en attendant, la
conversion est un `awk` de trois lignes.

**Pas de métadonnées.** Ni point d'entrée ni adresse de chargement : le premier est
déjà le `PC` du snapshot, la seconde est dans le binaire, et les redire ici
créerait deux sources de vérité. Des lignes synthétiques `entry`/`load`
planteraient de surcroît des noms dans un espace qui appartient à l'auteur — une
source ayant un label nommé `entry` aurait produit deux lignes de même nom, cassant
l'unicité sur laquelle tout consommateur s'appuie. L'auteur pose un label s'il en
veut un.

## Quand le fichier n'est pas écrit

**Assemblage en échec : pas de fichier.** Une table partielle qu'un débogueur
charge sans le savoir est pire que pas de table.

**Banques ≥ 8 : le fichier sort quand même**, avant le refus d'export. L'assemblage
a réussi, seul le dump plat échoue (ADR 0006) — et c'est justement là que les
adresses sont utiles.

**`--beautify` et `--normalize` : refus explicite.** Ces deux modes transforment du
texte en texte sans passer par l'assembleur : il n'y a pas de table de symboles
sans assemblage. `-E`, lui, assemble, et cohabite donc avec `--sym` : les deux
sorties sont écrites, aucun binaire ne l'est.

## Le chemin par défaut

Dérivé de `-o`, pas de la source. La table décrit le **binaire**, pas le texte :
elle doit voyager avec lui, dans le même répertoire, là où un émulateur ira la
chercher à côté du `.sna` qu'on vient de lui donner. `--sym` ne prend pas
d'argument positionnel — `fantams --sym src.asm` serait ambigu — le chemin
explicite passe par `--sym=<chemin>`.

## Où vit le code

Dans `sym.cpp`, une fonction **pure** qui prend un `Output` et rend une
`std::string` ; le CLI n'écrit que le fichier. Le formatage est la seule partie qui
peut être fausse, c'est donc la partie qui doit être testable sans toucher au
disque. C'est aussi ce qui rendra mécanique son exposition au WASM, laissée hors
de ce lot.

## Conséquences

Le WASM ne renvoie pas encore la table : l'éditeur ne peut pas faire « aller à la
définition ». La structure est en place, l'exposer est un lot à part.

La colonne `store` n'est distincte de `value` que depuis l'implémentation du second
paramètre d'`ORG` (ADR 0005). Elle a été ajoutée avec lui, et non réservée à
l'avance : une colonne qui duplique sa voisine à l'identique pendant des mois
enseigne aux consommateurs qu'ils peuvent l'ignorer, et le jour où elle diverge,
personne ne lit celle qui compte.

Les tests portent sur des **invariants**, non sur un fichier témoin : un golden sur
un format qu'on est en train d'inventer se met à jour à chaque itération et cesse
d'affirmer quoi que ce soit.
