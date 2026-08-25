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
//   1. Un label écrit sans son deux-points, SEUL sur sa ligne, le reçoit.
//   2. Une ligne de code sans label voit son indentation remplacée par quatre
//      espaces — tabulations comprises.
//   3. Un label suivi de code sur la même ligne en est DÉTACHÉ : le label prend
//      sa ligne, le code la suivante (ADR 0017). C'est un STYLE, pas un canon —
//      `boucle: ld a,1` est parfaitement licite — et il est le défaut pour une
//      raison d'alignement : l'indentation fixe aligne tous les opcodes, un
//      label de longueur variable ne les aligne pas. `detachLabels` l'éteint.
//
// La règle 3 change le nombre de lignes, ce que les deux premières ne faisaient
// pas. Rien dans la chaîne n'en dépend : l'assembleur consomme `pp::Result::lines`
// et leur provenance, jamais un texte mis en forme (`asm_main.cpp`). Seul le
// curseur de l'éditeur retrouvait sa ligne par son numéro.
//
// Toute autre ligne est rendue à l'octet près. En particulier :
//
//   - `sprite 4,12` est laissé intact. C'est peut-être un appel de macro, et
//     rien dans le texte ne le distingue d'un label suivi d'une directive
//     inconnue : ajouter un deux-points produirait un source qui ne s'assemble
//     plus. Conséquence : le beautify n'éteint pas TOUS les avertissements — et
//     celui-là doit survivre, puisque le doute est justifié.
//   - `nom EQU 5` et `nom = 5` ne reçoivent jamais de deux-points : le reste de
//     la ligne n'est pas vide, donc la règle 1 ne s'applique pas.
//
// La largeur n'est pas paramétrable, et rien ne l'est par une directive dans le
// source (ADR 0004 appliqué à l'entrée plutôt qu'à la sortie). Seul le
// détachement des labels l'est, parce qu'il exprime un goût et non une règle.
#pragma once

#include "keywords.h"

#include <string>

namespace beautify {

// `ph` dit à quel moment le texte est lu, et c'est la seule différence entre
// les deux usages : `kw::Phase::Assembly` pour une source d'origine (macros
// encore inconnues), `kw::Phase::Preprocess` pour une source déroulée (où les
// mots-clés du préprocesseur peuvent subsister).
//
// Les fins de ligne et l'absence ou la présence d'un saut de ligne final sont
// préservées telles quelles. Le nombre de lignes ne l'est que si `detachLabels`
// est faux (règle 3).
std::string apply(const std::string &src, kw::Phase ph, bool detachLabels = true);

} // namespace beautify
