#!/usr/bin/env bash
# build-wasm.sh — compile fantams (CLI bout-en-bout) vers WASM.
#
# Produit un module ES6 isomorphe (Node + navigateur) au même format que
# wasm/rasm.mjs : factory `export default createFantams`, `callMain` + `FS`
# exposés, pas d'exécution auto. La sortie est copiée dans ../wasm/.
#
# emcc n'étant pas requis en local, on passe par l'image officielle
# emscripten/emsdk sous podman (ou docker). Override : CONTAINER=docker.
#
#   ./build-wasm.sh           # ne recompile que si une source a bougé
#   ./build-wasm.sh --force   # recompile inconditionnellement
set -euo pipefail

FORCE=0
[ "${1:-}" = "--force" ] && FORCE=1

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
# Le podman écrit ses artefacts dans $HERE (volume monté) et le `mv` final les y
# reprend : le script doit donc s'y tenir, quel que soit l'endroit d'où on l'appelle
# — `npm run build` le lance depuis la racine.
cd "$HERE"
OUT_DIR="$HERE/../wasm"
CONTAINER="${CONTAINER:-podman}"
IMAGE="${IMAGE:-docker.io/emscripten/emsdk:latest}"

CORE=(z80.cpp expr.cpp keywords.cpp parser.cpp pp.cpp asm.cpp beautify.cpp sna.cpp sym.cpp asm_main.cpp)

# Note pile 8 Mo : le parseur récursif de fantams déborde la pile Emscripten
# par défaut (64 Ko) sur les grosses sources -> trap "table index out of bounds".
EMFLAGS=(
  -std=c++17 -O2
  # expr::eval s'appuie sur try/catch (throw EvalError). Sans -fexceptions,
  # Emscripten transforme tout throw en abort() -> "Aborted(undefined)".
  -fexceptions
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createFantams
  -sEXPORTED_RUNTIME_METHODS=callMain,FS
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608
  -sFORCE_FILESYSTEM=1
  -o fantams.mjs
)

PUB_DIR="$HERE/../app/public/wasm"

# Test de fraîcheur. Le .wasm est un artefact compilé qui vit dans l'arbre, à côté
# de sources qu'on édite tous les jours : rien dans git ne signale qu'il est en
# retard sur elles, et un .wasm périmé ne se manifeste que par des bugs déjà
# corrigés — le pire des symptômes, puisqu'il accuse le code plutôt que le build.
# On rend donc l'appel systématique bon marché, pour qu'il n'y ait jamais de
# raison de le sauter : `npm run build` l'invoque toujours, et il ne coûte le
# conteneur que si une source l'exige.
#
# La copie publiée compte comme une source de vérité : c'est elle que le
# navigateur charge, et un `git checkout` peut la désynchroniser sans toucher
# aux .cpp.
is_stale() {
  [ "$FORCE" = 1 ] && { echo "--force"; return 0; }
  local w="$OUT_DIR/fantams.wasm"
  [ -f "$w" ] || { echo "$w absent"; return 0; }
  [ -f "$OUT_DIR/fantams.mjs" ] || { echo "$OUT_DIR/fantams.mjs absent"; return 0; }
  local f
  for f in "${CORE[@]}" "$HERE"/*.h; do
    [ -e "$f" ] || continue
    [ "$HERE/$(basename "$f")" -nt "$w" ] && { echo "$(basename "$f") plus récent que le .wasm"; return 0; }
  done
  if [ -d "$PUB_DIR" ]; then
    cmp -s "$w" "$PUB_DIR/fantams.wasm" || { echo "app/public/wasm désynchronisé"; return 0; }
  fi
  return 1
}

if ! reason="$(is_stale)"; then
  echo ">> WASM à jour, rien à recompiler (--force pour l'imposer)"
  exit 0
fi
echo ">> rebuild nécessaire : $reason"

echo ">> compilation WASM via $CONTAINER ($IMAGE)"
"$CONTAINER" run --rm -v "$HERE":/src:z -w /src "$IMAGE" \
  em++ "${EMFLAGS[@]}" "${CORE[@]}"

mkdir -p "$OUT_DIR"
mv -f fantams.mjs fantams.wasm "$OUT_DIR/"
echo ">> écrit : $OUT_DIR/fantams.mjs + fantams.wasm"
ls -l "$OUT_DIR/fantams.mjs" "$OUT_DIR/fantams.wasm"

# Les factories WASM sont chargées à l'exécution depuis /wasm (servi par app/public/wasm
# en dev/build). On y recopie les artefacts + assemble.mjs pour éviter la dérive.
if [ -d "$PUB_DIR" ]; then
  cp -f "$OUT_DIR/fantams.mjs" "$OUT_DIR/fantams.wasm" "$OUT_DIR/assemble.mjs" "$PUB_DIR/"
  echo ">> synchronisé -> $PUB_DIR/ (fantams.mjs, fantams.wasm, assemble.mjs)"
fi
