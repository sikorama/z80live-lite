// pp.h - Préprocesseur fantams (texte -> texte)
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
    bool col0 = false;   // true si, dans le fichier source d'origine, la ligne commençait en colonne 1
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
    std::vector<Diagnostic> warnings; // bonnes pratiques (non bloquant), ex: "ei:ret" collé
    std::string dump() const;         // texte pour -E (lignes jointes par \n)
};

// `strict` (ADR 0017) : refuse tout ce qui n'est pas du Z80 canonique — le sucre
// un-vers-plusieurs (`push hl,de`, plusieurs opcodes sur une ligne) comme les
// orthographes non canoniques (`defb`). C'est le drapeau du PIPELINE : l'auteur qui
// le demande demande « ma source est-elle canonique ? ». Il n'AJOUTE rien, il refuse.
Result preprocess(const std::string &mainContent, const std::string &mainFile,
                  const FileProvider &files, bool strict = false);

// Canoniser SANS dérouler (ADR 0017). Applique la seule canonisation — orthographes
// non canoniques et opcodes composés — en laissant macros, boucles et conditions
// exactement où elles sont. C'est ce qui distingue `--normalize` de `-E` : `-E`
// canonise PUIS déroule, et les deux sorties ne sont donc pas la même.
//
// Le nombre de lignes change délibérément : `push hl,de` en devient deux. La
// préservation des lignes est l'invariant du beautify (ADR 0013), pas le sien.
//
// Une ligne portant un `{...}` est rendue telle quelle : sa forme dépend du site
// d'appel, et il peut y en avoir plusieurs. Ce n'est pas signalé — la promesse est
// « plus d'orthographe obsolète, un opcode par ligne », pas « tout est canonique ».
//
// Ne peut pas échouer : c'est une transformation de texte, pas une analyse.
// Idempotente, et l'assemblage du résultat rend les mêmes octets.
std::string normalize(const std::string &src);

} // namespace pp
