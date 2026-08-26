// asm.cpp - Assembleur 2 passes (voir asm.h)
#include "asm.h"

#include <map>

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
// Texte d'un littéral, ou l'argument tel quel s'il n'en est pas un — pour les
// messages (ASSERT), où un argument non quoté reste lisible.
std::string literalText(const std::string &s) {
    const kw::Literal lit = kw::readLiteral(s, 0);
    return (lit.present && lit.error.empty()) ? lit.bytes : s;
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
        spaces_.clear();
        sites_.clear();
        ov_.active = false;
        symbols_.clear();
        ciIndex_.clear();
        symInfo_.clear();

        pass_ = 1; pc_ = 0; lo_ = 0x10000; hi_ = 0; orgBank_ = -1; displacement_ = 0; definedP1_.clear(); equDefs_.clear(); currentGlobal_.clear();
        badNames_.clear();
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

        pass_ = 2; pc_ = 0; lo_ = 0x10000; hi_ = 0; orgBank_ = -1; displacement_ = 0;
        displacedRanges_.clear(); currentGlobal_.clear();
        for (const auto &l : lines) process(l);
        flushOverlap();   // le dernier chevauchement accumulé doit sortir avant les diagnostics
        warnRunDisplaced();

        Output o;
        // Output.symbols reste entier : c'est une table d'ADRESSES destinee aux
        // outils et aux humains. La precision reelle n'a d'interet qu'a
        // l'interieur du calcul d'expressions.
        for (const auto &kv : symbols_) o.symbols[kv.first] = (int64_t)std::llround(kv.second);
        // La table exportable : la VALEUR vient de `symbols_`, pour qu'un EQU
        // resolu a point fixe porte sa valeur finale et non sa premiere lecture.
        for (const auto &kv : symInfo_) {
            auto v = symbols_.find(kv.first);
            if (v == symbols_.end()) continue;   // nom refuse en cours de route
            Symbol sy = kv.second;
            sy.value = (int64_t)std::llround(v->second);
            o.symbolTable.push_back(sy);
        }
        o.errors = errors_;
        o.warnings = warnings_;
        o.prints = prints_;
        o.ok = errors_.empty();
        // L'image plate est RECONSTITUEE depuis les banques de base, pour que
        // `sna::build` et le harnais de comparaison restent inchangés tant que le
        // format de sortie est plat.
        o.image.assign(kFlatBanks * 0x4000, 0);
        o.coverage.assign(kFlatBanks * 0x4000, 0);
        for (const auto &kv : spaces_) {
            o.banksWritten.push_back(kv.first);
            if (kv.first >= kFlatBanks) continue;   // hors de portée d'un dump plat
            const int base = kv.first * 0x4000;
            for (int k = 0; k < 0x4000; ++k) {
                o.image[base + k] = kv.second.bytes[k];
                o.coverage[base + k] = kv.second.prov[k] ? 1 : 0;
            }
        }
        if (hi_ > lo_) {
            o.loadAddress = (uint16_t)lo_;
            o.bin.assign(o.image.begin() + lo_, o.image.begin() + hi_);
        }
        o.runAddress = hasRun_ ? (uint16_t)run_ : o.loadAddress;
        return o;
    }

    // Un RUN qui tombe dans un bloc deplace fait demarrer le PC sur de la memoire
    // vide : les octets sont ranges ailleurs, en attente d'etre recopies. Le PC
    // reste celui que la source a demande — « run label » doit valoir ce que vaut
    // « label », sinon plus rien n'est previsible — mais le silence laisserait une
    // panne a l'execution sans diagnostic, la meme raison qui fait avertir sur une
    // banque remanente (ADR 0005).
    void warnRunDisplaced() {
        if (!hasRun_) return;
        const int r = run_ & 0xFFFF;
        for (const auto &g : displacedRanges_) {
            if (r < g.first || r >= g.second) continue;
            char msg[192];
            snprintf(msg, sizeof msg,
                     "RUN &%04X falls inside a displaced ORG block: the bytes are stored elsewhere, "
                     "so nothing is at this address until a loader copies them there", r);
            warnings_.push_back({runFile_, runLine_, msg});
            return;
        }
    }

    // --- IAsmContext ---
    void emit(uint8_t b) override {
        if (pass_ == 2) {
            // L'octet va a l'adresse de RANGEMENT ; l'adresse logique, elle, ne
            // sert qu'aux labels et aux expressions. Hors bloc deplace les deux
            // coincident, `displacement_` valant zero.
            const int st = (pc_ + displacement_) & 0xFFFF;
            const int bank = bankOf(st);
            const int off = st & 0x3FFF;   // ADR 0005 : l'offset est le masquage
            Space &sp = spaceFor(bank);
            noteWrite(bank, off, st, sp);
            sp.bytes[off] = b;
            // lo_/hi_ ne decrivent que le binaire plat des 64 K de base.
            if (bank < 4) {
                const int flat = bank * 0x4000 + off;
                if (flat < lo_) lo_ = flat;
                if (flat + 1 > hi_) hi_ = flat + 1;
            }
            if (displacement_) noteDisplaced(pc_ & 0xFFFF);
        }
        ++pc_;
    }
    uint16_t pc() const override { return (uint16_t)(pc_ & 0xFFFF); }
    void error(const std::string &msg) override { if (pass_ == 2) push(msg); }
    int64_t eval(const std::string &e) override { return evalExpr(e); }

private:
    // --- Le modele memoire (ADR 0006) --------------------------------------
    // Une collection d'espaces de 16 K indexee par banque, et non un tableau plat
    // de 64 K : le masquage 16 bits ne laissait aucun endroit ou loger une banque.
    // Les banques 0..3 forment les 64 K de base, les suivantes l'extension.
    //
    // Chaque espace porte SA coverage : elle est allouee a la premiere ecriture,
    // si bien qu'un source qui n'ecrit qu'en banque 4 ne paie pas les 64 K de base.
    // Les banques 0..7 : les 64 K de base plus l'extension du 6128, soit ce qu'un
    // dump plat de 128 K sait porter.
    static const int kFlatBanks = 8;

    struct Space {
        std::vector<uint8_t> bytes;
        std::vector<uint16_t> prov;   // 0 = jamais ecrit, sinon 1+index dans sites_
        Space() : bytes(0x4000, 0), prov(0x4000, 0) {}
    };
    std::map<int, Space> spaces_;

    Space &spaceFor(int bank) { return spaces_[bank]; }

    // La banque ou ranger l'octet d'adresse logique `pc`.
    //
    // Sans prefixe rencontre, elle SUIT l'adresse — c'est le comportement
    // historique, et il reste juste : les 64 K de base sont les banques 0..3.
    // Apres un « org b<n>: », elle est REMANENTE jusqu'au prochain ORG (ADR 0005).
    int bankOf(int pc) const { return orgBank_ < 0 ? ((pc >> 14) & 3) : orgBank_; }

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
    struct Overlap { bool active = false; int bank = 0, start = 0, end = 0; uint16_t prev = 0, cur = 0; };

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
        // La banque n'est nommee que si elle sort des 64 K de base : la mentionner
        // partout ferait du bruit sur l'immense majorite des sources, qui n'en ont
        // qu'une notion implicite.
        char bk[24] = "";
        if (ov_.bank >= 4) snprintf(bk, sizeof bk, "banque %d, ", ov_.bank);
        warnings_.push_back({where, line,
            std::string("chevauchement : ") + bk + range + " deja ecrit par " + siteLabel(ov_.prev)});
    }

    // `off` localise l'octet DANS sa banque (c'est la que le recouvrement se
    // produit) ; `addr` est l'adresse de RANGEMENT, celle ou les octets s'ecrasent
    // reellement et donc celle que le diagnostic doit nommer. Hors bloc deplace
    // elle est aussi l'adresse logique.
    void noteWrite(int bank, int off, int addr, Space &sp) {
        uint16_t site = siteId();
        uint16_t prev = sp.prov[off];
        sp.prov[off] = site;
        if (prev == 0 || prev == site) return;   // écrire sur du vierge, ou se relire soi-même
        if (ov_.active && ov_.bank == bank && ov_.end == addr &&
            ov_.prev == prev && ov_.cur == site) { ov_.end = addr + 1; return; }
        flushOverlap();
        ov_ = {true, bank, addr, addr + 1, prev, site};
    }

    void noteDisplaced(int a) {
        if (!displacedRanges_.empty() && displacedRanges_.back().second == a) {
            displacedRanges_.back().second = a + 1;
            return;
        }
        displacedRanges_.push_back({a, a + 1});
    }

    std::vector<Diagnostic> prints_;
    double lastReal_ = 0;
    bool evalRealLast_ = false;
    int instrStart_ = 0;
    bool inInstruction_ = false;
    std::map<std::string, double> symbols_;   // exact ; arrondi seulement a la sortie
    std::map<std::string, std::string> ciIndex_; // MAJUSCULES(nom) -> nom exact, pour le repli insensible à la casse
    std::map<std::string, Symbol> symInfo_;      // type, rangement et provenance, pour la table exportable
    std::set<std::string> definedP1_;
    std::vector<std::pair<std::string, std::string>> equDefs_; // (nom, texte expr) pour la résolution
    std::set<std::string> badNames_;          // noms refusés déjà signalés (ADR 0015)
    std::vector<Diagnostic> errors_;
    std::vector<Diagnostic> warnings_;
    int pass_ = 1, pc_ = 0, lo_ = 0, hi_ = 0;
    int orgBank_ = -1;   // -1 : aucun prefixe rencontre, la banque suit l'adresse
    // Ecart entre l'adresse de rangement et l'adresse logique, pose par le second
    // parametre d'ORG. Zero hors bloc deplace, remis a zero par tout ORG nu.
    int displacement_ = 0;
    // Les plages d'adresses LOGIQUES couvertes par un bloc deplace. Elles seules
    // permettent de dire qu'un RUN tombe sur du code qui n'est pas encore la.
    std::vector<std::pair<int, int>> displacedRanges_;
    int run_ = 0; bool hasRun_ = false;
    std::string runFile_; int runLine_ = 0;   // provenance du RUN, pour son avertissement
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

    // Note ce que la table exportable a besoin de savoir et que `symbols_` ne
    // porte pas (ADR 0019) : le type, le rangement et la PROVENANCE — fichier et
    // ligne d'ORIGINE, avant preprocesseur, seule reponse a « ou l'auteur a-t-il
    // ecrit ce nom ? ».
    //
    // En passe 1 seulement : les adresses y sont deja definitives, et les valeurs
    // sont relues de `symbols_` a la sortie, ce qui donne aux EQU leur valeur
    // resolue a point fixe plutot que celle de leur premiere lecture.
    //
    // Une redefinition ecrase : elle a deja son erreur pour une constante, et pour
    // une variable il n'y a rien a noter — elles n'entrent pas dans la table.
    void noteSymbol(const std::string &qn, bool isConst) {
        if (pass_ != 1) return;
        Symbol s;
        s.name = qn;
        s.isConst = isConst;
        s.file = cur_.file;
        s.line = cur_.line;
        if (!isConst) {
            const int st = (pc_ + displacement_) & 0xFFFF;
            s.bank = bankOf(st);
            s.store = st;
        }
        symInfo_[qn] = s;
    }

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

    // ADR 0015 : aucun identifiant utilisateur ne porte un nom du langage ni de la
    // machine. Le refus vit ici pour les symboles et les labels — l'assembleur est
    // le seul étage qui les définisse, et un fait ne se diagnostique qu'une fois.
    // Dédupliqué sur le nom : une variable refusée à l'intérieur d'un REPEAT déroulé
    // reparaîtrait sinon à chaque itération de la passe 1.
    bool nameRefused(const std::string &n, const std::string &position) {
        std::string bad = kw::reservedName(n, position);
        if (bad.empty()) return false;
        if (pass_ == 1 && badNames_.insert(n).second) push(bad);
        return true;
    }

    void defineLabel(const std::string &n) {
        if (nameRefused(n, "a label")) return;
        std::string qn = qualify(n);
        if (pass_ == 1) { if (!definedP1_.insert(qn).second) { structErr("duplicate symbol: '" + qn + "'"); return; } }
        setSymbol(qn, (double)(pc_ & 0xFFFF));
        noteSymbol(qn, /*isConst=*/false);
    }
    // `reassignable` : une VARIABLE ('=') peut être redéfinie, une CONSTANTE
    // ('EQU') non. Cf. ADR 0003 — "angle = i - 1" dans un "repeat 256,i" est
    // idiomatique, et l'interdire rejetait 9 sources du corpus.
    void defineSymbol(const std::string &n, double v, bool reassignable = false) {
        if (nameRefused(n, "a symbol")) return;
        std::string qn = qualify(n);
        if (pass_ == 1 && !reassignable) {
            if (!definedP1_.insert(qn).second) { structErr("duplicate symbol: '" + qn + "'"); return; }
        } else if (pass_ == 1) definedP1_.insert(qn);
        setSymbol(qn, v);
        // Une variable ne va pas dans la table exportable : sa valeur n'est celle
        // d'aucun point precis du programme, et un desassembleur n'en ferait rien.
        if (!reassignable) noteSymbol(qn, /*isConst=*/true);
    }

    // Un élément de « db » : soit une expression, soit un littéral de chaîne,
    // soit une CHAÎNE DÉCALÉE — un littéral suivi d'une queue arithmétique
    // appliquée à chacun de ses octets (« db 'hello'-'a' » émet cinq octets).
    //
    // Le littéral doit être en TÊTE de l'élément. « db 1+'hello' » et
    // « db ('hello')-'a' » ne sont pas des chaînes décalées : ce sont des
    // expressions, et expr les refuse lui-même puisqu'un littéral multi-octets
    // n'y a pas de valeur. Ce refus vit donc en un seul endroit.
    void emitByteOrStr(const std::string &p) {
        const kw::Literal lit = kw::readLiteral(p, 0);
        if (!lit.present) { emit((uint8_t)(evalExpr(p) & 0xFF)); return; }
        if (!lit.error.empty()) { structErr(lit.error + ": " + p); return; }

        const std::string tail = trim(p.substr(lit.end));
        if (tail.empty()) {                       // littéral nu
            for (char c : lit.bytes) emit((uint8_t)c);
            return;
        }
        // Un second littéral dans la queue n'a pas de lecture : « db 'ab'-'cd' »
        // décalerait deux octets par deux autres, sans règle d'appariement.
        for (size_t k = 0; k < tail.size(); ++k) {
            if (tail[k] != '"' && tail[k] != '\'') continue;
            const kw::Literal t2 = kw::readLiteral(tail, k);
            if (t2.error.empty() && t2.bytes.size() > 1) {
                structErr("two string literals in one element: " + p +
                          " (a shifted string takes a numeric tail, e.g. db 'hello'-'a')");
                return;
            }
            k = t2.error.empty() ? t2.end - 1 : tail.size();
        }
        for (char c : lit.bytes)
            emit((uint8_t)(evalExpr(std::to_string((unsigned char)c) + " " + tail) & 0xFF));
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
    // « org [b<n>:]adresse » (ADR 0005). Le prefixe designe l'emplacement de
    // RANGEMENT, le nombre qui suit reste l'adresse LOGIQUE — celle que prennent
    // les labels. L'offset dans la banque vaut « adresse & 0x3FFF ».
    //
    // Rien n'est deduit : une banque n'a pas de slot naturel, les configurations
    // RAM du gate array paginant toute banque supplementaire dans le slot 1. Un
    // offset derive du numero donnerait des labels faux trois fois sur quatre.
    // Detache un eventuel prefixe de banque « b<n>: » de `arg`. Renvoie false sur
    // refus, le message etant deja pousse.
    bool peelBank(std::string &arg, int &bank) {
        const size_t colon = arg.find(':');
        if (colon == std::string::npos) return true;
        const std::string pfx = trim(arg.substr(0, colon));
        if (!kw::isBankRef(pfx)) {
            structErr("ORG: '" + pfx + "' is not a bank reference — write 'org b<n>:<address>'");
            return false;
        }
        const long n = strtol(pfx.c_str() + 1, nullptr, 10);
        if (n < 0 || n > 255) { structErr("ORG: bank " + std::to_string(n) + " is out of range"); return false; }
        arg = trim(arg.substr(colon + 1));
        if (arg.empty()) { structErr("ORG: missing address after the bank prefix"); return false; }
        bank = (int)n;
        return true;
    }

    // ORG prend un ou DEUX parametres, a la semantique rasm (ADR 0005) :
    //
    //     org <logique>[,<rangement>]
    //
    // Le premier est l'adresse LOGIQUE : celle que prennent les labels, celle
    // pour laquelle le code est assemble. Le second est l'adresse de RANGEMENT :
    // la ou les octets sont reellement ecrits, en attendant qu'un chargeur les
    // recopie a l'adresse logique. Un bloc « org #A600,#100 » est donc du code
    // ecrit en #100 et destine a tourner en #A600.
    //
    // Le prefixe de banque qualifie le RANGEMENT (ADR 0005), il se porte donc sur
    // le parametre de rangement — le DERNIER. En forme a un parametre, l'unique
    // adresse fait les deux offices et le prefixe s'y porte, comme avant. Le
    // prefixe sur le premier parametre d'une forme a deux est REFUSE : le
    // rangement serait decrit de part et d'autre de l'adresse logique.
    //
    // Le deplacement N'EST PAS REMANENT : un ORG sans second parametre le remet a
    // zero. C'est le comportement de rasm, et c'est le comportement SUR — la
    // remise a zero remet le bloc la ou son ORG le dit. La banque, elle, reste
    // remanente et AVERTIT : c'est l'heritage silencieux qui est risque, pas la
    // remise a zero, d'ou l'asymetrie entre les deux.
    void doOrg(const std::string &ops) {
        auto parts = splitTopLevel(ops, ',');
        dropTrailingEmpty(parts);
        if (parts.empty()) { structErr("ORG: missing address"); return; }
        if (parts.size() > 2) {
            structErr("ORG: too many parameters — write 'org <address>[,<storage address>]'");
            return;
        }
        const bool displaced = parts.size() == 2;
        std::string logical = trim(parts[0]);
        std::string storage = displaced ? trim(parts[1]) : std::string();

        const size_t colon = logical.find(':');
        if (displaced && colon != std::string::npos) {
            const std::string pfx = trim(logical.substr(0, colon));
            const std::string addr = trim(logical.substr(colon + 1));
            if (storage.find(':') != std::string::npos)
                structErr("ORG: two bank prefixes — the bank qualifies the storage address only, "
                          "so it belongs on the last parameter");
            else
                structErr("ORG: the bank prefix qualifies the STORAGE address, which is the last "
                          "parameter — write 'org " + addr + "," + pfx + ":" + storage + "'");
            return;
        }

        int bank = -1;
        if (!peelBank(displaced ? storage : logical, bank)) return;
        if (bank >= 0) orgBank_ = bank;
        else if (orgBank_ >= 4) {
            // La banque est REMANENTE, mais un ORG nu qui en herite une hors des
            // 64 K de base deplacerait silencieusement le bloc si le prefixe a
            // simplement ete oublie — panne a l'execution, sans diagnostic.
            warn("ORG without a bank prefix inherits bank " + std::to_string(orgBank_) +
                 " (write 'org b" + std::to_string(orgBank_) + ":...' to confirm, or 'org b0:...' to leave it)");
        }
        pc_ = (int)evalExpr(logical);
        displacement_ = displaced ? (int)evalExpr(storage) - pc_ : 0;
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
            (kw::findAssign(rest) != std::string::npos && trim(rest.substr(0, kw::findAssign(rest))).empty());
        if (!label.empty() && !labelHasColon && !isDefinition)
            warn("label without ':': '" + label + "' (best practice: write '" + label + ":')");
        // contexte de qualification pour les labels locaux ".nom" sur les lignes suivantes
        // (un label local ne change pas le contexte : qualify() ne modifie que ceux en '.').
        //
        // Une DÉFINITION ne le change pas non plus : « delta equ 4 » au milieu d'une
        // routine nomme une constante, pas une adresse, et les « .x: » qui suivent
        // appartiennent toujours à la routine. Sans ça, ils devenaient « delta.x » et
        // « ld (plot.x+1),a » ne trouvait plus rien.
        if (!label.empty() && label[0] != '.' && !isDefinition) currentGlobal_ = label;

        // L'assembleur TOLÈRE des orthographes, jamais des structures (ADR 0017) :
        // « ld pc,hl » est acceptée ici au même titre que « defb », sans que la
        // canonisation ait eu à passer. Un pour un, un octet, aucune adresse.
        parser::Result pr = parser::parseLine(kw::canonicalJump(code));
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
        if (W0 == "ORG") { doOrg(after0); if (!label.empty()) defineLabel(label); return; }
        if (W0 == "RUN") { run_ = (int)evalExpr(after0); hasRun_ = true; runFile_ = cur_.file; runLine_ = cur_.line; if (!label.empty()) defineLabel(label); return; }
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
                if (parts.size() > 1) msg += " — " + literalText(trim(parts[1]));
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
                const kw::Literal lit = kw::readLiteral(a, 0);
                if (lit.present && lit.error.empty()) {
                    // La chaîne décalée est une construction d'ÉMISSION : elle
                    // rend une suite d'octets, et PRINT attend un texte.
                    // push() et non structErr() : PRINT ne s'évalue qu'en passe 2,
                    // où structErr se tait pour éviter les doublons — le
                    // diagnostic y était avalé (même piège que pour ASSERT).
                    if (!trim(a.substr(lit.end)).empty()) {
                        push("PRINT takes a string or a value, not a shifted string: " + a);
                        return;
                    }
                    out += lit.bytes; continue;
                }
                if (lit.present) { push(lit.error + ": " + a); return; }
                // Prefixe de format en MOT NU (ADR 0011) : « print "a=", hex v ».
                // Pas d'accolades : dans fantams « {X} » ne veut dire qu'une chose,
                // evaluer X et substituer.
                std::string fmt = upper(firstToken(a));
                if (fmt == "HEX" || fmt == "BIN" || fmt == "CHAR" || fmt == "INT") a = restAfterFirst(a);
                else fmt = "INT";
                const int64_t v = evalExpr(a);
                // Une expression qui n'a pas pu être évaluée a DÉJÀ produit son
                // erreur (evalExpr pousse en passe 2). Afficher « 0 » à côté
                // ajouterait une valeur fabriquée à un diagnostic — le lecteur
                // croirait à un résultat, alors qu'il n'y en a pas.
                if (!evalOk_) return;
                out += formatValue(v, fmt);
            }
            prints_.push_back({cur_.file, cur_.line, out});
            return;
        }
        // Refusees ou differees : le diagnostic nomme le remplacant plutot que de
        // laisser croire a un oubli. Cf. ADR 0004 (formats hors du source),
        // ADR 0005 (banques) et round 4 (TICKER).
        if (W0 == "BANK") { structErr("BANK is not supported: write 'org b" + trim(after0) + ":<address>' instead (the bank and the address belong on the same line)"); return; }
        if (W0 == "SNASET" || W0 == "SETCPC") { structErr(w0 + " describes the OUTPUT format, not the program: pass it at invocation instead of in the source"); return; }
        // Refus de fond, pas un manque : une permutation de jeu de caractères est
        // un encodage d'asset, au même titre qu'une image convertie en tuiles.
        // Et « charset » ne se lit nulle part : il change les octets émis par
        // toutes les lignes suivantes sans que la source déroulée le montre —
        // or elle est un livrable, réassemblable et lisible (ADR 0010).
        if (W0 == "CHARSET") { structErr("CHARSET is not supported: a character-set permutation is an asset encoding — generate the 'db' with a script (fantams macros cannot manipulate strings)"); return; }
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
        size_t eq = kw::findAssign(rest);
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
