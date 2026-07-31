// asm_main.cpp - CLI bout-en-bout : source .asm -> préprocesseur -> assembleur -> .bin
//
//   fantams fichier.asm [-o sortie.bin] [-s]
//     -o : fichier binaire de sortie (défaut : <source>.bin)
//     -s : affiche la table des symboles
#include "asm.h"
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
    std::string path, outPath;
    bool showSyms = false;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-o" && i + 1 < argc) outPath = argv[++i];
        else if (a == "-s") showSyms = true;
        else path = a;
    }
    if (path.empty()) { fprintf(stderr, "usage: fantams fichier.asm [-o sortie.bin] [-s]\n"); return 2; }
    if (outPath.empty()) {
        size_t dot = path.find_last_of('.');
        outPath = (dot == std::string::npos ? path : path.substr(0, dot)) + ".bin";
    }

    std::string content;
    if (!readFile(path, content)) { fprintf(stderr, "erreur: fichier introuvable: %s\n", path.c_str()); return 2; }

    // 1) préprocesseur
    pp::Result pre = pp::preprocess(content, path, readFile);
    if (!pre.ok) {
        for (auto &e : pre.errors) fprintf(stderr, "%s:%d: erreur (préproc): %s\n", e.file.c_str(), e.line, e.message.c_str());
        return 1;
    }

    // 2) assembleur (2 passes) sur le texte plat
    std::vector<asmb::SourceLine> lines;
    for (auto &l : pre.lines) lines.push_back({l.text, l.file, l.line});
    asmb::Output out = asmb::assemble(lines);
    if (!out.ok) {
        for (auto &e : out.errors) fprintf(stderr, "%s:%d: erreur: %s\n", e.file.c_str(), e.line, e.message.c_str());
        return 1;
    }

    // 3) écriture : .sna -> snapshot ; sinon binaire brut
    bool asSna = outPath.size() >= 4 && outPath.substr(outPath.size() - 4) == ".sna";
    std::vector<uint8_t> data;
    if (asSna) {
        sna::Options o; o.pc = out.runAddress;
        data = sna::build(out.image, o);
    } else {
        data = out.bin;
    }
    std::ofstream f(outPath, std::ios::binary);
    if (!f) { fprintf(stderr, "erreur: écriture impossible: %s\n", outPath.c_str()); return 2; }
    f.write((const char *)data.data(), (std::streamsize)data.size());
    if (asSna)
        fprintf(stderr, "%s : snapshot (%zu o), PC=0x%04X\n", outPath.c_str(), data.size(), out.runAddress);
    else
        fprintf(stderr, "%s : %zu octets @ 0x%04X\n", outPath.c_str(), data.size(), out.loadAddress);

    if (showSyms)
        for (auto &s : out.symbols)
            fprintf(stderr, "  %-20s = 0x%04llX\n", s.first.c_str(), (unsigned long long)(s.second & 0xFFFF));
    return 0;
}
