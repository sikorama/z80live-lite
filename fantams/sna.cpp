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

namespace sna {

std::vector<uint8_t> build(const std::vector<uint8_t> &image64k, const Options &opt) {
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
