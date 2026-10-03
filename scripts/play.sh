#!/usr/bin/env bash
# One command to play aura-parkour (Soft world + viewport).
# A raylib window is used when it was built and a display exists
# (macOS Cocoa, or DISPLAY/WAYLAND). Otherwise the ANSI corridor.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
echo "aura-parkour: building viewport..."
if [[ ! -s "$ROOT/c/play.c" ]]; then
  python3 "$ROOT/scripts/expand_playc.py"
fi
# Mac and the angel bind-mount must not share one CMake cache.
if [[ "$(uname -s)" == "Darwin" ]]; then
  BUILD="$ROOT/build/mac"
else
  BUILD="$ROOT/build/c"
fi
CMAKE_ARGS=()
if command -v brew >/dev/null 2>&1; then
  CMAKE_ARGS+=(-DCMAKE_PREFIX_PATH="$(brew --prefix)")
fi
cmake -S "$ROOT/c" -B "$BUILD" "${CMAKE_ARGS[@]}" >/dev/null
cmake --build "$BUILD" --parallel >/dev/null
echo "  controls: Space/w jump | s slide | a/d lane | p pause | r restart | q quit"
echo "  Soft seed may take ~30-60s on first start."
VIEW="$BUILD/parkour_view"
use_window=0
if [[ -x "$VIEW" ]]; then
  if [[ "$(uname -s)" == "Darwin" || -n "${DISPLAY:-}" || -n "${WAYLAND_DISPLAY:-}" ]]; then
    use_window=1
  fi
fi
# Always launch Soft through bash: bind mounts / ACL can strip +x on soft_play.sh.
if [[ "$use_window" == 1 ]]; then
  echo "aura-parkour: windowed 3D corridor (raylib). Soft still owns the world."
  exec "$VIEW" -- /bin/bash "$ROOT/scripts/soft_play.sh"
fi
echo "aura-parkour: no window (headless or no raylib); ANSI corridor."
exec "$BUILD/parkour_play" -- /bin/bash "$ROOT/scripts/soft_play.sh"
