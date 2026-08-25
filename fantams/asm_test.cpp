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

static void okc(const char *desc, bool cond) {
    if (cond) ++g_pass; else { ++g_fail; printf("  \033[31mFAIL\033[0m %s\n", desc); }
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
        "  org 0x8000\nstart:\n  jp done\n  nop\ndone:\n  ret\n",
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

    // --- coverage et chevauchement (ADR 0012) ------------------------------
    {
        asmb::Output o = asmb::assembleText("  org #8000\n  db 0,0\n", "t.asm");
        int n = 0;
        for (auto c : o.coverage) if (c) ++n;
        okc("coverage : deux zeros ecrits sont couverts", o.ok && n == 2 &&
            o.coverage[0x8000] && o.coverage[0x8001]);
        okc("coverage : le reste ne l'est pas", !o.coverage[0x7FFF] && !o.coverage[0x8002]);
        okc("coverage : pas d'avertissement sans chevauchement", o.warnings.empty());
    }
    {
        // deux ORG qui se recouvrent : un seul avertissement pour la plage, avec
        // les DEUX lignes en conflit nommees.
        asmb::Output o = asmb::assembleText(
            "  org #8000\n  db 1,2,3,4\n  org #8001\n  db 9,9\n", "t.asm");
        bool one = o.warnings.size() == 1;
        std::string m = one ? o.warnings[0].message : std::string();
        okc("chevauchement : un seul avertissement pour la plage", one);
        okc("chevauchement : plage coalescee &8001-&8002",
            m.find("&8001-&8002") != std::string::npos);
        okc("chevauchement : nomme le site ecrase", m.find("t.asm:2") != std::string::npos);
        okc("chevauchement : rapporte sur le site ecrasant",
            one && o.warnings[0].line == 4);
    }
    {
        asmb::Output o = asmb::assembleText("  org #8000\n  db 1\n  org #9000\n  db 1\n", "t.asm");
        okc("chevauchement : deux ORG disjoints n'en produisent pas", o.warnings.empty());
    }

    // --- Chaines : les deux delimiteurs, et la chaine decalee (ADR 0010) -----
    // Cas de reference du lot : chaque ligne exerce UNE construction, et les
    // refus en font partie autant que les acceptations — un message d'erreur
    // qui cesse de nommer son remplacant est une regression que rien d'autre
    // n'attrape.
    {
        // Les deux delimiteurs designent le meme objet : aucune ecriture ne
        // marche d'un cote et echoue de l'autre.
        chk("chaine : db simple quote", "  org #8000\n  db 'hi'\n", {0x68, 0x69}, 0x8000);
        chk("chaine : db double quote", "  org #8000\n  db \"hi\"\n", {0x68, 0x69}, 0x8000);
        chk("chaine : dm accepte aussi le simple quote", "  org #8000\n  dm 'hi'\n", {0x68, 0x69}, 0x8000);

        // Le delimiteur OPPOSE est du contenu ordinaire, sans echappement.
        chk("chaine : guillemet dans un litteral simple", "  org #8000\n  db 'a\"b'\n",
            {0x61, 0x22, 0x62}, 0x8000);
        chk("chaine : apostrophe dans un litteral double", "  org #8000\n  db \"a'b\"\n",
            {0x61, 0x27, 0x62}, 0x8000);

        // Un litteral d'UN octet vaut son code, quel que soit le delimiteur.
        chk("chaine : 'x' en expression", "  org #8000\n  ld a,'x'\n", {0x3E, 0x78}, 0x8000);
        chk("chaine : \"x\" en expression", "  org #8000\n  ld a,\"x\"\n", {0x3E, 0x78}, 0x8000);
        chk("chaine : arithmetique sur un litteral d'un octet",
            "  org #8000\n  db 'a'+1\n", {0x62}, 0x8000);

        // Litteral vide : zero octet emis, et l'element suivant reste en place.
        chk("chaine : litteral vide n'emet rien", "  org #8000\n  db '',#AA\n", {0xAA}, 0x8000);

        // Chaine decalee : la queue s'applique a CHAQUE octet.
        chk("chaine decalee : db 'hello'-'a'", "  org #8000\n  db 'hello'-'a'\n",
            {0x07, 0x04, 0x0B, 0x0B, 0x0E}, 0x8000);
        chk("chaine decalee : queue composee", "  org #8000\n  db 'hello'-'a'+1\n",
            {0x08, 0x05, 0x0C, 0x0C, 0x0F}, 0x8000);
        chk("chaine decalee : les deux delimiteurs, meme resultat",
            "  org #8000\n  db \"hello\"-\"a\"\n", {0x07, 0x04, 0x0B, 0x0B, 0x0E}, 0x8000);
        chk("chaine decalee : tout operateur binaire, pas seulement + et -",
            "  org #8000\n  db 'ab'*2\n", {0xC2, 0xC4}, 0x8000);
        chk("chaine decalee : le decalage se masque sur 8 bits",
            "  org #8000\n  db 'a'-'z'\n", {0xE7}, 0x8000);

        // --- Refus ----------------------------------------------------------
        // Contexte SCALAIRE : aucune valeur n'existe, et rasm y repond par un
        // zero silencieux qu'on se refuse a reproduire.
        chkErr("refus : ld hl,'ab' (aucune convention d'endianness)",
               "  org #8000\n  ld hl,'ab'\n");
        chkErr("refus : ld hl,\"\" (litteral vide sans valeur)",
               "  org #8000\n  ld hl,\"\"\n");
        chkErr("refus : dw n'est pas un contexte de chaine decalee",
               "  org #8000\n  dw 'hello'-'a'\n");
        // Les parentheses annoncent une expression : le litteral y redevient un
        // operande, donc sans valeur.
        chkErr("refus : db ('hello')-'a' n'est pas une chaine decalee",
               "  org #8000\n  db ('hello')-'a'\n");
        // Le litteral doit etre en TETE de l'element.
        chkErr("refus : db 1+'hello' (litteral pas en tete)",
               "  org #8000\n  db 1+'hello'\n");
        chkErr("refus : deux litteraux dans un element",
               "  org #8000\n  db 'ab'-'cd'\n");
        chkErr("refus : litteral non termine", "  org #8000\n  db 'hello\n");
        chkErr("refus : PRINT n'accepte pas une chaine decalee",
               "  org #8000\n  print 'hello'-'a'\n");
        chkErr("refus : CHARSET", "  org #8000\n  charset '0123',0\n");
    }
    {
        // Le choix du delimiteur n'est pas un avertissement : il ne nomme
        // aucune ambiguite (ADR 0010), contrairement au label sans ':'.
        chkWarn("chaine : le simple quote n'avertit pas",
                "  org #8000\n  db 'hello'\n", false);
        chkWarn("chaine : le double quote n'avertit pas non plus",
                "  org #8000\n  db \"hello\"\n", false);
    }
    {
        // Le refus de CHARSET nomme son remplacant, comme BANK ou TICKER.
        asmb::Output o = asmb::assembleText("  org #8000\n  charset '0123',0\n", "t.asm");
        bool named = !o.errors.empty() &&
                     o.errors[0].message.find("asset encoding") != std::string::npos;
        okc("refus : CHARSET nomme son remplacant", named);
    }

    // ADR 0015 : aucun identifiant utilisateur ne porte un nom du langage ni de la

    // machine. Les deux positions que l'assembleur possède : symbole et label.

    chkErr("registre en nom de constante", "hl equ 5\n");

    chkErr("registre en nom de variable", "c = 7\n");

    chkErr("condition en nom de constante", "nz equ 1\n");

    chkErr("mnémonique en label", "call: nop\n");

    chkErr("directive en label", "org: nop\n");

    chkErr("registre en label", "hl: nop\n");

    chkSym("label libre", "boucle: nop\n", "boucle", 0);

    chkSym("label local non concerné", "g: nop\n.b: nop\n", "g.b", 1);


    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
