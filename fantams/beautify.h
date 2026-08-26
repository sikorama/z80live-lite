// beautify.h - Mise en forme d'un source assembleur (ADR 0013)
//
// Passe texte -> texte, indépendante du préprocesseur et de l'assembleur : elle
// ne connaît ni adresse, ni octet, ni symbole. Les deux seules décisions
// qu'elle prend — « ce premier mot est-il un label ? », « cette ligne commence-
// t-elle par du code ? » — se lisent dans `keywords.h`. Elle tourne donc même
// sur un source qui ne s'assemble pas.
//
// TROIS RÈGLES, pas une de plus. Les deux premières éteignent un avertissement
// que l'assembleur émet déjà :
//
//   1. Un label écrit sans son deux-points le reçoit. « Est-ce un label ? » se
//      décide sur le texte, jamais par une devinette : les macros définies dans
//      le texte sont collectées d'abord (les deux graphies, `macro nom` et
//      `nom macro`), un nom SEUL n'est un label qu'en colonne 1, et un nom suivi
//      de code ne l'est que si ce code commence par un mot réservé. Le reste est
//      laissé intact. Cf. l'amendement de l'ADR 0013.
//   2. Une ligne de code sans label voit son indentation remplacée par quatre
//      espaces — tabulations comprises.
//   3. Un label suivi de code sur la même ligne en est DÉTACHÉ : le label prend
//      sa ligne, le code la suivante (ADR 0017). C'est un STYLE, pas un canon —
//      `boucle: ld a,1` est parfaitement licite — et il est le défaut pour une
//      raison d'alignement : l'indentation fixe aligne tous les opcodes, un
//      label de longueur variable ne les aligne pas. `detachLabels` l'éteint.
//
//   4. Un LABEL va en colonne 1 ; le corps d'un bloc (`repeat`, `macro`, `if`,
//      `while`, `for`, `struct`) s'indente d'un cran de plus par niveau. La passe
//      n'observe donc pas la convention « seul un label commence en colonne 1 »,
//      elle l'ÉTABLIT — et c'est ce qui fonde la règle 1. `indentBlocks`
//      l'éteint.
//
// Les espaces de FIN de ligne sont retirés partout. Ils ne changent aucun octet
// et aucune règle ne peut les vouloir.
//
// La règle 3 change le nombre de lignes, ce que les deux premières ne faisaient
// pas. Rien dans la chaîne n'en dépend : l'assembleur consomme `pp::Result::lines`
// et leur provenance, jamais un texte mis en forme (`asm_main.cpp`). Seul le
// curseur de l'éditeur retrouvait sa ligne par son numéro.
//
// Toute autre ligne est rendue à l'octet près. En particulier :
//
//   - `sprite 4,12` est laissé intact : `4` n'est pas un mot réservé, donc rien
//     ne distingue ce texte d'un appel de macro. Conséquence : le beautify
//     n'éteint pas TOUS les avertissements — et celui-là doit survivre, puisque
//     le doute est justifié. `pcoltab dw coltab`, lui, est décidable.
//   - `nom EQU 5`, `nom = 5` et `nom MACRO p` ne reçoivent jamais de
//     deux-points : ce sont les formes canoniques d'une définition, et `asm.cpp`
//     les exclut déjà de son avertissement. Les deux étages doivent s'accorder.
//   - Un nom seul mais INDENTÉ est laissé intact : c'est la forme d'un appel de
//     macro sans argument, que le prescan ne voit pas si un `include` l'apporte.
//     Un label indenté garde donc son avertissement — prix consenti.
//
// La largeur n'est pas paramétrable, et rien ne l'est par une directive dans le
// source (ADR 0004 appliqué à l'entrée plutôt qu'à la sortie). Seul le
// détachement des labels l'est, parce qu'il exprime un goût et non une règle.
#pragma once

#include "keywords.h"

#include <string>
#include <vector>

namespace beautify {

// `ph` dit à quel moment le texte est lu, et c'est la seule différence entre
// les deux usages : `kw::Phase::Assembly` pour une source d'origine (macros
// encore inconnues), `kw::Phase::Preprocess` pour une source déroulée (où les
// mots-clés du préprocesseur peuvent subsister).
//
// Les fins de ligne et l'absence ou la présence d'un saut de ligne final sont
// préservées telles quelles. Le nombre de lignes ne l'est que si `detachLabels`
// est faux (règle 3).
// `knownMacros` (MAJUSCULES) complète le prescan textuel. Le prescan ne lit que
// le texte qu'on lui donne : il ne voit ni les `include`, ni une macro pas encore
// écrite. Quand l'appelant sait mieux — il a fait tourner le préprocesseur, qui a
// lu les includes — il le dit ici. Facultatif : sans lui la passe reste correcte,
// simplement plus prudente.
std::string apply(const std::string &src, kw::Phase ph, bool detachLabels = true,
                  bool indentBlocks = true,
                  const std::vector<std::string> &knownMacros = {});

} // namespace beautify
