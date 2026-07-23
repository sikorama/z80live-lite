// parser_test.cpp - Tests bout-en-bout : texte -> parseur -> encodeur -> octets
#include "parser.h"
#include "z80.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

struct TestCtx : z80::IAsmContext {
    std::vector<uint8_t> out;
    uint16_t pc_ = 0;
    bool errored = false;
    std::string lastError;
    void emit(uint8_t b) override { out.push_back(b); }
    uint16_t pc() const override { return pc_; }
    void error(const std::string &msg) override { errored = true; lastError = msg; }
    int64_t eval(const std::string &e) override {
        // évaluateur minimal pour les tests : nombre, ou 'c'
        std::string s = e;
        size_t a = 0; while (a < s.size() && isspace((unsigned char)s[a])) ++a;
        if (a < s.size() && s[a] == '\'') return (unsigned char)s[a + 1];
        // gère un simple "x+y"/"x*y" pour les cas testés
        return evalSimple(s);
    }
    static int64_t evalSimple(const std::string &s) {
        // suffisant pour "(1+2)*3", "2+3", nombres $/0x/décimal
        size_t i = 0; return parseAdd(s, i);
    }
    static void skip(const std::string &s, size_t &i) { while (i < s.size() && isspace((unsigned char)s[i])) ++i; }
    static int64_t parseAdd(const std::string &s, size_t &i) {
        int64_t v = parseMul(s, i);
        for (;;) { skip(s, i); if (i < s.size() && s[i] == '+') { ++i; v += parseMul(s, i); }
                   else if (i < s.size() && s[i] == '-') { ++i; v -= parseMul(s, i); } else break; }
        return v;
    }
    static int64_t parseMul(const std::string &s, size_t &i) {
        int64_t v = parseAtom(s, i);
        for (;;) { skip(s, i); if (i < s.size() && s[i] == '*') { ++i; v *= parseAtom(s, i); } else break; }
        return v;
    }
    static int64_t parseAtom(const std::string &s, size_t &i) {
        skip(s, i);
        if (i < s.size() && s[i] == '(') { ++i; int64_t v = parseAdd(s, i); skip(s, i); if (i < s.size() && s[i] == ')') ++i; return v; }
        if (i < s.size() && (s[i] == '-')) { ++i; return -parseAtom(s, i); }
        int base = 10; size_t st;
        if (i + 1 < s.size() && s[i] == '0' && (s[i+1]=='x'||s[i+1]=='X')) { base=16; i+=2; }
        else if (i < s.size() && s[i]=='$') { base=16; ++i; }
        st = i; int64_t v = 0;
        while (i < s.size()) {
            char c = s[i]; int d;
            if (c>='0'&&c<='9') d=c-'0';
            else if (base==16 && ((c>='a'&&c<='f')||(c>='A'&&c<='F'))) d = tolower(c)-'a'+10;
            else break;
            v=v*base+d; ++i;
        }
        (void)st;
        return v;
    }
};

static int g_pass = 0, g_fail = 0;
static std::string hex(const std::vector<uint8_t> &v) {
    std::string s; char b[8];
    for (size_t i = 0; i < v.size(); ++i) { snprintf(b, sizeof b, "%02X", v[i]); if (i) s += ' '; s += b; }
    return s;
}

// Parse `line`, encode, compare aux octets attendus.
static void asmchk(const std::string &line, std::initializer_list<uint8_t> expected, uint16_t pc = 0) {
    parser::Result pr = parser::parseLine(line);
    std::vector<uint8_t> exp(expected);
    if (!pr.ok || !pr.isInstruction) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %-20s parse: %s\n", line.c_str(),
               pr.error.empty() ? "pas une instruction" : pr.error.c_str());
        return;
    }
    TestCtx ctx; ctx.pc_ = pc;
    bool ok = z80::encode(ctx, pr.instr);
    if (!ok || ctx.errored || ctx.out != exp) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %-20s attendu [%s] obtenu [%s] %s\n",
               line.c_str(), hex(exp).c_str(), hex(ctx.out).c_str(),
               ctx.errored ? ctx.lastError.c_str() : "");
    } else ++g_pass;
}

static void chkLabel(const std::string &line, const std::string &lbl, bool isInstr) {
    parser::Result pr = parser::parseLine(line);
    if (!pr.ok || pr.label != lbl || pr.isInstruction != isInstr) {
        ++g_fail; printf("  \033[31mFAIL\033[0m label %-24s label='%s' instr=%d\n",
                         line.c_str(), pr.label.c_str(), pr.isInstruction);
    } else ++g_pass;
}

int main() {
    printf("Tests parseur (texte -> octets)\n");

    // registres, casse insensible
    asmchk("ld a,b", {0x78});
    asmchk("LD A,B", {0x78});
    asmchk("  ld   h , l  ", {0x65});
    asmchk("nop", {0x00});
    asmchk("halt", {0x76});

    // immédiats vs mémoire (parenthèses englobantes)
    asmchk("ld a,0x40", {0x3E, 0x40});
    asmchk("ld a,(0x4000)", {0x3A, 0x00, 0x40});
    asmchk("ld (0x4000),a", {0x32, 0x00, 0x40});
    asmchk("ld a,(2+3)", {0x3A, 0x05, 0x00});     // mémoire, adresse 5
    asmchk("ld a,(1+2)*3", {0x3E, 0x09});          // immédiat 9 (pas englobant)
    asmchk("ld hl,0x1234", {0x21, 0x34, 0x12});
    asmchk("ld hl,(0x8000)", {0x2A, 0x00, 0x80});

    // indirections registre
    asmchk("ld a,(hl)", {0x7E});
    asmchk("ld (hl),a", {0x77});
    asmchk("ld a,(bc)", {0x0A});
    asmchk("ld a,(de)", {0x1A});

    // IX/IY : (IX), (IX+d), (IY-d)
    asmchk("ld a,(ix)", {0xDD, 0x7E, 0x00});
    asmchk("ld a,(ix+5)", {0xDD, 0x7E, 0x05});
    asmchk("ld b,(iy-1)", {0xFD, 0x46, 0xFF});
    asmchk("ld (ix+0),10", {0xDD, 0x36, 0x00, 0x0A});
    asmchk("inc (ix)", {0xDD, 0x34, 0x00});
    asmchk("ld ix,0x1234", {0xDD, 0x21, 0x34, 0x12});
    asmchk("add ix,de", {0xDD, 0x19});

    // désambiguïsation de C : registre vs condition
    asmchk("ld a,c", {0x79});        // C = registre
    asmchk("ld c,a", {0x4F});        // C = registre
    asmchk("add a,c", {0x81});       // C = registre
    asmchk("ret c", {0xD8});         // C = condition
    asmchk("ret nc", {0xD0});
    asmchk("ret", {0xC9});
    asmchk("jp c,0x4000", {0xDA, 0x00, 0x40});   // C = condition
    asmchk("jp nz,0x4000", {0xC2, 0x00, 0x40});
    asmchk("jp 0x4000", {0xC3, 0x00, 0x40});
    asmchk("jp (hl)", {0xE9});
    asmchk("jp (ix)", {0xDD, 0xE9});
    asmchk("call c,0x4000", {0xDC, 0x00, 0x40});
    asmchk("call 0x4000", {0xCD, 0x00, 0x40});

    // (C) port
    asmchk("in b,(c)", {0xED, 0x40});
    asmchk("out (c),d", {0xED, 0x51});
    asmchk("in a,(0xfe)", {0xDB, 0xFE});
    asmchk("out (0xfe),a", {0xD3, 0xFE});

    // rotations / bits
    asmchk("rlc b", {0xCB, 0x00});
    asmchk("bit 7,a", {0xCB, 0x7F});
    asmchk("set 0,(hl)", {0xCB, 0xC6});
    asmchk("res 3,(iy+2)", {0xFD, 0xCB, 0x02, 0x9E});

    // sauts relatifs (PC=0x4000)
    asmchk("jr 0x4002", {0x18, 0x00}, 0x4000);
    asmchk("jr nz,0x4010", {0x20, 0x0E}, 0x4000);
    asmchk("djnz 0x3ff0", {0x10, 0xEE}, 0x4000);

    // pile / échange / caractère
    asmchk("push bc", {0xC5});
    asmchk("push ix", {0xDD, 0xE5});
    asmchk("pop af", {0xF1});
    asmchk("ex af,af'", {0x08});
    asmchk("ex de,hl", {0xEB});
    asmchk("ex (sp),hl", {0xE3});
    asmchk("ld a,'A'", {0x3E, 0x41});
    asmchk("rst 0x38", {0xFF});
    asmchk("im 1", {0xED, 0x56});

    // non documentés
    asmchk("ld ixh,5", {0xDD, 0x26, 0x05});
    asmchk("add a,iyl", {0xFD, 0x85});
    asmchk("sll b", {0xCB, 0x30});

    // labels et directives (routage)
    chkLabel("loop: djnz loop", "loop", true);
    chkLabel("start:", "start", false);
    chkLabel("  nop", "", true);
    chkLabel("org 0x4000", "", false);     // directive -> non-instruction
    chkLabel("count: db 1,2,3", "count", false);

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
