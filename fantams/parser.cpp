// parser.cpp - Parseur d'instructions Z80 (voir parser.h)
#include "parser.h"

#include <cctype>
#include <string>
#include <vector>

namespace parser {
namespace {

using z80::Reg;
using z80::Cond;
using z80::Operand;

bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '@';
}
std::string upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string firstToken(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = a; while (b < s.size() && !std::isspace((unsigned char)s[b])) ++b;
    return s.substr(a, b - a);
}
std::string restAfterFirst(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = a; while (b < s.size() && !std::isspace((unsigned char)s[b])) ++b;
    return trim(s.substr(b));
}
void peelLabel(const std::string &code, std::string &label, std::string &rest) {
    size_t p = 0; while (p < code.size() && isIdentChar(code[p])) ++p;
    if (p > 0) {
        size_t q = p; while (q < code.size() && std::isspace((unsigned char)code[q])) ++q;
        if (q < code.size() && code[q] == ':') {
            label = code.substr(0, p);
            rest = trim(code.substr(q + 1));
            return;
        }
    }
    label.clear(); rest = code;
}
// Découpe en respectant () [] {} et les littéraux "..." / '...'.
std::vector<std::string> splitTopLevel(const std::string &s, char delim) {
    std::vector<std::string> out;
    if (trim(s).empty()) return out;
    int depth = 0; bool inStr = false; char q = 0; std::string cur;
    for (char c : s) {
        if (inStr) { cur += c; if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; cur += c; continue; }
        if (c == ')' || c == ']' || c == '}') { --depth; cur += c; continue; }
        if (c == delim && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(trim(cur));
    return out;
}
// Parenthèses englobant la totalité de l'opérande ?  "(a)b" -> non, "(a)" -> oui
bool fullyEnclosed(const std::string &t) {
    if (t.size() < 2 || t.front() != '(') return false;
    int depth = 0; bool inStr = false; char q = 0;
    for (size_t i = 0; i < t.size(); ++i) {
        char c = t[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; continue; }
        if (c == '(') ++depth;
        else if (c == ')') { if (--depth == 0) return i == t.size() - 1; }
    }
    return false;
}

Reg regFromUpper(const std::string &u) {
    if (u == "A") return Reg::A;
    if (u == "B") return Reg::B;
    if (u == "C") return Reg::C;
    if (u == "D") return Reg::D;
    if (u == "E") return Reg::E;
    if (u == "H") return Reg::H;
    if (u == "L") return Reg::L;
    if (u == "I") return Reg::I;
    if (u == "R") return Reg::R;
    if (u == "AF") return Reg::AF;
    if (u == "BC") return Reg::BC;
    if (u == "DE") return Reg::DE;
    if (u == "HL") return Reg::HL;
    if (u == "SP") return Reg::SP;
    if (u == "IX") return Reg::IX;
    if (u == "IY") return Reg::IY;
    if (u == "AF'") return Reg::AFp;
    if (u == "IXH" || u == "XH" || u == "HX") return Reg::IXH;
    if (u == "IXL" || u == "XL" || u == "LX") return Reg::IXL;
    if (u == "IYH" || u == "YH" || u == "HY") return Reg::IYH;
    if (u == "IYL" || u == "YL" || u == "LY") return Reg::IYL;
    return Reg::None;
}
Cond condFromUpper(const std::string &u) {
    if (u == "NZ") return Cond::NZ;
    if (u == "Z") return Cond::Z;
    if (u == "NC") return Cond::NC;
    if (u == "C") return Cond::C;
    if (u == "PO") return Cond::PO;
    if (u == "PE") return Cond::PE;
    if (u == "P") return Cond::P;
    if (u == "M") return Cond::M;
    return Cond::None;
}
// Registre valide dans une indirection simple (BC)(DE)(HL)(SP)(C)(IX)(IY)
Reg regIndFromUpper(const std::string &u) {
    if (u == "BC") return Reg::BC;
    if (u == "DE") return Reg::DE;
    if (u == "HL") return Reg::HL;
    if (u == "SP") return Reg::SP;
    if (u == "C") return Reg::C;
    if (u == "IX") return Reg::IX;
    if (u == "IY") return Reg::IY;
    return Reg::None;
}

} // namespace

bool parseOperand(const std::string &text, bool allowCondition,
                  Operand &out, std::string &err) {
    std::string t = trim(text);
    if (t.empty()) { err = "opérande vide"; return false; }
    std::string u = upper(t);

    // condition (uniquement en position autorisée)
    if (allowCondition) {
        Cond c = condFromUpper(u);
        if (c != Cond::None) { out = Operand::condition(c); return true; }
    }

    // indirection / mémoire : parenthèses englobantes
    if (fullyEnclosed(t)) {
        std::string inner = trim(t.substr(1, t.size() - 2));
        std::string ui = upper(inner);
        // (BC)(DE)(HL)(SP)(C)(IX)(IY)
        Reg ri = regIndFromUpper(ui);
        if (ri != Reg::None) { out = Operand::rind(ri); return true; }
        // (IX+d) / (IY-d)
        if ((ui.rfind("IX", 0) == 0 || ui.rfind("IY", 0) == 0) &&
            (inner.size() == 2 || !isIdentChar(inner[2]))) {
            Reg xy = (ui[1] == 'X') ? Reg::IX : Reg::IY;
            std::string disp = trim(inner.substr(2));
            if (!disp.empty() && disp[0] == '+') disp = trim(disp.substr(1));
            if (disp.empty()) disp = "0";
            out = Operand::idx(xy, disp);
            return true;
        }
        // (nn) mémoire absolue
        out = Operand::mem(inner);
        return true;
    }

    // registre simple
    Reg r = regFromUpper(u);
    if (r != Reg::None) { out = Operand::r(r); return true; }

    // sinon : expression immédiate (nombre, label, calcul, caractère)
    out = Operand::imm(t);
    return true;
}

Result parseLine(const std::string &line) {
    Result r;
    std::string code = trim(line);
    if (code.empty()) { r.ok = true; return r; }

    std::string rest;
    peelLabel(code, r.label, rest);
    if (rest.empty()) { r.ok = true; return r; } // ligne label-seul

    std::string mnemoTok = firstToken(rest);
    r.mnemonic = upper(mnemoTok);
    r.operandsText = restAfterFirst(rest);

    z80::Mnemo m = z80::mnemoFromString(r.mnemonic);
    if (m == z80::Mnemo::Invalid) {
        // directive ou inconnu : routé vers l'assembleur
        r.ok = true; r.isInstruction = false;
        return r;
    }

    std::vector<std::string> ops = splitTopLevel(r.operandsText, ',');
    if (ops.size() > 2) { r.error = "trop d'opérandes"; return r; }

    // désambiguïsation de `C` : position condition ?
    bool condOp1 = false;
    if ((m == z80::Mnemo::JP || m == z80::Mnemo::CALL || m == z80::Mnemo::JR) && ops.size() == 2)
        condOp1 = true;
    if (m == z80::Mnemo::RET && ops.size() == 1)
        condOp1 = true;

    r.instr.mnemo = m;
    if (ops.size() >= 1 && !parseOperand(ops[0], condOp1, r.instr.a, r.error)) return r;
    if (ops.size() >= 2 && !parseOperand(ops[1], false, r.instr.b, r.error)) return r;

    r.isInstruction = true;
    r.ok = true;
    return r;
}

} // namespace parser
