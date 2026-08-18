// pp_main.cpp - Outil "-E" : exporte la source préprocessée (texte plat).
//
//   ppdump fichier.asm         -> écrit la version expansée sur stdout
//   ppdump -m fichier.asm      -> ajoute un source-map (; fichier:ligne)
//
// Démontre l'étape préprocesseur séparée : macros, REPEAT/WHILE, IF et scope
// sont entièrement résolus AVANT l'assembleur.
#include "pp.h"

#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>

// FileProvider adossé au système de fichiers (répertoire courant + chemins relatifs).
static bool readFile(const std::string &path, std::string &out) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return false;
    std::ostringstream ss; ss << f.rdbuf();
    out = ss.str();
    return true;
}

int main(int argc, char **argv) {
    bool withMap = false;
    const char *path = nullptr;
    for (int i = 1; i < argc; ++i) {
        std::string a = argv[i];
        if (a == "-m") withMap = true;
        else path = argv[i];
    }
    if (!path) { fprintf(stderr, "usage: ppdump [-m] fichier.asm\n"); return 2; }

    std::string content;
    if (!readFile(path, content)) { fprintf(stderr, "erreur: fichier introuvable: %s\n", path); return 2; }

    pp::Result r = pp::preprocess(content, path, readFile);

    if (withMap)
        for (const auto &l : r.lines) printf("%-40s ; %s:%d\n", l.text.c_str(), l.file.c_str(), l.line);
    else
        fputs(r.dump().c_str(), stdout);

    for (const auto &w : r.warnings)
        fprintf(stderr, "%s:%d: avertissement: %s\n", w.file.c_str(), w.line, w.message.c_str());
    if (!r.ok) {
        for (const auto &e : r.errors)
            fprintf(stderr, "%s:%d: erreur: %s\n", e.file.c_str(), e.line, e.message.c_str());
        return 1;
    }
    return 0;
}
