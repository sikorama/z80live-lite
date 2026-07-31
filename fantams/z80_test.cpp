// z80_test.cpp - Tests d'encodage de l'encodeur Z80 (fantams)
//
// Contexte factice : capture les octets, évalue une expression = simple nombre
// (décimal, 0x.. ou $..), fournit un PC fixe pour l'adressage relatif.
#include "z80.h"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <vector>

using namespace z80;

struct TestCtx : IAsmContext {
    std::vector<uint8_t> out;
    uint16_t pc_ = 0;
    bool errored = false;
    std::string lastError;

    void emit(uint8_t b) override { out.push_back(b); }
    uint16_t pc() const override { return pc_; }
    void error(const std::string &msg) override { errored = true; lastError = msg; }
    int64_t eval(const std::string &expr) override {
        // suffit pour les tests : nombre décimal, 0x.. ou $..
        if (!expr.empty() && expr[0] == '$') return strtoll(expr.c_str() + 1, nullptr, 16);
        return strtoll(expr.c_str(), nullptr, 0);
    }
};

static int g_pass = 0, g_fail = 0;

static std::string hex(const std::vector<uint8_t> &v) {
    std::string s;
    char buf[8];
    for (size_t i = 0; i < v.size(); ++i) {
        snprintf(buf, sizeof buf, "%02X", v[i]);
        if (i) s += ' ';
        s += buf;
    }
    return s;
}

// Vérifie qu'une instruction s'encode exactement en `expected`.
static void chk(const char *desc, const Instruction &in,
                std::initializer_list<uint8_t> expected, uint16_t pc = 0) {
    TestCtx ctx; ctx.pc_ = pc;
    bool ok = encode(ctx, in);
    std::vector<uint8_t> exp(expected);
    if (!ok || ctx.errored || ctx.out != exp) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %-22s attendu [%s] obtenu [%s]%s\n",
               desc, hex(exp).c_str(), hex(ctx.out).c_str(),
               ctx.errored ? (" err: " + ctx.lastError).c_str() : "");
    } else {
        ++g_pass;
    }
}

// Vérifie qu'une combinaison invalide est bien rejetée.
static void chkErr(const char *desc, const Instruction &in) {
    TestCtx ctx;
    bool ok = encode(ctx, in);
    if (ok && !ctx.errored) { ++g_fail; printf("  \033[31mFAIL\033[0m %-22s aurait dû être rejeté\n", desc); }
    else ++g_pass;
}

// raccourcis
static Operand R(Reg r) { return Operand::r(r); }
static Operand RI(Reg r) { return Operand::rind(r); }
static Operand IDX(Reg xy, const char *d) { return Operand::idx(xy, d); }
static Operand N(const char *e) { return Operand::imm(e); }
static Operand M(const char *e) { return Operand::mem(e); }
static Operand CC(Cond c) { return Operand::condition(c); }
static Instruction I(Mnemo m, Operand a = Operand::none(), Operand b = Operand::none()) { return {m, a, b}; }

int main() {
    printf("Tests encodeur Z80\n");

    // --- implicites ---
    chk("NOP",  I(Mnemo::NOP),  {0x00});
    chk("HALT", I(Mnemo::HALT), {0x76});
    chk("DI",   I(Mnemo::DI),   {0xF3});
    chk("EXX",  I(Mnemo::EXX),  {0xD9});
    chk("LDIR", I(Mnemo::LDIR), {0xED, 0xB0});
    chk("NEG",  I(Mnemo::NEG),  {0xED, 0x44});
    chk("RRD",  I(Mnemo::RRD),  {0xED, 0x67});

    // --- LD r,r' ---
    chk("LD A,B",     I(Mnemo::LD, R(Reg::A), R(Reg::B)),   {0x78});
    chk("LD B,C",     I(Mnemo::LD, R(Reg::B), R(Reg::C)),   {0x41});
    chk("LD H,(HL)",  I(Mnemo::LD, R(Reg::H), RI(Reg::HL)), {0x66});
    chk("LD (HL),A",  I(Mnemo::LD, RI(Reg::HL), R(Reg::A)), {0x77});
    chkErr("LD (HL),(HL)", I(Mnemo::LD, RI(Reg::HL), RI(Reg::HL)));

    // --- LD r,n / rr,nn ---
    chk("LD A,12",    I(Mnemo::LD, R(Reg::A), N("12")),     {0x3E, 0x0C});
    chk("LD (HL),0",  I(Mnemo::LD, RI(Reg::HL), N("0")),    {0x36, 0x00});
    chk("LD BC,0x1234", I(Mnemo::LD, R(Reg::BC), N("0x1234")), {0x01, 0x34, 0x12});
    chk("LD SP,0xC000", I(Mnemo::LD, R(Reg::SP), N("0xC000")), {0x31, 0x00, 0xC0});

    // --- LD accumulateur / mémoire ---
    chk("LD A,(BC)",  I(Mnemo::LD, R(Reg::A), RI(Reg::BC)), {0x0A});
    chk("LD A,(DE)",  I(Mnemo::LD, R(Reg::A), RI(Reg::DE)), {0x1A});
    chk("LD A,(nn)",  I(Mnemo::LD, R(Reg::A), M("0x4000")), {0x3A, 0x00, 0x40});
    chk("LD (nn),A",  I(Mnemo::LD, M("0x4000"), R(Reg::A)), {0x32, 0x00, 0x40});
    chk("LD HL,(nn)", I(Mnemo::LD, R(Reg::HL), M("0x8000")), {0x2A, 0x00, 0x80});
    chk("LD (nn),HL", I(Mnemo::LD, M("0x8000"), R(Reg::HL)), {0x22, 0x00, 0x80});
    chk("LD (nn),BC", I(Mnemo::LD, M("0x9000"), R(Reg::BC)), {0xED, 0x43, 0x00, 0x90});
    chk("LD DE,(nn)", I(Mnemo::LD, R(Reg::DE), M("0x9000")), {0xED, 0x5B, 0x00, 0x90});
    chk("LD SP,HL",   I(Mnemo::LD, R(Reg::SP), R(Reg::HL)), {0xF9});
    chk("LD A,I",     I(Mnemo::LD, R(Reg::A), R(Reg::I)),   {0xED, 0x57});
    chk("LD R,A",     I(Mnemo::LD, R(Reg::R), R(Reg::A)),   {0xED, 0x4F});

    // --- IX / IY ---
    chk("LD IX,0x1234", I(Mnemo::LD, R(Reg::IX), N("0x1234")), {0xDD, 0x21, 0x34, 0x12});
    chk("LD (nn),IY",   I(Mnemo::LD, M("0x8000"), R(Reg::IY)), {0xFD, 0x22, 0x00, 0x80});
    chk("LD SP,IX",     I(Mnemo::LD, R(Reg::SP), R(Reg::IX)),  {0xDD, 0xF9});
    chk("LD (IX+5),A",  I(Mnemo::LD, IDX(Reg::IX, "5"), R(Reg::A)), {0xDD, 0x77, 0x05});
    chk("LD B,(IY-1)",  I(Mnemo::LD, R(Reg::B), IDX(Reg::IY, "-1")), {0xFD, 0x46, 0xFF});
    chk("LD (IX+0),10", I(Mnemo::LD, IDX(Reg::IX, "0"), N("10")), {0xDD, 0x36, 0x00, 0x0A});

    // --- ALU 8 bits ---
    chk("ADD A,B",    I(Mnemo::ADD, R(Reg::A), R(Reg::B)),  {0x80});
    chk("ADD A,(HL)", I(Mnemo::ADD, R(Reg::A), RI(Reg::HL)),{0x86});
    chk("ADC A,5",    I(Mnemo::ADC, R(Reg::A), N("5")),     {0xCE, 0x05});
    chk("SUB C",      I(Mnemo::SUB, R(Reg::C)),             {0x91});
    chk("AND 0x0F",   I(Mnemo::AND, N("0x0F")),             {0xE6, 0x0F});
    chk("XOR A",      I(Mnemo::XOR, R(Reg::A)),             {0xAF});
    chk("CP (IX+2)",  I(Mnemo::CP, IDX(Reg::IX, "2")),      {0xDD, 0xBE, 0x02});

    // --- ADD 16 bits ---
    chk("ADD HL,BC",  I(Mnemo::ADD, R(Reg::HL), R(Reg::BC)), {0x09});
    chk("ADD HL,SP",  I(Mnemo::ADD, R(Reg::HL), R(Reg::SP)), {0x39});
    chk("ADD IX,DE",  I(Mnemo::ADD, R(Reg::IX), R(Reg::DE)), {0xDD, 0x19});
    chk("ADD IX,IX",  I(Mnemo::ADD, R(Reg::IX), R(Reg::IX)), {0xDD, 0x29});
    chk("ADC HL,BC",  I(Mnemo::ADC, R(Reg::HL), R(Reg::BC)), {0xED, 0x4A});
    chk("SBC HL,DE",  I(Mnemo::SBC, R(Reg::HL), R(Reg::DE)), {0xED, 0x52});
    chkErr("ADD IX,HL", I(Mnemo::ADD, R(Reg::IX), R(Reg::HL)));

    // --- INC / DEC ---
    chk("INC A",      I(Mnemo::INC, R(Reg::A)),   {0x3C});
    chk("INC (HL)",   I(Mnemo::INC, RI(Reg::HL)), {0x34});
    chk("INC BC",     I(Mnemo::INC, R(Reg::BC)),  {0x03});
    chk("DEC IX",     I(Mnemo::DEC, R(Reg::IX)),  {0xDD, 0x2B});
    chk("DEC (IY+3)", I(Mnemo::DEC, IDX(Reg::IY, "3")), {0xFD, 0x35, 0x03});

    // --- rotations / bits ---
    chk("RLC B",      I(Mnemo::RLC, R(Reg::B)),   {0xCB, 0x00});
    chk("SRL A",      I(Mnemo::SRL, R(Reg::A)),   {0xCB, 0x3F});
    chk("RL (HL)",    I(Mnemo::RL, RI(Reg::HL)),  {0xCB, 0x16});
    chk("RLC (IX+1)", I(Mnemo::RLC, IDX(Reg::IX, "1")), {0xDD, 0xCB, 0x01, 0x06});
    chk("BIT 7,A",    I(Mnemo::BIT, N("7"), R(Reg::A)),  {0xCB, 0x7F});
    chk("SET 0,(HL)", I(Mnemo::SET, N("0"), RI(Reg::HL)),{0xCB, 0xC6});
    chk("RES 3,(IY+2)", I(Mnemo::RES, N("3"), IDX(Reg::IY, "2")), {0xFD, 0xCB, 0x02, 0x9E});

    // --- push / pop ---
    chk("PUSH BC",    I(Mnemo::PUSH, R(Reg::BC)), {0xC5});
    chk("POP AF",     I(Mnemo::POP, R(Reg::AF)),  {0xF1});
    chk("PUSH IX",    I(Mnemo::PUSH, R(Reg::IX)), {0xDD, 0xE5});

    // --- saut / appel / retour ---
    chk("JP nn",      I(Mnemo::JP, N("0x4000")),  {0xC3, 0x00, 0x40});
    chk("JP Z,nn",    I(Mnemo::JP, CC(Cond::Z), N("0x4000")), {0xCA, 0x00, 0x40});
    chk("JP (HL)",    I(Mnemo::JP, RI(Reg::HL)),  {0xE9});
    chk("JP (IX)",    I(Mnemo::JP, RI(Reg::IX)),  {0xDD, 0xE9});
    chk("CALL nn",    I(Mnemo::CALL, N("0x4000")),{0xCD, 0x00, 0x40});
    chk("CALL NZ,nn", I(Mnemo::CALL, CC(Cond::NZ), N("0x4000")), {0xC4, 0x00, 0x40});
    chk("RET",        I(Mnemo::RET),              {0xC9});
    chk("RET C",      I(Mnemo::RET, CC(Cond::C)), {0xD8});
    chk("RST 0x38",   I(Mnemo::RST, N("0x38")),   {0xFF});
    chk("IM 1",       I(Mnemo::IM, N("1")),       {0xED, 0x56});

    // --- sauts relatifs (PC = 0x4000) ---
    chk("JR +2",      I(Mnemo::JR, N("0x4002")),  {0x18, 0x00}, 0x4000);
    chk("JR back",    I(Mnemo::JR, N("0x3FFE")),  {0x18, 0xFC}, 0x4000); // -4
    chk("JR NZ,fwd",  I(Mnemo::JR, CC(Cond::NZ), N("0x4010")), {0x20, 0x0E}, 0x4000);
    chk("DJNZ back",  I(Mnemo::DJNZ, N("0x3FF0")),{0x10, 0xEE}, 0x4000); // -16
    chkErr("JR trop loin", I(Mnemo::JR, N("0x5000")));
    chkErr("JR PO (invalide)", I(Mnemo::JR, CC(Cond::PO), N("0x4000")));

    // --- EX / IN / OUT ---
    chk("EX AF,AF'",  I(Mnemo::EX, R(Reg::AF), R(Reg::AFp)), {0x08});
    chk("EX DE,HL",   I(Mnemo::EX, R(Reg::DE), R(Reg::HL)),  {0xEB});
    chk("EX (SP),HL", I(Mnemo::EX, RI(Reg::SP), R(Reg::HL)), {0xE3});
    chk("EX (SP),IX", I(Mnemo::EX, RI(Reg::SP), R(Reg::IX)), {0xDD, 0xE3});
    chk("IN A,(n)",   I(Mnemo::IN, R(Reg::A), M("0xFE")),    {0xDB, 0xFE});
    chk("IN B,(C)",   I(Mnemo::IN, R(Reg::B), RI(Reg::C)),   {0xED, 0x40});
    chk("OUT (n),A",  I(Mnemo::OUT, M("0xFE"), R(Reg::A)),   {0xD3, 0xFE});
    chk("OUT (C),D",  I(Mnemo::OUT, RI(Reg::C), R(Reg::D)),  {0xED, 0x51});

    // --- non documentés fréquents ---
    chk("SLL B",      I(Mnemo::SLL, R(Reg::B)),   {0xCB, 0x30});
    chk("LD IXH,5",   I(Mnemo::LD, R(Reg::IXH), N("5")), {0xDD, 0x26, 0x05});
    chk("ADD A,IYL",  I(Mnemo::ADD, R(Reg::A), R(Reg::IYL)), {0xFD, 0x85});

    // --- résolution nom -> mnémonique ---
    if (mnemoFromString("LD") != Mnemo::LD) { ++g_fail; printf("  \033[31mFAIL\033[0m mnemoFromString LD\n"); } else ++g_pass;
    if (mnemoFromString("ZZZ") != Mnemo::Invalid) { ++g_fail; printf("  \033[31mFAIL\033[0m mnemoFromString ZZZ\n"); } else ++g_pass;

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
