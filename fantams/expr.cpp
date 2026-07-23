// expr.cpp - Évaluateur d'expressions entières (voir expr.h)
#include "expr.h"

#include <cctype>
#include <stdexcept>

namespace expr {
namespace {

struct EvalError { std::string msg; };

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

    [[noreturn]] void fail(const std::string &m) { throw EvalError{m}; }

    // --- niveaux de précédence (croissante) ---
    int64_t logOr() {
        int64_t v = logAnd();
        for (;;) { skip(); if (eat("||")) { int64_t r = logAnd(); v = (v != 0 || r != 0) ? 1 : 0; } else break; }
        return v;
    }
    int64_t logAnd() {
        int64_t v = bitOr();
        for (;;) { skip(); if (eat("&&")) { int64_t r = bitOr(); v = (v != 0 && r != 0) ? 1 : 0; } else break; }
        return v;
    }
    int64_t bitOr() {
        int64_t v = bitXor();
        for (;;) { skip(); if (i+1 < s.size() && s[i]=='|' && s[i+1]=='|') break; if (eat("|")) v |= bitXor(); else break; }
        return v;
    }
    int64_t bitXor() {
        int64_t v = bitAnd();
        for (;;) { skip(); if (eat("^")) v ^= bitAnd(); else break; }
        return v;
    }
    int64_t bitAnd() {
        int64_t v = equality();
        for (;;) { skip(); if (i+1 < s.size() && s[i]=='&' && s[i+1]=='&') break; if (eat("&")) v &= equality(); else break; }
        return v;
    }
    int64_t equality() {
        int64_t v = relational();
        for (;;) { skip(); if (eat("==")) v = (v == relational()); else if (eat("!=")) v = (v != relational()); else break; }
        return v;
    }
    int64_t relational() {
        int64_t v = shift();
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
    int64_t shift() {
        int64_t v = additive();
        for (;;) { skip(); if (eat("<<")) v <<= additive(); else if (eat(">>")) v >>= additive(); else break; }
        return v;
    }
    int64_t additive() {
        int64_t v = term();
        for (;;) { skip(); if (eat("+")) v += term(); else if (eat("-")) v -= term(); else break; }
        return v;
    }
    int64_t term() {
        int64_t v = unary();
        for (;;) {
            skip();
            if (eat("*")) v *= unary();
            else if (eat("/")) { int64_t d = unary(); if (d == 0) fail("division par zéro"); v /= d; }
            else if (eat("%")) { int64_t d = unary(); if (d == 0) fail("modulo par zéro"); v %= d; }
            else break;
        }
        return v;
    }
    int64_t unary() {
        skip();
        if (eat("-")) return -unary();
        if (eat("+")) return unary();
        if (eat("~")) return ~unary();
        if (eat("!")) return unary() == 0 ? 1 : 0;
        return primary();
    }
    int64_t primary() {
        skip();
        if (eat("(")) { int64_t v = logOr(); if (!eat(")")) fail("')' attendu"); return v; }
        char c = peek();
        if (c == '\'') return parseChar();
        if (c == '$') {
            // '$' suivi d'un chiffre hexa = nombre ; sinon = symbole (adresse courante)
            if (i + 1 < s.size() && std::isxdigit((unsigned char)s[i + 1])) return parseNumber();
            ++i;
            int64_t out;
            if (resolver && resolver("$", out)) return out;
            fail("symbole '$' inconnu");
        }
        if (std::isdigit((unsigned char)c) || c == '%' || c == '#')
            return parseNumber();
        if (std::isalpha((unsigned char)c) || c == '_' || c == '.' || c == '@')
            return parseIdent();
        fail("expression invalide");
    }
    int64_t parseChar() {
        ++i; // '
        if (i >= s.size()) fail("caractère non terminé");
        int64_t v = (unsigned char)s[i++];
        if (i >= s.size() || s[i] != '\'') fail("caractère non terminé");
        ++i;
        return v;
    }
    int64_t parseNumber() {
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
        if (i == start) fail("nombre invalide");
        return v;
    }
    int64_t parseIdent() {
        size_t start = i;
        while (i < s.size() && (std::isalnum((unsigned char)s[i]) || s[i]=='_' || s[i]=='.' || s[i]=='@')) ++i;
        std::string name = s.substr(start, i - start);
        int64_t out;
        if (resolver && resolver(name, out)) return out;
        fail("symbole inconnu : '" + name + "'");
    }
};

} // namespace

Result eval(const std::string &text, const Resolver &resolver) {
    Result r;
    try {
        Parser p(text, resolver);
        r.value = p.logOr();
        p.skip();
        if (p.i < text.size()) { r.ok = false; r.error = "caractère inattendu : '" + std::string(1, text[p.i]) + "'"; return r; }
        r.ok = true;
    } catch (const EvalError &e) {
        r.ok = false; r.error = e.msg;
    }
    return r;
}

} // namespace expr
