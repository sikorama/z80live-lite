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

// `image` porte les banques 0..7 à plat. `dumpKo` vaut 64 (banques 0..3) ou 128
// (le 6128 complet) : c'est l'appelant qui tranche, à partir des banques que le
// source a réellement écrites. Renvoie l'en-tête (256o) + le dump.
//
// Le dump reste PLAT dans les deux cas. Les chunks `MEM0`/`MEM1` du v3 ne
// deviennent nécessaires qu'au-delà de 128 K, ou pour la compression ; un dump
// plat de 128 K est lu par tout ce qui lit un dump de 64 K, ce que des chunks ne
// garantissent pas.
//
// Avec une base, `coverage` (non nul là où le source a écrit — cf.
// `asmb::Output::coverage`) départage : hors coverage, l'octet vient de la base.
// Une coverage absente ferait écraser toute la base par l'image, ce qui vide la
// base de son sens : l'appelant doit la fournir. La base ne couvre que les 64 K
// de base — le firmware ne vit pas dans l'extension — donc les banques hautes
// viennent toujours de l'image.
std::vector<uint8_t> build(const std::vector<uint8_t> &image, const Options &opt,
                           const Base *base = nullptr,
                           const std::vector<uint8_t> *coverage = nullptr,
                           int dumpKo = 64);

} // namespace sna
