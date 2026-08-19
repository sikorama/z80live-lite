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

// Vérifie le résultat ET la présence (ou non) d'un avertissement.
static void chkWarn(const char *desc, const std::string &src, const std::string &expected, bool expectWarning) {
    pp::Result r = pp::preprocess(src, "test.asm", provider);
    std::string got = r.dump();
    bool hasWarn = !r.warnings.empty();
    if (!r.ok || got != expected || hasWarn != expectWarning) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s : ok=%d warnings=%zu (attendu=%d)\n", desc, r.ok, r.warnings.size(), expectWarning);
        printf("    attendu:\n%s\n    obtenu:\n%s\n", expected.c_str(), got.c_str());
    } else ++g_pass;
}

int main() {
    printf("Tests préprocesseur\n");

    // passe-plat + suppression commentaires/vides
    chk("passthrough", "  ld a,1  ; commentaire\n\n  ret\n", "ld a,1\nret\n");

    // commentaires bloc /* ... */ (comme rasm), y compris multi-lignes et sur une ligne de code
    chk("commentaire bloc simple", "/* commentaire */\n  ld a,1\n", "ld a,1\n");
    chk("commentaire bloc multi-lignes", "/* ligne1\nligne2\nligne3 */\n  ld a,1\n", "ld a,1\n");
    chk("commentaire bloc + code sur la même ligne", "/* c */ ld a,1\n", "ld a,1\n");

    // variable PP + substitution
    chk("LET + subst", "LET N = 3\n  ld a,{N}\n", "ld a,3\n");
    chk("subst expr", "LET N = 3\n  ld a,{=N*2+1}\n", "ld a,7\n");

    // REPEAT avec index (1-based, comme rasm : {i} vaut 1 à la 1re itération)
    chk("REPEAT", "REPEAT 3, i\n  ld a,{i}\nREND\n", "ld a,1\nld a,2\nld a,3\n");
    chk("REPEAT expr count", "LET n=2\nREPEAT n\n  nop\nREND\n", "nop\nnop\n");

    // Variables PP / compteurs de boucle écrits en clair (sans {}) : rasm les
    // expose comme des symboles ordinaires, fantams les substitue textuellement.
    chk("compteur REPEAT en clair", "REPEAT 3, i\n  db i*2\nREND\n", "db 1*2\ndb 2*2\ndb 3*2\n");
    chk("LET en clair", "LET v=7\n db v+1\n", "db 7+1\n");
    chk("substitution hors chaînes/caractères",
        "LET y=3\n db y, \"y\", 'y'\n", "db 3, \"y\", 'y'\n");
    chk("registres jamais substitués", "LET b=3\n ld a,b\n", "ld a,b\n");
    chk("registre 1er opérande", "LET a=3\n ld a,#10\n", "ld a,#10\n");
    chk("registre indirect", "LET hl=3\n ld (hl),a\n", "ld (hl),a\n");
    chk("condition de saut", "LET z=3\n jr z,#100\n", "jr z,#100\n");
    chk("registre dans une sous-expression", "LET i=5\n ld a,(tbl+i)\n", "ld a,(tbl+5)\n");
    chk("registre après directive", "LET i=5\n db i\n", "db 5\n");
    chk("label jamais substitué", "LET v=1\nv: nop\n", "v: nop\n");
    chk("pas de substitution dans un nombre hexa", "LET f=1\n db #ff\n", "db #ff\n");

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

    // auto-local : seul un label préfixé par '@' est unique par invocation (convention
    // rasm) ; un label ordinaire n'est PAS renommé (une vraie collision, détectée par
    // l'assembleur si réutilisé, reste possible — comme chez rasm).
    chk("MACRO auto-local (@ préfixé)",
        "MACRO DELAY\n@loop: djnz @loop\nENDM\n  DELAY\n  DELAY\n",
        "@loop__1: djnz @loop__1\n@loop__2: djnz @loop__2\n");
    chk("MACRO label ordinaire non renommé",
        "MACRO DELAY\nloop: djnz loop\nENDM\n  DELAY\n  DELAY\n",
        "loop: djnz loop\nloop: djnz loop\n");

    // @@export : un label @ reste global malgré le préfixe
    chk("MACRO export",
        "MACRO M\n@@export @total\n@total: nop\n@loc: nop\nENDM\n  M\n",
        "@total: nop\n@loc__1: nop\n");

    // REPEAT : seuls les labels @ sont uniques par itération
    chk("REPEAT auto-local (@ préfixé)",
        "REPEAT 2\n@lab: nop\nREND\n",
        "@lab__1: nop\n@lab__2: nop\n");
    chk("REPEAT label ordinaire non renommé",
        "REPEAT 2\nlab: nop\nREND\n",
        "lab: nop\nlab: nop\n");

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

    // MODULE : préfixe les labels définis avec '.' (PAS '_' comme rasm — délibérément
    // incompatible : '_' est un caractère d'identifiant ordinaire donc ambigu, alors que
    // '.' est déjà le séparateur des labels locaux ; ça permet de chaîner "module.label.local"
    // sans mécanisme séparé). Laisse les globaux non définis dans le module en fallback.
    chk("MODULE prefix",
        "MODULE vid\nclear: ld hl,buf\n  ret\nbuf: db 0\nENDMODULE\n  call vid.clear\n",
        "vid.clear: ld hl,vid.buf\nret\nvid.buf: db 0\ncall vid.clear\n");
    chk("MODULE fallback global",
        "MODULE m\nfoo: call ext\nENDMODULE\n",
        "m.foo: call ext\n");
    // rasm ne cumule PAS les MODULE : "MODULE b" remplace "MODULE a" (pas de préfixe a.b.).
    chk("MODULE switch (pas de nesting)",
        "MODULE a\nz: nop\nMODULE b\nx: nop\nENDMODULE\ny: jp x\n",
        "a.z: nop\nb.x: nop\ny: jp x\n"); // y est hors module -> x (non renommé) reste global
    chk("MODULE OFF",
        "MODULE a\nz: nop\nMODULE OFF\ny: nop\n",
        "a.z: nop\ny: nop\n");
    // label sans ':' dans un MODULE (comme un vrai bout de source rasm réel)
    chk("MODULE label sans ':'",
        "MODULE icons\ndisplay\n  ret\nMODULE OFF\n  call icons.display\n",
        "icons.display\nret\ncall icons.display\n");
    // label local ".nom" dans un MODULE : pas renommé par MODULE lui-même (laissé à
    // asm.cpp), mais le préprocesseur ne doit pas le casser -> passe-plat tel quel ici.
    chk("MODULE + label local (préprocesseur : passe-plat)",
        "MODULE icons\ndisplay:\n.loop:\n  djnz .loop\n  ret\nMODULE OFF\n",
        "icons.display:\n.loop:\ndjnz .loop\nret\n");

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

    // "mnémo:mnémo" collé (style non canonique, mais rasm le découpe quand même en 2
    // instructions car "ei"/"ret" sont des mnémos connus, pas des labels) -> avertissement.
    chkWarn("mnémo:mnémo collé", "ei:ret\n", "ei\n\tret\n", true);
    chkWarn("mnémo: mnémo (espace après)", "ei: ret\n", "ei\n\tret\n", true);
    chkWarn("mnémo:mnémo inversé", "ret:ei\n", "ret\n\tei\n", true);
    chkWarn("mnémo : mnémo (espace avant, pas d'avertissement)", "ret : ei\n", "ret\n\tei\n", false);

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
