// sym.cpp - Table des symboles exportable (ADR 0019)
#include "sym.h"

#include <algorithm>
#include <cstdio>

namespace sym {
namespace {

// Hexa de largeur VARIABLE : un lecteur qui voit « 0x1F » la ou il attendait
// « 0x001F » s'en sort, un lecteur qui doit deviner la base selon la colonne
// `type`, non. Le signe est porte devant : une constante peut valoir -1, et
// « 0xFFFFFFFFFFFFFFFF » ne serait pas la valeur que l'assembleur a utilisee.
std::string hex(int64_t v) {
    char b[32];
    if (v < 0) snprintf(b, sizeof b, "-0x%llX", (unsigned long long)(-(v + 1)) + 1ULL);
    else snprintf(b, sizeof b, "0x%llX", (unsigned long long)v);
    return b;
}

// Guillemetage RFC 4180, applique SEULEMENT si le champ en a besoin. Un chemin
// contenant une virgule est rare mais legal, et sans ce guillemetage il produirait
// une ligne a huit champs au milieu d'un fichier a sept — le seul mode d'echec
// silencieux que ce format puisse avoir.
std::string csv(const std::string &s) {
    if (s.find(',') == std::string::npos && s.find('"') == std::string::npos) return s;
    std::string out = "\"";
    for (char c : s) {
        if (c == '"') out += "\"\"";
        else out += c;
    }
    out += '"';
    return out;
}

} // namespace

std::string format(const asmb::Output &o) {
    std::vector<asmb::Symbol> rows;
    rows.reserve(o.symbolTable.size());
    for (const auto &s : o.symbolTable) rows.push_back(s);

    // Les constantes n'ont ni banque ni rangement : elles se rangent en queue,
    // triees par nom. Le tri est TOTAL — deux symboles a la meme adresse est le
    // cas courant (« screen: » puis « .start: »), et sans departage la sortie
    // cesserait d'etre reproductible d'un assemblage a l'autre.
    std::sort(rows.begin(), rows.end(), [](const asmb::Symbol &a, const asmb::Symbol &b) {
        if (a.isConst != b.isConst) return !a.isConst;
        if (!a.isConst) {
            if (a.bank != b.bank) return a.bank < b.bank;
            if (a.store != b.store) return a.store < b.store;
        }
        return a.name < b.name;
    });

    std::string out = "name,type,value,bank,store,file,line\n";
    for (const auto &s : rows) {
        out += csv(s.name);
        out += s.isConst ? ",const," : ",label,";
        out += hex(s.value);
        out += ',';
        out += s.isConst ? "-" : std::to_string(s.bank);
        out += ',';
        out += s.isConst ? "-" : hex(s.store);
        out += ',';
        out += csv(s.file);
        out += ',';
        out += std::to_string(s.line);
        out += '\n';
    }
    return out;
}

} // namespace sym
