#!/usr/bin/env bash
# build-wasm.sh — compile rasm-lite (CLI bout-en-bout) vers WASM.
#
# Produit un module ES6 isomorphe (Node + navigateur) au même format que
# wasm/rasm.mjs : factory `export default createRasmLite`, `callMain` + `FS`
# exposés, pas d'exécution auto. La sortie est copiée dans ../wasm/.
#
# emcc n'étant pas requis en local, on passe par l'image officielle
# emscripten/emsdk sous podman (ou docker). Override : CONTAINER=docker.
#
#   ./build-wasm.sh
set -euo pipefail

HERE="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
OUT_DIR="$HERE/../wasm"
CONTAINER="${CONTAINER:-podman}"
IMAGE="${IMAGE:-docker.io/emscripten/emsdk:latest}"

CORE=(z80.cpp expr.cpp parser.cpp pp.cpp asm.cpp sna.cpp asm_main.cpp)

# Note pile 8 Mo : le parseur récursif de rasm-lite déborde la pile Emscripten
# par défaut (64 Ko) sur les grosses sources -> trap "table index out of bounds".
EMFLAGS=(
  -std=c++17 -O2
  # expr::eval s'appuie sur try/catch (throw EvalError). Sans -fexceptions,
  # Emscripten transforme tout throw en abort() -> "Aborted(undefined)".
  -fexceptions
  -sMODULARIZE=1 -sEXPORT_ES6=1 -sEXPORT_NAME=createRasmLite
  -sEXPORTED_RUNTIME_METHODS=callMain,FS
  -sINVOKE_RUN=0 -sEXIT_RUNTIME=0
  -sALLOW_MEMORY_GROWTH=1 -sSTACK_SIZE=8388608
  -sFORCE_FILESYSTEM=1
  -o rasmlite.mjs
)

echo ">> compilation WASM via $CONTAINER ($IMAGE)"
"$CONTAINER" run --rm -v "$HERE":/src:z -w /src "$IMAGE" \
  em++ "${EMFLAGS[@]}" "${CORE[@]}"

mkdir -p "$OUT_DIR"
mv -f rasmlite.mjs rasmlite.wasm "$OUT_DIR/"
echo ">> écrit : $OUT_DIR/rasmlite.mjs + rasmlite.wasm"
ls -l "$OUT_DIR/rasmlite.mjs" "$OUT_DIR/rasmlite.wasm"

# Les factories WASM sont chargées à l'exécution depuis /wasm (servi par app/public/wasm
# en dev/build). On y recopie les artefacts + assemble.mjs pour éviter la dérive.
PUB_DIR="$HERE/../app/public/wasm"
if [ -d "$PUB_DIR" ]; then
  cp -f "$OUT_DIR/rasmlite.mjs" "$OUT_DIR/rasmlite.wasm" "$OUT_DIR/assemble.mjs" "$PUB_DIR/"
  echo ">> synchronisé -> $PUB_DIR/ (rasmlite.mjs, rasmlite.wasm, assemble.mjs)"
fi
