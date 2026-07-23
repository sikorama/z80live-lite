// parser.h - Parseur d'instructions Z80 (texte -> z80::Instruction)
//
// Analyse une ligne « [label:] mnémo op1,op2 » en une z80::Instruction prête
// pour l'encodeur. Il NE résout PAS les symboles : les opérandes portent le
// texte brut de l'expression (évalué plus tard par IAsmContext).
//
// Le parseur travaille au niveau INSTRUCTION (et non opérande isolé) car le
// mnémonique est nécessaire pour désambiguïser `C` (registre vs condition).
#pragma once

#include "z80.h"
#include <string>

namespace parser {

struct Result {
    bool ok = false;              // false uniquement en cas d'erreur de syntaxe
    std::string error;

    std::string label;            // label de tête (sans ':'), ou vide

    bool isInstruction = false;   // true si mnémonique Z80 reconnu -> `instr` valide
    std::string mnemonic;         // mnémonique/directive en MAJUSCULES
    std::string operandsText;     // opérandes bruts (utile pour les directives non-Z80)
    z80::Instruction instr;       // valide si isInstruction
};

// Analyse une ligne complète. Une ligne vide ou label-seul renvoie ok=true,
// isInstruction=false. Un mnémonique inconnu (directive : DB, ORG…) renvoie
// ok=true, isInstruction=false, avec mnemonic/operandsText remplis.
Result parseLine(const std::string &line);

// Bas niveau (exposé pour les tests). allowCondition=true autorise l'opérande
// à être un code condition (position condition de JP/JR/CALL/RET).
bool parseOperand(const std::string &text, bool allowCondition,
                  z80::Operand &out, std::string &err);

} // namespace parser
