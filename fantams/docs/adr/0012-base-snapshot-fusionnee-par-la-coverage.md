---
status: accepted
---

# La base est un snapshot, fusionnée à l'export par la coverage

Un snapshot produit par un assembleur ne contient que le code assemblé : tout le
reste vaut zéro, donc aucun appel firmware n'y fonctionne. fantams accepte une
**base** — un snapshot de référence pris après le boot d'une machine — sur
laquelle les octets du source sont écrits. La fusion a lieu dans le backend
`sna`, pas dans l'assembleur, et elle suit la **coverage** : chaque adresse
effectivement écrite par le source écrase la base, tout le reste vient de la
base.

## Contexte

Du code qui appelle `&BB5A` a besoin que la ROM ait initialisé la RAM avant lui :
les vecteurs d'indirection au-delà de `&B000`, les variables système, les vecteurs
de RST en bas de mémoire. Aucun assemblage ne produit ces octets, et un
assembleur classique ne peut pas les fournir : lui demander de poser des données
sous le code revient à écrire deux fois la même adresse, ce qu'il refuse.

Le palliatif était de produire un DSK, d'y déposer le binaire et de le lancer
depuis l'émulateur, `autotype` à l'appui. Ça marche et c'est lent — là où un
snapshot démarre instantanément, ce qui, pour une boucle de développement,
change tout.

## Pourquoi un snapshot et non un dump de RAM

Un dump de 64 K ne suffit pas. Le jumpblock firmware saute dans la **ROM basse**,
qui doit être activée ; il faut aussi `I`, `IM 1`, les IFF, la ROM sélectionnée,
un `SP` dans la pile que le firmware s'est choisie, et un gate array cohérent
avec ce que le firmware croit avoir programmé. Or `sna.cpp` écrit
`multiconfig = 0x8D` — ROMs désactivées : avec la RAM seule, `CALL &BB5A` part
dans le vide.

Coder cet état matériel en dur dans `sna.cpp` créerait deux vérités pour une
seule réalité, à tenir alignées à la main. Un snapshot les porte ensemble et
cohérents par construction, et il s'obtient d'une touche dans l'émulateur.

Conséquence assumée : quand une base est fournie, son en-tête est repris
**intégralement** et seul `PC` est patché. `sna::Options{sp, cpcType, version}`
cesse alors d'être consulté. Forcer `SP = 0xC000` comme aujourd'hui casserait le
premier `RET` d'une routine firmware.

## Pourquoi à l'export et non en pré-remplissage de l'image

Pré-remplir `image_` avec la base serait trois lignes et serait faux : `bin` est
`image[lo..hi]`, donc le moindre trou entre deux `ORG` livrerait un extrait de
firmware dans un binaire brut, et le listing ne saurait plus dire qui a écrit
quoi. La base est une décision de livraison, pas d'assemblage : **Image** garde
son sens — ce que le source a produit — et la base rejoint le pipeline de
l'ADR 0007 au seul endroit qui reçoit la collection entière des espaces
d'adressage, le backend `sna`.

## Ce que la coverage paie, et ce qu'elle rend

La fusion « l'octet écrit gagne » exige de distinguer « le source a écrit `0x00`
ici » de « le source n'a rien écrit ici ». L'image seule ne porte pas cette
distinction, et c'est exactement la feature. D'où la coverage, portée par
l'espace d'adressage de l'ADR 0006 et allouée paresseusement : un source de 64 K
paie quatre banques, un source CPR paie les banques qu'il touche, et une banque
jamais écrite ne coûte rien. Un tableau plat dimensionné sur l'image entière
ferait payer 512 K à qui n'écrit que dans trois banques.

La règle alternative — déclarer les plages à reprendre de la base — a été écartée
parce qu'elle oblige le programmeur à énumérer ce qu'il ne connaît pas : quelles
adresses le firmware utilise-t-il.

La coverage se rembourse une seconde fois : elle rend détectable le chevauchement
de deux écritures **du source**, aujourd'hui silencieux. C'est un avertissement,
pas une erreur ; écraser la base n'en émet jamais aucun. Il nomme les deux sites
en conflit, ce qui suppose une provenance par octet — un index sur une table des
sites d'émission, deux octets par octet écrit, dégradé en « site inconnu » au-delà
de 65 535 sites. « Ligne 42 » sans l'expansion ne désigne rien dans un assembleur
à macros. Les octets consécutifs partageant le même couple de sites sont
regroupés en une seule plage : c'est la coalescence, et non un plafond, qui
empêche le flood.

## Ce que la base ne couvre pas

Elle est lue en v1/v2 — en-tête suivi d'un dump plat de 64 K —, donc elle ne
peuple que les banques 0 à 3. Un snapshot v3 à chunks `MEM0` compressés est
refusé en nommant son remplaçant, plutôt que d'embarquer un décompresseur RLE
tant que les bases sont produites à la main. fantams *écrit* pourtant des chunks
v3 (ADR 0007) : l'asymétrie est voulue, lire est contraint par ce que l'émulateur
enregistre, écrire par ce que l'émulateur attend.

Aucun repli silencieux : une base absente, illisible ou de mauvaise taille est
une erreur dure. Un repli sur des zéros produirait un `.sna` qui s'ouvre, démarre
et plante au premier appel firmware — le pire diagnostic possible. Un `cpcType`
de base qui contredit la machine demandée, en revanche, n'est qu'un
avertissement : la base est cohérente avec elle-même, et c'est elle qui fait foi.

La mémoire écran vient de la base comme le reste, sans exception : le `Ready` du
boot reste affiché jusqu'à ce que le code écrive dessus, exactement comme sur la
machine. Toute exception rouvrirait la règle de fusion.

## Rapport à l'ADR 0004

Le cœur ne lit jamais de base nommée dans un source : `--base` est une option
d'invocation, et sa valeur est un **chemin**. Les identifiants de catalogue
(`cpc6128-en`) vivent dans l'hôte, qui les résout, télécharge et dépose le
fichier avant d'appeler fantams — même trajet que les includes. La directive
`;z80: base=…` de z80live est de la configuration d'hôte qui se trouve stockée
dans le texte du source, pas une directive du langage : l'ADR 0004 tient.

Fournir une base pour une sortie qui n'est pas un snapshot est une erreur quand
les deux viennent du même appel. Quand la base est héritée d'une couche
inférieure et la sortie fixée par une couche supérieure, la base est écartée avec
un avertissement.

## Conséquences

`asmb::Output` gagne la coverage et la provenance ; `sna::build()` gagne un
paramètre de base et perd l'usage de trois champs d'options quand elle est
fournie.

La règle de priorité entre couches devient **option explicite > directive
`;z80:` > fiche en base > défaut du code**, et doit être documentée en un seul
endroit. Elle inverse partiellement le commentaire actuel de `parseDirectives` :
la directive fait autorité sur le **stocké**, non sur l'**explicite** — sinon
toucher un contrôle de l'UI reste sans effet. Côté z80live, il faut un catalogue
de bases servies statiquement, et un `base=none` pour annuler une base héritée,
la valeur vide étant aujourd'hui indistinguable de l'absence dans
`buildDirectiveLine`.
