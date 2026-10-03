#!/usr/bin/env bash
# One command to play aura-parkour (Soft world + C 3D viewport).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
mkdir -p "$ROOT/build"
echo "aura-parkour: building viewport..."
python3 "$ROOT/scripts/expand_playc.py"
cmake -S "$ROOT/c" -B "$ROOT/build/c" >/dev/null
cmake --build "$ROOT/build/c" --parallel >/dev/null
echo "aura-parkour: starting Soft-backed 3D corridor..."
echo "  controls: Space/w jump | s slide | a/d lane | p pause | r restart | q quit"
echo "  Soft seed may take ~30-60s on first start."
exec "$ROOT/build/c/parkour_play" -- "$ROOT/scripts/soft_play.sh"
