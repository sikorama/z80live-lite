// asm_main.cpp - end-to-end CLI: .asm source -> preprocessor -> assembler -> .bin
//
//   fantams file.asm [-o output.bin] [-s] [-E] [--beautify] [--base base.sna]
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
//          forme » de l'editeur appelle.
#include "asm.h"
#include "beautify.h"
#include "pp.h"
#include "sna.h"

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
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (a == "--base" && i + 1 < argc) basePath = argv[++i];
        else if (a == "-s") showSyms = true;
        else if (a == "-E") dumpOnly = true;
        else if (a == "--beautify") beautifyOnly = true;
        else path = a;
    }
    if (path.empty()) { fprintf(stderr, "usage: fantams file.asm [-o output.bin] [-s] [-E] [--beautify] [--base base.sna]\n"); return 2; }
    if (outPath.empty()) {
        size_t dot = path.find_last_of('.');
        std::string stem = (dot == std::string::npos ? path : path.substr(0, dot));
        outPath = stem + (beautifyOnly ? ".fmt.asm" : dumpOnly ? ".pp.asm" : ".bin");
    }

    if (beautifyOnly && dumpOnly) {
        fprintf(stderr, "error: -E et --beautify demandent deux sorties differentes : la source deroulee, ou le source mis en forme\n");
        return 2;
    }

    bool wantSna = outPath.size() >= 4 && outPath.substr(outPath.size() - 4) == ".sna";
    if (!basePath.empty() && (dumpOnly || beautifyOnly || !wantSna)) {
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

    // --beautify : mise en forme SEULE. Ni preprocesseur ni assemblage — c'est
    // tout l'interet : le source de l'editeur garde ses macros et ses includes.
    //
    // Temps PREPROCESSEUR : ce texte est celui que le preprocesseur va lire, ses
    // mots-cles y sont donc vivants. Au temps d'assemblage, « MEND » n'est pas
    // reserve et serait lu comme un label seul sur sa ligne — le beautify lui
    // ajouterait un deux-points et detruirait le source.
    if (beautifyOnly) {
        std::string text = beautify::apply(content, kw::Phase::Preprocess);
        std::ofstream f(outPath, std::ios::binary);
        if (!f) { fprintf(stderr, "error: cannot write: %s\n", outPath.c_str()); return 2; }
        f.write(text.data(), (std::streamsize)text.size());
        fprintf(stderr, "%s: source mis en forme\n", outPath.c_str());
        return 0;
    }

    // 1) preprocessor
    pp::Result pre = pp::preprocess(content, path, readFile);
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
        std::string text = beautify::apply(pre.dump(), kw::Phase::Assembly);
        std::ofstream f(outPath, std::ios::binary);
        if (!f) { fprintf(stderr, "error: cannot write: %s\n", outPath.c_str()); return 2; }
        f.write(text.data(), (std::streamsize)text.size());
        fprintf(stderr, "%s: unrolled source (%zu lines)\n", outPath.c_str(), pre.lines.size());
        return 0;
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

    // 3) write out : .sna -> snapshot ; otherwise raw binary
    bool asSna = wantSna;
    std::vector<uint8_t> data;
    if (asSna) {
        sna::Options o; o.pc = out.runAddress;
        data = sna::build(out.image, o, hasBase ? &base : nullptr,
                          hasBase ? &out.coverage : nullptr);
    } else {
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
