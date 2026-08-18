// asm_test.cpp - Tests de l'assembleur 2 passes
#include "asm.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

static std::string hex(const std::vector<uint8_t> &v) {
    std::string s; char b[8];
    for (size_t i = 0; i < v.size(); ++i) { snprintf(b, sizeof b, "%02X", v[i]); if (i) s += ' '; s += b; }
    return s;
}

// Assemble `src`, vérifie octets + adresse de chargement.
static void chk(const char *desc, const std::string &src,
                std::initializer_list<uint8_t> expected, uint16_t load = 0) {
    asmb::Output o = asmb::assembleText(src, "t.asm");
    std::vector<uint8_t> exp(expected);
    bool okLoad = (exp.empty() || o.loadAddress == load);
    if (!o.ok || o.bin != exp || !okLoad) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s\n    attendu @%04X [%s]\n    obtenu  @%04X [%s]\n",
               desc, load, hex(exp).c_str(), o.loadAddress, hex(o.bin).c_str());
        for (auto &e : o.errors) printf("    err %s:%d %s\n", e.file.c_str(), e.line, e.message.c_str());
    } else ++g_pass;
}

static void chkSym(const char *desc, const std::string &src, const char *sym, int64_t val) {
    asmb::Output o = asmb::assembleText(src, "t.asm");
    auto it = o.symbols.find(sym);
    if (!o.ok || it == o.symbols.end() || it->second != val) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s : %s attendu %lld obtenu %lld (ok=%d)\n",
               desc, sym, (long long)val, it == o.symbols.end() ? -1 : (long long)it->second, o.ok);
    } else ++g_pass;
}

static void chkErr(const char *desc, const std::string &src) {
    asmb::Output o = asmb::assembleText(src, "t.asm");
    if (o.ok) { ++g_fail; printf("  \033[31mFAIL\033[0m %s (aurait dû échouer)\n", desc); }
    else ++g_pass;
}

// Vérifie qu'un avertissement (bonne pratique) est bien émis, sans bloquer l'assemblage.
static void chkWarn(const char *desc, const std::string &src, bool expectWarning) {
    asmb::Output o = asmb::assembleText(src, "t.asm");
    bool hasWarn = !o.warnings.empty();
    if (!o.ok || hasWarn != expectWarning) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s : ok=%d warnings=%zu (attendu=%d)\n",
               desc, o.ok, o.warnings.size(), expectWarning);
    } else ++g_pass;
}

int main() {
    printf("Tests assembleur 2 passes\n");

    // base + ORG
    chk("nop/ret", "  nop\n  ret\n", {0x00, 0xC9});
    chk("org", "  org 0x8000\n  ld a,1\n", {0x3E, 0x01}, 0x8000);

    // référence AVANT (le point clé des 2 passes)
    chk("forward jp",
        "  org 0x8000\nstart:\n  jp end\n  nop\nend:\n  ret\n",
        {0xC3, 0x04, 0x80, 0x00, 0xC9}, 0x8000);
    chk("forward jr",
        "  org 0\n  jr next\nnext:\n  nop\n",
        {0x18, 0x00, 0x00}, 0);
    chk("backward ref",
        "  org 0x100\nloop:\n  djnz loop\n",
        {0x10, 0xFE}, 0x100); // -2

    // symboles / EQU / '='
    chkSym("label addr", "  org 0x4000\n  nop\nhere:\n  ret\n", "here", 0x4001);
    chk("equ usage", "VAL equ 0x42\n  ld a,VAL\n", {0x3E, 0x42});
    chk("equ colon", "VAL: equ 7\n  ld b,VAL\n", {0x06, 0x07});
    chk("assign =", "port = 0xFE\n  in a,(port)\n", {0xDB, 0xFE});
    chk("equ forward", "  ld hl,SIZE\nSIZE equ tail-head\nhead:\n  nop\ntail:\n",
        {0x21, 0x01, 0x00, 0x00}); // ld hl,1 (tail-head=1) + nop

    // $ = adresse courante
    chk("dollar", "  org 0x0100\n  dw $\n", {0x00, 0x01}, 0x0100);

    // directives data
    chk("db mixte", "  db 1,2,\"AB\",0\n", {0x01, 0x02, 0x41, 0x42, 0x00});
    chk("dw", "  dw 0x1234,0xABCD\n", {0x34, 0x12, 0xCD, 0xAB});
    chk("ds", "  ds 3\n", {0x00, 0x00, 0x00});
    chk("ds fill", "  ds 2,0xFF\n", {0xFF, 0xFF});
    chk("db expr", "  db 2*3+1, 1<<4\n", {0x07, 0x10});
    chk("db char", "  db 'A','Z'\n", {0x41, 0x5A});

    // label sans ':' (toléré, comme rasm) tant que le 1er mot n'est pas un mnémo/directive connu
    chk("label sans ':'", "start\n  ld a,1\n  jp start\n", {0x3E, 0x01, 0xC3, 0x00, 0x00});
    chkSym("label sans ':' addr", "  org 0x4000\nstart\n  nop\n", "start", 0x4000);
    // NOLIST/LIST : no-op (contrôle du listing seulement, comme rasm)
    chk("nolist no-op", "  nolist\n  ld a,1\n  list\n  ld b,2\n", {0x3E, 0x01, 0x06, 0x02});
    // BUILDSNA/BANKSET : no-op chez fantams (le split sur ':' est fait par pp.cpp en amont ;
    // ici chaque statement est déjà sur sa propre ligne, comme le reçoit vraiment asm.cpp).
    chk("BUILDSNA en-tête rasm (no-op)",
        "BUILDSNA V2\nBANKSET 0\nORG 0x8000\nRUN $\n  ld a,1\n", {0x3E, 0x01}, 0x8000);

    // avertissements de bonne pratique (non bloquants)
    chkWarn("warn label sans ':'", "start\n  nop\n", true);
    chkWarn("pas de warn label avec ':'", "start:\n  nop\n", false);
    chkWarn("warn instruction en colonne 1", "start:\nnop\n", true);
    chkWarn("pas de warn instruction indentée", "start:\n  nop\n", false);

    // labels locaux ".nom" : qualifiés par le dernier label global (comme rasm) ->
    // deux ".loop" sous deux globaux différents ne collisionnent pas.
    chkSym("label local .nom sous 2 globaux distincts (A)",
        "blockA:\n.loop:\n  nop\nblockB:\n.loop:\n  nop\n", "blockA.loop", 0);
    chkSym("label local .nom sous 2 globaux distincts (B)",
        "blockA:\n.loop:\n  nop\nblockB:\n.loop:\n  nop\n", "blockB.loop", 1);
    chkErr("label local .nom hors contexte -> non résolu",
        "blockA:\n.loop:\n  nop\n  ret\nblockB:\n  nop\n  ret\nblockC:\n  jp .loop\n");
    // référence qualifiée explicite "global.local" depuis un autre contexte
    chkSym("label local référencé via global.local", "blockA:\n.loop:\n  nop\n  jp blockA.loop\n", "blockA.loop", 0);

    // repli insensible à la casse (rasm ne distingue pas la casse) : résolu + avertissement
    chkWarn("repli casse : résolu avec avertissement", "Foo: nop\n  jp foo\n", true);
    chkErr("casse : rien à replier -> erreur si vraiment absent", "  jp doesNotExist\n");

    // align
    chk("align",
        "  org 0x4001\n  db 0xAA\n  align 4\n  db 0xBB\n",
        {0xAA, 0x00, 0x00, 0xBB}, 0x4001); // AA@4001, align->4004, BB@4004

    // programme complet réaliste
    chk("prog",
        "  org 0x8000\n"
        "  ld hl,msg\n"
        "loop:\n"
        "  ld a,(hl)\n"
        "  or a\n"
        "  ret z\n"
        "  inc hl\n"
        "  jr loop\n"
        "msg:\n"
        "  db \"Hi\",0\n",
        {0x21, 0x09, 0x80,   // ld hl,msg (msg=0x8009)
         0x7E,               // ld a,(hl)
         0xB7,               // or a
         0xC8,               // ret z
         0x23,               // inc hl
         0x18, 0xFA,         // jr loop (-6)
         0x48, 0x69, 0x00},  // "Hi",0
        0x8000);

    // erreurs
    chkErr("symbole indéfini", "  ld a,UNDEF\n");
    chkErr("label dupliqué", "foo:\n  nop\nfoo:\n  nop\n");
    chkErr("directive inconnue", "  bogus 1,2\n");

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
