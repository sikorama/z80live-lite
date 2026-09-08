# Liste de contrôle : la vérification dans le navigateur

Les vérifications que **seul un humain** peut faire. Tout le reste du chantier
est sous test : l'équivalence entre l'adaptateur natif et l'adaptateur WASM vit
dans fantams (`tests/accept_wasm_equiv.sh`), le comportement de l'hôte dans
`npm run test:wasm`. Ce qui reste ici, aucune suite ne le remplace — et c'est
donc la seule étape qu'on peut déclarer faite sans l'avoir faite.

Elle se rejoue sans relire la spec. Chaque point énonce **le geste** et **le
résultat attendu**.

## Avant

```bash
npm run build      # WASM depuis les sources du submodule, puis la SPA
npm run dev        # http://localhost:3000
```

## La liste

| # | Geste | Résultat attendu |
|---|---|---|
| 1 | Ouvrir la page. | L'en-tête affiche `z80live`, puis, à sa droite, une ligne de la forme `fantams AAAA-MM-JJ (compile AAAA-MM-JJ)`. Elle est là **sans qu'on ait rien cliqué**. |
| 2 | Lire les deux dates du point 1. | Elles sont lisibles toutes les deux, et l'écart entre elles se voit. Un écart de plusieurs mois veut dire que l'artefact chargé est en retard sur les sources : reconstruire avant d'aller plus loin. |
| 3 | Choisir une source qui assemble, assembleur `fantams`, type de sortie `sna`. Lancer l'assemblage. | Le journal affiche une ligne verte `✔ fantams → .sna <N> bytes in <T> ms`, avec `N` = 65792. |
| 4 | Ouvrir le panneau « Code sent to the assembler (after the preprocessor) ». | La **source déroulée** s'affiche : macros expansées, boucles déroulées, includes insérés. Ce n'est pas le texte de l'éditeur. |
| 5 | Dans l'éditeur, remplacer une ligne par `zorglub xyz`. Réassembler. | Le journal passe au rouge et affiche `unknown directive/mnemonic: 'xyz'`, avec le numéro de ligne **de la source affichée** — cliquer dessus place le curseur sur cette ligne. |
| 6 | Annuler la ligne fautive, réassembler, puis récupérer le snapshot par le bouton de téléchargement. | Un fichier `.sna` arrive. Ses huit premiers octets sont `MV - SNA` (`head -c 8 <fichier>`), et il pèse 65792 octets. |
| 7 | Recharger la page (F5). | La version du point 1 réapparaît, identique. Elle ne dépend d'aucun état de session. |

## Après

Consigner ci-dessous la date du passage et ce qui a été observé. Une liste
parcourue est une liste **signée** : sans cette trace, le point 15 de la spec du
chantier n'est pas tenu.

## Journal des passages

| Date | Par | Version fantams affichée | Résultat |
|---|---|---|---|
| _(à remplir)_ | | | |
