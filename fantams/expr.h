// expr.h - Évaluateur d'expressions entières réutilisable (fantams)
//
// Autonome, sans état global. Le resolver de symboles est injecté : le
// préprocesseur l'utilise avec les seules variables PP ; l'assembleur le
// réutilisera plus tard avec un resolver connaissant les labels.
#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace expr {

// Renvoie true et remplit `out` si le symbole est connu, false sinon.
// Le résolveur rend un DOUBLE : une constante ou une variable peut valoir un
// réel, et l'arrondir au passage perdrait l'information avant même que
// l'expression soit calculée. Cf. ADR 0008 — un seul type, jusqu'au bout.
using Resolver = std::function<bool(const std::string &name, double &out)>;

struct Result {
    bool ok = false;
    int64_t value = 0;    // valeur arrondie, pour l'émission d'octets
    double real = 0;      // valeur exacte, pour un stockage sans perte
    std::string error;
};

// Évalue une expression entière. Opérateurs (précédence C) :
//   || && | ^ &  == !=  < <= > >=  << >>  + -  * / %  unaires - ~ !
// Nombres : décimal, 0x.. / $.. / #.. (hexa), %.. (binaire), 'c' (caractère).
Result eval(const std::string &text, const Resolver &resolver);

} // namespace expr
