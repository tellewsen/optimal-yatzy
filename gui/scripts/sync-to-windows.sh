#!/usr/bin/env bash
# gui/scripts/sync-to-windows.sh — copies gui/ to a native Windows
# build-staging folder. Windows can't execute a binary sitting on the WSL
# UNC path, so `tauri dev`/`build` must run against a native copy. This is
# a one-way disposable sync, not a git checkout: node_modules/target/dist
# are excluded and regenerated fresh on the Windows side.
set -euo pipefail

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
GUI_DIR="$(dirname "$SCRIPT_DIR")"
WIN_DEST="/mnt/c/Users/AndreasEllewsen/yatzy-gui-build"

mkdir -p "$WIN_DEST"
rsync -a --delete \
  --exclude node_modules \
  --exclude dist \
  --exclude target \
  "$GUI_DIR/" "$WIN_DEST/"

echo "synced gui/ -> $WIN_DEST (Windows path: C:\\Users\\AndreasEllewsen\\yatzy-gui-build)"
