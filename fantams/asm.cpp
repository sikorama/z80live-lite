// asm.cpp - Assembleur 2 passes (voir asm.h)
#include "asm.h"

#include <cmath>
#include "expr.h"
#include "keywords.h"
#include "parser.h"
#include "z80.h"

#include <cctype>
#include <set>
#include <string>
#include <vector>

namespace asmb {
namespace {

using kw::isIdentChar;
std::string upper(std::string s) { for (char &c : s) c = (char)std::toupper((unsigned char)c); return s; }
std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
using kw::stripComment;
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
// Retire les guillemets englobants et deshabille les echappements.
std::string unquote(const std::string &s) {
    if (s.size() >= 2 && (s[0] == '"' || s[0] == '\'') && s.back() == s[0])
        return s.substr(1, s.size() - 2);
    return s;
}
char unescape(char c) {
    switch (c) { case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return '\0'; case '\\': return '\\'; case '"': return '"'; case '\'': return '\''; }
    return c;
}

// Rendu d'une valeur pour PRINT, selon le mot-cle de format (ADR 0011).
std::string formatValue(int64_t v, const std::string &fmt) {
    if (fmt == "CHAR") return std::string(1, (char)(v & 0xFF));
    if (fmt == "HEX") { char b[32]; snprintf(b, sizeof b, "#%llX", (unsigned long long)(v & 0xFFFFFFFF)); return b; }
    if (fmt == "BIN") {
        uint64_t u = (uint64_t)v; int hi = 63; while (hi > 0 && !((u >> hi) & 1)) --hi;
        std::string s = "%"; for (int k = hi; k >= 0; --k) s += ((u >> k) & 1) ? '1' : '0';
        return s;
    }
    return std::to_string(v);
}

// ---------------------------------------------------------------------------
class Assembler : public z80::IAsmContext {
public:
    Output run(const std::vector<SourceLine> &lines) {
        image_.assign(65536, 0);
        prov_.assign(65536, 0);
        sites_.clear();
        ov_.active = false;
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
                double v = evalExprReal(d.second);
                auto it = symbols_.find(d.first);
                if (it == symbols_.end() || it->second != v) { setSymbol(d.first, v); changed = true; }
            }
            if (!changed) break;
        }

        pass_ = 2; pc_ = 0; lo_ = 0x10000; hi_ = 0; currentGlobal_.clear();
        for (const auto &l : lines) process(l);
        flushOverlap();   // le dernier chevauchement accumulé doit sortir avant les diagnostics

        Output o;
        // Output.symbols reste entier : c'est une table d'ADRESSES destinee aux
        // outils et aux humains. La precision reelle n'a d'interet qu'a
        // l'interieur du calcul d'expressions.
        for (const auto &kv : symbols_) o.symbols[kv.first] = (int64_t)std::llround(kv.second);
        o.errors = errors_;
        o.warnings = warnings_;
        o.prints = prints_;
        o.ok = errors_.empty();
        o.image = image_;
        o.coverage.assign(65536, 0);
        for (int a = 0; a < 65536; ++a) o.coverage[a] = prov_[a] ? 1 : 0;
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
            noteWrite(a);
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
    // --- coverage et provenance (ADR 0012) ---------------------------------
    // prov_[a] : 0 = jamais écrit, sinon 1+index dans sites_, ou kUnknownSite.
    // Deux octets par octet d'image, aujourd'hui sur une image plate de 64K ; le
    // jour où l'image devient une collection d'espaces d'adressage (ADR 0006),
    // c'est l'espace qui portera sa coverage, allouée à la première écriture.
    static const uint16_t kUnknownSite = 0xFFFF;

    struct Site { std::string file; int line; };
    // Un chevauchement en cours d'accumulation : les octets consécutifs qui
    // partagent le même couple (site écrasé, site écrasant) ne donnent qu'un
    // seul avertissement. C'est cette coalescence, et non un plafond, qui
    // empêche un bloc réécrit de produire un avertissement par octet.
    struct Overlap { bool active = false; int start = 0, end = 0; uint16_t prev = 0, cur = 0; };

    std::vector<uint16_t> prov_;
    std::vector<Site> sites_;
    int curSite_ = -1;      // site de la ligne courante, alloué à sa 1re écriture
    Overlap ov_;

    // Alloue paresseusement le site de la ligne courante : seules les lignes qui
    // émettent des octets entrent dans la table.
    uint16_t siteId() {
        if (curSite_ >= 0) return (uint16_t)curSite_;
        if (sites_.size() >= kUnknownSite - 1) { curSite_ = kUnknownSite; return kUnknownSite; }
        sites_.push_back({cur_.file, cur_.line});
        curSite_ = (int)sites_.size();   // 1-based : 0 signifie « jamais écrit »
        return (uint16_t)curSite_;
    }

    std::string siteLabel(uint16_t id) const {
        if (id == 0 || id == kUnknownSite) return "site inconnu";
        const Site &s = sites_[id - 1];
        return s.file + ":" + std::to_string(s.line);
    }

    void flushOverlap() {
        if (!ov_.active) return;
        ov_.active = false;
        char range[64];
        if (ov_.end - ov_.start == 1) snprintf(range, sizeof range, "&%04X", ov_.start);
        else snprintf(range, sizeof range, "&%04X-&%04X", ov_.start, ov_.end - 1);
        std::string where = (ov_.cur == 0 || ov_.cur == kUnknownSite)
                                ? std::string() : sites_[ov_.cur - 1].file;
        int line = (ov_.cur == 0 || ov_.cur == kUnknownSite) ? 0 : sites_[ov_.cur - 1].line;
        warnings_.push_back({where, line,
            std::string("chevauchement : ") + range + " deja ecrit par " + siteLabel(ov_.prev)});
    }

    void noteWrite(int a) {
        uint16_t site = siteId();
        uint16_t prev = prov_[a];
        prov_[a] = site;
        if (prev == 0 || prev == site) return;   // écrire sur du vierge, ou se relire soi-même
        if (ov_.active && ov_.end == a && ov_.prev == prev && ov_.cur == site) { ov_.end = a + 1; return; }
        flushOverlap();
        ov_ = {true, a, a + 1, prev, site};
    }

    std::vector<uint8_t> image_;
    std::vector<Diagnostic> prints_;
    double lastReal_ = 0;
    bool evalRealLast_ = false;
    int instrStart_ = 0;
    bool inInstruction_ = false;
    std::map<std::string, double> symbols_;   // exact ; arrondi seulement a la sortie
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
    void setSymbol(const std::string &n, double v) { symbols_[n] = v; ciIndex_[upper(n)] = n; }

    // Deux lectures d'une même expression : `evalExpr` pour émettre des octets,
    // `evalExprReal` pour DÉFINIR un symbole. Stocker l'arrondi ferait perdre
    // l'information avant tout usage — « v = 2.4 » puis « db v*2 » donnait 4 au
    // lieu de 5, l'écrasement ayant lieu au stockage, pas au calcul.
    double evalExprReal(const std::string &text) { evalRealLast_ = true; int64_t v = evalExpr(text); (void)v; return lastReal_; }
    int64_t evalExpr(const std::string &text) {
        evalOk_ = true;
        auto r = expr::eval(text, [&](const std::string &n, double &o) -> bool {
            // '$' vaut l'adresse de DÉBUT de l'instruction, pas la position
            // courante : pendant l'encodage, les octets d'opcode sont déjà émis
            // et pc_ a avancé (de 1, ou de 2 pour un préfixe DD/FD). Hors
            // instruction (db/dw/equ), pc_ EST la bonne réponse.
            if (n == "$") { o = (double)((inInstruction_ ? instrStart_ : pc_) & 0xFFFF); return true; }
            std::string qn = qualify(n);
            auto it = symbols_.find(qn);
            if (it != symbols_.end()) { o = it->second; return true; }
            // repli insensible à la casse (rasm ne distingue pas la casse des symboles) : on
            // avertit plutôt que d'échouer silencieusement sur une simple différence de casse.
            auto cit = ciIndex_.find(upper(qn));
            if (cit != ciIndex_.end()) {
                warn("symbol '" + qn + "' not found exactly, using '" + cit->second +
                     "' (case mismatch — best practice: match case exactly)");
                o = symbols_[cit->second];
                return true;
            }
            return false;
        });
        if (!r.ok) { evalOk_ = false; if (pass_ == 2) push(r.error); return 0; }
        lastReal_ = r.real;
        return r.value;
    }

    void defineLabel(const std::string &n) {
        std::string qn = qualify(n);
        if (pass_ == 1) { if (!definedP1_.insert(qn).second) { structErr("duplicate symbol: '" + qn + "'"); return; } }
        setSymbol(qn, (double)(pc_ & 0xFFFF));
    }
    // `reassignable` : une VARIABLE ('=') peut être redéfinie, une CONSTANTE
    // ('EQU') non. Cf. ADR 0003 — "angle = i - 1" dans un "repeat 256,i" est
    // idiomatique, et l'interdire rejetait 9 sources du corpus.
    void defineSymbol(const std::string &n, double v, bool reassignable = false) {
        std::string qn = qualify(n);
        if (pass_ == 1 && !reassignable) {
            if (!definedP1_.insert(qn).second) { structErr("duplicate symbol: '" + qn + "'"); return; }
        } else if (pass_ == 1) definedP1_.insert(qn);
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
    // Une virgule finale ("db 1,2,") est tolérée : elle est courante dans les
    // tables de données générées, et rasm l'accepte. Seul le DERNIER élément vide
    // est retiré — "db 1,,2" reste une erreur.
    static void dropTrailingEmpty(std::vector<std::string> &parts) {
        if (parts.size() > 1 && parts.back().empty()) parts.pop_back();
    }
    void emitDB(const std::string &ops) {
        auto parts = splitTopLevel(ops, ','); dropTrailingEmpty(parts);
        for (auto &p : parts) emitByteOrStr(p);
    }
    void emitDW(const std::string &ops) {
        auto parts = splitTopLevel(ops, ','); dropTrailingEmpty(parts);
        for (auto &p : parts) { int64_t v = evalExpr(p); emit((uint8_t)(v & 0xFF)); emit((uint8_t)((v >> 8) & 0xFF)); }
    }
    void emitDS(const std::string &ops) {
        auto parts = splitTopLevel(ops, ',');
        dropTrailingEmpty(parts);
        if (parts.empty()) { structErr("DS: missing size"); return; }
        // Plusieurs paires "compte,valeur" sur une même ligne : "ds 3,1,3,2"
        // réserve 3 octets à 1 puis 3 à 2. N'honorer que la première faussait la
        // LONGUEUR autant que le contenu.
        for (size_t p = 0; p < parts.size(); p += 2) {
            int64_t n = evalExpr(parts[p]);
            if (!evalOk_) { structErr("DS: size not resolvable in pass 1"); return; }
            int64_t fill = (p + 1 < parts.size()) ? evalExpr(parts[p + 1]) : 0;
            for (int64_t k = 0; k < n; ++k) emit((uint8_t)(fill & 0xFF));
        }
    }

    void process(const SourceLine &sl) {
        cur_ = sl;
        curSite_ = -1;
        std::string code = trim(stripComment(sl.text));
        if (code.empty()) return;

        std::string label, rest; bool labelHasColon = true;
        kw::peelLabel(code, label, rest, kw::Phase::Assembly, &labelHasColon);
        // "nom EQU valeur" et "nom = valeur" SONT la forme canonique d'une
        // définition de constante ou de variable : le ':' n'y a pas cours, et
        // avertir dessus noierait les vrais cas (un label d'adresse sans ':').
        const bool isDefinition =
            upper(firstToken(rest)) == "EQU" ||
            (findAssign(rest) != std::string::npos && trim(rest.substr(0, findAssign(rest))).empty());
        if (!label.empty() && !labelHasColon && !isDefinition)
            warn("label without ':': '" + label + "' (best practice: write '" + label + ":')");
        // contexte de qualification pour les labels locaux ".nom" sur les lignes suivantes
        // (un label local ne change pas le contexte : qualify() ne modifie que ceux en '.').
        if (!label.empty() && label[0] != '.') currentGlobal_ = label;

        parser::Result pr = parser::parseLine(code);
        if (pr.isInstruction) {
            if (!label.empty()) defineLabel(label);
            else if (cur_.col0)
                warn("instruction '" + pr.mnemonic + "' in column 1 (best practice: indent instructions — only labels/symbols should start in column 1)");
            instrStart_ = pc_;
            inInstruction_ = true;
            z80::encode(*this, pr.instr);
            inInstruction_ = false;
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

        // --- ASSERT / PRINT : verifier et inspecter -------------------------
        // Evalues en passe 2 uniquement : les labels y sont resolus, et PRINT ne
        // doit parler qu'une fois. Ce sont des outils de DIAGNOSTIC DE BUILD, la
        // meme famille que la source deroulee — ils disent ce que l'assembleur a
        // compris, ils ne decrivent pas la machine cible.
        if (W0 == "ASSERT") {
            if (!label.empty()) defineLabel(label);
            if (pass_ != 2) return;
            auto parts = splitTopLevel(after0, ',');
            if (parts.empty()) { structErr("ASSERT: missing condition"); return; }
            if (evalExpr(parts[0]) == 0) {
                std::string msg = "assertion failed: " + trim(parts[0]);
                if (parts.size() > 1) msg += " — " + unquote(trim(parts[1]));
                // push() et non structErr() : ce dernier ne rapporte qu'en passe 1
                // pour eviter les doublons, or ASSERT ne s'evalue qu'en passe 2,
                // quand les labels sont resolus. Son erreur y etait avalee.
                push(msg);
            }
            return;
        }
        if (W0 == "PRINT") {
            if (!label.empty()) defineLabel(label);
            if (pass_ != 2) return;
            std::string out;
            for (auto &p : splitTopLevel(after0, ',')) {
                std::string a = trim(p);
                if (a.empty()) continue;
                if (a[0] == '"' || a[0] == '\'') { out += unquote(a); continue; }
                // Prefixe de format en MOT NU (ADR 0011) : « print "a=", hex v ».
                // Pas d'accolades : dans fantams « {X} » ne veut dire qu'une chose,
                // evaluer X et substituer.
                std::string fmt = upper(firstToken(a));
                if (fmt == "HEX" || fmt == "BIN" || fmt == "CHAR" || fmt == "INT") a = restAfterFirst(a);
                else fmt = "INT";
                out += formatValue(evalExpr(a), fmt);
            }
            prints_.push_back({cur_.file, cur_.line, out});
            return;
        }
        // Refusees ou differees : le diagnostic nomme le remplacant plutot que de
        // laisser croire a un oubli. Cf. ADR 0004 (formats hors du source),
        // ADR 0005 (banques) et round 4 (TICKER).
        if (W0 == "BANK") { structErr("BANK is not supported: write 'org b" + trim(after0) + ":<address>' instead (the bank and the address belong on the same line)"); return; }
        if (W0 == "SNASET" || W0 == "SETCPC") { structErr(w0 + " describes the OUTPUT format, not the program: pass it at invocation instead of in the source"); return; }
        if (W0 == "TICKER") { structErr("TICKER is not supported: cycle counting is a control-flow analysis, not a directive (it cannot account for conditional jumps)"); return; }
        if (W0 == "STR") { structErr("STR is not implemented yet: use 'db' (STR emits the string with bit 7 set on the last character)"); return; }

        // définition de symbole : "name: EQU v" / "name EQU v" / "name = v"
        if (W0 == "EQU") {
            if (label.empty()) { structErr("EQU without a name"); return; }
            defineSymbol(label, evalExprReal(after0));
            if (pass_ == 1) equDefs_.push_back({qualify(label), after0});
            return;
        }
        if (W1 == "EQU") {
            std::string e = restAfterFirst(after0);
            defineSymbol(w0, evalExprReal(e));
            if (pass_ == 1) equDefs_.push_back({qualify(w0), e});
            return;
        }
        size_t eq = findAssign(rest);
        if (eq != std::string::npos) {
            std::string lhs = trim(rest.substr(0, eq));
            std::string rhs = trim(rest.substr(eq + 1));
            std::string name = lhs.empty() ? label : lhs;
            if (name.empty()) { structErr("assignment without a name"); return; }
            // Une variable est SÉQUENTIELLE : sa valeur en un point d'usage est celle
            // de la dernière affectation au-dessus. Elle n'entre donc PAS dans
            // equDefs_, dont la résolution à point fixe est le mécanisme des
            // constantes — il écraserait la valeur vue par les usages antérieurs.
            // Corollaire assumé : une variable ne se référence pas en avant.
            defineSymbol(name, evalExprReal(rhs), /*reassignable=*/true);
            return;
        }

        if (!label.empty()) defineLabel(label);
        structErr("unknown directive/mnemonic: '" + w0 + "'");
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
