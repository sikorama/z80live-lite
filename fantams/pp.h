// pp.h - Préprocesseur rasm-lite (texte -> texte)
//
// Étape SÉPARÉE de l'assembleur : expanse includes, macros, REPEAT/WHILE et
// résout le scope par renommage. Produit un texte plat, ré-assemblable, et
// exportable (option -E de l'assembleur).
//
// Modèle « PP strict » : le contrôle de flux (IF/REPEAT/WHILE) exige des
// expressions résolubles au préprocesseur (constantes, variables PP, arguments
// de macro). Une référence à un label temps-assemblage est une erreur.
//
// Scope « auto-local » : tout label défini dans un corps de macro (ou une
// itération de REPEAT) est rendu unique par expansion, sauf @@export.
#pragma once

#include <functional>
#include <string>
#include <vector>

namespace pp {

// Ligne de source avec provenance (pour les diagnostics et le source-map).
struct SrcLine {
    std::string text;
    std::string file;
    int line = 0;
};

// Fourniture du contenu d'un fichier inclus (injectée : testable, WASM-friendly).
// Renvoie true et remplit `outContent` si trouvé.
using FileProvider = std::function<bool(const std::string &path, std::string &outContent)>;

struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
};

struct Result {
    bool ok = true;
    std::vector<SrcLine> lines;       // source expansée, plate
    std::vector<Diagnostic> errors;
    std::string dump() const;         // texte pour -E (lignes jointes par \n)
};

Result preprocess(const std::string &mainContent, const std::string &mainFile,
                  const FileProvider &files);

} // namespace pp
