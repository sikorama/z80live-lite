# Cœur pur et adaptateurs fins, plutôt qu'un CLI émulé

Le cœur de fantams est une fonction pure sans état ni entrées-sorties : les six
modules ne contiennent aucun `fstream`/`fopen`, et les fichiers inclus arrivent
par le callback `pp::FileProvider`. Nous en faisons un invariant et exposons ce
cœur directement à chaque hôte (CLI natif, WASM, serveur, CI, MCP) via un
adaptateur fin, au lieu de faire imiter à fantams l'interface en ligne de
commande de rasm et sjasmplus.

## Contexte

L'intégration WASM initiale reproduisait un CLI — `argv`, `callMain`, système de
fichiers virtuel — pour que `wasm/assemble.mjs` traite les trois assembleurs par
un chemin unique. rasm et sjasmplus sont des programmes externes non
modifiables ; fantams ne l'est pas, et payait ce coût d'imitation sans
contrepartie : `-sFORCE_FILESYSTEM`, l'écriture de `/in.asm` puis la relecture du
binaire produit en devinant son extension, et l'inspection d'`ExitStatus` pour
récupérer un code de retour.

Ce n'est pas resté théorique : les deux seules divergences d'octets du corpus ne
venaient pas de l'assembleur mais de `wrapFantams`, dont la regex `hasLiteOrg`
devinait à tort qu'une source portait déjà son `ORG` parce qu'elle en contenait
un 200 lignes plus bas. L'encodeur, lui, était byte-identique à rasm.

## Conséquences

Les paramètres (`org`, `run`, options de backend) passent par une structure, plus
par du texte concaténé en tête de source : il n'y a plus d'en-tête à fabriquer ni
de regex à avoir raison. Les diagnostics sortent structurés — `pp::Diagnostic` et
`asmb::Diagnostic` existent déjà mais étaient aplatis en `fprintf(stderr)`, au
point que le harnais de comparaison devait les reparser par expression
régulière.

En contrepartie, fantams cesse d'être interchangeable avec rasm et sjasmplus dans
`assemble.mjs` : il y devient un cas particulier, mieux traité que les deux
autres. C'est le prix assumé.

La règle de tri qui en découle, et qui protège le projet des dérives reprochées à
rasm : **une fonctionnalité qui n'a pas de sens pour les cinq hôtes est un
adaptateur, pas le cœur.**
