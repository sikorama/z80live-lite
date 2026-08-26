// asm.h - Assembleur 2 passes (fantams)
//
// Entrée : lignes de source DÉJÀ préprocessées (plates : ni macros ni includes).
// Passe 1 : calcule les adresses (ORG) et collecte tous les symboles.
// Passe 2 : encode réellement, avec les références avant résolues.
//
// La taille d'une instruction Z80 dépend du TYPE des opérandes (pas de leur
// valeur), donc la passe 1 obtient des adresses correctes sans connaître encore
// les valeurs des symboles.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace asmb {

struct SourceLine {
    std::string text;
    std::string file;
    int line = 0;
    bool col0 = false;   // true si, dans la source d'origine, la ligne commençait en colonne 1 (sans indentation)
};

struct Diagnostic {
    std::string file;
    int line = 0;
    std::string message;
};

// Une entree de la table des symboles exportable (`--sym`, ADR 0019).
//
// `value` est l'adresse LOGIQUE : ce que le nom vaut dans une expression, et ce
// qu'un desassembleur doit substituer. `bank` et `store` decrivent le RANGEMENT :
// ou l'octet est reellement ecrit. Les deux ne divergent que dans un bloc
// « org <logique>,<rangement> ».
//
// Une CONSTANTE n'habite nulle part : `bank` et `store` valent -1, ce que la
// table rend par un tiret. Les VARIABLES ('=') n'entrent pas dans la table — leur
// valeur change en cours de route, et un desassembleur n'en ferait rien.
struct Symbol {
    std::string name;       // tel que l'assembleur le connait : QUALIFIE et MANGLE
    bool isConst = false;   // EQU ; sinon label
    int64_t value = 0;      // adresse logique, ou valeur de la constante
    int bank = -1;          // banque de rangement, -1 pour une constante
    int store = -1;         // adresse de rangement, -1 pour une constante
    std::string file;       // fichier D'ORIGINE, avant preprocesseur
    int line = 0;           // ligne dans ce fichier
};

struct Output {
    bool ok = true;
    std::vector<uint8_t> bin;                 // octets [loadAddress .. loadAddress+size)
    uint16_t loadAddress = 0;                  // 1re adresse écrite
    uint16_t runAddress = 0;                    // point d'entrée (directive RUN, sinon = loadAddress)
    std::map<std::string, int64_t> symbols;
    // La table exportable (ADR 0019) : les memes noms que `symbols`, moins les
    // variables, plus le type et la provenance. Rendue dans l'ordre des noms ;
    // le tri du fichier (banque, rangement, nom) appartient au format, pas ici.
    std::vector<Symbol> symbolTable;
    std::vector<Diagnostic> errors;
    std::vector<Diagnostic> warnings;          // bonnes pratiques (non bloquant) : label sans ':', instruction en colonne 1...
    std::vector<Diagnostic> prints;            // sorties de PRINT (diagnostic de build, ni erreur ni avertissement)
    // Image mémoire des banques 0..7, à plat : l'octet (banque b, offset o) est en
    // `b * 0x4000 + o`. Les banques 0..3 sont les 64 K de base, 4..7 l'extension
    // du 6128. 131 072 octets, quelle que soit l'étendue réellement écrite.
    std::vector<uint8_t> image;
    // coverage : 65536 octets, non nul là où le source a RÉELLEMENT écrit. Elle
    // distingue « le source a écrit 0x00 ici » de « le source n'a rien écrit
    // ici » — distinction que l'image seule ne porte pas, et sans laquelle le
    // backend ne saurait pas quels octets d'une base laisser en place (ADR 0012).
    std::vector<uint8_t> coverage;
    // Les banques où le source a RÉELLEMENT écrit, triées. L'appelant en tire
    // deux décisions que l'assembleur n'a pas à prendre : la taille du dump —
    // 64 K si tout tient dans les banques 0..3, 128 K sinon — et le refus des
    // banques >= 8, qu'aucun dump plat ne peut porter (ADR 0006).
    std::vector<int> banksWritten;
};

Output assemble(const std::vector<SourceLine> &lines);
Output assembleText(const std::string &source, const std::string &file);

} // namespace asmb
