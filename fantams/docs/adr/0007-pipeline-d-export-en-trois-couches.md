---
status: accepted
---

# Pipeline d'export en trois couches, et noms de morceaux dérivés

L'export se décompose en trois couches composables plutôt qu'en un backend
monolithique par format :

```
image → découpage → morceaux → encapsulation → conteneur → artefacts
```

Un **morceau** porte `{ banque, adresse logique, point d'entrée, octets }`. Son
nom n'est pas déclaré mais dérivé de son emplacement.

## Contexte

Les formats visés — binaire brut, binaire préfixé AMSDOS, DSK, SNA, CPR, CRO —
partagent leurs constituants bien plus qu'il n'y paraît. Le découpage (un fichier
par bloc `ORG`, un englobant tout, un par banque de 16 K ou de 64 K) est commun au
binaire brut, au binaire AMSDOS et au DSK. L'en-tête AMSDOS de 128 octets est
commun au binaire AMSDOS et au DSK. Écrire DSK comme un bloc autonome reviendrait
à réimplémenter les deux.

C'est précisément par là qu'un assembleur dérive : chaque format finissant par
avoir ses propres options de découpage, mutuellement incohérentes. Composé, le
coût est de quatre découpages plus une encapsulation plus un conteneur par
format ; écrit à la main, c'est six fois quatre comportements.

Le DSK cesse alors d'être un format pour devenir une composition, et « binaire
AMSDOS sur disquette » vient sans effort supplémentaire.

## Le SNA reste à part

Le SNA n'est pas un conteneur de morceaux mais un état machine : il lui faut la
mémoire entière plus des registres, une palette et un état CRTC, que `sna.cpp`
fabrique déjà. Il ne reçoit donc pas de morceaux nommés mais la collection
d'espaces d'adressage de l'ADR 0006. Reconnaître cette asymétrie coûte moins cher
que d'inventer un découpage « tout en un » pour le faire ressembler aux autres.

Il transporte les banques sous forme de chunks `MEM0`, `MEM1`… de la version 3,
et non par un dump plat de 128 K : c'est ce qu'attendent les émulateurs, et cela
permet de n'émettre que les espaces effectivement remplis. `sna.cpp` écrit
aujourd'hui un en-tête suivi d'un dump plat de 64 K avec `h[0x6B] = 64`, ce qui
n'a nulle part où loger un octet écrit en banque 4.

La compression RLE de ces chunks est souhaitable mais facultative : un lecteur
accepte les deux, et elle peut donc arriver après. Un piège à ne pas manquer le
jour où elle sera écrite — l'encodage est `0xE5 <compte> <valeur>`, et un octet
`0xE5` littéral doit être échappé en `0xE5 0x00`, sur deux octets et sans
troisième. Cette exception est vérifiée empiriquement contre la sortie de rasm
(`compare/compare.mjs`, fonction `decodeRLE`).

## Nommage

Un nom de morceau est dérivé du couple (banque, adresse logique) du morceau,
et non « de l'`ORG` » — la nuance compte pour le découpage par banque, où
plusieurs `ORG` tombent dans le même morceau et où l'adresse retenue est celle du
début du morceau. Deux morceaux ne peuvent alors porter le même nom que s'ils
occupent le même emplacement, ce que l'assembleur détecte déjà comme
recouvrement.

La forme dépend du conteneur, parce que la contrainte dépend du conteneur :

```
hôte :  <global>_b04_4000.bin     global = nom du source principal par défaut,
                                  redéfinissable à l'invocation
DSK  :  B04_4000.BIN              b + 2 chiffres hexa + _ + 4 chiffres hexa
                                  = 8 caractères, la limite AMSDOS exactement
```

Dériver plutôt que déclarer découle de l'ADR 0004 : `SAVE "file.bin", start, len`
décrit une livraison, pas un programme. Et le nom porte l'information qui compte à
l'exécution — où charger le bloc — plutôt qu'une étiquette arbitraire.

## Conséquences

**Cet ADR amende l'ADR 0002.** Le contrat de backend y était décrit sur le modèle
de `sna::build()`, qui rend un `vector<uint8_t>` unique. Le binaire brut, le
binaire AMSDOS et le DSK produisent plusieurs fichiers : un backend rend un
ensemble d'artefacts nommés. Le fond de l'ADR 0002 — sélection à la compilation,
aucun chargement dynamique — reste inchangé.

Sur un DSK, les huit caractères sont saturés : il ne reste aucune place pour un
préfixe. Le seul degré de liberté restant est l'extension, sur trois caractères.
La question ne se pose pas tant qu'un seul jeu de fichiers occupe une disquette,
et sera tranchée quand elle se présentera — mais c'est là, et nulle part ailleurs,
que la place existe.

Le CRO introduit une notion qu'aucun autre format n'a : une arborescence, donc des
chemins. C'est une affaire de conteneur et non de mémoire, mais un contrat de
backend rendant une liste plate d'artefacts devra être élargi le jour venu.
