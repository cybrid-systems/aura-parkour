#!/usr/bin/env bash
# Soft M0 smoke. Image ghcr.io/cybrid-systems/dev:v1.0.9, tip binary only.
# Never build_soft4132. set-code seed needs AURA_SANDBOX=off.
set -euo pipefail
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
AURA_SRC="${AURA_SRC:-/workspace/aura-grok}"
IMG="${AURA_DEV_IMAGE:-ghcr.io/cybrid-systems/dev:v1.0.9}"
mkdir -p "$ROOT/build"
if docker info >/dev/null 2>&1; then
  DOCKER=(docker)
else
  DOCKER=(sudo docker)
fi
exec "${DOCKER[@]}" run --rm \
  -v "$AURA_SRC":/workspace/aura-grok \
  -v "$ROOT":/workspace/aura-parkour \
  -w /workspace/aura-parkour \
  -e AURA_PATH=/workspace/aura-grok/lib \
  -e AURA_PIPELINE_STRICT=0 \
  -e AURA_SANDBOX=off \
  -e PARKOUR_M0=1 \
  -e AURA_BIN=/workspace/aura-grok/build/aura \
  "$IMG" \
  /workspace/aura-grok/build/aura /workspace/aura-parkour/soft/parkour/m0_smoke.aura
