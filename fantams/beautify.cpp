// beautify.cpp - Mise en forme d'un source assembleur (voir beautify.h)
#include "beautify.h"

#include <cctype>
#include <string>

namespace beautify {
namespace {

const char *const INDENT = "    ";

std::string upper(std::string s) {
    for (char &c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}
std::string trim(const std::string &s) {
    size_t a = 0, b = s.size();
    while (a < b && std::isspace((unsigned char)s[a])) ++a;
    while (b > a && std::isspace((unsigned char)s[b - 1])) --b;
    return s.substr(a, b - a);
}
std::string firstToken(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = a; while (b < s.size() && !std::isspace((unsigned char)s[b])) ++b;
    return s.substr(a, b - a);
}

// Une seule ligne. Les modifications sont TEXTUELLES et minimales : on n'insère
// qu'un deux-points, ou on ne remplace que l'indentation. Tout le reste de la
// ligne — espacement interne, position du commentaire, espaces de fin, '\r'
// éventuel — est recopié tel quel. C'est ce qui garantit l'invariant d'octets :
// aucune réécriture d'opérande ne peut se glisser là.
std::string line(const std::string &ln, kw::Phase ph) {
    const size_t cp = kw::commentPos(ln);
    const std::string code = (cp == std::string::npos) ? ln : ln.substr(0, cp);

    const size_t ind = code.find_first_not_of(" \t");
    if (ind == std::string::npos) return ln; // ligne vide, ou commentaire seul

    const std::string body = trim(code);
    std::string label, rest;
    bool sawColon = true;
    kw::peelLabel(body, label, rest, ph, &sawColon);

    if (!label.empty()) {
        // Règle 1 — et son garde-fou : SEUL sur sa ligne. Un label suivi de
        // quelque chose est soit déjà `label: instruction` (rien à faire), soit
        // un appel de macro qu'on n'a pas le droit de deviner.
        if (!sawColon && rest.empty()) {
            const size_t at = ind + label.size();
            return ln.substr(0, at) + ":" + ln.substr(at);
        }
        return ln;
    }

    // Pas de label : soit la ligne commence par un mot réservé à cette phase —
    // instruction Z80 ou directive —, soit `peelLabel` n'a rien su éplucher
    // (ligne commençant par un caractère non identifiant, macro en cours de
    // déclaration…), et dans ce cas on ne touche à rien.
    if (!kw::isReservedWord(upper(firstToken(body)), ph)) return ln;

    // Règle 2 — l'indentation est REMPLACÉE, pas complétée : une tabulation
    // n'est pas une indentation, c'est une indentation dont la largeur dépend du
    // lecteur, et huit espaces délibérés valent quatre espaces ici.
    return INDENT + ln.substr(ind);
}

} // namespace

std::string apply(const std::string &src, kw::Phase ph) {
    std::string out;
    out.reserve(src.size() + src.size() / 16);
    size_t i = 0;
    for (;;) {
        const size_t nl = src.find('\n', i);
        const size_t end = (nl == std::string::npos) ? src.size() : nl;
        out += line(src.substr(i, end - i), ph);
        if (nl == std::string::npos) break;
        out += '\n';
        i = nl + 1;
    }
    return out;
}

} // namespace beautify
