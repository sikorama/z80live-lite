// z80.cpp - Encodeur Z80 data-driven pour fantams (voir z80.h)
//
// Principe : les opérandes sont d'abord *normalisés* (IX/IY -> préfixe DD/FD +
// slot HL ; (IX+d) -> préfixe + (HL) + déplacement). Après normalisation, les
// opcodes proviennent de FORMULES régulières et de petites TABLES (codes
// registres/conditions/ALU), pas d'une fonction géante par mnémonique.
#include "z80.h"

#include <array>
#include <string>

namespace z80 {
namespace {

// ---------------------------------------------------------------------------
// Tables de codes (source de vérité "data-driven")
// ---------------------------------------------------------------------------

// Code registre 8 bits : B=0 C=1 D=2 E=3 H=4 L=5 (HL)=6 A=7
int reg8code(Reg r) {
    switch (r) {
        case Reg::B: return 0; case Reg::C: return 1;
        case Reg::D: return 2; case Reg::E: return 3;
        case Reg::H: return 4; case Reg::L: return 5;
        case Reg::A: return 7;
        // moitiés non documentées : mêmes codes que H/L, sélectionnées par préfixe
        case Reg::IXH: return 4; case Reg::IXL: return 5;
        case Reg::IYH: return 4; case Reg::IYL: return 5;
        default: return -1;
    }
}
uint8_t reg8prefix(Reg r) {
    switch (r) {
        case Reg::IXH: case Reg::IXL: return 0xDD;
        case Reg::IYH: case Reg::IYL: return 0xFD;
        default: return 0;
    }
}

// Code registre 16 bits (BC=0 DE=1 HL=2 SP=3), IX/IY -> HL + préfixe
int reg16code(Reg r, uint8_t &prefix) {
    switch (r) {
        case Reg::BC: return 0; case Reg::DE: return 1;
        case Reg::HL: return 2; case Reg::SP: return 3;
        case Reg::IX: prefix = 0xDD; return 2;
        case Reg::IY: prefix = 0xFD; return 2;
        default: return -1;
    }
}
// Variante PUSH/POP : AF=3 au lieu de SP
int reg16codeAF(Reg r, uint8_t &prefix) {
    switch (r) {
        case Reg::BC: return 0; case Reg::DE: return 1;
        case Reg::HL: return 2; case Reg::AF: return 3;
        case Reg::IX: prefix = 0xDD; return 2;
        case Reg::IY: prefix = 0xFD; return 2;
        default: return -1;
    }
}

int condcode(Cond c) {
    switch (c) {
        case Cond::NZ: return 0; case Cond::Z: return 1;
        case Cond::NC: return 2; case Cond::C: return 3;
        case Cond::PO: return 4; case Cond::PE: return 5;
        case Cond::P: return 6;  case Cond::M: return 7;
        default: return -1;
    }
}

bool is16bit(Reg r) {
    switch (r) {
        case Reg::BC: case Reg::DE: case Reg::HL: case Reg::SP:
        case Reg::IX: case Reg::IY: case Reg::AF: return true;
        default: return false;
    }
}

// Opérande 8 bits normalisé : r8 documenté, (HL), ou (IX+d)/(IY+d)
struct R8 {
    bool ok = false;
    int code = 0;
    uint8_t prefix = 0;
    bool indexed = false;   // était (IX+d)/(IY+d)
    std::string disp;       // déplacement si indexed
};
R8 asR8(const Operand &o) {
    R8 r;
    if (o.kind == Operand::Kind::Reg) {
        int c = reg8code(o.reg);
        if (c >= 0) { r.ok = true; r.code = c; r.prefix = reg8prefix(o.reg); }
    } else if (o.kind == Operand::Kind::RegInd && o.reg == Reg::HL) {
        r.ok = true; r.code = 6;
    } else if (o.kind == Operand::Kind::RegInd &&
               (o.reg == Reg::IX || o.reg == Reg::IY)) {
        // (IX)/(IY) sans déplacement == indexé disp 0 (ex. LD A,(IX) = DD 7E 00)
        r.ok = true; r.code = 6; r.indexed = true;
        r.prefix = (o.reg == Reg::IX) ? 0xDD : 0xFD;
        r.disp = "0";
    } else if (o.kind == Operand::Kind::Indexed &&
               (o.reg == Reg::IX || o.reg == Reg::IY)) {
        r.ok = true; r.code = 6; r.indexed = true;
        r.prefix = (o.reg == Reg::IX) ? 0xDD : 0xFD;
        r.disp = o.expr;
    }
    return r;
}

// Fusion de préfixes DD/FD ; renvoie false si conflit (DD + FD)
bool mergePrefix(uint8_t a, uint8_t b, uint8_t &out) {
    if (a == 0) { out = b; return true; }
    if (b == 0) { out = a; return true; }
    if (a == b) { out = a; return true; }
    return false;
}

// ---------------------------------------------------------------------------
// Petit encodeur : accumule dans le contexte
// ---------------------------------------------------------------------------
struct Emitter {
    IAsmContext &ctx;
    void op(uint8_t b) { ctx.emit(b); }
    void prefix(uint8_t p) { if (p) ctx.emit(p); }
    void imm8(int64_t v) { ctx.emit((uint8_t)(v & 0xFF)); }
    void imm16(int64_t v) { ctx.emit((uint8_t)(v & 0xFF)); ctx.emit((uint8_t)((v >> 8) & 0xFF)); }
    void disp(int64_t v) { ctx.emit((uint8_t)(v & 0xFF)); }
};

// ---------------------------------------------------------------------------
// Instructions implicites (sans opérande) : opcode fixe (1 ou 2 octets)
// ---------------------------------------------------------------------------
struct Implied { Mnemo m; uint8_t p; uint8_t op; }; // p = préfixe ED sinon 0
const Implied kImplied[] = {
    {Mnemo::NOP,0,0x00}, {Mnemo::HALT,0,0x76}, {Mnemo::DI,0,0xF3}, {Mnemo::EI,0,0xFB},
    {Mnemo::EXX,0,0xD9},
    {Mnemo::RLCA,0,0x07}, {Mnemo::RRCA,0,0x0F}, {Mnemo::RLA,0,0x17}, {Mnemo::RRA,0,0x1F},
    {Mnemo::DAA,0,0x27}, {Mnemo::CPL,0,0x2F}, {Mnemo::SCF,0,0x37}, {Mnemo::CCF,0,0x3F},
    {Mnemo::LDI,0xED,0xA0}, {Mnemo::LDIR,0xED,0xB0}, {Mnemo::LDD,0xED,0xA8}, {Mnemo::LDDR,0xED,0xB8},
    {Mnemo::CPI,0xED,0xA1}, {Mnemo::CPIR,0xED,0xB1}, {Mnemo::CPD,0xED,0xA9}, {Mnemo::CPDR,0xED,0xB9},
    {Mnemo::INI,0xED,0xA2}, {Mnemo::INIR,0xED,0xB2}, {Mnemo::IND,0xED,0xAA}, {Mnemo::INDR,0xED,0xBA},
    {Mnemo::OUTI,0xED,0xA3}, {Mnemo::OTIR,0xED,0xB3}, {Mnemo::OUTD,0xED,0xAB}, {Mnemo::OTDR,0xED,0xBB},
    {Mnemo::NEG,0xED,0x44}, {Mnemo::RETI,0xED,0x4D}, {Mnemo::RETN,0xED,0x45},
    {Mnemo::RLD,0xED,0x6F}, {Mnemo::RRD,0xED,0x67},
};

// Index ALU pour ADD/ADC/SUB/SBC/AND/XOR/OR/CP
int aluIndex(Mnemo m) {
    switch (m) {
        case Mnemo::ADD: return 0; case Mnemo::ADC: return 1;
        case Mnemo::SUB: return 2; case Mnemo::SBC: return 3;
        case Mnemo::AND: return 4; case Mnemo::XOR: return 5;
        case Mnemo::OR:  return 6; case Mnemo::CP:  return 7;
        default: return -1;
    }
}
// Index rotation/décalage pour le préfixe CB
int rotIndex(Mnemo m) {
    switch (m) {
        case Mnemo::RLC: return 0; case Mnemo::RRC: return 1;
        case Mnemo::RL:  return 2; case Mnemo::RR:  return 3;
        case Mnemo::SLA: return 4; case Mnemo::SRA: return 5;
        case Mnemo::SLL: return 6; case Mnemo::SRL: return 7;
        default: return -1;
    }
}

// ---------------------------------------------------------------------------
// Encodages de familles
// ---------------------------------------------------------------------------

// ALU 8 bits : "<op> A,src" ou "<op> src" (A implicite). src = r8/(HL)/(IX+d)/n
bool encodeAlu(IAsmContext &ctx, int idx, const Operand &src) {
    Emitter e{ctx};
    if (src.kind == Operand::Kind::Imm) {
        e.op((uint8_t)(0xC6 + idx * 8));
        e.imm8(ctx.eval(src.expr));
        return true;
    }
    R8 s = asR8(src);
    if (!s.ok) { ctx.error("opérande ALU 8 bits invalide"); return false; }
    e.prefix(s.prefix);
    e.op((uint8_t)(0x80 + idx * 8 + s.code));
    if (s.indexed) e.disp(ctx.eval(s.disp));
    return true;
}

// Rotation/décalage sur r8/(HL)/(IX+d) : préfixe CB (ordre spécial si indexé)
bool encodeRot(IAsmContext &ctx, int idx, const Operand &tgt) {
    Emitter e{ctx};
    R8 t = asR8(tgt);
    if (!t.ok) { ctx.error("cible de rotation/décalage invalide"); return false; }
    e.prefix(t.prefix);
    e.op(0xCB);
    if (t.indexed) e.disp(ctx.eval(t.disp)); // DD CB d op
    e.op((uint8_t)(idx * 8 + t.code));
    return true;
}

// BIT/RES/SET b,r : base 0x40/0x80/0xC0
bool encodeBit(IAsmContext &ctx, uint8_t base, const Operand &nb, const Operand &tgt) {
    Emitter e{ctx};
    if (nb.kind != Operand::Kind::Imm) { ctx.error("numéro de bit attendu"); return false; }
    int64_t b = ctx.eval(nb.expr);
    if (b < 0 || b > 7) ctx.error("numéro de bit hors [0..7]"); // émet quand même (taille stable)
    R8 t = asR8(tgt);
    if (!t.ok) { ctx.error("cible d'opération sur bit invalide"); return false; }
    e.prefix(t.prefix);
    e.op(0xCB);
    if (t.indexed) e.disp(ctx.eval(t.disp));
    e.op((uint8_t)(base + (b & 7) * 8 + t.code));
    return true;
}

// ---------------------------------------------------------------------------
// LD (la plus grosse famille)
// ---------------------------------------------------------------------------
bool encodeLD(IAsmContext &ctx, const Operand &A, const Operand &B) {
    Emitter e{ctx};
    R8 da = asR8(A), sb = asR8(B);

    // LD r,r'
    if (da.ok && sb.ok) {
        if (da.code == 6 && sb.code == 6) { ctx.error("LD (HL),(HL) invalide (= HALT)"); return false; }
        if (da.indexed && sb.indexed) { ctx.error("deux opérandes indexés impossibles"); return false; }
        // si un opérande est (IX+d), l'autre doit être un r8 SANS préfixe (A/B/C/D/E/H/L réels)
        if ((da.indexed && sb.prefix) || (sb.indexed && da.prefix)) {
            ctx.error("(IX+d) incompatible avec IXH/IXL/IYH/IYL"); return false;
        }
        uint8_t pfx;
        if (!mergePrefix(da.prefix, sb.prefix, pfx)) { ctx.error("mélange de préfixes DD/FD"); return false; }
        e.prefix(pfx);
        e.op((uint8_t)(0x40 + da.code * 8 + sb.code));
        if (da.indexed) e.disp(ctx.eval(da.disp));
        else if (sb.indexed) e.disp(ctx.eval(sb.disp));
        return true;
    }
    // LD r,n
    if (da.ok && B.kind == Operand::Kind::Imm) {
        e.prefix(da.prefix);
        e.op((uint8_t)(0x06 + da.code * 8));
        if (da.indexed) e.disp(ctx.eval(da.disp));
        e.imm8(ctx.eval(B.expr));
        return true;
    }
    // Formes chargement accumulateur / mémoire directe
    auto isReg = [](const Operand &o, Reg r) {
        return o.kind == Operand::Kind::Reg && o.reg == r;
    };
    auto isRegInd = [](const Operand &o, Reg r) {
        return o.kind == Operand::Kind::RegInd && o.reg == r;
    };
    // LD A,(BC)/(DE)/(nn) et réciproques
    if (isReg(A, Reg::A) && isRegInd(B, Reg::BC)) { e.op(0x0A); return true; }
    if (isReg(A, Reg::A) && isRegInd(B, Reg::DE)) { e.op(0x1A); return true; }
    if (isReg(A, Reg::A) && B.kind == Operand::Kind::MemImm) { e.op(0x3A); e.imm16(ctx.eval(B.expr)); return true; }
    if (isRegInd(A, Reg::BC) && isReg(B, Reg::A)) { e.op(0x02); return true; }
    if (isRegInd(A, Reg::DE) && isReg(B, Reg::A)) { e.op(0x12); return true; }
    if (A.kind == Operand::Kind::MemImm && isReg(B, Reg::A)) { e.op(0x32); e.imm16(ctx.eval(A.expr)); return true; }
    // LD A,I / A,R / I,A / R,A
    if (isReg(A, Reg::A) && isReg(B, Reg::I)) { e.op(0xED); e.op(0x57); return true; }
    if (isReg(A, Reg::A) && isReg(B, Reg::R)) { e.op(0xED); e.op(0x5F); return true; }
    if (isReg(A, Reg::I) && isReg(B, Reg::A)) { e.op(0xED); e.op(0x47); return true; }
    if (isReg(A, Reg::R) && isReg(B, Reg::A)) { e.op(0xED); e.op(0x4F); return true; }
    // LD SP,HL/IX/IY
    if (isReg(A, Reg::SP) && (isReg(B, Reg::HL) || isReg(B, Reg::IX) || isReg(B, Reg::IY))) {
        uint8_t p = 0; reg16code(B.reg, p); e.prefix(p); e.op(0xF9); return true;
    }
    // LD rr,nn  et  LD rr,(nn) / LD (nn),rr
    if (A.kind == Operand::Kind::Reg && is16bit(A.reg) && A.reg != Reg::AF) {
        uint8_t p = 0; int rr = reg16code(A.reg, p);
        if (rr < 0) { ctx.error("registre 16 bits invalide"); return false; }
        if (B.kind == Operand::Kind::Imm) { // LD rr,nn
            e.prefix(p); e.op((uint8_t)(0x01 + rr * 16)); e.imm16(ctx.eval(B.expr)); return true;
        }
        if (B.kind == Operand::Kind::MemImm) { // LD rr,(nn)
            if (rr == 2) { e.prefix(p); e.op(0x2A); }       // HL/IX/IY
            else { e.op(0xED); e.op((uint8_t)(0x4B + rr * 16)); } // BC/DE/SP
            e.imm16(ctx.eval(B.expr)); return true;
        }
    }
    if (B.kind == Operand::Kind::Reg && is16bit(B.reg) && B.reg != Reg::AF &&
        A.kind == Operand::Kind::MemImm) { // LD (nn),rr
        uint8_t p = 0; int rr = reg16code(B.reg, p);
        if (rr == 2) { e.prefix(p); e.op(0x22); }           // HL/IX/IY
        else { e.op(0xED); e.op((uint8_t)(0x43 + rr * 16)); }
        e.imm16(ctx.eval(A.expr)); return true;
    }
    ctx.error("forme de LD non reconnue");
    return false;
}

// ---------------------------------------------------------------------------
// INC / DEC (8 ou 16 bits)
// ---------------------------------------------------------------------------
bool encodeIncDec(IAsmContext &ctx, Mnemo m, const Operand &A) {
    Emitter e{ctx};
    if (A.kind == Operand::Kind::Reg && is16bit(A.reg) && A.reg != Reg::AF) {
        uint8_t p = 0; int rr = reg16code(A.reg, p);
        e.prefix(p);
        e.op((uint8_t)((m == Mnemo::INC ? 0x03 : 0x0B) + rr * 16));
        return true;
    }
    R8 t = asR8(A);
    if (!t.ok) { ctx.error("opérande INC/DEC invalide"); return false; }
    e.prefix(t.prefix);
    e.op((uint8_t)((m == Mnemo::INC ? 0x04 : 0x05) + t.code * 8));
    if (t.indexed) e.disp(ctx.eval(t.disp));
    return true;
}

// ADD/ADC/SBC 16 bits
bool encode16Add(IAsmContext &ctx, Mnemo m, const Operand &A, const Operand &B) {
    Emitter e{ctx};
    if (B.kind != Operand::Kind::Reg) { ctx.error("ADD/ADC/SBC 16 bits : 2e opérande invalide"); return false; }
    uint8_t aprefix = 0;
    if (A.reg == Reg::IX) aprefix = 0xDD; else if (A.reg == Reg::IY) aprefix = 0xFD;
    // code du registre source (slot rr)
    int rr;
    switch (B.reg) {
        case Reg::BC: rr = 0; break;
        case Reg::DE: rr = 1; break;
        case Reg::SP: rr = 3; break;
        case Reg::HL: if (A.reg != Reg::HL) { ctx.error("appariement 16 bits invalide"); return false; } rr = 2; break;
        case Reg::IX: if (A.reg != Reg::IX) { ctx.error("appariement 16 bits invalide"); return false; } rr = 2; break;
        case Reg::IY: if (A.reg != Reg::IY) { ctx.error("appariement 16 bits invalide"); return false; } rr = 2; break;
        default: ctx.error("registre 16 bits invalide"); return false;
    }
    if (m == Mnemo::ADD) {
        e.prefix(aprefix); e.op((uint8_t)(0x09 + rr * 16)); return true;
    }
    // ADC/SBC HL,rr : seulement HL, préfixe ED
    if (A.reg != Reg::HL) { ctx.error("ADC/SBC 16 bits : uniquement HL"); return false; }
    e.op(0xED);
    e.op((uint8_t)((m == Mnemo::ADC ? 0x4A : 0x42) + rr * 16));
    return true;
}

} // namespace

// ---------------------------------------------------------------------------
// Point d'entrée : encode()
// ---------------------------------------------------------------------------
bool encode(IAsmContext &ctx, const Instruction &in) {
    Emitter e{ctx};
    const Operand &A = in.a;
    const Operand &B = in.b;
    const uint16_t pc0 = ctx.pc();

    // 1) implicites
    if (A.kind == Operand::Kind::None && B.kind == Operand::Kind::None) {
        for (const auto &imp : kImplied) {
            if (imp.m == in.mnemo) { e.prefix(imp.p); e.op(imp.op); return true; }
        }
    }

    switch (in.mnemo) {
        case Mnemo::LD:
            return encodeLD(ctx, A, B);

        case Mnemo::PUSH: case Mnemo::POP: {
            uint8_t p = 0; int rr = reg16codeAF(A.reg, p);
            if (A.kind != Operand::Kind::Reg || rr < 0) { ctx.error("PUSH/POP : registre invalide"); return false; }
            e.prefix(p);
            e.op((uint8_t)((in.mnemo == Mnemo::PUSH ? 0xC5 : 0xC1) + rr * 16));
            return true;
        }

        case Mnemo::ADD: case Mnemo::ADC: case Mnemo::SBC: {
            // 16 bits si 1er opérande est HL/IX/IY
            if (A.kind == Operand::Kind::Reg &&
                (A.reg == Reg::HL || A.reg == Reg::IX || A.reg == Reg::IY)) {
                return encode16Add(ctx, in.mnemo, A, B);
            }
            // sinon 8 bits : "ADD A,src" ou (rare) "ADD src"
            const Operand &src = (A.kind == Operand::Kind::Reg && A.reg == Reg::A &&
                                  B.kind != Operand::Kind::None) ? B : A;
            return encodeAlu(ctx, aluIndex(in.mnemo), src);
        }
        case Mnemo::SUB: case Mnemo::AND: case Mnemo::XOR:
        case Mnemo::OR:  case Mnemo::CP: {
            const Operand &src = (A.kind == Operand::Kind::Reg && A.reg == Reg::A &&
                                  B.kind != Operand::Kind::None) ? B : A;
            return encodeAlu(ctx, aluIndex(in.mnemo), src);
        }

        case Mnemo::INC: case Mnemo::DEC:
            return encodeIncDec(ctx, in.mnemo, A);

        case Mnemo::RLC: case Mnemo::RRC: case Mnemo::RL: case Mnemo::RR:
        case Mnemo::SLA: case Mnemo::SRA: case Mnemo::SLL: case Mnemo::SRL:
            return encodeRot(ctx, rotIndex(in.mnemo), A);

        case Mnemo::BIT: return encodeBit(ctx, 0x40, A, B);
        case Mnemo::RES: return encodeBit(ctx, 0x80, A, B);
        case Mnemo::SET: return encodeBit(ctx, 0xC0, A, B);

        case Mnemo::JP: {
            if (A.kind == Operand::Kind::RegInd &&
                (A.reg == Reg::HL || A.reg == Reg::IX || A.reg == Reg::IY)) {
                uint8_t p = 0; reg16code(A.reg, p); e.prefix(p); e.op(0xE9); return true;
            }
            if (A.kind == Operand::Kind::Cond) {
                int cc = condcode(A.cc);
                e.op((uint8_t)(0xC2 + cc * 8)); e.imm16(ctx.eval(B.expr)); return true;
            }
            if (A.kind == Operand::Kind::Imm) { e.op(0xC3); e.imm16(ctx.eval(A.expr)); return true; }
            ctx.error("forme de JP non reconnue"); return false;
        }
        case Mnemo::CALL: {
            if (A.kind == Operand::Kind::Cond) {
                int cc = condcode(A.cc);
                e.op((uint8_t)(0xC4 + cc * 8)); e.imm16(ctx.eval(B.expr)); return true;
            }
            if (A.kind == Operand::Kind::Imm) { e.op(0xCD); e.imm16(ctx.eval(A.expr)); return true; }
            ctx.error("forme de CALL non reconnue"); return false;
        }
        case Mnemo::RET: {
            if (A.kind == Operand::Kind::None) { e.op(0xC9); return true; }
            if (A.kind == Operand::Kind::Cond) { e.op((uint8_t)(0xC0 + condcode(A.cc) * 8)); return true; }
            ctx.error("forme de RET non reconnue"); return false;
        }
        case Mnemo::JR: case Mnemo::DJNZ: {
            const Operand *tgt; int base;
            int cc = -1;
            if (in.mnemo == Mnemo::DJNZ) { tgt = &A; base = 0x10; }
            else if (A.kind == Operand::Kind::Cond) {
                cc = condcode(A.cc);
                if (cc > 3) { ctx.error("JR : condition non relative (NZ/Z/NC/C uniquement)"); return false; }
                tgt = &B; base = 0x20 + cc * 8;
            } else { tgt = &A; base = 0x18; }
            if (tgt->kind != Operand::Kind::Imm) { ctx.error("cible relative attendue"); return false; }
            int64_t target = ctx.eval(tgt->expr);
            int64_t disp = target - (int64_t)(pc0 + 2);
            // erreur valeur-dépendante : on émet quand même 2 octets (taille stable en 2 passes)
            if (disp < -128 || disp > 127) ctx.error("saut relatif hors de portée (-128..127)");
            e.op((uint8_t)base); e.disp(disp); return true;
        }

        case Mnemo::RST: {
            if (A.kind != Operand::Kind::Imm) { ctx.error("RST : vecteur attendu"); return false; }
            int64_t n = ctx.eval(A.expr);
            if (n < 0 || n > 0x38 || (n & 7)) ctx.error("RST : vecteur invalide (00,08,...,38)");
            e.op((uint8_t)(0xC7 + (n & 0x38))); return true;
        }
        case Mnemo::IM: {
            if (A.kind != Operand::Kind::Imm) { ctx.error("IM : mode attendu"); return false; }
            int64_t n = ctx.eval(A.expr);
            uint8_t op = 0x46;
            if (n == 1) op = 0x56; else if (n == 2) op = 0x5E;
            else if (n != 0) ctx.error("IM : mode invalide (0/1/2)");
            e.op(0xED); e.op(op); return true;
        }

        case Mnemo::EX: {
            if (A.kind == Operand::Kind::Reg && A.reg == Reg::AF &&
                B.kind == Operand::Kind::Reg && B.reg == Reg::AFp) { e.op(0x08); return true; }
            if (A.kind == Operand::Kind::Reg && A.reg == Reg::DE &&
                B.kind == Operand::Kind::Reg && B.reg == Reg::HL) { e.op(0xEB); return true; }
            if (A.kind == Operand::Kind::RegInd && A.reg == Reg::SP &&
                B.kind == Operand::Kind::Reg &&
                (B.reg == Reg::HL || B.reg == Reg::IX || B.reg == Reg::IY)) {
                uint8_t p = 0; reg16code(B.reg, p); e.prefix(p); e.op(0xE3); return true;
            }
            ctx.error("forme de EX non reconnue"); return false;
        }

        case Mnemo::IN: {
            if (A.kind == Operand::Kind::Reg && A.reg == Reg::A && B.kind == Operand::Kind::MemImm) {
                e.op(0xDB); e.imm8(ctx.eval(B.expr)); return true; // IN A,(n)
            }
            if (A.kind == Operand::Kind::Reg && B.kind == Operand::Kind::RegInd && B.reg == Reg::C) {
                int c = reg8code(A.reg);
                if (c < 0) { ctx.error("IN r,(C) : registre invalide"); return false; }
                e.op(0xED); e.op((uint8_t)(0x40 + c * 8)); return true; // IN r,(C)
            }
            ctx.error("forme de IN non reconnue"); return false;
        }
        case Mnemo::OUT: {
            if (A.kind == Operand::Kind::MemImm && B.kind == Operand::Kind::Reg && B.reg == Reg::A) {
                e.op(0xD3); e.imm8(ctx.eval(A.expr)); return true; // OUT (n),A
            }
            if (A.kind == Operand::Kind::RegInd && A.reg == Reg::C && B.kind == Operand::Kind::Reg) {
                int c = reg8code(B.reg);
                if (c < 0) { ctx.error("OUT (C),r : registre invalide"); return false; }
                e.op(0xED); e.op((uint8_t)(0x41 + c * 8)); return true; // OUT (C),r
            }
            ctx.error("forme de OUT non reconnue"); return false;
        }

        default:
            ctx.error("mnémonique non géré");
            return false;
    }
}

// ---------------------------------------------------------------------------
// Utilitaires nom <-> mnémonique
// ---------------------------------------------------------------------------
namespace {
struct NameMap { const char *name; Mnemo m; };
const NameMap kNames[] = {
    {"LD",Mnemo::LD},{"PUSH",Mnemo::PUSH},{"POP",Mnemo::POP},{"EX",Mnemo::EX},{"EXX",Mnemo::EXX},
    {"LDI",Mnemo::LDI},{"LDIR",Mnemo::LDIR},{"LDD",Mnemo::LDD},{"LDDR",Mnemo::LDDR},
    {"ADD",Mnemo::ADD},{"ADC",Mnemo::ADC},{"SUB",Mnemo::SUB},{"SBC",Mnemo::SBC},
    {"AND",Mnemo::AND},{"XOR",Mnemo::XOR},{"OR",Mnemo::OR},{"CP",Mnemo::CP},
    {"INC",Mnemo::INC},{"DEC",Mnemo::DEC},{"DAA",Mnemo::DAA},{"CPL",Mnemo::CPL},
    {"NEG",Mnemo::NEG},{"CCF",Mnemo::CCF},{"SCF",Mnemo::SCF},
    {"CPI",Mnemo::CPI},{"CPIR",Mnemo::CPIR},{"CPD",Mnemo::CPD},{"CPDR",Mnemo::CPDR},
    {"RLCA",Mnemo::RLCA},{"RRCA",Mnemo::RRCA},{"RLA",Mnemo::RLA},{"RRA",Mnemo::RRA},
    {"RLC",Mnemo::RLC},{"RRC",Mnemo::RRC},{"RL",Mnemo::RL},{"RR",Mnemo::RR},
    {"SLA",Mnemo::SLA},{"SRA",Mnemo::SRA},{"SLL",Mnemo::SLL},{"SRL",Mnemo::SRL},
    {"RLD",Mnemo::RLD},{"RRD",Mnemo::RRD},
    {"BIT",Mnemo::BIT},{"RES",Mnemo::RES},{"SET",Mnemo::SET},
    {"JP",Mnemo::JP},{"JR",Mnemo::JR},{"DJNZ",Mnemo::DJNZ},{"CALL",Mnemo::CALL},
    {"RET",Mnemo::RET},{"RETI",Mnemo::RETI},{"RETN",Mnemo::RETN},{"RST",Mnemo::RST},
    {"NOP",Mnemo::NOP},{"HALT",Mnemo::HALT},{"DI",Mnemo::DI},{"EI",Mnemo::EI},{"IM",Mnemo::IM},
    {"IN",Mnemo::IN},{"OUT",Mnemo::OUT},{"INI",Mnemo::INI},{"INIR",Mnemo::INIR},
    {"IND",Mnemo::IND},{"INDR",Mnemo::INDR},{"OUTI",Mnemo::OUTI},{"OTIR",Mnemo::OTIR},
    {"OUTD",Mnemo::OUTD},{"OTDR",Mnemo::OTDR},
};
} // namespace

Mnemo mnemoFromString(const std::string &s) {
    for (const auto &n : kNames) if (s == n.name) return n.m;
    return Mnemo::Invalid;
}
const char *mnemoName(Mnemo m) {
    for (const auto &n : kNames) if (n.m == m) return n.name;
    return "?";
}

} // namespace z80
