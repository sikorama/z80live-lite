// asm_test.cpp - Tests de l'assembleur 2 passes
#include "asm.h"
#include "sym.h"

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

// Compte les lignes d'un CSV, en-tete comprise.
static size_t csvLines(const std::string &t) {
    size_t n = 0;
    for (char c : t) if (c == '\n') ++n;
    return n;
}

// La ligne de la table qui commence par « nom, ». Chaine vide si absente.
static std::string symRow(const std::string &table, const std::string &name) {
    const std::string key = name + ",";
    size_t p = 0;
    while (p < table.size()) {
        size_t e = table.find('\n', p);
        if (e == std::string::npos) e = table.size();
        if (table.compare(p, key.size(), key) == 0) return table.substr(p, e - p);
        p = e + 1;
    }
    return std::string();
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

// --- Banques (ADR 0005 / ADR 0006) ------------------------------------------
static void chkBank(const char *desc, const std::string &src,
                    std::initializer_list<int> expectedExtra) {
    asmb::Output o = asmb::assembleText(src, "t.asm");
    std::vector<int> exp(expectedExtra);
    if (!o.ok || o.banksWritten != exp) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s : ok=%d banksWritten={", desc, o.ok);
        for (size_t k = 0; k < o.banksWritten.size(); ++k) printf("%s%d", k ? "," : "", o.banksWritten[k]);
        printf("}\n");
        for (auto &e : o.errors) printf("    err %s:%d %s\n", e.file.c_str(), e.line, e.message.c_str());
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
        // Une expression qui echoue a deja produit son erreur : PRINT ne doit pas
        // afficher en plus une valeur fabriquee, que le lecteur prendrait pour un
        // resultat.
        chkErr("refus : PRINT sur expression invalide", "  org #8000\n  print 1+\n");
        chkErr("refus : PRINT sur symbole inconnu", "  org #8000\n  print nexistepas\n");
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


    {
        // --- « org b<n>:adresse » (ADR 0005) --------------------------------
        // Le prefixe designe le RANGEMENT, le nombre qui suit reste l'adresse
        // LOGIQUE : c'est elle que prend le label.
        chkSym("b2 : le label prend l'adresse logique",
               "  org b2:#8000\nlab: db 1\n", "lab", 0x8000);
        chkSym("b4 : idem hors des 64K de base",
               "  org b4:#4000\nlab: db 1\n", "lab", 0x4000);
        chk("banque de base : le binaire est inchange",
            "  org b2:#8000\n  db 1,2,3\n", {1, 2, 3}, 0x8000);
        // L'offset vaut « adresse & 0x3FFF » : b2:#8000 range au meme endroit que
        // le #8000 nu, puisque 0x8000 >> 14 vaut 2.
        chk("b2:#8000 equivaut au #8000 nu", "  org #8000\n  db 9\n", {9}, 0x8000);
        // `banksWritten` liste TOUTES les banques ecrites : c'est l'appelant qui en
        // tire la taille du dump (64 ou 128 Ko) et le refus au-dela de la 7.
        chkBank("banque de base", "  org b2:#8000\n  db 1\n", {2});
        chkBank("sans prefixe, la banque suit l'adresse", "  org #8000\n  db 1\n", {2});
        chkBank("banque haute", "  org b4:#4000\n  db 1\n", {4});
        chkBank("plusieurs banques, triees",
                "  org b5:#4000\n  db 1\n  org b4:#4000\n  db 2\n", {4, 5});
        chkBank("base et extension melangees",
                "  org #0000\n  db 1\n  org b6:#4000\n  db 2\n", {0, 6});
        // Au-dela de la banque 7, l'assemblage marche : c'est l'EXPORT qui refuse.
        chkBank("banque 8 : assemblee quand meme", "  org b8:#4000\n  db 1\n", {8});

        // L'image porte les banques 0..7 a plat : (banque, offset) -> b*0x4000+o.
        {
            asmb::Output o = asmb::assembleText("  org b5:#4000\n  db #AB\n", "t.asm");
            okc("image : 128K", o.image.size() == 131072);
            okc("image : l'octet est en banque 5, offset 0", o.image[5 * 0x4000] == 0xAB);
            okc("image : la coverage suit", o.coverage[5 * 0x4000] != 0);
            okc("image : rien ailleurs", o.coverage[0x4000] == 0);
        }

        // La banque est REMANENTE, et un ORG nu qui en herite une haute avertit :
        // un prefixe oublie deplacerait le bloc sans aucun diagnostic.
        chkWarn("ORG nu heritant une banque haute : avertit",
                "  org b4:#4000\n  db 1\n  org #c000\n  db 2\n", true);
        chkWarn("ORG nu sans banque explicite : rien",
                "  org #4000\n  db 1\n  org #c000\n  db 2\n", false);
        chkWarn("b0 ramene dans les 64K de base, sans avertir",
                "  org b4:#4000\n  db 1\n  org b0:#0000\n  db 2\n", false);

        // Le cout du masquage, assume par l'ADR 0005 : b4:#4000 et b4:#8000 se
        // rangent au meme offset. C'est le detecteur de recouvrement qui le dit.
        chkWarn("masquage : deux adresses logiques, un seul rangement",
                "  org b4:#4000\n  db 1\n  org b4:#8000\n  db 2\n", true);

        chkErr("prefixe qui n'est pas une banque", "  org x2:#4000\n  db 1\n");
        chkErr("prefixe sans adresse", "  org b4:\n  db 1\n");
    }


    // --- ORG a deux parametres : logique et rangement (ADR 0005) --------------
    //
    // Semantique rasm, mesuree contre rasm : le PREMIER parametre est l'adresse
    // logique — celle des labels, celle pour laquelle le code est assemble — et le
    // SECOND l'adresse de rangement, ou les octets sont reellement ecrits en
    // attendant qu'un chargeur les recopie.
    printf("\n-- ORG deplace (logique, rangement) --\n");
    {
        const char *src = "  org #2000,#3000\nstart:\n  ld hl,start\n";
        asmb::Output o = asmb::assembleText(src, "t.asm");
        okc("deplace : le label vaut l'adresse LOGIQUE", o.symbols["start"] == 0x2000);
        okc("deplace : l'octet est range a l'adresse de RANGEMENT",
            o.image[0x3000] == 0x21 && o.image[0x3001] == 0x00 && o.image[0x3002] == 0x20);
        okc("deplace : rien a l'adresse logique", o.coverage[0x2000] == 0);
        okc("deplace : loadAddress est le rangement", o.loadAddress == 0x3000);
    }
    // ALIGN aligne le LOGIQUE (comme rasm) : c'est l'adresse ou le code tournera
    // apres recopie. Le rangement suit du meme ecart, donc n'est pas aligne.
    chkSym("deplace : ALIGN aligne le logique",
           "  org #2000,#3000\n  nop\n  align 16\naligned:\n  nop\n", "aligned", 0x2010);
    // Le deplacement N'EST PAS REMANENT : un ORG nu le remet a zero (comme rasm).
    {
        asmb::Output o = asmb::assembleText("  org #2000,#3000\n  nop\n  org #5000\n  db #42\n", "t.asm");
        okc("deplace : un ORG nu remet le deplacement a zero", o.image[0x5000] == 0x42);
        okc("deplace : et n'ecrit pas a l'ancien ecart", o.coverage[0x6000] == 0);
    }
    // Le chevauchement se produit AU RANGEMENT : c'est la que les octets s'ecrasent.
    chkWarn("deplace : le chevauchement est detecte au rangement",
            "  org #2000,#3000\n  db 1\n  org #3000\n  db 2\n", true);
    // Un RUN qui tombe dans un bloc deplace demarre sur de la memoire vide : le PC
    // reste celui que la source demande, mais le silence serait une panne sans
    // diagnostic.
    chkWarn("deplace : RUN dans un bloc deplace avertit",
            "  org #2000,#3000\nstart:\n  nop\n  run start\n", true);
    chkWarn("deplace : RUN hors bloc deplace n'avertit pas",
            "  org #2000,#3000\n  nop\n  org #4000\nboot:\n  nop\n  run boot\n", false);
    // Le prefixe de banque qualifie le RANGEMENT : il se porte donc sur le DERNIER
    // parametre. Sur le premier d'une forme a deux, il est refuse — le rangement
    // serait decrit de part et d'autre de l'adresse logique.
    {
        asmb::Output o = asmb::assembleText("  org #4000,b4:#100\n  db #AB\n", "t.asm");
        okc("deplace : prefixe sur le rangement", o.ok && o.image[4 * 0x4000 + 0x100] == 0xAB);
    }
    chkErr("deplace : prefixe sur le premier parametre", "  org b4:#4000,#100\n  db 1\n");
    chkErr("deplace : deux prefixes", "  org b4:#4000,b5:#100\n  db 1\n");
    chkErr("deplace : trois parametres", "  org #4000,#100,#200\n  db 1\n");

    // --- Table des symboles (ADR 0019) ---------------------------------------
    printf("\n-- Table des symboles (--sym) --\n");
    {
        const std::string src =
            "SCREEN equ #C000\n"
            "BIG    equ 1<<20\n"
            "MINUS  equ -1\n"
            "count = 1\n"
            "count = 5\n"
            "  org #8000\n"
            "main:\n"
            "  nop\n"
            "  org #4000,b4:#100\n"
            "far:\n"
            "  nop\n";
        asmb::Output o = asmb::assembleText(src, "t.asm");
        const std::string t = sym::format(o);

        // L'en-tete est une VRAIE ligne CSV : les noms de colonnes SONT la version.
        okc("sym : en-tete exacte", t.rfind("name,type,value,bank,store,file,line\n", 0) == 0);
        // 5 symboles : 2 labels + 3 constantes. La variable 'count' n'y est PAS.
        okc("sym : une ligne par symbole, plus l'en-tete", csvLines(t) == 6);
        okc("sym : la variable n'est pas exportee", symRow(t, "count").empty());

        okc("sym : un label porte son type, sa valeur et son rangement",
            symRow(t, "main") == "main,label,0x8000,2,0x8000,t.asm,7");
        // Bloc deplace ET en banque : la valeur reste logique, le rangement suit.
        okc("sym : un label deplace separe valeur et rangement",
            symRow(t, "far") == "far,label,0x4000,4,0x100,t.asm,10");
        // Une constante n'habite nulle part : ni banque ni rangement.
        okc("sym : une constante n'a ni banque ni rangement",
            symRow(t, "SCREEN") == "SCREEN,const,0xC000,-,-,t.asm,1");
        // Non masquee, et signee : c'est la valeur que l'assembleur a utilisee.
        okc("sym : une constante n'est pas masquee en 16 bits",
            symRow(t, "BIG") == "BIG,const,0x100000,-,-,t.asm,2");
        okc("sym : une constante negative garde son signe",
            symRow(t, "MINUS") == "MINUS,const,-0x1,-,-,t.asm,3");

        // Tri : banque, puis rangement, puis nom ; les constantes en QUEUE. Un
        // consommateur qui lit jusqu'a la premiere banque « - » a tous les
        // symboles adressables.
        const size_t pMain = t.find("\nmain,"), pFar = t.find("\nfar,"),
                     pBig = t.find("\nBIG,"), pMinus = t.find("\nMINUS,");
        okc("sym : tri par banque puis rangement", pMain < pFar);
        okc("sym : les constantes forment la queue", pFar < pBig);
        okc("sym : les constantes sont triees par nom", pBig < pMinus);
    }
    {
        // Les noms sont ceux que l'assembleur connait, sans retouche. Ici le cas
        // QUALIFIE : « .inner » sort en « top.inner ». Le cas MANGLE (« @retry__2 »)
        // se produit au preprocesseur, que `assembleText` ne fait pas tourner — il
        // se verifie de bout en bout au CLI, pas ici.
        asmb::Output o = asmb::assembleText(
            "  org #8000\n@loop:\n  nop\ntop:\n.inner:\n  nop\n", "t.asm");
        const std::string t = sym::format(o);
        okc("sym : un label local sort qualifie", !symRow(t, "top.inner").empty());
    }
    {
        // Le champ fichier est guillemete SEULEMENT s'il en a besoin : sans ca, un
        // chemin a virgule produirait une ligne a huit champs dans un fichier a sept.
        asmb::Output o = asmb::assembleText("  org #8000\nmain:\n  nop\n", "mon,brouillon.asm");
        const std::string t = sym::format(o);
        okc("sym : un chemin a virgule est guillemete",
            t.find(",\"mon,brouillon.asm\",2\n") != std::string::npos);
    }

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
