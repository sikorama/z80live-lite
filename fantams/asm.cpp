// asm.cpp - Assembleur 2 passes (voir asm.h)
#include "asm.h"
#include "expr.h"
#include "parser.h"
#include "z80.h"

#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace asmb {
namespace {

bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '@';
}
std::string upper(std::string s) { for (char &c : s) c = (char)std::toupper((unsigned char)c); return s; }
std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string stripComment(const std::string &s) {
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; }
        else if (c == ';') return s.substr(0, i);
    }
    return s;
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
// Mots-clés qui ne peuvent jamais être un label sans ':' — même liste que parser.cpp
// (mnémoniques Z80 + directives gérées ici). Permet de tolérer "start" (label,
// sans ':') sans le confondre avec une instruction/directive.
bool isReservedWord(const std::string &upperTok) {
    if (z80::mnemoFromString(upperTok) != z80::Mnemo::Invalid) return true;
    static const std::set<std::string> kw = {
        "ORG", "RUN", "ALIGN", "DB", "DEFB", "DM", "DEFM", "DW", "DEFW",
        "DS", "DEFS", "RMB", "EQU",
        // directives rasm reconnues mais non implémentées (hors périmètre) : gardées
        // réservées pour échouer proprement plutôt que d'être lues comme un label.
        "BUILDSNA", "BANKSET", "NOLIST", "LIST",
    };
    return kw.count(upperTok) != 0;
}
void peelLabel(const std::string &code, std::string &label, std::string &rest, bool *sawColon = nullptr) {
    size_t p = 0; while (p < code.size() && isIdentChar(code[p])) ++p;
    if (p > 0) {
        size_t q = p; while (q < code.size() && std::isspace((unsigned char)code[q])) ++q;
        if (q < code.size() && code[q] == ':') {
            label = code.substr(0, p); rest = trim(code.substr(q + 1));
            if (sawColon) *sawColon = true;
            return;
        }
        if (!isReservedWord(upper(code.substr(0, p)))) {
            label = code.substr(0, p); rest = trim(code.substr(p));
            if (sawColon) *sawColon = false;
            return;
        }
    }
    label.clear(); rest = code;
}
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
// Position d'un '=' d'assignation (pas ==, <=, >=, !=), hors chaîne.
size_t findAssign(const std::string &s) {
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; continue; }
        if (c == '=') {
            char prev = i > 0 ? s[i - 1] : 0, next = i + 1 < s.size() ? s[i + 1] : 0;
            if (prev == '<' || prev == '>' || prev == '!' || prev == '=') continue;
            if (next == '=') continue;
            return i;
        }
    }
    return std::string::npos;
}
char unescape(char c) {
    switch (c) { case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return '\0'; case '\\': return '\\'; case '"': return '"'; case '\'': return '\''; }
    return c;
}

// ---------------------------------------------------------------------------
class Assembler : public z80::IAsmContext {
public:
    Output run(const std::vector<SourceLine> &lines) {
        image_.assign(65536, 0);
        symbols_.clear();
        ciIndex_.clear();

        pass_ = 1; pc_ = 0; lo_ = 0x10000; hi_ = 0; definedP1_.clear(); equDefs_.clear(); currentGlobal_.clear();
        for (const auto &l : lines) process(l);

        // Les labels sont fixés (adresses indépendantes des valeurs). On réévalue
        // les EQU/= jusqu'à point fixe : gère les EQU utilisés avant leur définition
        // et les chaînes d'EQU dépendant de labels avant.
        for (int iter = 0; iter < 32; ++iter) {
            bool changed = false;
            for (const auto &d : equDefs_) {
                int64_t v = evalExpr(d.second);
                auto it = symbols_.find(d.first);
                if (it == symbols_.end() || it->second != v) { setSymbol(d.first, v); changed = true; }
            }
            if (!changed) break;
        }

        pass_ = 2; pc_ = 0; lo_ = 0x10000; hi_ = 0; currentGlobal_.clear();
        for (const auto &l : lines) process(l);

        Output o;
        o.symbols = symbols_;
        o.errors = errors_;
        o.warnings = warnings_;
        o.ok = errors_.empty();
        o.image = image_;
        if (hi_ > lo_) {
            o.loadAddress = (uint16_t)lo_;
            o.bin.assign(image_.begin() + lo_, image_.begin() + hi_);
        }
        o.runAddress = hasRun_ ? (uint16_t)run_ : o.loadAddress;
        return o;
    }

    // --- IAsmContext ---
    void emit(uint8_t b) override {
        int a = pc_ & 0xFFFF;
        if (pass_ == 2) {
            image_[a] = b;
            if (a < lo_) lo_ = a;
            if (a + 1 > hi_) hi_ = a + 1;
        }
        ++pc_;
    }
    uint16_t pc() const override { return (uint16_t)(pc_ & 0xFFFF); }
    void error(const std::string &msg) override { if (pass_ == 2) push(msg); }
    int64_t eval(const std::string &e) override { return evalExpr(e); }

private:
    std::vector<uint8_t> image_;
    std::map<std::string, int64_t> symbols_;
    std::map<std::string, std::string> ciIndex_; // MAJUSCULES(nom) -> nom exact, pour le repli insensible à la casse
    std::set<std::string> definedP1_;
    std::vector<std::pair<std::string, std::string>> equDefs_; // (nom, texte expr) pour la résolution
    std::vector<Diagnostic> errors_;
    std::vector<Diagnostic> warnings_;
    int pass_ = 1, pc_ = 0, lo_ = 0, hi_ = 0;
    int run_ = 0; bool hasRun_ = false;
    SourceLine cur_;
    bool evalOk_ = true;
    // dernier label "global" (non local) rencontré : contexte de qualification des
    // labels locaux ".nom" (comme rasm : ".nom" == "<global>.nom" — cf. defineLabel/qualify).
    std::string currentGlobal_;

    void push(const std::string &msg) { errors_.push_back({cur_.file, cur_.line, msg}); }
    // avertissement de bonne pratique (non bloquant) : émis en passe 2 seulement (pas de doublon).
    void warn(const std::string &msg) { if (pass_ == 2) warnings_.push_back({cur_.file, cur_.line, msg}); }
    // erreur structurelle (signalée dès la passe 1, ne se reproduit pas en passe 2)
    void structErr(const std::string &msg) { if (pass_ == 1) push(msg); }

    // Un label local ".nom" est qualifié par le dernier label global rencontré
    // (comme rasm : deux ".loop" sous deux labels globaux différents ne collisionnent pas).
    std::string qualify(const std::string &n) const {
        return (!n.empty() && n[0] == '.') ? currentGlobal_ + n : n;
    }
    void setSymbol(const std::string &n, int64_t v) { symbols_[n] = v; ciIndex_[upper(n)] = n; }

    int64_t evalExpr(const std::string &text) {
        evalOk_ = true;
        auto r = expr::eval(text, [&](const std::string &n, int64_t &o) -> bool {
            if (n == "$") { o = pc_ & 0xFFFF; return true; }
            std::string qn = qualify(n);
            auto it = symbols_.find(qn);
            if (it != symbols_.end()) { o = it->second; return true; }
            // repli insensible à la casse (rasm ne distingue pas la casse des symboles) : on
            // avertit plutôt que d'échouer silencieusement sur une simple différence de casse.
            auto cit = ciIndex_.find(upper(qn));
            if (cit != ciIndex_.end()) {
                warn("symbole '" + qn + "' non trouvé exactement, utilisation de '" + cit->second +
                     "' (différence de casse — bonne pratique : utiliser la casse exacte)");
                o = symbols_[cit->second];
                return true;
            }
            return false;
        });
        if (!r.ok) { evalOk_ = false; if (pass_ == 2) push(r.error); return 0; }
        return r.value;
    }

    void defineLabel(const std::string &n) {
        std::string qn = qualify(n);
        if (pass_ == 1) { if (!definedP1_.insert(qn).second) { structErr("symbole déjà défini : " + qn); return; } }
        setSymbol(qn, pc_ & 0xFFFF);
    }
    void defineSymbol(const std::string &n, int64_t v) {
        std::string qn = qualify(n);
        if (pass_ == 1) { if (!definedP1_.insert(qn).second) { structErr("symbole déjà défini : " + qn); return; } }
        setSymbol(qn, v);
    }

    void emitByteOrStr(const std::string &p) {
        if (!p.empty() && p[0] == '"') {
            for (size_t k = 1; k < p.size(); ++k) {
                char c = p[k];
                if (c == '"') break;
                if (c == '\\' && k + 1 < p.size()) { ++k; c = unescape(p[k]); }
                emit((uint8_t)c);
            }
        } else emit((uint8_t)(evalExpr(p) & 0xFF));
    }
    void emitDB(const std::string &ops) { for (auto &p : splitTopLevel(ops, ',')) emitByteOrStr(p); }
    void emitDW(const std::string &ops) {
        for (auto &p : splitTopLevel(ops, ',')) { int64_t v = evalExpr(p); emit((uint8_t)(v & 0xFF)); emit((uint8_t)((v >> 8) & 0xFF)); }
    }
    void emitDS(const std::string &ops) {
        auto parts = splitTopLevel(ops, ',');
        if (parts.empty()) { structErr("DS : taille manquante"); return; }
        int64_t n = evalExpr(parts[0]);
        if (!evalOk_) { structErr("DS : taille non résoluble en passe 1"); return; }
        int64_t fill = parts.size() > 1 ? evalExpr(parts[1]) : 0;
        for (int64_t k = 0; k < n; ++k) emit((uint8_t)(fill & 0xFF));
    }

    void process(const SourceLine &sl) {
        cur_ = sl;
        std::string code = trim(stripComment(sl.text));
        if (code.empty()) return;

        std::string label, rest; bool labelHasColon = true;
        peelLabel(code, label, rest, &labelHasColon);
        if (!label.empty() && !labelHasColon)
            warn("label sans ':' : '" + label + "' (bonne pratique : écrire '" + label + ":')");
        // contexte de qualification pour les labels locaux ".nom" sur les lignes suivantes
        // (un label local ne change pas le contexte : qualify() ne modifie que ceux en '.').
        if (!label.empty() && label[0] != '.') currentGlobal_ = label;

        parser::Result pr = parser::parseLine(code);
        if (pr.isInstruction) {
            if (!label.empty()) defineLabel(label);
            else if (cur_.col0)
                warn("instruction '" + pr.mnemonic + "' en colonne 1 : bonne pratique = indenter les instructions (seuls les labels/symboles commencent en colonne 1)");
            z80::encode(*this, pr.instr);
            return;
        }
        if (rest.empty()) { if (!label.empty()) defineLabel(label); return; }

        std::string w0 = firstToken(rest), W0 = upper(w0);
        std::string after0 = restAfterFirst(rest);
        std::string W1 = upper(firstToken(after0));

        // contrôle du listing : aucun effet sur le code généré (no-op, comme chez rasm)
        if (W0 == "NOLIST" || W0 == "LIST") { if (!label.empty()) defineLabel(label); return; }

        // BUILDSNA / BANKSET : en-tête rasm de génération de snapshot. fantams produit
        // toujours un .sna à plat (pas de multi-bank) -> no-op, pour accepter les sources
        // écrites pour rasm sans réécrire leur en-tête. (ORG/RUN sur la même ligne,
        // séparés par ':', sont déjà traités normalement comme des directives à part.)
        if (W0 == "BUILDSNA" || W0 == "BANKSET") { if (!label.empty()) defineLabel(label); return; }

        // directives d'émission / contrôle
        if (W0 == "ORG") { pc_ = (int)evalExpr(after0); if (!label.empty()) defineLabel(label); return; }
        if (W0 == "RUN") { run_ = (int)evalExpr(after0); hasRun_ = true; if (!label.empty()) defineLabel(label); return; }
        if (W0 == "ALIGN") {
            int64_t n = evalExpr(after0);
            if (n > 0) pc_ = (int)((pc_ + n - 1) & ~(n - 1));
            if (!label.empty()) defineLabel(label);
            return;
        }
        if (W0 == "DB" || W0 == "DEFB" || W0 == "DM" || W0 == "DEFM") { if (!label.empty()) defineLabel(label); emitDB(after0); return; }
        if (W0 == "DW" || W0 == "DEFW") { if (!label.empty()) defineLabel(label); emitDW(after0); return; }
        if (W0 == "DS" || W0 == "DEFS" || W0 == "RMB") { if (!label.empty()) defineLabel(label); emitDS(after0); return; }

        // définition de symbole : "name: EQU v" / "name EQU v" / "name = v"
        if (W0 == "EQU") {
            if (label.empty()) { structErr("EQU sans nom"); return; }
            defineSymbol(label, evalExpr(after0));
            if (pass_ == 1) equDefs_.push_back({qualify(label), after0});
            return;
        }
        if (W1 == "EQU") {
            std::string e = restAfterFirst(after0);
            defineSymbol(w0, evalExpr(e));
            if (pass_ == 1) equDefs_.push_back({qualify(w0), e});
            return;
        }
        size_t eq = findAssign(rest);
        if (eq != std::string::npos) {
            std::string lhs = trim(rest.substr(0, eq));
            std::string rhs = trim(rest.substr(eq + 1));
            std::string name = lhs.empty() ? label : lhs;
            if (name.empty()) { structErr("assignation sans nom"); return; }
            defineSymbol(name, evalExpr(rhs));
            if (pass_ == 1) equDefs_.push_back({qualify(name), rhs});
            return;
        }

        if (!label.empty()) defineLabel(label);
        structErr("directive/mnémonique inconnu : " + w0);
    }
};

} // namespace

Output assemble(const std::vector<SourceLine> &lines) {
    Assembler a;
    return a.run(lines);
}

Output assembleText(const std::string &source, const std::string &file) {
    std::vector<SourceLine> lines;
    std::string cur; int ln = 1;
    for (size_t i = 0; i <= source.size(); ++i) {
        char c = (i < source.size()) ? source[i] : '\n';
        if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back();
            bool col0 = !cur.empty() && !std::isspace((unsigned char)cur[0]);
            lines.push_back({cur, file, ln++, col0}); cur.clear(); }
        else cur += c;
    }
    if (!lines.empty() && lines.back().text.empty()) lines.pop_back();
    return assemble(lines);
}

} // namespace asmb
