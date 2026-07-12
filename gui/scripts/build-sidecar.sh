#!/usr/bin/env bash
# gui/scripts/build-sidecar.sh — cross-compiles the existing yatzy_cpu engine
# (unmodified C++ sources from the repo root) into a Windows binary, named
# per Tauri's sidecar convention: <name>-<host-target-triple>.exe. The
# target-triple suffix must match whatever `rustc --print host-tuple`
# reports on the machine that runs `tauri dev`/`build` — currently
# x86_64-pc-windows-msvc — even though this binary itself is built with an
# unrelated toolchain (MinGW-w64); Tauri only cares about the filename.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
REPO_ROOT="$(dirname "$GUI_DIR")"
OUT_DIR="$GUI_DIR/src-tauri/binaries"
TARGET_TRIPLE="x86_64-pc-windows-msvc"

mkdir -p "$OUT_DIR"

x86_64-w64-mingw32-g++-posix -O3 -std=c++17 -static \
  "$REPO_ROOT/yatzy_engine.cpp" "$REPO_ROOT/yatzy_cpu.cpp" \
  -o "$OUT_DIR/yatzy_cpu-$TARGET_TRIPLE.exe"

echo "built $OUT_DIR/yatzy_cpu-$TARGET_TRIPLE.exe"
