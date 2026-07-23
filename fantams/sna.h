// sna.h - Export snapshot CPC (.sna)
//
// Génère un snapshot CPC : en-tête 256 octets « MV - SNA » + image mémoire 64K.
// Les valeurs matérielles par défaut (palette, CRTC, PPI, gate array) reprennent
// celles de rasm pour qu'un émulateur démarre correctement.
#pragma once

#include <cstdint>
#include <vector>

namespace sna {

struct Options {
    uint8_t version = 3;    // 2 ou 3
    uint16_t pc = 0;        // point d'entrée (PC)
    uint16_t sp = 0xC000;   // pile
    uint8_t cpcType = 2;    // 0=464 1=664 2=6128 ...
};

// `image64k` doit faire 65536 octets. Renvoie l'en-tête (256o) + le dump 64K.
std::vector<uint8_t> build(const std::vector<uint8_t> &image64k, const Options &opt);

} // namespace sna
