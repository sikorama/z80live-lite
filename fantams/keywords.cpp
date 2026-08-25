// keywords.cpp - Vocabulaire réservé indexé par phase (voir keywords.h)
#include "keywords.h"

#include "z80.h"

#include <cctype>
#include <set>

namespace kw {
namespace {

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

// Cran 1 — ce que le parseur d'instruction doit désambiguïser.
const std::set<std::string> &instructionWords() {
    static const std::set<std::string> kws = {
        "ORG", "RUN", "ALIGN", "DB", "DEFB", "DM", "DEFM", "DW", "DEFW",
        "DS", "DEFS", "RMB", "EQU",
        // directives rasm reconnues mais non implémentées (hors périmètre) :
        // gardées réservées pour échouer proprement plutôt que d'être lues
        // comme un label.
        "BUILDSNA", "BANKSET", "NOLIST", "LIST",
    };
    return kws;
}

// Cran 2 — ce que l'assembleur traite ou refuse nommément.
const std::set<std::string> &assemblyWords() {
    static const std::set<std::string> kws = {
        "ASSERT", "PRINT", "BANK", "SNASET", "SETCPC", "TICKER", "STR",
        "CHARSET",
    };
    return kws;
}

// Cran 3 — ce qui n'existe qu'au temps préprocesseur.
const std::set<std::string> &preprocessWords() {
    static const std::set<std::string> kws = {
        "LET", "IF", "IFDEF", "IFNDEF", "ELSE", "ELSEIF", "ENDIF",
        "MACRO", "ENDM", "MEND", "REPEAT", "REND", "WHILE", "WEND",
        "MODULE", "ENDMODULE", "STRUCT", "ENDSTRUCT", "ENDS",
        "INCLUDE", "INCBIN", "READ", "@@EXPORT",
        // ADR 0016 : fermeture universelle, fermetures explicites, et la boucle
        // à bornes écrites. `TO` et `UNTIL` sont réservés parce qu'ils portent la
        // sémantique de la borne — les confondre avec un symbole la ferait deviner.
        "END", "ENDMACRO", "ENDREPEAT", "ENDWHILE", "ENDFOR",
        "FOR", "TO", "UNTIL",
    };
    return kws;
}

// Registres, paires et conditions : le vocabulaire de la MACHINE, sans phase.
const std::set<std::string> &machineWords() {
    static const std::set<std::string> kws = {
        "A", "B", "C", "D", "E", "H", "L", "I", "R",
        "AF", "BC", "DE", "HL", "SP", "PC",
        "IX", "IY", "IXL", "IXH", "IYL", "IYH", "LX", "LY", "HX", "HY",
        "NZ", "Z", "NC", "PO", "PE", "P", "M",
    };
    return kws;
}

// La CATÉGORIE d'un mot réservé, pour que le diagnostic dise de quoi il s'agit :
// « 'hl' is a Z80 register » se corrige, « 'hl' is reserved » se subit.
const char *reservedKind(const std::string &U) {
    if (z80::mnemoFromString(U) != z80::Mnemo::Invalid) return "a Z80 mnemonic";
    if (machineWords().count(U)) return "a Z80 register or condition";
    if (instructionWords().count(U) || assemblyWords().count(U)) return "an assembler directive";
    if (preprocessWords().count(U)) return "a preprocessor keyword";
    return nullptr;
}

} // namespace

bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '@';
}

bool isMachineWord(const std::string &upperTok) {
    return machineWords().count(upperTok) != 0;
}

std::string reservedName(const std::string &name, const std::string &position) {
    const char *kind = reservedKind(upper(name));
    if (!kind) return {};
    return "'" + name + "' is " + kind + " and cannot name " + position +
           ": reserved words are reserved — rename it";
}

bool isReservedWord(const std::string &upperTok, Phase ph) {
    if (z80::mnemoFromString(upperTok) != z80::Mnemo::Invalid) return true;
    if (instructionWords().count(upperTok)) return true;
    if (ph == Phase::Instruction) return false;
    if (assemblyWords().count(upperTok)) return true;
    if (ph == Phase::Assembly) return false;
    return preprocessWords().count(upperTok) != 0;
}

void peelLabel(const std::string &code, std::string &label, std::string &rest, Phase ph,
               bool *sawColon, const std::function<bool(const std::string &)> &isMacro) {
    size_t p = 0; while (p < code.size() && isIdentChar(code[p])) ++p;
    if (p > 0) {
        size_t q = p; while (q < code.size() && std::isspace((unsigned char)code[q])) ++q;
        if (q < code.size() && code[q] == ':') {
            label = code.substr(0, p);
            rest = trim(code.substr(q + 1));
            if (sawColon) *sawColon = true;
            return;
        }
        std::string tok = upper(code.substr(0, p));
        if (!isReservedWord(tok, ph) && !(isMacro && isMacro(tok))) {
            // Exception « nom MACRO params » (forme alternative de déclaration) :
            // `nom` n'est pas un label, c'est la macro en cours de définition, dont
            // le nom n'est pas encore connu. La ligne est laissée intacte pour que
            // la détection de mot-clé en amont la voie entière. Réservée au temps
            // préprocesseur : `MACRO` n'existe plus après.
            if (ph == Phase::Preprocess &&
                upper(firstToken(trim(code.substr(p)))) == "MACRO") {
                label.clear(); rest = code;
                if (sawColon) *sawColon = true; // rien à signaler : il n'y a pas de label
                return;
            }
            label = code.substr(0, p);
            rest = trim(code.substr(p));
            if (sawColon) *sawColon = false;
            return;
        }
    }
    label.clear(); rest = code;
}

// Échappements reconnus dans un littéral. Inchangés : ce lot unifie les
// DÉLIMITEURS, pas la table d'échappement.
static char unescapeChar(char c) {
    switch (c) { case 'n': return '\n'; case 't': return '\t'; case 'r': return '\r';
        case '0': return '\0'; case '\\': return '\\'; case '"': return '"'; case '\'': return '\''; }
    return c;
}

Literal readLiteral(const std::string &s, size_t pos) {
    Literal r;
    if (pos >= s.size() || (s[pos] != '"' && s[pos] != '\'')) return r;
    const char q = s[pos];
    size_t i = pos + 1;
    for (;;) {
        if (i >= s.size()) { r.error = "unterminated string literal"; return r; }
        const char c = s[i];
        if (c == q) { ++i; break; }
        // Le délimiteur OPPOSÉ n'est pas échappé : il est du contenu.
        if (c == '\\' && i + 1 < s.size()) { r.bytes += unescapeChar(s[i + 1]); i += 2; continue; }
        r.bytes += c; ++i;
    }
    r.present = true;
    r.end = i;
    return r;
}

size_t commentPos(const std::string &s) {
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; }
        else if (c == ';') return i;
        // Commentaire de ligne C : '//' (le '/' isolé reste la division).
        else if (c == '/' && i + 1 < s.size() && s[i + 1] == '/') return i;
    }
    return std::string::npos;
}

} // namespace kw
