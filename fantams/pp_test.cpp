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

// Vérifie qu'un source est refusé — ou accepté — en mode strict (ADR 0017).
static void chkStrict(const char *desc, const std::string &src, bool shouldPass) {
    pp::Result r = pp::preprocess(src, "test.asm", provider, /*strict=*/true);
    if (r.ok != shouldPass) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s (ok=%d attendu=%d)\n", desc, r.ok, shouldPass);
        for (auto &e : r.errors) printf("    err %s:%d %s\n", e.file.c_str(), e.line, e.message.c_str());
    } else ++g_pass;
}

// Vérifie que `--normalize` rend exactement `expected` (ADR 0017).
static void chkNorm(const char *desc, const std::string &src, const std::string &expected) {
    std::string got = pp::normalize(src);
    if (got != expected) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s\n    attendu:\n%s\n    obtenu:\n%s\n",
               desc, expected.c_str(), got.c_str());
    } else ++g_pass;
}

// Les deux propriétés qui font de `--normalize` un outil : il est idempotent, et
// l'assemblage de son résultat rend les mêmes octets (ADR 0017). La seconde est
// vérifiée en comparant les sources déroulées, l'assembleur n'étant pas lié ici.
static void chkNormStable(const char *desc, const std::string &src) {
    const std::string once = pp::normalize(src);
    const bool idem = pp::normalize(once) == once;
    // Les deux sources déroulées sont comparées LIGNE À LIGNE TRIMÉE : quand le PP
    // coupe lui-même « push hl,de », il indente ses sous-lignes de quatre espaces
    // (emit), ce que la source déjà canonisée n'a pas à faire. Le code est le même,
    // l'indentation non — et c'est l'égalité des octets qui compte, vérifiée avec
    // l'assembleur dans beautify_test.
    pp::Result a = pp::preprocess(src, "test.asm", provider);
    pp::Result b = pp::preprocess(once, "test.asm", provider);
    auto trimmedLines = [](const pp::Result &r) {
        std::string out;
        for (const auto &l : r.lines) {
            size_t x = l.text.find_first_not_of(" \t"), y = l.text.find_last_not_of(" \t");
            out += (x == std::string::npos ? "" : l.text.substr(x, y - x + 1)) + "\n";
        }
        return out;
    };
    const bool same = a.ok && b.ok && trimmedLines(a) == trimmedLines(b);
    if (!idem || !same) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s (idempotent=%d meme source deroulee=%d)\n", desc, idem, same);
        if (!same) printf("    avant:\n%s\n    apres:\n%s\n", trimmedLines(a).c_str(), trimmedLines(b).c_str());
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

// Compte les avertissements d'un source (ADR 0014 : le repli sur le texte).
static void chkWarnCount(const char *desc, const std::string &src, size_t expected) {
    pp::Result r = pp::preprocess(src, "test.asm", provider);
    if (r.warnings.size() != expected) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s : %zu avertissement(s), attendu %zu\n",
               desc, r.warnings.size(), expected);
        for (auto &w : r.warnings) printf("    warn %s:%d %s\n", w.file.c_str(), w.line, w.message.c_str());
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
    chk("REPEAT", "REPEAT 3, k\n  ld a,{k}\nREND\n", "ld a,0\nld a,1\nld a,2\n");
    chkErr("registre en index de boucle", "REPEAT 3, i\n  ld a,{i}\nREND\n");
    chk("REPEAT expr count", "LET n=2\nREPEAT n\n  nop\nREND\n", "nop\nnop\n");

    // Variables PP / compteurs de boucle écrits en clair (sans {}) : rasm les
    // expose comme des symboles ordinaires, fantams les substitue textuellement.
    chk("compteur REPEAT en clair", "REPEAT 3, k\n  db k*2\nREND\n", "db 0*2\ndb 1*2\ndb 2*2\n");
    chk("LET en clair", "LET v=7\n db v+1\n", "db 7+1\n");
    chk("substitution hors chaînes/caractères",
        "LET y=3\n db y, \"y\", 'y'\n", "db 3, \"y\", 'y'\n");
    chkErr("registre en nom de variable PP", "LET b=3\n ld a,b\n");
    chkErr("registre en nom de variable PP (1er operande)", "LET a=3\n ld a,#10\n");
    chkErr("paire de registres en nom de variable PP", "LET hl=3\n ld (hl),a\n");
    chkErr("condition en nom de variable PP", "LET z=3\n jr z,#100\n");
    chk("variable PP dans une sous-expression", "LET n=5\n ld a,(tbl+n)\n", "ld a,(tbl+5)\n");
    chkErr("registre I en nom de variable PP", "LET i=5\n ld a,(tbl+i)\n");
    chk("variable PP après directive", "LET n=5\n db n\n", "db 5\n");
    chk("label jamais substitué", "LET v=1\nv: nop\n", "v: nop\n");
    chk("pas de substitution dans un nombre hexa", "LET f=1\n db #ff\n", "db #ff\n");

    // IF / ELSE / ELSEIF (PP-strict)
    chk("IF vrai", "LET FLAG=1\nIF FLAG\n  ld a,1\nELSE\n  ld a,2\nENDIF\n", "ld a,1\n");
    chk("IF faux", "LET FLAG=0\nIF FLAG\n  ld a,1\nELSE\n  ld a,2\nENDIF\n", "ld a,2\n");
    chk("ELSEIF", "LET X=2\nIF X==1\n a\nELSEIF X==2\n b\nELSE\n c\nENDIF\n", "b\n");
    chk("IFDEF", "LET FOO=0\nIFDEF FOO\n yes\nENDIF\nIFDEF BAR\n no\nENDIF\n", "yes\n");
    chk("IF imbriqué", "LET ONE=1\nLET ZERO=0\nIF ONE\nIF ZERO\n x\nELSE\n y\nENDIF\nENDIF\n", "y\n");

    // --- ADR 0017 : --normalize, canoniser sans dérouler --------------------
    chkNorm("orthographes obsoletes", " defb 1,2\n", " db 1,2\n");
    chkNorm("fermetures obsoletes", "macro m\n nop\nmend\n", "macro m\n nop\nendmacro\n");
    chkNorm("casse epousee", " DEFW 3\n", " DW 3\n");
    chkNorm("push multi-registres", " push hl,de\n", " push hl\n push de\n");
    chkNorm("deux opcodes sur une ligne", " ld a,1: inc a\n", " ld a,1\n inc a\n");
    chkNorm("label conserve, suite indentee", "s: ld a,1: inc a\n", "s: ld a,1\n    inc a\n");
    chkNorm("commentaire suit la premiere", " push hl,de ; ctx\n", " push hl ; ctx\n push de\n");
    chkNorm("ligne a substitution laissee telle quelle", " push {r}\n", " push {r}\n");
    chkNorm("macros et boucles NON deroulees",
            "macro m\n nop\nendmacro\n repeat 2\n nop\n endrepeat\n",
            "macro m\n nop\nendmacro\n repeat 2\n nop\n endrepeat\n");
    chkNormStable("idempotent sur du sucre", " push hl,de ; ctx\ns: ld a,1: inc a\n defb 1\n");
    chkNormStable("idempotent sur du canonique", "start:\n    ld a,1\n    ret\n");

    // Le mode strict n'ajoute rien, il refuse.
    chkStrict("strict refuse le push multi-registres", " push hl,de\n", false);
    chkStrict("strict refuse deux opcodes sur une ligne", " ld a,1: inc a\n", false);
    chkStrict("strict refuse une orthographe obsolete", " defb 1\n", false);
    chkStrict("strict accepte le canonique", " push hl\n push de\n db 1\n", true);
    chkStrict("strict laisse passer macros et boucles",
              "macro m\n nop\nendmacro\n repeat 2\n m\n endrepeat\n", true);
    // Le sucre normalisé passe le mode strict : c'est la promesse de --normalize.
    chkStrict("normalize rend une source strict-propre",
              pp::normalize(" push hl,de\n ld a,1: inc a\n defb 1\n"), true);

    // --- ADR 0016 : blocs, fermetures, boucles -----------------------------
    // `end` ferme le bloc le plus interne, y compris imbriqué dans un autre.
    chk("end ferme le plus interne",
        "macro m\n repeat 2\n nop\n end\n end\n m\n", "nop\nnop\n");
    chk("fermetures explicites",
        " repeat 2\n nop\n endrepeat\n", "nop\nnop\n");
    chk("formes courtes tolerees en silence",
        "macro m\n nop\nmend\n m\n", "nop\n");

    // --- arguments de macro : forme nue vs accolades (ADR 0014) -------------
    // La forme NUE capture la VALEUR au site d'appel ; « {} » substitue le TEXTE,
    // que l'assembleur resout au point d'emission. C'est appel par valeur contre
    // appel par nom, et la difference est observable.
    chk("forme nue : la valeur, capturee a l'appel",
        "n = 5\n macro m x\n repeat 3\n db x\n n = n - 1\n rend\n endm\n m(n)\n",
        "n = 5\ndb 5\nn = n - 1\ndb 5\nn = n - 1\ndb 5\nn = n - 1\n");
    chk("accolades : le texte, resolu a l'emission",
        "n = 5\n macro m x\n repeat 3\n db {x}\n n = n - 1\n rend\n endm\n m(n)\n",
        "n = 5\ndb n\nn = n - 1\ndb n\nn = n - 1\ndb n\nn = n - 1\n");
    chk("forme nue dans une ligne EMISE (le manque d'hier)",
        " macro m val\n db val\n endm\n m(3)\n", "db 3\n");
    // « 1+1 » substitue TEXTUELLEMENT dans « db n2*2 » donnerait « db 1+1*2 », soit
    // 3 au lieu de 4 — un bug silencieux de precedence. C'est la VALEUR qui est
    // posee, donc « db 2*2 », que l'assembleur lit 4.
    chk("l'argument est evalue, jamais substitue textuellement",
        " macro m n2\n db n2*2\n endm\n m(1+1)\n", "db 2*2\n");
    chk("forme nue dans une expression du PP",
        " macro m cnt\n repeat cnt\n nop\n rend\n endm\n m(2)\n", "nop\nnop\n");
    // Repli SILENCIEUX : un fragment sans valeur par nature.
    chk("repli : un registre reste un registre",
        " macro savereg reg\n push reg\n pop reg\n endm\n savereg(hl)\n",
        "push hl\npop hl\n");
    chk("repli : (ix+2) n'a pas de valeur",
        " macro poke adr,v\n ld adr,a\n endm\n poke((ix+2),7)\n", "ld (ix+2),a\n");
    chk("repli : une chaine de plus d'un octet",
        " macro dire txt\n db txt\n endm\n dire(\"ab\")\n", "db \"ab\"\n");
    // Un argument d'un octet, lui, A une valeur (ADR 0010) : la forme nue la prend.
    chk("une chaine d'UN octet a une valeur",
        " macro m ch\n db ch\n endm\n m('A')\n", "db 65\n");

    // Repli AVERTI : l'argument depend d'un label, donc l'assembleur le resoudra
    // mais pas le preprocesseur. Le repli deplace le MOMENT de la resolution.
    chkWarnCount("repli sur un label : avertit une fois",
                 " macro m x\n ld hl,x\n endm\n m(buffer+2)\nbuffer: nop\n", 1);
    // La cle de deduplication est (ligne du corps, site d'appel) : cinq tours de
    // boucle sur le MEME appel ne font qu'un avertissement...
    chkWarnCount("un appel dans une boucle : toujours un seul",
                 " macro m x\n ld hl,x\n endm\n repeat 5\n m(buffer+2)\n rend\nbuffer: nop\n", 1);
    // ...mais deux sites distincts sont deux problemes distincts.
    chkWarnCount("deux sites d'appel : deux avertissements",
                 " macro m x\n ld hl,x\n endm\n m(buffer+2)\n m(buffer+3)\nbuffer: nop\n", 2);
    // Les replis silencieux ne doivent RIEN dire : sans quoi « push reg » crierait
    // a chaque usage d'un idiome majoritaire.
    chkWarnCount("un registre ne dit rien",
                 " macro savereg reg\n push reg\n endm\n savereg(hl)\n", 0);
    chkWarnCount("(ix+2) ne dit rien",
                 " macro poke adr\n ld adr,a\n endm\n poke((ix+2))\n", 0);
    chkWarnCount("une chaine longue ne dit rien",
                 " macro dire txt\n db txt\n endm\n dire(\"ab\")\n", 0);
    chkWarnCount("un argument resolu ne dit rien",
                 " macro m val\n db val\n endm\n m(3)\n", 0);

    // --- « ld pc,rr » : une orthographe (ADR 0017) --------------------------
    chk("ld pc,hl est canonise en jp (hl)", " ld pc,hl\n", "jp (hl)\n");
    chk("ld pc,ix / ld pc,iy", " ld pc,ix\n ld pc,iy\n", "jp (ix)\njp (iy)\n");
    chk("la casse est epousee", " LD PC,HL\n", "JP (HL)\n");
    chk("ld pc,de n'est pas cette forme : laisse tel quel", " ld pc,de\n", "ld pc,de\n");
    // Le dump du preprocesseur garde « label: instruction » sur une ligne ; c'est
    // la mise en forme, en aval, qui detache (ADR 0017, regle 3).
    chk("un label devant est conserve", "saut: ld pc,hl\n", "saut: jp (hl)\n");
    chkStrict("strict refuse l'orthographe", " ld pc,hl\n", false);
    chkStrict("strict accepte la forme canonique", " jp (hl)\n", true);

    // --- appel parenthese (ADR 0018) ---------------------------------------
    chk("appel parenthese", "macro m2 a1,a2\n db {a1},{a2}\nendm\n m2(1,2)\n", "db 1,2\n");
    chk("appel nu toujours accepte", "macro m2 a1,a2\n db {a1},{a2}\nendm\n m2 1,2\n", "db 1,2\n");
    chk("appel sans argument : nom()", "macro m\n nop\nendm\n m()\n", "nop\n");
    chk("definition parenthesee", "macro m2(a1,a2)\n db {a1},{a2}\nendm\n m2(3,4)\n", "db 3,4\n");
    chk("definition parenthesee, appel nu", "macro m2(a1,a2)\n db {a1},{a2}\nendm\n m2 3,4\n", "db 3,4\n");
    // La parenthese COLLEE et la fermeture en DERNIER : c'est ce qui permet a un
    // argument d'etre lui-meme parenthese sans ambiguite.
    chk("premier argument parenthese", "macro m2 a1,a2\n db {a1},{a2}\nendm\n m2((5),6)\n", "db (5),6\n");
    chk("appel nu dont un argument est parenthese",
        "macro m2 a1,a2\n db {a1},{a2}\nendm\n m2 (5),6\n", "db (5),6\n");
    chkErr("fermeture croisee refusee",
           "macro m\n repeat 2\n nop\n endm\n end\n m\n");
    chkErr("bloc non ferme", " repeat 2\n nop\n");

    // L'index de repeat commence à 0, et la forme à index est signalée.
    chkWarn("index de repeat 0-based et signale",
            " repeat 3,k\n db k\n rend\n", "db 0\ndb 1\ndb 2\n", true);
    chkWarn("repeat sans index ne signale rien",
            " repeat 2\n nop\n rend\n", "nop\nnop\n", false);

    // FOR : `to` inclut la borne, `until` l'exclut.
    chk("for until exclut la borne",
        " for x = 0 until 4\n db x\n endfor\n", "db 0\ndb 1\ndb 2\ndb 3\n");
    chk("for to inclut la borne",
        " for x = 1 to 3\n db x\n end\n", "db 1\ndb 2\ndb 3\n");
    chk("for a bornes symboliques",
        "n equ 3\n for x = 0 until n\n db x*2\n end\n",
        "n equ 3\ndb 0*2\ndb 1*2\ndb 2*2\n");
    chk("for vide ne deroule rien",
        " for x = 0 until 0\n nop\n end\n nop\n", "nop\n");
    chkErr("for sans to ni until", " for x = 0, 4\n nop\n end\n");
    chkErr("for sans affectation", " for x\n nop\n end\n");
    chkErr("registre en index de for", " for b = 0 until 2\n nop\n end\n");

    // ADR 0015 : les mots réservés sont réservés, dans les quatre positions.
    // Ici les deux que seul le préprocesseur connaît.
    chkErr("registre en paramètre de macro",
           "macro m b\n ld a,b\nendm\n m 3\n");
    chkErr("mnémonique en paramètre de macro",
           "macro m call\n db call\nendm\n m 3\n");
    chkErr("directive en paramètre de macro",
           "macro m org\n db org\nendm\n m 3\n");
    chkErr("mot-clé PP en paramètre de macro",
           "macro m repeat\n db repeat\nendm\n m 3\n");
    chk("paramètre libre", "macro m n\n db {n}\nendm\n m 3\n", "db 3\n");

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
        "LET n=0\nWHILE n<3\n  db {n}\n  LET n = n+1\nWEND\n",
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
    chk("colon sep", "  ld a,1 : ld b,2 : ret\n", "ld a,1\n    ld b,2\n    ret\n");
    chk("colon garde label collé", "start: di : ret\n", "start: di\n    ret\n");
    chk("colon mnémo 0-op", "nop : ret\n", "nop\n    ret\n");
    chk("colon dans chaîne protégé", "  db \"a:b\" : nop\n", "db \"a:b\"\n    nop\n");
    chk("colon dans (ix+d)", "  ld a,(ix+0) : ret\n", "ld a,(ix+0)\n    ret\n");

    // "mnémo:mnémo" collé (style non canonique, mais rasm le découpe quand même en 2
    // instructions car "ei"/"ret" sont des mnémos connus, pas des labels) -> avertissement.
    chkWarn("mnémo:mnémo collé", "ei:ret\n", "ei\n    ret\n", true);
    chkWarn("mnémo: mnémo (espace après)", "ei: ret\n", "ei\n    ret\n", true);
    chkWarn("mnémo:mnémo inversé", "ret:ei\n", "ret\n    ei\n", true);
    chkWarn("mnémo : mnémo (espace avant, pas d'avertissement)", "ret : ei\n", "ret\n    ei\n", false);

    // push/pop multi-registres -> une instruction par registre
    chk("push multi", "  push af,bc,de\n", "push af\n    push bc\n    push de\n");
    chk("pop multi + label", "lbl: pop hl,de\n", "lbl: pop hl\n    pop de\n");
    chk("push multi + colon", "  push af,bc : ret\n", "push af\n    push bc\n    ret\n");

    // --- erreurs (PP strict) ---
    chkErr("IF sur label temps-assemblage", "IF taille>0x4000\n nop\nENDIF\n");
    chkErr("mauvais nb d'args macro", "MACRO M a,b\n nop\nENDM\n M 1\n");
    chkErr("REPEAT sans REND", "REPEAT 3\n nop\n");
    chkErr("include manquant", "  INCLUDE \"absent.asm\"\n");
    chkErr("subst inconnue", "  ld a,{inconnu}\n");

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
