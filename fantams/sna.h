// sna.h - Export snapshot CPC (.sna)
//
// Génère un snapshot CPC : en-tête 256 octets « MV - SNA » + image mémoire 64K.
// Les valeurs matérielles par défaut (palette, CRTC, PPI, gate array) reprennent
// celles de rasm pour qu'un émulateur démarre correctement.
//
// Une **base** (ADR 0012) remplace ces défauts : c'est un snapshot de référence
// pris après le boot d'une machine, dont l'en-tête ET la mémoire font foi. Les
// octets écrits par le source sont posés dessus, désignés par la coverage.
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace sna {

struct Options {
    uint8_t version = 3;    // 2 ou 3
    uint16_t pc = 0;        // point d'entrée (PC)
    uint16_t sp = 0xC000;   // pile        — ignoré quand une base est fournie
    uint8_t cpcType = 2;    // 0=464 1=664 2=6128 ... — idem
};

// Une base lue et validée : en-tête matériel + les 64K de mémoire post-boot.
struct Base {
    std::vector<uint8_t> header;   // 256 octets, repris intégralement sauf PC
    std::vector<uint8_t> memory;   // 65536 octets
    uint8_t cpcType = 0;           // offset 0x6D de l'en-tête, pour comparaison
};

// Lit une base depuis les octets d'un .sna. N'accepte que le v1/v2 à dump plat
// de 64K : refuse le reste en nommant le remplaçant, plutôt que de deviner.
bool parseBase(const std::vector<uint8_t> &snapshot, Base &out, std::string &error);

// `image64k` doit faire 65536 octets. Renvoie l'en-tête (256o) + le dump 64K.
//
// Avec une base, `coverage` (65536 octets, non nul là où le source a écrit — cf.
// `asmb::Output::coverage`) départage : hors coverage, l'octet vient de la base.
// Une coverage absente ferait écraser toute la base par l'image, ce qui vide la
// base de son sens : l'appelant doit la fournir.
std::vector<uint8_t> build(const std::vector<uint8_t> &image64k, const Options &opt,
                           const Base *base = nullptr,
                           const std::vector<uint8_t> *coverage = nullptr);

} // namespace sna
