# La mise en forme préserve les lignes et refuse de deviner

Le beautify est une passe texte vers texte, indépendante du préprocesseur et de
l'assembleur, tenue par trois règles :

1. **Bijection sur les lignes.** La ligne *n* de l'entrée devient la ligne *n* de
   la sortie. Jamais de scission, jamais de jointure.
2. **Refus de deviner.** Le deux-points n'est ajouté qu'à un label **seul sur sa
   ligne**. Un premier mot suivi de quoi que ce soit est laissé intact.
3. **Rien de paramétrable.** Quatre espaces, pas de largeur configurable, pas de
   directive dans le source, pas d'option d'invocation pour choisir les règles.

## Contexte

L'assembleur signale deux écarts de mise en forme : un label écrit sans son
deux-points, et une instruction laissée en colonne 1. Signaler sans savoir
corriger fait porter à l'auteur un travail purement mécanique — d'où cette
passe. Mais une passe qui réécrit du source est une passe qui peut le détruire,
et chacune des trois règles ferme une porte précise.

**La bijection** protège la provenance. Un diagnostic se recale dans l'éditeur
par un décalage de lignes ; une source déroulée porte le fichier et la ligne
d'origine de chacune de ses lignes. Une mise en forme qui scinde `start: ld a,1`
en deux lignes décale tout ce qui suit et rompt ce lien — au moment même où
l'auteur en a le plus besoin, puisqu'il lit une vue mise en forme pour
comprendre une erreur. Le prix est assumé : la sortie n'est pas canonique. Deux
sources qui ne diffèrent que par le regroupement label-instruction restent
différentes après mise en forme.

**Le refus de deviner** protège les appels de macro. Sur une source non
déroulée, `sprite 4,12` en colonne 1 est un appel de macro, mais rien dans le
texte ne le distingue d'un label suivi d'une directive inconnue : les macros ne
sont connues qu'après leur collecte, et un `include` peut les apporter. Ajouter
un deux-points à `sprite` produirait un source qui ne s'assemble plus. La règle
« label seul sur sa ligne » traverse ce cas sans avoir à trancher, parce que la
forme que les avertissements visent — un nom sur sa ligne, l'instruction en
dessous — est précisément celle où le reste de la ligne est vide. Conséquence
directe : le beautify **n'éteint pas tous** les avertissements. Celui de
`sprite 4,12` survit, et c'est le bon résultat — là, l'assembleur a raison de
douter.

**L'étendue de la règle 2.** L'indentation couvre les instructions **et les
directives**, alors que l'avertissement de l'assembleur ne parle que des
instructions. Ce n'est pas une règle de plus, c'est la même règle appliquée à
tout ce qui est du code : une sortie où `ld a,1` serait indenté mais `org
#8000` resterait en colonne 1 se lirait comme un bug de la mise en forme. Le
choix inverse aurait été d'élargir l'avertissement aux directives, mais il
ferait crier tous les en-têtes rasm (`BUILDSNA`, `BANKSET`, `ORG`, `RUN` en
colonne 1) et noierait le signal que l'avertissement existe pour porter.

**Le rien-de-paramétrable** est l'ADR 0004 appliqué à l'entrée plutôt qu'à la
sortie. Chaque règle ajoutée est une règle que quelqu'un voudra désactiver, et
chaque paramètre exige un endroit où le mettre — un cran de plus vers la
directive de mise en forme dans le source. Les deux règles retenues ont
l'avantage rare d'être déjà justifiées par un avertissement existant. La casse
des mnémoniques ne l'est pas : rien ne dit aujourd'hui si `ld` ou `LD` est
correct, et le beautify n'est pas l'endroit où en décider.

## Conséquences

Le beautify vit dans son propre module du cœur, sans état ni entrées-sorties,
et prend un texte pour rendre un texte. Il ne dépend pas de l'assemblage : les
deux seules décisions qu'il prend se lisent dans le parseur, sans adresse ni
octet. Il tourne donc dès que le préprocesseur aboutit — y compris quand
l'assemblage échoue, cas où la source déroulée est le plus utile.

Il s'applique aux deux entrées, avec la phase pour seule différence — et la
phase désigne **qui va lire le texte**, non d'où il sort :

- la source d'origine, celle du tampon de l'éditeur, est le texte que le
  **préprocesseur** va lire : ses mots-clés y sont vivants. Au mauvais cran,
  `MEND` seul sur sa ligne n'est pas réservé, donc lu comme un label seul, donc
  pourvu d'un deux-points — et le source est détruit. Le cas a été trouvé à
  l'essai, pas au tableau ;
- la source déroulée est le texte que l'**assembleur** va lire : les mots-clés du
  préprocesseur n'y sont plus. Les y traiter comme réservés ferait indenter un
  label nommé `read` au lieu de lui donner son deux-points.

Cela exige un vocabulaire de mots réservés indexé par phase, là où trois copies
de la même liste et de la même fonction d'épluchage de label coexistaient
(`parser.cpp`, `asm.cpp`, `pp.cpp`) — refactor que cette décision rend
nécessaire plutôt qu'optionnel. Les trois listes formaient une inclusion
stricte : ce ne sont pas des copies qui ont dérivé, ce sont trois phases, et
c'est ce que le paramètre nomme.

Deux points d'entrée l'exposent, parce que les deux entrées ne se ressemblent
pas : `--beautify` met en forme un source sans le préprocesser ni l'assembler —
c'est ce que le bouton de l'éditeur appelle —, tandis que `-E` met en forme la
source déroulée qu'il produit déjà. Un `-E` qui ne déroulerait rien serait un
contresens, d'où le drapeau séparé plutôt qu'une option de `-E`.

Trois propriétés sont tenues par les tests, dans cet ordre de gravité : les
octets assemblés sont inchangés ; la mise en forme est idempotente ; les deux
avertissements s'éteignent, aux exceptions de la règle 2 près — un test vérifie
justement qu'un avertissement **survit** sur `sprite 4,12`.

La source déroulée étant mise en forme par définition, le préprocesseur perd son
propre avis sur le sujet : ses sous-lignes, jusqu'ici indentées d'une
tabulation, le sont de quatre espaces comme le reste.

Cette décision ne couvre pas l'alignement des commentaires, la casse des
mnémoniques ni l'espacement des opérandes. Les ajouter plus tard reste possible,
mais chacun devra d'abord exister comme avertissement — c'est ce qui distingue
une correction d'un goût.

## Amendement — la préservation des lignes ne survit qu'en option

Le titre de cet ADR est désormais inexact pour le beautify lui-même. Sa règle 3
(ADR 0017) détache « label: instruction » en deux lignes, par défaut, et rompt
donc la bijection.

Ce qui a rendu la décision possible est que la justification écrite ici était
fausse. Cet ADR affirmait que la bijection protégeait « la provenance et le
recalage des diagnostics ». Elle ne protège ni l'un ni l'autre : l'assembleur
consomme `pp::Result::lines`, chaque ligne portant son fichier et son numéro
d'origine (`asm_main.cpp`), et le beautify n'est **jamais** dans le chemin
d'assemblage. En mode `--beautify` il produit un fichier que l'auteur réassemble
ensuite ; en mode `-E` il met en forme un texte qu'on écrit et qu'on ne relit pas.

Le seul consommateur réel était le curseur de l'éditeur, qui retrouvait sa ligne
par son numéro. Il corrige maintenant le décalage en comptant les labels collés
au-dessus de lui.

La préservation des lignes reste vraie, et testée, sous `--no-detach-labels`.

## Note — `--normalize` n'est pas de la mise en forme

Le reste de cet ADR vaut pour le **beautify**.

`--normalize` (ADR 0017) est un outil **distinct**, pas une option du beautify :
il canonise sans dérouler, et il change délibérément le nombre de lignes —
`push hl,de` devient deux lignes, `ld a,1:inc a` aussi. Il ne contredit pas la
préservation des lignes énoncée ici, il n'y est pas soumis.

Ce que les deux partagent, en revanche, est le refus de deviner et l'égalité des
octets assemblés.
