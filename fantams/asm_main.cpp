// asm_main.cpp - end-to-end CLI: .asm source -> preprocessor -> assembler -> .bin
//
//   fantams file.asm [-o out] [-s] [-E] [--strict] [--beautify] [--normalize]
//           [--no-detach-labels] [--no-indent-blocks] [--base base.sna]
//     -o : output binary file (default: <source>.bin)
//     -s : print the symbol table
//     --base : reference snapshot the assembled bytes are laid onto (ADR 0012).
//          Only meaningful for a .sna output. Every address the source did NOT
//          write keeps the base's byte — that is what makes firmware calls work.
//     -E : write the UNROLLED source to -o instead of assembling (macros
//          expanded, loops unrolled, includes inserted, scopes renamed).
//          C'est un livrable de premier plan, pas un artefact de debogage :
//          c'est lui qui rend verifiable ce que le preprocesseur a compris.
//          La sortie est MISE EN FORME : la mise en forme fait partie de la
//          definition de la source deroulee (ADR 0013).
//     --beautify : mettre en forme le source et l'ecrire dans -o, sans
//          preprocesseur ni assemblage. C'est ce que le bouton « Mettre en
//          forme » de l'editeur appelle. Preserve le nombre de lignes (ADR 0013).
//     --strict : refuser tout ce qui n'est pas du Z80 canonique — sucre
//          un-vers-plusieurs et orthographes obsoletes (ADR 0017). N'ajoute rien,
//          refuse. C'est le drapeau du PIPELINE, pas d'une couche.
//     --no-detach-labels : garder « label: instruction » sur une seule ligne.
//     --no-indent-blocks : ne pas indenter le corps des blocs (repeat, macro,
//          if, while, for, struct). Comme le detachement, c'est un STYLE et non
//          un canon, d'ou l'opt-out (ADR 0013, regle 4).
//          Le beautify detache par defaut (regle 3) : l'indentation fixe aligne
//          tous les opcodes, un label de longueur variable ne les aligne pas.
//     --sym[=fichier] : ecrire la TABLE DES SYMBOLES (ADR 0019) : un CSV d'une
//          ligne par label et par constante, avec type, adresse logique, banque et
//          adresse de rangement, fichier et ligne D'ORIGINE (avant preprocesseur).
//          Destinee a un desassembleur ou un emulateur, pas a un humain — pour
//          l'humain, c'est « -s ». Sans « = », le chemin est derive de -o : le
//          fichier voyage a cote du binaire qu'il decrit.
//     --normalize : canoniser le source SANS le derouler (ADR 0017) : orthographes
//          obsoletes et opcodes composes. Change deliberement le nombre de lignes.
//          Transformation INDEPENDANTE du beautify, composable avec lui — les deux
//          ensemble normalisent puis mettent en forme. Incompatible avec -E, qui
//          canonise deja PUIS deroule : deux sorties differentes.
#include "asm.h"
#include "beautify.h"
#include "pp.h"
#include "sna.h"
#include "sym.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <vector>

static bool readFile(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

int main(int argc, char **argv) {
    std::string path, outPath, basePath;
    bool showSyms = false;
    bool dumpOnly = false;
    bool beautifyOnly = false;
    bool normalizeOnly = false;
    bool wantSym = false;
    std::string symPath;
    bool strict = false;
    bool detachLabels = true;
    bool indentBlocks = true;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--base" && i + 1 < argc) basePath = argv[++i];
        else if (a == "-s") showSyms = true;
        else if (a == "-E") dumpOnly = true;
        else if (a == "--beautify") beautifyOnly = true;
        else if (a == "--normalize") normalizeOnly = true;
        // « --sym » ne prend pas d'argument positionnel : « fantams --sym src.asm »
        // serait ambigu (chemin de sortie, ou source ?). Le chemin explicite passe
        // par « --sym=... », le defaut se derive de -o.
        else if (a == "--sym") wantSym = true;
        else if (a.rfind("--sym=", 0) == 0) { wantSym = true; symPath = a.substr(6); }
        else if (a == "--strict") strict = true;
        else if (a == "--no-detach-labels") detachLabels = false;
        else if (a == "--no-indent-blocks") indentBlocks = false;
        else path = a;
    }
    if (path.empty()) { fprintf(stderr, "usage: fantams file.asm [-o out] [-s] [-E] [--strict] [--beautify] [--normalize] [--no-detach-labels] [--no-indent-blocks] [--base base.sna] [--sym[=out.sym]]\n"); return 2; }
    // Le mode « la sortie est un source » : l'un ou l'autre des deux drapeaux suffit.
    const bool sourceOut = beautifyOnly || normalizeOnly;
    if (outPath.empty()) {
        size_t dot = path.find_last_of('.');
        std::string stem = (dot == std::string::npos ? path : path.substr(0, dot));
        outPath = stem + (sourceOut ? ".fmt.asm" : dumpOnly ? ".pp.asm" : ".bin");
    }

    // Le .sym decrit le BINAIRE, pas la source : il se derive de -o pour se poser
    // a cote de lui, la ou un emulateur ira le chercher.
    if (wantSym && symPath.empty()) {
        size_t dot = outPath.find_last_of('.');
        size_t slash = outPath.find_last_of('/');
        std::string stem = (dot == std::string::npos || (slash != std::string::npos && dot < slash))
                               ? outPath : outPath.substr(0, dot);
        symPath = stem + ".sym";
    }
    // Ni --beautify ni --normalize ne passent par l'assembleur : il n'y a pas de
    // table de symboles sans assemblage. -E, lui, assemble — il est autorise.
    if (wantSym && sourceOut) {
        fprintf(stderr, "error: --sym demande un assemblage ; %s ne passe pas par l'assembleur (il transforme du texte en texte)\n",
                beautifyOnly ? "--beautify" : "--normalize");
        return 2;
    }

    if (beautifyOnly && dumpOnly) {
        fprintf(stderr, "error: -E et --beautify demandent deux sorties differentes : la source deroulee, ou le source mis en forme\n");
        return 2;
    }
    if (normalizeOnly && dumpOnly) {
        fprintf(stderr, "error: -E canonise deja PUIS deroule les macros et les boucles ; --normalize canonise sans derouler. Deux sorties differentes.\n");
        return 2;
    }

    bool wantSna = outPath.size() >= 4 && outPath.substr(outPath.size() - 4) == ".sna";
    if (!basePath.empty() && (dumpOnly || sourceOut || !wantSna)) {
        fprintf(stderr, "error: --base ne s'applique qu'a une sortie .sna : %s\n", outPath.c_str());
        return 2;
    }

    // Aucun repli silencieux sur une base : un repli sur des zeros produirait un
    // .sna qui demarre et plante au premier appel firmware (ADR 0012).
    sna::Base base;
    bool hasBase = false;
    if (!basePath.empty()) {
        std::string raw;
        if (!readFile(basePath, raw)) {
            fprintf(stderr, "error: base introuvable : %s\n", basePath.c_str());
            return 2;
        }
        std::vector<uint8_t> bytes(raw.begin(), raw.end());
        std::string err;
        if (!sna::parseBase(bytes, base, err)) {
            fprintf(stderr, "error: %s : %s\n", basePath.c_str(), err.c_str());
            return 2;
        }
        hasBase = true;
    }

    std::string content;
    if (!readFile(path, content)) { fprintf(stderr, "error: file not found: %s\n", path.c_str()); return 2; }

    // --beautify : la mise en forme rend le source de l'AUTEUR — ses macros, ses
    // includes et ses boucles restent ou ils sont, rien n'est deroule.
    //
    // Le preprocesseur tourne quand meme, mais on ne garde de lui que sa TABLE DE
    // MACROS : c'est la seule chose qui voie a l'interieur des `include`, et sans
    // elle la mise en forme ne peut pas distinguer « fill_screen » appele de
    // « fill_screen » declare. Ses erreurs sont ignorees — la mise en forme doit
    // tourner sur un source casse, c'est meme la qu'on la demande le plus.
    //
    // Temps PREPROCESSEUR : ce texte est celui que le preprocesseur va lire, ses
    // mots-cles y sont donc vivants. Au temps d'assemblage, « MEND » n'est pas
    // reserve et serait lu comme un label seul sur sa ligne — le beautify lui
    // ajouterait un deux-points et detruirait le source.
    if (sourceOut) {
        // L'ordre compte : --normalize change le nombre de lignes, le beautify met
        // en forme ce qui en resulte. L'inverse mettrait en forme des lignes que
        // la canonisation allait couper.
        std::string text = content;
        if (normalizeOnly) text = pp::normalize(text);
        if (beautifyOnly) {
            const pp::Result probe = pp::preprocess(content, path, readFile, /*strict=*/false);
            text = beautify::apply(text, kw::Phase::Preprocess, detachLabels, indentBlocks,
                                   probe.macroNames);
        }
        std::ofstream f(outPath, std::ios::binary);
        if (!f) { fprintf(stderr, "error: cannot write: %s\n", outPath.c_str()); return 2; }
        f.write(text.data(), (std::streamsize)text.size());
        fprintf(stderr, "%s: %s\n", outPath.c_str(),
                beautifyOnly && normalizeOnly ? "source canonise et mis en forme"
                : normalizeOnly ? "source canonise" : "source mis en forme");
        return 0;
    }

    // 1) preprocessor
    pp::Result pre = pp::preprocess(content, path, readFile, strict);
    for (auto &w : pre.warnings) fprintf(stderr, "%s:%d: warning: %s\n", w.file.c_str(), w.line, w.message.c_str());
    if (!pre.ok) {
        for (auto &e : pre.errors) fprintf(stderr, "%s:%d: error (preproc): %s\n", e.file.c_str(), e.line, e.message.c_str());
        return 1;
    }

    // -E : la source deroulee est le resultat demande, on s'arrete la. Elle sort
    // MISE EN FORME (ADR 0013), au temps d'ASSEMBLAGE : c'est le texte que
    // l'assembleur va lire, les mots-cles du preprocesseur n'y sont plus. Les
    // traiter comme reserves ferait indenter un label nomme « read » au lieu de
    // lui donner son deux-points.
    if (dumpOnly) {
        std::string text = beautify::apply(pre.dump(), kw::Phase::Assembly, detachLabels, indentBlocks);
        std::ofstream f(outPath, std::ios::binary);
        if (!f) { fprintf(stderr, "error: cannot write: %s\n", outPath.c_str()); return 2; }
        f.write(text.data(), (std::streamsize)text.size());
        // Le compte est celui du TEXTE ECRIT, pas celui de pre.lines : le
        // detachement des labels ajoute des lignes, et annoncer l'autre chiffre
        // ferait mentir le seul nombre que le lecteur peut verifier.
        size_t written = text.empty() ? 0 : 1;
        for (char c : text) if (c == '\n') ++written;
        if (!text.empty() && text.back() == '\n') --written;
        fprintf(stderr, "%s: unrolled source (%zu lines)\n", outPath.c_str(), written);
        // --sym exige d'assembler, on continue donc. Aucun binaire ne sera ecrit :
        // -E a pris `-o`, et l'ecraser detruirait la sortie demandee.
        if (!wantSym) return 0;
    }

    // 2) assembler (2 passes) on the flat text
    std::vector<asmb::SourceLine> lines;
    for (auto &l : pre.lines) lines.push_back({l.text, l.file, l.line, l.col0});
    asmb::Output out = asmb::assemble(lines);
    // PRINT n'est ni une erreur ni un avertissement : c'est ce que la source a
    // demande d'afficher. Sur stderr comme le reste, pour que stdout reste libre
    // (l'option -E y ecrit la source deroulee).
    for (auto &p : out.prints) fprintf(stderr, "%s:%d: %s\n", p.file.c_str(), p.line, p.message.c_str());
    for (auto &w : out.warnings) fprintf(stderr, "%s:%d: warning: %s\n", w.file.c_str(), w.line, w.message.c_str());
    if (!out.ok) {
        for (auto &e : out.errors) fprintf(stderr, "%s:%d: error: %s\n", e.file.c_str(), e.line, e.message.c_str());
        return 1;
    }

    // La table des symboles sort ICI : apres le refus sur erreur d'assemblage — un
    // .sym partiel qu'un debogueur charge sans le savoir est pire que pas de .sym —
    // mais AVANT le refus des banques >= 8, ou l'assemblage a reussi et ou seul
    // l'export a plat echoue. C'est justement la que les adresses sont utiles.
    if (wantSym) {
        const std::string table = sym::format(out);
        std::ofstream f(symPath, std::ios::binary);
        if (!f) { fprintf(stderr, "error: cannot write: %s\n", symPath.c_str()); return 2; }
        f.write(table.data(), (std::streamsize)table.size());
        fprintf(stderr, "%s: %zu symboles\n", symPath.c_str(), out.symbolTable.size());
    }
    if (dumpOnly) return 0;   // -E --sym : les deux sorties demandees sont ecrites

    // Le dump est PLAT : 64 Ko s'il ne sort pas des banques 0..3, 128 Ko pour le
    // 6128 complet. Au-dela de la banque 7, aucun dump plat ne peut porter les
    // octets : il faudrait les chunks MEM du v3 (ADR 0006). On le dit en nommant
    // les banques, plutot que d'ecrire un fichier ampute qui aurait l'air correct.
    int dumpKo = 64;
    std::string tooHigh;
    for (int b : out.banksWritten) {
        if (b >= 8) tooHigh += (tooHigh.empty() ? "" : ", ") + std::to_string(b);
        else if (b >= 4) dumpKo = 128;
    }
    if (!tooHigh.empty()) {
        fprintf(stderr, "error: bank(s) %s written, beyond bank 7: a flat dump stops at 128K. "
                        "Assembling there works, exporting does not yet — it needs the chunked v3 "
                        "snapshot.\n", tooHigh.c_str());
        return 1;
    }

    // 3) write out : .sna -> snapshot ; otherwise raw binary
    bool asSna = wantSna;
    std::vector<uint8_t> data;
    if (asSna) {
        sna::Options o; o.pc = out.runAddress;
        data = sna::build(out.image, o, hasBase ? &base : nullptr,
                          hasBase ? &out.coverage : nullptr, dumpKo);
    } else {
        // Le binaire brut est un intervalle contigu d'adresses logiques : il n'a
        // pas de place pour dire « et ces octets-la sont en banque 5 ».
        if (dumpKo > 64)
            fprintf(stderr, "warning: banks beyond the base 64K were written; a raw binary cannot "
                            "carry them — export a .sna to keep them\n");
        data = out.bin;
    }
    std::ofstream f(outPath, std::ios::binary);
    if (!f) { fprintf(stderr, "error: cannot write: %s\n", outPath.c_str()); return 2; }
    f.write((const char *)data.data(), (std::streamsize)data.size());
    if (asSna) {
        if (hasBase)
            fprintf(stderr, "base: %s (CPCType %d)\n", basePath.c_str(), (int)base.cpcType);
        else
            fprintf(stderr, "base: aucune (hors du code assemble, la memoire vaut zero)\n");
        fprintf(stderr, "%s: snapshot (%zu bytes), PC=0x%04X\n", outPath.c_str(), data.size(), out.runAddress);
    }
    else
        fprintf(stderr, "%s: %zu bytes @ 0x%04X\n", outPath.c_str(), data.size(), out.loadAddress);

    if (showSyms)
        for (auto &s : out.symbols)
            fprintf(stderr, "  %-20s = 0x%04llX\n", s.first.c_str(), (unsigned long long)(s.second & 0xFFFF));
    return 0;
}
