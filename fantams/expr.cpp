// expr.cpp - Évaluateur d'expressions entières (voir expr.h)
//
// Calcul interne en double (comme rasm) : permet les littéraux flottants et les
// fonctions (sin/cos/abs/hi/lo) sans perte de précision intermédiaire — seule la
// valeur FINALE est convertie en entier (arrondi "half up", cf. toInt()). Les
// opérateurs bit à bit (| ^ & << >> ~ %) convertissent chaque opérande en entier
// avant de calculer (ils n'ont pas de sens en flottant), puis reconvertissent le
// résultat en double pour rester dans le même arbre d'évaluation.
#include "expr.h"

#include "keywords.h"

#include <cctype>
#include <cmath>
#include <set>
#include <stdexcept>

namespace expr {
namespace {

struct EvalError { std::string msg; };

// Arrondi "half up" (comme rasm : 3.5 -> 4, -3.5 -> -3 — cf. arrondi vers +infini,
// pas arrondi au plus proche pair ni troncature vers zéro). Vérifié empiriquement
// contre rasm (db 7/2 -> 4, db -7/2 -> -3).
int64_t toInt(double v) { return (int64_t)std::floor(v + 0.5); }

struct Parser {
    const std::string &s;
    size_t i = 0;
    const Resolver &resolver;

    Parser(const std::string &str, const Resolver &r) : s(str), resolver(r) {}

    void skip() { while (i < s.size() && std::isspace((unsigned char)s[i])) ++i; }
    bool eof() { skip(); return i >= s.size(); }
    char peek() { skip(); return i < s.size() ? s[i] : 0; }
    bool eat(const char *op) {
        skip();
        size_t n = 0; while (op[n]) ++n;
        if (i + n <= s.size() && s.compare(i, n, op) == 0) { i += n; return true; }
        return false;
    }
    // Opérateur textuel (and/or/xor/not/mod/shl/shr/div) : casse indifférente, et
    // doit être suivi d'un caractère non-identifiant — sinon "android" serait lu
    // comme "and" suivi de "roid". Ces mots sont aussi des mnémoniques Z80, mais
    // sans conflit possible : parser.cpp sépare le mnémonique des opérandes avant
    // qu'expr ne voie quoi que ce soit ("ld a, b and 3" livre bien "b and 3").
    bool eatWord(const char *w) {
        skip();
        size_t n = 0; while (w[n]) ++n;
        if (i + n > s.size()) return false;
        for (size_t k = 0; k < n; ++k)
            if (std::tolower((unsigned char)s[i + k]) != std::tolower((unsigned char)w[k])) return false;
        if (i + n < s.size()) {
            char nx = s[i + n];
            if (std::isalnum((unsigned char)nx) || nx == '_' || nx == '.' || nx == '@') return false;
        }
        i += n;
        return true;
    }

    [[noreturn]] void fail(const std::string &m) { throw EvalError{m}; }

    // --- niveaux de précédence (croissante) ---
    double logOr() {
        double v = logAnd();
        for (;;) { skip(); if (eat("||")) { double r = logAnd(); v = (v != 0 || r != 0) ? 1 : 0; } else break; }
        return v;
    }
    double logAnd() {
        double v = bitOr();
        for (;;) { skip(); if (eat("&&")) { double r = bitOr(); v = (v != 0 && r != 0) ? 1 : 0; } else break; }
        return v;
    }
    double bitOr() {
        double v = bitXor();
        for (;;) { skip(); if (i+1 < s.size() && s[i]=='|' && s[i+1]=='|') break; if (eat("|") || eatWord("or")) v = (double)(toInt(v) | toInt(bitXor())); else break; }
        return v;
    }
    double bitXor() {
        double v = bitAnd();
        for (;;) { skip(); if (eat("^") || eatWord("xor")) v = (double)(toInt(v) ^ toInt(bitAnd())); else break; }
        return v;
    }
    double bitAnd() {
        double v = equality();
        for (;;) { skip(); if (i+1 < s.size() && s[i]=='&' && s[i+1]=='&') break; if (eat("&") || eatWord("and")) v = (double)(toInt(v) & toInt(equality())); else break; }
        return v;
    }
    double equality() {
        double v = relational();
        for (;;) { skip(); if (eat("==")) v = (v == relational()); else if (eat("!=")) v = (v != relational()); else break; }
        return v;
    }
    double relational() {
        double v = shift();
        for (;;) {
            skip();
            if (eat("<=")) v = (v <= shift());
            else if (eat(">=")) v = (v >= shift());
            else if (i+1 < s.size() && s[i]=='<' && s[i+1]=='<') break;
            else if (i+1 < s.size() && s[i]=='>' && s[i+1]=='>') break;
            else if (eat("<")) v = (v < shift());
            else if (eat(">")) v = (v > shift());
            else break;
        }
        return v;
    }
    double shift() {
        double v = additive();
        for (;;) { skip(); if (eat("<<") || eatWord("shl")) v = (double)(toInt(v) << toInt(additive()));
            else if (eat(">>") || eatWord("shr")) v = (double)(toInt(v) >> toInt(additive()));
            else break; }
        return v;
    }
    double additive() {
        double v = term();
        for (;;) { skip(); if (eat("+")) v += term(); else if (eat("-")) v -= term(); else break; }
        return v;
    }
    double term() {
        double v = unary();
        for (;;) {
            skip();
            if (eat("*")) v *= unary();
            else if (eat("/")) { double d = unary(); if (d == 0) fail("division by zero"); v /= d; }
            else if (eat("%") || eatWord("mod")) { int64_t d = toInt(unary()); if (d == 0) fail("modulo by zero"); v = (double)(toInt(v) % d); }
            // Division entière. '//' n'est PAS retenu pour cet usage : il est
            // réservé au commentaire de ligne. Arrondi vers -infini (floor), pas
            // troncature vers zéro : "div" est le raccourci de floor(a/b).
            else if (eatWord("div")) { double d = unary(); if (d == 0) fail("division by zero"); v = std::floor(v / d); }
            else break;
        }
        return v;
    }
    double unary() {
        skip();
        if (eat("-")) return -unary();
        if (eat("+")) return unary();
        if (eat("~") || eatWord("not")) return (double)(~toInt(unary()));
        if (eat("!")) return unary() == 0 ? 1 : 0;
        return power();
    }
    // Puissance. Plus liante que les unaires — `-2**2` vaut -4, comme partout —
    // et associative à DROITE : `2**3**2` vaut 2**9. L'exposant passe par unary()
    // pour que `2**-1` s'écrive.
    //
    // La graphie est `**` et non `^`, déjà pris par le ou exclusif. term() n'y voit
    // que du feu : unary() consomme `2**3` en entier avant que sa boucle ne cherche
    // un `*`.
    double power() {
        double v = primary();
        skip();
        if (eat("**")) return std::pow(v, unary());
        return v;
    }
    double primary() {
        skip();
        if (eat("(")) { double v = logOr(); if (!eat(")")) fail("expected ')'"); return v; }
        char c = peek();
        if (c == '\'' || c == '"') return (double)parseLiteral();
        if (c == '$') {
            // '$' suivi d'un chiffre hexa = nombre ; sinon = symbole (adresse courante)
            if (i + 1 < s.size() && std::isxdigit((unsigned char)s[i + 1])) return parseNumber();
            ++i;
            double out;
            if (resolver && resolver("$", out)) return out;
            fail("unknown symbol '$'");
        }
        if (std::isdigit((unsigned char)c) || c == '%' || c == '#')
            return parseNumber();
        if (std::isalpha((unsigned char)c) || c == '_' || c == '.' || c == '@')
            return parseIdentOrCall();
        fail("invalid expression");
    }
    // Un littéral en EXPRESSION doit valoir un nombre, et seul un littéral d'un
    // octet en a un. « ld hl,'ab' » n'est pas un cas à tolérer : aucune
    // convention d'endianness n'est écrite dans le source, et rasm y répond par
    // un zéro silencieux — reproduire ça, c'est émettre du faux sans le dire.
    // Le contexte qui accepte une SUITE d'octets, lui, c'est « db » : là le
    // littéral n'est pas un opérande, et la chaîne décalée s'en charge.
    int64_t parseLiteral() {
        kw::Literal lit = kw::readLiteral(s, i);
        if (!lit.error.empty()) fail(lit.error);
        i = lit.end;
        if (lit.bytes.size() == 1) return (unsigned char)lit.bytes[0];
        if (lit.bytes.empty())
            fail("an empty string literal has no value");
        fail("a string literal of " + std::to_string(lit.bytes.size()) +
             " bytes has no value: only a 1-byte literal has one (to emit the "
             "bytes, use 'db')");
    }
    // Nombre : entier (décimal/hexa/binaire) OU flottant décimal ("0.2", "3.14").
    // Le point décimal n'est reconnu qu'en base 10 (pas de sens en hexa/binaire).
    double parseNumber() {
        skip();
        int base = 10;
        if (s[i] == '$' || s[i] == '#') { base = 16; ++i; }
        else if (s[i] == '%') { base = 2; ++i; }
        else if (i + 1 < s.size() && s[i] == '0' && (s[i+1] == 'x' || s[i+1] == 'X')) { base = 16; i += 2; }
        size_t start = i;
        auto isdig = [&](char ch) -> bool {
            if (base == 16) return std::isxdigit((unsigned char)ch);
            if (base == 2) return ch == '0' || ch == '1';
            return std::isdigit((unsigned char)ch);
        };
        int64_t v = 0;
        while (i < s.size() && isdig(s[i])) {
            int d;
            char ch = s[i];
            if (ch >= '0' && ch <= '9') d = ch - '0';
            else d = std::tolower((unsigned char)ch) - 'a' + 10;
            v = v * base + d;
            ++i;
        }
        if (i == start) fail("invalid number");
        if (base == 10 && i < s.size() && s[i] == '.' && i + 1 < s.size() && std::isdigit((unsigned char)s[i + 1])) {
            double frac = 0, scale = 1;
            ++i;
            while (i < s.size() && std::isdigit((unsigned char)s[i])) { frac = frac * 10 + (s[i] - '0'); scale *= 10; ++i; }
            return (double)v + frac / scale;
        }
        return (double)v;
    }
    // Fonctions rasm reconnues, à une divergence près : les angles de sin/cos sont
    // en RADIANS, pas en degrés comme rasm (ADR 0021).
    // hi()/lo() opèrent sur la valeur convertie en entier (extraction d'octet).
    bool callBuiltin(const std::string &upperName, double &out) {
        static const std::set<std::string> unary1 = {
            "SIN", "COS", "ABS", "HI", "LO",
            // Arrondis explicites. FLOOR vaut 99 usages dans le corpus — de loin le
            // plus gros manque mesuré — parce que '/' est une division flottante :
            // qui veut tronquer doit l'écrire. ROUND délègue à toInt(), donc il suit
            // automatiquement la règle de départage retenue.
            "FLOOR", "CEIL", "INT", "ROUND",
        };
        static const std::set<std::string> binary2 = { "MIN", "MAX" };
        const bool is1 = unary1.count(upperName) != 0;
        const bool is2 = binary2.count(upperName) != 0;
        if (!is1 && !is2) return false;
        if (!eat("(")) fail("expected '(' after " + upperName);
        double a = logOr();
        double b = 0;
        if (is2) { if (!eat(",")) fail(upperName + ": expected ',' (2 arguments)"); b = logOr(); }
        if (!eat(")")) fail("expected ')'");
        if (upperName == "SIN") out = std::sin(a);        // radians (ADR 0021)
        else if (upperName == "COS") out = std::cos(a);   // radians (ADR 0021)
        else if (upperName == "ABS") out = std::fabs(a);
        else if (upperName == "HI") out = (double)((toInt(a) >> 8) & 0xFF);
        else if (upperName == "LO") out = (double)(toInt(a) & 0xFF);
        else if (upperName == "FLOOR") out = std::floor(a);   // vers -infini
        else if (upperName == "CEIL") out = std::ceil(a);     // vers +infini
        else if (upperName == "INT") out = std::trunc(a);     // vers zéro
        else if (upperName == "ROUND") out = (double)toInt(a);
        else if (upperName == "MIN") out = a < b ? a : b;
        else out = a > b ? a : b; // MAX
        return true;
    }
    double parseIdentOrCall() {
        size_t start = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i]=='_' || s[i]=='.' || s[i]=='@')) ++i;
        std::string name = s.substr(start, i - start);
        std::string up = name; for (char &c : up) c = (char)std::toupper((unsigned char)c);
        size_t save = i;
        if (peek() == '(') {
            double out;
            if (callBuiltin(up, out)) return out;
            i = save; // pas une fonction connue : laisse '(' pour l'appelant (ne devrait pas arriver ici)
        }
        double out;
        if (resolver && resolver(name, out)) return out;
        fail("unknown symbol '" + name + "'");
    }
};

} // namespace

Result eval(const std::string &text, const Resolver &resolver) {
    Result r;
    try {
        Parser p(text, resolver);
        double v = p.logOr();
        p.skip();
        if (p.i < text.size()) { r.ok = false; r.error = "unexpected character: '" + std::string(1, text[p.i]) + "'"; return r; }
        r.value = toInt(v);
        r.real = v;
        r.ok = true;
    } catch (const EvalError &e) {
        r.ok = false; r.error = e.msg;
    }
    return r;
}

} // namespace expr
