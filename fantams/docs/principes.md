# Principes

Les critères dégagés en concevant le langage, sous une forme réutilisable. Un
ADR tranche une question ; un principe permet de trancher la suivante sans
rejouer la discussion. Chacun est né d'un cas réel, cité en fin de ligne.

## Formules

Les mêmes règles, énonçables en une phrase, pour les dire à quelqu'un.

### Les trois couches

Le préprocesseur déroule les macros et canonise le code, l'assembleur encode,
le beautify met en forme.

Le préprocesseur ne produit qu'une chose : du Z80 canonique, à plat.

L'assembleur ne connaît que le Z80 et les expressions — ni macro, ni boucle, ni condition.

Le beautify ne change que ce qui ne change pas le programme.

Le préprocesseur est toujours obligatoire, le beautify ne l'est jamais.

### Ce qu'accepte chacun

L'assembleur tolère des orthographes, jamais des structures.

Ce qui devient plusieurs instructions disparaît avant l'assembleur.

Aucune source ne s'assemble seulement après avoir été mise en forme.

Le mode strict n'ajoute rien, il refuse.

### Le canon

Une ligne qui porte un opcode n'en porte qu'un.

Plusieurs opcodes sur une ligne, ce n'est pas du code, c'est du sucre.

Un label n'est pas un opcode.

Le canon est une structure, jamais un goût.

### Les mots réservés

Les mots réservés sont réservés : aucun identifiant utilisateur ne porte un nom du langage ou de la machine.

Un registre est un mot de la machine, donc réservé.

Le deux-points n'affranchit pas : `call:` est illégal.

### Substitution

Un argument nu vaut sa valeur, `{arg}` vaut son texte.

Les accolades ne sont jamais un préfixe de format.

### Boucles

Les index commencent à 0.

`to` inclut la borne, `until` l'exclut.

### Outils

Normaliser, c'est canoniser sans dérouler.

`-E`, c'est normaliser **puis** dérouler les macros et les boucles — d'où
l'incompatibilité des deux.

Normaliser ne change pas le programme.

### Divergences

Une divergence silencieuse n'est pas une divergence, c'est un bug.

On ne devine jamais.

## Divergences et diagnostics

**Une divergence doit être bruyante.** Une incompatibilité qui produit un refus
est assumable : la source ne s'assemble pas, l'auteur est prévenu, le nombre de
sources à éditer se chiffre. Une incompatibilité qui produit du code
*apparemment correct* ne l'est pas — des données décalées d'un cran ne se
diagnostiquent jamais. Quand la divergence est indétectable ligne par ligne, on
rend bruyante la **construction** qui la porte : tout usage est signalé, sans
faux positif possible, puisque c'est la construction elle-même qu'on déprécie.
_Index de `repeat` passé à 0._

**Un diagnostic ne parle que d'une faute.** Un avertissement qui se déclenche
sur du code correct sera désactivé, et il emportera avec lui ceux qui parlaient
juste. Avant d'ajouter un avertissement, décrire l'ensemble des sources qui le
déclencheront à tort : s'il n'est pas vide, le diagnostic désigne un style, pas
une faute.
_Substitution nue d'un argument de macro ; « préfère `{}` »._

**Un message de diagnostic ne porte pas de donnée variable.** La déduplication
se fait sur `(fichier, ligne, message)` ; un message qui embarque une valeur
change à chaque itération et contourne son propre garde-fou. La clé doit être
proportionnelle au nombre de problèmes **distincts**, jamais au nombre de
déroulages. Quand un avertissement concerne deux lieux — un corps de macro et
son site d'appel — les nommer tous les deux règle la déduplication au bon grain.
_`readAtPP` : quatorze avertissements pour une ligne (`pp.cpp:284`)._

**Un fait, un étage.** Deux diagnostics pour la même faute, dont le plus faible
arrive en premier, c'est du bruit avec une étape de plus. Le refus appartient à
l'étage qui peut le **nommer exactement**.
_L'avertissement du préprocesseur contre le `duplicate symbol` de l'assembleur._

**Un chiffre du corpus avant d'abaisser le plafond de compatibilité.** Le corpus
est un instrument de mesure ; refuser une notation sans savoir combien de textes
distincts elle concerne, c'est décider à l'aveugle. À défaut de chiffre, le dire
plutôt que l'inventer.

## Notations

**Rien qui change le sens ne se devine.** Aucune borne, aucun index, aucun
moment de résolution ne doit dépendre d'une convention à mémoriser : la ligne
doit le dire. C'est la forme générale du grief contre les accolades de rasm — une
syntaxe, deux rôles grammaticaux, et rien dans la ligne ne dit lequel s'applique.
_`to` inclusif et `until` exclusif plutôt qu'un `to` dont il faut savoir le sens._

**Deux sémantiques, deux orthographes.** Quand une différence est observable, elle
doit être écrite. Réciproquement, on n'ajoute pas d'orthographe à sémantique
égale.
_Argument nu (appel par valeur) contre `{arg}` (appel par nom)._

**On n'étend pas une notation existante, on en ajoute une.** Changer le sens
d'une forme déjà écrite casse en silence ; ajouter un mot ne casse rien. C'est le
geste de `LET` face à `=`, et celui de `for` face à `repeat n,i`.

**Les mots réservés sont réservés.** Aucun identifiant utilisateur — paramètre,
index de boucle, symbole, label — ne porte un nom du vocabulaire de la machine ou
du langage, registres et conditions compris. Une règle vraie partout coûte moins
à retenir que trois règles justes par position, et une exception nommée détruit
le bénéfice qui justifiait la règle.

**Une règle avec une exception n'est plus la règle qui valait le coût.** Si le
prix d'une exception paraît trop élevé, c'est la règle qu'il faut changer, pas la
trouer.
_`end:` devenu illégal, sans dérogation._

## Couches et placement

**Distinguer ce qui est forcé de ce qui est choisi.** Deux choses seulement
forcent une réécriture dans le préprocesseur : produire du **texte source à
ré-analyser** (macros, boucles), et exiger une **valeur avant qu'aucune adresse
existe** (contrôle de flux). Tout le reste — sucre, facilités d'écriture,
orthographes héritées — est une **politique**, pas une conséquence. Un assembleur
peut parfaitement émettre plusieurs octets depuis une ligne : `emitDB` le fait
déjà (`asm.cpp:337`). Ne jamais justifier un placement par une contrainte qu'on
n'a pas vérifiée.

**La source déroulée juge le placement.** Parmi les choix libres, la canonisation
**un-vers-plusieurs** va au préprocesseur, pour que `-E` soit un programme Z80 à
une instruction par ligne où l'auteur lit exactement ce qui sera assemblé.
L'**un-pour-un** peut rester à l'assembleur : la fidélité n'y est pas en jeu.
_`push hl,de` au préprocesseur, `defb` à l'assembleur._

**La canonisation est la dernière étape.** Elle n'intervient qu'en aval de toute
substitution (`pp.cpp:551-559`), et une règle de canonisation ne produit que du Z80
canonique — jamais de directive de préprocesseur, jamais de substitution, jamais
de sucre. C'est ce qui garantit qu'une seule passe suffira toujours, et ce qui
met `-E`, `--normalize` et l'assembleur d'accord **par construction** plutôt que
par chance.

**La tolérance et la pureté ne s'arbitrent pas : on ajoute un mode strict.** Le
laxisme reste le défaut, compatible ; la garantie s'obtient en la demandant. Le
mode strict est le `LET` du vocabulaire. Il porte sur le **pipeline**, pas sur une
couche : l'utilisateur demande « ma source est-elle canonique ? », pas « la
couche interne n° 3 a-t-elle vu du canon ? ».

**Un mode que le projet ne lance pas sur ses propres sources pourrit.** Les
exemples et les tests s'assemblent en mode strict, sinon le mode strict est du
code mort qui ne défend rien.

## Outils

**Aucune source ne doit être assemblable seulement après passage par un outil de
mise en forme.** Un outil cosmétique qui devient obligatoire est une étape de
compilation déguisée, et la garantie qui le rendait sûr — ne rien changer au
programme — disparaît. Tout ce qu'un outil de forme canonise, la chaîne
d'assemblage doit déjà l'accepter.

**Une transformation outillée énonce une promesse vérifiable.** Idempotence, et
neutralité de l'assemblage : `assemble(f(src)) == assemble(src)`, testée
(`beautify_test.cpp:78`). Une transformation sans propriété testable n'est pas un
outil, c'est une habitude.

**Rétrécir la promesse plutôt que la trahir.** Quand un outil ne peut pas tenir sa
promesse sur une classe de cas — une ligne dont la forme dépend du site d'appel —
la bonne réponse est de réduire ce qu'il promet, pas d'avertir sur chaque cas
qu'il n'atteint pas.
_`--normalize` ramené aux orthographes et aux opcodes composés._

**Le canon est structurel, le style est optionnel.** « Une ligne portant un opcode
n'en porte qu'un, sous son orthographe canonique » est un canon : non négociable,
outillé par défaut. « Le label sur sa propre ligne » est un style : justifiable —
l'indentation fixe aligne les opcodes, un label de longueur variable ne les
aligne pas — mais toujours désactivable.

**Une transformation par défaut doit être indiscutable.** Ce que personne ne peut
refuser va dans le comportement par défaut ; tout ce qui exprime un goût s'active
ou se désactive. Le critère n'est ni « espace contre jeton » ni « déterminé contre
devinné » : c'est « quelqu'un peut-il légitimement ne pas le vouloir ? ».
