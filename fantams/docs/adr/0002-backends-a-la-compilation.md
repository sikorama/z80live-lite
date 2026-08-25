# Backends de format sélectionnés à la compilation, pas de plugins dynamiques

Les producteurs de formats de sortie (`sna`, plus tard `dsk`) sont des backends
derrière une interface commune, choisis dans une table statique à la
compilation. Nous renonçons explicitement au chargement dynamique.

## Contexte

L'idée d'enrichir le langage par des directives appelant du code externalisé,
sous forme de plugins, se heurte à la cible réelle du projet : WASM n'a pas de
`dlopen`. Emscripten propose `MAIN_MODULE`/`SIDE_MODULE`, mais au prix d'un
runtime regonflé — précisément ce que nous cherchons à éviter.

Dans cette cible, « plugin » ne peut donc signifier que modularité à la
compilation derrière une interface stable. `sna.h` a déjà exactement cette forme :
fonction pure, options en structure, sans état ni entrées-sorties. Ajouter un
format revient à un fichier de plus avec la même signature et une ligne dans la
table.

> **Amendé par l'ADR 0007.** Le contrat décrit ici sur le modèle de
> `sna::build()` — une fonction rendant un `vector<uint8_t>` unique — ne tient
> pas : plusieurs formats produisent plusieurs fichiers. Un backend rend un
> ensemble d'artefacts nommés. Ce qui suit reste valable par ailleurs.

## Conséquences

Si un besoin de véritable extensibilité au runtime apparaît, la réponse ne sera
pas de charger du code dans fantams mais de déplacer la frontière : le cœur rend
une image mémoire et ses métadonnées, et l'empaquetage se fait chez l'hôte. Cette
option reste ouverte et présente l'avantage de rendre structurellement impossible
la contamination de l'assembleur par les formats de sortie.
