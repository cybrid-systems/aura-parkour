#!/usr/bin/env bash
# Soft writes build/m0.snap, then the thin C viewport blits it once.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
bash "$ROOT/scripts/smoke_soft.sh"
cmake -S "$ROOT/c" -B "$ROOT/build/c" >/dev/null
cmake --build "$ROOT/build/c" --parallel >/dev/null
"$ROOT/build/c/parkour_blit" "$ROOT/build/m0.snap"
