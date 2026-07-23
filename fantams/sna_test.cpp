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

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
