# Bases (ADR 0012)

Une **base** est un snapshot de référence pris **après le boot** d'un CPC : sa
mémoire contient ce que la ROM a initialisé (vecteurs d'indirection firmware
au-delà de `&B000`, variables système, vecteurs de RST en bas de mémoire), et son
en-tête l'état matériel qui va avec (ROM basse activée, `I`, `IM`, `SP` dans la
pile firmware, gate array). Sans elle, un `CALL &BB5A` dans un snapshot produit
par un assembleur part dans le vide.

## Produire une base

1. Démarrer l'émulateur sur la machine visée et **attendre le `Ready`**.
2. Enregistrer un snapshot en **version 2** — en-tête de 256 octets suivi d'un
   dump plat de 64 Ko. Les chunks `MEM0` de la version 3 ne sont pas lus : le
   refus le dit et nomme le remplaçant.
3. Déposer le fichier ici et l'ajouter à `index.json`.

## Le contenu dépend de la ROM

C'est la **ROM** qui doit correspondre, pas seulement le modèle : une ROM
anglaise et une ROM française n'initialisent pas la même mémoire. D'où le champ
`rom` du catalogue, et des identifiants qui nomment la ROM (`cpc6128-en`) plutôt
que la machine.

## Catalogue

`index.json` — un tableau d'objets :

| champ     | rôle                                                        |
|-----------|-------------------------------------------------------------|
| `id`      | identifiant employé par `; z80: base=<id>`                  |
| `label`   | libellé affiché dans l'éditeur                              |
| `file`    | nom du fichier, servi depuis `/bases/`                      |
| `cpcType` | modèle attendu (offset `0x6D` du snapshot) : 0=464 1=664 2=6128 |
| `rom`     | la ROM d'où la base a été capturée                          |

Un `id` du catalogue dont le fichier est absent est une **erreur** à
l'assemblage, jamais un repli silencieux sur des zéros : un tel repli produirait
un `.sna` qui démarre et plante au premier appel firmware.
