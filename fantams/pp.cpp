// pp.cpp - Préprocesseur fantams (voir pp.h)
#include "pp.h"
#include "expr.h"
#include "keywords.h"
#include "z80.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <functional>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace pp {
namespace {

// --- petits utilitaires texte ---------------------------------------------
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
bool isIdentifier(const std::string &s) {
    if (s.empty() || std::isdigit((unsigned char)s[0])) return false;
    for (char c : s) if (!isIdentChar(c)) return false;
    return true;
}
// Retire les commentaires bloc /* ... */ (peuvent s'étendre sur plusieurs lignes),
// hors chaînes/caractères. Les '\n' à l'intérieur du bloc sont préservés pour ne
// pas décaler la numérotation des lignes dans les diagnostics.
std::string stripBlockComments(const std::string &s) {
    std::string out;
    out.reserve(s.size());
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ) {
        char c = s[i];
        if (inStr) { out += c; if (c == q) inStr = false; ++i; continue; }
        // Commentaire de ligne (';' ou '//') : recopié tel quel jusqu'au saut de
        // ligne, SANS interpréter les quotes. Sans ça, une apostrophe dans un
        // commentaire français ("l'image", "d'après") ouvre une chaîne qui ne se
        // referme jamais, et tous les /* ... */ du reste du fichier deviennent
        // invisibles — constaté sur deux sources du corpus, où le bloc masqué
        // était respectivement 28 et 1000 lignes plus bas.
        if (c == ';' || (c == '/' && i + 1 < s.size() && s[i + 1] == '/')) {
            while (i < s.size() && s[i] != '\n') out += s[i++];
            continue;
        }
        if (c == '"' || c == '\'') { inStr = true; q = c; out += c; ++i; continue; }
        if (c == '/' && i + 1 < s.size() && s[i + 1] == '*') {
            size_t j = s.find("*/", i + 2);
            size_t end = (j == std::string::npos) ? s.size() : j + 2;
            for (size_t k = i; k < end; ++k) if (s[k] == '\n') out += '\n';
            i = end;
            continue;
        }
        out += c; ++i;
    }
    return out;
}
// Formate une valeur numerique pour REINJECTION dans du texte source.
// Entiere -> forme entiere (« 3 », pas « 3.000000 ») : c'est le cas de tous les
// compteurs de boucle et de la generation de labels, ou des decimales seraient
// catastrophiques. Reelle -> forme decimale sans zeros inutiles.
std::string fmtNum(double v) {
    if (v == (double)(int64_t)v && std::fabs(v) < 9e15) return std::to_string((int64_t)v);
    std::string s = std::to_string(v);
    while (s.size() > 1 && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}
// Le préprocesseur travaille au cran le plus large : ses propres mots-clés
// (MACRO, REPEAT, LET…) ne sont pas des labels ici, et n'existent plus après.
// Sert aussi à distinguer "ident:" (label collé) de "ei:ret" (deux instructions
// collées : "ei" est un mnémo connu, pas un label).
bool isReservedWord(const std::string &upperTok) {
    return kw::isReservedWord(upperTok, kw::Phase::Preprocess);
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
// Épluchage de label au cran préprocesseur. La phase est fixée ici, une fois,
// plutôt qu'à chacun des sites d'appel.
void peelLabel(const std::string &code, std::string &label, std::string &rest,
              const std::function<bool(const std::string &)> &isMacro = nullptr) {
    kw::peelLabel(code, label, rest, kw::Phase::Preprocess, nullptr, isMacro);
}
// Position d'un '=' d'assignation (pas ==, <=, >=, !=), hors chaîne.
// Même règle qu'asm.cpp : les deux étages doivent s'accorder sur ce qui est une
// définition, sans quoi le préprocesseur et l'assembleur liraient des sources
// différentes. À factoriser dans un en-tête commun le jour où un troisième
// appelant apparaît.
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
// Découpe en respectant () [] {} "" ''.
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
// Remplace `from` par `to`, uniquement sur des mots entiers.
std::string replaceWord(const std::string &text, const std::string &from, const std::string &to) {
    std::string out; size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, from.size(), from) == 0 &&
            (i == 0 || !isIdentChar(text[i - 1])) &&
            (i + from.size() >= text.size() || !isIdentChar(text[i + from.size()]))) {
            out += to; i += from.size();
        } else out += text[i++];
    }
    return out;
}
// Découpe une ligne en instructions sur le séparateur ':' (retour à la ligne),
// en respectant chaînes/caractères et () [] {}. Le ':' qui termine un label de
// tête collé ("foo:") est conservé et NE coupe pas — SAUF si "foo" est un mnémo/
// directive connu (ex. "ei:ret") : dans ce cas ':' sépare comme d'habitude, et
// l'identifiant est ajouté à `warnOut` (style non canonique : ambigu avec un label,
// même si techniquement accepté — cf. `ei : ret` ou `ei: ret`, sans ambiguïté).
std::vector<std::string> splitStatements(const std::string &s, std::vector<std::string> *warnOut = nullptr) {
    std::vector<std::string> out;
    std::string cur;
    bool inStr = false; char q = 0; int depth = 0; bool firstColon = true;
    auto flush = [&]() { std::string t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); firstColon = true; };
    for (char c : s) {
        if (inStr) { cur += c; if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; cur += c; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; cur += c; continue; }
        if (c == ':' && depth == 0) {
            std::string ident = trim(cur);
            bool glued = firstColon && !cur.empty() && isIdentChar(cur.back()) && isIdentifier(ident);
            if (glued && !isReservedWord(upper(ident))) {
                cur += c; firstColon = false; continue; // vrai label collé : conservé
            }
            if (glued && warnOut) warnOut->push_back(ident); // mnémo/directive collé à ':' -> avertir
            flush(); continue;
        }
        cur += c;
    }
    std::string t = trim(cur); if (!t.empty()) out.push_back(t);
    return out;
}
// push/pop multi-registres -> un push/pop par registre : "push af,bc" => "push af" / "push bc".
// Sucre préprocesseur (les mnémoniques Z80 réels ne prennent qu'un opérande).
std::vector<std::string> expandPushPop(const std::string &stmt) {
    std::string label, rest; peelLabel(stmt, label, rest);
    std::string mnTok = firstToken(rest), MN = upper(mnTok);
    if (MN != "PUSH" && MN != "POP") return {stmt};
    auto regs = splitTopLevel(restAfterFirst(rest), ',');
    if (regs.size() <= 1) return {stmt};
    std::vector<std::string> out;
    for (size_t k = 0; k < regs.size(); ++k)
        out.push_back((k == 0 && !label.empty() ? label + ": " : "") + mnTok + " " + regs[k]);
    return out;
}

// Position du mot-clé de plage (`to` ou `until`) dans l'en-tête d'un FOR, au
// niveau supérieur et en JETON entier — un symbole nommé « stop » contient « to »,
// et le chercher en sous-chaîne le couperait en deux. `inclusive` dit lequel a été
// trouvé. Rend npos si aucun des deux n'y est.
inline size_t findRangeKeyword(const std::string &s, bool &inclusive) {
    int depth = 0; bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        const char c = s[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; continue; }
        if (c == '(' || c == '[') { ++depth; continue; }
        if (c == ')' || c == ']') { if (depth > 0) --depth; continue; }
        if (depth || !std::isalpha((unsigned char)c)) continue;
        if (i && isIdentChar(s[i - 1])) continue;             // milieu d'identifiant
        size_t j = i; while (j < s.size() && isIdentChar(s[j])) ++j;
        const std::string tok = upper(s.substr(i, j - i));
        if (tok == "TO")    { inclusive = true;  return i; }
        if (tok == "UNTIL") { inclusive = false; return i; }
        i = j - 1;
    }
    return std::string::npos;
}

// --- Canonisation (ADR 0017) -------------------------------------------------
// Une règle de canonisation ne produit QUE du Z80 canonique : jamais de directive
// de préprocesseur, jamais de substitution, jamais de sucre. C'est cette contrainte
// qui garantit qu'une seule passe suffit, et qui met `-E`, `--normalize` et
// l'assembleur d'accord par construction plutôt que par chance.
//
// Deux familles, et elles ne vivent pas au même endroit :
//   - un-vers-plusieurs (`push hl,de`, plusieurs opcodes sur une ligne) : ici,
//     parce que la source déroulée doit montrer une instruction par ligne ;
//   - un-pour-un (les orthographes ci-dessous) : ici aussi, mais l'assembleur les
//     tolère de son côté — la politique des alias reste différée.
const std::map<std::string, std::string> &canonicalSpelling() {
    static const std::map<std::string, std::string> t = {
        {"DEFB", "db"}, {"DM", "db"}, {"DEFM", "db"},
        {"DEFW", "dw"},
        {"DEFS", "ds"}, {"RMB", "ds"},
        {"ENDM", "endmacro"}, {"MEND", "endmacro"},
        {"REND", "endrepeat"},
        {"WEND", "endwhile"},
        {"ENDS", "endstruct"},
    };
    return t;
}

// La casse du mot d'origine est épousée : un source tout en majuscules qui reçoit
// un `endwhile` minuscule au milieu de ses `ORG` a l'air abîmé, et son auteur
// désactivera l'option pour cette seule raison.
inline std::string matchCase(const std::string &orig, const std::string &canonLower) {
    bool allUpper = true;
    for (char c : orig) if (std::isalpha((unsigned char)c) && std::islower((unsigned char)c)) allUpper = false;
    return allUpper ? upper(canonLower) : canonLower;
}

// Réécrit le mot de tête d'une instruction (label éventuel conservé) dans son
// orthographe canonique. Rend `stmt` inchangé si elle l'est déjà.
std::string canonicalizeSpelling(const std::string &stmt) {
    std::string label, rest; peelLabel(stmt, label, rest);
    if (rest.empty()) return stmt;
    const std::string tok = firstToken(rest);
    auto it = canonicalSpelling().find(upper(tok));
    if (it == canonicalSpelling().end()) return stmt;
    const std::string tail = restAfterFirst(rest);
    const std::string head = label.empty() ? std::string() : label + ": ";
    return head + matchCase(tok, it->second) + (tail.empty() ? "" : " " + tail);
}

// --- Blocs (ADR 0016) --------------------------------------------------------
// Un bloc a un OUVREUR, une fermeture CANONIQUE et d'éventuelles fermetures
// TOLÉRÉES, héritées de rasm et conservées en silence : elles sont omniprésentes
// et, la correspondance étant désormais vérifiée, elles ne sont plus ambiguës.
// `END` ferme le bloc ouvert le plus interne, quel qu'il soit.
//
// MODULE n'y figure pas : il bascule le module actif, il n'ouvre pas un bloc.
struct BlockKind {
    const char *kind;                  // nom du bloc dans les diagnostics
    std::vector<const char *> openers; // mots qui l'ouvrent
    std::vector<const char *> closers; // canonique en tête, puis les tolérées
};

const std::vector<BlockKind> &blockKinds() {
    static const std::vector<BlockKind> t = {
        {"IF",      {"IF", "IFDEF", "IFNDEF"}, {"ENDIF"}},
        {"REPEAT",  {"REPEAT"},                {"ENDREPEAT", "REND"}},
        {"WHILE",   {"WHILE"},                 {"ENDWHILE", "WEND"}},
        {"FOR",     {"FOR"},                   {"ENDFOR"}},
        {"MACRO",   {"MACRO"},                 {"ENDMACRO", "ENDM", "MEND"}},
        {"STRUCT",  {"STRUCT"},                {"ENDSTRUCT", "ENDS"}},
    };
    return t;
}

// Le bloc qu'ouvre ce mot-clé, ou "".
std::string blockOfOpener(const std::string &kw) {
    for (const auto &b : blockKinds())
        for (const char *o : b.openers) if (kw == o) return b.kind;
    return "";
}

// Le bloc que ferme ce mot-clé, "*" pour `END` qui ferme n'importe lequel, ou "".
std::string blockOfCloser(const std::string &kw) {
    if (kw == "END") return "*";
    for (const auto &b : blockKinds())
        for (const char *c : b.closers) if (kw == c) return b.kind;
    return "";
}

// La fermeture canonique d'un bloc, pour les diagnostics.
std::string canonicalCloser(const std::string &kind) {
    for (const auto &b : blockKinds()) if (kind == b.kind) return b.closers.front();
    return "END";
}

struct Macro {
    std::string name;
    std::vector<std::string> params;
    std::vector<SrcLine> body;
};

struct Env {
    std::map<std::string, std::string> args;   // arguments de macro (texte brut)
    std::map<std::string, int64_t> locals;      // variables de boucle (REPEAT/WHILE)
};

// Champ / définition de structure
struct StructField {
    std::string name;       // "" si anonyme
    std::string directive;  // DB/DW/DS/... ou nom d'une struct imbriquée (majuscules)
    std::string operands;   // valeurs par défaut (texte brut)
    int offset = 0;
    int size = 0;
    bool nested = false;    // directive = struct imbriquée
};
struct StructDef {
    std::string name;
    std::vector<StructField> fields;
    int size = 0;
};

// Est-ce une directive de données (taille connue) ?
inline bool isDataDir(const std::string &U) {
    return U == "DB" || U == "DEFB" || U == "DM" || U == "DEFM" ||
           U == "DW" || U == "DEFW" || U == "DS" || U == "DEFS" || U == "RMB";
}
// Nombre d'octets d'un littéral chaîne "..." (avec échappements simples).
int stringByteLen(const std::string &p) {
    if (p.size() < 2 || p[0] != '"') return 0;
    int n = 0;
    for (size_t k = 1; k < p.size(); ++k) {
        if (p[k] == '"') break;
        if (p[k] == '\\' && k + 1 < p.size()) ++k;
        ++n;
    }
    return n;
}
// Nombre de tokens séparés par des espaces.
int countTokens(const std::string &s) {
    int n = 0; size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        ++n;
    }
    return n;
}

// Mot-clé de bloc pour l'analyse d'imbrication (ou "" / mnémonique).
// ---------------------------------------------------------------------------
class PP {
public:
    PP(const FileProvider &fp, bool strict) : files(fp), strict_(strict) {}
    Result result;

    void runFile(const std::string &content, const std::string &file) {
        run(splitLines(content, file), Env{}, 0);
    }

private:
    FileProvider files;
    bool strict_ = false;
    // Un refus du mode strict porte sur une LIGNE SOURCE : dans un corps de macro
    // appelé dix fois, la ligne fautive est la même dix fois. Une ligne ne parle
    // qu'une fois, comme pour les avertissements.
    std::set<std::string> strictOnce;
    void strictErr(const SrcLine &sl, const std::string &msg) {
        if (!strictOnce.insert(sl.file + "\x01" + std::to_string(sl.line) + "\x01" + msg).second) return;
        error(sl, msg);
    }
    std::map<std::string, double> ppvars;       // variables PP globales (LET)
    // ADR 0003 : constantes EQU et variables '=' lues par le préprocesseur quand
    // leur expression y est résoluble. `asmDeferred` retient celles qui sont bien
    // définies mais dépendent d'un label — connues, mais pas ici.
    std::map<std::string, double> asmvars;
    std::set<std::string> asmDeferred;
    std::set<std::string> readAtPP;             // noms d'asmvars effectivement lus
    std::set<std::string> seenLabels;           // labels deja rencontres (pour IFDEF)
    std::map<std::string, Macro> macros;         // clé = nom majuscule
    std::map<std::string, StructDef> structs_;    // clé = nom majuscule
    long uid = 0;

    void error(const SrcLine &sl, const std::string &msg) {
        result.ok = false;
        result.errors.push_back({sl.file, sl.line, msg});
    }
    // Avertissement de bonne pratique (non bloquant, n'affecte pas result.ok).
    // Dédupliqué sur (fichier, ligne, message) : une ligne source à l'intérieur
    // d'un REPEAT déroulé ou d'une macro appelée N fois produirait sinon N
    // copies du même message — mesuré à 3,9 Mo sur une source du corpus, assez
    // pour faire déborder le tampon de l'appelant. Une ligne source ne parle
    // qu'une fois.
    std::set<std::string> warnedOnce;
    void warning(const SrcLine &sl, const std::string &msg) {
        if (!warnedOnce.insert(sl.file + "\x01" + std::to_string(sl.line) + "\x01" + msg).second) return;
        result.warnings.push_back({sl.file, sl.line, msg});
    }

    std::vector<SrcLine> splitLines(const std::string &rawContent, const std::string &file) {
        std::string content = stripBlockComments(rawContent);
        std::vector<SrcLine> out; std::string cur; int ln = 1;
        for (size_t i = 0; i <= content.size(); ++i) {
            char c = (i < content.size()) ? content[i] : '\n';
            if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                bool col0 = !cur.empty() && !std::isspace((unsigned char)cur[0]);
                out.push_back({cur, file, ln++, col0}); cur.clear(); }
            else cur += c;
        }
        if (!out.empty() && out.back().text.empty()) out.pop_back();
        return out;
    }

    // IFDEF / IFNDEF. Voit tout ce que le préprocesseur a RENCONTRÉ jusqu'ici :
    // variables LET, macros, locaux et arguments, mais aussi les constantes et
    // variables d'assemblage (ADR 0003) et les labels déjà définis — « FOO » seul
    // sur une ligne est un drapeau, idiome rasm courant.
    //
    // La limite est celle de la frontière de phase : un symbole défini PLUS BAS
    // reste invisible, le préprocesseur n'ayant qu'une passe avant. Ce n'est pas
    // un manque, c'est ce qu'un préprocesseur peut savoir.
    bool isDefined(const std::string &name, const Env &env) {
        return ppvars.count(name) || macros.count(upper(name)) ||
               env.locals.count(name) || env.args.count(name) ||
               asmvars.count(name) || asmDeferred.count(name) ||
               seenLabels.count(name);
    }

    expr::Result evalPP(const std::string &text, const Env &env) {
        std::string deferred;
        auto resolver = [&](const std::string &name, double &out) -> bool {
            auto l = env.locals.find(name); if (l != env.locals.end()) { out = (double)l->second; return true; }
            auto p = ppvars.find(name); if (p != ppvars.end()) { out = p->second; return true; }
            auto a = env.args.find(name);
            if (a != env.args.end()) { auto r = expr::eval(a->second, {}); if (r.ok) { out = r.real; return true; } }
            auto v = asmvars.find(name);
            if (v != asmvars.end()) { readAtPP.insert(name); out = v->second; return true; }
            if (asmDeferred.count(name)) deferred = name;
            return false;
        };
        auto r = expr::eval(text, resolver);
        // Frontière de l'ADR 0005/0003 : le préprocesseur s'exécute avant qu'aucune
        // adresse existe. Un nom qui dépend d'un label n'est pas « inconnu » — il est
        // connu et pas encore calculable. Le dire, plutôt que laisser un
        // « unknown symbol » trompeur.
        if (!r.ok && !deferred.empty())
            r.error = "'" + deferred + "' is defined at assembly time and cannot be resolved here "
                      "(it depends on a label or on the current address); the preprocessor runs "
                      "before any address exists";
        return r;
    }

    // Enregistre "nom EQU expr" / "nom = expr" SANS consommer la ligne : c'est
    // l'assembleur qui définit réellement le symbole, le préprocesseur ne fait
    // que le lire au passage.
    void noteAsmDefinition(const std::string &name, const std::string &ev,
                           const Env &env, const SrcLine &raw) {
        if (!isIdentifier(name)) return;
        // Ambiguïté réelle : le PP a déjà utilisé cette valeur plus haut et elle
        // change ici. L'assembleur, lui, résout les EQU/= jusqu'à point fixe et
        // retiendra la dernière — les deux étages ne verraient pas la même chose.
        if (readAtPP.count(name)) {
            auto prev = asmvars.find(name);
            warning(raw, "'" + name + "' was already used by the preprocessor" +
                         (prev != asmvars.end() ? " with value " + fmtNum(prev->second) : "") +
                         "; redefining it here makes the preprocessor and the assembler disagree");
            readAtPP.erase(name);
        }
        auto r = evalPP(ev, env);
        if (r.ok) { asmvars[name] = r.real; asmDeferred.erase(name); }
        else { asmvars.erase(name); asmDeferred.insert(name); }
    }

    // Substitution {name} / {=expr} / {expr}
    std::string substitute(const std::string &text, const Env &env, const SrcLine &sl) {
        std::string out; size_t i = 0;
        while (i < text.size()) {
            if (text[i] == '{') {
                size_t j = i + 1; int d = 1;
                while (j < text.size() && d) { if (text[j] == '{') ++d; else if (text[j] == '}') --d; if (d) ++j; }
                if (j >= text.size()) { error(sl, "unclosed brace '{'"); out += text.substr(i); break; }
                std::string inner = trim(text.substr(i + 1, j - i - 1));
                // rasm surcharge les accolades : « {hex}valeur » y est un format
                // d'affichage et « {sizeof}type » un opérateur. Dans fantams, {X}
                // a un seul rôle — évaluer X et substituer. Ces sources doivent
                // être éditées, autant le dire précisément. Cf. ADR 0011.
                static const std::map<std::string, std::string> rasmBrace = {
                    {"SIZEOF", "write sizeof(name) instead"},
                    {"HEX", "use the print format prefix: print \"x=\", hex expr"},
                    {"BIN", "use the print format prefix: print \"x=\", bin expr"},
                    {"CHAR", "use the print format prefix: print \"x=\", char expr"},
                    {"INT", "use the print format prefix: print \"x=\", int expr"},
                };
                auto rb = rasmBrace.find(upper(inner));
                if (rb != rasmBrace.end()) {
                    error(sl, "'{" + inner + "}' is a rasm notation that fantams does not accept: "
                              "here '{X}' only ever means 'evaluate X and substitute' — " + rb->second);
                    i = j + 1;
                    continue;
                }
                if (inner.empty()) error(sl, "empty substitution '{}'");
                else if (inner[0] == '=') {
                    auto r = evalPP(trim(inner.substr(1)), env);
                    if (!r.ok) error(sl, r.error); else out += fmtNum(r.real);
                } else if (isIdentifier(inner) && env.args.count(inner)) {
                    out += env.args.at(inner);
                } else {
                    auto r = evalPP(inner, env);
                    if (!r.ok) error(sl, r.error); else out += fmtNum(r.real);
                }
                i = j + 1;
            } else out += text[i++];
        }
        return out;
    }

    // Registres, paires et conditions Z80 : dans une VRAIE instruction, un opérande
    // qui vaut exactement l'un de ces noms est un registre, pas un symbole — on ne le
    // substitue donc jamais (sinon "ld a,b" serait détruit dès qu'un compteur de boucle
    // s'appelle 'b'). Dans une sous-expression ("ld a,(tbl+i)") ou après une directive
    // ("db i"), c'est un symbole. La liste vit dans keywords.h (ADR 0015).
    //
    // GARDE-FOU INATTEIGNABLE, conservé délibérément. `substituteVars` ne consulte que
    // `env.locals` (index de REPEAT) et `ppvars` (LET), et l'ADR 0015 refuse désormais
    // un nom de registre dans ces deux positions : aucun utilisateur ne peut plus
    // fabriquer une entrée nommée 'b'. Aucun test ne peut donc atteindre ce test — il
    // reste là pour qu'une construction future qui alimenterait ces tables sans passer
    // par la validation ne détruise pas silencieusement « ld a,b ».

    // Index du 1er caractère des opérandes dans une ligne déjà trim : saute le label
    // éventuel puis le mnémonique/directive (ni l'un ni l'autre n'est substituable).
    size_t operandStart(const std::string &code) {
        size_t p = 0, q = 0;
        while (q < code.size() && isIdentChar(code[q])) ++q;
        if (q > 0) {
            size_t r = q; while (r < code.size() && std::isspace((unsigned char)code[r])) ++r;
            std::string tok = upper(code.substr(0, q));
            if (r < code.size() && code[r] == ':') p = r + 1;
            else if (!isReservedWord(tok) && !macros.count(tok)) p = q;
        }
        while (p < code.size() && std::isspace((unsigned char)code[p])) ++p;
        while (p < code.size() && isIdentChar(code[p])) ++p; // mnémonique / directive
        return p;
    }

    // Remplace, dans les opérandes d'une ligne prête à être émise, les variables du
    // préprocesseur (LET) et les compteurs de boucle (REPEAT/WHILE) écrits en clair
    // par leur valeur. rasm les expose comme des symboles ordinaires de l'assembleur ;
    // ici elles n'existent qu'au préprocesseur, d'où cette substitution textuelle.
    // Chaînes et littéraux caractère sont recopiés tels quels, et un identifiant collé
    // à un préfixe numérique (#FF, $1A, %10, 0x1F) n'est pas un symbole.
    std::string substituteVars(const std::string &code, const Env &env) {
        if (env.locals.empty() && ppvars.empty()) return code;
        size_t op = operandStart(code);
        std::string mnemo = upper(trim(code.substr(0, op)));
        size_t sp = mnemo.rfind(' ');
        if (sp != std::string::npos) mnemo = mnemo.substr(sp + 1);
        bool instr = z80::mnemoFromString(mnemo) != z80::Mnemo::Invalid;

        const std::string text = code.substr(op);
        std::string out = code.substr(0, op);
        const size_t base = out.size();
        size_t i = 0;
        while (i < text.size()) {
            char c = text[i];
            if (c == '"' || c == '\'') {
                char q = c; out += text[i++];
                while (i < text.size()) {
                    if (text[i] == '\\' && i + 1 < text.size()) { out += text[i]; out += text[i + 1]; i += 2; continue; }
                    out += text[i];
                    if (text[i++] == q) break;
                }
                continue;
            }
            char prev = out.size() > base ? out.back() : 0;
            bool numPrefix = (prev == '#' || prev == '$' || prev == '%' ||
                              std::isalnum((unsigned char)prev) || prev == '_');
            // dernier caractère significatif (espaces ignorés) pour le test "opérande entier"
            char prevSig = 0;
            for (size_t b = out.size(); b > base; --b)
                if (!std::isspace((unsigned char)out[b - 1])) { prevSig = out[b - 1]; break; }
            if (!numPrefix && (std::isalpha((unsigned char)c) || c == '_')) {
                size_t j = i;
                while (j < text.size() && isIdentChar(text[j])) ++j;
                std::string name = text.substr(i, j - i);
                auto l = env.locals.find(name);
                bool has = false; double val = 0;
                if (l != env.locals.end()) { val = (double)l->second; has = true; }
                else { auto p = ppvars.find(name); if (p != ppvars.end()) { val = p->second; has = true; } }
                // opérande entier d'une instruction = registre/condition, jamais un symbole
                bool whole = false;
                if (instr && kw::isMachineWord(upper(name))) {
                    size_t k = j; while (k < text.size() && std::isspace((unsigned char)text[k])) ++k;
                    char next = k < text.size() ? text[k] : 0;
                    whole = (prevSig == 0 || prevSig == ',' || prevSig == '(') &&
                            (next == 0 || next == ',' || next == ')');
                }
                if (has && !whole) out += fmtNum(val);
                else out += name;
                i = j;
                continue;
            }
            out += c; ++i;
        }
        return out;
    }

    // Classe une ligne brute selon son mot-clé de bloc.
    std::string classify(const std::string &raw) {
        std::string code = trim(stripComment(raw));
        if (code.empty()) return "";
        std::string label, rest; peelLabel(code, label, rest);
        if (rest.empty()) return "";
        std::string t1 = firstToken(restAfterFirst(rest));
        if (upper(t1) == "MACRO") return "MACRO";
        std::string k = upper(firstToken(rest));
        // STRUCT : bloc seulement en DÉCLARATION (1 seul argument) ;
        // "STRUCT type instance" (2+ args) est une instanciation, pas un bloc.
        if (k == "STRUCT" && countTokens(restAfterFirst(rest)) != 1) return "STRUCTINS";
        return k;
    }

    // Remplace sizeof(NOM) par la taille declaree de la structure NOM.
    // C'est le preprocesseur qui porte cette connaissance : apres l'abaissement
    // des STRUCT, il n'y a plus de structure, seulement des EQU.
    //
    // On n'implemente PAS la notation rasm {sizeof}NOM : dans fantams, {X}
    // signifie « evalue X et substitue », un seul role grammatical. Cf. ADR 0011.
    std::string expandSizeof(const std::string &code, const SrcLine &src) {
        std::string out; size_t i = 0;
        while (i < code.size()) {
            bool boundary = (i == 0) || !isIdentChar(code[i - 1]);
            if (boundary && upper(code.substr(i, 6)) == "SIZEOF") {
                size_t j = i + 6;
                while (j < code.size() && std::isspace((unsigned char)code[j])) ++j;
                if (j < code.size() && code[j] == '(') {
                    size_t k = code.find(')', j);
                    if (k != std::string::npos) {
                        std::string arg = trim(code.substr(j + 1, k - j - 1));
                        auto it = structs_.find(upper(arg));
                        if (it == structs_.end())
                            error(src, "sizeof: unknown struct '" + arg + "'");
                        else {
                            out += std::to_string(it->second.size);
                            i = k + 1;
                            continue;
                        }
                    }
                }
            }
            out += code[i++];
        }
        return out;
    }

    void emit(const std::string &code, const SrcLine &src) {
        std::string t = trim(expandSizeof(code, src));
        if (t.empty()) return;
        // ':' -> retour à la ligne ; push/pop multi-registres -> une instruction chacun.
        // Les sous-lignes après la 1re sont indentées (jamais lues comme un label).
        // Quatre espaces, comme le beautify : la source déroulée est mise en
        // forme par définition, et une tabulation y serait une indentation dont
        // la largeur dépend du lecteur (ADR 0013).
        bool first = true;
        std::vector<std::string> glued;
        const std::vector<std::string> stmts = splitStatements(t, &glued);
        if (strict_ && stmts.size() > 1)
            strictErr(src, "strict: several instructions on one line is a writing facility, "
                           "not canonical Z80 — write one instruction per line");
        for (const auto &stmt : stmts) {
            const std::string canon = canonicalizeSpelling(stmt);
            if (strict_ && canon != stmt) {
                // Le mot fautif est la directive, pas le label éventuel qui la précède.
                std::string l0, r0, l1, r1;
                peelLabel(stmt, l0, r0);
                peelLabel(canon, l1, r1);
                strictErr(src, "strict: '" + firstToken(r0) + "' is a non-canonical spelling — write '" +
                               firstToken(r1) + "'");
            }
            const std::vector<std::string> expanded = expandPushPop(canon);
            if (strict_ && expanded.size() > 1)
                strictErr(src, "strict: a multi-register '" + firstToken(canon) +
                               "' is a writing facility, not canonical Z80 — write one per line");
            for (const auto &line : expanded) {
                result.lines.push_back({first ? line : "    " + line, src.file, src.line, first && src.col0});
                first = false;
            }
        }
        // ADR 0015 : un mot réservé ne nomme pas un label. Ici le ':' n'a donc pas
        // pu être celui d'un label — il sépare deux instructions, ce qui est licite
        // (« ei: ret ») mais se lit mal. Le dire, plutôt que parler de style seul.
        for (const auto &ident : glued)
            warning(src, "'" + ident + ":' is read as two statements, not as a label — '" + ident +
                         "' is a reserved word and cannot name a label; write '" + ident +
                         " : ...' if that is what you meant");
    }

    // Collecte les labels définis (par "ident:") dans un corps, hors @@export.
    // skipNestedModule=true : ignore les labels des blocs MODULE imbriqués
    // (ils seront préfixés par le MODULE interne lors de la récursion).
    // onlyAtPrefixed=true : ne retient que les labels préfixés par '@' — c'est la
    // convention rasm pour l'auto-unicité par expansion (MACRO/REPEAT/WHILE) ; un
    // label ordinaire réutilisé entre deux expansions doit rester une vraie collision
    // ("symbole déjà défini"), comme chez rasm. MODULE, lui, renomme tout (onlyAtPrefixed=false).
    std::vector<std::string> collectLabels(const std::vector<SrcLine> &body, bool skipNestedModule,
                                           bool onlyAtPrefixed = false) {
        std::vector<std::string> locals;
        std::map<std::string, bool> exported;
        for (const auto &l : body) {
            std::string code = trim(stripComment(l.text));
            if (upper(firstToken(code)) == "@@EXPORT")
                for (const auto &n : splitTopLevel(restAfterFirst(code), ' '))
                    if (!n.empty()) exported[n] = true;
        }
        int moduleDepth = 0;
        for (const auto &l : body) {
            std::string kw = classify(l.text);
            if (skipNestedModule) {
                if (kw == "MODULE") { ++moduleDepth; continue; }
                if (kw == "ENDMODULE") { --moduleDepth; continue; }
                if (moduleDepth > 0) continue;
            }
            std::string code = trim(stripComment(l.text));
            std::string label, rest; peelLabel(code, label, rest, [&](const std::string &n) { return macros.count(n) != 0; });
            if (!label.empty() && !exported.count(label) && (!onlyAtPrefixed || label[0] == '@')) {
                bool seen = false; for (auto &x : locals) if (x == label) seen = true;
                if (!seen) locals.push_back(label);
            }
        }
        return locals;
    }
    // Applique un renommage (def+réfs) aux labels donnés, retire les @@export.
    std::vector<SrcLine> renameScope(const std::vector<SrcLine> &body,
                                     const std::vector<std::string> &names,
                                     const std::function<std::string(const std::string &)> &mangle) {
        std::vector<SrcLine> out;
        for (const auto &l : body) {
            if (upper(firstToken(trim(stripComment(l.text)))) == "@@EXPORT") continue;
            std::string t = l.text;
            for (const auto &name : names) t = replaceWord(t, name, mangle(name));
            out.push_back({t, l.file, l.line, l.col0});
        }
        return out;
    }
    // Renommage auto-local (macros, itérations REPEAT/WHILE) : suffixe __id.
    std::vector<SrcLine> renameLocals(const std::vector<SrcLine> &body, long id) {
        auto names = collectLabels(body, false, /*onlyAtPrefixed=*/true);
        std::string suf = "__" + std::to_string(id);
        return renameScope(body, names, [&](const std::string &n) { return n + suf; });
    }

    // Trouve la ligne de fermeture correspondante (ADR 0016).
    //
    // La profondeur est comptée sur l'UNION des ouvreurs, pas sur une seule paire :
    // c'est la condition pour que `end` puisse fermer le bloc le plus interne sans
    // qu'un `end` imbriqué soit pris pour celui du bloc englobant. Et puisque la pile
    // dit désormais quel bloc est ouvert, une fermeture nommée qui ne correspond pas
    // est une ERREUR qui le dit — là où l'ancienne version rendait -1 et laissait
    // l'appelant annoncer « REPEAT without REND », diagnostic trompeur.
    //
    // MODULE est délibérément absent de la table : « MODULE x » bascule le module
    // actif au lieu d'ouvrir un bloc, si bien que deux MODULE pour un ENDMODULE est
    // la forme normale — le compter déséquilibrerait la pile.
    int findMatching(const std::vector<SrcLine> &lines, int start, const std::string &block) {
        std::vector<std::string> stack{block};
        for (int i = start + 1; i < (int)lines.size(); ++i) {
            const std::string kw = classify(lines[i].text);
            const std::string op = blockOfOpener(kw);
            if (!op.empty()) { stack.push_back(op); continue; }
            const std::string cl = blockOfCloser(kw);
            if (cl.empty()) continue;
            if (cl != "*" && cl != stack.back()) {
                error(lines[i], "'" + kw + "' closes a " + cl + " block, but the open block here is a " +
                                stack.back() + " — write '" + canonicalCloser(stack.back()) + "' or 'end'");
                return -2;   // déjà diagnostiqué : l'appelant n'ajoute rien
            }
            stack.pop_back();
            if (stack.empty()) return i;
        }
        return -1;
    }

    void expandMacro(const Macro &m, const std::string &argstr,
                     const SrcLine &sl, int depth) {
        if (depth > 200) { error(sl, "macro nesting too deep (recursive?)"); return; }
        std::vector<std::string> args = splitTopLevel(argstr, ',');
        if (args.size() != m.params.size()) {
            error(sl, "macro '" + m.name + "': expected " + std::to_string(m.params.size()) +
                          " argument(s), got " + std::to_string(args.size()));
            return;
        }
        Env ne;
        for (size_t k = 0; k < m.params.size(); ++k) ne.args[m.params[k]] = args[k];
        std::vector<SrcLine> scoped = renameLocals(m.body, ++uid);
        run(scoped, ne, depth + 1);
    }

    // Analyse un champ de structure : "[nom] directive operandes" ou "nom StructType".
    StructField parseField(const std::string &code) {
        StructField f;
        std::string label, rest; peelLabel(code, label, rest);
        if (!label.empty()) {
            f.name = label; f.directive = upper(firstToken(rest)); f.operands = restAfterFirst(rest);
        } else {
            std::string w0 = firstToken(rest);
            std::string U1 = upper(firstToken(restAfterFirst(rest)));
            if (isDataDir(U1) || structs_.count(U1)) {           // "nom directive ops" sans ':'
                f.name = w0; f.directive = U1; f.operands = restAfterFirst(restAfterFirst(rest));
            } else {                                              // champ anonyme "directive ops"
                f.directive = upper(w0); f.operands = restAfterFirst(rest);
            }
        }
        return f;
    }

    void defineStruct(const std::string &sname, const std::vector<SrcLine> &body,
                      const Env &env, const SrcLine &raw) {
        if (structs_.count(upper(sname))) { error(raw, "struct redefined: '" + sname + "'"); return; }
        StructDef def; def.name = sname; int off = 0;
        for (const auto &l : body) {
            std::string code = trim(substitute(stripComment(l.text), env, l));
            if (code.empty()) continue;
            StructField f = parseField(code);
            const std::string &U = f.directive;
            if (isDataDir(U)) {
                auto ops = splitTopLevel(f.operands, ',');
                if (U == "DB" || U == "DEFB" || U == "DM" || U == "DEFM") {
                    int n = 0; for (auto &p : ops) { if (!p.empty() && p[0] == '"') n += stringByteLen(p); else if (!p.empty()) n += 1; }
                    f.size = n;
                } else if (U == "DW" || U == "DEFW") {
                    int n = 0; for (auto &p : ops) if (!p.empty()) ++n;
                    f.size = 2 * n;
                } else { // DS / DEFS / RMB
                    auto r = ops.empty() ? expr::Result{} : evalPP(ops[0], env);
                    if (!r.ok) { error(l, "struct: DS field size not resolvable"); f.size = 0; }
                    else f.size = (int)r.value;
                }
            } else if (structs_.count(U)) {
                f.nested = true; f.size = structs_[U].size;
            } else {
                error(l, "struct: unknown field directive '" + f.directive + "'");
                continue;
            }
            f.offset = off; off += f.size;
            def.fields.push_back(f);
        }
        def.size = off;
        structs_[upper(sname)] = def;
        // émission des symboles portables (offsets + sizeof)
        for (const auto &f : def.fields) {
            if (f.name.empty()) continue;
            emit(sname + "." + f.name + " EQU " + std::to_string(f.offset), raw);
            if (f.nested) {
                const StructDef &inner = structs_[f.directive];
                for (const auto &g : inner.fields)
                    if (!g.name.empty())
                        emit(sname + "." + f.name + "." + g.name + " EQU " + std::to_string(f.offset + g.offset), raw);
            }
        }
        emit(sname + " EQU " + std::to_string(def.size), raw);
    }

    void instantiateStruct(const std::string &rest, const SrcLine &raw) {
        std::string aft = restAfterFirst(rest);        // "type instance ov..."
        std::string type = firstToken(aft);
        std::string aft2 = restAfterFirst(aft);         // "instance ov..."
        std::string instance = firstToken(aft2);
        std::string overText = restAfterFirst(aft2);
        auto it = structs_.find(upper(type));
        if (it == structs_.end()) { error(raw, "unknown struct to instantiate: '" + type + "'"); return; }
        if (instance.empty()) { error(raw, "struct instantiation without a name"); return; }
        const StructDef &d = it->second;
        auto overrides = splitTopLevel(overText, ',');
        emit(instance + ":", raw);
        for (const auto &f : d.fields) {
            if (f.name.empty()) continue;
            emit(instance + "." + f.name + " EQU " + instance + "+" + std::to_string(f.offset), raw);
            if (f.nested) {
                const StructDef &inner = structs_[f.directive];
                for (const auto &g : inner.fields)
                    if (!g.name.empty())
                        emit(instance + "." + f.name + "." + g.name + " EQU " + instance + "+" +
                             std::to_string(f.offset + g.offset), raw);
            }
        }
        for (size_t fi = 0; fi < d.fields.size(); ++fi) {
            const StructField &f = d.fields[fi];
            if (f.nested) { emit("ds " + std::to_string(f.size), raw); continue; }
            std::string val = f.operands;
            bool isDS = (f.directive == "DS" || f.directive == "DEFS" || f.directive == "RMB");
            if (!isDS && fi < overrides.size() && !overrides[fi].empty()) val = overrides[fi];
            emit(f.directive + (val.empty() ? "" : " " + val), raw);
        }
    }

    void run(const std::vector<SrcLine> &lines, const Env &env, int depth) {
        int i = 0;
        while (i < (int)lines.size()) {
            const SrcLine &raw = lines[i];
            std::string sub = substitute(stripComment(raw.text), env, raw);
            std::string code = trim(sub);
            if (code.empty()) { ++i; continue; }

            std::string label, rest; peelLabel(code, label, rest, [&](const std::string &n) { return macros.count(n) != 0; });
            if (!label.empty()) seenLabels.insert(label);
            std::string kw = upper(firstToken(rest));
            std::string secondUp = upper(firstToken(restAfterFirst(rest)));

            // --- INCLUDE ---
            if (kw == "INCLUDE") {
                if (!label.empty()) emit(label + ":", raw);
                auto parts = splitTopLevel(restAfterFirst(rest), ',');
                std::string path = parts.empty() ? "" : parts[0];
                // Lecteur partagé (ADR 0010) : un chemin entre quotes suit la même
                // règle qu'une chaîne de « db » — sans quoi « include 'a"b.asm' »
                // se lirait ici autrement qu'ailleurs.
                const kw::Literal lit = kw::readLiteral(path, 0);
                if (lit.present && lit.error.empty() && trim(path.substr(lit.end)).empty())
                    path = lit.bytes;
                std::string content;
                if (!files || !files(path, content)) error(raw, "include not found: '" + path + "'");
                else run(splitLines(content, path), env, depth + 1);
                ++i; continue;
            }

            // --- LET (variable PP) ---
            if (kw == "LET") {
                std::string a = restAfterFirst(rest); // "name = expr", "name=expr" ou "name expr"
                std::string name, ev;
                size_t eq = a.find('=');
                if (eq != std::string::npos) { name = trim(a.substr(0, eq)); ev = trim(a.substr(eq + 1)); }
                else { name = firstToken(a); ev = restAfterFirst(a); }
                auto r = evalPP(ev, env);
                std::string badLet = isIdentifier(name) ? kw::reservedName(name, "a symbol")
                                                         : std::string();
                if (!isIdentifier(name)) error(raw, "LET: invalid variable name");
                else if (!badLet.empty()) error(raw, badLet);
                else if (!r.ok) error(raw, r.error);
                else ppvars[name] = r.real;
                ++i; continue;
            }

            // --- IF / IFDEF / IFNDEF ---
            if (kw == "IF" || kw == "IFDEF" || kw == "IFNDEF") {
                int endif = findMatching(lines, i, "IF");
                if (endif < 0) { if (endif == -1) error(raw, "IF without ENDIF or end"); return; }
                // bornes des branches (profondeur 0)
                // Profondeur comptée sur l'UNION des ouvreurs (ADR 0016) : un bloc
                // imbriqué fermé par `end` ferait sinon perdre le ELSE de niveau 1.
                std::vector<int> bounds = {i};
                int d = 1;
                for (int j = i + 1; j < endif; ++j) {
                    std::string k = classify(lines[j].text);
                    if (!blockOfOpener(k).empty()) ++d;
                    else if (!blockOfCloser(k).empty()) --d;
                    else if (d == 1 && (k == "ELSE" || k == "ELSEIF")) bounds.push_back(j);
                }
                bounds.push_back(endif);
                bool taken = false;
                for (size_t b = 0; b + 1 < bounds.size() && !taken; ++b) {
                    const SrcLine &hdr = lines[bounds[b]];
                    std::string hl, hr; peelLabel(trim(stripComment(hdr.text)), hl, hr);
                    std::string hkw = upper(firstToken(hr));
                    std::string operand = substitute(restAfterFirst(hr), env, hdr);
                    bool cond;
                    if (hkw == "ELSE") cond = true;
                    else if (hkw == "IFDEF") cond = isDefined(trim(operand), env);
                    else if (hkw == "IFNDEF") cond = !isDefined(trim(operand), env);
                    else { auto r = evalPP(operand, env); if (!r.ok) { error(hdr, r.error); cond = false; } else cond = (r.value != 0); }
                    if (cond) {
                        std::vector<SrcLine> branch(lines.begin() + bounds[b] + 1, lines.begin() + bounds[b + 1]);
                        run(branch, env, depth);
                        taken = true;
                    }
                }
                i = endif + 1; continue;
            }

            // --- REPEAT count[,var] ... REND ---
            if (kw == "REPEAT") {
                int rend = findMatching(lines, i, "REPEAT");
                if (rend < 0) { if (rend == -1) error(raw, "REPEAT without ENDREPEAT or end"); return; }
                if (!label.empty()) emit(label + ":", raw);
                auto parts = splitTopLevel(restAfterFirst(rest), ',');
                auto r = parts.empty() ? expr::Result{} : evalPP(parts[0], env);
                std::string var = parts.size() > 1 ? trim(parts[1]) : "";
                if (!var.empty()) {
                    std::string bad = kw::reservedName(var, "a loop index");
                    if (!bad.empty()) { error(raw, bad); var.clear(); }
                }
                // ADR 0016 : l'index vaut 0 au premier tour, contre 1 chez rasm. C'est
                // la seule divergence du projet qu'aucune détection ne peut trouver — une
                // table décalée d'un cran s'assemble parfaitement. Elle est donc rendue
                // bruyante autrement : TOUT usage de la forme à index est signalé, sans
                // faux positif possible puisque c'est la construction qu'on déprécie.
                if (!var.empty())
                    warning(raw, "the index of 'repeat' starts at 0 here, not at 1 as in rasm: "
                                 "a source written for rasm must read '" + var + "+1' — prefer "
                                 "'for " + var + " = 0 until <count>', where the bounds are written");
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + rend);
                if (!r.ok) error(raw, "REPEAT: " + (parts.empty() ? "missing counter" : r.error));
                else if (r.value < 0 || r.value > 1000000) error(raw, "REPEAT: counter out of range");
                else for (int k = 0; k < r.value; ++k) {
                    Env ne = env; if (!var.empty()) ne.locals[var] = k;
                    run(renameLocals(body, ++uid), ne, depth);
                }
                i = rend + 1; continue;
            }

            // --- FOR var = lo TO hi | UNTIL hi ... ENDFOR (ADR 0016) ---
            // `to` inclut la borne haute, `until` l'exclut : aucune borne ne se devine,
            // et « until n » fait exactement n tours, ce qui en fait le remplacement
            // direct de « repeat n,var ».
            if (kw == "FOR") {
                int endfor = findMatching(lines, i, "FOR");
                if (endfor < 0) { if (endfor == -1) error(raw, "FOR without ENDFOR or end"); return; }
                if (!label.empty()) emit(label + ":", raw);
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + endfor);
                std::string head = restAfterFirst(rest);
                size_t eq = findAssign(head);
                if (eq == std::string::npos) {
                    error(raw, "FOR: expected 'for <var> = <low> to <high>' (or 'until <high>')");
                    i = endfor + 1; continue;
                }
                std::string var = trim(head.substr(0, eq));
                std::string range = trim(head.substr(eq + 1));
                bool inclusive = true;
                size_t kwPos = findRangeKeyword(range, inclusive);
                if (kwPos == std::string::npos) {
                    error(raw, "FOR: missing 'to' or 'until' — write 'for " + var +
                               " = 0 until <count>' (high bound excluded) or 'to <last>' (included)");
                    i = endfor + 1; continue;
                }
                std::string badVar = !isIdentifier(var) ? "FOR: invalid loop index name"
                                                        : kw::reservedName(var, "a loop index");
                auto lo = evalPP(trim(range.substr(0, kwPos)), env);
                auto hi = evalPP(trim(range.substr(kwPos + (inclusive ? 2 : 5))), env);
                if (!badVar.empty()) error(raw, badVar);
                else if (!lo.ok) error(raw, "FOR: " + lo.error);
                else if (!hi.ok) error(raw, "FOR: " + hi.error);
                else if (hi.value - lo.value > 1000000) error(raw, "FOR: range out of range");
                else {
                    const long last = inclusive ? hi.value : hi.value - 1;
                    for (long k = lo.value; k <= last; ++k) {
                        Env ne = env; ne.locals[var] = k;
                        run(renameLocals(body, ++uid), ne, depth);
                    }
                }
                i = endfor + 1; continue;
            }

            // --- WHILE expr ... WEND ---
            if (kw == "WHILE") {
                int wend = findMatching(lines, i, "WHILE");
                if (wend < 0) { if (wend == -1) error(raw, "WHILE without ENDWHILE or end"); return; }
                if (!label.empty()) emit(label + ":", raw);
                std::string condRaw = restAfterFirst(rest);
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + wend);
                long guard = 0;
                for (;;) {
                    auto r = evalPP(substitute(condRaw, env, raw), env);
                    if (!r.ok) { error(raw, "WHILE: " + r.error); break; }
                    if (r.value == 0) break;
                    if (++guard > 1000000) { error(raw, "WHILE: too many iterations"); break; }
                    run(renameLocals(body, ++uid), env, depth);
                }
                i = wend + 1; continue;
            }

            // --- définition de macro : "MACRO name p.." ou "name MACRO p.." ---
            if (kw == "MACRO" || secondUp == "MACRO") {
                int endm = findMatching(lines, i, "MACRO");
                if (endm < 0) { if (endm == -1) error(raw, "MACRO without ENDMACRO or end"); return; }
                Macro m;
                std::string decl;
                // "macro foo:" — le ':' de fin fait partie du style courant, il
                // n'appartient pas au nom. Sans ce retrait, la macro s'enregistre
                // sous "foo:" et l'appel nu ne la trouve jamais.
                if (kw == "MACRO") { m.name = firstToken(restAfterFirst(rest)); decl = restAfterFirst(restAfterFirst(rest)); }
                else { m.name = firstToken(rest); decl = restAfterFirst(restAfterFirst(rest)); }
                for (auto &p : splitTopLevel(decl, ',')) {
                    if (p.empty()) continue;
                    // Le paramètre refusé est CONSERVÉ : l'écarter changerait l'arité,
                    // et chaque appel produirait une seconde erreur qui ne parle de rien.
                    std::string bad = kw::reservedName(p, "a macro parameter");
                    if (!bad.empty()) error(raw, bad);
                    m.params.push_back(p);
                }
                for (int j = i + 1; j < endm; ++j) m.body.push_back(lines[j]);
                if (!m.name.empty() && m.name.back() == ':') m.name.pop_back();
                if (m.name.empty()) error(raw, "MACRO without a name");
                else macros[upper(m.name)] = m;
                i = endm + 1; continue;
            }

            // --- MODULE name | MODULE [OFF] | ENDMODULE (scope par préfixe) ---
            // rasm : PAS de nesting. "MODULE x" bascule le module actif (remplace, ne cumule
            // pas) ; "MODULE", "MODULE OFF" et "ENDMODULE" désactivent le module en cours.
            if (kw == "MODULE" || kw == "ENDMODULE") {
                std::string arg = trim(restAfterFirst(rest));
                if (kw == "ENDMODULE" || arg.empty() || upper(arg) == "OFF") { ++i; continue; }
                std::string mname = firstToken(arg);
                // fin de CE module : prochaine ligne MODULE/ENDMODULE (qu'elle ferme ou ouvre
                // un autre module), sinon fin du bloc courant.
                int endIdx = (int)lines.size();
                for (int j = i + 1; j < endIdx; ++j) {
                    std::string kw2 = classify(lines[j].text);
                    if (kw2 == "MODULE" || kw2 == "ENDMODULE") { endIdx = j; break; }
                }
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + endIdx);
                // Séparateur '.' (pas '_' comme rasm) : cohérent avec le mécanisme des labels
                // locaux ".nom" (asm.cpp), qui qualifie déjà par le label global précédent —
                // ici ce "global précédent" devient le nom renommé "module.label", donnant
                // naturellement "module.label.local" sans traitement spécial. Les labels
                // locaux (".nom") ne sont donc PAS renommés ici : asm.cpp s'en charge lui-même.
                std::string prefix = mname + ".";
                auto names = collectLabels(body, /*skipNestedModule=*/true);
                names.erase(std::remove_if(names.begin(), names.end(),
                            [](const std::string &n) { return !n.empty() && n[0] == '.'; }), names.end());
                auto scoped = renameScope(body, names, [&](const std::string &n) { return prefix + n; });
                run(scoped, env, depth);
                i = endIdx; continue; // ne consomme pas la ligne de fin : rejouée (OFF/ENDMODULE ou MODULE suivant)
            }

            // --- STRUCT : déclaration (1 arg) ou instanciation (2+ args) ---
            if (kw == "STRUCT") {
                if (countTokens(restAfterFirst(rest)) == 1) {
                    int ends = findMatching(lines, i, "STRUCT");
                    if (ends < 0) { if (ends == -1) error(raw, "STRUCT without ENDSTRUCT or end"); return; }
                    std::string sname = firstToken(restAfterFirst(rest));
                    std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + ends);
                    defineStruct(sname, body, env, raw);
                    i = ends + 1; continue;
                }
                instantiateStruct(rest, raw);
                ++i; continue;
            }

            // --- fermetures orphelines (déjà consommées par les blocs) ---
            // (ENDMODULE est intercepté plus haut avec MODULE, jamais atteint ici.)
            if (kw == "ENDSTRUCT" || kw == "ENDS") { ++i; continue; }

            // --- @@export hors macro : ignorer ---
            if (kw == "@@EXPORT") { ++i; continue; }

            // --- appel de macro ---
            auto mit = macros.find(upper(firstToken(rest)));
            if (mit != macros.end()) {
                if (!label.empty()) emit(label + ":", raw);
                // Les arguments sont evalues dans la portee de l'APPELANT : sans ce
                // substituteVars, un compteur de REPEAT passe a une macro
                // ("repeat 3,k / poke k*2") traversait l'expansion tel quel et
                // echouait plus tard en "unknown symbol 'k'". Le nom de la macro
                // n'etant pas un mnemonique Z80, la protection des noms de
                // registres ne s'applique pas ici — c'est le comportement voulu.
                expandMacro(mit->second, restAfterFirst(substituteVars(rest, env)), raw, depth);
                ++i; continue;
            }

            // --- constante EQU / variable '=' : observée, pas consommée ---
            if (!label.empty()) {
                if (kw == "EQU") noteAsmDefinition(label, restAfterFirst(rest), env, raw);
                else {
                    size_t eq = findAssign(rest);
                    if (eq != std::string::npos && trim(rest.substr(0, eq)).empty())
                        noteAsmDefinition(label, trim(rest.substr(eq + 1)), env, raw);
                }
            }

            // --- ligne ordinaire : passe-plat ---
            emit(substituteVars(code, env), raw);
            ++i;
        }
    }
};

} // namespace

std::string Result::dump() const {
    std::string s;
    for (const auto &l : lines) { s += l.text; s += '\n'; }
    return s;
}

std::string normalize(const std::string &src) {
    std::string out;
    out.reserve(src.size() + src.size() / 16);
    size_t i = 0;
    for (;;) {
        const size_t nl = src.find('\n', i);
        const size_t end = (nl == std::string::npos) ? src.size() : nl;
        const std::string ln = src.substr(i, end - i);

        const size_t cp = kw::commentPos(ln);
        const std::string code = (cp == std::string::npos) ? ln : ln.substr(0, cp);
        const std::string comment = (cp == std::string::npos) ? std::string() : ln.substr(cp);
        const size_t ind = code.find_first_not_of(" \t");

        // Ni code, ni forme jugeable : rendu à l'octet près.
        if (ind == std::string::npos || code.find('{') != std::string::npos) out += ln;
        else {
            const std::string indent = code.substr(0, ind);
            const std::string body = trim(code);
            std::vector<std::string> canon;
            for (const auto &stmt : splitStatements(body))
                for (const auto &l : expandPushPop(canonicalizeSpelling(stmt)))
                    canon.push_back(l);
            if (canon.size() == 1 && canon[0] == body) out += ln;   // déjà canonique
            else {
                // Les lignes suivantes reprennent l'indentation de l'originale — sauf
                // quand celle-ci est nulle parce que la ligne portait un label : une
                // instruction en colonne 1 déclencherait l'avertissement de l'assembleur.
                const std::string cont = indent.empty() ? "    " : indent;
                for (size_t k = 0; k < canon.size(); ++k) {
                    if (k) out += "\n";
                    out += (k ? cont : indent) + canon[k];
                    // Le commentaire de la ligne d'origine suit la PREMIÈRE
                    // instruction : c'est là qu'il était, et le dupliquer serait
                    // inventer une intention.
                    if (k == 0 && !comment.empty()) out += " " + comment;
                }
            }
        }
        if (nl == std::string::npos) break;
        out += '\n';
        i = nl + 1;
    }
    return out;
}

Result preprocess(const std::string &mainContent, const std::string &mainFile,
                  const FileProvider &files, bool strict) {
    PP pp(files, strict);
    pp.runFile(mainContent, mainFile);
    return pp.result;
}

} // namespace pp
