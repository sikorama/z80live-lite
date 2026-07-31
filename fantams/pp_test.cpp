// pp_test.cpp - Tests du préprocesseur fantams
#include "pp.h"

#include <cstdio>
#include <map>
#include <string>

static int g_pass = 0, g_fail = 0;

// Fournisseur de fichiers en mémoire (pour INCLUDE).
static std::map<std::string, std::string> g_files;
static pp::FileProvider provider = [](const std::string &path, std::string &out) {
    auto it = g_files.find(path);
    if (it == g_files.end()) return false;
    out = it->second; return true;
};

// Vérifie que `src` se préprocesse exactement en `expected` (lignes jointes \n).
static void chk(const char *desc, const std::string &src, const std::string &expected) {
    pp::Result r = pp::preprocess(src, "test.asm", provider);
    std::string got = r.dump();
    if (!r.ok || got != expected) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s\n", desc);
        printf("    attendu:\n%s\n    obtenu:\n%s\n", expected.c_str(), got.c_str());
        for (auto &e : r.errors) printf("    err %s:%d %s\n", e.file.c_str(), e.line, e.message.c_str());
    } else ++g_pass;
}

// Vérifie qu'un préprocessing échoue (erreur attendue).
static void chkErr(const char *desc, const std::string &src) {
    pp::Result r = pp::preprocess(src, "test.asm", provider);
    if (r.ok) { ++g_fail; printf("  \033[31mFAIL\033[0m %s (aurait dû échouer)\n", desc); }
    else ++g_pass;
}

int main() {
    printf("Tests préprocesseur\n");

    // passe-plat + suppression commentaires/vides
    chk("passthrough", "  ld a,1  ; commentaire\n\n  ret\n", "ld a,1\nret\n");

    // variable PP + substitution
    chk("LET + subst", "LET N = 3\n  ld a,{N}\n", "ld a,3\n");
    chk("subst expr", "LET N = 3\n  ld a,{=N*2+1}\n", "ld a,7\n");

    // REPEAT avec index (0-based)
    chk("REPEAT", "REPEAT 3, i\n  ld a,{i}\nREND\n", "ld a,0\nld a,1\nld a,2\n");
    chk("REPEAT expr count", "LET n=2\nREPEAT n\n  nop\nREND\n", "nop\nnop\n");

    // IF / ELSE / ELSEIF (PP-strict)
    chk("IF vrai", "LET D=1\nIF D\n  ld a,1\nELSE\n  ld a,2\nENDIF\n", "ld a,1\n");
    chk("IF faux", "LET D=0\nIF D\n  ld a,1\nELSE\n  ld a,2\nENDIF\n", "ld a,2\n");
    chk("ELSEIF", "LET X=2\nIF X==1\n a\nELSEIF X==2\n b\nELSE\n c\nENDIF\n", "b\n");
    chk("IFDEF", "LET FOO=0\nIFDEF FOO\n yes\nENDIF\nIFDEF BAR\n no\nENDIF\n", "yes\n");
    chk("IF imbriqué", "LET A=1\nLET B=0\nIF A\nIF B\n x\nELSE\n y\nENDIF\nENDIF\n", "y\n");

    // MACRO simple + paramètre
    chk("MACRO param",
        "MACRO SETA val\n  ld a,{val}\nENDM\n  SETA 42\n  SETA 7\n",
        "ld a,42\nld a,7\n");

    // MACRO forme "name MACRO"
    chk("MACRO forme name",
        "ADDXY MACRO x,y\n  ld hl,{=x+y}\nENDM\n  ADDXY 10,20\n",
        "ld hl,30\n");

    // auto-local : le label est unique par invocation
    chk("MACRO auto-local",
        "MACRO DELAY\nloop: djnz loop\nENDM\n  DELAY\n  DELAY\n",
        "loop__1: djnz loop__1\nloop__2: djnz loop__2\n");

    // @@export : le label reste global
    chk("MACRO export",
        "MACRO M\n@@export total\ntotal: nop\nloc: nop\nENDM\n  M\n",
        "total: nop\nloc__1: nop\n");

    // REPEAT : labels uniques par itération
    chk("REPEAT auto-local",
        "REPEAT 2\nlab: nop\nREND\n",
        "lab__1: nop\nlab__2: nop\n");

    // WHILE piloté par variable PP
    chk("WHILE",
        "LET i=0\nWHILE i<3\n  db {i}\n  LET i = i+1\nWEND\n",
        "db 0\ndb 1\ndb 2\n");

    // INCLUDE
    g_files["lib.asm"] = "  inc a\n  inc b\n";
    chk("INCLUDE", "  nop\n  INCLUDE \"lib.asm\"\n  ret\n", "nop\ninc a\ninc b\nret\n");

    // macro définie dans un include, utilisée après
    g_files["mac.asm"] = "MACRO ZERO\n  xor a\nENDM\n";
    chk("INCLUDE macro",
        "  INCLUDE \"mac.asm\"\n  ZERO\n", "xor a\n");

    // label devant une directive de bloc
    chk("label + REPEAT",
        "start: REPEAT 2\n nop\nREND\n", "start:\nnop\nnop\n");

    // MODULE : préfixe les labels définis, laisse les globaux (fallback)
    chk("MODULE prefix",
        "MODULE vid\nclear: ld hl,buf\n  ret\nbuf: db 0\nENDMODULE\n  call vid_clear\n",
        "vid_clear: ld hl,vid_buf\nret\nvid_buf: db 0\ncall vid_clear\n");
    chk("MODULE fallback global",
        "MODULE m\nfoo: call ext\nENDMODULE\n",
        "m_foo: call ext\n");
    chk("MODULE imbriqué",
        "MODULE a\nMODULE b\nx: nop\nENDMODULE\ny: jp x\nENDMODULE\n",
        "a_b_x: nop\na_y: jp x\n"); // x défini dans b, non visible au niveau a -> reste global

    // STRUCT : déclaration -> offsets + sizeof (EQU portables)
    chk("STRUCT decl",
        "STRUCT Point\nx db 0\ny db 0\nw dw 0\nENDSTRUCT\n",
        "Point.x EQU 0\nPoint.y EQU 1\nPoint.w EQU 2\nPoint EQU 4\n");
    chk("STRUCT tailles DS/chaine",
        "STRUCT T\nname db \"AB\"\nbuf ds 4\nk dw 0\nENDSTRUCT\n",
        "T.name EQU 0\nT.buf EQU 2\nT.k EQU 6\nT EQU 8\n");

    // STRUCT : instanciation (STRUCT type instance)
    chk("STRUCT instance",
        "STRUCT Point\nx db 0\ny db 0\nENDSTRUCT\nSTRUCT Point p1\n",
        "Point.x EQU 0\nPoint.y EQU 1\nPoint EQU 2\n"
        "p1:\np1.x EQU p1+0\np1.y EQU p1+1\nDB 0\nDB 0\n");
    chk("STRUCT instance override",
        "STRUCT P\nv db 0\nENDSTRUCT\nSTRUCT P inst 0xFF\n",
        "P.v EQU 0\nP EQU 1\ninst:\ninst.v EQU inst+0\nDB 0xFF\n");

    // STRUCT imbriquée
    chk("STRUCT imbriquée",
        "STRUCT Point\nx db 0\ny db 0\nENDSTRUCT\n"
        "STRUCT Line\na Point\nb Point\nENDSTRUCT\n",
        "Point.x EQU 0\nPoint.y EQU 1\nPoint EQU 2\n"
        "Line.a EQU 0\nLine.a.x EQU 0\nLine.a.y EQU 1\n"
        "Line.b EQU 2\nLine.b.x EQU 2\nLine.b.y EQU 3\nLine EQU 4\n");

    // séparateur d'instructions ':' -> retour à la ligne (+ tabulation)
    chk("colon sep", "  ld a,1 : ld b,2 : ret\n", "ld a,1\n\tld b,2\n\tret\n");
    chk("colon garde label collé", "start: di : ret\n", "start: di\n\tret\n");
    chk("colon mnémo 0-op", "nop : ret\n", "nop\n\tret\n");
    chk("colon dans chaîne protégé", "  db \"a:b\" : nop\n", "db \"a:b\"\n\tnop\n");
    chk("colon dans (ix+d)", "  ld a,(ix+0) : ret\n", "ld a,(ix+0)\n\tret\n");

    // push/pop multi-registres -> une instruction par registre
    chk("push multi", "  push af,bc,de\n", "push af\n\tpush bc\n\tpush de\n");
    chk("pop multi + label", "lbl: pop hl,de\n", "lbl: pop hl\n\tpop de\n");
    chk("push multi + colon", "  push af,bc : ret\n", "push af\n\tpush bc\n\tret\n");

    // --- erreurs (PP strict) ---
    chkErr("IF sur label temps-assemblage", "IF taille>0x4000\n nop\nENDIF\n");
    chkErr("mauvais nb d'args macro", "MACRO M a,b\n nop\nENDM\n M 1\n");
    chkErr("REPEAT sans REND", "REPEAT 3\n nop\n");
    chkErr("include manquant", "  INCLUDE \"absent.asm\"\n");
    chkErr("subst inconnue", "  ld a,{inconnu}\n");

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
