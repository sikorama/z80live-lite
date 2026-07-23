// z80.h - Encodeur Z80 data-driven pour rasm-lite
//
// Ce module est autonome : il ne dépend d'AUCUN autre morceau de rasm.
// Il encode une instruction Z80 (mnémonique + opérandes) en octets, via une
// interface abstraite IAsmContext que l'hôte fournit (émission, évaluation
// d'expression, PC courant, erreurs). Les expressions ne sont PAS évaluées
// ici : c'est le rôle du contexte (ce qui autorise la résolution différée /
// forward references côté assembleur).
//
// Portage WASM/JS : aucune allocation manuelle, aucune I/O, aucun état global.
#pragma once

#include <cstdint>
#include <string>

namespace z80 {

// --- Registres --------------------------------------------------------------
enum class Reg {
    None,
    // 8 bits documentés
    A, B, C, D, E, H, L,
    // 8 bits non documentés (moitiés de IX/IY)
    IXH, IXL, IYH, IYL,
    // registres spéciaux
    I, R,
    // 16 bits
    AF, BC, DE, HL, SP, IX, IY,
    AFp, // AF'
};

// --- Codes conditions -------------------------------------------------------
enum class Cond { None, NZ, Z, NC, C, PO, PE, P, M };

// --- Mnémoniques ------------------------------------------------------------
enum class Mnemo {
    Invalid,
    // transfert
    LD, PUSH, POP, EX, EXX, LDI, LDIR, LDD, LDDR,
    // arithmétique / logique 8/16 bits
    ADD, ADC, SUB, SBC, AND, XOR, OR, CP, INC, DEC,
    DAA, CPL, NEG, CCF, SCF,
    CPI, CPIR, CPD, CPDR,
    // rotations / décalages
    RLCA, RRCA, RLA, RRA, RLC, RRC, RL, RR, SLA, SRA, SLL, SRL, RLD, RRD,
    // bits
    BIT, RES, SET,
    // saut / appel / retour
    JP, JR, DJNZ, CALL, RET, RETI, RETN, RST,
    // CPU / interruptions
    NOP, HALT, DI, EI, IM,
    // E/S
    IN, OUT, INI, INIR, IND, INDR, OUTI, OTIR, OUTD, OTDR,
};

// --- Opérande ---------------------------------------------------------------
struct Operand {
    enum class Kind {
        None,
        Reg,     // un registre (champ reg)
        RegInd,  // (BC) (DE) (HL) (SP) (C)  -> reg = BC/DE/HL/SP/C
        Indexed, // (IX+d) / (IY+d)          -> reg = IX/IY, expr = déplacement
        Imm,     // valeur immédiate n / nn / numéro de bit / vecteur RST -> expr
        MemImm,  // (nn) adressage absolu mémoire                          -> expr
        Cond,    // code condition                                          -> cc
    };
    Kind kind = Kind::None;
    Reg reg = Reg::None;
    Cond cc = Cond::None;
    std::string expr; // pour Imm / MemImm / déplacement Indexed

    // fabriques pratiques (le futur parseur les utilisera)
    static Operand none() { return {}; }
    static Operand r(Reg rr) { Operand o; o.kind = Kind::Reg; o.reg = rr; return o; }
    static Operand rind(Reg rr) { Operand o; o.kind = Kind::RegInd; o.reg = rr; return o; }
    static Operand idx(Reg xy, std::string disp) { Operand o; o.kind = Kind::Indexed; o.reg = xy; o.expr = std::move(disp); return o; }
    static Operand imm(std::string e) { Operand o; o.kind = Kind::Imm; o.expr = std::move(e); return o; }
    static Operand mem(std::string e) { Operand o; o.kind = Kind::MemImm; o.expr = std::move(e); return o; }
    static Operand condition(Cond c) { Operand o; o.kind = Kind::Cond; o.cc = c; return o; }
};

struct Instruction {
    Mnemo mnemo = Mnemo::Invalid;
    Operand a; // 1er opérande (ou None)
    Operand b; // 2e opérande (ou None)
};

// --- Interface de contexte (frontière avec le reste de l'assembleur) --------
// L'encodeur n'appelle QUE ces méthodes. Un contexte factice suffit à le tester.
struct IAsmContext {
    virtual ~IAsmContext() = default;
    // Émet un octet dans le flux de sortie et avance le PC de sortie.
    virtual void emit(uint8_t b) = 0;
    // Évalue une expression et renvoie sa valeur. Peut être différée côté hôte ;
    // ici on suppose une valeur disponible (l'hôte gère la 2e passe).
    virtual int64_t eval(const std::string &expr) = 0;
    // Adresse courante (PC logique) AVANT émission de l'instruction courante.
    // Nécessaire pour l'adressage relatif (JR / DJNZ).
    virtual uint16_t pc() const = 0;
    // Signale une erreur d'encodage (combinaison mnémonique/opérandes invalide,
    // déplacement hors bornes, etc.).
    virtual void error(const std::string &msg) = 0;
};

// Encode une instruction. Renvoie true si encodée, false si combinaison
// invalide (dans ce cas ctx.error() a été appelé et rien n'est émis).
bool encode(IAsmContext &ctx, const Instruction &in);

// Utilitaires exposés (pratiques pour le futur parseur et les tests).
Mnemo mnemoFromString(const std::string &s); // "LD" -> Mnemo::LD, sinon Invalid
const char *mnemoName(Mnemo m);

} // namespace z80
