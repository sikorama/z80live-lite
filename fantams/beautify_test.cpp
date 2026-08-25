// beautify_test.cpp - Tests de la mise en forme (ADR 0013)
//
// Trois familles, dans l'ordre de gravité de l'ADR :
//   1. neutralité sémantique — les octets assemblés sont inchangés ;
//   2. idempotence ;
//   3. extinction des avertissements de bonne pratique.
// Le reste vérifie chaque règle et, surtout, ce que la mise en forme REFUSE de
// toucher.
#include "beautify.h"

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
    // La bijection sur les lignes vaut pour toute source, y compris celles que
    // la mise en forme laisse intactes : c'est elle qui protège la provenance.
    okc("bijection sur les lignes", countLines(src) == countLines(b));
}

// Invariant n°2 : idempotence.
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
    chk("label indenté : le ':' oui, l'indentation non",
        "  start\n    nop\n", "  start:\n    nop\n");
    chk("commentaire de fin conservé (décalé d'un cran)",
        "start ; entree\n", "start: ; entree\n");

    // Ce que la règle 1 REFUSE de faire : deviner.
    keep("appel de macro laissé intact", "sprite 4,12\n");
    keep("appel de macro sans argument laissé intact", "cls 0\n");
    keep("constante EQU", "ecran EQU #C000\n");
    keep("variable '='", "compteur = 3\n");
    keep("label + instruction sur la même ligne (bijection)", "start: ld a,1\n");
    keep("label sans ':' + instruction : intact, le doute est justifié", "start ld a,1\n");

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
    keep("LET au temps d'assemblage : inconnu, donc intact", "let n = 3\n");
    chk("LET au temps préprocesseur : c'est une directive",
        "let n = 3\n", "    let n = 3\n", kw::Phase::Preprocess);
    chk("REPEAT au temps préprocesseur", "repeat 3\nnop\nrend\n",
        "    repeat 3\n    nop\n    rend\n", kw::Phase::Preprocess);
    keep("déclaration « nom MACRO params » : ce n'est pas un label",
         "cls MACRO couleur\n", kw::Phase::Preprocess);
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
    chk("macro complète : rien de détruit",
        "cls MACRO c\nld a,{c}\nMEND\n", "cls MACRO c\n    ld a,{c}\n    MEND\n",
        kw::Phase::Preprocess);

    // --- préservation du texte --------------------------------------------
    keep("pas de saut de ligne final : rien n'est ajouté", "start:\n    nop");
    chk("saut de ligne final unique conservé", "nop\n", "    nop\n");
    chk("fin de ligne Windows conservée", "nop\r\n", "    nop\r\n");
    chk("texte vide", "", "");
    keep("espaces de fin conservés", "    nop   \n");
    keep("espacement interne des opérandes non touché", "    ld  a ,  1\n");

    {
        std::string src = "start\nnop\n\tld a,1\n";
        okc("nombre de lignes préservé",
            countLines(src) == countLines(beautify::apply(src, kw::Phase::Assembly)));
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

    printf("\n%d réussis, %d échoués\n", g_pass, g_fail);
    return g_fail ? 1 : 0;
}
