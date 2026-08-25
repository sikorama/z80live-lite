// beautify.h - Mise en forme d'un source assembleur (ADR 0013)
//
// Passe texte -> texte, indépendante du préprocesseur et de l'assembleur : elle
// ne connaît ni adresse, ni octet, ni symbole. Les deux seules décisions
// qu'elle prend — « ce premier mot est-il un label ? », « cette ligne commence-
// t-elle par du code ? » — se lisent dans `keywords.h`. Elle tourne donc même
// sur un source qui ne s'assemble pas.
//
// DEUX RÈGLES, pas une de plus. Chacune éteint un avertissement que
// l'assembleur émet déjà :
//
//   1. Un label écrit sans son deux-points, SEUL sur sa ligne, le reçoit.
//   2. Une ligne de code sans label voit son indentation remplacée par quatre
//      espaces — tabulations comprises.
//
// Toute autre ligne est rendue à l'octet près. En particulier :
//
//   - `start: ld a,1` reste sur UNE ligne : la mise en forme est bijective sur
//     les lignes, sans quoi la provenance et le recalage des diagnostics dans
//     l'éditeur sautent. La sortie n'est donc pas canonique, et c'est assumé.
//   - `sprite 4,12` est laissé intact. C'est peut-être un appel de macro, et
//     rien dans le texte ne le distingue d'un label suivi d'une directive
//     inconnue : ajouter un deux-points produirait un source qui ne s'assemble
//     plus. Conséquence : le beautify n'éteint pas TOUS les avertissements — et
//     celui-là doit survivre, puisque le doute est justifié.
//   - `nom EQU 5` et `nom = 5` ne reçoivent jamais de deux-points : le reste de
//     la ligne n'est pas vide, donc la règle 1 ne s'applique pas.
//
// Rien n'est paramétrable : ni la largeur, ni le jeu de règles, ni par une
// directive dans le source (ADR 0004 appliqué à l'entrée plutôt qu'à la sortie).
#pragma once

#include "keywords.h"

#include <string>

namespace beautify {

// `ph` dit à quel moment le texte est lu, et c'est la seule différence entre
// les deux usages : `kw::Phase::Assembly` pour une source d'origine (macros
// encore inconnues), `kw::Phase::Preprocess` pour une source déroulée (où les
// mots-clés du préprocesseur peuvent subsister).
//
// Le nombre de lignes, les fins de ligne et l'absence ou la présence d'un saut
// de ligne final sont préservés tels quels.
std::string apply(const std::string &src, kw::Phase ph);

} // namespace beautify
