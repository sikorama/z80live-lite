// pp.cpp - Préprocesseur rasm-lite (voir pp.h)
#include "pp.h"
#include "expr.h"

#include <cctype>
#include <map>
#include <string>
#include <vector>

namespace pp {
namespace {

// --- petits utilitaires texte ---------------------------------------------
bool isIdentChar(char c) {
    return std::isalnum((unsigned char)c) || c == '_' || c == '.' || c == '@';
}
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
bool isIdentifier(const std::string &s) {
    if (s.empty() || std::isdigit((unsigned char)s[0])) return false;
    for (char c : s) if (!isIdentChar(c)) return false;
    return true;
}
// Retire le commentaire ';' (hors chaîne/caractère).
std::string stripComment(const std::string &s) {
    bool inStr = false; char q = 0;
    for (size_t i = 0; i < s.size(); ++i) {
        char c = s[i];
        if (inStr) { if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; }
        else if (c == ';') return s.substr(0, i);
    }
    return s;
}
std::string firstToken(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = a; while (b < s.size() && !std::isspace((unsigned char)s[b])) ++b;
    return s.substr(a, b - a);
}
std::string restAfterFirst(const std::string &s) {
    size_t a = 0; while (a < s.size() && std::isspace((unsigned char)s[a])) ++a;
    size_t b = a; while (b < s.size() && !std::isspace((unsigned char)s[b])) ++b;
    return trim(s.substr(b));
}
// Sépare un éventuel label de tête "ident:" du reste.
void peelLabel(const std::string &code, std::string &label, std::string &rest) {
    size_t p = 0; while (p < code.size() && isIdentChar(code[p])) ++p;
    if (p > 0) {
        size_t q = p; while (q < code.size() && std::isspace((unsigned char)code[q])) ++q;
        if (q < code.size() && code[q] == ':') {
            label = code.substr(0, p);
            rest = trim(code.substr(q + 1));
            return;
        }
    }
    label.clear(); rest = code;
}
// Découpe en respectant () [] {} "" ''.
std::vector<std::string> splitTopLevel(const std::string &s, char delim) {
    std::vector<std::string> out;
    if (trim(s).empty()) return out;
    int depth = 0; bool inStr = false; char q = 0; std::string cur;
    for (char c : s) {
        if (inStr) { cur += c; if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; cur += c; continue; }
        if (c == ')' || c == ']' || c == '}') { --depth; cur += c; continue; }
        if (c == delim && depth == 0) { out.push_back(trim(cur)); cur.clear(); continue; }
        cur += c;
    }
    out.push_back(trim(cur));
    return out;
}
// Remplace `from` par `to`, uniquement sur des mots entiers.
std::string replaceWord(const std::string &text, const std::string &from, const std::string &to) {
    std::string out; size_t i = 0;
    while (i < text.size()) {
        if (text.compare(i, from.size(), from) == 0 &&
            (i == 0 || !isIdentChar(text[i - 1])) &&
            (i + from.size() >= text.size() || !isIdentChar(text[i + from.size()]))) {
            out += to; i += from.size();
        } else out += text[i++];
    }
    return out;
}
// Découpe une ligne en instructions sur le séparateur ':' (retour à la ligne),
// en respectant chaînes/caractères et () [] {}. Le ':' qui termine un label de
// tête collé ("foo:") est conservé et NE coupe pas ; tout autre ':' sépare.
// (Choix : pp agnostique de l'assembleur -> heuristique « label collé », pas de
//  table de mnémoniques ; `nop : ret` fonctionne, `nop:ret` traite nop en label.)
std::vector<std::string> splitStatements(const std::string &s) {
    std::vector<std::string> out;
    std::string cur;
    bool inStr = false; char q = 0; int depth = 0; bool firstColon = true;
    auto flush = [&]() { std::string t = trim(cur); if (!t.empty()) out.push_back(t); cur.clear(); firstColon = true; };
    for (char c : s) {
        if (inStr) { cur += c; if (c == q) inStr = false; continue; }
        if (c == '"' || c == '\'') { inStr = true; q = c; cur += c; continue; }
        if (c == '(' || c == '[' || c == '{') { ++depth; cur += c; continue; }
        if (c == ')' || c == ']' || c == '}') { if (depth > 0) --depth; cur += c; continue; }
        if (c == ':' && depth == 0) {
            // label de tête collé (identifiant immédiatement suivi de ':') -> conservé
            if (firstColon && !cur.empty() && isIdentChar(cur.back()) && isIdentifier(trim(cur))) {
                cur += c; firstColon = false; continue;
            }
            flush(); continue;
        }
        cur += c;
    }
    std::string t = trim(cur); if (!t.empty()) out.push_back(t);
    return out;
}
// push/pop multi-registres -> un push/pop par registre : "push af,bc" => "push af" / "push bc".
// Sucre préprocesseur (les mnémoniques Z80 réels ne prennent qu'un opérande).
std::vector<std::string> expandPushPop(const std::string &stmt) {
    std::string label, rest; peelLabel(stmt, label, rest);
    std::string mnTok = firstToken(rest), MN = upper(mnTok);
    if (MN != "PUSH" && MN != "POP") return {stmt};
    auto regs = splitTopLevel(restAfterFirst(rest), ',');
    if (regs.size() <= 1) return {stmt};
    std::vector<std::string> out;
    for (size_t k = 0; k < regs.size(); ++k)
        out.push_back((k == 0 && !label.empty() ? label + ": " : "") + mnTok + " " + regs[k]);
    return out;
}

struct Macro {
    std::string name;
    std::vector<std::string> params;
    std::vector<SrcLine> body;
};

struct Env {
    std::map<std::string, std::string> args;   // arguments de macro (texte brut)
    std::map<std::string, int64_t> locals;      // variables de boucle (REPEAT/WHILE)
    std::string modulePrefix;                    // préfixe MODULE accumulé (ex "Outer_Inner_")
};

// Champ / définition de structure
struct StructField {
    std::string name;       // "" si anonyme
    std::string directive;  // DB/DW/DS/... ou nom d'une struct imbriquée (majuscules)
    std::string operands;   // valeurs par défaut (texte brut)
    int offset = 0;
    int size = 0;
    bool nested = false;    // directive = struct imbriquée
};
struct StructDef {
    std::string name;
    std::vector<StructField> fields;
    int size = 0;
};

// Est-ce une directive de données (taille connue) ?
inline bool isDataDir(const std::string &U) {
    return U == "DB" || U == "DEFB" || U == "DM" || U == "DEFM" ||
           U == "DW" || U == "DEFW" || U == "DS" || U == "DEFS" || U == "RMB";
}
// Nombre d'octets d'un littéral chaîne "..." (avec échappements simples).
int stringByteLen(const std::string &p) {
    if (p.size() < 2 || p[0] != '"') return 0;
    int n = 0;
    for (size_t k = 1; k < p.size(); ++k) {
        if (p[k] == '"') break;
        if (p[k] == '\\' && k + 1 < p.size()) ++k;
        ++n;
    }
    return n;
}
// Nombre de tokens séparés par des espaces.
int countTokens(const std::string &s) {
    int n = 0; size_t i = 0;
    while (i < s.size()) {
        while (i < s.size() && std::isspace((unsigned char)s[i])) ++i;
        if (i >= s.size()) break;
        while (i < s.size() && !std::isspace((unsigned char)s[i])) ++i;
        ++n;
    }
    return n;
}

// Mot-clé de bloc pour l'analyse d'imbrication (ou "" / mnémonique).
// ---------------------------------------------------------------------------
class PP {
public:
    explicit PP(const FileProvider &fp) : files(fp) {}
    Result result;

    void runFile(const std::string &content, const std::string &file) {
        run(splitLines(content, file), Env{}, 0);
    }

private:
    FileProvider files;
    std::map<std::string, int64_t> ppvars;      // variables PP globales (LET)
    std::map<std::string, Macro> macros;         // clé = nom majuscule
    std::map<std::string, StructDef> structs_;    // clé = nom majuscule
    long uid = 0;

    void error(const SrcLine &sl, const std::string &msg) {
        result.ok = false;
        result.errors.push_back({sl.file, sl.line, msg});
    }

    std::vector<SrcLine> splitLines(const std::string &content, const std::string &file) {
        std::vector<SrcLine> out; std::string cur; int ln = 1;
        for (size_t i = 0; i <= content.size(); ++i) {
            char c = (i < content.size()) ? content[i] : '\n';
            if (c == '\n') { if (!cur.empty() && cur.back() == '\r') cur.pop_back();
                out.push_back({cur, file, ln++}); cur.clear(); }
            else cur += c;
        }
        if (!out.empty() && out.back().text.empty()) out.pop_back();
        return out;
    }

    bool isDefined(const std::string &name, const Env &env) {
        return ppvars.count(name) || macros.count(upper(name)) ||
               env.locals.count(name) || env.args.count(name);
    }

    expr::Result evalPP(const std::string &text, const Env &env) {
        auto resolver = [&](const std::string &name, int64_t &out) -> bool {
            auto l = env.locals.find(name); if (l != env.locals.end()) { out = l->second; return true; }
            auto p = ppvars.find(name); if (p != ppvars.end()) { out = p->second; return true; }
            auto a = env.args.find(name);
            if (a != env.args.end()) { auto r = expr::eval(a->second, {}); if (r.ok) { out = r.value; return true; } }
            return false;
        };
        return expr::eval(text, resolver);
    }

    // Substitution {name} / {=expr} / {expr}
    std::string substitute(const std::string &text, const Env &env, const SrcLine &sl) {
        std::string out; size_t i = 0;
        while (i < text.size()) {
            if (text[i] == '{') {
                size_t j = i + 1; int d = 1;
                while (j < text.size() && d) { if (text[j] == '{') ++d; else if (text[j] == '}') --d; if (d) ++j; }
                if (j >= text.size()) { error(sl, "accolade '{' non fermée"); out += text.substr(i); break; }
                std::string inner = trim(text.substr(i + 1, j - i - 1));
                if (inner.empty()) error(sl, "substitution vide {}");
                else if (inner[0] == '=') {
                    auto r = evalPP(trim(inner.substr(1)), env);
                    if (!r.ok) error(sl, r.error); else out += std::to_string(r.value);
                } else if (isIdentifier(inner) && env.args.count(inner)) {
                    out += env.args.at(inner);
                } else {
                    auto r = evalPP(inner, env);
                    if (!r.ok) error(sl, r.error); else out += std::to_string(r.value);
                }
                i = j + 1;
            } else out += text[i++];
        }
        return out;
    }

    // Classe une ligne brute selon son mot-clé de bloc.
    std::string classify(const std::string &raw) {
        std::string code = trim(stripComment(raw));
        if (code.empty()) return "";
        std::string label, rest; peelLabel(code, label, rest);
        if (rest.empty()) return "";
        std::string t1 = firstToken(restAfterFirst(rest));
        if (upper(t1) == "MACRO") return "MACRO";
        std::string k = upper(firstToken(rest));
        // STRUCT : bloc seulement en DÉCLARATION (1 seul argument) ;
        // "STRUCT type instance" (2+ args) est une instanciation, pas un bloc.
        if (k == "STRUCT" && countTokens(restAfterFirst(rest)) != 1) return "STRUCTINS";
        return k;
    }

    void emit(const std::string &code, const SrcLine &src) {
        std::string t = trim(code);
        if (t.empty()) return;
        // ':' -> retour à la ligne ; push/pop multi-registres -> une instruction chacun.
        // Les sous-lignes après la 1re sont indentées (jamais lues comme un label).
        bool first = true;
        for (const auto &stmt : splitStatements(t))
            for (const auto &line : expandPushPop(stmt)) {
                result.lines.push_back({first ? line : "\t" + line, src.file, src.line});
                first = false;
            }
    }

    // Collecte les labels définis (par "ident:") dans un corps, hors @@export.
    // skipNestedModule=true : ignore les labels des blocs MODULE imbriqués
    // (ils seront préfixés par le MODULE interne lors de la récursion).
    std::vector<std::string> collectLabels(const std::vector<SrcLine> &body, bool skipNestedModule) {
        std::vector<std::string> locals;
        std::map<std::string, bool> exported;
        for (const auto &l : body) {
            std::string code = trim(stripComment(l.text));
            if (upper(firstToken(code)) == "@@EXPORT")
                for (const auto &n : splitTopLevel(restAfterFirst(code), ' '))
                    if (!n.empty()) exported[n] = true;
        }
        int moduleDepth = 0;
        for (const auto &l : body) {
            std::string kw = classify(l.text);
            if (skipNestedModule) {
                if (kw == "MODULE") { ++moduleDepth; continue; }
                if (kw == "ENDMODULE") { --moduleDepth; continue; }
                if (moduleDepth > 0) continue;
            }
            std::string code = trim(stripComment(l.text));
            std::string label, rest; peelLabel(code, label, rest);
            if (!label.empty() && !exported.count(label)) {
                bool seen = false; for (auto &x : locals) if (x == label) seen = true;
                if (!seen) locals.push_back(label);
            }
        }
        return locals;
    }
    // Applique un renommage (def+réfs) aux labels donnés, retire les @@export.
    std::vector<SrcLine> renameScope(const std::vector<SrcLine> &body,
                                     const std::vector<std::string> &names,
                                     const std::function<std::string(const std::string &)> &mangle) {
        std::vector<SrcLine> out;
        for (const auto &l : body) {
            if (upper(firstToken(trim(stripComment(l.text)))) == "@@EXPORT") continue;
            std::string t = l.text;
            for (const auto &name : names) t = replaceWord(t, name, mangle(name));
            out.push_back({t, l.file, l.line});
        }
        return out;
    }
    // Renommage auto-local (macros, itérations REPEAT/WHILE) : suffixe __id.
    std::vector<SrcLine> renameLocals(const std::vector<SrcLine> &body, long id) {
        auto names = collectLabels(body, false);
        std::string suf = "__" + std::to_string(id);
        return renameScope(body, names, [&](const std::string &n) { return n + suf; });
    }

    // Trouve la ligne de fermeture correspondante (imbrication même type).
    int findMatching(const std::vector<SrcLine> &lines, int start,
                     const std::vector<std::string> &open, const std::vector<std::string> &close) {
        auto in = [](const std::vector<std::string> &v, const std::string &k) {
            for (auto &x : v) { if (x == k) return true; }
            return false;
        };
        int depth = 1;
        for (int i = start + 1; i < (int)lines.size(); ++i) {
            std::string kw = classify(lines[i].text);
            if (in(open, kw)) ++depth;
            else if (in(close, kw)) { if (--depth == 0) return i; }
        }
        return -1;
    }

    void expandMacro(const Macro &m, const std::string &argstr,
                     const SrcLine &sl, int depth) {
        if (depth > 200) { error(sl, "profondeur de macro excessive (récursion ?)"); return; }
        std::vector<std::string> args = splitTopLevel(argstr, ',');
        if (args.size() != m.params.size()) {
            error(sl, "macro " + m.name + " : " + std::to_string(m.params.size()) +
                          " paramètre(s) attendu(s), " + std::to_string(args.size()) + " fourni(s)");
            return;
        }
        Env ne;
        for (size_t k = 0; k < m.params.size(); ++k) ne.args[m.params[k]] = args[k];
        std::vector<SrcLine> scoped = renameLocals(m.body, ++uid);
        run(scoped, ne, depth + 1);
    }

    // Analyse un champ de structure : "[nom] directive operandes" ou "nom StructType".
    StructField parseField(const std::string &code) {
        StructField f;
        std::string label, rest; peelLabel(code, label, rest);
        if (!label.empty()) {
            f.name = label; f.directive = upper(firstToken(rest)); f.operands = restAfterFirst(rest);
        } else {
            std::string w0 = firstToken(rest);
            std::string U1 = upper(firstToken(restAfterFirst(rest)));
            if (isDataDir(U1) || structs_.count(U1)) {           // "nom directive ops" sans ':'
                f.name = w0; f.directive = U1; f.operands = restAfterFirst(restAfterFirst(rest));
            } else {                                              // champ anonyme "directive ops"
                f.directive = upper(w0); f.operands = restAfterFirst(rest);
            }
        }
        return f;
    }

    void defineStruct(const std::string &sname, const std::vector<SrcLine> &body,
                      const Env &env, const SrcLine &raw) {
        if (structs_.count(upper(sname))) { error(raw, "STRUCT redéfinie : " + sname); return; }
        StructDef def; def.name = sname; int off = 0;
        for (const auto &l : body) {
            std::string code = trim(substitute(stripComment(l.text), env, l));
            if (code.empty()) continue;
            StructField f = parseField(code);
            const std::string &U = f.directive;
            if (isDataDir(U)) {
                auto ops = splitTopLevel(f.operands, ',');
                if (U == "DB" || U == "DEFB" || U == "DM" || U == "DEFM") {
                    int n = 0; for (auto &p : ops) { if (!p.empty() && p[0] == '"') n += stringByteLen(p); else if (!p.empty()) n += 1; }
                    f.size = n;
                } else if (U == "DW" || U == "DEFW") {
                    int n = 0; for (auto &p : ops) if (!p.empty()) ++n;
                    f.size = 2 * n;
                } else { // DS / DEFS / RMB
                    auto r = ops.empty() ? expr::Result{} : evalPP(ops[0], env);
                    if (!r.ok) { error(l, "STRUCT : taille de champ DS non résoluble"); f.size = 0; }
                    else f.size = (int)r.value;
                }
            } else if (structs_.count(U)) {
                f.nested = true; f.size = structs_[U].size;
            } else {
                error(l, "STRUCT : directive de champ inconnue '" + f.directive + "'");
                continue;
            }
            f.offset = off; off += f.size;
            def.fields.push_back(f);
        }
        def.size = off;
        structs_[upper(sname)] = def;
        // émission des symboles portables (offsets + sizeof)
        for (const auto &f : def.fields) {
            if (f.name.empty()) continue;
            emit(sname + "." + f.name + " EQU " + std::to_string(f.offset), raw);
            if (f.nested) {
                const StructDef &inner = structs_[f.directive];
                for (const auto &g : inner.fields)
                    if (!g.name.empty())
                        emit(sname + "." + f.name + "." + g.name + " EQU " + std::to_string(f.offset + g.offset), raw);
            }
        }
        emit(sname + " EQU " + std::to_string(def.size), raw);
    }

    void instantiateStruct(const std::string &rest, const SrcLine &raw) {
        std::string aft = restAfterFirst(rest);        // "type instance ov..."
        std::string type = firstToken(aft);
        std::string aft2 = restAfterFirst(aft);         // "instance ov..."
        std::string instance = firstToken(aft2);
        std::string overText = restAfterFirst(aft2);
        auto it = structs_.find(upper(type));
        if (it == structs_.end()) { error(raw, "STRUCT inconnue à instancier : " + type); return; }
        if (instance.empty()) { error(raw, "instanciation STRUCT sans nom"); return; }
        const StructDef &d = it->second;
        auto overrides = splitTopLevel(overText, ',');
        emit(instance + ":", raw);
        for (const auto &f : d.fields) {
            if (f.name.empty()) continue;
            emit(instance + "." + f.name + " EQU " + instance + "+" + std::to_string(f.offset), raw);
            if (f.nested) {
                const StructDef &inner = structs_[f.directive];
                for (const auto &g : inner.fields)
                    if (!g.name.empty())
                        emit(instance + "." + f.name + "." + g.name + " EQU " + instance + "+" +
                             std::to_string(f.offset + g.offset), raw);
            }
        }
        for (size_t fi = 0; fi < d.fields.size(); ++fi) {
            const StructField &f = d.fields[fi];
            if (f.nested) { emit("ds " + std::to_string(f.size), raw); continue; }
            std::string val = f.operands;
            bool isDS = (f.directive == "DS" || f.directive == "DEFS" || f.directive == "RMB");
            if (!isDS && fi < overrides.size() && !overrides[fi].empty()) val = overrides[fi];
            emit(f.directive + (val.empty() ? "" : " " + val), raw);
        }
    }

    void run(const std::vector<SrcLine> &lines, const Env &env, int depth) {
        int i = 0;
        while (i < (int)lines.size()) {
            const SrcLine &raw = lines[i];
            std::string sub = substitute(stripComment(raw.text), env, raw);
            std::string code = trim(sub);
            if (code.empty()) { ++i; continue; }

            std::string label, rest; peelLabel(code, label, rest);
            std::string kw = upper(firstToken(rest));
            std::string secondUp = upper(firstToken(restAfterFirst(rest)));

            // --- INCLUDE ---
            if (kw == "INCLUDE") {
                if (!label.empty()) emit(label + ":", raw);
                auto parts = splitTopLevel(restAfterFirst(rest), ',');
                std::string path = parts.empty() ? "" : parts[0];
                if (path.size() >= 2 && (path[0] == '"' || path[0] == '\'')) path = path.substr(1, path.size() - 2);
                std::string content;
                if (!files || !files(path, content)) error(raw, "include introuvable : " + path);
                else run(splitLines(content, path), env, depth + 1);
                ++i; continue;
            }

            // --- LET (variable PP) ---
            if (kw == "LET") {
                std::string a = restAfterFirst(rest); // "name = expr", "name=expr" ou "name expr"
                std::string name, ev;
                size_t eq = a.find('=');
                if (eq != std::string::npos) { name = trim(a.substr(0, eq)); ev = trim(a.substr(eq + 1)); }
                else { name = firstToken(a); ev = restAfterFirst(a); }
                auto r = evalPP(ev, env);
                if (!isIdentifier(name)) error(raw, "LET : nom de variable invalide");
                else if (!r.ok) error(raw, r.error);
                else ppvars[name] = r.value;
                ++i; continue;
            }

            // --- IF / IFDEF / IFNDEF ---
            if (kw == "IF" || kw == "IFDEF" || kw == "IFNDEF") {
                int endif = findMatching(lines, i, {"IF", "IFDEF", "IFNDEF"}, {"ENDIF"});
                if (endif < 0) { error(raw, "IF sans ENDIF"); return; }
                // bornes des branches (profondeur 0)
                std::vector<int> bounds = {i};
                int d = 1;
                for (int j = i + 1; j < endif; ++j) {
                    std::string k = classify(lines[j].text);
                    if (k == "IF" || k == "IFDEF" || k == "IFNDEF") ++d;
                    else if (k == "ENDIF") --d;
                    else if (d == 1 && (k == "ELSE" || k == "ELSEIF")) bounds.push_back(j);
                }
                bounds.push_back(endif);
                bool taken = false;
                for (size_t b = 0; b + 1 < bounds.size() && !taken; ++b) {
                    const SrcLine &hdr = lines[bounds[b]];
                    std::string hl, hr; peelLabel(trim(stripComment(hdr.text)), hl, hr);
                    std::string hkw = upper(firstToken(hr));
                    std::string operand = substitute(restAfterFirst(hr), env, hdr);
                    bool cond;
                    if (hkw == "ELSE") cond = true;
                    else if (hkw == "IFDEF") cond = isDefined(trim(operand), env);
                    else if (hkw == "IFNDEF") cond = !isDefined(trim(operand), env);
                    else { auto r = evalPP(operand, env); if (!r.ok) { error(hdr, r.error); cond = false; } else cond = (r.value != 0); }
                    if (cond) {
                        std::vector<SrcLine> branch(lines.begin() + bounds[b] + 1, lines.begin() + bounds[b + 1]);
                        run(branch, env, depth);
                        taken = true;
                    }
                }
                i = endif + 1; continue;
            }

            // --- REPEAT count[,var] ... REND ---
            if (kw == "REPEAT") {
                int rend = findMatching(lines, i, {"REPEAT"}, {"REND"});
                if (rend < 0) { error(raw, "REPEAT sans REND"); return; }
                if (!label.empty()) emit(label + ":", raw);
                auto parts = splitTopLevel(restAfterFirst(rest), ',');
                auto r = parts.empty() ? expr::Result{} : evalPP(parts[0], env);
                std::string var = parts.size() > 1 ? trim(parts[1]) : "";
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + rend);
                if (!r.ok) error(raw, "REPEAT : " + (parts.empty() ? "compteur manquant" : r.error));
                else if (r.value < 0 || r.value > 1000000) error(raw, "REPEAT : compteur hors limites");
                else for (int k = 0; k < r.value; ++k) {
                    Env ne = env; if (!var.empty()) ne.locals[var] = k;
                    run(renameLocals(body, ++uid), ne, depth);
                }
                i = rend + 1; continue;
            }

            // --- WHILE expr ... WEND ---
            if (kw == "WHILE") {
                int wend = findMatching(lines, i, {"WHILE"}, {"WEND"});
                if (wend < 0) { error(raw, "WHILE sans WEND"); return; }
                if (!label.empty()) emit(label + ":", raw);
                std::string condRaw = restAfterFirst(rest);
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + wend);
                long guard = 0;
                for (;;) {
                    auto r = evalPP(substitute(condRaw, env, raw), env);
                    if (!r.ok) { error(raw, "WHILE : " + r.error); break; }
                    if (r.value == 0) break;
                    if (++guard > 1000000) { error(raw, "WHILE : trop d'itérations"); break; }
                    run(renameLocals(body, ++uid), env, depth);
                }
                i = wend + 1; continue;
            }

            // --- définition de macro : "MACRO name p.." ou "name MACRO p.." ---
            if (kw == "MACRO" || secondUp == "MACRO") {
                int endm = findMatching(lines, i, {"MACRO"}, {"ENDM", "MEND"});
                if (endm < 0) { error(raw, "MACRO sans ENDM"); return; }
                Macro m;
                std::string decl;
                if (kw == "MACRO") { m.name = firstToken(restAfterFirst(rest)); decl = restAfterFirst(restAfterFirst(rest)); }
                else { m.name = firstToken(rest); decl = restAfterFirst(restAfterFirst(rest)); }
                for (auto &p : splitTopLevel(decl, ',')) if (!p.empty()) m.params.push_back(p);
                for (int j = i + 1; j < endm; ++j) m.body.push_back(lines[j]);
                if (m.name.empty()) error(raw, "MACRO sans nom");
                else macros[upper(m.name)] = m;
                i = endm + 1; continue;
            }

            // --- MODULE name ... ENDMODULE (scope par préfixe) ---
            if (kw == "MODULE") {
                int endm = findMatching(lines, i, {"MODULE"}, {"ENDMODULE"});
                if (endm < 0) { error(raw, "MODULE sans ENDMODULE"); return; }
                std::string mname = firstToken(restAfterFirst(rest));
                std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + endm);
                std::string prefix = env.modulePrefix + mname + "_";
                auto names = collectLabels(body, /*skipNestedModule=*/true);
                auto scoped = renameScope(body, names, [&](const std::string &n) { return prefix + n; });
                Env ne = env; ne.modulePrefix = prefix;
                run(scoped, ne, depth);
                i = endm + 1; continue;
            }

            // --- STRUCT : déclaration (1 arg) ou instanciation (2+ args) ---
            if (kw == "STRUCT") {
                if (countTokens(restAfterFirst(rest)) == 1) {
                    int ends = findMatching(lines, i, {"STRUCT"}, {"ENDSTRUCT", "ENDS"});
                    if (ends < 0) { error(raw, "STRUCT sans ENDSTRUCT"); return; }
                    std::string sname = firstToken(restAfterFirst(rest));
                    std::vector<SrcLine> body(lines.begin() + i + 1, lines.begin() + ends);
                    defineStruct(sname, body, env, raw);
                    i = ends + 1; continue;
                }
                instantiateStruct(rest, raw);
                ++i; continue;
            }

            // --- fermetures orphelines (déjà consommées par les blocs) ---
            if (kw == "ENDMODULE" || kw == "ENDSTRUCT" || kw == "ENDS") { ++i; continue; }

            // --- @@export hors macro : ignorer ---
            if (kw == "@@EXPORT") { ++i; continue; }

            // --- appel de macro ---
            auto mit = macros.find(upper(firstToken(rest)));
            if (mit != macros.end()) {
                if (!label.empty()) emit(label + ":", raw);
                expandMacro(mit->second, restAfterFirst(rest), raw, depth);
                ++i; continue;
            }

            // --- ligne ordinaire : passe-plat ---
            emit(code, raw);
            ++i;
        }
    }
};

} // namespace

std::string Result::dump() const {
    std::string s;
    for (const auto &l : lines) { s += l.text; s += '\n'; }
    return s;
}

Result preprocess(const std::string &mainContent, const std::string &mainFile,
                  const FileProvider &files) {
    PP pp(files);
    pp.runFile(mainContent, mainFile);
    return pp.result;
}

} // namespace pp
