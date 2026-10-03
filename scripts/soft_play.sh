#!/usr/bin/env bash
# Soft play child for parkour_play. Tip Soft binary + required host image.
# stdin: INPUT lines from C. stdout: SNAP blocks (line-buffered).
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AURA_SRC="${AURA_SRC:-/workspace/aura-grok}"
IMG="ghcr.io/cybrid-systems/dev:v1.0.9"
if docker info >/dev/null 2>&1; then
  DOCKER=(docker)
elif sudo docker info >/dev/null 2>&1; then
  DOCKER=(sudo docker)
else
  echo "soft_play: docker not available" >&2
  exit 1
fi
exec "${DOCKER[@]}" run --rm -i \
  -v "$AURA_SRC":/workspace/aura-grok \
  -v "$ROOT":/workspace/aura-parkour \
  -w /workspace/aura-parkour \
  -e AURA_PATH=/workspace/aura-grok/lib \
  -e AURA_PIPELINE_STRICT=0 \
  -e AURA_SANDBOX=off \
  -e AURA_BIN=/workspace/aura-grok/build/aura \
  "$IMG" \
  stdbuf -oL -eL /workspace/aura-grok/build/aura /workspace/aura-parkour/soft/parkour/play.aura
