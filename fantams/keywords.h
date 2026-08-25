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

// Registres, paires et conditions du Z80. Contrairement aux mots ci-dessus, cet
// ensemble n'est PAS indexé par phase : un registre est un mot de la machine à
// toutes les phases. Il vivait dans `pp.cpp`, seul à le consulter, alors qu'il
// décrit le vocabulaire de la machine — exactement la duplication que cet
// en-tête a supprimée pour les labels (ADR 0015).
bool isMachineWord(const std::string &upperTok);

// Un identifiant utilisateur ne porte pas un nom du langage ni de la machine
// (ADR 0015). Rend le diagnostic à émettre, ou "" si le nom est libre.
// `position` complète la phrase : "a macro parameter", "a loop index", "a
// symbol", "a label". Une seule formulation pour les quatre positions.
std::string reservedName(const std::string &name, const std::string &position);

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

// --- Littéraux de chaîne (ADR 0010) ------------------------------------------
//
// « 'texte' » et « "texte" » désignent le MÊME objet : une chaîne. Les deux
// délimiteurs sont synonymes, et rien ne distingue 'A' de "A" — un littéral
// d'un octet, dont la valeur en expression est le code de ce caractère.
//
// Un littéral court jusqu'à la prochaine occurrence de SON PROPRE délimiteur :
// l'autre y est un caractère ordinaire, sans échappement (« db 'a"b' » émet
// trois octets). C'est ce que fait déjà rasm, et ce que faisaient déjà les
// scanners de pp.cpp et parser.cpp — l'écrire ici une fois supprime une
// divergence plutôt qu'elle n'en ajoute une.
struct Literal {
    bool present = false;   // la position lue commençait bien par un délimiteur
    std::string bytes;      // contenu, échappements résolus
    size_t end = 0;         // index juste après le délimiteur fermant
    std::string error;      // non vide si le littéral n'est pas terminé
};

// Lit un littéral à la position `pos`. `present` reste faux — sans erreur — si
// `s[pos]` n'est pas un délimiteur : c'est à l'appelant de décider si un
// littéral était attendu là.
Literal readLiteral(const std::string &s, size_t pos);

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
