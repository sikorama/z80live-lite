// expr_test.cpp - Tests de l'évaluateur d'expressions (fantams)
#include "expr.h"

#include <cstdio>
#include <map>
#include <string>

static int g_pass = 0, g_fail = 0;

static std::map<std::string, int64_t> g_syms;
static expr::Resolver resolver = [](const std::string &n, int64_t &out) -> bool {
    auto it = g_syms.find(n);
    if (it == g_syms.end()) return false;
    out = it->second; return true;
};

static void chk(const char *desc, const std::string &text, int64_t expected) {
    expr::Result r = expr::eval(text, resolver);
    if (!r.ok || r.value != expected) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %-40s attendu %lld obtenu %lld%s\n",
               desc, (long long)expected, (long long)r.value,
               r.ok ? "" : (" err: " + r.error).c_str());
    } else ++g_pass;
}

static void chkErr(const char *desc, const std::string &text) {
    expr::Result r = expr::eval(text, resolver);
    if (r.ok) { ++g_fail; printf("  \033[31mFAIL\033[0m %-40s aurait dû échouer (= %lld)\n", desc, (long long)r.value); }
    else ++g_pass;
}

int main() {
    printf("Tests évaluateur d'expressions\n");
    g_syms["start"] = 0x8000;

    // --- de base (préservé du comportement historique) ---
    chk("entier décimal", "42", 42);
    chk("hexa #", "#1234", 0x1234);
    chk("hexa 0x", "0x1234", 0x1234);
    chk("binaire %", "%1010", 10);
    chk("caractère", "'A'", 65);
    chk("addition", "1+2*3", 7);
    chk("parenthèses", "(1+2)*3", 9);
    chk("bit or", "%1100 | %0011", 15);
    chk("bit and", "%1100 & %1010", 8);
    chk("shift", "1 << 4", 16);
    chk("modulo", "7 % 3", 1);
    chk("négatif", "-5+3", -2);
    chk("not bit à bit", "~0 & 0xFF", 255);
    chk("symbole", "start+2", 0x8002);
    chkErr("symbole inconnu", "nope");
    chkErr("division par zéro", "1/0");

    // --- rondage "half up" final (comme rasm : db 7/2 -> 4, db -7/2 -> -3) ---
    chk("division exacte", "6/2", 3);
    chk("arrondi positif .5 -> +1", "7/2", 4);
    chk("arrondi négatif .5 -> vers 0", "-7/2", -3);
    chk("arrondi .4 -> troncature", "9/4", 2);   // 2.25 -> 2
    chk("arrondi .6 -> +1", "11/4", 3);          // 2.75 -> 3

    // --- littéraux flottants ---
    chk("flottant simple", "0.5*10", 5);
    chk("flottant dans expression", "1 + 0.2*10", 3); // 1+2.0=3.0

    // --- fonctions : sin/cos (degrés) / abs / hi / lo ---
    // valeurs vérifiées empiriquement contre rasm (wasm), voir historique du commit.
    chk("sin(90)", "sin(90)", 1);
    chk("sin(90)*100", "sin(90)*100", 100);
    chk("cos(0)*100", "cos(0)*100", 100);
    chk("sin(45)*1000 mod 256 (comme db)", "(sin(45)*1000) & 255", 195);
    chk("abs négatif", "abs(-5)", 5);
    chk("abs positif", "abs(5)", 5);
    chk("hi", "hi(#1234)", 0x12);
    chk("lo", "lo(#1234)", 0x34);
    chk("expression complète (rasm réel)", "20 + 10 * sin(90)", 30);
    // division réelle avant sin (pas de troncature entière intermédiaire) :
    // (360*1)/256 = 1.40625° -> sin -> *1000 -> round -> 25 (vérifié vs rasm)
    chk("division flottante avant sin", "sin((360*1)/256)*1000", 25);

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
