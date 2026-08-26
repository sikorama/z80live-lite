// sna_test.cpp - Tests de l'export SNA
#include "sna.h"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;
static void ok(const char *desc, bool cond) {
    if (cond) ++g_pass; else { ++g_fail; printf("  \033[31mFAIL\033[0m %s\n", desc); }
}

int main() {
    printf("Tests export SNA\n");

    std::vector<uint8_t> image(65536, 0);
    image[0x8000] = 0xC9;  // un octet de code à l'adresse de chargement
    image[0xBFFF] = 0x42;

    sna::Options opt; opt.pc = 0x8000; opt.sp = 0xC000;
    std::vector<uint8_t> s = sna::build(image, opt);

    ok("taille = 256 + 64K", s.size() == 256 + 65536);
    ok("signature MV - SNA", std::string((char *)s.data(), 8) == "MV - SNA");
    ok("version 3", s[0x10] == 3);
    ok("SP = 0xC000", s[0x21] == 0x00 && s[0x22] == 0xC0);
    ok("PC = 0x8000", s[0x23] == 0x00 && s[0x24] == 0x80);
    ok("IM 1", s[0x25] == 1);
    ok("multiconfig 0x8D", s[0x40] == 0x8D);
    ok("ramconfig 0xC0", s[0x41] == 0xC0);
    ok("palette[0]=0x04", s[0x2F] == 0x04);
    ok("crtc r1 = 40", s[0x43 + 1] == 40);
    ok("ppi control 0x82", s[0x59] == 0x82);
    ok("psg r7 = 0x3F", s[0x5B + 7] == 0x3F);
    ok("dumpsize = 64", s[0x6B] == 64);
    ok("CPCType = 2", s[0x6D] == 2);
    ok("interruptrequestflag @0xB4 = 0", s[0xB4] == 0);
    // le dump mémoire suit l'en-tête : offset 256 == mem[0]
    ok("mem[0x8000] recopié", s[256 + 0x8000] == 0xC9);
    ok("mem[0xBFFF] recopié", s[256 + 0xBFFF] == 0x42);

    // --- base : lecture et fusion par la coverage (ADR 0012) ---------------
    // Une base synthetique : v2, dump plat de 64K, etat materiel reconnaissable.
    std::vector<uint8_t> raw(256 + 65536, 0xAA);
    const char *msig = "MV - SNA";
    for (int i = 0; i < 8; ++i) raw[i] = (uint8_t)msig[i];
    for (int i = 8; i < 256; ++i) raw[i] = 0;
    raw[0x10] = 2;        // version 2
    raw[0x21] = 0xF0; raw[0x22] = 0xBF;   // SP = 0xBFF0, la pile du firmware
    raw[0x40] = 0x84;     // multiconfig : ROM basse ACTIVEE (≠ 0x8D des defauts)
    raw[0x6B] = 64; raw[0x6C] = 0;
    raw[0x6D] = 2;        // CPCType 6128
    raw[256 + 0xBD00] = 0x55;   // un vecteur d'indirection quelconque

    sna::Base base; std::string err;
    ok("parseBase accepte un v2 de 64K", sna::parseBase(raw, base, err));
    ok("parseBase : CPCType relu", base.cpcType == 2);

    {
        std::vector<uint8_t> bad = raw; bad[0x10] = 3;
        sna::Base b; std::string e;
        ok("parseBase refuse le v3", !sna::parseBase(bad, b, e));
        ok("le refus nomme le remplacant", e.find("version 2") != std::string::npos);
    }
    {
        std::vector<uint8_t> bad = raw; bad[0x6B] = 128;
        sna::Base b; std::string e;
        ok("parseBase refuse un dump de 128 Ko", !sna::parseBase(bad, b, e));
    }
    {
        std::vector<uint8_t> bad = raw; bad[0] = 'X';
        sna::Base b; std::string e;
        ok("parseBase refuse ce qui n'est pas un snapshot", !sna::parseBase(bad, b, e));
    }
    {
        std::vector<uint8_t> bad(256 + 1024, 0);
        sna::Base b; std::string e;
        ok("parseBase refuse un fichier tronque", !sna::parseBase(bad, b, e));
    }

    {
        // l'assemblage n'a ecrit qu'un octet : tout le reste doit venir de la base.
        std::vector<uint8_t> img(65536, 0);
        std::vector<uint8_t> cov(65536, 0);
        img[0x8000] = 0xC9; cov[0x8000] = 1;
        img[0x8001] = 0x00; cov[0x8001] = 1;   // un ZERO ecrit : il doit gagner

        sna::Options o; o.pc = 0x8000; o.sp = 0xC000; o.cpcType = 0;
        std::vector<uint8_t> s2 = sna::build(img, o, &base, &cov);

        ok("base : taille inchangee", s2.size() == 256 + 65536);
        ok("base : l'en-tete de la base fait foi (multiconfig)", s2[0x40] == 0x84);
        ok("base : SP vient de la base, pas des Options", s2[0x21] == 0xF0 && s2[0x22] == 0xBF);
        ok("base : version de la base conservee", s2[0x10] == 2);
        ok("base : CPCType de la base conserve", s2[0x6D] == 2);
        ok("base : seul PC est patche", s2[0x23] == 0x00 && s2[0x24] == 0x80);
        ok("base : l'octet ecrit gagne", s2[256 + 0x8000] == 0xC9);
        ok("base : un ZERO ecrit gagne aussi", s2[256 + 0x8001] == 0x00);
        ok("base : hors coverage, la memoire vient de la base", s2[256 + 0x4000] == 0xAA);

        // --- dump de 128 K (ADR 0006) ---------------------------------------
        // Le dump reste PLAT : 131 072 octets, banques 0..7 dans l'ordre. Ce que
        // lit un dump de 64 K lit celui-la, ce que des chunks ne garantissent pas.
        {
            std::vector<uint8_t> big(131072, 0);
            big[0x8000] = 0xC9;
            big[5 * 0x4000] = 0xAB;      // banque 5, offset 0
            big[7 * 0x4000 + 3] = 0xCD;  // banque 7, offset 3
            sna::Options o3; o3.pc = 0x8000;
            std::vector<uint8_t> s3 = sna::build(big, o3, nullptr, nullptr, 128);

            ok("128K : taille = 256 + 128K", s3.size() == 256 + 131072);
            ok("128K : l'en-tete annonce 128 Ko", s3[0x6B] == 128 && s3[0x6C] == 0);
            ok("128K : banque 1 a sa place", s3[256 + 0x8000] == 0xC9);
            ok("128K : banque 5 a sa place", s3[256 + 5 * 0x4000] == 0xAB);
            ok("128K : banque 7 a sa place", s3[256 + 7 * 0x4000 + 3] == 0xCD);

            // 64 Ko reste le defaut, et n'emporte que les banques 0..3.
            std::vector<uint8_t> s4 = sna::build(big, o3);
            ok("64K : taille inchangee", s4.size() == 256 + 65536);
            ok("64K : l'en-tete annonce 64 Ko", s4[0x6B] == 64 && s4[0x6C] == 0);
            ok("64K : la banque 5 n'y est pas", s4.size() == 256 + 65536);
        }

        // Une base de 64 K sert de fond aux 64 K de base ; au-dela, l'image fait
        // foi seule — le firmware ne vit pas dans l'extension.
        {
            std::vector<uint8_t> img2(131072, 0), cov2(65536, 0);
            img2[0x8000] = 0xC9; cov2[0x8000] = 1;
            img2[6 * 0x4000] = 0xEE;
            sna::Options o4; o4.pc = 0x8000;
            std::vector<uint8_t> s5 = sna::build(img2, o4, &base, &cov2, 128);
            ok("base + 128K : taille", s5.size() == 256 + 131072);
            ok("base + 128K : hors coverage, la base gagne dans les 64K", s5[256 + 0x4000] == 0xAA);
            ok("base + 128K : la banque 6 vient de l'image", s5[256 + 6 * 0x4000] == 0xEE);
        }
        ok("base : le vecteur d'indirection survit", s2[256 + 0xBD00] == 0x55);
    }

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
