// asm.h - Assembleur 2 passes (fantams)
//
// Entrée : lignes de source DÉJÀ préprocessées (plates : ni macros ni includes).
// Passe 1 : calcule les adresses (ORG) et collecte tous les symboles.
// Passe 2 : encode réellement, avec les références avant résolues.
//
// La taille d'une instruction Z80 dépend du TYPE des opérandes (pas de leur
// valeur), donc la passe 1 obtient des adresses correctes sans connaître encore
// les valeurs des symboles.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace asmb {

struct SourceLine {
    std::string text;
    std::string file;
    int line = 0;
    bool col0 = false;   // true si, dans la source d'origine, la ligne commençait en colonne 1 (sans indentation)
};

struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
};

struct Output {
    bool ok = true;
    std::vector<uint8_t> bin;                 // octets [loadAddress .. loadAddress+size)
    uint16_t loadAddress = 0;                  // 1re adresse écrite
    uint16_t runAddress = 0;                    // point d'entrée (directive RUN, sinon = loadAddress)
    std::map<std::string, int64_t> symbols;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;          // bonnes pratiques (non bloquant) : label sans ':', instruction en colonne 1...
    std::vector<uint8_t> image;                // image mémoire 64K complète (pour SNA)
};

Output assemble(const std::vector<SourceLine> &lines);
Output assembleText(const std::string &source, const std::string &file);

} // namespace asmb
