#!/usr/bin/env bash
# gui/scripts/build-wasm.sh — compiles the standard-Yatzy engine's query
# path to WebAssembly for the browser (Solo mode only). Requires a
# pre-solved yatzy_cpu_dp.bin at the repo root (run ./yatzy_cpu once first)
# and emcc (Emscripten) on PATH. Bakes the DP table into the module via
# --preload-file so no client-side solve or threading is ever needed.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$GUI_DIR")"
DP_CACHE="$REPO_ROOT/yatzy_cpu_dp.bin"
OUT_DIR="$GUI_DIR/src/wasm"

if [ ! -f "$DP_CACHE" ]; then
  echo "error: $DP_CACHE not found — run ./yatzy_cpu once first to solve+cache it" >&2
  exit 1
fi

mkdir -p "$OUT_DIR"

emcc -O3 -std=c++17 --bind \
  "$REPO_ROOT/yatzy_engine.cpp" "$REPO_ROOT/wasm_bridge.cpp" \
  --preload-file "$DP_CACHE@/yatzy_cpu_dp.bin" \
  -s MODULARIZE=1 \
  -s EXPORT_ES6=1 \
  -s ENVIRONMENT=web \
  -s ALLOW_MEMORY_GROWTH=1 \
  -s EXPORT_NAME=createYatzyModule \
  -o "$OUT_DIR/yatzy_engine.js"

echo "built $OUT_DIR/yatzy_engine.js (+ .wasm, .data)"
