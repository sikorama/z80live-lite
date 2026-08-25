// keywords.h - Vocabulaire réservé et épluchage de label, indexés par phase
//
// « Ce premier mot est-il un label ? » est LA question dont dépendent le
// parseur, l'assembleur, le préprocesseur et le beautify. Elle vivait en trois
// copies (parser.cpp, asm.cpp, pp.cpp) que leurs commentaires respectifs
// disaient déjà être « la même liste » ; elle vit ici, une fois, avec la phase
// pour paramètre (ADR 0013).
//
// Les trois crans forment une inclusion STRICTE :
//
//   Instruction ⊂ Assembly ⊂ Preprocess
//
// Ce n'est pas une gradation de rigueur, c'est une question de phase. `LET`
// n'est pas un label au temps préprocesseur, et n'existe plus du tout au temps
// d'assemblage : les deux faits sont vrais, et une liste unique ne saurait dire
// ni l'un ni l'autre.
#pragma once

#include <functional>
#include <string>

namespace kw {

enum class Phase {
    // Ce qu'un parseur d'instruction doit désambiguïser : mnémoniques Z80 et
    // directives de placement/données.
    Instruction,
    // + les directives reconnues OU explicitement refusées. Dans les deux cas
    // ce ne sont pas des labels : sans ça « BANK 4 » est lu comme un label
    // « BANK » suivi d'une directive « 4 », et le diagnostic parle d'un token
    // que personne n'a écrit.
    Assembly,
    // + les mots-clés du préprocesseur lui-même. Sans eux « LET N = 3 » ou
    // « REPEAT 3,i » sont lus comme un label collé suivi du reste.
    Preprocess,
};

// `upperTok` doit déjà être en MAJUSCULES : tous les appelants l'ont sous la
// main au moment d'appeler, et le convertir ici le referait deux fois.
bool isReservedWord(const std::string &upperTok, Phase ph);

// Sépare un éventuel label de tête « ident: » du reste — ou « ident » seul
// (sans ':') si `ident` n'est pas réservé à cette phase, forme tolérée et
// courante chez rasm. `code` doit être dépourvu de commentaire et d'espaces de
// tête ; `rest` est rendu trimé.
//
// `sawColon` (optionnel) : lequel des deux cas s'est produit, pour permettre
// l'avertissement de bonne pratique en amont.
//
// `isMacro` (optionnel) : un nom de macro DÉFINI PAR L'UTILISATEUR est inconnu
// de la liste statique et doit être exclu lui aussi — sinon un appel « SETA 42 »
// sans ':' est lu comme un label « SETA ».
void peelLabel(const std::string &code, std::string &label, std::string &rest, Phase ph,
               bool *sawColon = nullptr,
               const std::function<bool(const std::string &)> &isMacro = nullptr);

// Index du début du commentaire de ligne (';' ou '//'), hors chaîne et
// caractère, ou npos. Partagé pour que le beautify et l'assembleur s'accordent
// sur ce qui est du code : deux réponses différentes ici, et le beautify
// réécrirait un commentaire comme s'il était une instruction.
size_t commentPos(const std::string &s);

inline std::string stripComment(const std::string &s) {
    size_t p = commentPos(s);
    return p == std::string::npos ? s : s.substr(0, p);
}

bool isIdentChar(char c);

} // namespace kw
