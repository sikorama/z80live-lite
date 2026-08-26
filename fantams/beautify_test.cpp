// beautify_test.cpp - Tests de la mise en forme (ADR 0013)
//
// Trois familles, dans l'ordre de gravité de l'ADR :
//   1. neutralité sémantique — les octets assemblés sont inchangés ;
//   2. idempotence ;
//   3. extinction des avertissements de bonne pratique.
// Le reste vérifie chaque règle et, surtout, ce que la mise en forme REFUSE de
// toucher.
#include "beautify.h"
#include "pp.h"

#include "asm.h"

#include <cstdio>
#include <string>
#include <vector>

static int g_pass = 0, g_fail = 0;

static std::string show(const std::string &s) {
    std::string o;
    for (char c : s) {
        if (c == '\n') o += "\\n";
        else if (c == '\t') o += "\\t";
        else if (c == '\r') o += "\\r";
        else o += c;
    }
    return o;
}

static void chk(const char *desc, const std::string &src, const std::string &expected,
                kw::Phase ph = kw::Phase::Assembly) {
    std::string got = beautify::apply(src, ph);
    if (got != expected) {
        ++g_fail;
        printf("  \033[31mFAIL\033[0m %s\n    attendu [%s]\n    obtenu  [%s]\n",
               desc, show(expected).c_str(), show(got).c_str());
    } else ++g_pass;
}

// La ligne est rendue à l'octet près.
static void keep(const char *desc, const std::string &src,
                 kw::Phase ph = kw::Phase::Assembly) {
    chk(desc, src, src, ph);
}

static void okc(const char *desc, bool cond) {
    if (cond) ++g_pass; else { ++g_fail; printf("  \033[31mFAIL\033[0m %s\n", desc); }
}

static size_t countLines(const std::string &s) {
    size_t n = 1;
    for (char c : s) if (c == '\n') ++n;
    return n;
}

// Invariant n°1 : les octets assemblés sont identiques avant et après.
static void sameBytes(const char *desc, const std::string &src) {
    asmb::Output a = asmb::assembleText(src, "t.asm");
    std::string b = beautify::apply(src, kw::Phase::Assembly);
    asmb::Output o = asmb::assembleText(b, "t.asm");
    okc(desc, a.ok && o.ok && a.bin == o.bin && a.loadAddress == o.loadAddress);
    if (a.ok && o.ok && a.bin != o.bin)
        printf("    (%zu octets contre %zu)\n", a.bin.size(), o.bin.size());
    // La bijection sur les lignes ne vaut plus que SANS détachement (ADR 0017) :
    // la règle 3 la rompt délibérément, et rien dans la chaîne n'en dépend —
    // l'assembleur consomme la provenance de `pp::Result::lines`, pas des numéros
    // de ligne de texte mis en forme.
    okc("bijection sur les lignes sans détachement",
        countLines(src) == countLines(beautify::apply(src, kw::Phase::Assembly, /*detach=*/false)));
}

// Invariant n°2 : idempotence.
// ADR 0017 : `--normalize` ne change pas le programme. C'est SA promesse, et la
// seule qui compte — le nombre de lignes, lui, change délibérément. Vérifiée ici
// parce que c'est le seul binaire de test où le préprocesseur et l'assembleur sont
// liés ensemble.
static void normKeepsBytes(const char *desc, const std::string &src) {
    auto build = [](const std::string &text) {
        pp::Result p = pp::preprocess(text, "t.asm", [](const std::string &, std::string &) { return false; });
        return p.ok ? asmb::assembleText(p.dump(), "t.asm") : asmb::Output{};
    };
    asmb::Output before = build(src);
    asmb::Output after = build(pp::normalize(src));
    okc(desc, before.ok && after.ok && before.bin == after.bin &&
              before.loadAddress == after.loadAddress);
}

static void idem(const char *desc, const std::string &src, kw::Phase ph = kw::Phase::Assembly) {
    std::string once = beautify::apply(src, ph);
    okc(desc, beautify::apply(once, ph) == once);
}

// Invariant n°3 : les deux avertissements visés s'éteignent.
static void noWarn(const char *desc, const std::string &src) {
    asmb::Output before = asmb::assembleText(src, "t.asm");
    asmb::Output after = asmb::assembleText(beautify::apply(src, kw::Phase::Assembly), "t.asm");
    bool had = false;
    for (auto &w : before.warnings)
        if (w.message.find("label without ':'") != std::string::npos ||
            w.message.find("in column 1") != std::string::npos) had = true;
    bool still = false;
    for (auto &w : after.warnings)
        if (w.message.find("label without ':'") != std::string::npos ||
            w.message.find("in column 1") != std::string::npos) still = true;
    okc(desc, had && !still);
}

int main() {
    printf("Tests mise en forme (beautify)\n\n");

    // --- règle 1 : le deux-points ------------------------------------------
    chk("label seul reçoit son ':'", "start\n  ld a,1\n", "start:\n    ld a,1\n");
    keep("label déjà pourvu", "start:\n    ld a,1\n");
    keep("label avec ':' détaché", "start :\n    ld a,1\n");
    chk("label local", ".loop\n    nop\n", ".loop:\n    nop\n");
    // Un nom INDENTÉ et seul ne reçoit rien : c'est la forme d'un appel de macro
    // sans argument, et un `include` peut apporter la macro sans que le prescan la
    // voie. L'assembleur avertit déjà que seuls les labels commencent en colonne 1 ;
    // le beautify s'appuie sur cette convention plutôt que de deviner.
    chk("nom indenté et seul : pas de ':', mais indenté comme du code",
        "  start\n    nop\n", "    start\n    nop\n");
    chk("commentaire de fin conservé (décalé d'un cran)",
        "start ; entree\n", "start: ; entree\n");

    // Ce que la règle 1 REFUSE de faire : deviner.
    chk("appel de macro : pas de ':', mais indenté", "sprite 4,12\n", "    sprite 4,12\n");
    chk("nom + argument non réservé : pas de ':'", "cls 0\n", "    cls 0\n");
    // Une définition est du CODE : jamais de ':', mais indentée comme le reste du
    // bloc où elle vit (« v=v-1 » dans un REPEAT est une étape de calcul).
    chk("constante EQU", "ecran EQU #C000\n", "    ecran EQU #C000\n");
    chk("variable '='", "compteur = 3\n", "    compteur = 3\n");
    // Ce qui SUIT lève le doute quand il est réservé : aucun appel de macro ne
    // commence par `ld` ni par `dw`. C'est le seul cas où un nom sans deux-points
    // suivi de code est décidable — et il l'est complètement.
    chk("nom + mot réservé : c'est un label, donc ':' et détachement",
        "start ld a,1\n", "start:\n    ld a,1\n");
    chk("nom + directive de données", "pcoltab dw coltab\n", "pcoltab:\n    dw coltab\n");

    // --- règle 1 : le prescan des macros (ADR 0013 amendé) ------------------
    // Un appel SANS ARGUMENT est un nom seul sur sa ligne : sans le prescan il
    // recevrait un deux-points et l'appel disparaîtrait.
    // (`macro`/`endm` sont réservés à cette phase : la règle 2 les indente. Ce que
    // ces cas vérifient est la DERNIÈRE ligne — l'appel, laissé intact.)
    chk("appel sans argument, macro définie « macro nom »",
        "macro fill_screen\n    ret\nendm\nfill_screen\n",
        "    macro fill_screen\n        ret\n    endm\n    fill_screen()\n", kw::Phase::Preprocess);
    chk("appel sans argument, macro définie « nom macro »",
        "fill_screen macro\n    ret\nendm\nfill_screen\n",
        "    macro fill_screen\n        ret\n    endm\n    fill_screen()\n", kw::Phase::Preprocess);
    chk("le ':' de « macro nom: » n'appartient pas au nom",
        "macro fill_screen:\n    ret\nendm\nfill_screen\n",
        "    macro fill_screen:\n        ret\n    endm\n    fill_screen()\n", kw::Phase::Preprocess);
    chk("appel AVANT la définition : le prescan lit tout le texte d'abord",
        "fill_screen\nmacro fill_screen\nret\nendm\n",
        "    fill_screen()\n    macro fill_screen\n        ret\n    endm\n", kw::Phase::Preprocess);
    // Le prescan ne doit pas neutraliser un VRAI label homonyme d'aucune macro.
    chk("nom qui n'est aucune macro : label comme avant",
        "macro autre\nendm\nstart\n", "    macro autre\n    endm\nstart:\n",
        kw::Phase::Preprocess);

    // --- règle 3 : détachement des labels (ADR 0017) ------------------------
    chk("label détaché de son instruction", "start: ld a,1\n", "start:\n    ld a,1\n");
    chk("détachement avec commentaire : il suit le CODE",
        "start: ld a,1  ; init\n", "start:\n    ld a,1 ; init\n");
    chk("label local détaché aussi", ".loop: djnz .loop\n", ".loop:\n    djnz .loop\n");
    okc("opt-out : la ligne reste entière",
        beautify::apply("start: ld a,1\n", kw::Phase::Assembly, /*detach=*/false) == "start: ld a,1\n");
    chk("sans ':' ni mot réservé, rien n'est détaché", "sprite 4,12\n", "    sprite 4,12\n");

    // --- appel parenthésé (ADR 0018) ---------------------------------------
    // `nom(` se lit SANS connaître les macros : c'est la seule graphie qui vaille
    // pour une macro pas encore écrite. Jamais un label, donc jamais de ':'.
    chk("appel parenthésé : jamais un label, indenté comme du code",
        "DBPIXM0(v,v)\n", "    DBPIXM0(v,v)\n");
    chk("appel parenthésé sans argument", "fill_screen()\n", "    fill_screen()\n");
    // Une macro CONNUE appelée nue reçoit ses parenthèses : l'assembleur avertit
    // désormais sur la forme nue, donc le beautify a le droit de l'éteindre.
    chk("macro connue appelée nue : les parenthèses sont posées",
        "macro cls\nnop\nendm\ncls\n",
        "    macro cls\n        nop\n    endm\n    cls()\n", kw::Phase::Preprocess);
    chk("macro connue avec arguments",
        "macro spr p,q\nnop\nendm\nspr 4,12\n",
        "    macro spr p,q\n        nop\n    endm\n    spr(4,12)\n", kw::Phase::Preprocess);
    chk("définition parenthésée : le prescan retient le NOM, pas la liste",
        "macro spr(p,q)\nnop\nendm\nspr 4,12\n",
        "    macro spr(p,q)\n        nop\n    endm\n    spr(4,12)\n", kw::Phase::Preprocess);
    chk("commentaire conservé quand les parenthèses sont posées",
        "macro cls\nnop\nendm\ncls ; efface\n",
        "    macro cls\n        nop\n    endm\n    cls() ; efface\n", kw::Phase::Preprocess);
    // Un nom INCONNU reste intouché : c'est tout le sens de l'asymétrie.
    chk("nom inconnu : pas de parenthèses inventées", "sprite 4,12\n", "    sprite 4,12\n");
    // La table fournie par l'appelant (préprocesseur, includes lus) complète le
    // prescan textuel.
    okc("macros connues de l'appelant : traitées comme le prescan",
        beautify::apply("depuis_include\n", kw::Phase::Preprocess, true, true, {"DEPUIS_INCLUDE"})
            == "    depuis_include()\n");
    okc("sans cette table, le même texte reste un label",
        beautify::apply("depuis_include\n", kw::Phase::Preprocess) == "depuis_include:\n");

    // --- règle 4 : colonne 1 pour les labels, un cran par bloc --------------
    chk("label indenté ramené en colonne 1", "  start:\n  nop\n", "start:\n    nop\n");
    chk("label dans un bloc : colonne 1 quand même",
        "repeat 2\n  boucle:\n  nop\nrend\n",
        "    repeat 2\nboucle:\n        nop\n    rend\n", kw::Phase::Preprocess);
    chk("repeat imbriqués",
        "repeat 3\nv=15\nrepeat 14\nnop\nv=v-1\nrend\nrend\n",
        "    repeat 3\n        v=15\n        repeat 14\n            nop\n"
        "            v=v-1\n        rend\n    rend\n", kw::Phase::Preprocess);
    chk("la fermeture s'aligne sur son ouvreur, pas sur le corps",
        "if 1\nnop\nendif\n", "    if 1\n        nop\n    endif\n", kw::Phase::Preprocess);
    chk("'end' polyvalent ferme aussi un cran",
        "while 1\nnop\nend\n", "    while 1\n        nop\n    end\n", kw::Phase::Preprocess);
    chk("bloc ouvert ET fermé sur une ligne : la profondeur ne dérive pas",
        "repeat 3 : dw 1 : rend\nnop\n", "    repeat 3 : dw 1 : rend\n    nop\n",
        kw::Phase::Preprocess);
    chk("fermeture orpheline : pas de dérive vers la gauche",
        "rend\nnop\n", "    rend\n    nop\n", kw::Phase::Preprocess);
    chk("MODULE n'ouvre pas de bloc (ADR 0016)",
        "module gfx\nnop\n", "    module gfx\n    nop\n", kw::Phase::Preprocess);
    okc("opt-out : sans indentation de bloc, un seul cran",
        beautify::apply("repeat 2\nnop\nrend\n", kw::Phase::Preprocess, true, /*indentBlocks=*/false)
            == "    repeat 2\n    nop\n    rend\n");
    chk("commentaire seul : son alignement appartient à l'auteur",
        "repeat 2\n  ; a la main\nrend\n",
        "    repeat 2\n  ; a la main\n    rend\n", kw::Phase::Preprocess);

    // --- règle 2 : quatre espaces ------------------------------------------
    chk("instruction en colonne 1", "start:\nnop\n", "start:\n    nop\n");
    chk("tabulation remplacée", "start:\n\tld a,1\n", "start:\n    ld a,1\n");
    chk("indentation excédentaire ramenée à 4",
        "start:\n        ld a,1\n", "start:\n    ld a,1\n");
    keep("indentation déjà juste", "start:\n    ld a,1\n");
    chk("directive indentée elle aussi", "org #8000\nnop\n", "    org #8000\n    nop\n");
    chk("directive de données", "db 1,2,3\n", "    db 1,2,3\n");
    keep("commentaire seul", "; juste un commentaire\n");
    keep("commentaire seul indenté", "    ; aligné à la main\n");
    keep("ligne vide", "\n");
    keep("commentaire '//' seul", "// commentaire C\n");
    chk("directive rasm refusée : indentée quand même",
        "snaset CRTC_TYPE,1\n", "    snaset CRTC_TYPE,1\n");

    // Un ';' dans une chaîne n'ouvre pas un commentaire : la ligne reste du code.
    chk("point-virgule en chaîne", "db \"a;b\"\n", "    db \"a;b\"\n");

    // --- la phase ----------------------------------------------------------
    // `LET` n'est un mot réservé qu'au temps préprocesseur. Au temps
    // d'assemblage il n'existe plus, donc `let` y est lu comme un label — et la
    // ligne est laissée intacte, faute de savoir.
    chk("LET au temps d'assemblage : pas de ':', mais indenté",
        "let n = 3\n", "    let n = 3\n");
    chk("LET au temps préprocesseur : c'est une directive",
        "let n = 3\n", "    let n = 3\n", kw::Phase::Preprocess);
    chk("REPEAT au temps préprocesseur : le corps prend un cran",
        "repeat 3\nnop\nrend\n",
        "    repeat 3\n        nop\n    rend\n", kw::Phase::Preprocess);
    chk("déclaration « nom MACRO params » : ce n'est pas un label, et elle est réécrite",
        "cls MACRO couleur\n", "    MACRO cls couleur\n", kw::Phase::Preprocess);
    chk("mnémonique reconnu aux deux phases", "nop\n", "    nop\n", kw::Phase::Preprocess);
    // Les mots-clés de FIN de bloc sont seuls sur leur ligne : au mauvais cran
    // ils seraient lus comme un label seul et recevraient un deux-points, ce qui
    // détruirait le source. D'où la phase du tampon de l'éditeur : préprocesseur,
    // et non assemblage — c'est bien le préprocesseur qui va le lire.
    chk("MEND seul sur sa ligne est indenté, pas étiqueté",
        "MEND\n", "    MEND\n", kw::Phase::Preprocess);
    chk("ENDM, REND, WEND, ENDIF : idem",
        "endm\nrend\nwend\nendif\n", "    endm\n    rend\n    wend\n    endif\n",
        kw::Phase::Preprocess);
    // « nom MACRO p » est une graphie héritée, avertie par l'assembleur — donc que
    // le beautify a le droit de réécrire, comme les parenthèses d'appel.
    chk("macro complète : rien de détruit, le corps prend un cran",
        "cls MACRO c\nld a,{c}\nMEND\n", "    MACRO cls c\n        ld a,{c}\n    MEND\n",
        kw::Phase::Preprocess);
    chk("« nom macro » minuscule : la casse du mot-clé est épousée",
        "cls macro c\nnop\nendm\n", "    macro cls c\n        nop\n    endm\n",
        kw::Phase::Preprocess);
    chk("« nom macro » sans paramètre", "foo macro\nnop\nendm\n",
        "    macro foo\n        nop\n    endm\n", kw::Phase::Preprocess);
    keep("« macro nom » est déjà canonique", "    macro cls c\n        nop\n    endm\n",
         kw::Phase::Preprocess);

    // --- ld pc,rr (ADR 0017 : une orthographe) ------------------------------
    keep("« ld pc,hl » n'est pas de la mise en forme : le beautify n'y touche pas",
         "    ld pc,hl\n");

    // --- préservation du texte --------------------------------------------
    keep("pas de saut de ligne final : rien n'est ajouté", "start:\n    nop");
    chk("saut de ligne final unique conservé", "nop\n", "    nop\n");
    chk("fin de ligne Windows conservée", "nop\r\n", "    nop\r\n");
    chk("texte vide", "", "");
chk("espaces de fin retirés", "    nop   \n", "    nop\n");
    keep("espacement interne des opérandes non touché", "    ld  a ,  1\n");

    {
        std::string src = "start\nnop\n\tld a,1\n";
        okc("nombre de lignes préservé quand aucun label n'est collé",
            countLines(src) == countLines(beautify::apply(src, kw::Phase::Assembly)));
        std::string glued = "start: ld a,1\n";
        okc("détachement : une ligne de plus, et une seule",
            countLines(beautify::apply(glued, kw::Phase::Assembly)) == countLines(glued) + 1);
    }

    // --- invariants --------------------------------------------------------
    printf("\n  invariants\n");
    sameBytes("octets inchangés : label sans ':' + colonne 1",
              "  org #8000\nstart\nld a,1\n\tjp start\n");
    sameBytes("octets inchangés : source déjà en forme",
              "    org #8000\nstart:\n    ld a,1\n    jp start\n");
    sameBytes("octets inchangés : données et labels locaux",
              "  org #8000\ntable\n.a\ndb 1,2,3\n\tdw table\n");

    idem("idempotence : source mal formée", "start\nnop\n\tld a,1\ndb 1\n");
    idem("idempotence : source déjà en forme", "start:\n    ld a,1\n");
    idem("idempotence : préprocesseur", "let n = 3\nrepeat n\nnop\nrend\n",
         kw::Phase::Preprocess);

    noWarn("avertissements éteints : label sans ':'", "  org #8000\nstart\n  nop\n");
    noWarn("avertissements éteints : instruction en colonne 1", "start:\nnop\n");
    noWarn("avertissements éteints : les deux à la fois", "start\nnop\n  jp start\n");

    // Le contre-exemple assumé : ce que la mise en forme n'éteint PAS, parce
    // qu'elle refuse de deviner. `sprite` peut être un appel de macro.
    {
        asmb::Output o = asmb::assembleText(
            beautify::apply("start:\nsprite 4,12\n", kw::Phase::Assembly), "t.asm");
        bool still = false;
        for (auto &w : o.warnings)
            if (w.message.find("label without ':'") != std::string::npos) still = true;
        okc("l'avertissement survit sur un appel de macro possible", still);
    }

    // ADR 0017 : normaliser ne change pas le programme.

    normKeepsBytes("normalize : sucre un-vers-plusieurs",

                    "  org 0x8000\nstart:\n  push hl,de\n  ld a,1: inc a\n  ret\n");

    normKeepsBytes("normalize : orthographes obsoletes",

                    "  org 0x100\n  defb 1,2\n  defw 0x1234\n  defs 3\n");

    normKeepsBytes("normalize : macros et boucles conservees",

                    "  org 0\nmacro m\n  push hl,de\nmend\n  repeat 2\n  m\n  rend\n");


    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
