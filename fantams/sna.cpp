// sna.cpp - Export snapshot CPC (.sna) — voir sna.h
//
// Disposition de l'en-tête (offsets vérifiés sur la struct s_snapshot de rasm) :
//   0x00 "MV - SNA"      0x10 version
//   0x11 F A C B E D L H (registres)   0x19 R  0x1A I  0x1B IFF0 0x1C IFF1
//   0x1D IXl IXh IYl IYh 0x21 SPl SPh  0x23 PCl PCh    0x25 IM
//   0x26 registres alternatifs F A C B E D L H
//   0x2E pen  0x2F palette[17]  0x40 multiconfig  0x41 ramconfig
//   0x42 crtc sel  0x43 crtc regs[18]  0x55 romselect  0x56 PPI A B C ctrl
//   0x5A psg sel   0x5B psg regs[16]   0x6B dumpsize(2)  0x6D CPCType
//   0xA4 crtcstate.model  0xB2 vsyncdelay  0xB4 interruptrequestflag
#include "sna.h"

#include <cstdio>

namespace sna {

bool parseBase(const std::vector<uint8_t> &snapshot, Base &out, std::string &error) {
    // La signature d'abord : sur un fichier qui n'est pas un snapshot du tout,
    // « base tronquee » designerait le mauvais probleme.
    if (snapshot.size() < 8 || std::string((const char *)snapshot.data(), 8) != "MV - SNA") {
        error = "ce n'est pas un snapshot CPC : signature « MV - SNA » absente";
        return false;
    }
    if (snapshot.size() < 256 + 65536) {
        char b[128];
        snprintf(b, sizeof b, "base tronquee : %zu octets, il en faut 256 + 65536",
                 snapshot.size());
        error = b;
        return false;
    }
    uint8_t version = snapshot[0x10];
    if (version > 2) {
        error = "base en version " + std::to_string((int)version) +
                " : reenregistre-la en version 2 (en-tete + dump plat de 64K), "
                "les chunks MEM0 ne sont pas lus";
        return false;
    }
    int dumpKo = snapshot[0x6B] | (snapshot[0x6C] << 8);
    if (dumpKo != 64) {
        error = "base de " + std::to_string(dumpKo) +
                " Ko : une base ne peuple que les 64K de memoire de base";
        return false;
    }
    out.header.assign(snapshot.begin(), snapshot.begin() + 256);
    out.memory.assign(snapshot.begin() + 256, snapshot.begin() + 256 + 65536);
    out.cpcType = snapshot[0x6D];
    return true;
}

std::vector<uint8_t> build(const std::vector<uint8_t> &image64k, const Options &opt,
                           const Base *base, const std::vector<uint8_t> *coverage) {
    // Avec une base, son en-tete fait foi : l'etat materiel post-boot (ROM basse
    // activee, I, IM, SP dans la pile firmware, gate array) est coherent par
    // construction, et le recomposer a la main creerait une seconde verite.
    // Seul PC est patche — sinon le snapshot redemarrerait sur le BASIC.
    if (base) {
        std::vector<uint8_t> h = base->header;
        h[0x23] = (uint8_t)(opt.pc & 0xFF);
        h[0x24] = (uint8_t)((opt.pc >> 8) & 0xFF);

        std::vector<uint8_t> mem = base->memory;
        bool hasCov = coverage && coverage->size() >= 65536;
        size_t n = image64k.size() < 65536 ? image64k.size() : 65536;
        for (size_t a = 0; a < n; ++a)
            if (!hasCov || (*coverage)[a]) mem[a] = image64k[a];

        std::vector<uint8_t> out;
        out.reserve(256 + 65536);
        out.insert(out.end(), h.begin(), h.end());
        out.insert(out.end(), mem.begin(), mem.end());
        return out;
    }

    std::vector<uint8_t> h(256, 0);

    const char *sig = "MV - SNA";
    for (int i = 0; i < 8; ++i) h[i] = (uint8_t)sig[i];
    h[0x10] = opt.version;

    // registres (défaut 0), sauf ce qui est utile pour démarrer
    h[0x21] = (uint8_t)(opt.sp & 0xFF);        // SP bas
    h[0x22] = (uint8_t)((opt.sp >> 8) & 0xFF); // SP haut
    h[0x23] = (uint8_t)(opt.pc & 0xFF);        // PC bas
    h[0x24] = (uint8_t)((opt.pc >> 8) & 0xFF); // PC haut
    h[0x25] = 1;                                // IM 1

    // gate array : palette firmware par défaut
    static const uint8_t pal[17] = {
        0x04, 0x0A, 0x15, 0x1C, 0x18, 0x1D, 0x0C, 0x05, 0x0D,
        0x16, 0x06, 0x17, 0x1E, 0x00, 0x1F, 0x0E, 0x04};
    for (int i = 0; i < 17; ++i) h[0x2F + i] = pal[i];
    h[0x40] = 0x8D;   // multiconfig : ROM basse/haute off + mode 1
    h[0x41] = 0xC0;   // ramconfiguration

    // CRTC (type 6845) : registres par défaut
    h[0x43 + 0] = 0x3F;
    h[0x43 + 1] = 40;
    h[0x43 + 2] = 46;
    h[0x43 + 3] = 0x8E;
    h[0x43 + 4] = 38;
    h[0x43 + 6] = 25;
    h[0x43 + 7] = 30;
    h[0x43 + 9] = 7;
    h[0x43 + 12] = 0x30;

    // PPI
    h[0x59] = 0x82;   // control

    // PSG : tous canaux audio coupés
    h[0x5B + 7] = 0x3F;

    // taille du dump mémoire (en Ko)
    h[0x6B] = 64;
    h[0x6C] = 0;

    h[0x6D] = opt.cpcType;

    // état CRTC
    h[0xA4] = 0;      // model : CRTC 0
    h[0xB2] = 2;      // vsyncdelay

    // assemblage : en-tête + 64K mémoire
    std::vector<uint8_t> out;
    out.reserve(256 + 65536);
    out.insert(out.end(), h.begin(), h.end());
    if (image64k.size() >= 65536)
        out.insert(out.end(), image64k.begin(), image64k.begin() + 65536);
    else {
        out.insert(out.end(), image64k.begin(), image64k.end());
        out.resize(256 + 65536, 0);
    }
    return out;
}

} // namespace sna
