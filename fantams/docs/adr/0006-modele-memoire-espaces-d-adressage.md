---
status: accepted
---

# Le modèle mémoire devient une collection d'espaces d'adressage

L'image assemblée cesse d'être un tableau plat de 64 K pour devenir une
collection indexée d'espaces d'adressage : banques RAM 0..n et ROMs 0..n, chacune
de 16 K. Les ROMs ne sont pas des banques RAM de numéro élevé.

## Contexte

`asm.cpp` alloue `image_` en 65 536 octets (`asm.cpp:117`) et `emit()` écrit en
`image_[pc_ & 0xFFFF]` (`asm.cpp:156`). Il n'existe aucun endroit où loger une
banque : le masquage 16 bits *est* le modèle mémoire.

Deux décisions convergent pour le remplacer. L'ADR 0005 introduit `Bn:adresse`,
donc des octets qui appartiennent à une banque. Et les formats CPR et CRO — des
cartouches CPC+ et leur généralisation — encapsulent des ROMs de 16 K, ce qui
suppose de pouvoir assembler *dans* une ROM, et rend implémentable le préfixe
`R:` que l'ADR 0005 s'était contenté de réserver.

Ces deux besoins arrivent au même endroit du code. Reconstruire le modèle mémoire
deux fois n'aurait pas de sens, d'où le choix de trancher la forme maintenant,
sans implémenter les ROMs pour autant.

## Pourquoi pas des banques de numéro élevé

Traiter une ROM comme une banque RAM à un autre index serait plus simple et
serait faux. Une ROM est en lecture seule, elle est sélectionnée par un mécanisme
distinct de celui des banques RAM, et elle ne participe pas au même espace. Les
confondre reviendrait à accepter silencieusement des écritures qui n'ont aucun
sens sur la machine cible — exactement le type d'erreur qu'un assembleur est bien
placé pour refuser.

## Le format de sortie : un dump plat de 128 K, pas des chunks

L'export suit le modèle mémoire, mais **pas** par les chunks `MEM0`/`MEM1` du
snapshot v3. Le champ « taille du dump » de l'en-tête (`0x6B`–`0x6C`, en Ko)
accepte 128 aussi bien que 64 : un dump plat de 131 072 octets porte les banques
0 à 7 dans l'ordre, sans structure supplémentaire.

Le choix se décide sur la compatibilité, pas sur l'élégance. **Tout ce qui lit un
dump de 64 K lit un dump de 128 K** — c'est le même format, avec un champ de
taille différent — alors que les chunks demandent un lecteur qui les connaisse.
L'émulateur embarqué du projet (`emu/tiny8bit/cpc.wasm`) ne porte aucune trace de
`MEM0`, et `sna::parseBase` refuse déjà les snapshots chunkés en lecture. Produire
un format que la chaîne d'aval ne relit pas aurait rendu la fonctionnalité
invérifiable au moment même où elle devient testable.

Les chunks restent le bon format pour ce qu'un dump plat ne peut pas faire :
au-delà de 128 K, et la compression. Ils se poseront par-dessus celui-ci sans
avoir à le défaire. En attendant, écrire au-delà de la **banque 7** s'assemble mais
ne s'exporte pas, et le refus nomme les banques concernées.

**La base reste de 64 K.** Elle est un snapshot post-boot : le firmware ne vit pas
dans l'extension, et il n'y a donc rien à préserver au-delà des 64 K de base. Les
banques hautes viennent de l'image seule, sans départage par la coverage.

**Le binaire brut ne suit pas.** `-o fichier.bin` rend un intervalle contigu
d'adresses logiques ; il n'a aucune place pour dire « ces octets-là sont en
banque 5 ». Un source banqué exporté en binaire brut reçoit un avertissement.

**Le harnais de comparaison est concerné**, comme cet ADR l'annonçait :
`ramDump()` ne reconnaissait que le dump plat de 64 K et les chunks. Face à un
dump de 128 K, il serait tombé dans sa branche « chunks », n'aurait rien trouvé et
aurait rendu un tampon de zéros — déclarant conformes des octets jamais lus. Il
rend désormais 128 K dans les trois cas, et **lève une erreur** s'il ne reconnaît
aucune forme, plutôt que de rendre des zéros.

## Conséquences

`asmb::Output` ne peut plus exposer `image` comme un `vector<uint8_t>` de 64 K, et
`sna::build()` ne peut plus en exiger un. Le harnais de comparaison est concerné
au même titre : `ramDump()` dans `compare.mjs` ne reconstruit que le chunk `MEM0`,
avec le commentaire « fantams ne gère pas le multi-bank ». Sans mise à jour, tout
octet écrit hors des 64 K de base serait déclaré conforme à la référence sans
jamais avoir été lu — un test qui passe en ne regardant rien.

Le calcul du `loadAddress` et du `bin` contigu, aujourd'hui dérivés d'un minimum
et d'un maximum sur une plage unique, deviennent relatifs à un espace donné.
