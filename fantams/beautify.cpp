// beautify.cpp - Mise en forme d'un source assembleur (voir beautify.h)
#include "beautify.h"

#include <cctype>
#include <set>
#include <string>
#include <vector>

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

std::string secondToken(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    while (a < s.size() && !std::isspace((unsigned char)s[a])) ++a;
    return firstToken(s.substr(a));
}

// PRESCAN — les noms de macro définis dans le texte lui-même.
//
// Le beautify ne peut pas trancher « nom seul sur sa ligne » sans ça : un appel
// de macro SANS ARGUMENT a exactement cette forme, et lui ajouter un deux-points
// le change en label, c'est-à-dire en rien. L'ADR 0013 n'avait examiné que
// `sprite 4,12` et en avait conclu, à tort, que la forme « nom seul » était sans
// ambiguïté.
//
// Les DEUX graphies comptent, parce que le préprocesseur accepte les deux
// (pp.cpp, « MACRO name » et « name MACRO ») : n'en lire qu'une laisserait les
// macros de l'autre se faire détruire à chaque appel. Le ':' final de
// « macro foo: » n'appartient pas au nom, là aussi comme le préprocesseur.
//
// Le balayage est purement textuel — il ne suit pas les `include`. C'est une
// limite assumée, pas un oubli : la règle de colonne ci-dessous couvre le reste.
std::set<std::string> collectMacroNames(const std::string &src) {
    std::set<std::string> names;
    size_t i = 0;
    while (i <= src.size()) {
        const size_t nl = src.find('\n', i);
        const size_t end = (nl == std::string::npos) ? src.size() : nl;
        const std::string raw = src.substr(i, end - i);
        const size_t cp = kw::commentPos(raw);
        const std::string body = trim(cp == std::string::npos ? raw : raw.substr(0, cp));
        const std::string t0 = firstToken(body), t1 = secondToken(body);
        std::string name;
        if (upper(t0) == "MACRO") name = t1;
        else if (upper(t1) == "MACRO") name = t0;
        if (!name.empty() && name.back() == ':') name.pop_back();
        // « macro nom(p,q) » : la liste de paramètres n'appartient pas au nom.
        { std::string n, a; if (kw::parenCall(name, n, a)) name = n; }
        if (!name.empty()) names.insert(upper(name));
        if (nl == std::string::npos) break;
        i = nl + 1;
    }
    return names;
}

// Ce premier mot, écrit SANS deux-points, est-il un label ?
//
// Trois façons de répondre non, dans l'ordre de leur force :
//
//   - C'est une macro connue. Preuve directe, tirée du texte.
//   - C'est une DÉFINITION : `nom EQU v`, `nom = v`, `nom MACRO p`. Ce sont les
//     formes canoniques — le deux-points n'y a pas cours, et `asm.cpp` les
//     exclut déjà de son avertissement. Le beautify doit refléter exactement le
//     même test : il n'éteint que des avertissements existants, il n'en invente
//     pas.
//   - Le doute demeure, et alors la RÉPONSE DÉPEND DE CE QUI SUIT :
//       · rien — la colonne tranche. L'assembleur avertit déjà que « only
//         labels/symbols should start in column 1 » : un nom indenté n'est donc
//         pas un label, c'est un appel de macro qu'un `include` nous cache.
//       · du code — le premier mot du reste doit être RÉSERVÉ. `pcoltab dw
//         coltab` est décidable : aucun appel de macro ne commence par `dw`.
//         `sprite 4,12` ne l'est pas, et reste intact.
//
// Le prix est le même que celui qu'assumait déjà l'ADR 0013 : le beautify
// n'éteint pas TOUS les avertissements. Un label indenté garde le sien. C'est le
// bon sens de l'échange — laisser un avertissement debout coûte une ligne à
// corriger à la main, détruire un appel de macro coûte un après-midi.
bool looksLikeLabel(const std::string &label, const std::string &rest, size_t indent,
                    kw::Phase ph, const std::set<std::string> &macros) {
    if (macros.count(upper(label))) return false;
    // `nom(...)` est un appel, quoi qu'on sache par ailleurs du nom (ADR 0018).
    // C'est la seule graphie qui se lit sans connaître les macros — donc la seule
    // qui vaille pour une macro pas encore écrite.
    { std::string n, a; if (kw::parenCall(label + (rest.empty() ? "" : " " + rest), n, a)) return false; }
    const std::string head = upper(firstToken(rest));
    if (head == "MACRO" || head == "EQU") return false;
    const size_t eq = kw::findAssign(rest);
    if (eq != std::string::npos && trim(rest.substr(0, eq)).empty()) return false;
    if (rest.empty()) return indent == 0;
    return kw::isReservedWord(head, ph);
}

// Retire les espaces de FIN, sans toucher au '\r' d'une fin de ligne CRLF : les
// fins de ligne sont préservées telles quelles (cf. beautify.h).
std::string rstrip(const std::string &ln) {
    size_t e = ln.size();
    while (e > 0 && ln[e - 1] == '\r') --e;      // la fin de ligne, mise de côté
    const std::string eol = ln.substr(e);
    while (e > 0 && (ln[e - 1] == ' ' || ln[e - 1] == '\t')) --e;
    return ln.substr(0, e) + eol;
}

// Les mots-clés de bloc portés par une ligne, dans l'ordre. Une ligne peut en
// porter plusieurs : `repeat 3 : dw a,b : rend` ouvre et referme (c'est la forme
// que `pp::splitBlockLines` éclate plus tard). On compte donc chaque instruction,
// pas seulement la première, sinon la profondeur dériverait pour tout le reste du
// fichier.
struct Delta { int opens = 0, closes = 0; bool firstIsCloser = false; };

Delta blockDelta(const std::string &body, kw::Phase ph) {
    Delta d;
    std::string label, rest;
    kw::peelLabel(body, label, rest, ph);
    // Découpe sur les ':' de séparation. Un label collé a déjà été épluché, et un
    // ':' en chaîne ou entre parenthèses ne sépare pas.
    std::string cur; bool inStr = false; char q = 0; int par = 0;
    std::vector<std::string> stmts;
    auto flush = [&]() { const std::string t = trim(cur); if (!t.empty()) stmts.push_back(t); cur.clear(); };
    for (char c : rest) {
        if (inStr) { cur += c; if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++par; cur += c; continue; }
        if (c == ')' || c == ']' || c == '}') { if (par > 0) --par; cur += c; continue; }
        if (c == ':' && par == 0) { flush(); continue; }
        cur += c;
    }
    flush();
    for (size_t k = 0; k < stmts.size(); ++k) {
        const std::string w = upper(firstToken(stmts[k]));
        // « nom MACRO p » : le mot-clé est en seconde position, et seulement sur
        // la première instruction (le label a déjà été retiré par peelLabel).
        if (k == 0 && upper(secondToken(stmts[k])) == "MACRO") { ++d.opens; continue; }
        if (!kw::blockOfOpener(w).empty()) ++d.opens;
        else if (!kw::blockOfCloser(w).empty()) {
            if (k == 0) d.firstIsCloser = true;
            ++d.closes;
        }
    }
    return d;
}

// Une seule ligne. Les modifications sont TEXTUELLES et minimales : on n'insère
// qu'un deux-points, ou on ne remplace que l'indentation. Tout le reste de la
// ligne — espacement interne, position du commentaire, espaces de fin, '\r'
// éventuel — est recopié tel quel. C'est ce qui garantit l'invariant d'octets :
// aucune réécriture d'opérande ne peut se glisser là.
std::string line(const std::string &ln, kw::Phase ph, bool detachLabels,
                 const std::set<std::string> &macros, const std::string &indent) {
    const size_t cp = kw::commentPos(ln);
    const std::string code = (cp == std::string::npos) ? ln : ln.substr(0, cp);

    const size_t ind = code.find_first_not_of(" \t");
    // Ligne vide, ou commentaire seul : l'alignement des commentaires appartient à
    // l'auteur (ADR 0013), on ne fait que retirer les espaces de fin.
    if (ind == std::string::npos) return rstrip(ln);

    const std::string body = trim(code);
    std::string label, rest;
    bool sawColon = true;
    kw::peelLabel(body, label, rest, ph, &sawColon);

    if (!label.empty()) {
        // Règle 1 — le deux-points. Le garde-fou n'est plus « seul sur sa ligne »
        // (un appel de macro sans argument a cette forme) mais `looksLikeLabel`,
        // qui interroge le texte au lieu de s'abstenir uniformément.
        // Ce premier mot n'est PAS un label : appel de macro, ou définition
        // (`v=0`). Les deux sont du code, et s'indentent au cran de leur bloc.
        // Déplacer une ligne horizontalement ne peut changer aucun octet — c'est
        // ce qui autorise à le faire même quand le doute subsiste, là où poser un
        // deux-points ne le permettait pas.
        if (!sawColon && !looksLikeLabel(label, rest, ind, ph, macros)) {
            // Appel d'une macro CONNUE écrit sans parenthèses : on les pose.
            // Autorisé par le critère de cet ADR — l'assembleur avertit désormais
            // sur la forme nue, et le beautify n'éteint que des avertissements qui
            // existent. Le nombre d'octets ne bouge pas : c'est la même expansion.
            std::string cn, ca;
            if (macros.count(upper(label)) && !kw::parenCall(body, cn, ca)) {
                const size_t cpc = kw::commentPos(ln);
                const std::string tl = (cpc == std::string::npos) ? std::string() : trim(ln.substr(cpc));
                return rstrip(indent + label + "(" + rest + ")" + (tl.empty() ? "" : " " + tl));
            }
            return rstrip(indent + ln.substr(ind));
        }

        // Règle 4 — un label va en COLONNE 1, quelle que soit la profondeur.
        // Le beautify n'observe pas cette convention, il l'établit : après un
        // passage, tout label est en colonne 1, donc un nom indenté n'est pas un
        // label. C'est ce qui donne son fondement à `looksLikeLabel`.
        // La tête est REPRISE telle quelle quand le deux-points est déjà là :
        // `start :` garde son espace. Aucune règle n'autorise à le retirer, et la
        // règle 4 ne déplace la ligne que horizontalement.
        std::string head = label + ":";
        if (sawColon) {
            const size_t col = code.find(':', ind);
            if (col != std::string::npos) head = trim(code.substr(ind, col - ind + 1));
        }

        // Règle 3 — détachement. Il suit le deux-points, qu'il ait été écrit par
        // l'auteur ou posé à l'instant : une fois établi que c'est un label, les
        // deux cas ne se distinguent plus.
        const size_t cp2 = kw::commentPos(ln);
        const std::string tail = (cp2 == std::string::npos) ? std::string() : trim(ln.substr(cp2));
        if (!rest.empty()) {
            if (detachLabels)
                // Le commentaire suit le CODE, pas le label : c'est lui qu'il commente.
                return head + "\n" + rstrip(indent + rest + (tail.empty() ? "" : " " + tail));
            return rstrip(head + " " + rest + (tail.empty() ? "" : " " + tail));
        }
        return rstrip(head + (tail.empty() ? "" : " " + tail));
    }

    // « nom MACRO p,q » -> « macro nom p,q ». peelLabel rend ici un label VIDE
    // (c'est son exception : le nom n'est pas un label, c'est la macro en cours de
    // définition), donc le cas se traite avant la règle 2. Autorisé par le même
    // critère que les parenthèses : l'assembleur avertit sur cette graphie.
    if (ph == kw::Phase::Preprocess) {
        const std::string t0 = firstToken(body);
        if (upper(secondToken(body)) == "MACRO" && kw::isIdentifier(t0)) {
            const std::string tail2 = trim(body.substr(t0.size() + secondToken(body).size() + 1));
            const size_t cpm = kw::commentPos(ln);
            const std::string cm = (cpm == std::string::npos) ? std::string() : trim(ln.substr(cpm));
            // La casse du mot-clé d'origine est épousée, comme dans la canonisation.
            const std::string kwWord = secondToken(body) == "MACRO" ? "MACRO" : "macro";
            return rstrip(indent + kwWord + " " + t0 + (tail2.empty() ? "" : " " + tail2) +
                          (cm.empty() ? "" : " " + cm));
        }
    }

    // Pas de label : soit la ligne commence par un mot réservé à cette phase —
    // instruction Z80 ou directive —, soit `peelLabel` n'a rien su éplucher
    // (ligne commençant par un caractère non identifiant, macro en cours de
    // déclaration…), et dans ce cas on ne touche à rien.
    if (!kw::isReservedWord(upper(firstToken(body)), ph)) return rstrip(ln);

    // Règle 2 — l'indentation est REMPLACÉE, pas complétée : une tabulation
    // n'est pas une indentation, c'est une indentation dont la largeur dépend du
    // lecteur, et huit espaces délibérés valent quatre espaces ici. Sa largeur
    // est celle du bloc courant (règle 4).
    return rstrip(indent + ln.substr(ind));
}

} // namespace

std::string apply(const std::string &src, kw::Phase ph, bool detachLabels, bool indentBlocks,
                  const std::vector<std::string> &knownMacros) {
    // Le prescan lit TOUT le texte avant qu'une seule ligne soit mise en forme :
    // une macro peut être appelée avant d'être définie. Ce que l'appelant sait en
    // plus (les includes, via le préprocesseur) s'y ajoute.
    std::set<std::string> macros = collectMacroNames(src);
    macros.insert(knownMacros.begin(), knownMacros.end());
    std::string out;
    out.reserve(src.size() + src.size() / 16);
    size_t i = 0;
    int depth = 0;
    for (;;) {
        const size_t nl = src.find('\n', i);
        const size_t end = (nl == std::string::npos) ? src.size() : nl;
        const std::string raw = src.substr(i, end - i);

        int lineDepth = depth;
        if (indentBlocks) {
            const size_t cp = kw::commentPos(raw);
            const std::string body = trim(cp == std::string::npos ? raw : raw.substr(0, cp));
            const Delta d = blockDelta(body, ph);
            // Une ligne qui COMMENCE par une fermeture se rend au cran de son
            // ouvreur, pas à celui du corps : `rend` s'aligne sur son `repeat`.
            if (d.firstIsCloser && lineDepth > 0) --lineDepth;
            depth += d.opens - d.closes;
            // Une fermeture orpheline ne fait pas dériver le reste du fichier vers
            // la gauche : le préprocesseur la signalera, la mise en forme se tait.
            if (depth < 0) depth = 0;
        }
        std::string indent;
        for (int k = 0; k <= lineDepth; ++k) indent += INDENT;

        out += line(raw, ph, detachLabels, macros, indent);
        if (nl == std::string::npos) break;
        out += '\n';
        i = nl + 1;
    }
    return out;
}

} // namespace beautify
