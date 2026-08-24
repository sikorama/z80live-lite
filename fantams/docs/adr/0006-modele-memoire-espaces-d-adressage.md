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

## Conséquences

`asmb::Output` ne peut plus exposer `image` comme un `vector<uint8_t>` de 64 K, et
`sna::build()` ne peut plus en exiger un. Le harnais de comparaison est concerné
au même titre : `ramDump()` dans `compare.mjs` ne reconstruit que le chunk `MEM0`,
avec le commentaire « fantams ne gère pas le multi-bank ». Sans mise à jour, tout
octet écrit hors des 64 K de base serait déclaré conforme à la référence sans
jamais avoir été lu — un test qui passe en ne regardant rien.

Le calcul du `loadAddress` et du `bin` contigu, aujourd'hui dérivés d'un minimum
et d'un maximum sur une plage unique, deviennent relatifs à un espace donné.
